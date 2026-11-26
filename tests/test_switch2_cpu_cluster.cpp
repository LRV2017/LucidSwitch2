#include "lucid/core/memory_bus.hpp"
#include "lucid/switch2/cpu_cluster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void expect(const bool condition, const char* message) {
    if (condition) {
        std::cout << "[PASS] " << message << "\n";
    } else {
        ++failures;
        std::cerr << "[FAIL] " << message << "\n";
    }
}

void write_u32(std::array<std::uint8_t, 8U>& bytes,
               const std::size_t offset,
               const std::uint32_t value) {
    for (std::size_t byte = 0; byte < 4U; ++byte) {
        bytes[offset + byte] = static_cast<std::uint8_t>(
            (value >> (byte * 8U)) & 0xFFU);
    }
}

void test_eight_independent_core_contexts() {
    using namespace lucid;
    constexpr std::uint64_t base = 0x1000U;
    core::MemoryBus memory;
    std::string error;
    expect(memory.map_region(base, 0x1000U, &error),
           "map shared guest memory for the Switch 2 CPU cluster");

    switch2::CpuCluster cluster;
    expect(cluster.state(0U) == switch2::CpuCoreState::Parked,
           "secondary core contexts begin parked");
    expect(!cluster.start_core(switch2::CpuCluster::CoreCount, base),
           "reject an out-of-range CPU core index");
    expect(!cluster.start_core(0U, base + 2U),
           "reject a misaligned core entry point");

    for (std::size_t core = 0; core < switch2::CpuCluster::CoreCount; ++core) {
        std::array<std::uint8_t, 8U> program{};
        const std::uint32_t immediate =
            42U + static_cast<std::uint32_t>(core);
        write_u32(program, 0U, 0xD2800000U | (immediate << 5U)); // MOVZ X0, #imm
        write_u32(program, 4U, 0x14000000U);                    // B .
        const std::uint64_t entry = base + static_cast<std::uint64_t>(core) * 8U;
        expect(memory.write(entry, program, &error),
               "write a per-core synthetic A64 program");
        expect(cluster.start_core(core, entry, 0x1FF0U),
               "start an independent A64 architectural context");
    }

    const auto report = cluster.run_round_robin(
        memory, switch2::CpuCluster::CoreCount * 2U);
    expect(report.instructions_executed ==
               switch2::CpuCluster::CoreCount * 2U,
           "retire a bounded instruction budget across all cores");
    expect(report.runnable_cores == switch2::CpuCluster::CoreCount &&
           report.halted_cores == 0U && report.faulted_cores == 0U,
           "keep all eight healthy cores runnable");
    expect(report.next_core == 0U,
           "round-robin cursor wraps after one complete pair of core sweeps");

    for (std::size_t core = 0; core < switch2::CpuCluster::CoreCount; ++core) {
        const auto* context = cluster.context(core);
        expect(context != nullptr, "expose the selected core architectural context");
        if (context != nullptr) {
            expect(context->x[0] ==
                       42ULL + static_cast<std::uint64_t>(core),
                   "keep general-purpose registers independent per core");
            expect(context->instructions_retired == 2U,
                   "give every runnable core two instruction quanta");
            expect(context->program_counter ==
                       base + static_cast<std::uint64_t>(core) * 8U + 4U,
                   "keep each core program counter independent");
            expect(context->sp == 0x1FF0U,
                   "preserve each core stack pointer");
        }
    }

    cpu::StepResult result = cpu::StepResult::Executed;
    expect(cluster.step(0U, memory, result),
           "allow a manually selected runnable core to step");
    expect(!cluster.step(switch2::CpuCluster::CoreCount, memory, result),
           "reject a step request for an invalid core");
    expect(cluster.context(switch2::CpuCluster::CoreCount) == nullptr,
           "return no context for an invalid core index");
}

void test_fault_isolation() {
    using namespace lucid;
    constexpr std::uint64_t base = 0x8000U;
    core::MemoryBus memory;
    std::string error;
    expect(memory.map_region(base, 0x100U, &error),
           "map memory for core fault-isolation test");

    std::array<std::uint8_t, 4U> invalid{0xFFU, 0xFFU, 0xFFU, 0xFFU};
    std::array<std::uint8_t, 8U> healthy{};
    write_u32(healthy, 0U, 0xD2800120U); // MOVZ X0, #9
    write_u32(healthy, 4U, 0x14000000U); // B .

    expect(memory.write(base, invalid, &error), "write an unsupported opcode for core 0");
    expect(memory.write(base + 0x10U, healthy, &error),
           "write a healthy instruction stream for core 1");

    switch2::CpuCluster cluster;
    expect(cluster.start_core(0U, base), "start core that will fault");
    expect(cluster.start_core(1U, base + 0x10U), "start independent healthy core");
    const auto report = cluster.run_round_robin(memory, 3U);
    expect(cluster.state(0U) == switch2::CpuCoreState::Faulted,
           "mark only the core with an unsupported instruction as faulted");
    expect(cluster.state(1U) == switch2::CpuCoreState::Runnable,
           "keep the other core running after a peer faults");
    expect(report.faulted_cores == 1U && report.runnable_cores == 1U,
           "report per-core fault and runnable counts");
    const auto* healthy_context = cluster.context(1U);
    expect(healthy_context != nullptr && healthy_context->x[0] == 9U &&
           healthy_context->instructions_retired == 2U,
           "continue round-robin execution on the healthy core");
}
} // namespace

int main() {
    test_eight_independent_core_contexts();
    test_fault_isolation();
    if (failures != 0) {
        std::cerr << failures << " Switch 2 CPU cluster test(s) failed.\n";
        return 1;
    }
    std::cout << "All Switch 2 CPU cluster tests passed.\n";
    return 0;
}
