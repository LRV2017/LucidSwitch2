#include "lucid/gpu/command_processor.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace lucid::gpu {
namespace {

struct DecodedCommand {
    GpuOpcode opcode{};
    std::array<std::uint32_t, 7> payload{};
    std::vector<std::uint32_t> texture_pixels{};
    std::vector<GraphicsVertex> vertices{};
    std::vector<std::uint32_t> indices{};
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

std::uint64_t load_u64_words(const std::uint32_t low,
                             const std::uint32_t high) noexcept {
    return static_cast<std::uint64_t>(low) |
           (static_cast<std::uint64_t>(high) << 32U);
}

bool read_u32_array(const GpuMemoryView& memory, const std::uint64_t address,
                    const std::size_t count, std::vector<std::uint32_t>& output) {
    if (count > std::numeric_limits<std::size_t>::max() / 4U) return false;
    std::vector<std::uint8_t> bytes(count * 4U);
    if (!memory.read(address, bytes)) return false;
    output.resize(count);
    for (std::size_t index = 0U; index < count; ++index)
        output[index] = load_u32_le(bytes.data() + index * 4U);
    return true;
}

} // namespace

bool GpuCommandProcessor::submit(const GpuMemoryView& memory,
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
    auto active_textures = texture_ids_;
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
            case GpuOpcode::CreateTexture: return 6U;
            case GpuOpcode::DestroyTexture: return 1U;
            case GpuOpcode::DrawIndexed: return 7U;
            case GpuOpcode::ClearDepth: return 1U;
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
                set_error(error, "CLEAR requires an active render pass.");
                return false;
            }
            break;
        case GpuOpcode::DrawRect: {
            if (!in_frame) {
                set_error(error, "DRAW_RECT requires an active render pass.");
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
                set_error(error, "PRESENT requires an active render pass.");
                return false;
            }
            in_frame = false;
            has_presented_frame = true;
            break;
        case GpuOpcode::ClearDepth: {
            if (!in_frame) {
                set_error(error, "CLEAR_DEPTH requires an active render pass.");
                return false;
            }
            const auto depth = std::bit_cast<float>(command.payload[0]);
            if (!std::isfinite(depth) || depth < 0.0F || depth > 1.0F) {
                set_error(error, "CLEAR_DEPTH value must be finite and between 0 and 1.");
                return false;
            }
            break;
        }
        case GpuOpcode::CreateTexture: {
            const TextureId id = command.payload[0];
            const auto texture_width = command.payload[1];
            const auto texture_height = command.payload[2];
            const auto texture_address = load_u64_words(
                command.payload[3], command.payload[4]);
            const auto pixel_count =
                static_cast<std::uint64_t>(texture_width) * texture_height;
            if (id == 0U || active_textures.contains(id) ||
                texture_width == 0U || texture_height == 0U ||
                pixel_count > 16ULL * 1024ULL * 1024ULL ||
                pixel_count > std::numeric_limits<std::size_t>::max()) {
                set_error(error, "CREATE_TEXTURE has an invalid ID or dimensions.");
                return false;
            }
            if (!read_u32_array(memory, texture_address,
                                static_cast<std::size_t>(pixel_count),
                                command.texture_pixels)) {
                set_error(error, "CREATE_TEXTURE pixel data is outside guest GPU memory.");
                return false;
            }
            if ((command.payload[5] & ~0x7U) != 0U) {
                set_error(error, "CREATE_TEXTURE contains unsupported sampler flags.");
                return false;
            }
            active_textures.insert(id);
            break;
        }
        case GpuOpcode::DestroyTexture: {
            const TextureId id = command.payload[0];
            if (id == 0U || !active_textures.contains(id)) {
                set_error(error, "DESTROY_TEXTURE references an unknown texture.");
                return false;
            }
            active_textures.erase(id);
            break;
        }
        case GpuOpcode::DrawIndexed: {
            if (!in_frame) {
                set_error(error, "DRAW_INDEXED requires an active render pass.");
                return false;
            }
            const auto vertex_address = load_u64_words(
                command.payload[0], command.payload[1]);
            const auto vertex_count = command.payload[2];
            const auto index_address = load_u64_words(
                command.payload[3], command.payload[4]);
            const auto index_count = command.payload[5];
            const TextureId texture_id = command.payload[6];
            if (vertex_count == 0U || vertex_count > MaxVerticesPerDraw ||
                index_count == 0U || index_count > MaxIndicesPerDraw ||
                (index_count % 3U) != 0U ||
                (texture_id != 0U && !active_textures.contains(texture_id))) {
                set_error(error, "DRAW_INDEXED has invalid counts or a missing texture binding.");
                return false;
            }
            constexpr std::size_t vertex_words = 6U;
            std::vector<std::uint32_t> vertex_data;
            if (!read_u32_array(memory, vertex_address,
                                static_cast<std::size_t>(vertex_count) * vertex_words,
                                vertex_data)) {
                set_error(error, "DRAW_INDEXED vertex data is outside guest GPU memory.");
                return false;
            }
            command.vertices.resize(vertex_count);
            for (std::size_t index = 0U; index < command.vertices.size(); ++index) {
                const auto* value = vertex_data.data() + index * vertex_words;
                command.vertices[index] = {
                    std::bit_cast<float>(value[0]),
                    std::bit_cast<float>(value[1]),
                    std::bit_cast<float>(value[2]),
                    value[3],
                    std::bit_cast<float>(value[4]),
                    std::bit_cast<float>(value[5])
                };
                const auto& vertex = command.vertices[index];
                if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
                    !std::isfinite(vertex.z) || !std::isfinite(vertex.u) ||
                    !std::isfinite(vertex.v) || vertex.z < 0.0F ||
                    vertex.z > 1.0F) {
                    set_error(error, "DRAW_INDEXED contains a non-finite or out-of-range vertex.");
                    return false;
                }
            }
            if (!read_u32_array(memory, index_address, index_count,
                                command.indices)) {
                set_error(error, "DRAW_INDEXED index data is outside guest GPU memory.");
                return false;
            }
            for (const auto index : command.indices) {
                if (index >= vertex_count) {
                    set_error(error, "DRAW_INDEXED index is outside the vertex buffer.");
                    return false;
                }
            }
            break;
        }
        case GpuOpcode::End:
            if (in_frame || !has_presented_frame || cursor != bytes.size()) {
                set_error(error, "END must be the final command after at least one complete frame.");
                return false;
            }
            ended = true;
            break;
        }
        commands.push_back(std::move(command));
        if (ended) break;
    }

    if (!ended) {
        set_error(error, "GPU command buffer is missing its END command.");
        return false;
    }

    std::uint64_t presented = 0U;
    std::uint64_t indexed_draws = 0U;
    std::uint64_t textures_created = 0U;
    std::uint64_t textures_destroyed = 0U;
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
        case GpuOpcode::ClearDepth:
            if (!backend.clear_depth(std::bit_cast<float>(command.payload[0]), error))
                return false;
            break;
        case GpuOpcode::DrawRect: {
            const GraphicsRect rect{
                command.payload[0], command.payload[1],
                command.payload[2], command.payload[3]
            };
            if (!backend.draw_rect(rect, command.payload[4], error)) return false;
            break;
        }
        case GpuOpcode::CreateTexture: {
            const auto flags = command.payload[5];
            const SamplerDesc sampler{
                (flags & 1U) != 0U ? TextureFilter::Linear : TextureFilter::Nearest,
                (flags & 1U) != 0U ? TextureFilter::Linear : TextureFilter::Nearest,
                (flags & 2U) != 0U ? TextureAddress::Repeat : TextureAddress::Clamp,
                (flags & 4U) != 0U ? TextureAddress::Repeat : TextureAddress::Clamp
            };
            if (!backend.create_texture(command.payload[0], command.payload[1],
                                        command.payload[2], command.texture_pixels,
                                        sampler, error)) return false;
            ++textures_created;
            break;
        }
        case GpuOpcode::DestroyTexture:
            if (!backend.destroy_texture(command.payload[0], error)) return false;
            ++textures_destroyed;
            break;
        case GpuOpcode::DrawIndexed:
            if (!backend.draw_indexed(command.vertices, command.indices,
                                      command.payload[6], error)) return false;
            ++indexed_draws;
            break;
        case GpuOpcode::Present:
            if (!backend.present_frame(error)) return false;
            ++presented;
            break;
        }
    }

    texture_ids_ = std::move(active_textures);
    ++stats_.submissions;
    stats_.commands += static_cast<std::uint64_t>(commands.size());
    stats_.frames_presented += presented;
    stats_.indexed_draws += indexed_draws;
    stats_.textures_created += textures_created;
    stats_.textures_destroyed += textures_destroyed;
    return true;
}

const GpuProcessorStats& GpuCommandProcessor::stats() const noexcept {
    return stats_;
}

std::size_t GpuCommandProcessor::live_texture_count() const noexcept {
    return texture_ids_.size();
}

} // namespace lucid::gpu
