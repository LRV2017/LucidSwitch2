#include "lucid/arch/system_registers.hpp"
#include "lucid/devices/device_bus.hpp"
#include "lucid/devices/interrupt_controller.hpp"
#include "lucid/devices/virtual_timer.hpp"
#include "lucid/mmu/address_space.hpp"
#include "lucid/mmu/stage1_mmu.hpp"
#include "lucid/core/memory_bus.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
namespace { int fails=0; void check(bool v,const char*n){if(v)std::cout<<"[PASS] "<<n<<'\n';else{++fails;std::cerr<<"[FAIL] "<<n<<'\n';}}
bool write64(lucid::core::MemoryBus& memory, std::uint64_t address,
             std::uint64_t value) {
 std::array<std::uint8_t,8> bytes{};
 for(std::size_t i=0;i<bytes.size();++i)
  bytes[i]=static_cast<std::uint8_t>((value>>(i*8U))&0xFFU);
 return memory.write(address,bytes);
}
void test_stage1_mmu() {
 using namespace lucid;
 core::MemoryBus memory;
 check(memory.map_region(0x1000U,0x9000U),"map stage-1 table storage");
 check(memory.map_region(0xB000U,0x2000U),"map stage-1 translated targets");

 constexpr std::uint64_t va=0x0000123456789000ULL;
 constexpr std::uint64_t root=0x1000U,l1_table=0x2000U,
                         l2_table=0x3000U,l3_table=0x4000U;
 const auto l0_index=(va>>39U)&0x1FFU;
 const auto l1_index=(va>>30U)&0x1FFU;
 const auto l2_index=(va>>21U)&0x1FFU;
 const auto l3_index=(va>>12U)&0x1FFU;
 check(write64(memory,root+l0_index*8U,l1_table|3U),"write L0 table descriptor");
 check(write64(memory,l1_table+l1_index*8U,l2_table|3U),"write L1 table descriptor");
 check(write64(memory,l2_table+l2_index*8U,l3_table|3U),"write L2 table descriptor");
 constexpr std::uint64_t page_descriptor=0xB000U|(1ULL<<10U)|3U;
 check(write64(memory,l3_table+l3_index*8U,page_descriptor),"write L3 page descriptor");

 mmu::Stage1Mmu mmu(memory);
 check(mmu.configure(root,mmu::Stage1Mmu::DefaultTcr),"configure 48-bit 4 KiB stage-1 regime");
 auto translated=mmu.translate(va+0x123U,mmu::AccessType::Read);
 check(translated&&translated.physical_address==0xB123U,
       "walk L0-L3 tables and preserve page offset");
 const auto misses_after_walk=mmu.tlb_misses();
 translated=mmu.translate(va+0x456U,mmu::AccessType::Read);
 check(translated&&translated.physical_address==0xB456U&&
       mmu.tlb_hits()>0U&&mmu.tlb_misses()==misses_after_walk,
       "reuse cached translation for a page TLB hit");
 check(static_cast<bool>(mmu.translate(va,mmu::AccessType::Write)),
       "allow EL1 write through an AP=00 page");
 
 // A changed descriptor does not take effect until its VA is invalidated.
 check(write64(memory,l3_table+l3_index*8U,
       0xC000U|(1ULL<<10U)|3U),"remap page-table entry");
 translated=mmu.translate(va,mmu::AccessType::Read);
 check(translated&&translated.physical_address==0xB000U,
       "cached translation remains until TLB invalidation");
 mmu.invalidate_va(va);
 translated=mmu.translate(va,mmu::AccessType::Read);
 check(translated&&translated.physical_address==0xC000U,
       "VA invalidation exposes updated page descriptor");

 // AP[1] makes the page read-only for privileged accesses.
 constexpr std::uint64_t read_only_page=
     0xC000U|(1ULL<<10U)|(1ULL<<7U)|3U;
 check(write64(memory,l3_table+l3_index*8U,read_only_page),
       "set page AP read-only bit");
 mmu.invalidate_va(va);
 check(static_cast<bool>(mmu.translate(va,mmu::AccessType::Read)),
       "read remains allowed for read-only page");
 check(mmu.translate(va,mmu::AccessType::Write).fault==
       mmu::TranslationFault::PermissionDenied,
       "reject write to AP read-only page");

 // Hierarchical restrictions on table descriptors are inherited by leaf mappings.
 check(write64(memory,root+l0_index*8U,l1_table|3U|(1ULL<<61U)),
       "set APTable no-write restriction");
 mmu.invalidate_all();
 check(mmu.translate(va,mmu::AccessType::Write).fault==
       mmu::TranslationFault::PermissionDenied,
       "inherit APTable no-write restriction");
 check(write64(memory,root+l0_index*8U,l1_table|3U|(1ULL<<59U)),
       "set PXNTable restriction");
 mmu.invalidate_all();
 check(mmu.translate(va,mmu::AccessType::Execute).fault==
       mmu::TranslationFault::PermissionDenied,
       "inherit PXNTable execute restriction");
 check(write64(memory,root+l0_index*8U,l1_table|3U),
       "restore unrestricted L0 table descriptor");
 mmu.invalidate_all();

 // PXN blocks privileged instruction fetches.
 check(write64(memory,l3_table+l3_index*8U,
       read_only_page|(1ULL<<53U)),"set page PXN bit");
 mmu.invalidate_va(va);
 check(mmu.translate(va,mmu::AccessType::Execute).fault==
       mmu::TranslationFault::PermissionDenied,
       "enforce privileged execute-never bit");

 // Access flag is software-managed in this narrow model and must be set.
 check(write64(memory,l3_table+l3_index*8U,
       (read_only_page&~(1ULL<<53U))&~(1ULL<<10U)),
       "clear access flag");
 mmu.invalidate_va(va);
 check(mmu.translate(va,mmu::AccessType::Read).fault==
       mmu::TranslationFault::AccessFlagFault,
       "report access-flag fault");

 check(write64(memory,l3_table+l3_index*8U,0U),"clear page descriptor");
 mmu.invalidate_all();
 check(mmu.translate(va,mmu::AccessType::Read).fault==
       mmu::TranslationFault::Unmapped,
       "report invalid translation descriptor");

 // L1 1 GiB block descriptor.
 constexpr std::uint64_t block_root=0x5000U,block_l1=0x6000U;
 constexpr std::uint64_t block_va=0x40001234U;
 check(write64(memory,block_root,block_l1|3U),"write block L0 descriptor");
 check(write64(memory,block_l1+8U,0x80000000U|(1ULL<<10U)|1U),
       "write L1 1 GiB block descriptor");
 mmu::Stage1Mmu block_mmu(memory);
 check(block_mmu.configure(block_root,mmu::Stage1Mmu::DefaultTcr),
       "configure L1 block test regime");
 translated=block_mmu.translate(block_va,mmu::AccessType::Read);
 check(translated&&translated.physical_address==0x80001234U,
       "translate through 1 GiB L1 block");

 // L2 2 MiB block descriptor.
 constexpr std::uint64_t block2_root=0x7000U,
                         block2_l1=0x8000U,block2_l2=0x9000U;
 constexpr std::uint64_t block2_va=0x00401234U;
 check(write64(memory,block2_root,block2_l1|3U),"write L2 test L0 descriptor");
 check(write64(memory,block2_l1,block2_l2|3U),"write L2 test L1 table descriptor");
 check(write64(memory,block2_l2+2U*8U,
       0x20000000U|(1ULL<<10U)|1U),"write L2 2 MiB block descriptor");
 mmu::Stage1Mmu block2_mmu(memory);
 check(block2_mmu.configure(block2_root,mmu::Stage1Mmu::DefaultTcr),
       "configure L2 block test regime");
 translated=block2_mmu.translate(block2_va,mmu::AccessType::Read);
 check(translated&&translated.physical_address==0x20001234U,
       "translate through 2 MiB L2 block");

 check(!mmu.configure(root,mmu::Stage1Mmu::TcrIps48Bit|17U),
       "reject unsupported TCR address size");
 check(!mmu.configure(root|2U,mmu::Stage1Mmu::DefaultTcr),
       "reject reserved low bits in TTBR0");
 check(mmu.translate(va,mmu::AccessType::Read).fault==
       mmu::TranslationFault::UnsupportedConfiguration,
       "report disabled stage-1 configuration");
 mmu::Stage1Mmu broken_root(memory);
 check(broken_root.configure(0xF000U,mmu::Stage1Mmu::DefaultTcr),
       "accept aligned table base before a walk");
 check(broken_root.translate(va,mmu::AccessType::Read).fault==
       mmu::TranslationFault::TableWalkFault,
       "report physical fault during page-table walk");
}
class TestDevice final:public lucid::devices::MmioDevice{
public: lucid::devices::DeviceResult access(std::uint64_t off,lucid::devices::DeviceAccess op,std::uint64_t v,unsigned w) noexcept override{if(off||w!=4U)return{false,0};if(op==lucid::devices::DeviceAccess::Write)value=v;return{true,value};}
std::string_view name()const noexcept override{return"test";} std::uint64_t value{};
};}
int main(){using namespace lucid;
 test_stage1_mmu();
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