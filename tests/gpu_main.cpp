#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "vulkan/vulkan_context.hpp"

int main(int argc, char** argv)
{
    starforge::vulkan::ContextCreateInfo probeInfo;
    probeInfo.enableValidation = false;
    probeInfo.requiredExtensions.clear();

    if (auto probe = starforge::vulkan::VulkanContext::create(probeInfo); !probe)
    {
        return 77; // CTest SKIP_RETURN_CODE when no suitable GPU is available
    }

    doctest::Context ctx;
    ctx.applyCommandLine(argc, argv);
    return ctx.run();
}
