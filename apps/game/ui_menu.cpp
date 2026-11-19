#include "ui_menu.hpp"

namespace starforge::game
{

std::vector<MenuButton> menuButtons()
{
    // Panel is 220 px wide; 180 px buttons leave a 20 px margin on each
    // side. Labels stay short enough to fit inside the button at scale 2
    // (12 px per glyph cell: 10 chars = 120 px + 10 px inset < 180 px).
    return {{"resume", "RESUME", 20.0f, 70.0f, 180.0f, 36.0f},
            {"map", "SECTOR MAP", 20.0f, 118.0f, 180.0f, 36.0f},
            {"quit", "QUIT", 20.0f, 166.0f, 180.0f, 36.0f}};
}

MenuAction menuActionFor(std::string_view id) noexcept
{
    if (id == "resume") return MenuAction::Resume;
    if (id == "map") return MenuAction::ToggleMap;
    if (id == "quit") return MenuAction::Quit;
    return MenuAction::None;
}

} // namespace starforge::game
