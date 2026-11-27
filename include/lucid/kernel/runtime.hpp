#pragma once

#include "lucid/cpu/block_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>

namespace lucid::kernel {

inline constexpr std::size_t MaximumProcesses = 64U;
inline constexpr std::size_t MaximumThreads = 256U;
inline constexpr std::size_t MaximumHandles = 4096U;
inline constexpr std::size_t MaximumProcessMemoryBytes = 512U * 1024U * 1024U;

enum class ProcessState { Active, Terminated };
enum class ThreadState { Ready, Running, Blocked, Terminated, Faulted };
enum class KernelObjectType { Event, Mutex, Semaphore, Session, TransferMemory };

struct ProcessInfo {
    std::uint32_t id{};
    std::string name;
    ProcessState state{ProcessState::Active};
};

struct ThreadInfo {
    std::uint32_t id{};
    std::uint32_t process_id{};
    cpu::CpuContext context{};
    ThreadState state{ThreadState::Ready};
    std::uint8_t priority{32U};
    std::uint64_t wait_key{};
    std::uint64_t ready_order{};
    cpu::StepResult last_step_result{cpu::StepResult::Executed};
};

enum class KernelSyncStatus { Signaled, WouldBlock, InvalidHandle };

struct ThreadRunReport {
    std::uint64_t instructions_executed{};
    cpu::StepResult stop_reason{cpu::StepResult::Executed};
    bool instruction_budget_exhausted{};
};

struct HandleInfo {
    std::uint32_t value{};
    std::uint32_t owner_process{};
    KernelObjectType type{KernelObjectType::Event};
    std::uint64_t object_id{};
};

// Deterministic cooperative process/thread and handle-table substrate.
// This is not a Horizon OS implementation or a binary-compatible kernel.
class Runtime final {
public:
    [[nodiscard]] bool create_process(
        const std::string& name, std::uint32_t& process_id,
        std::string* error = nullptr);
    [[nodiscard]] bool create_thread(
        std::uint32_t process_id, std::uint64_t entry_point,
        std::uint64_t stack_pointer, std::uint8_t priority,
        std::uint64_t argument, std::uint32_t& thread_id,
        std::string* error = nullptr);

    // Lower numeric priority wins; equal-priority threads are FIFO.
    [[nodiscard]] bool schedule_next(
        std::uint32_t& thread_id, std::string* error = nullptr);
    [[nodiscard]] bool yield_current(
        std::uint32_t& next_thread_id, std::string* error = nullptr);
    // Execute the currently scheduled thread against shared guest memory.
    // A budget stop leaves it Running; CPU faults/halt update its lifecycle.
    [[nodiscard]] bool run_current(core::MemoryBus& memory,
        std::uint64_t instruction_budget, ThreadRunReport& report,
        std::string* error = nullptr);
    [[nodiscard]] bool map_process_region(std::uint32_t process_id,
        std::uint64_t base, std::size_t size, core::MemoryPermission permissions,
        std::string* error = nullptr);
    [[nodiscard]] bool protect_process_region(std::uint32_t process_id,
        std::uint64_t base, std::size_t size, core::MemoryPermission permissions,
        std::string* error = nullptr);
    [[nodiscard]] bool unmap_process_region(std::uint32_t process_id,
        std::uint64_t base, std::size_t size, std::string* error = nullptr);
    [[nodiscard]] bool read_process_memory(std::uint32_t process_id,
        std::uint64_t address, std::span<std::uint8_t> output,
        std::string* error = nullptr) const;
    [[nodiscard]] bool write_process_memory(std::uint32_t process_id,
        std::uint64_t address, std::span<const std::uint8_t> input,
        std::string* error = nullptr);
    [[nodiscard]] std::size_t process_memory_bytes(std::uint32_t process_id) const noexcept;
    [[nodiscard]] bool run_current(std::uint64_t instruction_budget,
        ThreadRunReport& report, std::string* error = nullptr);
    [[nodiscard]] bool block_current(
        std::uint64_t wait_key, std::uint32_t& next_thread_id,
        std::string* error = nullptr);
    [[nodiscard]] std::size_t wake_waiters(std::uint64_t wait_key);
    [[nodiscard]] bool wake_one_waiter(std::uint64_t wait_key,
                                       std::uint32_t& awakened_thread_id);
    [[nodiscard]] bool terminate_current(
        std::uint32_t& next_thread_id, std::string* error = nullptr);
    [[nodiscard]] bool terminate_process(
        std::uint32_t process_id, std::string* error = nullptr);

    [[nodiscard]] bool get_process(std::uint32_t id, ProcessInfo& output) const;
    [[nodiscard]] bool get_thread(std::uint32_t id, ThreadInfo& output) const;
    [[nodiscard]] std::uint32_t current_thread_id() const noexcept;
    [[nodiscard]] std::size_t process_count() const noexcept;
    [[nodiscard]] std::size_t thread_count() const noexcept;

    [[nodiscard]] bool create_handle(
        std::uint32_t owner_process, KernelObjectType type,
        std::uint64_t object_id, std::uint32_t& handle,
        std::string* error = nullptr);
    [[nodiscard]] bool resolve_handle(
        std::uint32_t owner_process, std::uint32_t handle,
        KernelObjectType expected_type, std::uint64_t& object_id) const;
    [[nodiscard]] bool close_handle(
        std::uint32_t owner_process, std::uint32_t handle);
    [[nodiscard]] std::size_t handle_count() const noexcept;

    // Host-side synchronization-object behavior. Wait operations are
    // nonblocking probes; a caller that receives WouldBlock can block its
    // current thread on the resolved object_id and retry after a signal.
    [[nodiscard]] bool create_event(std::uint32_t owner_process,
        bool initially_signaled, bool auto_reset, std::uint32_t& handle,
        std::string* error = nullptr);
    [[nodiscard]] bool signal_event(std::uint32_t owner_process,
        std::uint32_t handle, std::string* error = nullptr);
    [[nodiscard]] bool clear_event(std::uint32_t owner_process,
        std::uint32_t handle, std::string* error = nullptr);
    [[nodiscard]] bool try_wait_event(std::uint32_t owner_process,
        std::uint32_t handle, bool& signaled, std::string* error = nullptr);

    [[nodiscard]] bool create_semaphore(std::uint32_t owner_process,
        std::uint32_t initial_count, std::uint32_t maximum_count,
        std::uint32_t& handle, std::string* error = nullptr);
    [[nodiscard]] bool release_semaphore(std::uint32_t owner_process,
        std::uint32_t handle, std::uint32_t release_count,
        std::uint32_t& previous_count, std::string* error = nullptr);
    [[nodiscard]] bool try_wait_semaphore(std::uint32_t owner_process,
        std::uint32_t handle, bool& acquired, std::string* error = nullptr);

    [[nodiscard]] bool create_mutex(std::uint32_t owner_process,
        bool recursive, std::uint32_t& handle, std::string* error = nullptr);
    [[nodiscard]] bool try_lock_mutex(std::uint32_t owner_process,
        std::uint32_t handle, std::uint32_t thread_id, bool& acquired,
        std::string* error = nullptr);
    [[nodiscard]] bool unlock_mutex(std::uint32_t owner_process,
        std::uint32_t handle, std::uint32_t thread_id,
        std::string* error = nullptr);

private:
    [[nodiscard]] bool process_is_active(std::uint32_t id) const noexcept;
    [[nodiscard]] bool enqueue_ready(ThreadInfo& thread, std::string* error);
    [[nodiscard]] bool dispatch_ready(std::uint32_t& thread_id);
    [[nodiscard]] std::uint64_t allocate_sync_object_id() noexcept;
    void prune_sync_objects() noexcept;
    void release_owned_mutexes(std::uint32_t thread_id) noexcept;

    struct ProcessAddressSpace {
        std::unique_ptr<core::MemoryBus> memory;
        std::size_t mapped_bytes{};
    };

    struct SyncObject {
        KernelObjectType type{KernelObjectType::Event};
        bool signaled{};
        bool auto_reset{};
        std::uint32_t count{};
        std::uint32_t maximum_count{};
        bool recursive{};
        std::uint32_t owner_thread{};
        std::uint32_t recursion_depth{};
    };
    [[nodiscard]] SyncObject* find_sync_object(
        std::uint32_t owner_process, std::uint32_t handle,
        KernelObjectType expected_type, std::uint64_t& object_id) noexcept;

    std::map<std::uint32_t, ProcessInfo> processes_;
    std::map<std::uint32_t, ProcessAddressSpace> process_address_spaces_;
    std::map<std::uint32_t, ThreadInfo> threads_;
    std::map<std::uint32_t, HandleInfo> handles_;
    std::map<std::uint64_t, SyncObject> sync_objects_;
    std::map<std::uint32_t, cpu::A64BlockCache> shared_execution_caches_;
    std::map<std::uint32_t, cpu::A64BlockCache> process_execution_caches_;
    std::uint32_t next_process_id_{1U};
    std::uint32_t next_thread_id_{1U};
    std::uint32_t next_handle_{0x100U};
    std::uint32_t current_thread_id_{};
    std::uint64_t next_ready_order_{1U};
    std::uint64_t next_sync_object_id_{0x8000000000000001ULL};
};

} // namespace lucid::kernel
