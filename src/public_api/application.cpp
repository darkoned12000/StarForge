#include "starforge/application.hpp"
#include "application_impl.hpp"

#include <limits>
#include <utility>

namespace starforge
{

Application::Application(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
{
}

Application::~Application() = default;

Application::Application(Application&& other) noexcept : impl_(std::move(other.impl_))
{
}

Application& Application::operator=(Application&& other) noexcept
{
    if (this != &other)
    {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::expected<Application, std::string> Application::create(const CreateInfo& info)
{
    if (info.title.empty())
    {
        return std::unexpected("Application title must not be empty");
    }
    constexpr auto kMaxDimension = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    if (info.width == 0 || info.height == 0 || info.width > kMaxDimension || info.height > kMaxDimension)
    {
        return std::unexpected("Application width and height must be non-zero and within valid range");
    }

    auto window = platform::Window::create({
        .title = std::string(info.title),
        .width = static_cast<int>(info.width),
        .height = static_cast<int>(info.height),
        .resizable = info.resizable,
    });
    if (!window)
    {
        return std::unexpected(window.error());
    }

    return Application(std::make_unique<Impl>(std::move(*window), std::string(info.title)));
}

void Application::pollEvents() noexcept
{
    if (impl_)
    {
        impl_->window.pollEvents();
    }
}

bool Application::shouldClose() const noexcept
{
    return !impl_ || impl_->window.shouldClose();
}

bool Application::takeResized() noexcept
{
    return impl_ ? impl_->window.takeResized() : false;
}

Size2D Application::framebufferSize() const noexcept
{
    if (!impl_)
    {
        return {};
    }

    const auto size = impl_->window.framebufferSize();
    return Size2D{.width = size.width, .height = size.height};
}

bool Application::isMinimized() const noexcept
{
    return impl_ ? impl_->window.isMinimized() : true;
}

const std::string& Application::title() const noexcept
{
    static const std::string kEmpty;
    return impl_ ? impl_->title : kEmpty;
}

bool Application::isKeyDown(Key key) const noexcept
{
    if (!impl_)
    {
        return false;
    }
    platform::PlatformKey platformKey = platform::PlatformKey::Unknown;
    switch (key)
    {
    case Key::W:
        platformKey = platform::PlatformKey::W;
        break;
    case Key::A:
        platformKey = platform::PlatformKey::A;
        break;
    case Key::S:
        platformKey = platform::PlatformKey::S;
        break;
    case Key::D:
        platformKey = platform::PlatformKey::D;
        break;
    case Key::Space:
        platformKey = platform::PlatformKey::Space;
        break;
    case Key::P:
        platformKey = platform::PlatformKey::P;
        break;
    case Key::Escape:
        platformKey = platform::PlatformKey::Escape;
        break;
    case Key::Up:
        platformKey = platform::PlatformKey::Up;
        break;
    case Key::Down:
        platformKey = platform::PlatformKey::Down;
        break;
    case Key::Left:
        platformKey = platform::PlatformKey::Left;
        break;
    case Key::Right:
        platformKey = platform::PlatformKey::Right;
        break;
    default:
        return false;
    }
    return impl_->window.isKeyDown(platformKey);
}

bool Application::isMouseButtonDown(MouseButton button) const noexcept
{
    if (!impl_)
    {
        return false;
    }
    platform::PlatformMouseButton platformButton = platform::PlatformMouseButton::Left;
    switch (button)
    {
    case MouseButton::Left:
        platformButton = platform::PlatformMouseButton::Left;
        break;
    case MouseButton::Right:
        platformButton = platform::PlatformMouseButton::Right;
        break;
    case MouseButton::Middle:
        platformButton = platform::PlatformMouseButton::Middle;
        break;
    default:
        return false;
    }
    return impl_->window.isMouseButtonDown(platformButton);
}

MouseMotion Application::takeMouseMotion() noexcept
{
    float dx = 0.0f;
    float dy = 0.0f;
    float scroll = 0.0f;
    if (impl_)
    {
        impl_->window.takeMouseMotion(dx, dy, scroll);
    }
    return MouseMotion{.dx = dx, .dy = dy, .scrollY = scroll};
}

void Application::setRelativeMouseMode(bool enabled) noexcept
{
    if (impl_)
    {
        impl_->window.setRelativeMouseMode(enabled);
    }
}

} // namespace starforge
