#include "lucid/core/memory_bus.hpp"
#include "lucid/switch2/homebrew_elf_loader.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (condition) std::cout << "[PASS] " << message << "\n";
    else { ++failures; std::cerr << "[FAIL] " << message << "\n"; }
}
void put16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFU);
    b[o + 1U] = static_cast<std::uint8_t>(v >> 8U);
}
void put32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    for (std::size_t i=0;i<4U;++i) b[o+i]=static_cast<std::uint8_t>((v>>(i*8U))&0xFFU);
}
void put64(std::vector<std::uint8_t>& b, std::size_t o, std::uint64_t v) {
    for (std::size_t i=0;i<8U;++i) b[o+i]=static_cast<std::uint8_t>((v>>(i*8U))&0xFFU);
}
std::vector<std::uint8_t> valid_image() {
    std::vector<std::uint8_t> b(0x104U,0U);
    b[0]=0x7FU; b[1]='E'; b[2]='L'; b[3]='F';
    b[4]=2U; b[5]=1U; b[6]=1U;
    put16(b,16U,2U); put16(b,18U,183U); put32(b,20U,1U);
    put64(b,24U,0x100000U); put64(b,32U,64U);
    put16(b,52U,64U); put16(b,54U,56U); put16(b,56U,1U);
    put32(b,64U,1U); put32(b,68U,5U); put64(b,72U,0x100U);
    put64(b,80U,0x100000U); put64(b,96U,4U); put64(b,104U,16U);
    put64(b,112U,0x100U);
    b[0x100U]=0x1FU; b[0x101U]=0x20U; b[0x102U]=0x03U; b[0x103U]=0xD5U; // NOP
    return b;
}
}
int main() {
    using namespace lucid;
    switch2::HomebrewElfLoader loader;
    auto image=valid_image();
    core::MemoryBus memory;
    switch2::HomebrewElfLoadInfo info{};
    std::string error;
    expect(loader.load(image,memory,info,&error),"load a bounded static AArch64 ELF64 homebrew image");
    expect(info.entry_point==0x100000U && info.loaded_segments==1U &&
           info.total_memory_bytes==16U,"report entry point and segment memory usage");
    std::array<std::uint8_t,16U> loaded{};
    expect(memory.read(0x100000U,loaded),"read the loaded executable segment");
    expect(loaded[0]==0x1FU && loaded[1]==0x20U && loaded[2]==0x03U &&
           loaded[3]==0xD5U && loaded[15]==0U,"copy file bytes and zero-fill BSS tail");
    expect(memory.can_access(0x100000U,4U,core::MemoryPermission::Execute),
           "retain executable permission on a loaded code segment");
    const std::array<std::uint8_t,1U> byte{0xFFU};
    expect(!memory.write(0x100000U,byte,&error),
           "remove write permission after loading an executable segment");

    auto bad_flags=valid_image(); put32(bad_flags,68U,7U);
    core::MemoryBus rejected_memory;
    info={}; error.clear();
    expect(!loader.load(bad_flags,rejected_memory,info,&error),
           "reject writable-executable ELF segments");
    expect(rejected_memory.region_count()==0U,
           "reject invalid ELF before mapping guest memory");

    auto bad_machine=valid_image(); put16(bad_machine,18U,62U);
    core::MemoryBus wrong_cpu_memory;
    expect(!loader.load(bad_machine,wrong_cpu_memory,info,&error),
           "reject non-AArch64 ELF images");

    auto truncated=valid_image(); truncated.resize(100U);
    core::MemoryBus truncated_memory;
    expect(!loader.load(truncated,truncated_memory,info,&error),
           "reject truncated ELF program-header tables");

    auto bad_entry=valid_image(); put64(bad_entry,24U,0x200000U);
    core::MemoryBus bad_entry_memory;
    expect(!loader.load(bad_entry,bad_entry_memory,info,&error),
           "reject entry points outside executable segments");

    core::MemoryBus cleanup_memory;
    expect(cleanup_memory.map_region(0x300000U,0x1000U,&error),
           "map a pre-existing region to exercise loader rollback");
    auto conflict=valid_image(); put64(conflict,80U,0x300000U); put64(conflict,24U,0x300000U);
    expect(!loader.load(conflict,cleanup_memory,info,&error),
           "reject a segment overlapping an existing guest mapping");
    expect(cleanup_memory.region_count()==1U,
           "leave pre-existing guest mappings untouched on loader failure");

    if(failures!=0){std::cerr<<failures<<" ELF loader test(s) failed.\n";return 1;}
    std::cout<<"All Switch 2 ELF loader tests passed.\n"; return 0;
}
