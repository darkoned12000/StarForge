#include "targeting.hpp"

#include <algorithm>

namespace starforge::game
{

void TargetingState::setContacts(std::vector<TargetContact> contacts)
{
    const bool hadSelection = index_ < contacts_.size();
    const std::string selectedId = hadSelection ? contacts_[index_].id : std::string{};
    contacts_ = std::move(contacts);
    index_ = kNoSelection;
    if (!selectedId.empty())
    {
        for (std::size_t i = 0; i < contacts_.size(); ++i)
            if (contacts_[i].id == selectedId)
            {
                index_ = i;
                return;
            }
        // The previously selected contact disappeared - clear the lock.
        lockProgress_ = 0.0f;
    }
}

void TargetingState::cycleNext() noexcept
{
    if (contacts_.empty()) return;
    index_ = index_ == kNoSelection ? 0 : (index_ + 1) % contacts_.size();
    lockProgress_ = 0.0f;
}

void TargetingState::cyclePrevious() noexcept
{
    if (contacts_.empty()) return;
    index_ = index_ == kNoSelection ? contacts_.size() - 1
                                    : (index_ + contacts_.size() - 1) % contacts_.size();
    lockProgress_ = 0.0f;
}

void TargetingState::lockNearest() noexcept
{
    if (contacts_.empty()) return;
    std::size_t best = 0;
    bool bestHostile = contacts_[0].hostile;
    float bestDistance = contacts_[0].distance;
    for (std::size_t i = 1; i < contacts_.size(); ++i)
    {
        const auto& c = contacts_[i];
        const bool better = (c.hostile && !bestHostile) ||
                            (c.hostile == bestHostile && c.distance < bestDistance);
        if (better)
        {
            best = i;
            bestHostile = c.hostile;
            bestDistance = c.distance;
        }
    }
    index_ = best;
    lockProgress_ = 0.0f;
}

void TargetingState::lockNearestHostileToReticle() noexcept
{
    // "Nearest the reticle" is the combat default: hostiles are ranked by
    // angular offset from ship-forward, with distance only as a tie-break.
    // A contact behind the ship is never selected by this path.
    float bestScore = 0.0f;
    std::size_t best = kNoSelection;
    for (std::size_t i = 0; i < contacts_.size(); ++i)
    {
        const auto& c = contacts_[i];
        if (!c.hostile || c.direction.z <= 0.0f) continue;
        // direction is SHIP-LOCAL: +Z is ahead. Nearer targets win ties.
        const float alignment = c.direction.z; // 1 = dead ahead
        const float score = alignment + 1.0f / (1000.0f + c.distance);
        if (best == kNoSelection || score > bestScore)
        {
            best = i;
            bestScore = score;
        }
    }
    if (best == kNoSelection)
    {
        // No hostile ahead: fall back to the nearest hostile behind us.
        for (std::size_t i = 0; i < contacts_.size(); ++i)
            if (contacts_[i].hostile &&
                (best == kNoSelection || contacts_[i].distance < contacts_[best].distance))
                best = i;
    }
    if (best == kNoSelection)
    {
        // Nothing hostile at all: still let the player lock something (a
        // station or a trader) so T is never a dead key.
        for (std::size_t i = 0; i < contacts_.size(); ++i)
            if (contacts_[i].direction.z > 0.0f &&
                (best == kNoSelection ||
                 contacts_[i].direction.z > contacts_[best].direction.z))
                best = i;
    }
    if (best == kNoSelection && !contacts_.empty()) best = 0;
    if (best == kNoSelection) return;
    index_ = best;
    lockProgress_ = 0.0f;
}

void TargetingState::toggleLock() noexcept
{
    if (hasSelection())
        clearSelection();
    else
        lockNearest();
}

void TargetingState::toggleReticleLock() noexcept
{
    if (hasSelection())
        clearSelection();
    else
        lockNearestHostileToReticle();
}

void TargetingState::clearSelection() noexcept
{
    index_ = kNoSelection;
    lockProgress_ = 0.0f;
}

const TargetContact* TargetingState::selected() const noexcept
{
    return index_ < contacts_.size() ? &contacts_[index_] : nullptr;
}

void TargetingState::tick(float dt, bool wantLock) noexcept
{
    const auto* target = selected();
    const bool eligible = target && target->hostile && target->distance <= kLockRange;
    if (!wantLock || !eligible)
    {
        lockProgress_ = 0.0f;
        return;
    }
    if (dt > 0.0f)
        lockProgress_ = std::min(1.0f, lockProgress_ + dt / kLockSeconds);
}

} // namespace starforge::game
