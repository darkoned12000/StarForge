#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace starforge
{

struct Size2D
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    bool operator==(const Size2D&) const noexcept = default;
};

inline bool operator==(const Size2D& lhs, const Size2D& rhs) noexcept
{
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

// Backend-neutral application and window owner.
// Movable; destruction releases the window and its platform subsystem.
class Application
{
public:
    struct CreateInfo
    {
        std::string_view title{"STARFORGE"};
        std::uint32_t width = 1280;
        std::uint32_t height = 720;
        bool resizable = true;
    };

    [[nodiscard]] static std::expected<Application, std::string> create(const CreateInfo& info);

    ~Application();
    Application(Application&& other) noexcept;
    Application& operator=(Application&& other) noexcept;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void pollEvents() noexcept;
    [[nodiscard]] bool shouldClose() const noexcept;
    [[nodiscard]] bool takeResized() noexcept;
    [[nodiscard]] Size2D framebufferSize() const noexcept;
    [[nodiscard]] bool isMinimized() const noexcept;
    [[nodiscard]] const std::string& title() const noexcept;

private:
    struct Impl;
    explicit Application(std::unique_ptr<Impl> impl) noexcept;

    // Internal STARFORGE facades may access the platform window without
    // exposing it in the public interface.
    friend struct ApplicationInternalAccess;

    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
