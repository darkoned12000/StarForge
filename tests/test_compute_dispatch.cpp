#include <doctest/doctest.h>

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/compute_pipeline.hpp"
#include "gpu_resources/transfer.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
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

TEST_CASE("Compute dispatch: consecutive dispatches on same pipeline")
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

        constexpr std::uint32_t count = 256;
        constexpr VkDeviceSize size = count * sizeof(std::uint32_t);

        auto bufA = Buffer::create(*allocator, {
            .size = size,
            .usage = BufferUsage::Storage,
            .memory = MemoryPlacement::CpuToGpu,
        });
        REQUIRE(bufA.has_value());

        auto bufB = Buffer::create(*allocator, {
            .size = size,
            .usage = BufferUsage::Storage,
            .memory = MemoryPlacement::GpuPreferred,
        });
        REQUIRE(bufB.has_value());

        auto readback = Buffer::create(*allocator, {
            .size = size,
            .usage = BufferUsage::TransferDst,
            .memory = MemoryPlacement::GpuToCpu,
        });
        REQUIRE(readback.has_value());

        const std::string shaderPath = std::string(STARFORGE_SHADER_DIR) + "/test_compute.spv";
        auto pipeline = ComputePipeline::createFromFile(*device, shaderPath, "main", 2);
        REQUIRE(pipeline.has_value());

        // First dispatch: A -> B.
        {
            auto mapped = bufA->map();
            REQUIRE(mapped.has_value());
            for (std::uint32_t i = 0; i < count; ++i)
                static_cast<std::uint32_t*>(*mapped)[i] = i;
            (void)bufA->flush(0, size);
            bufA->unmap();
        }

        const Buffer* bindings1[] = {&(*bufA), &(*bufB)};
        REQUIRE(pipeline->dispatchAndWait(bindings1, count / kWorkgroupSize).has_value());

        // Second dispatch: B -> A (consecutive on same pipeline).
        const Buffer* bindings2[] = {&(*bufB), &(*bufA)};
        REQUIRE(pipeline->dispatchAndWait(bindings2, count / kWorkgroupSize).has_value());

        auto uploader = Uploader::create(*device);
        REQUIRE(uploader.has_value());
        REQUIRE(uploader->copyBuffer(*bufA, *readback, size).has_value());

        auto mapped = readback->map();
        REQUIRE(mapped.has_value());
        (void)readback->invalidate(0, size);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t expected = cpuReferenceKernel(cpuReferenceKernel(i));
            CHECK(static_cast<std::uint32_t*>(*mapped)[i] == expected);
        }
        readback->unmap();
    }

    CHECK(context->validationErrorCount() == 0);
}

TEST_CASE("Compute dispatch: boundary validation")
{
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        // Create a minimal compute pipeline with a dummy shader.
        // We only test validation, not execution.
        // Use a simple compute shader if available.
        const std::string shaderPath = std::string(STARFORGE_SHADER_DIR) + "/test_compute.spv";
        std::ifstream file(shaderPath, std::ios::ate | std::ios::binary);
        if (!file)
        {
            // Skip if shader not available.
            return;
        }
        const auto size = static_cast<std::size_t>(file.tellg());
        std::vector<std::uint8_t> bytes(size);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));

        auto pipeline = ComputePipeline::create(*device, {
            .spirv = bytes,
            .entryPoint = "main",
            .storageBufferCount = 2,
        });
        REQUIRE(pipeline.has_value());

        // Test zero dispatch dimensions.
        std::vector<std::uint32_t> dummy(4, 0);
        auto allocator = Allocator::create({.instance = context->instance(), .physicalDevice = context->physicalDevice(), .device = device->device()});
        REQUIRE(allocator.has_value());
        auto buf = Buffer::create(*allocator, {
            .size = 4 * sizeof(std::uint32_t),
            .usage = BufferUsage::Storage,
            .memory = MemoryPlacement::GpuPreferred,
        });
        REQUIRE(buf.has_value());

        const Buffer* bindings[] = {&(*buf), &(*buf)};
        auto r1 = pipeline->dispatchAndWait(bindings, 0, 1, 1);
        CHECK_FALSE(r1.has_value());

        auto r2 = pipeline->dispatchAndWait(bindings, 1, 0, 1);
        CHECK_FALSE(r2.has_value());

        auto r3 = pipeline->dispatchAndWait(bindings, 1, 1, 0);
        CHECK_FALSE(r3.has_value());

        // Test mismatched buffer count.
        const Buffer* singleBinding[] = {&(*buf)};
        auto r4 = pipeline->dispatchAndWait(singleBinding, 1, 1, 1);
        CHECK_FALSE(r4.has_value());

        // Test null buffer pointer.
        const Buffer* nullBindings[] = {nullptr, &(*buf)};
        auto r5 = pipeline->dispatchAndWait(nullBindings, 1, 1, 1);
        CHECK_FALSE(r5.has_value());
    }

    CHECK(context->validationErrorCount() == 0);
}

TEST_CASE("Compute dispatch: multidimensional dispatch API")
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
            // Verify API accepts 2D and 3D dispatch dimensions.
            const std::string shaderPath = std::string(STARFORGE_SHADER_DIR) + "/test_compute.spv";
            std::ifstream file(shaderPath, std::ios::ate | std::ios::binary);
            if (!file)
            {
                return;
            }
            const auto size = static_cast<std::size_t>(file.tellg());
            std::vector<std::uint8_t> bytes(size);
            file.seekg(0);
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));

            auto pipeline = ComputePipeline::create(*device, {
                .spirv = bytes,
                .entryPoint = "main",
                .storageBufferCount = 2,
            });
            REQUIRE(pipeline.has_value());

            constexpr std::uint32_t count = 256;
            constexpr VkDeviceSize bufSize = count * sizeof(std::uint32_t);
            auto bufA = Buffer::create(*allocator, { .size = bufSize, .usage = BufferUsage::Storage, .memory = MemoryPlacement::CpuToGpu });
            REQUIRE(bufA.has_value());
            auto bufB = Buffer::create(*allocator, { .size = bufSize, .usage = BufferUsage::Storage, .memory = MemoryPlacement::GpuPreferred });
            REQUIRE(bufB.has_value());

            {
                auto mapped = bufA->map();
                REQUIRE(mapped.has_value());
                for (std::uint32_t i = 0; i < count; ++i)
                    static_cast<std::uint32_t*>(*mapped)[i] = i;
                (void)bufA->flush(0, bufSize);
                bufA->unmap();
            }

            {
                const Buffer* bindings[] = {&(*bufA), &(*bufB)};
                // API accepts 2D and 3D dimensions (dispatch covers first dimension)
                REQUIRE(pipeline->dispatchAndWait(bindings, count / kWorkgroupSize, 1, 1).has_value());
                REQUIRE(pipeline->dispatchAndWait(bindings, count / kWorkgroupSize, 2, 1).has_value());
                REQUIRE(pipeline->dispatchAndWait(bindings, count / kWorkgroupSize, 2, 3).has_value());
            }
        }

        CHECK(allocator->allocationCount() == 0);
    }

    CHECK(context->validationErrorCount() == 0);
}
