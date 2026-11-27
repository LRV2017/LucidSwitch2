#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>

namespace lucid::devices {

struct StereoFrame {
    std::int16_t left{};
    std::int16_t right{};
};

struct AudioMixerStats {
    std::uint64_t submitted_frames{};
    std::uint64_t mixed_frames{};
    std::uint64_t rejected_submissions{};
};

// Deterministic host-neutral linear PCM mixer. Samples are signed 16-bit,
// stereo, 48 kHz frames. An OS audio adapter can pull frames with mix_stereo().
class AudioMixer final {
public:
    static constexpr std::size_t SampleRate = 48000U;
    static constexpr std::size_t MaxChannels = 16U;
    static constexpr std::size_t MaxQueuedFramesPerChannel = 96000U;
    static constexpr std::uint16_t UnityGainQ15 = 32768U;

    [[nodiscard]] bool set_gain(std::size_t channel, std::uint16_t gain_q15) noexcept;
    [[nodiscard]] bool submit_stereo(std::size_t channel,
                                     std::span<const std::int16_t> interleaved_samples);
    [[nodiscard]] std::size_t mix_stereo(std::span<std::int16_t> output_interleaved);
    [[nodiscard]] std::size_t queued_frames(std::size_t channel) const noexcept;
    [[nodiscard]] AudioMixerStats stats() const noexcept;
    void clear() noexcept;

private:
    struct Channel {
        std::deque<StereoFrame> frames;
        std::uint16_t gain_q15{UnityGainQ15};
    };
    mutable std::mutex mutex_;
    std::array<Channel, MaxChannels> channels_{};
    AudioMixerStats stats_{};
};

} // namespace lucid::devices
