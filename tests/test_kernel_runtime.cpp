#include "lucid/kernel/runtime.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void expect(const bool condition, const char* message) {
    if (condition) std::cout << "[PASS] " << message << "\n";
    else { ++failures; std::cerr << "[FAIL] " << message << "\n"; }
}

void test_process_and_thread_creation() {
    lucid::kernel::Runtime runtime;
    std::uint32_t process = 0U, invalid = 0U;
    std::string error;
    expect(!runtime.create_process("", invalid, &error), "reject an empty process name");
    expect(runtime.create_process("homebrew-test", process, &error),
           "create an active guest process");
    expect(process != 0U && runtime.process_count() == 1U,
           "assign a stable nonzero process identifier");
    std::uint32_t thread = 0U;
    expect(!runtime.create_thread(process, 0x1001U, 0x8000U, 10U, 0U, thread, &error),
           "reject an unaligned thread entry point");
    expect(!runtime.create_thread(process, 0x1000U, 0x8008U, 10U, 0U, thread, &error),
           "reject a misaligned thread stack pointer");
    expect(runtime.create_thread(process, 0x1000U, 0x8000U, 10U, 42U, thread, &error),
           "create a ready guest thread");
    lucid::kernel::ThreadInfo info{};
    expect(runtime.get_thread(thread, info) &&
           info.state == lucid::kernel::ThreadState::Ready &&
           info.context.program_counter == 0x1000U &&
           info.context.sp == 0x8000U && info.context.x[0] == 42U,
           "initialize entry PC, aligned SP and argument register X0");
}

void test_priority_scheduling_and_waits() {
    lucid::kernel::Runtime runtime;
    std::uint32_t process_a = 0U, process_b = 0U;
    std::uint32_t low = 0U, middle = 0U, high_a = 0U, high_b = 0U;
    std::string error;
    expect(runtime.create_process("process-a", process_a, &error) &&
           runtime.create_process("process-b", process_b, &error),
           "create two independent guest processes");
    expect(runtime.create_thread(process_a, 0x1000U, 0x8000U, 20U, 0U, low, &error) &&
           runtime.create_thread(process_a, 0x2000U, 0x9000U, 10U, 0U, middle, &error) &&
           runtime.create_thread(process_b, 0x3000U, 0xA000U, 1U, 0U, high_a, &error) &&
           runtime.create_thread(process_b, 0x4000U, 0xB000U, 1U, 0U, high_b, &error),
           "create threads at three scheduler priorities");

    std::uint32_t scheduled = 0U;
    expect(runtime.schedule_next(scheduled, &error) && scheduled == high_a,
           "dispatch the highest-priority ready thread first");
    expect(runtime.yield_current(scheduled, &error) && scheduled == high_b,
           "yield fairly between equal-priority ready threads");
    expect(runtime.block_current(0x55U, scheduled, &error) && scheduled == high_a,
           "block current thread and dispatch its equal-priority peer");
    expect(runtime.wake_waiters(0x55U) == 1U,
           "wake exactly the thread waiting on a matching wait key");
    expect(runtime.block_current(0x66U, scheduled, &error) && scheduled == high_b,
           "return to the other ready high-priority thread");
    expect(runtime.block_current(0x55U, scheduled, &error) && scheduled == middle,
           "block the second high-priority thread and dispatch the next priority");
    expect(runtime.wake_waiters(0x55U) == 1U,
           "wake a blocked thread back into the ready set");
    expect(runtime.yield_current(scheduled, &error) && scheduled == high_b,
           "select the newly woken higher-priority thread");
    expect(runtime.terminate_process(process_b, &error),
           "terminate a process with runnable and blocked threads");
    expect(runtime.schedule_next(scheduled, &error) && scheduled == middle,
           "dispatch the remaining process after terminating its peer");
    lucid::kernel::ThreadInfo info{};
    expect(runtime.get_thread(middle, info) &&
           info.state == lucid::kernel::ThreadState::Running,
           "track the dispatched thread state");
}

void test_process_owned_handles() {
    lucid::kernel::Runtime runtime;
    std::uint32_t process_a = 0U, process_b = 0U;
    std::string error;
    expect(runtime.create_process("owner", process_a, &error) &&
           runtime.create_process("other", process_b, &error),
           "create processes for handle-isolation tests");
    std::uint32_t handle = 0U;
    expect(runtime.create_handle(process_a, lucid::kernel::KernelObjectType::Event,
                                 0xABCDEFU, handle, &error),
           "allocate a process-owned event handle");
    std::uint64_t object_id = 0U;
    expect(runtime.resolve_handle(process_a, handle,
                                  lucid::kernel::KernelObjectType::Event, object_id) &&
           object_id == 0xABCDEFU,
           "resolve a handle for its owner and required type");
    expect(!runtime.resolve_handle(process_b, handle,
                                   lucid::kernel::KernelObjectType::Event, object_id),
           "prevent another process from resolving the handle");
    expect(!runtime.resolve_handle(process_a, handle,
                                   lucid::kernel::KernelObjectType::Mutex, object_id),
           "reject resolving a handle as the wrong object type");
    expect(runtime.close_handle(process_a, handle) &&
           !runtime.resolve_handle(process_a, handle,
                                   lucid::kernel::KernelObjectType::Event, object_id),
           "close and invalidate a process-owned handle");
    expect(runtime.create_handle(process_a, lucid::kernel::KernelObjectType::Session,
                                 0x1234U, handle, &error),
           "create a handle for process cleanup testing");
    expect(runtime.handle_count() == 1U, "count active process handles");
    expect(runtime.terminate_process(process_a, &error),
           "terminate a guest process");
    lucid::kernel::ProcessInfo info{};
    expect(runtime.get_process(process_a, info) &&
           info.state == lucid::kernel::ProcessState::Terminated,
           "record process termination");
    expect(runtime.handle_count() == 0U,
           "close all process-owned handles during termination");
    expect(!runtime.resolve_handle(process_a, handle,
                                   lucid::kernel::KernelObjectType::Session, object_id),
           "prevent handle use after owner process termination");
    expect(!runtime.create_thread(process_a, 0x5000U, 0xC000U, 5U, 0U,
                                  handle, &error),
           "prevent thread creation in a terminated process");
}
} // namespace

int main() {
    test_process_and_thread_creation();
    test_priority_scheduling_and_waits();
    test_process_owned_handles();
    if (failures != 0) {
        std::cerr << failures << " kernel runtime test(s) failed.\n";
        return 1;
    }
    std::cout << "All kernel runtime tests passed.\n";
    return 0;
}
