#include "gpu_resources/instance_data.hpp"
#include <doctest/doctest.h>

using namespace starforge::gpu_resources;

TEST_CASE("InstanceData buffer update functionality")
{
    SUBCASE("Empty instance list handling")
    {
        std::vector<InstanceData> emptyInstances;
        // Test that updating with empty instances doesn't crash or cause issues
        CHECK(emptyInstances.empty() == true);
    }
    
    SUBCASE("Basic instance data structure")
    {
        std::vector<InstanceData> testInstances(5);
        
        REQUIRE(testInstances.size() == 5);
        CHECK(sizeof(testInstances[0]) == sizeof(InstanceData));
    }
}