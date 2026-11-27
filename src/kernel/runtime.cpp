#include "lucid/kernel/runtime.hpp"

#include <algorithm>

#include <limits>
#include <new>
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
    const std::uint32_t new_id = next_process_id_++;
    try {
        ProcessInfo process{};
        process.id = new_id;
        process.name = name;
        process.state = ProcessState::Active;
        auto memory = std::make_unique<core::MemoryBus>();
        processes_.emplace(new_id, std::move(process));
        process_address_spaces_.emplace(new_id,
            ProcessAddressSpace{std::move(memory), 0U});
    } catch (const std::bad_alloc&) {
        processes_.erase(new_id);
        process_address_spaces_.erase(new_id);
        set_error(error, "Not enough host memory to create the process address space.");
        return false;
    }
    process_id = new_id;
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

bool Runtime::run_current(core::MemoryBus& memory,
    const std::uint64_t instruction_budget, ThreadRunReport& report,
    std::string* error) {
    report = {};
    if (current_thread_id_ == 0U || instruction_budget == 0U) {
        set_error(error, "Running a guest thread requires a scheduled thread and a nonzero instruction budget.");
        return false;
    }
    const auto found = threads_.find(current_thread_id_);
    if (found == threads_.end() || found->second.state != ThreadState::Running ||
        !process_is_active(found->second.process_id)) {
        set_error(error, "The current guest-thread state is invalid.");
        return false;
    }

    const cpu::A64BlockRunReport execution = shared_execution_caches_[found->second.process_id].run(
        found->second.context, memory, instruction_budget);
    report.instructions_executed = execution.instructions_executed;
    report.stop_reason = execution.stop_reason;
    report.instruction_budget_exhausted = execution.instruction_budget_exhausted;
    found->second.last_step_result = execution.stop_reason;

    if (execution.stop_reason == cpu::StepResult::Halted) {
        found->second.state = ThreadState::Terminated;
        found->second.wait_key = 0U;
        current_thread_id_ = 0U;
        release_owned_mutexes(found->second.id);
    } else if (execution.stop_reason == cpu::StepResult::UnsupportedInstruction ||
               execution.stop_reason == cpu::StepResult::MemoryFault ||
               execution.stop_reason == cpu::StepResult::MisalignedProgramCounter ||
               execution.stop_reason == cpu::StepResult::SystemRegisterFault) {
        found->second.state = ThreadState::Faulted;
        found->second.wait_key = 0U;
        current_thread_id_ = 0U;
        release_owned_mutexes(found->second.id);
    }
    return true;
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

bool Runtime::wake_one_waiter(const std::uint64_t wait_key,
                                std::uint32_t& awakened_thread_id) {
    awakened_thread_id = 0U;
    if (wait_key == 0U) return false;
    ThreadInfo* selected = nullptr;
    for (auto& [id, thread] : threads_) {
        (void)id;
        if (thread.state != ThreadState::Blocked || thread.wait_key != wait_key ||
            !process_is_active(thread.process_id))
            continue;
        if (selected == nullptr ||
            thread.priority < selected->priority ||
            (thread.priority == selected->priority &&
             thread.ready_order < selected->ready_order))
            selected = &thread;
    }
    if (selected == nullptr || !enqueue_ready(*selected, nullptr)) return false;
    awakened_thread_id = selected->id;
    return true;
}

std::size_t Runtime::wake_waiters(const std::uint64_t wait_key) {
    if (wait_key == 0U) return 0U;
    std::size_t awakened = 0U;
    for (auto& [id, thread] : threads_) {
        (void)id;
        if (thread.state != ThreadState::Blocked ||
            thread.wait_key != wait_key || !process_is_active(thread.process_id)) continue;
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
    const std::uint32_t terminated_thread_id = current->second.id;
    current_thread_id_ = 0U;
    release_owned_mutexes(terminated_thread_id);
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
    for (const auto& [id, thread] : threads_) {
        if (thread.process_id == process_id) release_owned_mutexes(id);
    }
    for (auto handle = handles_.begin(); handle != handles_.end();) {
        if (handle->second.owner_process == process_id)
            handle = handles_.erase(handle);
        else
            ++handle;
    }
    prune_sync_objects();
    process_address_spaces_.erase(process_id);
    shared_execution_caches_.erase(process_id);
    process_execution_caches_.erase(process_id);
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

bool Runtime::map_process_region(const std::uint32_t process_id,
    const std::uint64_t base, const std::size_t size,
    const core::MemoryPermission permissions, std::string* error) {
    const auto space=process_address_spaces_.find(process_id);
    if(!process_is_active(process_id)||space==process_address_spaces_.end()){
        set_error(error,"The process has no active address space.");return false;
    }
    if(size==0U||size>MaximumProcessMemoryBytes-space->second.mapped_bytes){
        set_error(error,"The requested mapping exceeds the process memory budget.");return false;
    }
    if(!space->second.memory->map_region(base,size,error,permissions))return false;
    space->second.mapped_bytes+=size;
    return true;
}

bool Runtime::protect_process_region(const std::uint32_t process_id,
    const std::uint64_t base,const std::size_t size,
    const core::MemoryPermission permissions,std::string* error){
    const auto space=process_address_spaces_.find(process_id);
    if(!process_is_active(process_id)||space==process_address_spaces_.end()){
        set_error(error,"The process has no active address space.");return false;
    }
    return space->second.memory->protect_region(base,size,permissions,error);
}

bool Runtime::unmap_process_region(const std::uint32_t process_id,
    const std::uint64_t base,const std::size_t size,std::string* error){
    const auto space=process_address_spaces_.find(process_id);
    if(!process_is_active(process_id)||space==process_address_spaces_.end()){
        set_error(error,"The process has no active address space.");return false;
    }
    if(!space->second.memory->unmap_region(base,size,error))return false;
    space->second.mapped_bytes-=size;
    return true;
}

bool Runtime::read_process_memory(const std::uint32_t process_id,
    const std::uint64_t address,std::span<std::uint8_t> output,
    std::string* error) const{
    const auto space=process_address_spaces_.find(process_id);
    if(!process_is_active(process_id)||space==process_address_spaces_.end()){
        set_error(error,"The process has no active address space.");return false;
    }
    return space->second.memory->read(address,output,error);
}

bool Runtime::write_process_memory(const std::uint32_t process_id,
    const std::uint64_t address,std::span<const std::uint8_t> input,
    std::string* error){
    const auto space=process_address_spaces_.find(process_id);
    if(!process_is_active(process_id)||space==process_address_spaces_.end()){
        set_error(error,"The process has no active address space.");return false;
    }
    return space->second.memory->write(address,input,error);
}

std::size_t Runtime::process_memory_bytes(const std::uint32_t process_id) const noexcept{
    const auto space=process_address_spaces_.find(process_id);
    return space==process_address_spaces_.end()?0U:space->second.mapped_bytes;
}

bool Runtime::run_current(const std::uint64_t instruction_budget,
    ThreadRunReport& report,std::string* error){
    report={};
    if(current_thread_id_==0U||instruction_budget==0U){
        set_error(error,"Running a guest thread requires a scheduled thread and nonzero instruction budget.");
        return false;
    }
    const auto thread=threads_.find(current_thread_id_);
    if(thread==threads_.end()||thread->second.state!=ThreadState::Running||
       !process_is_active(thread->second.process_id)){
        set_error(error,"The current guest-thread state is invalid.");return false;
    }
    const auto space=process_address_spaces_.find(thread->second.process_id);
    if(space==process_address_spaces_.end()||space->second.memory==nullptr){
        set_error(error,"The current process has no address space.");return false;
    }
    const cpu::A64BlockRunReport execution=process_execution_caches_[thread->second.process_id].run(
        thread->second.context,*space->second.memory,instruction_budget);
    report.instructions_executed=execution.instructions_executed;
    report.stop_reason=execution.stop_reason;
    report.instruction_budget_exhausted=execution.instruction_budget_exhausted;
    thread->second.last_step_result=execution.stop_reason;
    if(execution.stop_reason==cpu::StepResult::Halted){
        thread->second.state=ThreadState::Terminated;
        thread->second.wait_key=0U;
        current_thread_id_=0U;
        release_owned_mutexes(thread->second.id);
    }else if(execution.stop_reason==cpu::StepResult::UnsupportedInstruction||
             execution.stop_reason==cpu::StepResult::MemoryFault||
             execution.stop_reason==cpu::StepResult::MisalignedProgramCounter||
             execution.stop_reason==cpu::StepResult::SystemRegisterFault){
        thread->second.state=ThreadState::Faulted;
        thread->second.wait_key=0U;
        current_thread_id_=0U;
        release_owned_mutexes(thread->second.id);
    }
    return true;
}

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
    prune_sync_objects();
    return true;
}

std::size_t Runtime::handle_count() const noexcept { return handles_.size(); }

std::uint64_t Runtime::allocate_sync_object_id() noexcept {
    const std::uint64_t result = next_sync_object_id_;
    if (next_sync_object_id_ != std::numeric_limits<std::uint64_t>::max())
        ++next_sync_object_id_;
    else
        next_sync_object_id_ = 0U;
    return result;
}

Runtime::SyncObject* Runtime::find_sync_object(
    const std::uint32_t owner_process, const std::uint32_t handle,
    const KernelObjectType expected_type, std::uint64_t& object_id) noexcept {
    if (!resolve_handle(owner_process, handle, expected_type, object_id))
        return nullptr;
    const auto found = sync_objects_.find(object_id);
    return found == sync_objects_.end() ? nullptr : &found->second;
}

void Runtime::release_owned_mutexes(const std::uint32_t thread_id) noexcept {
    for (auto& [object_id, object] : sync_objects_) {
        if (object.type != KernelObjectType::Mutex ||
            object.owner_thread != thread_id) continue;
        object.owner_thread = 0U;
        object.recursion_depth = 0U;
        std::uint32_t awakened = 0U;
        (void)wake_one_waiter(object_id, awakened);
    }
}

void Runtime::prune_sync_objects() noexcept {
    for (auto object = sync_objects_.begin(); object != sync_objects_.end();) {
        const bool referenced = std::any_of(handles_.begin(), handles_.end(),
            [&object](const auto& entry) {
                return entry.second.object_id == object->first;
            });
        if (!referenced) object = sync_objects_.erase(object);
        else ++object;
    }
}

bool Runtime::create_event(const std::uint32_t owner_process,
    const bool initially_signaled, const bool auto_reset,
    std::uint32_t& handle, std::string* error) {
    if (!process_is_active(owner_process) ||
        sync_objects_.size() >= MaximumHandles || next_sync_object_id_ == 0U) {
        set_error(error, "An event requires an active owner and available kernel-object capacity.");
        return false;
    }
    const std::uint64_t object_id = allocate_sync_object_id();
    SyncObject object{};
    object.type = KernelObjectType::Event;
    object.signaled = initially_signaled;
    object.auto_reset = auto_reset;
    sync_objects_.emplace(object_id, object);
    if (!create_handle(owner_process, KernelObjectType::Event, object_id, handle, error)) {
        sync_objects_.erase(object_id);
        return false;
    }
    return true;
}

bool Runtime::signal_event(const std::uint32_t owner_process,
    const std::uint32_t handle, std::string* error) {
    std::uint64_t object_id = 0U;
    SyncObject* object = find_sync_object(owner_process, handle,
                                          KernelObjectType::Event, object_id);
    if (object == nullptr) {
        set_error(error, "The event handle is invalid or does not reference a live event.");
        return false;
    }
    object->signaled = true;
    if (object->auto_reset) {
        std::uint32_t awakened = 0U;
        if (wake_one_waiter(object_id, awakened)) object->signaled = false;
    } else {
        (void)wake_waiters(object_id);
    }
    return true;
}

bool Runtime::clear_event(const std::uint32_t owner_process,
    const std::uint32_t handle, std::string* error) {
    std::uint64_t object_id = 0U;
    SyncObject* object = find_sync_object(owner_process, handle,
                                          KernelObjectType::Event, object_id);
    if (object == nullptr) {
        set_error(error, "The event handle is invalid or does not reference a live event.");
        return false;
    }
    object->signaled = false;
    return true;
}

bool Runtime::try_wait_event(const std::uint32_t owner_process,
    const std::uint32_t handle, bool& signaled, std::string* error) {
    signaled = false;
    std::uint64_t object_id = 0U;
    SyncObject* object = find_sync_object(owner_process, handle,
                                          KernelObjectType::Event, object_id);
    if (object == nullptr) {
        set_error(error, "The event handle is invalid or does not reference a live event.");
        return false;
    }
    signaled = object->signaled;
    if (signaled && object->auto_reset) object->signaled = false;
    return true;
}

bool Runtime::create_semaphore(const std::uint32_t owner_process,
    const std::uint32_t initial_count, const std::uint32_t maximum_count,
    std::uint32_t& handle, std::string* error) {
    if (!process_is_active(owner_process) || maximum_count == 0U ||
        initial_count > maximum_count || sync_objects_.size() >= MaximumHandles ||
        next_sync_object_id_ == 0U) {
        set_error(error, "Semaphore counts or owner are invalid, or kernel-object capacity is exhausted.");
        return false;
    }
    const std::uint64_t object_id = allocate_sync_object_id();
    SyncObject object{};
    object.type = KernelObjectType::Semaphore;
    object.count = initial_count;
    object.maximum_count = maximum_count;
    sync_objects_.emplace(object_id, object);
    if (!create_handle(owner_process, KernelObjectType::Semaphore, object_id,
                       handle, error)) {
        sync_objects_.erase(object_id);
        return false;
    }
    return true;
}

bool Runtime::release_semaphore(const std::uint32_t owner_process,
    const std::uint32_t handle, const std::uint32_t release_count,
    std::uint32_t& previous_count, std::string* error) {
    std::uint64_t object_id = 0U;
    SyncObject* object = find_sync_object(owner_process, handle,
                                          KernelObjectType::Semaphore, object_id);
    if (object == nullptr || release_count == 0U) {
        set_error(error, "The semaphore handle or release count is invalid.");
        return false;
    }
    if (release_count > object->maximum_count - object->count) {
        set_error(error, "Releasing would exceed the semaphore maximum count.");
        return false;
    }
    previous_count = object->count;
    object->count += release_count;
    (void)wake_waiters(object_id);
    return true;
}

bool Runtime::try_wait_semaphore(const std::uint32_t owner_process,
    const std::uint32_t handle, bool& acquired, std::string* error) {
    acquired = false;
    std::uint64_t object_id = 0U;
    SyncObject* object = find_sync_object(owner_process, handle,
                                          KernelObjectType::Semaphore, object_id);
    if (object == nullptr) {
        set_error(error, "The semaphore handle is invalid or does not reference a live semaphore.");
        return false;
    }
    if (object->count != 0U) {
        --object->count;
        acquired = true;
    }
    return true;
}

bool Runtime::create_mutex(const std::uint32_t owner_process,
    const bool recursive, std::uint32_t& handle, std::string* error) {
    if (!process_is_active(owner_process) || sync_objects_.size() >= MaximumHandles ||
        next_sync_object_id_ == 0U) {
        set_error(error, "A mutex requires an active owner and available kernel-object capacity.");
        return false;
    }
    const std::uint64_t object_id = allocate_sync_object_id();
    SyncObject object{};
    object.type = KernelObjectType::Mutex;
    object.recursive = recursive;
    sync_objects_.emplace(object_id, object);
    if (!create_handle(owner_process, KernelObjectType::Mutex, object_id, handle, error)) {
        sync_objects_.erase(object_id);
        return false;
    }
    return true;
}

bool Runtime::try_lock_mutex(const std::uint32_t owner_process,
    const std::uint32_t handle, const std::uint32_t thread_id,
    bool& acquired, std::string* error) {
    acquired = false;
    std::uint64_t object_id = 0U;
    SyncObject* object = find_sync_object(owner_process, handle,
                                          KernelObjectType::Mutex, object_id);
    const auto thread = threads_.find(thread_id);
    if (object == nullptr || thread == threads_.end() ||
        thread->second.process_id != owner_process ||
        thread->second.state == ThreadState::Terminated) {
        set_error(error, "Mutex handle or owning thread is invalid.");
        return false;
    }
    if (object->owner_thread == 0U) {
        object->owner_thread = thread_id;
        object->recursion_depth = 1U;
        acquired = true;
    } else if (object->owner_thread == thread_id && object->recursive &&
               object->recursion_depth < std::numeric_limits<std::uint32_t>::max()) {
        ++object->recursion_depth;
        acquired = true;
    }
    return true;
}

bool Runtime::unlock_mutex(const std::uint32_t owner_process,
    const std::uint32_t handle, const std::uint32_t thread_id,
    std::string* error) {
    std::uint64_t object_id = 0U;
    SyncObject* object = find_sync_object(owner_process, handle,
                                          KernelObjectType::Mutex, object_id);
    if (object == nullptr || object->owner_thread != thread_id ||
        object->recursion_depth == 0U) {
        set_error(error, "Only the owning thread can unlock this mutex.");
        return false;
    }
    --object->recursion_depth;
    if (object->recursion_depth == 0U) {
        object->owner_thread = 0U;
        std::uint32_t awakened = 0U;
        (void)wake_one_waiter(object_id, awakened);
    }
    return true;
}

} // namespace lucid::kernel
