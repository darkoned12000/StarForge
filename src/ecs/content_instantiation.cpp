#include "content_instantiation.hpp"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

namespace starforge::ecs
{

namespace
{

// Left-shift a coarse cell coordinate back to base-cell space with range
// checks. Returns false when the base coordinate is not representable.
[[nodiscard]] bool shiftCellCoordinate(std::int64_t value, int shift, std::int64_t& result) noexcept
{
    const std::int64_t multiplier = 1LL << shift;
    if (value > std::numeric_limits<std::int64_t>::max() / multiplier ||
        value < std::numeric_limits<std::int64_t>::min() / multiplier)
    {
        return false;
    }
    result = value * multiplier;
    return true;
}

} // namespace

MountedChunk instantiateChunk(Ecs& world, const content::ContentChunk& chunk)
{
    MountedChunk result;
    result.cell = {chunk.cellX, chunk.cellY, chunk.cellZ, chunk.level};

    // Chunk coordinates are coarse (level-shifted); fromCellAndOffset
    // takes base (level-0) cell indices, so shift them back before use.
    std::int64_t baseX = chunk.cellX;
    std::int64_t baseY = chunk.cellY;
    std::int64_t baseZ = chunk.cellZ;
    if (chunk.level > 0 && chunk.level < content::kMaxScaleLevel)
    {
        const int shift = spatial::WorldPosition::kCellBits * chunk.level;
        if (!shiftCellCoordinate(chunk.cellX, shift, baseX) ||
            !shiftCellCoordinate(chunk.cellY, shift, baseY) ||
            !shiftCellCoordinate(chunk.cellZ, shift, baseZ))
        {
            return result;
        }
    }
    const auto cellOrigin =
        spatial::WorldPosition::fromCellAndOffset(baseX, baseY, baseZ, 0.0, 0.0, 0.0);
    if (!cellOrigin.has_value())
    {
        return result;
    }

    result.entities.reserve(chunk.objects.size());
    for (const auto& object : chunk.objects)
    {
        const auto position = cellOrigin->translated({object.localX, object.localY, object.localZ});
        if (!position.has_value())
        {
            continue;
        }

        const auto entity = world.createAsteroid(*position, {});
        if (object.type != content::ContentType::Asteroid)
        {
            entity.remove<AsteroidTag>();
        }
        auto& transform = entity.get_mut<Transform>();
        transform.scale = object.scale;
        const glm::quat yaw =
            glm::angleAxis(static_cast<float>(content::asteroidYawRadians(object)), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::quat pitch =
            glm::angleAxis(static_cast<float>(content::asteroidPitchRadians(object)), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::quat orientation = yaw * pitch;
        transform.orientation = {orientation.x, orientation.y, orientation.z, orientation.w};
        entity.modified<Transform>();
        result.entities.push_back(entity);
    }
    return result;
}

void unmountChunk(MountedChunk& mounted)
{
    for (const auto entity : mounted.entities)
    {
        if (entity.is_alive())
        {
            entity.destruct();
        }
    }
    mounted.entities.clear();
}

} // namespace starforge::ecs
