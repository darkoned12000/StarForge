#include "ui_menu.hpp"

namespace starforge::game
{

std::vector<MenuButton> menuButtons()
{
    return {{"resume", "RESUME", 20.0f, 70.0f, 160.0f, 36.0f},
            {"map", "SECTOR MAP (M)", 20.0f, 118.0f, 160.0f, 36.0f},
            {"quit", "QUIT", 20.0f, 166.0f, 160.0f, 36.0f}};
}

MenuAction menuActionFor(std::string_view id) noexcept
{
    if (id == "resume") return MenuAction::Resume;
    if (id == "map") return MenuAction::ToggleMap;
    if (id == "quit") return MenuAction::Quit;
    return MenuAction::None;
}

} // namespace starforge::game
