#pragma once

// Internal conversion helpers between public starforge math types and
// public_api_bridge plain-data types. Not installed.

#include "bridge_types.hpp"
#include "world_bridge.hpp"
#include "starforge/math.hpp"
#include "starforge/world.hpp"

#include <cstddef>

namespace starforge::public_api_convert
{
namespace bridge = public_api_bridge;

[[nodiscard]] inline bridge::RawVec2f toRaw(const math::Vec2f& value) noexcept
{
    return {value.x(), value.y()};
}

[[nodiscard]] inline bridge::RawVec3f toRaw(const math::Vec3f& value) noexcept
{
    return {value.x(), value.y(), value.z()};
}

[[nodiscard]] inline bridge::RawVec4f toRaw(const math::Vec4f& value) noexcept
{
    return {value.x(), value.y(), value.z(), value.w()};
}

[[nodiscard]] inline bridge::RawVec3d toRaw(const math::Vec3d& value) noexcept
{
    return {value.x(), value.y(), value.z()};
}

[[nodiscard]] inline bridge::RawMat4f toRaw(const math::Mat4f& value) noexcept
{
    bridge::RawMat4f result;
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result.columns[column][row] = value.columns[column][row];
        }
    }
    return result;
}

[[nodiscard]] inline bridge::RawWorldPosition toRaw(const WorldPosition& value) noexcept
{
    return {value.cellX, value.cellY, value.cellZ, value.offsetX, value.offsetY, value.offsetZ};
}

} // namespace starforge::public_api_convert
