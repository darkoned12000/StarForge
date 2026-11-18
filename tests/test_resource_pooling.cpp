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

namespace
{

constexpr std::uint32_t kCount = 256;
constexpr std::uint32_t kWorkgroupSize = 64;
constexpr VkDeviceSize kSize = kCount * sizeof(std::uint32_t);

std::uint32_t pattern(std::uint32_t op, std::uint32_t index)
{
    return op * 1000u + index * 7u + 5u;
}

} // namespace

TEST_CASE("Resource pooling: Uploader reuses its command buffer and fence (2.2)")
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
            auto staging = Buffer::create(*allocator, {
                .size = kSize, .usage = BufferUsage::TransferSrc, .memory = MemoryPlacement::CpuToGpu});
            REQUIRE(staging.has_value());
            auto deviceBuf = Buffer::create(*allocator, {
                .size = kSize, .usage = BufferUsage::TransferSrcDst, .memory = MemoryPlacement::GpuPreferred});
            REQUIRE(deviceBuf.has_value());
            auto readback = Buffer::create(*allocator, {
                .size = kSize, .usage = BufferUsage::TransferDst, .memory = MemoryPlacement::GpuToCpu});
            REQUIRE(readback.has_value());

            auto uploader = Uploader::create(*device);
            REQUIRE(uploader.has_value());

            for (std::uint32_t op = 0; op < 5; ++op)
            {
                std::vector<std::uint32_t> src(kCount);
                for (std::uint32_t i = 0; i < kCount; ++i) src[i] = pattern(op, i);

                auto mapped = staging->map();
                REQUIRE(mapped.has_value());
                std::memcpy(*mapped, src.data(), kSize);
                (void)staging->flush(0, kSize);
                staging->unmap();

                REQUIRE(uploader->copyBuffer(*staging, *deviceBuf, kSize).has_value());
                REQUIRE(uploader->copyBuffer(*deviceBuf, *readback, kSize).has_value());

                auto out = readback->map();
                REQUIRE(out.has_value());
                (void)readback->invalidate(0, kSize);
                CHECK(std::memcmp(*out, src.data(), kSize) == 0);
                readback->unmap();
            }
            CHECK(uploader->operationCount() == 10);
        }
        CHECK(allocator->allocationCount() == 0);
    }
    CHECK(context->validationErrorCount() == 0);
}

TEST_CASE("Resource pooling: ComputePipeline reuses its command buffer and fence (2.2)")
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
            auto staging = Buffer::create(*allocator, {
                .size = kSize, .usage = BufferUsage::TransferSrc, .memory = MemoryPlacement::CpuToGpu});
            REQUIRE(staging.has_value());
            auto input = Buffer::create(*allocator, {
                .size = kSize, .usage = BufferUsage::Storage, .memory = MemoryPlacement::GpuPreferred});
            REQUIRE(input.has_value());
            auto output = Buffer::create(*allocator, {
                .size = kSize, .usage = BufferUsage::Storage, .memory = MemoryPlacement::GpuPreferred});
            REQUIRE(output.has_value());
            auto readback = Buffer::create(*allocator, {
                .size = kSize, .usage = BufferUsage::TransferDst, .memory = MemoryPlacement::GpuToCpu});
            REQUIRE(readback.has_value());

            auto uploader = Uploader::create(*device);
            REQUIRE(uploader.has_value());
            auto pipeline = ComputePipeline::createFromFile(
                *device, std::string(STARFORGE_SHADER_DIR) + "/test_compute.spv", "main", 2);
            REQUIRE(pipeline.has_value());

            for (std::uint32_t op = 0; op < 5; ++op)
            {
                std::vector<std::uint32_t> src(kCount);
                std::vector<std::uint32_t> expected(kCount);
                for (std::uint32_t i = 0; i < kCount; ++i)
                {
                    src[i] = pattern(op, i);
                    expected[i] = src[i] * 3u + 17u;
                }

                auto mapped = staging->map();
                REQUIRE(mapped.has_value());
                std::memcpy(*mapped, src.data(), kSize);
                (void)staging->flush(0, kSize);
                staging->unmap();
                REQUIRE(uploader->copyBuffer(*staging, *input, kSize).has_value());

                const Buffer* bindings[] = {&(*input), &(*output)};
                REQUIRE(pipeline->dispatchAndWait(bindings, kCount / kWorkgroupSize).has_value());
                REQUIRE(uploader->copyBuffer(*output, *readback, kSize).has_value());

                auto out = readback->map();
                REQUIRE(out.has_value());
                (void)readback->invalidate(0, kSize);
                CHECK(std::memcmp(*out, expected.data(), kSize) == 0);
                readback->unmap();
            }
            CHECK(pipeline->operationCount() == 5);
        }
        CHECK(allocator->allocationCount() == 0);
    }
    CHECK(context->validationErrorCount() == 0);
}
