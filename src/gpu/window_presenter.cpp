#include "lucid/gpu/window_presenter.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if LUCID_HAS_SDL2
#define SDL_MAIN_HANDLED
#include <SDL.h>
#endif

namespace lucid::gpu {
namespace {
void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}
}

struct WindowPresenter::Impl {
#if LUCID_HAS_SDL2
    SDL_Window* window{};
    SDL_Renderer* renderer{};
    SDL_Texture* texture{};
    std::uint32_t texture_width{};
    std::uint32_t texture_height{};
    bool initialized{};
    bool close_requested{};
    bool owns_video_subsystem{};

    ~Impl() { close(); }

    void close() noexcept {
        if (texture != nullptr) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        if (owns_video_subsystem) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            owns_video_subsystem = false;
        }
        initialized = false;
    }
#else
    bool close_requested{};
#endif
};

WindowPresenter::WindowPresenter() : impl_(std::make_unique<Impl>()) {}
WindowPresenter::~WindowPresenter() = default;
WindowPresenter::WindowPresenter(WindowPresenter&&) noexcept = default;
WindowPresenter& WindowPresenter::operator=(WindowPresenter&&) noexcept = default;

bool WindowPresenter::compiled_with_sdl2() noexcept {
#if LUCID_HAS_SDL2
    return true;
#else
    return false;
#endif
}

bool WindowPresenter::initialize(const std::string_view title,
                                 const std::uint32_t width,
                                 const std::uint32_t height,
                                 std::string* error) {
#if LUCID_HAS_SDL2
    if (width == 0U || height == 0U ||
        width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        set_error(error, "Window dimensions must be nonzero and fit the SDL2 integer size range.");
        return false;
    }
    if (impl_->initialized) return true;
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0U) {
        SDL_SetMainReady();
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        impl_->owns_video_subsystem = true;
    }
    const std::string window_title(title);
    impl_->window = SDL_CreateWindow(window_title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        static_cast<int>(width), static_cast<int>(height),
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (impl_->window == nullptr) {
        if (error != nullptr) *error = SDL_GetError();
        impl_->close();
        return false;
    }
    impl_->renderer = SDL_CreateRenderer(impl_->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (impl_->renderer == nullptr) {
        SDL_ClearError();
        impl_->renderer = SDL_CreateRenderer(impl_->window, -1,
                                             SDL_RENDERER_SOFTWARE);
    }
    if (impl_->renderer == nullptr) {
        if (error != nullptr) *error = SDL_GetError();
        impl_->close();
        return false;
    }
    impl_->initialized = true;
    impl_->close_requested = false;
    return true;
#else
    (void)title; (void)width; (void)height;
    set_error(error, "SDL2 was not detected at configure time. Install SDL2 and reconfigure to enable window presentation.");
    return false;
#endif
}

bool WindowPresenter::present(const std::span<const std::uint32_t> rgba_pixels,
                              const std::uint32_t width,
                              const std::uint32_t height,
                              std::string* error) {
#if LUCID_HAS_SDL2
    const auto pixel_count = static_cast<std::uint64_t>(width) * height;
    if (!impl_->initialized || width == 0U || height == 0U ||
        width > static_cast<std::uint32_t>(std::numeric_limits<int>::max() / 4) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        pixel_count > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max() / 4U) ||
        rgba_pixels.size() != static_cast<std::size_t>(pixel_count)) {
        set_error(error, "Window presentation requires an initialized window and a complete RGBA frame.");
        return false;
    }
    if (impl_->texture == nullptr || impl_->texture_width != width ||
        impl_->texture_height != height) {
        if (impl_->texture != nullptr) {
            SDL_DestroyTexture(impl_->texture);
            impl_->texture = nullptr;
        }
        impl_->texture = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING, static_cast<int>(width),
            static_cast<int>(height));
        if (impl_->texture == nullptr) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        impl_->texture_width = width;
        impl_->texture_height = height;
    }

    std::vector<std::uint8_t> rgba_bytes(rgba_pixels.size() * 4U);
    for (std::size_t index = 0U; index < rgba_pixels.size(); ++index) {
        const auto pixel = rgba_pixels[index];
        rgba_bytes[index * 4U] = static_cast<std::uint8_t>((pixel >> 24U) & 0xFFU);
        rgba_bytes[index * 4U + 1U] = static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU);
        rgba_bytes[index * 4U + 2U] = static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU);
        rgba_bytes[index * 4U + 3U] = static_cast<std::uint8_t>(pixel & 0xFFU);
    }
    if (SDL_UpdateTexture(impl_->texture, nullptr, rgba_bytes.data(),
                          static_cast<int>(width * 4U)) != 0 ||
        SDL_RenderClear(impl_->renderer) != 0 ||
        SDL_RenderCopy(impl_->renderer, impl_->texture, nullptr, nullptr) != 0) {
        if (error != nullptr) *error = SDL_GetError();
        return false;
    }
    SDL_RenderPresent(impl_->renderer);
    poll_events();
    return true;
#else
    (void)rgba_pixels; (void)width; (void)height;
    set_error(error, "SDL2 window presentation is unavailable in this build.");
    return false;
#endif
}

void WindowPresenter::poll_events() noexcept {
#if LUCID_HAS_SDL2
    if (impl_ == nullptr || !impl_->initialized) return;
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT &&
             event.window.event == SDL_WINDOWEVENT_CLOSE)) {
            impl_->close_requested = true;
        }
    }
#endif
}

bool WindowPresenter::should_close() const noexcept {
    return impl_ == nullptr || impl_->close_requested;
}

void WindowPresenter::close() noexcept {
    if (impl_ != nullptr) {
#if LUCID_HAS_SDL2
        impl_->close();
#else
        impl_->close_requested = true;
#endif
    }
}

} // namespace lucid::gpu
