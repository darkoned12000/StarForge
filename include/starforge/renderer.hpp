#pragma once

#include "starforge/application.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace starforge
{

// Backend-neutral renderer owner. Owns the swapchain, frame synchronization,
// and presentation; the backend selects and owns color/depth formats, queues,
// fences, and semaphores. Move-only; destruction releases all GPU resources.
//
// The application passed to create() must outlive the renderer.
//
// Frame recording is exposed through renderFrame(): the backend sets the
// viewport and scissor for the acquired image, then invokes the caller's
// record callback with a FrameContext. Domain facades (e.g. Starfield)
// record into that context through an internal bridge; the command buffer
// itself never appears in the public interface.

// Opaque per-frame recording context passed to renderFrame() callbacks.
class FrameContext
{
public:
    [[nodiscard]] Size2D extent() const noexcept;

private:
    FrameContext() = default;

    friend class Renderer;
    friend struct RendererInternalAccess;

    void* commandBuffer_ = nullptr;
    Size2D extent_{};
};
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

    // Records `record` into the acquired image (after the clear), submits,
    // and presents. The backend sets viewport/scissor before invoking the
    // callback. Domain facades record through this callback.
    using RecordFn = std::function<void(FrameContext& frameContext)>;
    [[nodiscard]] std::expected<FrameResult, std::string>
    renderFrame(const ClearColor& clearColor, const RecordFn& record);

    // Blocks until all submitted GPU work has completed. Call before
    // destroying or reassigning a renderer that has presented frames.
    void waitIdle() const noexcept;

    enum class RecreateResult
    {
        Resized,
        FormatChanged,
    };

    // Recreates the swapchain at the application's current framebuffer size.
    // Returns FormatChanged when the backend selected a different color
    // format, in which case format-dependent facades (e.g. Starfield) must
    // be recreated. Fails when the framebuffer is minimized or empty.
    [[nodiscard]] std::expected<RecreateResult, std::string> recreate();

    [[nodiscard]] Size2D extent() const noexcept;

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl) noexcept;

    // Internal STARFORGE facades may access backend state without
    // exposing it in the public interface.
    friend struct RendererInternalAccess;

    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
