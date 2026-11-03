#include <doctest/doctest.h>

#include "vulkan/vulkan_swapchain.hpp"

#include <vector>

using namespace starforge::vulkan;

TEST_CASE("chooseSurfaceFormat prefers B8G8R8A8_SRGB with SRGB_NONLINEAR")
{
    const std::vector<VkSurfaceFormatKHR> formats = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_SRGB,  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };

    const auto chosen = chooseSurfaceFormat(formats);
    REQUIRE(chosen.has_value());
    CHECK(chosen->format == VK_FORMAT_B8G8R8A8_SRGB);
    CHECK(chosen->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
}

TEST_CASE("chooseSurfaceFormat falls back to first format when preferred is absent")
{
    const std::vector<VkSurfaceFormatKHR> formats = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };

    const auto chosen = chooseSurfaceFormat(formats);
    REQUIRE(chosen.has_value());
    CHECK(chosen->format == VK_FORMAT_B8G8R8A8_UNORM);
}

TEST_CASE("chooseSurfaceFormat rejects empty format list")
{
    const std::vector<VkSurfaceFormatKHR> formats;
    const auto chosen = chooseSurfaceFormat(formats);
    CHECK_FALSE(chosen.has_value());
}

TEST_CASE("choosePresentMode prefers MAILBOX and falls back to FIFO")
{
    const std::vector<VkPresentModeKHR> withMailbox = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
    };
    CHECK(choosePresentMode(withMailbox) == VK_PRESENT_MODE_MAILBOX_KHR);

    const std::vector<VkPresentModeKHR> fifoOnly = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
    };
    CHECK(choosePresentMode(fifoOnly) == VK_PRESENT_MODE_FIFO_KHR);
}

TEST_CASE("chooseSwapchainExtent honors fixed surface extent and clamps variable extent")
{
    VkSurfaceCapabilitiesKHR fixed{};
    fixed.currentExtent = {1920, 1080};
    fixed.minImageExtent = {1, 1};
    fixed.maxImageExtent = {4096, 4096};

    const auto fixedChosen = chooseSwapchainExtent(fixed, 1280, 720);
    REQUIRE(fixedChosen.has_value());
    CHECK(fixedChosen->width == 1920);
    CHECK(fixedChosen->height == 1080);

    VkSurfaceCapabilitiesKHR variable{};
    variable.currentExtent = {UINT32_MAX, UINT32_MAX};
    variable.minImageExtent = {320, 240};
    variable.maxImageExtent = {1920, 1080};

    const auto clamped = chooseSwapchainExtent(variable, 3840, 100);
    REQUIRE(clamped.has_value());
    CHECK(clamped->width == 1920);
    CHECK(clamped->height == 240);
}

TEST_CASE("chooseSwapchainExtent rejects zero extent when minimized")
{
    VkSurfaceCapabilitiesKHR minimized{};
    minimized.currentExtent = {0, 0};
    CHECK_FALSE(chooseSwapchainExtent(minimized, 1280, 720).has_value());

    VkSurfaceCapabilitiesKHR variable{};
    variable.currentExtent = {UINT32_MAX, UINT32_MAX};
    variable.minImageExtent = {1, 1};
    variable.maxImageExtent = {4096, 4096};
    CHECK_FALSE(chooseSwapchainExtent(variable, 0, 720).has_value());
}
