#pragma once

#include <string>
#include <string_view>

namespace lucid::gpu {

// Host-renderer boundary. No console GPU command processor exists yet.
class GraphicsBackend {
public:
    virtual ~GraphicsBackend() = default;
    [[nodiscard]] virtual bool initialize(std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool present_frame(std::string* error = nullptr) = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

} // namespace lucid::gpu
