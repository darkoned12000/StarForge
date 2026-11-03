#include <doctest/doctest.h>

#include "vulkan/vulkan_version.hpp"

using namespace starforge::vulkan;

TEST_CASE("API version floor is 1.3")
{
    CHECK_FALSE(meetsMinimumApiVersion(VK_API_VERSION_1_0));
    CHECK_FALSE(meetsMinimumApiVersion(VK_API_VERSION_1_2));
    CHECK(meetsMinimumApiVersion(VK_API_VERSION_1_3));
    CHECK(meetsMinimumApiVersion(VK_API_VERSION_1_4));
    CHECK(meetsMinimumApiVersion(VK_MAKE_API_VERSION(0, 1, 4, 309)));
}

TEST_CASE("API version formatting")
{
    CHECK(apiVersionString(VK_MAKE_API_VERSION(0, 1, 4, 309)) == "1.4.309");
    CHECK(apiVersionString(VK_API_VERSION_1_3) == "1.3.0");
}

TEST_CASE("preferred version is not below the floor")
{
    CHECK(kPreferredApiVersion >= kMinimumApiVersion);
}
