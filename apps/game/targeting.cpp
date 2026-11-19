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

void TargetingState::toggleLock() noexcept
{
    if (hasSelection())
        clearSelection();
    else
        lockNearest();
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
