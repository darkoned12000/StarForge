#pragma once

#include "content/content_generation.hpp"

#include <cstddef>
#include <cstdint>

namespace starforge::content
{

// Per-frame GPU ingestion budget: a request is accepted all-or-nothing
// only if it fits both the staging-bytes and draw-call caps.
struct UploadDecision
{
    bool accepted = false;
    std::size_t bytesUsed = 0;
    std::uint32_t drawCallsUsed = 0;
};

class UploadBudget
{
public:
    explicit UploadBudget(const GenerationBudget& budget = {}) noexcept : budget_(budget)
    {
    }

    [[nodiscard]] UploadDecision reserve(std::size_t bytes, std::uint32_t drawCalls) noexcept;
    void resetFrame() noexcept;

    [[nodiscard]] std::size_t bytesUsed() const noexcept { return bytesUsed_; }
    [[nodiscard]] std::uint32_t drawCallsUsed() const noexcept { return drawCallsUsed_; }
    [[nodiscard]] const GenerationBudget& budget() const noexcept { return budget_; }

private:
    GenerationBudget budget_;
    std::size_t bytesUsed_ = 0;
    std::uint32_t drawCallsUsed_ = 0;
};

} // namespace starforge::content
