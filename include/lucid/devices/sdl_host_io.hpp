#pragma once
#include "lucid/devices/audio_mixer.hpp"
#include "lucid/devices/controller_hub.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace lucid::devices {
class SdlHostIo final {
public:
    explicit SdlHostIo(ControllerHub& controllers, AudioMixer& mixer) noexcept;
    ~SdlHostIo();
    SdlHostIo(const SdlHostIo&) = delete;
    SdlHostIo& operator=(const SdlHostIo&) = delete;
    [[nodiscard]] static bool compiled_with_sdl2() noexcept;
    [[nodiscard]] bool initialize(std::string* error = nullptr);
    void shutdown() noexcept;
    [[nodiscard]] std::size_t poll_controllers() noexcept;
    [[nodiscard]] bool open_audio_device(std::string* error = nullptr);
    void close_audio_device() noexcept;
    [[nodiscard]] bool audio_device_open() const noexcept;
private:
    ControllerHub& controllers_;
    AudioMixer& mixer_;
    std::array<void*, ControllerHub::MaxControllers> controller_handles_{};
    std::array<std::int32_t, ControllerHub::MaxControllers> instance_ids_{};
    std::uint32_t audio_device_id_{};
    bool initialized_{};
};
} // namespace lucid::devices
