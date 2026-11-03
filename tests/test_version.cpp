#include <doctest/doctest.h>

#include "core/version.hpp"

TEST_CASE("version string comes from CMake")
{
    CHECK(starforge::core::kVersion == "0.1.0");
}
