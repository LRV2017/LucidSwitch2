#include "lucid/core/memory_bus.hpp"
#include "lucid/cpu/block_cache.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
namespace {int failures=0;void expect(bool c,const char*m){if(c)std::cout<<"[PASS] "<<m<<"\n";else{++failures;std::cerr<<"[FAIL] "<<m<<"\n";}}
void write32(lucid::core::MemoryBus&m,std::uint64_t a,std::uint32_t w){std::array<std::uint8_t,4U>b{static_cast<std::uint8_t>(w),static_cast<std::uint8_t>(w>>8U),static_cast<std::uint8_t>(w>>16U),static_cast<std::uint8_t>(w>>24U)};expect(m.write(a,b),"write cached A64 instruction");}}
int main(){using namespace lucid;constexpr std::uint64_t base=0x4000U;core::MemoryBus memory;std::string error;
expect(memory.map_region(base,0x100U,&error),"map basic-block cache code memory");
write32(memory,base,0xD2800540U);write32(memory,base+4U,0x91002000U);write32(memory,base+8U,0x14000000U);
cpu::A64BlockCache cache;cpu::CpuContext context{};context.program_counter=base;
auto first=cache.run(context,memory,2U);expect(first.instructions_executed==2U&&context.x[0]==50U&&context.program_counter==base+8U,"execute cached block with interpreter semantics");
expect(cache.stats().blocks_compiled==1U&&cache.stats().cache_misses==1U,"compile first block and count cache miss");
context.program_counter=base;auto second=cache.run(context,memory,2U);expect(second.instructions_executed==2U&&cache.stats().cache_hits>=1U,"reuse unchanged cached block");
write32(memory,base+4U,0xD2800160U);context.program_counter=base;auto third=cache.run(context,memory,2U);
expect(third.instructions_executed==2U&&context.x[0]==11U,"recompile block after guest code modification");
expect(cache.stats().invalidated_blocks>=1U,"count stale blocks invalidated by memory generation");
context.program_counter=0x900000U;auto fault=cache.run(context,memory,1U);expect(fault.stop_reason==cpu::StepResult::MemoryFault&&fault.instructions_executed==0U,"report fetch fault outside mapped memory");
cache.clear();expect(cache.stats().blocks_compiled==0U&&cache.stats().cache_hits==0U,"clear blocks and stats");
if(failures){std::cerr<<failures<<" A64 block cache test(s) failed.\n";return 1;}std::cout<<"All A64 block cache tests passed.\n";return 0;}
