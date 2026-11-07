#pragma once

#include "starforge/application.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace starforge
{

// Backend-neutral renderer owner. Owns the swapchain, frame synchronization,
// and presentation; the backend selects and owns color/depth formats, queues,
// fences, and semaphores. Move-only; destruction releases all GPU resources.
//
// The application passed to create() must outlive the renderer.
// Record callbacks for drawing scene content arrive with the domain facades
// (M9.5); this facade covers swapchain lifecycle, clear frames, resize
// handling, and presentation.
class Renderer
{
public:
    struct CreateInfo
    {
        std::uint32_t framesInFlight = 2;
    };

    struct ClearColor
    {
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 1.0f;
    };

    enum class FrameResult
    {
        Presented,
        SwapchainOutOfDate,
    };

    [[nodiscard]] static std::expected<Renderer, std::string> create(Application& app, const CreateInfo& info);

    ~Renderer();
    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Records a clear frame, submits it, and presents it.
    // Returns SwapchainOutOfDate when the swapchain must be recreated
    // (e.g. after a resize); call recreate() and retry the next frame.
    [[nodiscard]] std::expected<FrameResult, std::string> renderClearedFrame(const ClearColor& clearColor);

    // Blocks until all submitted GPU work has completed. Call before
    // destroying or reassigning a renderer that has presented frames.
    void waitIdle() const noexcept;

    // Recreates the swapchain at the application's current framebuffer size.
    // Fails when the framebuffer is minimized or empty.
    [[nodiscard]] std::expected<void, std::string> recreate();

    [[nodiscard]] Size2D extent() const noexcept;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
