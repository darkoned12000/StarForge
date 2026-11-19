#include "ui_state.hpp"

#include <algorithm>
#include <utility>

namespace starforge::game
{

void UiState::beginFrame(float mouseX, float mouseY, bool clickEdge) noexcept
{
    widgets_.clear();
    mouseX_ = mouseX;
    mouseY_ = mouseY;
    clickEdge_ = clickEdge;
}

std::size_t UiState::findWidget(const std::vector<UiWidgetFrame>& widgets,
                                std::string_view id) noexcept
{
    // Reverse search: later registrations are drawn (and hit) on top.
    for (std::size_t i = widgets.size(); i-- > 0;)
        if (widgets[i].id == id) return i;
    return widgets.size();
}

void UiState::addWidget(std::string id, UiRect rect)
{
    UiWidgetFrame widget;
    widget.hovered = rect.contains(mouseX_, mouseY_);
    widget.clicked = widget.hovered && clickEdge_;
    if (widget.hovered)
    {
        // Topmost wins: this widget is drawn/hit last, so earlier widgets
        // under the cursor lose hover and click for this frame.
        for (auto& earlier : widgets_)
        {
            if (earlier.rect.contains(mouseX_, mouseY_))
            {
                earlier.hovered = false;
                earlier.clicked = false;
            }
        }
        focus_ = id; // hover takes focus immediately
    }
    widget.id = std::move(id);
    widget.rect = rect;
    widgets_.push_back(std::move(widget));
}

bool UiState::hovered(std::string_view id) const noexcept
{
    const std::size_t i = findWidget(widgets_, id);
    return i < widgets_.size() && widgets_[i].hovered;
}

bool UiState::clicked(std::string_view id) const noexcept
{
    const std::size_t i = findWidget(widgets_, id);
    return i < widgets_.size() && widgets_[i].clicked;
}

void UiState::setFocus(std::string id)
{
    focus_ = std::move(id);
}

void UiState::focusNext() noexcept
{
    if (widgets_.empty()) return;
    for (std::size_t i = 0; i < widgets_.size(); ++i)
        if (widgets_[i].id == focus_)
        {
            focus_ = widgets_[(i + 1) % widgets_.size()].id;
            return;
        }
    focus_ = widgets_.front().id;
}

void UiState::focusPrevious() noexcept
{
    if (widgets_.empty()) return;
    for (std::size_t i = 0; i < widgets_.size(); ++i)
        if (widgets_[i].id == focus_)
        {
            focus_ = widgets_[(i + widgets_.size() - 1) % widgets_.size()].id;
            return;
        }
    focus_ = widgets_.back().id;
}

void UiState::setScreen(UiScreen screen) noexcept
{
    screens_.clear();
    screens_.push_back(screen);
}

void UiState::pushScreen(UiScreen screen)
{
    screens_.push_back(screen);
}

void UiState::popScreen() noexcept
{
    if (screens_.size() > 1) screens_.pop_back();
}

} // namespace starforge::game
