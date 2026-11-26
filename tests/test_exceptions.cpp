#include "lucid/arch/system_registers.hpp"
#include "lucid/core/emulator.hpp"
#include "lucid/core/memory_bus.hpp"
#include "lucid/cpu/cpu_backend.hpp"
#include "lucid/devices/virtual_timer.hpp"

#include <array>
#include <cstdint>
#include <iostream>

namespace {
int failures = 0;

void expect(const bool condition, const char* message) {
    if (condition) {
        std::cout << "[PASS] " << message << '\n';
    } else {
        ++failures;
        std::cerr << "[FAIL] " << message << '\n';
    }
}

bool write32(lucid::core::MemoryBus& memory, const std::uint64_t address,
             const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 24U) & 0xFFU)
    };
    return memory.write(address, bytes);
}

void test_svc_and_eret() {
    using namespace lucid;
    core::MemoryBus memory;
    expect(memory.map_region(0x1000U, 0x3000U), "map SVC test memory");

    constexpr std::uint64_t entry = 0x1000U;
    constexpr std::uint64_t vector_base = 0x2000U;
    constexpr std::uint64_t lower_el_sync_vector = vector_base + 0x400U;
    constexpr std::uint32_t svc_42 = 0xD4000001U | (0x42U << 5U);
    constexpr std::uint32_t eret = 0xD69F03E0U;
    expect(write32(memory, entry, svc_42), "write SVC instruction");
    expect(write32(memory, lower_el_sync_vector, eret),
           "write ERET handler instruction");

    cpu::A64Interpreter interpreter;
    cpu::CpuContext context{};
    context.program_counter = entry;
    context.flags = {true, false, true, false};
    (void)context.system_registers.write(arch::SystemRegister::VBAR_EL1,
                                         vector_base);
    (void)context.system_registers.write(arch::SystemRegister::DAIF, 1ULL << 6U);

    expect(interpreter.step(context, memory) == cpu::StepResult::ExceptionTaken,
           "SVC takes a synchronous exception");
    expect(context.program_counter == lower_el_sync_vector,
           "EL0 SVC selects lower-EL AArch64 synchronous vector");
    expect(context.system_registers.current_el() == 1U &&
           context.system_registers.read(arch::SystemRegister::CurrentEL) == 4U,
           "exception entry switches CurrentEL from EL0 to EL1");
    expect(context.system_registers.read(arch::SystemRegister::ELR_EL1) ==
               entry + 4U,
           "SVC saves the next instruction address in ELR_EL1");
    expect(context.system_registers.read(arch::SystemRegister::ESR_EL1) ==
               ((0x15ULL << 26U) | 0x42U),
           "SVC records exception class and immediate in ESR_EL1");
    expect((context.system_registers.read(arch::SystemRegister::DAIF) & 0x3C0U)
               == 0x3C0U,
           "exception entry masks asynchronous exceptions");

    expect(interpreter.step(context, memory) == cpu::StepResult::Branch,
           "ERET returns from the EL1 exception handler");
    expect(context.program_counter == entry + 4U &&
           context.system_registers.current_el() == 0U,
           "ERET restores the EL0 execution level and return address");
    expect(context.system_registers.read(arch::SystemRegister::DAIF) ==
               (1ULL << 6U),
           "ERET restores the saved DAIF mask state");
    expect(context.flags.negative && context.flags.carry &&
           !context.flags.zero && !context.flags.overflow,
           "ERET restores the saved NZCV flags");

    const std::uint32_t mrs_vbar =
        0xD5200000U | (0xC600U << 5U) | 1U;
    expect(write32(memory, entry + 4U, mrs_vbar),
           "write privileged MRS test instruction");
    const auto before_pc = context.program_counter;
    expect(interpreter.step(context, memory) == cpu::StepResult::SystemRegisterFault,
           "EL0 cannot access privileged EL1 system registers");
    expect(context.program_counter == before_pc,
           "privileged system-register fault leaves the PC unchanged");
}

void test_el1_sp0_vector() {
    using namespace lucid;
    core::MemoryBus memory;
    expect(memory.map_region(0x1000U, 0x2000U), "map EL1 SP0 test memory");
    constexpr std::uint32_t svc = 0xD4000001U;
    expect(write32(memory, 0x1000U, svc), "write EL1 SVC instruction");

    cpu::A64Interpreter interpreter;
    cpu::CpuContext context{};
    context.program_counter = 0x1000U;
    context.system_registers.set_exception_level(1U);
    (void)context.system_registers.write(arch::SystemRegister::SPSel, 0U);
    (void)context.system_registers.write(arch::SystemRegister::VBAR_EL1, 0x2000U);

    expect(interpreter.step(context, memory) == cpu::StepResult::ExceptionTaken,
           "EL1 SVC is routed as a current-EL exception");
    expect(context.program_counter == 0x2000U,
           "EL1 using SP_EL0 selects the current-EL SP0 vector slot");
}

void test_virtual_timer_one_shot() {
    lucid::devices::VirtualTimer timer;
    timer.set_compare(5U);
    timer.set_enabled(true);
    timer.advance(4U);
    expect(!timer.interrupt_pending(), "timer remains quiet before compare");
    timer.advance(1U);
    expect(timer.interrupt_pending(), "timer asserts at the compare value");
    timer.acknowledge();
    expect(!timer.interrupt_pending(), "timer acknowledgement clears pending");
    timer.advance(10U);
    expect(!timer.interrupt_pending(),
           "acknowledged one-shot timer does not immediately re-trigger");
    timer.set_compare(20U);
    timer.advance(5U);
    expect(timer.interrupt_pending(),
           "programming a new compare value rearms the timer");
}

void test_timer_irq_integration() {
    using namespace lucid;
    core::Emulator emulator;
    core::EmulatorConfig config{};
    config.memory_base = 0x1000U;
    config.memory_size = 0x10000U;
    config.entry_point = 0x1000U;
    expect(emulator.initialize(config), "initialize emulator for timer IRQ test");

    constexpr std::uint64_t vbar = 0x1000U;
    constexpr std::uint64_t irq_vector = vbar + 0x480U;
    constexpr std::uint32_t nop = 0xD503201FU;
    constexpr std::uint32_t movz_x0_7 = 0xD28000E0U;
    constexpr std::uint32_t eret = 0xD69F03E0U;
    expect(write32(emulator.memory_bus(), 0x1000U, nop),
           "write guest instruction before timer IRQ");
    expect(write32(emulator.memory_bus(), irq_vector, movz_x0_7),
           "write timer IRQ handler body");
    expect(write32(emulator.memory_bus(), irq_vector + 4U, eret),
           "write timer IRQ handler ERET");

    (void)emulator.cpu_context().system_registers.write(
        arch::SystemRegister::VBAR_EL1, vbar);
    (void)emulator.cpu_context().system_registers.write(
        arch::SystemRegister::DAIF, 0U);
    emulator.interrupt_controller().set_enabled(
        devices::InterruptController::TimerInterruptId, true);
    emulator.virtual_timer().set_compare(1U);
    emulator.virtual_timer().set_enabled(true);

    expect(emulator.run(3U) == core::RunResult::InstructionLimitReached,
           "emulator executes through timer IRQ and ERET");
    expect(emulator.cpu_context().x[0] == 7U,
           "CPU executes the timer IRQ handler body");
    expect(emulator.cpu_context().program_counter == 0x1004U &&
           emulator.cpu_context().system_registers.current_el() == 0U,
           "timer IRQ ERET resumes EL0 at the interrupted PC");
    expect(emulator.virtual_timer().counter() == 3U &&
           !emulator.virtual_timer().interrupt_pending() &&
           emulator.interrupt_controller().pending_mask() == 0U,
           "timer IRQ is acknowledged and not spuriously re-delivered");
}

void test_emulator_svc_dispatch_subset() {
    using namespace lucid;
    core::Emulator emulator;
    core::EmulatorConfig config{};
    config.memory_base = 0x1000U;
    config.memory_size = 0x10000U;
    config.entry_point = 0x1000U;
    expect(emulator.initialize(config),
           "initialize emulator for the EL0 SVC dispatch test");

    constexpr std::uint64_t entry = 0x1000U;
    constexpr std::uint64_t vector_base = 0x4000U;
    constexpr std::uint32_t svc_get_processor =
        0xD4000001U | (0x10U << 5U);
    constexpr std::uint32_t svc_get_tick =
        0xD4000001U | (0x1EU << 5U);
    constexpr std::uint32_t svc_wait_sync =
        0xD4000001U | (0x18U << 5U);

    expect(write32(emulator.memory_bus(), entry, svc_get_processor),
           "write GetCurrentProcessorNumber SVC #0x10");
    expect(write32(emulator.memory_bus(), entry + 4U, svc_get_tick),
           "write GetSystemTick SVC #0x1E");
    expect(write32(emulator.memory_bus(), entry + 8U, svc_wait_sync),
           "write unimplemented WaitSynchronization SVC #0x18");
    (void)emulator.cpu_context().system_registers.write(
        arch::SystemRegister::VBAR_EL1, vector_base);

    expect(emulator.run(1U) == core::RunResult::InstructionLimitReached,
           "dispatch SVC #0x10 and return directly to EL0");
    expect(emulator.cpu_context().x[0] == 0U &&
           emulator.cpu_context().program_counter == entry + 4U &&
           emulator.cpu_context().system_registers.current_el() == 0U,
           "GetCurrentProcessorNumber returns virtual CPU zero");

    expect(emulator.run(1U) == core::RunResult::InstructionLimitReached,
           "dispatch SVC #0x1E and return directly to EL0");
    expect(emulator.cpu_context().x[0] == 1U &&
           emulator.cpu_context().program_counter == entry + 8U &&
           emulator.cpu_context().system_registers.current_el() == 0U,
           "GetSystemTick returns the deterministic guest tick count");

    expect(emulator.run(1U) == core::RunResult::InstructionLimitReached,
           "leave SVC #0x18 to the guest exception-vector path");
    expect(emulator.cpu_context().program_counter == vector_base + 0x400U &&
           emulator.cpu_context().system_registers.current_el() == 1U,
           "unimplemented WaitSynchronization is not falsely claimed as handled");
}

} // namespace

int main() {
    test_svc_and_eret();
    test_el1_sp0_vector();
    test_virtual_timer_one_shot();
    test_timer_irq_integration();
    test_emulator_svc_dispatch_subset();

    if (failures != 0) {
        std::cerr << failures << " exception test(s) failed.\n";
        return 1;
    }
    std::cout << "All exception and interrupt tests passed.\n";
    return 0;
}
