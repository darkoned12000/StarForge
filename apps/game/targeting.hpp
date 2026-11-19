#pragma once

// M16.5: pure-CPU target selection and lock state. No rendering/backend types.
#include <cstddef>
#include <string>
#include <vector>

namespace starforge::game
{

struct TargetContact
{
    std::string id;
    bool hostile = false;
    float distance = 0.0f;
    float shield = 0.0f;
    float hull = 0.0f;
};

class TargetingState
{
public:
    // Rebuild the contact list each frame; selection is preserved by id.
    void setContacts(std::vector<TargetContact> contacts);

    void cycleNext() noexcept;
    void cyclePrevious() noexcept;
    void clearSelection() noexcept;

    [[nodiscard]] const TargetContact* selected() const noexcept;
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    [[nodiscard]] std::size_t count() const noexcept { return contacts_.size(); }

    // Lock builds only while the trigger is held on a hostile in range.
    void tick(float dt, bool wantLock) noexcept;
    [[nodiscard]] float lockProgress() const noexcept { return lockProgress_; }
    [[nodiscard]] bool locked() const noexcept { return lockProgress_ >= 1.0f; }

    static constexpr float kLockSeconds = 1.5f;
    static constexpr float kLockRange = 600.0f;

private:
    std::vector<TargetContact> contacts_;
    std::size_t index_ = 0;
    float lockProgress_ = 0.0f;
};

} // namespace starforge::game
