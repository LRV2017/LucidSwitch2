#pragma once

#include <string>

namespace lucid::core {

enum class SessionState { Cold, Ready, Running, Paused, Stopped, Faulted };
enum class RunResult { Started, NotInitialized, BackendUnavailable };

// Session orchestration only. A real execution backend is not implemented yet.
class Emulator final {
public:
    [[nodiscard]] bool initialize(std::string* error = nullptr);
    [[nodiscard]] RunResult run() noexcept;
    [[nodiscard]] SessionState state() const noexcept;

private:
    SessionState state_{SessionState::Cold};
};

[[nodiscard]] const char* to_string(SessionState state) noexcept;
[[nodiscard]] const char* to_string(RunResult result) noexcept;

} // namespace lucid::core
