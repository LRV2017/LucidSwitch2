#include "lucid/gpu/gpu_address_space.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <shared_mutex>

namespace lucid::gpu {
namespace {
void set_error(std::string* error,const char* message){if(error!=nullptr)*error=message;}
bool has_permission(const core::MemoryPermission available,
                    const core::MemoryPermission requested) noexcept {
    return (static_cast<std::uint8_t>(available)&
            static_cast<std::uint8_t>(requested))==
           static_cast<std::uint8_t>(requested);
}
bool valid_permission(const core::MemoryPermission permissions) noexcept {
    const auto bits=static_cast<std::uint8_t>(permissions);
    return bits!=0U&&(bits&~0x7U)==0U;
}
bool ranges_overlap(std::uint64_t a,std::uint64_t as,
                    std::uint64_t b,std::uint64_t bs) noexcept {
    return a<b+bs&&b<a+as;
}
}

GpuAddressSpace::GpuAddressSpace(core::MemoryBus& physical_memory) noexcept
    : physical_memory_(physical_memory) {}

bool GpuAddressSpace::map(const std::uint64_t gpu_address,
    const std::uint64_t physical_address,const std::size_t size,
    const core::MemoryPermission permissions,std::string* error) {
    const auto size64=static_cast<std::uint64_t>(size);
    if(size==0U||(gpu_address%PageSize)!=0U||(physical_address%PageSize)!=0U||
       (size64%PageSize)!=0U||gpu_address>=MaximumGpuVirtualAddress||
       size64>MaximumGpuVirtualAddress-gpu_address||
       size64>std::numeric_limits<std::uint64_t>::max()-physical_address||
       !valid_permission(permissions)){
        set_error(error,"GPU mappings require nonempty page-aligned ranges, valid permissions, and bounded addresses.");
        return false;
    }
    if((has_permission(permissions,core::MemoryPermission::Read)&&
        !physical_memory_.can_access(physical_address,size,core::MemoryPermission::Read))||
       (has_permission(permissions,core::MemoryPermission::Write)&&
        !physical_memory_.can_access(physical_address,size,core::MemoryPermission::Write))||
       (has_permission(permissions,core::MemoryPermission::Execute)&&
        !physical_memory_.can_access(physical_address,size,core::MemoryPermission::Execute))){
        set_error(error,"GPU mapping exceeds physical memory or requests unavailable backing permissions.");
        return false;
    }
    std::unique_lock lock(mutex_);
    for(const auto& mapping:mappings_){
        if(ranges_overlap(gpu_address,size64,mapping.gpu_base,
                          static_cast<std::uint64_t>(mapping.size))){
            set_error(error,"GPU virtual mapping overlaps an existing mapping.");
            return false;
        }
    }
    mappings_.push_back({gpu_address,physical_address,size,permissions});
    std::sort(mappings_.begin(),mappings_.end(),[](const Mapping& a,const Mapping& b){
        return a.gpu_base<b.gpu_base;
    });
    return true;
}

bool GpuAddressSpace::unmap(const std::uint64_t gpu_address,
    const std::size_t size,std::string* error){
    std::unique_lock lock(mutex_);
    const auto it=std::find_if(mappings_.begin(),mappings_.end(),
        [gpu_address,size](const Mapping& m){return m.gpu_base==gpu_address&&m.size==size;});
    if(it==mappings_.end()){set_error(error,"Exact GPU mapping was not found.");return false;}
    mappings_.erase(it);return true;
}

bool GpuAddressSpace::protect(const std::uint64_t gpu_address,
    const std::size_t size,const core::MemoryPermission permissions,
    std::string* error){
    if(!valid_permission(permissions)){set_error(error,"GPU mapping permissions are invalid.");return false;}
    std::unique_lock lock(mutex_);
    for(auto& mapping:mappings_){
        if(mapping.gpu_base!=gpu_address||mapping.size!=size)continue;
        if((has_permission(permissions,core::MemoryPermission::Read)&&
            !physical_memory_.can_access(mapping.physical_base,mapping.size,core::MemoryPermission::Read))||
           (has_permission(permissions,core::MemoryPermission::Write)&&
            !physical_memory_.can_access(mapping.physical_base,mapping.size,core::MemoryPermission::Write))||
           (has_permission(permissions,core::MemoryPermission::Execute)&&
            !physical_memory_.can_access(mapping.physical_base,mapping.size,core::MemoryPermission::Execute))){
            set_error(error,"The physical backing does not permit the requested GPU mapping access.");return false;
        }
        mapping.permissions=permissions;return true;
    }
    set_error(error,"Exact GPU mapping was not found.");return false;
}

const GpuAddressSpace::Mapping* GpuAddressSpace::find_mapping(
    const std::uint64_t address) const noexcept {
    const auto it=std::upper_bound(mappings_.begin(),mappings_.end(),address,
        [](std::uint64_t value,const Mapping& mapping){return value<mapping.gpu_base;});
    if(it==mappings_.begin())return nullptr;
    const auto& candidate=*std::prev(it);
    return address>=candidate.gpu_base&&
        address-candidate.gpu_base<static_cast<std::uint64_t>(candidate.size)
        ? &candidate:nullptr;
}

bool GpuAddressSpace::validate_range(const std::uint64_t address,
    const std::size_t size,const core::MemoryPermission required) const {
    if(size==0U)return true;
    if(address>=MaximumGpuVirtualAddress||
       static_cast<std::uint64_t>(size)>MaximumGpuVirtualAddress-address)return false;
    std::uint64_t cursor=address;
    std::size_t remaining=size;
    while(remaining!=0U){
        const Mapping* mapping=find_mapping(cursor);
        if(mapping==nullptr||!has_permission(mapping->permissions,required))return false;
        const std::uint64_t offset=cursor-mapping->gpu_base;
        const std::size_t available=mapping->size-static_cast<std::size_t>(offset);
        const std::size_t chunk=std::min(remaining,available);
        const std::uint64_t physical=mapping->physical_base+offset;
        if(!physical_memory_.can_access(physical,chunk,required))return false;
        cursor+=static_cast<std::uint64_t>(chunk);remaining-=chunk;
    }
    return true;
}

bool GpuAddressSpace::valid() const {
    std::shared_lock lock(mutex_);return !mappings_.empty();
}

bool GpuAddressSpace::read(const std::uint64_t address,
    const std::span<std::uint8_t> output) const {
    std::shared_lock lock(mutex_);
    if(!validate_range(address,output.size(),core::MemoryPermission::Read))return false;
    std::uint64_t cursor=address;std::size_t remaining=output.size();std::size_t written=0U;
    while(remaining!=0U){
        const Mapping* mapping=find_mapping(cursor);
        const std::uint64_t offset=cursor-mapping->gpu_base;
        const std::size_t chunk=std::min(remaining,mapping->size-static_cast<std::size_t>(offset));
        if(!physical_memory_.read(mapping->physical_base+offset,output.subspan(written,chunk)))return false;
        cursor+=static_cast<std::uint64_t>(chunk);written+=chunk;remaining-=chunk;
    }
    return true;
}

bool GpuAddressSpace::write(const std::uint64_t address,
    const std::span<const std::uint8_t> input) {
    std::shared_lock lock(mutex_);
    if(!validate_range(address,input.size(),core::MemoryPermission::Write))return false;
    std::uint64_t cursor=address;std::size_t remaining=input.size();std::size_t consumed=0U;
    while(remaining!=0U){
        const Mapping* mapping=find_mapping(cursor);
        const std::uint64_t offset=cursor-mapping->gpu_base;
        const std::size_t chunk=std::min(remaining,mapping->size-static_cast<std::size_t>(offset));
        if(!physical_memory_.write(mapping->physical_base+offset,input.subspan(consumed,chunk)))return false;
        cursor+=static_cast<std::uint64_t>(chunk);consumed+=chunk;remaining-=chunk;
    }
    return true;
}

std::size_t GpuAddressSpace::mapping_count() const {
    std::shared_lock lock(mutex_);return mappings_.size();
}

} // namespace lucid::gpu
