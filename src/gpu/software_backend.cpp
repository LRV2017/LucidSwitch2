#include "lucid/gpu/software_backend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace lucid::gpu {
namespace {
constexpr std::uint64_t kMaxPixels = 16ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxVertices = 65536U;
constexpr std::size_t kMaxIndices = 196608U;
constexpr double kEdgeEpsilon = 1.0e-8;

void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

bool finite_vertex(const GraphicsVertex& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           std::isfinite(v.u) && std::isfinite(v.v);
}

std::uint8_t channel(const std::uint32_t color, const unsigned shift) noexcept {
    return static_cast<std::uint8_t>((color >> shift) & 0xFFU);
}

std::uint32_t pack_rgba(const double r, const double g,
                        const double b, const double a) noexcept {
    const auto to_byte = [](const double value) {
        return static_cast<std::uint32_t>(std::clamp(
            std::lround(value), 0L, 255L));
    };
    return (to_byte(r) << 24U) | (to_byte(g) << 16U) |
           (to_byte(b) << 8U) | to_byte(a);
}

double edge(const GraphicsVertex& a, const GraphicsVertex& b,
            const double x, const double y) noexcept {
    return (static_cast<double>(b.x) - a.x) * (y - a.y) -
           (static_cast<double>(b.y) - a.y) * (x - a.x);
}
}

bool SoftwareBackend::initialize(std::string*) {
    initialized_ = true;
    return true;
}

bool SoftwareBackend::begin_frame(const std::uint32_t width,
                                  const std::uint32_t height,
                                  std::string* error) {
    if (!initialized_) {
        set_error(error, "Software graphics backend is not initialized.");
        return false;
    }
    if (width == 0U || height == 0U ||
        static_cast<std::uint64_t>(width) * height > kMaxPixels) {
        set_error(error, "Requested software render target exceeds the 16-megapixel limit.");
        return false;
    }
    width_ = width;
    height_ = height;
    const auto pixels = static_cast<std::size_t>(width) * height;
    pixels_.assign(pixels, 0U);
    depth_.assign(pixels, 1.0F);
    frame_open_ = true;
    frame_presented_ = false;
    return true;
}

bool SoftwareBackend::clear(const std::uint32_t rgba, std::string* error) {
    if (!frame_open_) {
        set_error(error, "CLEAR requires an open render pass.");
        return false;
    }
    std::fill(pixels_.begin(), pixels_.end(), rgba);
    return true;
}

bool SoftwareBackend::clear_depth(const float depth, std::string* error) {
    if (!frame_open_) {
        set_error(error, "Depth clear requires an open render pass.");
        return false;
    }
    if (!std::isfinite(depth) || depth < 0.0F || depth > 1.0F) {
        set_error(error, "Depth clear value must be finite and between 0 and 1.");
        return false;
    }
    std::fill(depth_.begin(), depth_.end(), depth);
    return true;
}

bool SoftwareBackend::draw_rect(const GraphicsRect& rect,
                                const std::uint32_t rgba,
                                std::string* error) {
    if (!frame_open_) {
        set_error(error, "DRAW_RECT requires an open render pass.");
        return false;
    }
    const std::uint64_t right =
        static_cast<std::uint64_t>(rect.x) + rect.width;
    const std::uint64_t bottom =
        static_cast<std::uint64_t>(rect.y) + rect.height;
    if (rect.width == 0U || rect.height == 0U ||
        right > width_ || bottom > height_) {
        set_error(error, "Rectangle is empty or extends outside the render target.");
        return false;
    }

    const float left = static_cast<float>(rect.x);
    const float top = static_cast<float>(rect.y);
    const float x_right = static_cast<float>(right);
    const float y_bottom = static_cast<float>(bottom);
    const std::array<GraphicsVertex, 4> vertices{{
        {left, top, 0.0F, rgba, 0.0F, 0.0F},
        {x_right, top, 0.0F, rgba, 1.0F, 0.0F},
        {left, y_bottom, 0.0F, rgba, 0.0F, 1.0F},
        {x_right, y_bottom, 0.0F, rgba, 1.0F, 1.0F}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0U, 1U, 2U, 2U, 1U, 3U};
    return draw_indexed(vertices, indices, 0U, error);
}

bool SoftwareBackend::create_texture(const TextureId id,
                                     const std::uint32_t width,
                                     const std::uint32_t height,
                                     const std::span<const std::uint32_t> pixels,
                                     const SamplerDesc& sampler,
                                     std::string* error) {
    if (id == 0U || width == 0U || height == 0U ||
        static_cast<std::uint64_t>(width) * height > kMaxPixels ||
        pixels.size() != static_cast<std::size_t>(width) * height) {
        set_error(error, "Texture ID, dimensions, or pixel count is invalid.");
        return false;
    }
    if (textures_.contains(id)) {
        set_error(error, "Texture ID is already allocated.");
        return false;
    }
    Texture texture{};
    texture.width = width;
    texture.height = height;
    texture.pixels.assign(pixels.begin(), pixels.end());
    texture.sampler = sampler;
    textures_.emplace(id, std::move(texture));
    return true;
}

bool SoftwareBackend::destroy_texture(const TextureId id, std::string* error) {
    const auto found = textures_.find(id);
    if (id == 0U || found == textures_.end()) {
        set_error(error, "Texture ID does not exist.");
        return false;
    }
    textures_.erase(found);
    return true;
}

std::uint32_t SoftwareBackend::sample_texture(const Texture& texture,
                                              const float input_u,
                                              const float input_v) const noexcept {
    const auto address = [](const float value, const TextureAddress mode) {
        if (mode == TextureAddress::Repeat) {
            const float wrapped = value - std::floor(value);
            return wrapped < 0.0F ? wrapped + 1.0F : wrapped;
        }
        return std::clamp(value, 0.0F, 1.0F);
    };
    const float u = address(input_u, texture.sampler.address_u);
    const float v = address(input_v, texture.sampler.address_v);
    const auto pixel_at = [&](int x, int y) {
        const auto resolve = [](int coordinate, const std::uint32_t extent,
                                const TextureAddress mode) {
            const int size = static_cast<int>(extent);
            if (mode == TextureAddress::Repeat) {
                int wrapped = coordinate % size;
                if (wrapped < 0) wrapped += size;
                return static_cast<std::uint32_t>(wrapped);
            }
            return static_cast<std::uint32_t>(
                std::clamp(coordinate, 0, size - 1));
        };
        const auto px = resolve(x, texture.width, texture.sampler.address_u);
        const auto py = resolve(y, texture.height, texture.sampler.address_v);
        return texture.pixels[static_cast<std::size_t>(py) * texture.width + px];
    };

    if (texture.sampler.min_filter == TextureFilter::Nearest &&
        texture.sampler.mag_filter == TextureFilter::Nearest) {
        const auto x = static_cast<int>(std::floor(u * texture.width));
        const auto y = static_cast<int>(std::floor(v * texture.height));
        return pixel_at(x, y);
    }

    const float fx = u * static_cast<float>(texture.width) - 0.5F;
    const float fy = v * static_cast<float>(texture.height) - 0.5F;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const double tx = static_cast<double>(fx - static_cast<float>(x0));
    const double ty = static_cast<double>(fy - static_cast<float>(y0));
    const std::array<std::uint32_t, 4> samples{
        pixel_at(x0, y0), pixel_at(x0 + 1, y0),
        pixel_at(x0, y0 + 1), pixel_at(x0 + 1, y0 + 1)
    };
    std::array<double, 4> channels{};
    for (unsigned shift_index = 0U; shift_index < 4U; ++shift_index) {
        const unsigned shift = 24U - shift_index * 8U;
        const double top = static_cast<double>(channel(samples[0], shift)) *
                               (1.0 - tx) +
                           static_cast<double>(channel(samples[1], shift)) * tx;
        const double bottom = static_cast<double>(channel(samples[2], shift)) *
                                  (1.0 - tx) +
                              static_cast<double>(channel(samples[3], shift)) * tx;
        channels[shift_index] = top * (1.0 - ty) + bottom * ty;
    }
    return pack_rgba(channels[0], channels[1], channels[2], channels[3]);
}

bool SoftwareBackend::rasterize_triangle(const GraphicsVertex& a,
                                         const GraphicsVertex& b,
                                         const GraphicsVertex& c,
                                         const Texture* texture,
                                         std::string* error) {
    if (!finite_vertex(a) || !finite_vertex(b) || !finite_vertex(c)) {
        set_error(error, "Triangle vertices must contain finite position, depth, and UV values.");
        return false;
    }
    if (a.z < 0.0F || a.z > 1.0F || b.z < 0.0F || b.z > 1.0F ||
        c.z < 0.0F || c.z > 1.0F) {
        set_error(error, "Vertex depth must be between 0 and 1.");
        return false;
    }

    const double area = edge(a, b, c.x, c.y);
    if (std::abs(area) < kEdgeEpsilon) return true;

    const double min_x_value = std::min({static_cast<double>(a.x),
                                         static_cast<double>(b.x),
                                         static_cast<double>(c.x)});
    const double max_x_value = std::max({static_cast<double>(a.x),
                                         static_cast<double>(b.x),
                                         static_cast<double>(c.x)});
    const double min_y_value = std::min({static_cast<double>(a.y),
                                         static_cast<double>(b.y),
                                         static_cast<double>(c.y)});
    const double max_y_value = std::max({static_cast<double>(a.y),
                                         static_cast<double>(b.y),
                                         static_cast<double>(c.y)});
    const double clipped_min_x =
        std::max(0.0, std::ceil(min_x_value - 0.5));
    const double clipped_max_x =
        std::min(static_cast<double>(width_ - 1U), std::floor(max_x_value - 0.5));
    const double clipped_min_y =
        std::max(0.0, std::ceil(min_y_value - 0.5));
    const double clipped_max_y =
        std::min(static_cast<double>(height_ - 1U), std::floor(max_y_value - 0.5));
    if (clipped_min_x > clipped_max_x || clipped_min_y > clipped_max_y ||
        clipped_min_x >= static_cast<double>(width_) ||
        clipped_min_y >= static_cast<double>(height_) ||
        clipped_max_x < 0.0 || clipped_max_y < 0.0) return true;
    const auto min_x = static_cast<std::uint32_t>(clipped_min_x);
    const auto max_x = static_cast<std::uint32_t>(clipped_max_x);
    const auto min_y = static_cast<std::uint32_t>(clipped_min_y);
    const auto max_y = static_cast<std::uint32_t>(clipped_max_y);

    const double inverse_area = 1.0 / area;
    for (std::uint32_t y = min_y; y <= max_y; ++y) {
        for (std::uint32_t x = min_x; x <= max_x; ++x) {
            const double px = static_cast<double>(x) + 0.5;
            const double py = static_cast<double>(y) + 0.5;
            const double w0 = edge(b, c, px, py) * inverse_area;
            const double w1 = edge(c, a, px, py) * inverse_area;
            const double w2 = edge(a, b, px, py) * inverse_area;
            if (w0 < -kEdgeEpsilon || w1 < -kEdgeEpsilon ||
                w2 < -kEdgeEpsilon) continue;

            const double z = w0 * a.z + w1 * b.z + w2 * c.z;
            const auto pixel_index = static_cast<std::size_t>(y) * width_ + x;
            if (z > static_cast<double>(depth_[pixel_index])) continue;

            const auto interpolate = [&](const unsigned shift) {
                return w0 * channel(a.rgba, shift) +
                       w1 * channel(b.rgba, shift) +
                       w2 * channel(c.rgba, shift);
            };
            const auto vertex_color = pack_rgba(interpolate(24U), interpolate(16U),
                                                interpolate(8U), interpolate(0U));
            if (texture == nullptr) {
                pixels_[pixel_index] = vertex_color;
            } else {
                const float u = static_cast<float>(w0 * a.u + w1 * b.u + w2 * c.u);
                const float v = static_cast<float>(w0 * a.v + w1 * b.v + w2 * c.v);
                const auto texel = sample_texture(*texture, u, v);
                const auto modulate = [&](const unsigned shift) {
                    return static_cast<double>(channel(vertex_color, shift)) *
                           channel(texel, shift) / 255.0;
                };
                pixels_[pixel_index] = pack_rgba(
                    modulate(24U), modulate(16U), modulate(8U), modulate(0U));
            }
            depth_[pixel_index] = static_cast<float>(z);
        }
    }
    return true;
}

bool SoftwareBackend::draw_indexed(
    const std::span<const GraphicsVertex> vertices,
    const std::span<const std::uint32_t> indices,
    const TextureId texture_id, std::string* error) {
    if (!frame_open_) {
        set_error(error, "Indexed drawing requires an open render pass.");
        return false;
    }
    if (vertices.empty() || vertices.size() > kMaxVertices ||
        indices.empty() || indices.size() > kMaxIndices ||
        (indices.size() % 3U) != 0U) {
        set_error(error, "Indexed geometry exceeds limits or is not a triangle list.");
        return false;
    }

    const Texture* texture = nullptr;
    if (texture_id != 0U) {
        const auto found = textures_.find(texture_id);
        if (found == textures_.end()) {
            set_error(error, "Indexed draw references an unknown texture.");
            return false;
        }
        texture = &found->second;
    }
    for (const auto index : indices) {
        if (index >= vertices.size()) {
            set_error(error, "Index buffer references a vertex outside the vertex buffer.");
            return false;
        }
    }
    for (const auto& vertex : vertices) {
        if (!finite_vertex(vertex) || vertex.z < 0.0F || vertex.z > 1.0F) {
            set_error(error, "Vertex buffer contains invalid position, depth, or UV values.");
            return false;
        }
    }

    for (std::size_t index = 0U; index < indices.size(); index += 3U) {
        if (!rasterize_triangle(vertices[indices[index]],
                                vertices[indices[index + 1U]],
                                vertices[indices[index + 2U]],
                                texture, error)) return false;
    }
    return true;
}

bool SoftwareBackend::present_frame(std::string* error) {
    if (!frame_open_) {
        set_error(error, "PRESENT requires an open render pass.");
        return false;
    }
    frame_open_ = false;
    frame_presented_ = true;
    ++presented_frames_;
    return true;
}

bool SoftwareBackend::readback(std::span<std::uint32_t> pixels,
                               std::string* error) const {
    if (!frame_presented_) {
        set_error(error, "No completed frame is available for readback.");
        return false;
    }
    if (pixels.size() < pixels_.size()) {
        set_error(error, "Readback destination is smaller than the render target.");
        return false;
    }
    std::copy(pixels_.begin(), pixels_.end(), pixels.begin());
    return true;
}

GraphicsCapabilities SoftwareBackend::capabilities() const noexcept {
    return {false, false, true, true, false,
        "CPU triangle rasterizer with indexed geometry, depth attachment, textures, and samplers"};
}
std::string_view SoftwareBackend::name() const noexcept { return "Software reference renderer"; }
std::uint64_t SoftwareBackend::presented_frames() const noexcept { return presented_frames_; }
std::uint32_t SoftwareBackend::width() const noexcept { return width_; }
std::uint32_t SoftwareBackend::height() const noexcept { return height_; }
std::size_t SoftwareBackend::texture_count() const noexcept { return textures_.size(); }

} // namespace lucid::gpu
