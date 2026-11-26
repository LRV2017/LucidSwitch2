#include "lucid/kernel/runtime.hpp"

#include <limits>
#include <utility>

namespace lucid::kernel {
namespace {
void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}
} // namespace

bool Runtime::process_is_active(const std::uint32_t id) const noexcept {
    const auto found = processes_.find(id);
    return found != processes_.end() && found->second.state == ProcessState::Active;
}

bool Runtime::create_process(
    const std::string& name, std::uint32_t& process_id, std::string* error) {
    if (name.empty() || name.size() > 64U) {
        set_error(error, "Process names must contain between 1 and 64 characters.");
        return false;
    }
    if (processes_.size() >= MaximumProcesses ||
        next_process_id_ == std::numeric_limits<std::uint32_t>::max()) {
        set_error(error, "The bounded process table is full.");
        return false;
    }
    ProcessInfo process{};
    process.id = next_process_id_++;
    process.name = name;
    process.state = ProcessState::Active;
    process_id = process.id;
    processes_.emplace(process.id, std::move(process));
    return true;
}

bool Runtime::enqueue_ready(ThreadInfo& thread, std::string* error) {
    if (next_ready_order_ == std::numeric_limits<std::uint64_t>::max()) {
        set_error(error, "The scheduler ready-order counter is exhausted.");
        return false;
    }
    thread.state = ThreadState::Ready;
    thread.wait_key = 0U;
    thread.ready_order = next_ready_order_++;
    return true;
}

bool Runtime::create_thread(
    const std::uint32_t process_id, const std::uint64_t entry_point,
    const std::uint64_t stack_pointer, const std::uint8_t priority,
    const std::uint64_t argument, std::uint32_t& thread_id,
    std::string* error) {
    if (!process_is_active(process_id)) {
        set_error(error, "The target process does not exist or has terminated.");
        return false;
    }
    if (entry_point == 0U || (entry_point & 3U) != 0U) {
        set_error(error, "Thread entry points must be nonzero and 4-byte aligned.");
        return false;
    }
    if (stack_pointer == 0U || (stack_pointer & 0xFULL) != 0U) {
        set_error(error, "Thread stack pointers must be nonzero and 16-byte aligned.");
        return false;
    }
    if (threads_.size() >= MaximumThreads ||
        next_thread_id_ == std::numeric_limits<std::uint32_t>::max()) {
        set_error(error, "The bounded thread table is full.");
        return false;
    }
    if (next_ready_order_ == std::numeric_limits<std::uint64_t>::max()) {
        set_error(error, "The scheduler ready-order counter is exhausted.");
        return false;
    }
    ThreadInfo thread{};
    thread.id = next_thread_id_++;
    thread.process_id = process_id;
    thread.context.program_counter = entry_point;
    thread.context.sp = stack_pointer;
    thread.context.x[0] = argument;
    thread.priority = priority;
    thread.ready_order = next_ready_order_++;
    thread.state = ThreadState::Ready;
    thread_id = thread.id;
    threads_.emplace(thread.id, std::move(thread));
    return true;
}

bool Runtime::dispatch_ready(std::uint32_t& thread_id) {
    ThreadInfo* selected = nullptr;
    for (auto& [id, thread] : threads_) {
        (void)id;
        if (thread.state != ThreadState::Ready) continue;
        if (selected == nullptr ||
            thread.priority < selected->priority ||
            (thread.priority == selected->priority &&
             thread.ready_order < selected->ready_order)) {
            selected = &thread;
        }
    }
    if (selected == nullptr) {
        current_thread_id_ = 0U;
        thread_id = 0U;
        return true;
    }
    selected->state = ThreadState::Running;
    current_thread_id_ = selected->id;
    thread_id = selected->id;
    return true;
}

bool Runtime::schedule_next(
    std::uint32_t& thread_id, std::string* error) {
    if (current_thread_id_ != 0U) {
        auto current = threads_.find(current_thread_id_);
        if (current == threads_.end() ||
            current->second.state != ThreadState::Running) {
            set_error(error, "The scheduler current-thread state is inconsistent.");
            return false;
        }
        if (!enqueue_ready(current->second, error)) return false;
        current_thread_id_ = 0U;
    }
    return dispatch_ready(thread_id);
}

bool Runtime::yield_current(
    std::uint32_t& next_thread_id, std::string* error) {
    if (current_thread_id_ == 0U) {
        set_error(error, "There is no running thread to yield.");
        return false;
    }
    return schedule_next(next_thread_id, error);
}

bool Runtime::block_current(
    const std::uint64_t wait_key, std::uint32_t& next_thread_id,
    std::string* error) {
    if (current_thread_id_ == 0U || wait_key == 0U) {
        set_error(error, "Blocking requires a running thread and a nonzero wait key.");
        return false;
    }
    auto current = threads_.find(current_thread_id_);
    if (current == threads_.end() ||
        current->second.state != ThreadState::Running) {
        set_error(error, "The scheduler current-thread state is inconsistent.");
        return false;
    }
    current->second.state = ThreadState::Blocked;
    current->second.wait_key = wait_key;
    current_thread_id_ = 0U;
    return dispatch_ready(next_thread_id);
}

std::size_t Runtime::wake_waiters(const std::uint64_t wait_key) {
    if (wait_key == 0U) return 0U;
    std::size_t awakened = 0U;
    for (auto& [id, thread] : threads_) {
        (void)id;
        if (thread.state != ThreadState::Blocked ||
            thread.wait_key != wait_key) continue;
        if (!enqueue_ready(thread, nullptr)) break;
        ++awakened;
    }
    return awakened;
}

bool Runtime::terminate_current(
    std::uint32_t& next_thread_id, std::string* error) {
    if (current_thread_id_ == 0U) {
        set_error(error, "There is no running thread to terminate.");
        return false;
    }
    auto current = threads_.find(current_thread_id_);
    if (current == threads_.end() ||
        current->second.state != ThreadState::Running) {
        set_error(error, "The scheduler current-thread state is inconsistent.");
        return false;
    }
    current->second.state = ThreadState::Terminated;
    current->second.wait_key = 0U;
    current_thread_id_ = 0U;
    return dispatch_ready(next_thread_id);
}

bool Runtime::terminate_process(
    const std::uint32_t process_id, std::string* error) {
    auto process = processes_.find(process_id);
    if (process == processes_.end()) {
        set_error(error, "The process does not exist.");
        return false;
    }
    if (process->second.state == ProcessState::Terminated) return true;
    process->second.state = ProcessState::Terminated;
    for (auto& [id, thread] : threads_) {
        (void)id;
        if (thread.process_id != process_id) continue;
        if (thread.id == current_thread_id_) current_thread_id_ = 0U;
        thread.state = ThreadState::Terminated;
        thread.wait_key = 0U;
    }
    for (auto handle = handles_.begin(); handle != handles_.end();) {
        if (handle->second.owner_process == process_id)
            handle = handles_.erase(handle);
        else
            ++handle;
    }
    return true;
}

bool Runtime::get_process(
    const std::uint32_t id, ProcessInfo& output) const {
    const auto found = processes_.find(id);
    if (found == processes_.end()) return false;
    output = found->second;
    return true;
}

bool Runtime::get_thread(
    const std::uint32_t id, ThreadInfo& output) const {
    const auto found = threads_.find(id);
    if (found == threads_.end()) return false;
    output = found->second;
    return true;
}

std::uint32_t Runtime::current_thread_id() const noexcept {
    return current_thread_id_;
}
std::size_t Runtime::process_count() const noexcept { return processes_.size(); }
std::size_t Runtime::thread_count() const noexcept { return threads_.size(); }

bool Runtime::create_handle(
    const std::uint32_t owner_process, const KernelObjectType type,
    const std::uint64_t object_id, std::uint32_t& handle,
    std::string* error) {
    if (!process_is_active(owner_process)) {
        set_error(error, "Kernel handles can only be created for an active process.");
        return false;
    }
    if (object_id == 0U) {
        set_error(error, "Kernel object identifiers must be nonzero.");
        return false;
    }
    if (handles_.size() >= MaximumHandles ||
        next_handle_ == std::numeric_limits<std::uint32_t>::max()) {
        set_error(error, "The bounded kernel handle table is full.");
        return false;
    }
    HandleInfo info{};
    info.value = next_handle_++;
    info.owner_process = owner_process;
    info.type = type;
    info.object_id = object_id;
    handle = info.value;
    handles_.emplace(info.value, info);
    return true;
}

bool Runtime::resolve_handle(
    const std::uint32_t owner_process, const std::uint32_t handle,
    const KernelObjectType expected_type, std::uint64_t& object_id) const {
    const auto found = handles_.find(handle);
    if (found == handles_.end() ||
        found->second.owner_process != owner_process ||
        found->second.type != expected_type ||
        !process_is_active(owner_process)) return false;
    object_id = found->second.object_id;
    return true;
}

bool Runtime::close_handle(
    const std::uint32_t owner_process, const std::uint32_t handle) {
    const auto found = handles_.find(handle);
    if (found == handles_.end() ||
        found->second.owner_process != owner_process) return false;
    handles_.erase(found);
    return true;
}

std::size_t Runtime::handle_count() const noexcept { return handles_.size(); }

} // namespace lucid::kernel
