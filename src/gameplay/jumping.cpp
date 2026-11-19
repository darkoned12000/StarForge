#include "gameplay/jumping.hpp"

namespace starforge::gameplay
{

bool JumpDrive::canJump(float distanceLy) const noexcept
{
    return distanceLy >= 0.0f && distanceLy <= rangeLy_;
}

std::expected<JumpResult, std::string>
JumpDrive::jump(const std::string& targetSystem, float distanceLy)
{
    if (targetSystem.empty()) return std::unexpected("jump target cannot be empty");
    if (!canJump(distanceLy)) return std::unexpected("destination is beyond jump range");

    JumpResult result;
    result.fromSystem = currentSystem_;
    result.toSystem = targetSystem;
    result.distanceLy = distanceLy;
    currentSystem_ = targetSystem;
    return result;
}

} // namespace starforge::gameplay
