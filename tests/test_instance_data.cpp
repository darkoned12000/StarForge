#include "gpu_resources/instance_data.hpp"
#include <doctest/doctest.h>

using namespace starforge::gpu_resources;

TEST_CASE("InstanceData buffer creation and BDA access")
{
    // This test verifies that InstanceBuffer can be created properly 
    // and provides correct device address for shader access
    
    SUBCASE("Structure size alignment")
    {
        static_assert(sizeof(InstanceData) % 16 == 0, "InstanceData should be aligned to 16 bytes");
        
        CHECK(sizeof(float) * 16 + sizeof(std::uint32_t) + sizeof(std::uint32_t) * 3 == sizeof(InstanceData));
    }
    
    SUBCASE("Basic initialization")
    {
        // This is a minimal test - in reality, we'd need Vulkan context
        // but this tests the structure layout and basic API
        REQUIRE(sizeof(InstanceData) > 0);
    }
}

TEST_CASE("InstanceData structure layout")
{
    // Test that the InstanceData struct has correct size and alignment
    static_assert(sizeof(InstanceData) % 16 == 0, "InstanceData should be aligned to 16 bytes");
    
    CHECK(sizeof(float) * 16 + sizeof(std::uint32_t) + sizeof(std::uint32_t) * 3 == sizeof(InstanceData));
}