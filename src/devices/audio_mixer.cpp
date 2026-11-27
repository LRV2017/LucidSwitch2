#include "lucid/devices/audio_mixer.hpp"

#include <algorithm>
#include <limits>

namespace lucid::devices {
namespace {
std::int16_t clamp_sample(const std::int64_t value) noexcept {
    return static_cast<std::int16_t>(std::clamp<std::int64_t>(
        value, std::numeric_limits<std::int16_t>::min(),
        std::numeric_limits<std::int16_t>::max()));
}
}

bool AudioMixer::set_gain(const std::size_t channel,
                          const std::uint16_t gain_q15) noexcept {
    if (channel >= MaxChannels || gain_q15 > UnityGainQ15) return false;
    std::lock_guard lock(mutex_);
    channels_[channel].gain_q15 = gain_q15;
    return true;
}

bool AudioMixer::submit_stereo(
    const std::size_t channel,
    const std::span<const std::int16_t> interleaved_samples) {
    if (channel >= MaxChannels || (interleaved_samples.size() % 2U) != 0U)
        return false;
    const std::size_t frames = interleaved_samples.size() / 2U;
    std::lock_guard lock(mutex_);
    auto& queue = channels_[channel].frames;
    if (frames > MaxQueuedFramesPerChannel - queue.size()) {
        ++stats_.rejected_submissions;
        return false;
    }
    for (std::size_t frame = 0U; frame < frames; ++frame)
        queue.push_back({interleaved_samples[frame * 2U],
                         interleaved_samples[frame * 2U + 1U]});
    stats_.submitted_frames += frames;
    return true;
}

std::size_t AudioMixer::mix_stereo(
    const std::span<std::int16_t> output_interleaved) {
    if ((output_interleaved.size() % 2U) != 0U) return 0U;
    const std::size_t frames = output_interleaved.size() / 2U;
    std::lock_guard lock(mutex_);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        std::int64_t left = 0;
        std::int64_t right = 0;
        for (auto& channel : channels_) {
            if (channel.frames.empty()) continue;
            const StereoFrame sample = channel.frames.front();
            channel.frames.pop_front();
            left += (static_cast<std::int64_t>(sample.left) * channel.gain_q15) /
                    UnityGainQ15;
            right += (static_cast<std::int64_t>(sample.right) * channel.gain_q15) /
                     UnityGainQ15;
        }
        output_interleaved[frame * 2U] = clamp_sample(left);
        output_interleaved[frame * 2U + 1U] = clamp_sample(right);
    }
    stats_.mixed_frames += frames;
    return frames;
}

std::size_t AudioMixer::queued_frames(const std::size_t channel) const noexcept {
    if (channel >= MaxChannels) return 0U;
    std::lock_guard lock(mutex_);
    return channels_[channel].frames.size();
}

AudioMixerStats AudioMixer::stats() const noexcept {
    std::lock_guard lock(mutex_);
    return stats_;
}

void AudioMixer::clear() noexcept {
    std::lock_guard lock(mutex_);
    for (auto& channel : channels_) channel.frames.clear();
}

} // namespace lucid::devices
