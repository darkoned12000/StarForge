#pragma once

// M16.5/M17.5: pure-CPU target selection and lock state. No rendering or
// backend types. Contacts carry everything the HUD needs for relation-
//-aware markers, the lead pip and the turn cue.
#include "gameplay/ui_relation.hpp"
#include "gameplay/ui_targeting.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace starforge::game
{

struct TargetContact
{
    std::string id;
    std::string name;
    gameplay::ui_relation::Relation relation =
        gameplay::ui_relation::Relation::Neutral;
    bool hostile = false;                 // kept for existing simulation/AI callers
    float distance = 0.0f;
    float shield = 0.0f;
    float hull = 0.0f;
    // Unit direction from the ship to the contact (camera-relative world
    // space). Used by "target nearest the reticle" and by the lead solver.
    gameplay::ui_targeting::Vec3 direction{};
    gameplay::ui_targeting::Vec3 position{};
    gameplay::ui_targeting::Vec3 velocity{};
};

class TargetingState
{
public:
    // Rebuild the contact list each frame; selection is preserved by id.
    // Nothing is selected automatically - the player must lock a target.
    void setContacts(std::vector<TargetContact> contacts);

    // Tab: cycle through all contacts.
    void cycleNext() noexcept;
    void cyclePrevious() noexcept;
    // R: nearest contact by distance, hostiles first.
    void lockNearest() noexcept;
    // T: hostile nearest the reticle (smallest angle, tie-break distance).
    // This is the combat default because it matches where the player is
    // actually pointing.
    void lockNearestHostileToReticle() noexcept;
    // T toggles the reticle lock on/off.
    void toggleLock() noexcept;
    void toggleReticleLock() noexcept;
    // Clears the current selection but keeps the contact list.
    void clearSelection() noexcept;

    [[nodiscard]] const TargetContact* selected() const noexcept;
    [[nodiscard]] bool hasSelection() const noexcept { return selected() != nullptr; }
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    [[nodiscard]] std::size_t count() const noexcept { return contacts_.size(); }
    // Ship-local contact view for the passive HUD cue, so it follows the
    // nose just like T rather than using stale world-space axes.
    [[nodiscard]] const std::vector<TargetContact>& contacts() const noexcept
    {
        return contacts_;
    }

    // Lock builds only while the trigger is held on a hostile in range.
    // M17.5: the brackets converge over 0.25 s, not the old 1.5 s.
    void tick(float dt, bool wantLock) noexcept;
    [[nodiscard]] float lockProgress() const noexcept { return lockProgress_; }
    [[nodiscard]] bool locked() const noexcept { return lockProgress_ >= 1.0f; }

    static constexpr std::size_t kNoSelection = static_cast<std::size_t>(-1);
    static constexpr float kLockSeconds = 0.25f;
    static constexpr float kLockRange = 600.0f;

private:
    std::vector<TargetContact> contacts_;
    std::size_t index_ = kNoSelection;
    float lockProgress_ = 0.0f;
};

} // namespace starforge::game
