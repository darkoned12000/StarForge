#include "content/upload_budget.hpp"

namespace starforge::content
{

UploadDecision UploadBudget::reserve(std::size_t bytes, std::uint32_t drawCalls) noexcept
{
    UploadDecision decision;
    const std::size_t remainingBytes = budget_.maxUploadBytesPerFrame >= bytesUsed_
                                           ? budget_.maxUploadBytesPerFrame - bytesUsed_
                                           : 0;
    const std::size_t remainingDraws = budget_.maxUploadDrawCallsPerFrame >= drawCallsUsed_
                                           ? budget_.maxUploadDrawCallsPerFrame - drawCallsUsed_
                                           : 0;
    if (bytes <= remainingBytes && drawCalls <= remainingDraws)
    {
        bytesUsed_ += bytes;
        drawCallsUsed_ += drawCalls;
        decision.accepted = true;
    }
    decision.bytesUsed = bytesUsed_;
    decision.drawCallsUsed = drawCallsUsed_;
    return decision;
}

void UploadBudget::resetFrame() noexcept
{
    bytesUsed_ = 0;
    drawCallsUsed_ = 0;
}

} // namespace starforge::content
