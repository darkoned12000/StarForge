// External consumer smoke test (M9.8): uses only public headers.
// Compiled with -fsyntax-only against include/ as the sole include path,
// proving no internal src/ headers are needed to use the public API.
#include "starforge/application.hpp"
#include "starforge/audio.hpp"
#include "starforge/math.hpp"
#include "starforge/renderer.hpp"
#include "starforge/starfield.hpp"
#include "starforge/toon.hpp"
#include "starforge/world.hpp"

#include <type_traits>

static_assert(std::is_move_constructible_v<starforge::Application>);
static_assert(std::is_move_constructible_v<starforge::Audio>);
static_assert(std::is_move_constructible_v<starforge::Renderer>);
static_assert(std::is_move_constructible_v<starforge::Starfield>);
static_assert(std::is_move_constructible_v<starforge::Toon>);
static_assert(!std::is_copy_constructible_v<starforge::Audio>);
static_assert(!std::is_copy_constructible_v<starforge::Renderer>);
static_assert(!std::is_copy_constructible_v<starforge::Starfield>);
static_assert(!std::is_copy_constructible_v<starforge::Toon>);

namespace
{

[[nodiscard]] starforge::math::Vec3f demoVector()
{
    using namespace starforge::math;
    const Vec3f a{1.0f, 2.0f, 3.0f};
    const Vec3f b{4.0f, 5.0f, 6.0f};
    const Vec3f c = normalize(a + b) * 2.0f - -a;
    return c * dot(a, b);
}

[[nodiscard]] starforge::WorldPosition demoPosition()
{
    auto position = starforge::WorldPosition::fromMeters(1.0, 2.0, 3.0);
    return position.has_value() ? *position : starforge::WorldPosition{};
}

[[nodiscard]] starforge::math::Mat4f demoViewProj()
{
    using namespace starforge::math;
    const auto view = lookAt(Vec3f{0.0f, 0.0f, 5.0f}, Vec3f{0.0f, 0.0f, 0.0f}, kUpY);
    return perspectiveVulkan(1.0f, 16.0f / 9.0f, 0.1f, 1000.0f) * view;
}

} // namespace

int main()
{
    const auto vector = demoVector();
    const auto position = demoPosition();
    const auto viewProj = demoViewProj();
    const auto inverse = starforge::math::inverse(viewProj);
    const starforge::Size2D size{.width = 320, .height = 240};
    (void)vector;
    (void)position;
    (void)inverse;
    (void)size;
    return 0;
}
