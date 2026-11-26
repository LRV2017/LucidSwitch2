#include "lucid/arch/system_registers.hpp"
#include "lucid/devices/device_bus.hpp"
#include "lucid/devices/interrupt_controller.hpp"
#include "lucid/devices/virtual_timer.hpp"
#include "lucid/mmu/address_space.hpp"
#include <iostream>
#include <string_view>
namespace { int fails=0; void check(bool v,const char*n){if(v)std::cout<<"[PASS] "<<n<<'\n';else{++fails;std::cerr<<"[FAIL] "<<n<<'\n';}}
class TestDevice final:public lucid::devices::MmioDevice{
public: lucid::devices::DeviceResult access(std::uint64_t off,lucid::devices::DeviceAccess op,std::uint64_t v,unsigned w) noexcept override{if(off||w!=4U)return{false,0};if(op==lucid::devices::DeviceAccess::Write)value=v;return{true,value};}
std::string_view name()const noexcept override{return"test";} std::uint64_t value{};
};}
int main(){using namespace lucid;
 mmu::AddressSpace vm;check(vm.map_page(0x1000,0x9000,true,false,true),"map virtual page");check(!vm.map_page(0x1000,0xA000,true,true,true),"reject duplicate page");
 auto t=vm.translate(0x1234,mmu::AccessType::Read);check(t&&t.physical_address==0x9234,"translate virtual page offset");
 check(vm.translate(0x1234,mmu::AccessType::Write).fault==mmu::TranslationFault::PermissionDenied,"enforce virtual permissions");
 check(vm.translate(0x5000,mmu::AccessType::Read).fault==mmu::TranslationFault::Unmapped,"report unmapped virtual page");
 check(vm.unmap_page(0x1000)&&vm.mapped_pages()==0,"unmap page");
 arch::SystemRegisters regs;check(regs.write(arch::SystemRegister::VBAR_EL1,0x8000)==arch::RegisterAccess::Ok,"write VBAR");
 check(regs.write(arch::SystemRegister::CurrentEL,1)==arch::RegisterAccess::ReadOnly,"protect CurrentEL");
 arch::ExceptionRouter router;auto vector=router.enter(regs,{arch::ExceptionKind::IRQ,1,0x1234,0x55,0xABC});
 check(vector==0x8280,"calculate EL1 IRQ vector");check(regs.read(arch::SystemRegister::ELR_EL1)==0x1234&&regs.read(arch::SystemRegister::FAR_EL1)==0xABC,"capture exception state");
 devices::InterruptController irq;check(irq.raise(7)&&irq.set_enabled(7,true)&&irq.pending(7),"raise enabled interrupt");
 unsigned id=99;check(irq.acknowledge(id)&&id==7&&!irq.pending(7),"acknowledge interrupt");check(!irq.raise(64),"reject invalid interrupt id");
 devices::VirtualTimer timer;timer.set_compare(10);timer.set_enabled(true);timer.advance(9);check(!timer.interrupt_pending(),"timer before compare");timer.advance(1);check(timer.interrupt_pending(),"timer at compare");timer.acknowledge();check(!timer.interrupt_pending(),"acknowledge timer");
 TestDevice dev;devices::DeviceBus bus;check(bus.map(0x100000,0x1000,dev),"map MMIO device");check(!bus.map(0x100800,0x1000,dev),"reject overlapping MMIO");
 auto wr=bus.access(0x100000,devices::DeviceAccess::Write,0x12345678,4);auto rd=bus.access(0x100000,devices::DeviceAccess::Read,0,4);
 check(wr.success&&rd.success&&rd.value==0x12345678,"route MMIO accesses");check(!bus.access(0x200000,devices::DeviceAccess::Read,0,4).success,"fault unmapped MMIO");
 return fails?1:0;
}