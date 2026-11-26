#include "lucid/devices/device_bus.hpp"
#include <limits>
namespace lucid::devices {
bool DeviceBus::map(std::uint64_t base,std::uint64_t size,MmioDevice& d){if(!size||size>std::numeric_limits<std::uint64_t>::max()-base)return false;auto end=base+size;for(auto&m:mappings_){auto e=m.base+m.size;if(base<e&&m.base<end)return false;}mappings_.push_back({base,size,&d});return true;}
DeviceResult DeviceBus::access(std::uint64_t a,DeviceAccess op,std::uint64_t v,unsigned w) noexcept{for(auto&m:mappings_)if(a>=m.base&&a-m.base<m.size)return m.device->access(a-m.base,op,v,w);return {false,0U};}
std::size_t DeviceBus::device_count() const noexcept{return mappings_.size();}
}