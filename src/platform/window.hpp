#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

struct SDL_Window;

namespace starforge::platform
{

struct WindowDesc
{
    std::string title = "STARFORGE";
    int         width = 1280;
    int         height = 720;
    bool        resizable = true;
};

struct Extent2D
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Input state values (platform side). No public types here so Window
// stays independent of the public API headers; Application maps these.
enum class PlatformKey
{
    Unknown = 0,
    W,
    A,
    S,
    D,
    Space,
    P,
    Escape,
    Count,
};

enum class PlatformMouseButton
{
    Left = 0,
    Right,
    Middle,
};

// Owns the SDL video subsystem, the SDL Vulkan library and one window.
// Move-only; everything is released in the destructor.
class Window
{
public:
    [[nodiscard]] static std::expected<Window, std::string> create(const WindowDesc& desc);

    ~Window();
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Drains the OS event queue and updates close/resize state.
    void pollEvents();

    [[nodiscard]] bool shouldClose() const noexcept { return shouldClose_; }

    // True once per resize; clears the flag. Swapchains use this to recreate.
    [[nodiscard]] bool takeResized() noexcept;

    [[nodiscard]] Extent2D framebufferSize() const;
    [[nodiscard]] bool isMinimized() const;

    // Input state.
    [[nodiscard]] bool isKeyDown(PlatformKey key) const noexcept;
    [[nodiscard]] bool isMouseButtonDown(PlatformMouseButton button) const noexcept;
    [[nodiscard]] void takeMouseMotion(float& dxOut, float& dyOut) noexcept;
    void setRelativeMouseMode(bool enabled) noexcept;

    // Instance extensions the windowing system needs for Vulkan presentation.
    [[nodiscard]] std::expected<std::vector<const char*>, std::string>
    vulkanInstanceExtensions() const;

    // Backend-internal access (surface creation). Not part of the public API.
    [[nodiscard]] SDL_Window* sdlHandle() const noexcept { return window_; }
    [[nodiscard]] std::expected<VkSurfaceKHR, std::string> createVulkanSurface(VkInstance instance) const;

private:
    static constexpr std::size_t keyCount = static_cast<std::size_t>(PlatformKey::Count);
    explicit Window(SDL_Window* window) noexcept : window_(window) {}
    void destroy() noexcept;

    SDL_Window* window_ = nullptr;
    bool        shouldClose_ = false;
    bool        resized_ = false;
    bool        keyDown_[keyCount] = {};
    bool        mouseButtonDown_[3] = {};
    float       mouseMotionX_ = 0.0f;
    float       mouseMotionY_ = 0.0f;
};

} // namespace starforge::platform
