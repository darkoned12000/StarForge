#pragma once

#include "starforge/application.hpp"
#include "platform/window.hpp"

#include <string>
#include <utility>

namespace starforge {

// Internal implementation of the public application owner.
// This header must not be installed or included by applications.
struct Application::Impl
{
    platform::Window window;
    std::string title;

    explicit Impl(platform::Window window, std::string title)
        : window(std::move(window)), title(std::move(title))
    {
    }
};

// Internal-only bridge for public facades such as the M9.3 renderer.
struct ApplicationInternalAccess
{
    [[nodiscard]] static platform::Window& getWindow(Application& app) noexcept
    {
        return app.impl_->window;
    }

    [[nodiscard]] static const platform::Window& getWindow(const Application& app) noexcept
    {
        return app.impl_->window;
    }
};

} // namespace starforge
