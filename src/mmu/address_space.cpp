#include "lucid/mmu/address_space.hpp"
#include <limits>
namespace lucid::mmu {
bool AddressSpace::map_page(std::uint64_t va,std::uint64_t pa,bool r,bool w,bool x) {
 if((va&(PageSize-1U))||(pa&(PageSize-1U))||(!r&&!w&&!x)||pa>std::numeric_limits<std::uint64_t>::max()-(PageSize-1U)) return false;
 return pages_.emplace(va,Page{pa,r,w,x}).second;
}
bool AddressSpace::unmap_page(std::uint64_t va){return !(va&(PageSize-1U))&&pages_.erase(va)==1U;}
Translation AddressSpace::translate(std::uint64_t va,AccessType a) const noexcept {
 auto base=va&~(PageSize-1U); auto it=pages_.find(base);
 if(it==pages_.end()) return {TranslationFault::Unmapped,0U};
 const auto& p=it->second; bool ok=a==AccessType::Read?p.r:a==AccessType::Write?p.w:p.x;
 if(!ok) return {TranslationFault::PermissionDenied,0U};
 auto off=va&(PageSize-1U);
 if(p.pa>std::numeric_limits<std::uint64_t>::max()-off) return {TranslationFault::AddressOverflow,0U};
 return {TranslationFault::None,p.pa+off};
}
std::size_t AddressSpace::mapped_pages() const noexcept{return pages_.size();}
}