#include "lucid/arch/system_registers.hpp"
namespace lucid::arch {
std::uint64_t SystemRegisters::read(SystemRegister r) const noexcept { auto i=static_cast<std::size_t>(r); return i<values_.size()?values_[i]:0U; }
bool SystemRegisters::valid(SystemRegister r) const noexcept { return static_cast<std::size_t>(r)<values_.size(); }
RegisterAccess SystemRegisters::write(SystemRegister r,std::uint64_t v) noexcept {
 if(!valid(r)) return RegisterAccess::Invalid;
 if(r==SystemRegister::CurrentEL) return RegisterAccess::ReadOnly;
 values_[static_cast<std::size_t>(r)]=v; return RegisterAccess::Ok;
}
std::uint64_t ExceptionRouter::enter(SystemRegisters& r,const ExceptionRecord& e) const noexcept {
 const auto el=e.source_el>3U?3U:e.source_el;
 (void)r.write(SystemRegister::ELR_EL1,e.return_address);
 (void)r.write(SystemRegister::ESR_EL1,e.syndrome);
 (void)r.write(SystemRegister::FAR_EL1,e.fault_address);
 std::uint64_t slot=el==1U?0x200U:0x400U;
 if(e.kind==ExceptionKind::IRQ) slot+=0x80U;
 else if(e.kind==ExceptionKind::FIQ) slot+=0x100U;
 else if(e.kind==ExceptionKind::SError) slot+=0x180U;
 return (r.read(SystemRegister::VBAR_EL1)&~std::uint64_t{0x7FFU})+slot;
}
}