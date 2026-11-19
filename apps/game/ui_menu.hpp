#pragma once

// M16 polish: pause-menu button layout + action mapping (pure CPU).
#include <string>
#include <string_view>
#include <vector>

namespace starforge::game
{

struct MenuButton
{
    std::string id;
    std::string label;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

enum class MenuAction
{
    None,
    Resume,
    Quit,
    ToggleMap,
};

[[nodiscard]] std::vector<MenuButton> menuButtons();
[[nodiscard]] MenuAction menuActionFor(std::string_view id) noexcept;

} // namespace starforge::game
