#include "platform/window.hpp"

#include "core/log.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <format>
#include <utility>

namespace starforge::platform
{

std::expected<Window, std::string> Window::create(const WindowDesc& desc)
{
    // Hint SDL to prefer native Wayland over XWayland when available.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
    // Allow libdecor for window decorations on Wayland compositors that
    // don't support server-side decorations (e.g., GNOME).
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR, "1");
    // Prefer libdecor when available for consistent window decorations.
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR, "1");

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        return std::unexpected(std::format("SDL video init failed: {}", SDL_GetError()));
    }

    if (!SDL_Vulkan_LoadLibrary(nullptr))
    {
        std::string error = std::format("SDL_Vulkan_LoadLibrary failed: {}", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return std::unexpected(std::move(error));
    }

    SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
    if (desc.resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    SDL_Window* window = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
    if (!window)
    {
        std::string error = std::format("SDL_CreateWindow failed: {}", SDL_GetError());
        SDL_Vulkan_UnloadLibrary();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return std::unexpected(std::move(error));
    }

    // Log which video backend SDL selected (useful for Wayland vs. X11 diagnosis).
    const char* videoDriver = SDL_GetCurrentVideoDriver();
    core::logInfo("PLATFORM", "SDL video driver: {}", videoDriver ? videoDriver : "unknown");

    return Window(window);
}

Window::~Window()
{
    destroy();
}

Window::Window(Window&& other) noexcept
    : window_(std::exchange(other.window_, nullptr)),
      shouldClose_(other.shouldClose_),
      resized_(other.resized_)
{
}

Window& Window::operator=(Window&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        window_ = std::exchange(other.window_, nullptr);
        shouldClose_ = other.shouldClose_;
        resized_ = other.resized_;
    }
    return *this;
}

void Window::destroy() noexcept
{
    if (!window_)
    {
        return;
    }

    SDL_DestroyWindow(window_);
    SDL_Vulkan_UnloadLibrary();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    window_ = nullptr;
}

void Window::pollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            shouldClose_ = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
            resized_ = true;
            break;
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
            resized_ = true;
            break;
        default:
            break;
        }
    }
}

bool Window::takeResized() noexcept
{
    return std::exchange(resized_, false);
}

Extent2D Window::framebufferSize() const
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);

    return Extent2D{
        static_cast<std::uint32_t>(width > 0 ? width : 0),
        static_cast<std::uint32_t>(height > 0 ? height : 0),
    };
}

bool Window::isMinimized() const
{
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
    // On X11, minimized windows report SDL_WINDOW_MINIMIZED.
    // On Wayland, compositors may report SDL_WINDOW_HIDDEN instead
    // (there is no traditional minimize concept on Wayland).
    return (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0;
}

std::expected<std::vector<const char*>, std::string> Window::vulkanInstanceExtensions() const
{
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);

    if (!names || count == 0)
    {
        return std::unexpected(
            std::format("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError()));
    }

    return std::vector<const char*>(names, names + count);
}

std::expected<VkSurfaceKHR, std::string> Window::createVulkanSurface(VkInstance instance) const
{
    if (!window_ || instance == VK_NULL_HANDLE)
    {
        return std::unexpected("Invalid window or instance");
    }
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, &surface))
    {
        return std::unexpected(std::format("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError()));
    }
    return surface;
}

} // namespace starforge::platform
