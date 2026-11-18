#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/compute_pipeline.hpp"
#include "gpu_resources/transfer.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::gpu_resources;
using namespace starforge::vulkan;

TEST_CASE("BDA: buffer device address can be queried and is non-zero")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        auto allocator = Allocator::create({
            .instance = context->instance(),
            .physicalDevice = context->physicalDevice(),
            .device = device->device(),
        });
        REQUIRE(allocator.has_value());

        // Create a buffer with BDA usage.
        auto buffer = Buffer::create(*allocator, {
            .size = 256,
            .usage = BufferUsage::Storage,
            .memory = MemoryPlacement::CpuToGpu,
        });
        REQUIRE(buffer.has_value());

        // Write some data.
        {
            auto mapped = buffer->map();
            REQUIRE(mapped.has_value());
            std::memset(*mapped, 0, 256);
            static_cast<std::uint32_t*>(*mapped)[0] = 42;
            (void)buffer->flush(0, 256);
            buffer->unmap();
        }

        // Query device address.
        VkBufferDeviceAddressInfo addrInfo{};
        addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addrInfo.buffer = buffer->handle();
        VkDeviceAddress addr = vkGetBufferDeviceAddress(device->device(), &addrInfo);

        CHECK(addr != 0);
        // BDA alignment contract (2.3): at least 4- and 8-byte aligned.
        CHECK(addr % alignof(std::uint32_t) == 0);
        CHECK(addr % alignof(std::uint64_t) == 0);

        CHECK(allocator->allocationCount() == 1);
    }

    CHECK(context->validationErrorCount() == 0);
}

namespace
{
struct BdaPushConstants
{
    std::uint64_t srcAddress = 0;
    std::uint64_t dstAddress = 0;
    std::uint32_t count = 0;
    std::uint32_t padding = 0;
};
} // namespace

TEST_CASE("BDA: compute shader dereferences and writes through device addresses (2.3)")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();
    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());
        auto allocator = Allocator::create({
            .instance = context->instance(),
            .physicalDevice = context->physicalDevice(),
            .device = device->device(),
        });
        REQUIRE(allocator.has_value());

        {
            constexpr std::uint32_t kCount = 256;
            constexpr VkDeviceSize kSize = kCount * sizeof(std::uint32_t);

            auto staging = Buffer::create(*allocator, {
                .size = kSize, .usage = BufferUsage::TransferSrc, .memory = MemoryPlacement::CpuToGpu});
            REQUIRE(staging.has_value());
            auto src = Buffer::create(*allocator, {
                .size = kSize, .usage = BufferUsage::Storage, .memory = MemoryPlacement::GpuPreferred});
            REQUIRE(src.has_value());
            auto dst = Buffer::create(*allocator, {
                .size = kSize, .usage = BufferUsage::Storage, .memory = MemoryPlacement::GpuPreferred});
            REQUIRE(dst.has_value());
            auto readback = Buffer::create(*allocator, {
                .size = kSize, .usage = BufferUsage::TransferDst, .memory = MemoryPlacement::GpuToCpu});
            REQUIRE(readback.has_value());

            const std::uint64_t srcAddress = src->deviceAddress(device->device());
            const std::uint64_t dstAddress = dst->deviceAddress(device->device());
            REQUIRE(srcAddress != 0);
            REQUIRE(dstAddress != 0);
            CHECK(srcAddress % alignof(std::uint32_t) == 0);
            CHECK(dstAddress % alignof(std::uint32_t) == 0);

            std::vector<std::uint32_t> host(kCount);
            std::vector<std::uint32_t> expected(kCount);
            for (std::uint32_t i = 0; i < kCount; ++i)
            {
                host[i] = i * 7u + 5u;
                expected[i] = host[i] * 3u + 17u;
            }

            auto uploader = Uploader::create(*device);
            REQUIRE(uploader.has_value());
            auto pipeline = ComputePipeline::createFromFile(
                *device, std::string(STARFORGE_SHADER_DIR) + "/bda_deref.spv", "main", 1,
                static_cast<std::uint32_t>(sizeof(BdaPushConstants)));
            REQUIRE(pipeline.has_value());

            auto mapped = staging->map();
            REQUIRE(mapped.has_value());
            std::memcpy(*mapped, host.data(), kSize);
            (void)staging->flush(0, kSize);
            staging->unmap();
            REQUIRE(uploader->copyBuffer(*staging, *src, kSize).has_value());

            const BdaPushConstants push{srcAddress, dstAddress, kCount, 0};
            const Buffer* bindings[] = {&(*src)};
            REQUIRE(pipeline->dispatchAndWaitWithPushConstants(
                        bindings, &push, sizeof(push), kCount / 64).has_value());

            REQUIRE(uploader->copyBuffer(*dst, *readback, kSize).has_value());
            auto out = readback->map();
            REQUIRE(out.has_value());
            (void)readback->invalidate(0, kSize);
            CHECK(std::memcmp(*out, expected.data(), kSize) == 0);
            readback->unmap();
        }
        CHECK(allocator->allocationCount() == 0);
    }
    CHECK(context->validationErrorCount() == 0);
}
