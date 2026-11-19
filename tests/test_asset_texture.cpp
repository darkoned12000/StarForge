// M17.5 (T1): mip generation and budgeted texture residency.
#include <doctest/doctest.h>

#include "asset/texture_streamer.hpp"

using namespace starforge::asset;

namespace
{
TextureData solid(std::uint32_t size)
{
    TextureData texture;
    texture.width = size;
    texture.height = size;
    texture.rgba.resize(static_cast<std::size_t>(size) * size * 4, 180);
    return texture;
}
} // namespace

TEST_CASE("M17.5: mip chain halves dimensions down to 1x1")
{
    TextureMipChain chain(solid(8));
    REQUIRE(chain.mipCount() == 4); // 8, 4, 2, 1
    CHECK(chain.level(0).width == 8);
    CHECK(chain.level(1).width == 4);
    CHECK(chain.level(2).width == 2);
    CHECK(chain.level(3).width == 1);
    CHECK(chain.level(3).rgba.size() == 4);
    CHECK(chain.totalBytes() == 256 + 64 + 16 + 4);
    CHECK(chain.selectLevel(100) == 0);
    CHECK(chain.selectLevel(3) == 2);
    CHECK(chain.selectLevel(1) == 3);
}

TEST_CASE("M17.5: residency evicts least-recently-used levels within budget")
{
    StreamingTexture texture(solid(8), 80); // level0=256, level1=64, level2=16, level3=4
    REQUIRE(texture.request(1) != nullptr); // 64
    CHECK(texture.residentBytes() == 64);
    CHECK(texture.resident(1));

    REQUIRE(texture.request(2) != nullptr); // +16 = 80 exactly
    CHECK(texture.residentBytes() == 80);
    CHECK(texture.resident(1));
    CHECK(texture.resident(2));

    // Touch level 2 so level 1 is the oldest, then request level 3 (4 bytes).
    CHECK(texture.data(2) != nullptr);
    REQUIRE(texture.request(3) != nullptr);
    CHECK_FALSE(texture.resident(1)); // evicted
    CHECK(texture.resident(2));
    CHECK(texture.resident(3));

    CHECK(texture.evict(2));
    CHECK_FALSE(texture.evict(2));
    texture.evictAll();
    CHECK(texture.residentCount() == 0);
    CHECK(texture.residentBytes() == 0);

    texture.setBudget(300);
    CHECK(texture.budget() == 300);
    CHECK(texture.request(0) != nullptr); // full-res now fits
    CHECK(texture.residentBytes() == 256);
}

TEST_CASE("M17.5: stream() picks a mip from the screen footprint")
{
    StreamingTexture texture(solid(16), 1024);
    const auto* mip = texture.stream(2);
    REQUIRE(mip != nullptr);
    CHECK(mip->width == 2);
}
