#include "lucid/core/emulator.hpp"

namespace lucid::core {

bool Emulator::initialize(std::string* error) {
    if (state_ != SessionState::Cold) {
        if (error != nullptr) {
            *error = "The emulator session has already been initialized.";
        }
        return false;
    }

    // Host-side session setup only; no console CPU/GPU is created here.
    state_ = SessionState::Ready;
    return true;
}

RunResult Emulator::run() noexcept {
    if (state_ == SessionState::Cold) {
        return RunResult::NotInitialized;
    }

    // Fail closed until a verified CPU backend exists.
    return RunResult::BackendUnavailable;
}

SessionState Emulator::state() const noexcept {
    return state_;
}

const char* to_string(const SessionState state) noexcept {
    switch (state) {
        case SessionState::Cold: return "cold";
        case SessionState::Ready: return "ready";
        case SessionState::Running: return "running";
        case SessionState::Paused: return "paused";
        case SessionState::Stopped: return "stopped";
        case SessionState::Faulted: return "faulted";
    }
    return "unknown";
}

const char* to_string(const RunResult result) noexcept {
    switch (result) {
        case RunResult::Started: return "started";
        case RunResult::NotInitialized: return "not initialized";
        case RunResult::BackendUnavailable: return "execution backend unavailable";
    }
    return "unknown";
}

} // namespace lucid::core
