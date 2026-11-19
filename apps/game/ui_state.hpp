#pragma once

// M16.4: pure-CPU UI state — screens, per-frame widget hit-testing and
// keyboard focus. No SDL/Vulkan/math dependencies; fully T1-testable.
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace starforge::game
{

enum class UiScreen
{
    Flight,
    Pause,
    Inventory,
    Station,
    Comms,
    Map,
    Menu,
};

struct UiRect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(float px, float py) const noexcept
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

// One frame's widget registration. Hit state is derived at addWidget() from
// the cursor/click supplied to beginFrame(), so the game only builds layout.
struct UiWidgetFrame
{
    std::string id;
    UiRect rect;
    bool hovered = false;
    bool clicked = false;
};

class UiState
{
public:
    // Call once per frame before adding widgets.
    void beginFrame(float mouseX, float mouseY, bool clickEdge) noexcept;

    // Register an interactive widget in draw/hit order (last wins on overlap).
    void addWidget(std::string id, UiRect rect);

    [[nodiscard]] bool hovered(std::string_view id) const noexcept;
    [[nodiscard]] bool clicked(std::string_view id) const noexcept;
    [[nodiscard]] std::size_t widgetCount() const noexcept { return widgets_.size(); }

    // Focus is keyed by widget id and survives per-frame rebuilds.
    void setFocus(std::string id);
    [[nodiscard]] std::string_view focus() const noexcept { return focus_; }
    void focusNext() noexcept;
    void focusPrevious() noexcept;

    // Screen stack: Flight is the base; overlays push/pop on top.
    void setScreen(UiScreen screen) noexcept;
    void pushScreen(UiScreen screen);
    void popScreen() noexcept;
    [[nodiscard]] UiScreen screen() const noexcept { return screens_.back(); }
    [[nodiscard]] std::size_t stackDepth() const noexcept { return screens_.size(); }

private:
    static std::size_t findWidget(const std::vector<UiWidgetFrame>& widgets,
                                  std::string_view id) noexcept;

    std::vector<UiWidgetFrame> widgets_;
    std::vector<UiScreen> screens_{UiScreen::Flight};
    std::string focus_;
    float mouseX_ = 0.0f;
    float mouseY_ = 0.0f;
    bool clickEdge_ = false;
};

} // namespace starforge::game
