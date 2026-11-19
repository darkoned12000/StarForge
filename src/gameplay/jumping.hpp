#pragma once

#include <expected>
#include <string>

namespace starforge::gameplay
{

struct JumpResult
{
    std::string fromSystem;
    std::string toSystem;
    float distanceLy = 0.0f;
};

// Jump drive state. Range is gated by progression (15.3 unlock gate). A jump
// does not itself regenerate the world - it marks the destination system; the
// game layer re-centres the streaming traversal on the new system origin.
class JumpDrive
{
public:
    void setSystem(std::string system) noexcept { currentSystem_ = std::move(system); }
    [[nodiscard]] const std::string& currentSystem() const noexcept { return currentSystem_; }

    void setRangeLy(float range) noexcept { rangeLy_ = range; }
    [[nodiscard]] float rangeLy() const noexcept { return rangeLy_; }

    [[nodiscard]] bool canJump(float distanceLy) const noexcept;
    [[nodiscard]] std::expected<JumpResult, std::string>
    jump(const std::string& targetSystem, float distanceLy);

private:
    std::string currentSystem_ = "sol";
    float rangeLy_ = 1.0f;
};

} // namespace starforge::gameplay
