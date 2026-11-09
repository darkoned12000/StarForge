#include <doctest/doctest.h>

#include "content/player_delta.hpp"
#include "ecs/content_instantiation.hpp"
#include "ecs/world.hpp"

#include <cmath>
#include <vector>

using namespace starforge::content;
using namespace starforge::ecs;
using namespace starforge::spatial;

namespace
{

[[nodiscard]] bool objectsEqual(const ContentObject& a, const ContentObject& b)
{
    return a.objectId == b.objectId && a.type == b.type && a.localX == b.localX && a.localY == b.localY &&
           a.localZ == b.localZ && a.scale == b.scale && a.subtype == b.subtype &&
           a.properties == b.properties && a.orientation[0] == b.orientation[0] &&
           a.orientation[1] == b.orientation[1];
}

[[nodiscard]] bool chunksEqual(const ContentChunk& a, const ContentChunk& b)
{
    if (a.cellX != b.cellX || a.cellY != b.cellY || a.cellZ != b.cellZ || a.level != b.level || a.seed != b.seed ||
        a.objects.size() != b.objects.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.objects.size(); ++i)
    {
        if (!objectsEqual(a.objects[i], b.objects[i]))
        {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("Overlay: deltas apply at instantiation without mutating the base chunk")
{
    const auto base = generateContent(42, 0, 0, 0, 0);
    REQUIRE(!base.objects.empty());

    DeltaStore store;
    PlayerDelta removed;
    removed.cellX = 0;
    removed.cellY = 0;
    removed.cellZ = 0;
    removed.objectId = base.objects.front().objectId;
    removed.type = DeltaType::Removed;
    store.apply(removed);

    PlayerDelta modified;
    modified.cellX = 0;
    modified.cellY = 0;
    modified.cellZ = 0;
    if (base.objects.size() > 1)
    {
        modified.objectId = base.objects[1].objectId;
        modified.type = DeltaType::Modified;
        modified.scale = 3.0f;
        store.apply(modified);
    }

    const auto overlay = applyDeltas(base, store);
    CHECK(overlay.objects.size() == base.objects.size() - 1);
    // Base bytes never change.
    CHECK(chunksEqual(generateContent(42, 0, 0, 0, 0), base));

    Ecs world;
    auto mounted = instantiateChunk(world, overlay);
    CHECK(world.entityCount() == overlay.objects.size());
    unmountChunk(mounted);
    CHECK(world.entityCount() == 0);
}

TEST_CASE("Overlay: save/load restores byte-exact final state")
{
    const auto base = generateContent(42, 0, 0, 0, 0);
    REQUIRE(!base.objects.empty());

    DeltaStore original;
    PlayerDelta delta;
    delta.cellX = 0;
    delta.cellY = 0;
    delta.cellZ = 0;
    delta.objectId = base.objects.front().objectId;
    delta.type = DeltaType::Removed;
    original.apply(delta);

    DeltaStore restored;
    REQUIRE(restored.deserialize(original.serialize()) == 1);

    // Both stores apply the same overlay to an identical base.
    CHECK(chunksEqual(applyDeltas(base, original), applyDeltas(base, restored)));

    // The base bytes are unchanged across the round trip.
    CHECK(chunksEqual(generateContent(42, 0, 0, 0, 0), base));
}

TEST_CASE("Overlay: added content mounts alongside generated content")
{
    const auto base = generateContent(42, 0, 0, 0, 0);

    DeltaStore store;
    PlayerDelta added;
    added.cellX = 0;
    added.cellY = 0;
    added.cellZ = 0;
    added.objectId = 9999;
    added.type = DeltaType::Added;
    added.objectType = ContentType::Station;
    added.scale = 5.0f;
    store.apply(added);

    const auto overlay = applyDeltas(base, store);
    CHECK(overlay.objects.size() == base.objects.size() + 1);

    Ecs world;
    auto mounted = instantiateChunk(world, overlay);
    CHECK(world.entityCount() == overlay.objects.size());
    unmountChunk(mounted);
}

TEST_CASE("Overlay: multi-level chunks mount in base-cell space")
{
    const auto chunk = generateContent(42, -2, 1, -3, 1);
    REQUIRE(!chunk.objects.empty());

    Ecs world;
    auto mounted = instantiateChunk(world, chunk);
    REQUIRE(mounted.entities.size() == chunk.objects.size());

    constexpr std::int64_t kCellsPerAxis = 1LL << WorldPosition::kCellBits;
    const auto& position = mounted.entities.front().get<Transform>().position;
    CHECK(position.cellX >= chunk.cellX * kCellsPerAxis);
    CHECK(position.cellX < (chunk.cellX + 1) * kCellsPerAxis);
    CHECK(position.cellY >= chunk.cellY * kCellsPerAxis);
    CHECK(position.cellY < (chunk.cellY + 1) * kCellsPerAxis);
    CHECK(position.cellZ >= chunk.cellZ * kCellsPerAxis);
    CHECK(position.cellZ < (chunk.cellZ + 1) * kCellsPerAxis);

    unmountChunk(mounted);
}
