#include "lucid/gpu/command_processor.hpp"

#include <array>
#include <limits>
#include <span>
#include <vector>

namespace lucid::gpu {
namespace {

struct DecodedCommand {
    GpuOpcode opcode{};
    std::array<std::uint32_t, 5> payload{};
};

void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

std::uint32_t load_u32_le(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

} // namespace

bool GpuCommandProcessor::submit(const GuestGpuMemory& memory,
                                 const std::uint64_t guest_address,
                                 const std::size_t byte_length,
                                 GraphicsBackend& backend,
                                 std::string* error) {
    if (!memory.valid()) {
        set_error(error, "GPU command memory is not valid.");
        return false;
    }
    if (byte_length == 0U || byte_length > MaxCommandBufferBytes ||
        (byte_length % 4U) != 0U) {
        set_error(error, "GPU command buffer length must be aligned and within the 1 MiB limit.");
        return false;
    }

    std::vector<std::uint8_t> bytes(byte_length);
    if (!memory.read(guest_address, bytes)) {
        set_error(error, "GPU command buffer is outside guest GPU memory.");
        return false;
    }

    std::vector<DecodedCommand> commands;
    commands.reserve(byte_length / 4U);
    std::size_t cursor = 0U;
    bool in_frame = false;
    bool has_presented_frame = false;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    bool ended = false;

    while (cursor < bytes.size()) {
        if (bytes.size() - cursor < 4U) {
            set_error(error, "Truncated GPU command header.");
            return false;
        }
        const auto header = load_u32_le(bytes.data() + cursor);
        cursor += 4U;
        const auto opcode = static_cast<GpuOpcode>((header >> 24U) & 0xFFU);
        const auto payload_count = static_cast<std::size_t>(header & 0x00FFFFFFU);
        if (payload_count > (bytes.size() - cursor) / 4U) {
            set_error(error, "GPU command payload extends past the command buffer.");
            return false;
        }
        const auto required_words = [&]() -> std::size_t {
            switch (opcode) {
            case GpuOpcode::Nop: return 0U;
            case GpuOpcode::BeginFrame: return 2U;
            case GpuOpcode::Clear: return 1U;
            case GpuOpcode::DrawRect: return 5U;
            case GpuOpcode::Present: return 0U;
            case GpuOpcode::End: return 0U;
            }
            return std::numeric_limits<std::size_t>::max();
        }();
        if (required_words == std::numeric_limits<std::size_t>::max() ||
            payload_count != required_words) {
            set_error(error, "Unknown GPU opcode or invalid payload word count.");
            return false;
        }

        DecodedCommand command{};
        command.opcode = opcode;
        for (std::size_t index = 0U; index < payload_count; ++index) {
            command.payload[index] = load_u32_le(bytes.data() + cursor);
            cursor += 4U;
        }

        switch (opcode) {
        case GpuOpcode::Nop:
            break;
        case GpuOpcode::BeginFrame:
            if (in_frame) {
                set_error(error, "A frame cannot begin before the previous frame is presented.");
                return false;
            }
            width = command.payload[0];
            height = command.payload[1];
            if (width == 0U || height == 0U ||
                width > MaxRenderDimension || height > MaxRenderDimension) {
                set_error(error, "Frame dimensions must be between 1 and 8192 pixels.");
                return false;
            }
            in_frame = true;
            break;
        case GpuOpcode::Clear:
            if (!in_frame) {
                set_error(error, "CLEAR requires an active frame.");
                return false;
            }
            break;
        case GpuOpcode::DrawRect: {
            if (!in_frame) {
                set_error(error, "DRAW_RECT requires an active frame.");
                return false;
            }
            const std::uint64_t right =
                static_cast<std::uint64_t>(command.payload[0]) + command.payload[2];
            const std::uint64_t bottom =
                static_cast<std::uint64_t>(command.payload[1]) + command.payload[3];
            if (command.payload[2] == 0U || command.payload[3] == 0U ||
                right > width || bottom > height) {
                set_error(error, "DRAW_RECT must have a nonzero size and fit inside the active frame.");
                return false;
            }
            break;
        }
        case GpuOpcode::Present:
            if (!in_frame) {
                set_error(error, "PRESENT requires an active frame.");
                return false;
            }
            in_frame = false;
            has_presented_frame = true;
            break;
        case GpuOpcode::End:
            if (in_frame || !has_presented_frame || cursor != bytes.size()) {
                set_error(error, "END must be the final command after at least one complete frame.");
                return false;
            }
            ended = true;
            break;
        }
        commands.push_back(command);
        if (ended) break;
    }

    if (!ended) {
        set_error(error, "GPU command buffer is missing its END command.");
        return false;
    }

    std::uint64_t presented = 0U;
    for (const auto& command : commands) {
        switch (command.opcode) {
        case GpuOpcode::Nop:
        case GpuOpcode::End:
            break;
        case GpuOpcode::BeginFrame:
            if (!backend.begin_frame(command.payload[0], command.payload[1], error))
                return false;
            break;
        case GpuOpcode::Clear:
            if (!backend.clear(command.payload[0], error)) return false;
            break;
        case GpuOpcode::DrawRect: {
            const GraphicsRect rect{
                command.payload[0], command.payload[1],
                command.payload[2], command.payload[3]
            };
            if (!backend.draw_rect(rect, command.payload[4], error)) return false;
            break;
        }
        case GpuOpcode::Present:
            if (!backend.present_frame(error)) return false;
            ++presented;
            break;
        }
    }

    ++stats_.submissions;
    stats_.commands += static_cast<std::uint64_t>(commands.size());
    stats_.frames_presented += presented;
    return true;
}

const GpuProcessorStats& GpuCommandProcessor::stats() const noexcept {
    return stats_;
}

} // namespace lucid::gpu
