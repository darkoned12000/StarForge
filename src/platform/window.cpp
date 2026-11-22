#include "platform/window.hpp"

#include "core/log.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <format>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace starforge::platform
{

namespace
{

std::mutex& platformMutex()
{
    static std::mutex mutex;
    return mutex;
}

unsigned int& platformRefCount()
{
    static unsigned int count = 0;
    return count;
}

std::expected<void, std::string> acquirePlatform()
{
    std::scoped_lock lock(platformMutex());
    auto& count = platformRefCount();
    if (count == 0)
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            return std::unexpected(std::format("SDL video init failed: {}", SDL_GetError()));
        }

        if (!SDL_Vulkan_LoadLibrary(nullptr))
        {
            const std::string error = std::format("SDL_Vulkan_LoadLibrary failed: {}", SDL_GetError());
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return std::unexpected(error);
        }
    }

    ++count;
    return {};
}

void releasePlatform() noexcept
{
    std::scoped_lock lock(platformMutex());
    auto& count = platformRefCount();
    if (count == 0)
    {
        return;
    }

    --count;
    if (count == 0)
    {
        SDL_Vulkan_UnloadLibrary();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

} // namespace

std::expected<Window, std::string> Window::create(const WindowDesc& desc)
{
    if (desc.width <= 0 || desc.height <= 0)
    {
        return std::unexpected("Window dimensions must be positive");
    }

    // Respect an explicit driver selection (e.g. SDL_VIDEODRIVER=dummy in
    // headless tests); otherwise prefer native Wayland over XWayland.
    if (SDL_GetHint(SDL_HINT_VIDEO_DRIVER) == nullptr)
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
    }
    // Allow libdecor for window decorations on Wayland compositors that
    // don't support server-side decorations (e.g., GNOME).
    if (SDL_GetHint(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR) == nullptr)
    {
        SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR, "1");
    }
    // Prefer libdecor when available for consistent window decorations.
    if (SDL_GetHint(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR) == nullptr)
    {
        SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR, "1");
    }

    auto platform = acquirePlatform();
    if (!platform.has_value())
    {
        return std::unexpected(platform.error());
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
        releasePlatform();
        return std::unexpected(std::move(error));
    }

    // Log which video backend SDL selected (useful for Wayland vs. X11 diagnosis).
    const char* videoDriver = SDL_GetCurrentVideoDriver();
    core::logInfo("PLATFORM", "SDL video driver: {}", videoDriver ? videoDriver : "unknown");

    // SDL3 does not deliver SDL_EVENT_TEXT_INPUT unless text input is
    // explicitly started. The comms chat composer relies on it; the game
    // drains the accumulated characters every frame even when not chatting.
    if (!SDL_StartTextInput(window))
    {
        core::logWarning("PLATFORM", "SDL_StartTextInput failed: {}", SDL_GetError());
    }

    return Window(window);
}

Window::~Window()
{
    destroy();
}

Window::Window(Window&& other) noexcept
    : window_(std::exchange(other.window_, nullptr)),
      shouldClose_(std::exchange(other.shouldClose_, false)),
      resized_(std::exchange(other.resized_, false)),
      mouseMotionX_(std::exchange(other.mouseMotionX_, 0.0f)),
      mouseMotionY_(std::exchange(other.mouseMotionY_, 0.0f)),
      mouseScrollY_(std::exchange(other.mouseScrollY_, 0.0f)),
      textInput_(std::move(other.textInput_))
{
    for (std::size_t i = 0; i < keyCount; ++i)
    {
        keyDown_[i] = std::exchange(other.keyDown_[i], false);
    }
    for (std::size_t i = 0; i < 3; ++i)
    {
        mouseButtonDown_[i] = std::exchange(other.mouseButtonDown_[i], false);
    }
}

Window& Window::operator=(Window&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        window_ = std::exchange(other.window_, nullptr);
        shouldClose_ = std::exchange(other.shouldClose_, false);
        resized_ = std::exchange(other.resized_, false);
        for (std::size_t i = 0; i < keyCount; ++i)
        {
            keyDown_[i] = std::exchange(other.keyDown_[i], false);
        }
        for (std::size_t i = 0; i < 3; ++i)
        {
            mouseButtonDown_[i] = std::exchange(other.mouseButtonDown_[i], false);
        }
        mouseMotionX_ = std::exchange(other.mouseMotionX_, 0.0f);
        mouseMotionY_ = std::exchange(other.mouseMotionY_, 0.0f);
        mouseScrollY_ = std::exchange(other.mouseScrollY_, 0.0f);
        textInput_ = std::move(other.textInput_);
    }
    return *this;
}

void Window::destroy() noexcept
{
    if (!window_)
    {
        return;
    }

    SDL_StopTextInput(window_);
    SDL_DestroyWindow(window_);
    releasePlatform();
    window_ = nullptr;
}

void Window::pollEvents()
{
    if (!window_)
    {
        return;
    }

    const SDL_WindowID windowId = SDL_GetWindowID(window_);
    std::vector<SDL_Event> deferredEvents;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        SDL_WindowID eventWindowId = 0;
        switch (event.type)
        {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            eventWindowId = event.window.windowID;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            eventWindowId = event.key.windowID;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            eventWindowId = event.button.windowID;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            eventWindowId = event.motion.windowID;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            eventWindowId = event.wheel.windowID;
            break;
        case SDL_EVENT_TEXT_INPUT:
            eventWindowId = event.text.windowID;
            break;
        default:
            break;
        }

        if (windowId != 0 && eventWindowId != 0 && eventWindowId != windowId)
        {
            // Keep events for other live windows in the queue so their
            // owners can process them. Drop events for unknown windows.
            if (SDL_GetWindowFromID(eventWindowId) != nullptr)
            {
                deferredEvents.push_back(event);
            }
            continue;
        }

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
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            const bool down = event.type == SDL_EVENT_KEY_DOWN;
            switch (event.key.scancode)
            {
            case SDL_SCANCODE_W:
                keyDown_[static_cast<std::size_t>(PlatformKey::W)] = down;
                break;
            case SDL_SCANCODE_A:
                keyDown_[static_cast<std::size_t>(PlatformKey::A)] = down;
                break;
            case SDL_SCANCODE_S:
                keyDown_[static_cast<std::size_t>(PlatformKey::S)] = down;
                break;
            case SDL_SCANCODE_D:
                keyDown_[static_cast<std::size_t>(PlatformKey::D)] = down;
                break;
            case SDL_SCANCODE_SPACE:
                keyDown_[static_cast<std::size_t>(PlatformKey::Space)] = down;
                break;
            case SDL_SCANCODE_P:
                keyDown_[static_cast<std::size_t>(PlatformKey::P)] = down;
                break;
            case SDL_SCANCODE_ESCAPE:
                keyDown_[static_cast<std::size_t>(PlatformKey::Escape)] = down;
                break;
            case SDL_SCANCODE_UP:
                keyDown_[static_cast<std::size_t>(PlatformKey::Up)] = down;
                break;
            case SDL_SCANCODE_DOWN:
                keyDown_[static_cast<std::size_t>(PlatformKey::Down)] = down;
                break;
            case SDL_SCANCODE_LEFT:
                keyDown_[static_cast<std::size_t>(PlatformKey::Left)] = down;
                break;
            case SDL_SCANCODE_RIGHT:
                keyDown_[static_cast<std::size_t>(PlatformKey::Right)] = down;
                break;
            case SDL_SCANCODE_LSHIFT:
                keyDown_[static_cast<std::size_t>(PlatformKey::LeftShift)] = down;
                break;
            case SDL_SCANCODE_TAB:
                keyDown_[static_cast<std::size_t>(PlatformKey::Tab)] = down;
                break;
            case SDL_SCANCODE_RETURN:
                keyDown_[static_cast<std::size_t>(PlatformKey::Return)] = down;
                break;
            case SDL_SCANCODE_BACKSPACE:
                keyDown_[static_cast<std::size_t>(PlatformKey::Backspace)] = down;
                break;
            case SDL_SCANCODE_EQUALS:
                keyDown_[static_cast<std::size_t>(PlatformKey::Equal)] = down;
                break;
            case SDL_SCANCODE_MINUS:
                keyDown_[static_cast<std::size_t>(PlatformKey::Minus)] = down;
                break;
            case SDL_SCANCODE_M:
                keyDown_[static_cast<std::size_t>(PlatformKey::M)] = down;
                break;
            case SDL_SCANCODE_T:
                keyDown_[static_cast<std::size_t>(PlatformKey::T)] = down;
                break;
            case SDL_SCANCODE_PAGEUP:
                keyDown_[static_cast<std::size_t>(PlatformKey::PageUp)] = down;
                break;
            case SDL_SCANCODE_PAGEDOWN:
                keyDown_[static_cast<std::size_t>(PlatformKey::PageDown)] = down;
                break;
            default:
                break;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            switch (event.button.button)
            {
            case SDL_BUTTON_LEFT:
                mouseButtonDown_[0] = down;
                break;
            case SDL_BUTTON_RIGHT:
                mouseButtonDown_[1] = down;
                break;
            case SDL_BUTTON_MIDDLE:
                mouseButtonDown_[2] = down;
                break;
            default:
                break;
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            mouseMotionX_ += event.motion.xrel;
            mouseMotionY_ += event.motion.yrel;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            mouseScrollY_ += event.wheel.y;
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (event.text.text) textInput_ += event.text.text;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        {
            // Alt+Tab away can skip KEY_UP events; clear all held state
            // so keys don't stick.
            for (std::size_t i = 0; i < keyCount; ++i)
            {
                keyDown_[i] = false;
            }
            for (std::size_t i = 0; i < 3; ++i)
            {
                mouseButtonDown_[i] = false;
            }
            break;
        }
        default:
            break;
        }
    }

    for (const SDL_Event& deferred : deferredEvents)
    {
        SDL_PushEvent(const_cast<SDL_Event*>(&deferred));
    }
}

void Window::setRelativeMouseMode(bool enabled) noexcept
{
    if (window_)
    {
        SDL_SetWindowRelativeMouseMode(window_, enabled);
    }
}

bool Window::takeResized() noexcept
{
    return std::exchange(resized_, false);
}

bool Window::isKeyDown(PlatformKey key) const noexcept
{
    const auto idx = static_cast<std::size_t>(key);
    return idx < keyCount && keyDown_[idx];
}

bool Window::isMouseButtonDown(PlatformMouseButton button) const noexcept
{
    const auto idx = static_cast<std::size_t>(button);
    return idx < 3 && mouseButtonDown_[idx];
}

void Window::takeMouseMotion(float& dxOut, float& dyOut, float& scrollOut) noexcept
{
    dxOut = std::exchange(mouseMotionX_, 0.0f);
    dyOut = std::exchange(mouseMotionY_, 0.0f);
    scrollOut = std::exchange(mouseScrollY_, 0.0f);
}

void Window::takeTextInput(std::string& out)
{
    out = std::exchange(textInput_, std::string{});
}

void Window::mousePosition(float& xOut, float& yOut) const noexcept
{
    if (!window_)
    {
        xOut = 0.0f;
        yOut = 0.0f;
        return;
    }

    // Compute window-relative coordinates from global mouse state so the
    // result is tied to this window, not whichever window has focus.
    float globalX = 0.0f;
    float globalY = 0.0f;
    SDL_GetGlobalMouseState(&globalX, &globalY);

    int windowX = 0;
    int windowY = 0;
    if (SDL_GetWindowPosition(window_, &windowX, &windowY))
    {
        xOut = globalX - static_cast<float>(windowX);
        yOut = globalY - static_cast<float>(windowY);
        return;
    }

    // Fallback when window position is unavailable (e.g. some Wayland
    // environments): use focus-relative mouse state.
    SDL_GetMouseState(&xOut, &yOut);
}

Extent2D Window::framebufferSize() const
{
    if (!window_)
    {
        return {};
    }

    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window_, &width, &height))
    {
        return {};
    }

    return Extent2D{
        static_cast<std::uint32_t>(width > 0 ? width : 0),
        static_cast<std::uint32_t>(height > 0 ? height : 0),
    };
}

bool Window::isMinimized() const
{
    if (!window_)
    {
        return true;
    }

    const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
    // On X11, minimized windows report SDL_WINDOW_MINIMIZED. On Wayland,
    // compositors may report SDL_WINDOW_HIDDEN instead (there is no
    // traditional minimize concept). Occluded is not minimized: it means
    // fully covered, but the window may still need to exist; callers should
    // also check framebuffer size before rendering.
    const bool minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
    const bool hiddenNotOccluded = (flags & SDL_WINDOW_HIDDEN) != 0 && (flags & SDL_WINDOW_OCCLUDED) == 0;
    return minimized || hiddenNotOccluded;
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
