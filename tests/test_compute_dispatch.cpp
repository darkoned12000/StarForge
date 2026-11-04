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

constexpr std::uint32_t kWorkgroupSize = 64;
constexpr std::uint32_t kPoisonValue = 0xDEADBEEFu;

constexpr std::uint32_t cpuReferenceKernel(std::uint32_t x) noexcept
{
    return x * 3u + 17u;
}

} // namespace

TEST_CASE("Compute dispatch: Slang SPIR-V against CPU reference")
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

        {
            constexpr std::uint32_t count = 1024;
            constexpr VkDeviceSize size = count * sizeof(std::uint32_t);

            std::vector<std::uint32_t> inputHost(count);
            std::vector<std::uint32_t> poisonHost(count, kPoisonValue);
            std::vector<std::uint32_t> expectedHost(count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                inputHost[i] = i * 7u + 5u;
                expectedHost[i] = cpuReferenceKernel(inputHost[i]);
            }

            auto staging = Buffer::create(*allocator, {
                .size = size,
                .usage = BufferUsage::TransferSrc,
                .memory = MemoryPlacement::CpuToGpu,
            });
            REQUIRE(staging.has_value());

            auto inputBuf = Buffer::create(*allocator, {
                .size = size,
                .usage = BufferUsage::Storage,
                .memory = MemoryPlacement::GpuPreferred,
            });
            REQUIRE(inputBuf.has_value());

            auto outputBuf = Buffer::create(*allocator, {
                .size = size,
                .usage = BufferUsage::Storage,
                .memory = MemoryPlacement::GpuPreferred,
            });
            REQUIRE(outputBuf.has_value());

            auto readback = Buffer::create(*allocator, {
                .size = size,
                .usage = BufferUsage::TransferDst,
                .memory = MemoryPlacement::GpuToCpu,
            });
            REQUIRE(readback.has_value());

            auto uploader = Uploader::create(*device);
            REQUIRE(uploader.has_value());

            // Pre-fill output buffer with poison values so stale memory cannot pass.
            {
                auto mapped = staging->map();
                REQUIRE(mapped.has_value());
                std::memcpy(*mapped, poisonHost.data(), size);
                (void)staging->flush(0, size);
                staging->unmap();
            }
            REQUIRE(uploader->copyBuffer(*staging, *outputBuf, size).has_value());

            // Upload input data.
            {
                auto mapped = staging->map();
                REQUIRE(mapped.has_value());
                std::memcpy(*mapped, inputHost.data(), size);
                (void)staging->flush(0, size);
                staging->unmap();
            }
            REQUIRE(uploader->copyBuffer(*staging, *inputBuf, size).has_value());

            const std::string shaderPath = std::string(STARFORGE_SHADER_DIR) + "/test_compute.spv";
            auto pipeline = ComputePipeline::createFromFile(*device, shaderPath, "main", 2);
            REQUIRE(pipeline.has_value());

            const Buffer* bindings[] = {&(*inputBuf), &(*outputBuf)};
            REQUIRE(pipeline->dispatchAndWait(bindings, count / kWorkgroupSize).has_value());

            REQUIRE(uploader->copyBuffer(*outputBuf, *readback, size).has_value());

            {
                auto mapped = readback->map();
                REQUIRE(mapped.has_value());
                (void)readback->invalidate(0, size);
                CHECK(std::memcmp(*mapped, expectedHost.data(), size) == 0);
                readback->unmap();
            }
        }

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
