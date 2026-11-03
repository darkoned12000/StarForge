#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/transfer.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

namespace
{

std::uint32_t patternValue(std::uint32_t index) noexcept
{
    return (index * 2654435761u) ^ 0xA5A55A5Au;
}

} // namespace

TEST_CASE("GPU resources: 1 MiB staging upload, offset copy, readback, and allocation accounting")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        Allocator::CreateInfo allocInfo;
        allocInfo.instance = context->instance();
        allocInfo.physicalDevice = context->physicalDevice();
        allocInfo.device = device->device();
        auto allocator = Allocator::create(allocInfo);
        REQUIRE(allocator.has_value());
        CHECK(allocator->allocationCount() == 0);

        {
            constexpr std::uint32_t wordCount = (1024 * 1024) / sizeof(std::uint32_t);
            constexpr VkDeviceSize size = wordCount * sizeof(std::uint32_t);
            std::vector<std::uint32_t> data(wordCount);
            for (std::uint32_t i = 0; i < wordCount; ++i)
            {
                data[i] = patternValue(i);
            }

            Buffer::CreateInfo stagingInfo;
            stagingInfo.size = size;
            stagingInfo.usage = BufferUsage::TransferSrc;
            stagingInfo.memory = MemoryPlacement::CpuToGpu;
            auto staging = Buffer::create(*allocator, stagingInfo);
            REQUIRE(staging.has_value());

            {
                auto mapped = staging->map();
                REQUIRE(mapped.has_value());
                std::memcpy(*mapped, data.data(), size);
                staging->flush(0, size);
                staging->unmap();
            }

            Buffer::CreateInfo deviceInfoBuf;
            deviceInfoBuf.size = size;
            deviceInfoBuf.usage = BufferUsage::TransferSrcDst;
            deviceInfoBuf.memory = MemoryPlacement::GpuPreferred;
            auto deviceBuf = Buffer::create(*allocator, deviceInfoBuf);
            REQUIRE(deviceBuf.has_value());

            Buffer::CreateInfo readbackInfo;
            readbackInfo.size = size;
            readbackInfo.usage = BufferUsage::TransferDst;
            readbackInfo.memory = MemoryPlacement::GpuToCpu;
            auto readback = Buffer::create(*allocator, readbackInfo);
            REQUIRE(readback.has_value());

            CHECK(allocator->allocationCount() == 3);

            auto uploader = Uploader::create(*device);
            REQUIRE(uploader.has_value());

            // Full 1 MiB roundtrip through two offset halves to exercise non-zero srcOffset and dstOffset.
            constexpr VkDeviceSize halfSize = size / 2;
            REQUIRE(uploader->copyBuffer(*staging, *deviceBuf, halfSize, 0, 0).has_value());
            REQUIRE(uploader->copyBuffer(*staging, *deviceBuf, halfSize, halfSize, halfSize).has_value());
            REQUIRE(uploader->copyBuffer(*deviceBuf, *readback, size, 0, 0).has_value());

            // Bounds check rejects out-of-range copies cleanly.
            CHECK_FALSE(uploader->copyBuffer(*staging, *deviceBuf, size, 64, 0).has_value());

            {
                auto mapped = readback->map();
                REQUIRE(mapped.has_value());
                readback->invalidate(0, size);
                CHECK(std::memcmp(*mapped, data.data(), size) == 0);
                readback->unmap();
            }
        }

        // All buffers destroyed before allocator teardown.
        CHECK(allocator->allocationCount() == 0);
    }

    // Asserted after all child Vulkan objects and the logical device are destroyed.
    CHECK(context->validationErrorCount() == 0);
}
