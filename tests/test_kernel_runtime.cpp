#include "lucid/kernel/runtime.hpp"

#include <array>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void expect(const bool condition, const char* message) {
    if (condition) std::cout << "[PASS] " << message << "\n";
    else { ++failures; std::cerr << "[FAIL] " << message << "\n"; }
}

void put_elf16(std::vector<std::uint8_t>& bytes,const std::size_t offset,
                 const std::uint16_t value){
    bytes[offset]=static_cast<std::uint8_t>(value&0xFFU);
    bytes[offset+1U]=static_cast<std::uint8_t>(value>>8U);
}
void put_elf32(std::vector<std::uint8_t>& bytes,const std::size_t offset,
               const std::uint32_t value){
    for(std::size_t i=0U;i<4U;++i)
        bytes[offset+i]=static_cast<std::uint8_t>((value>>(i*8U))&0xFFU);
}
void put_elf64(std::vector<std::uint8_t>& bytes,const std::size_t offset,
               const std::uint64_t value){
    for(std::size_t i=0U;i<8U;++i)
        bytes[offset+i]=static_cast<std::uint8_t>((value>>(i*8U))&0xFFU);
}
std::vector<std::uint8_t> make_static_a64_elf(){
    std::vector<std::uint8_t> bytes(0x104U,0U);
    bytes[0]=0x7FU;bytes[1]='E';bytes[2]='L';bytes[3]='F';
    bytes[4]=2U;bytes[5]=1U;bytes[6]=1U;
    put_elf16(bytes,16U,2U);put_elf16(bytes,18U,183U);
    put_elf32(bytes,20U,1U);put_elf64(bytes,24U,0x100000U);
    put_elf64(bytes,32U,64U);
    put_elf16(bytes,52U,64U);put_elf16(bytes,54U,56U);
    put_elf16(bytes,56U,1U);
    put_elf32(bytes,64U,1U);put_elf32(bytes,68U,5U);
    put_elf64(bytes,72U,0x100U);
    put_elf64(bytes,80U,0x100000U);put_elf64(bytes,88U,0x100000U);
    put_elf64(bytes,96U,4U);put_elf64(bytes,104U,16U);
    put_elf64(bytes,112U,0x100U);
    bytes[0x100U]=0x1FU;bytes[0x101U]=0x20U;
    bytes[0x102U]=0x03U;bytes[0x103U]=0xD5U; // NOP
    return bytes;
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
void test_static_elf_loads_into_private_process_and_runs() {
    using namespace lucid;
    kernel::Runtime runtime;
    std::uint32_t process=0U,thread=0U,scheduled=0U;
    std::string error;
    expect(runtime.create_process("static-elf-process",process,&error),
        "create process for integrated static ELF loader test");
    const auto image=make_static_a64_elf();
    constexpr std::uint64_t stack_base=0x0000F00000000000ULL;
    constexpr std::size_t stack_size=64U*1024U;
    switch2::HomebrewElfLoadInfo info{};
    expect(runtime.load_process_elf(process,image,stack_base,stack_size,info,&error),
        "load an AArch64 static ELF and stack into the process-owned memory map");
    expect(info.entry_point==0x100000U&&info.loaded_segments==1U&&
           info.total_memory_bytes==16U,
        "report loaded ELF entry and process segment memory");
    expect(runtime.process_memory_bytes(process)==stack_size+16U,
        "account both ELF segments and the process stack against its memory budget");
    std::array<std::uint8_t,16U> code_and_bss{};
    expect(runtime.read_process_memory(process,info.entry_point,code_and_bss,&error)&&
           code_and_bss[0]==0x1FU&&code_and_bss[1]==0x20U&&
           code_and_bss[2]==0x03U&&code_and_bss[3]==0xD5U&&
           code_and_bss[15]==0U,
        "verify ELF code bytes and zero-filled BSS in the process address space");
    std::uint32_t stack_thread=0U;
    expect(runtime.create_thread(process,info.entry_point,stack_base+stack_size,
        8U,0U,stack_thread,&error)&&
        runtime.schedule_next(scheduled,&error)&&scheduled==stack_thread,
        "create and schedule the process main thread at the ELF entry point");
    kernel::ThreadRunReport report{};
    expect(runtime.run_current(1U,report,&error)&&
           report.instructions_executed==1U&&report.instruction_budget_exhausted,
        "execute the ELF entry instruction through the process-local A64 block cache");
    kernel::ThreadInfo thread_info{};
    expect(runtime.get_thread(stack_thread,thread_info)&&
           thread_info.state==kernel::ThreadState::Running&&
           thread_info.context.program_counter==info.entry_point+4U,
        "preserve the ELF thread CPU context after its first instruction");
    expect(!runtime.write_process_memory(process,info.entry_point,
        std::span<const std::uint8_t>(code_and_bss.data(),1U),&error),
        "keep ELF executable text read-only after process loading");
    expect(runtime.terminate_process(process,&error)&&
           runtime.process_memory_bytes(process)==0U,
        "release process ELF and stack mappings on process exit");

    kernel::Runtime rejected_runtime;
    std::uint32_t rejected_process=0U;
    expect(rejected_runtime.create_process("rejected-elf",rejected_process,&error),
        "create process for invalid ELF rollback test");
    auto truncated=image;
    truncated.resize(80U);
    switch2::HomebrewElfLoadInfo rejected_info{};
    expect(!rejected_runtime.load_process_elf(rejected_process,truncated,
        stack_base,stack_size,rejected_info,&error),
        "reject a truncated process ELF before executable launch");
    expect(rejected_runtime.process_memory_bytes(rejected_process)==0U,
        "roll back the reserved process stack after ELF load rejection");
}

void test_process_address_space_isolation_and_execution() {
    using namespace lucid;
    kernel::Runtime runtime;
    std::string error;
    std::uint32_t process_a=0U,process_b=0U,thread_a=0U,thread_b=0U,scheduled=0U;
    expect(runtime.create_process("isolated-a",process_a,&error)&&
           runtime.create_process("isolated-b",process_b,&error),
        "create two isolated guest processes");
    expect(runtime.map_process_region(process_a,0x1000U,0x1000U,
        core::MemoryPermission::ReadWriteExecute,&error)&&
           runtime.map_process_region(process_b,0x1000U,0x1000U,
        core::MemoryPermission::ReadWriteExecute,&error),
        "map the same virtual address independently in both processes");
    const std::array<std::uint32_t,3U> code_a{0xD2800540U,0x91002000U,0x14000000U};
    const std::array<std::uint32_t,2U> code_b{0xD2800120U,0x14000000U};
    std::array<std::uint8_t,12U> bytes_a{};
    std::array<std::uint8_t,8U> bytes_b{};
    for(std::size_t i=0U;i<code_a.size();++i)
        for(std::size_t b=0U;b<4U;++b)
            bytes_a[i*4U+b]=static_cast<std::uint8_t>((code_a[i]>>(b*8U))&0xFFU);
    for(std::size_t i=0U;i<code_b.size();++i)
        for(std::size_t b=0U;b<4U;++b)
            bytes_b[i*4U+b]=static_cast<std::uint8_t>((code_b[i]>>(b*8U))&0xFFU);
    expect(runtime.write_process_memory(process_a,0x1000U,bytes_a,&error)&&
           runtime.write_process_memory(process_b,0x1000U,bytes_b,&error),
        "write different code into matching virtual addresses in each process");
    std::array<std::uint8_t,12U> read_a{};
    expect(runtime.read_process_memory(process_a,0x1000U,read_a,&error)&&
           read_a==bytes_a,"read process A code from its own memory map");
    std::array<std::uint8_t,12U> read_b{};
    expect(runtime.read_process_memory(process_b,0x1000U,read_b,&error)&&
           read_b[0]==bytes_b[0]&&read_b[4]==bytes_b[4],
        "read process B code from its independent memory map");
    expect(runtime.create_thread(process_a,0x1000U,0x8000U,5U,0U,thread_a,&error)&&
           runtime.create_thread(process_b,0x1000U,0x9000U,10U,0U,thread_b,&error),
        "create threads owned by each isolated process");
    expect(runtime.schedule_next(scheduled,&error)&&scheduled==thread_a,
        "dispatch process A thread first");
    kernel::ThreadRunReport report{};
    expect(runtime.run_current(2U,report,&error)&&report.instructions_executed==2U,
        "execute process A thread from its private address space");
    kernel::ThreadInfo info_a{},info_b{};
    expect(runtime.get_thread(thread_a,info_a)&&info_a.context.x[0]==50U,
        "process A thread receives only its own instruction results");
    expect(runtime.block_current(0xAAU,scheduled,&error)&&scheduled==thread_b,
        "block process A and dispatch process B");
    expect(runtime.run_current(2U,report,&error)&&report.instructions_executed==2U,
        "execute process B thread using the same virtual PC in another address space");
    expect(runtime.get_thread(thread_b,info_b)&&info_b.context.x[0]==9U,
        "process B executes its independent instruction bytes");
    expect(runtime.get_thread(thread_a,info_a)&&info_a.context.x[0]==50U,
        "process B execution does not overwrite process A CPU state");
    expect(runtime.process_memory_bytes(process_a)==0x1000U&&
           runtime.process_memory_bytes(process_b)==0x1000U,
        "track per-process mapped memory usage");
    expect(!runtime.map_process_region(process_a,0x5000U,
        kernel::MaximumProcessMemoryBytes+1U,core::MemoryPermission::ReadWrite,&error)&&
        runtime.process_memory_bytes(process_a)==0x1000U,
        "reject a process mapping that exceeds its 512 MiB memory budget");
    expect(runtime.protect_process_region(process_b,0x1000U,0x1000U,
        core::MemoryPermission::Read|core::MemoryPermission::Execute,&error),
        "apply process-local executable read-only permissions");
    expect(!runtime.write_process_memory(process_b,0x1000U,bytes_b,&error),
        "reject writes after a process memory region becomes read-only");
    expect(runtime.terminate_process(process_b,&error),
        "terminate a process and release its private address space");
    expect(!runtime.read_process_memory(process_b,0x1000U,read_b,&error)&&
           runtime.process_memory_bytes(process_b)==0U,
        "prevent access to memory after process termination");
    expect(runtime.read_process_memory(process_a,0x1000U,read_a,&error)&&
           read_a==bytes_a,"preserve process A memory after process B exits");
    expect(runtime.unmap_process_region(process_a,0x1000U,0x1000U,&error)&&
           runtime.process_memory_bytes(process_a)==0U,
        "unmap a process region and release its memory budget");
    expect(!runtime.read_process_memory(process_a,0x1000U,read_a,&error),
        "reject accesses after a process region has been unmapped");
}

void test_scheduled_threads_execute_a64_code() {
    using namespace lucid;
    core::MemoryBus memory;
    std::string error;
    expect(memory.map_region(0x1000U,0x1000U,&error,
        core::MemoryPermission::ReadWriteExecute),
        "map executable memory for guest-thread execution");
    const std::array<std::uint32_t,3U> code{
        0xD2800540U, // MOVZ X0,#42
        0x91002000U, // ADD X0,X0,#8
        0x14000000U  // B .
    };
    std::array<std::uint8_t,12U> bytes{};
    for(std::size_t i=0U;i<code.size();++i)
        for(std::size_t b=0U;b<4U;++b)
            bytes[i*4U+b]=static_cast<std::uint8_t>((code[i]>>(b*8U))&0xFFU);
    expect(memory.write(0x1000U,bytes,&error),"write scheduled guest-thread A64 program");

    kernel::Runtime runtime;
    std::uint32_t process=0U,thread=0U,scheduled=0U;
    expect(runtime.create_process("a64-thread",process,&error),
        "create process for executable guest-thread test");
    expect(runtime.create_thread(process,0x1000U,0x8000U,8U,0U,thread,&error),
        "create a scheduled A64 guest thread");
    expect(runtime.schedule_next(scheduled,&error)&&scheduled==thread,
        "dispatch the A64 guest thread");
    kernel::ThreadRunReport report{};
    expect(runtime.run_current(memory,2U,report,&error),
        "execute the scheduled thread through the cached A64 interpreter");
    kernel::ThreadInfo info{};
    expect(runtime.get_thread(thread,info)&&info.state==kernel::ThreadState::Running&&
           info.context.x[0]==50U&&info.context.program_counter==0x1008U,
        "preserve CPU registers and PC after a bounded guest-thread run");
    expect(report.instructions_executed==2U&&report.instruction_budget_exhausted,
        "report a clean instruction-budget stop");
    expect(runtime.run_current(memory,3U,report,&error)&&
           report.instructions_executed==3U&&report.instruction_budget_exhausted,
        "resume the same running guest thread across a second budget");

    core::MemoryBus fault_memory;
    expect(fault_memory.map_region(0x3000U,0x1000U,&error),
        "map guest memory for unsupported-instruction fault test");
    const std::array<std::uint8_t,4U> bad{0xFFU,0xFFU,0xFFU,0xFFU};
    expect(fault_memory.write(0x3000U,bad,&error),
        "write unsupported A64 instruction for fault test");
    kernel::Runtime fault_runtime;
    std::uint32_t fault_process=0U,fault_thread=0U;
    expect(fault_runtime.create_process("fault-test",fault_process,&error)&&
           fault_runtime.create_thread(fault_process,0x3000U,0x9000U,1U,0U,
                                        fault_thread,&error)&&
           fault_runtime.schedule_next(scheduled,&error)&&scheduled==fault_thread,
        "create and dispatch a guest thread with unsupported code");
    expect(fault_runtime.run_current(fault_memory,1U,report,&error),
        "execute unsupported guest-thread instruction and report its fault");
    expect(report.stop_reason==cpu::StepResult::UnsupportedInstruction,
        "preserve the unsupported-instruction stop reason");
    expect(fault_runtime.get_thread(fault_thread,info)&&
           info.state==kernel::ThreadState::Faulted&&
           fault_runtime.current_thread_id()==0U,
        "faulted guest thread leaves the running slot without corrupting the scheduler");
}

void test_event_semaphore_and_mutex_primitives() {
    lucid::kernel::Runtime runtime;
    std::uint32_t process=0U, thread_a=0U, thread_b=0U, next=0U;
    std::string error;
    expect(runtime.create_process("sync-tests",process,&error),
        "create process for kernel synchronization tests");
    expect(runtime.create_thread(process,0x1000U,0x8000U,5U,0U,thread_a,&error)&&
           runtime.create_thread(process,0x2000U,0x9000U,10U,0U,thread_b,&error),
        "create two threads for event and mutex behavior");
    expect(runtime.schedule_next(next,&error)&&next==thread_a,
        "schedule first synchronization test thread");

    std::uint32_t event=0U;
    bool signaled=false;
    expect(runtime.create_event(process,false,false,event,&error),
        "create manual-reset event");
    expect(runtime.try_wait_event(process,event,signaled,&error)&&!signaled,
        "manual-reset event begins unsignaled");
    std::uint64_t event_id=0U;
    expect(runtime.resolve_handle(process,event,lucid::kernel::KernelObjectType::Event,event_id),
        "resolve event wait key");
    expect(runtime.block_current(event_id,next,&error)&&next==thread_b,
        "block current thread on event object wait key");
    expect(runtime.signal_event(process,event,&error),
        "signal event and wake matching waiters");
    lucid::kernel::ThreadInfo blocked_info{};
    expect(runtime.get_thread(thread_a,blocked_info)&&
           blocked_info.state==lucid::kernel::ThreadState::Ready,
        "event signal returns blocked thread to ready queue");
    expect(runtime.try_wait_event(process,event,signaled,&error)&&signaled,
        "manual-reset event remains signaled after observation");
    expect(runtime.try_wait_event(process,event,signaled,&error)&&signaled,
        "manual-reset event remains signaled for subsequent waiters");
    expect(runtime.clear_event(process,event,&error),
        "clear a manual-reset event");
    expect(runtime.try_wait_event(process,event,signaled,&error)&&!signaled,
        "cleared event returns to unsignaled state");

    std::uint32_t auto_event=0U;
    expect(runtime.create_event(process,true,true,auto_event,&error),
        "create initially signaled auto-reset event");
    expect(runtime.try_wait_event(process,auto_event,signaled,&error)&&signaled,
        "consume a signaled auto-reset event");
    expect(runtime.try_wait_event(process,auto_event,signaled,&error)&&!signaled,
        "auto-reset event clears after one successful wait");

    std::uint32_t semaphore=0U, previous=0U;
    bool acquired=false;
    expect(runtime.create_semaphore(process,1U,2U,semaphore,&error),
        "create semaphore with bounded initial count");
    expect(runtime.try_wait_semaphore(process,semaphore,acquired,&error)&&acquired,
        "consume an available semaphore permit");
    expect(runtime.try_wait_semaphore(process,semaphore,acquired,&error)&&!acquired,
        "report unavailable semaphore without blocking host thread");
    expect(runtime.release_semaphore(process,semaphore,2U,previous,&error)&&previous==0U,
        "release multiple permits and report previous count");
    expect(!runtime.release_semaphore(process,semaphore,1U,previous,&error),
        "reject semaphore release beyond configured maximum");
    expect(runtime.try_wait_semaphore(process,semaphore,acquired,&error)&&acquired,
        "consume released semaphore permit");

    std::uint32_t mutex=0U;
    expect(runtime.create_mutex(process,true,mutex,&error),
        "create recursive kernel mutex");
    expect(runtime.try_lock_mutex(process,mutex,thread_a,acquired,&error)&&acquired,
        "acquire an unowned mutex");
    expect(runtime.try_lock_mutex(process,mutex,thread_a,acquired,&error)&&acquired,
        "allow the owning thread to recursively acquire a recursive mutex");
    expect(!runtime.unlock_mutex(process,mutex,thread_b,&error),
        "reject mutex release by a non-owner thread");
    expect(runtime.unlock_mutex(process,mutex,thread_a,&error),
        "release one recursive mutex acquisition");
    expect(runtime.try_lock_mutex(process,mutex,thread_b,acquired,&error)&&!acquired,
        "prevent a competing thread from acquiring a held mutex");
    expect(runtime.unlock_mutex(process,mutex,thread_a,&error),
        "release the final recursive mutex acquisition");
    expect(runtime.try_lock_mutex(process,mutex,thread_b,acquired,&error)&&acquired,
        "allow another thread to acquire a released mutex");
    expect(runtime.unlock_mutex(process,mutex,thread_b,&error),
        "allow mutex owner to release ownership");

    expect(runtime.close_handle(process,event)&&runtime.close_handle(process,auto_event)&&
           runtime.close_handle(process,semaphore)&&runtime.close_handle(process,mutex),
        "close all synchronization-object handles");
    expect(runtime.handle_count()==0U,
        "remove synchronization handles and prune unreferenced objects");
    expect(!runtime.try_wait_event(process,event,signaled,&error),
        "reject event operations after handle closure");
}

} // namespace

int main() {
    test_process_and_thread_creation();
    test_priority_scheduling_and_waits();
    test_process_owned_handles();
    test_event_semaphore_and_mutex_primitives();
    test_scheduled_threads_execute_a64_code();
    test_process_address_space_isolation_and_execution();
    test_static_elf_loads_into_private_process_and_runs();
    if (failures != 0) {
        std::cerr << failures << " kernel runtime test(s) failed.\n";
        return 1;
    }
    std::cout << "All kernel runtime tests passed.\n";
    return 0;
}
