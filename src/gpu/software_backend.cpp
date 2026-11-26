#include "lucid/gpu/software_backend.hpp"

#include <algorithm>
#include <limits>

namespace lucid::gpu {
namespace {
constexpr std::uint64_t kMaxPixels = 16ULL * 1024ULL * 1024ULL;
void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
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
    pixels_.assign(static_cast<std::size_t>(width) * height, 0U);
    frame_open_ = true;
    frame_presented_ = false;
    return true;
}

bool SoftwareBackend::clear(const std::uint32_t rgba, std::string* error) {
    if (!frame_open_) {
        set_error(error, "CLEAR requires an open frame.");
        return false;
    }
    std::fill(pixels_.begin(), pixels_.end(), rgba);
    return true;
}

bool SoftwareBackend::draw_rect(const GraphicsRect& rect,
                                const std::uint32_t rgba,
                                std::string* error) {
    if (!frame_open_) {
        set_error(error, "DRAW_RECT requires an open frame.");
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

    for (std::uint32_t y = rect.y; y < rect.y + rect.height; ++y) {
        const auto row = static_cast<std::size_t>(y) * width_;
        std::fill_n(pixels_.begin() + static_cast<std::ptrdiff_t>(row + rect.x),
                    rect.width, rgba);
    }
    return true;
}

bool SoftwareBackend::present_frame(std::string* error) {
    if (!frame_open_) {
        set_error(error, "PRESENT requires an open frame.");
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
    return {false, false, "CPU reference rasterizer; deterministic RGBA8 off-screen rendering"};
}
std::string_view SoftwareBackend::name() const noexcept { return "Software reference renderer"; }
std::uint64_t SoftwareBackend::presented_frames() const noexcept { return presented_frames_; }
std::uint32_t SoftwareBackend::width() const noexcept { return width_; }
std::uint32_t SoftwareBackend::height() const noexcept { return height_; }

} // namespace lucid::gpu
