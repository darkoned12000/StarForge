#pragma once

#include <cstdint>

namespace starforge::gameplay::ui_relation
{

// How the player relates to another contact. Kept as a pure game-layer
// concept: relation drives colour AND shape everywhere (world markers,
// radar, target panel) so it survives red/green colour blindness and
// reads at long range when a hull is only a few pixels.
enum class Relation : std::uint8_t
{
    Self,
    Ally,
    Neutral,
    Hostile,
};

struct Color
{
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

// Friendly cyan and hostile red-orange remain distinguishable under
// red-green colour blindness; amber is reserved for neutral objects.
constexpr Color color(Relation relation) noexcept
{
    switch (relation)
    {
    case Relation::Self:    return {1.00f, 1.00f, 1.00f, 1.0f};
    case Relation::Ally:    return {0.25f, 0.85f, 1.00f, 1.0f};
    case Relation::Neutral: return {1.00f, 0.72f, 0.20f, 1.0f};
    case Relation::Hostile: return {1.00f, 0.35f, 0.16f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

// The same relation always uses the same silhouette: rounded brackets for
// allies, square for neutrals, diamond for hostiles. Self uses a square
// with the player's white accent.
enum class Shape : std::uint8_t
{
    Rounded,
    Square,
    Diamond,
};

constexpr Shape shape(Relation relation) noexcept
{
    switch (relation)
    {
    case Relation::Ally:    return Shape::Rounded;
    case Relation::Hostile: return Shape::Diamond;
    case Relation::Neutral:
    case Relation::Self:    return Shape::Square;
    }
    return Shape::Square;
}

constexpr const char* label(Relation relation) noexcept
{
    switch (relation)
    {
    case Relation::Self:    return "SELF";
    case Relation::Ally:    return "ALLY";
    case Relation::Neutral: return "NEUTRAL";
    case Relation::Hostile: return "HOSTILE";
    }
    return "UNKNOWN";
}

} // namespace starforge::gameplay::ui_relation
