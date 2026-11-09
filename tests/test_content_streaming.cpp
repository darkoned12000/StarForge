#include <doctest/doctest.h>

#include "content/cell_traversal.hpp"
#include "content/content_generation.hpp"

#include <cmath>
#include <set>
#include <tuple>

using namespace starforge::content;
using namespace starforge::spatial;

namespace
{

[[nodiscard]] std::tuple<int, std::int64_t, std::int64_t, std::int64_t>
key(const CellCoord& cell) noexcept
{
    return {cell.level, cell.x, cell.y, cell.z};
}

[[nodiscard]] std::set<std::tuple<int, std::int64_t, std::int64_t, std::int64_t>> keys(
    const std::vector<CellCoord>& cells)
{
    std::set<std::tuple<int, std::int64_t, std::int64_t, std::int64_t>> result;
    for (const auto& cell : cells)
    {
        result.insert(key(cell));
    }
    return result;
}

[[nodiscard]] WorldPosition fromX(double x)
{
    auto position = WorldPosition::fromMeters(x, 0.0, 0.0);
    REQUIRE(position.has_value());
    return *position;
}

} // namespace

TEST_CASE("Streaming traversal: loaded cells are stable at a fixed camera")
{
    StreamingTraversal stream(10000.0);
    const auto first = stream.advance(fromX(0.0), 0, 0, 1000.0);
    REQUIRE(!first.loaded.empty());
    CHECK(first.unloaded.empty());
    CHECK(stream.cells() == first.loaded);

    const auto second = stream.advance(fromX(0.0), 0, 0, 1000.0);
    CHECK(second.loaded.empty());
    CHECK(second.unloaded.empty());
}

TEST_CASE("Streaming traversal: a path loads each cell exactly once")
{
    StreamingTraversal stream(1000.0);
    std::set<std::int64_t> seenX;
    std::vector<std::int64_t> loadedX;

    // Start at cell 0, then approach each following boundary so the
    // next cell enters the radius without ever re-entering an old one.
    // Positioned near the y/z centre, only the x column matches the
    // explicit coordinate filter.
    for (int i = 0; i < 4; ++i)
    {
        const double x = WorldPosition::kCellSize * 0.5 +
                         (i == 0 ? 0.0 : static_cast<double>(i) * WorldPosition::kCellSize - WorldPosition::kCellSize * 0.5 - 50.0);
        auto position = WorldPosition::fromMeters(x, WorldPosition::kCellSize * 0.5, WorldPosition::kCellSize * 0.5);
        REQUIRE(position.has_value());
        const auto update = stream.advance(*position, 0, 0, 100000.0);
        for (const auto& cell : update.loaded)
        {
            if (cell.y == 0 && cell.z == 0)
            {
                REQUIRE(seenX.insert(cell.x).second);
                loadedX.push_back(cell.x);
            }
        }
    }

    CHECK(loadedX == std::vector<std::int64_t>{0, 1, 2, 3});
}

TEST_CASE("Streaming traversal: hysteresis prevents boundary oscillation")
{
    StreamingTraversal stream(200.0);
    const double boundary = WorldPosition::kCellSize;
    const auto at = [](double x) {
        auto position = WorldPosition::fromMeters(x, WorldPosition::kCellSize * 0.5, WorldPosition::kCellSize * 0.5);
        REQUIRE(position.has_value());
        return *position;
    };

    // Camera 50 m into cell 0: neighbour is inside the load radius.
    const auto entered = stream.advance(at(boundary - 50.0), 0, 0, 100.0);
    REQUIRE(stream.contains({1, 0, 0, 0}));

    // Move 50 m backwards: plain radius would drop the neighbour, the
    // expanded radius keeps it loaded.
    const auto retained = stream.advance(at(boundary - 150.0), 0, 0, 100.0);
    CHECK(retained.loaded.empty());
    CHECK(retained.unloaded.empty());
    CHECK(stream.contains({1, 0, 0, 0}));

    // Jitter inside the dead band: still no unload.
    const auto jitter = stream.advance(at(boundary - 120.0), 0, 0, 100.0);
    CHECK(jitter.loaded.empty());
    CHECK(jitter.unloaded.empty());

    // Only once the expanded radius is exceeded does it unload.
    const auto left = stream.advance(at(boundary - 400.0), 0, 0, 100.0);
    CHECK(left.loaded.empty());
    REQUIRE(left.unloaded.size() >= 1);
    CHECK(left.unloaded[0].x == 1);
    CHECK(!stream.contains({1, 0, 0, 0}));
}

TEST_CASE("Streaming traversal: invalid radius leaves the active set unchanged")
{
    StreamingTraversal stream(0.0);
    const auto first = stream.advance(fromX(0.0), 0, 0, 1000.0);
    REQUIRE(!first.loaded.empty());

    const auto invalid = stream.advance(fromX(0.0), 0, 0, -1.0);
    CHECK(invalid.loaded.empty());
    CHECK(invalid.unloaded.empty());
    CHECK(stream.cells() == first.loaded);
}

TEST_CASE("Streaming traversal: traversal output has no duplicates at world extremes")
{
    auto position = WorldPosition::fromMeters(1.0e24, -1.0e24, 2.0e24);
    REQUIRE(position.has_value());

    const auto cells = traverseCells(*position, 0, kMaxScaleLevel, 1.0e13);
    const auto unique = keys(cells);
    CHECK(unique.size() == cells.size());
}
