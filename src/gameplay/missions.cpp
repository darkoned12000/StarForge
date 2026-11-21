#include "gameplay/missions.hpp"

#include <algorithm>

namespace starforge::gameplay
{

void MissionBoard::define(MissionDef def)
{
    const std::string id = def.id;
    defs_[id] = std::move(def);
    if (!states_.contains(id))
    {
        MissionState state;
        state.id = id;
        state.status = MissionStatus::Offered;
        states_[id] = std::move(state);
    }
}

bool MissionBoard::isDefined(const std::string& id) const noexcept
{
    return defs_.contains(id);
}

std::expected<void, std::string> MissionBoard::offer(const std::string& id)
{
    auto it = states_.find(id);
    if (it == states_.end()) return std::unexpected("unknown mission: " + id);
    if (it->second.status == MissionStatus::Active)
    {
        return std::unexpected("cannot re-offer active mission: " + id);
    }
    it->second.status = MissionStatus::Offered;
    it->second.delivered = 0;
    return {};
}

std::expected<void, std::string> MissionBoard::accept(const std::string& id)
{
    auto it = states_.find(id);
    if (it == states_.end()) return std::unexpected("unknown mission: " + id);
    if (it->second.status != MissionStatus::Offered)
        return std::unexpected("mission is not offered: " + id);
    it->second.status = MissionStatus::Active;
    it->second.delivered = 0;
    return {};
}

std::expected<void, std::string> MissionBoard::fail(const std::string& id)
{
    auto it = states_.find(id);
    if (it == states_.end()) return std::unexpected("unknown mission: " + id);
    if (it->second.status != MissionStatus::Active)
        return std::unexpected("mission is not active: " + id);
    it->second.status = MissionStatus::Failed;
    return {};
}

std::expected<bool, std::string>
MissionBoard::deliver(const std::string& id, const std::string& commodity, int amount,
                      const std::string& station)
{
    auto it = states_.find(id);
    if (it == states_.end()) return std::unexpected("unknown mission: " + id);
    if (it->second.status != MissionStatus::Active)
        return std::unexpected("mission is not active: " + id);
    if (amount <= 0) return std::unexpected("delivery amount must be positive");

    const auto defIt = defs_.find(id);
    if (defIt == defs_.end()) return std::unexpected("mission has no definition: " + id);
    const auto& def = defIt->second;
    if (commodity != def.objectiveCommodity)
        return std::unexpected("wrong commodity for mission: " + commodity);
    if (station != def.dockStation)
        return std::unexpected("wrong delivery station: " + station);

    const int remaining = std::max(0, def.objectiveAmount - it->second.delivered);
    it->second.delivered += std::min(amount, remaining);
    return it->second.delivered >= def.objectiveAmount;
}

std::expected<MissionReward, std::string>
MissionBoard::complete(const std::string& id, const std::string& station)
{
    auto it = states_.find(id);
    if (it == states_.end()) return std::unexpected("unknown mission: " + id);
    if (it->second.status != MissionStatus::Active)
        return std::unexpected("mission is not active: " + id);
    const auto defIt = defs_.find(id);
    if (defIt == defs_.end()) return std::unexpected("mission has no definition: " + id);
    const auto& def = defIt->second;
    if (station != def.dockStation) return std::unexpected("complete at the wrong station: " + station);
    if (it->second.delivered < def.objectiveAmount) return std::unexpected("mission objective not met");

    it->second.status = MissionStatus::Completed;
    return MissionReward{def.reward, def.unlock};
}

const MissionState* MissionBoard::state(const std::string& id) const noexcept
{
    auto it = states_.find(id);
    return it != states_.end() ? &it->second : nullptr;
}

const MissionDef* MissionBoard::definition(const std::string& id) const noexcept
{
    auto it = defs_.find(id);
    return it != defs_.end() ? &it->second : nullptr;
}

std::vector<std::string> MissionBoard::offered() const
{
    std::vector<std::string> out;
    for (const auto& [id, state] : states_)
        if (state.status == MissionStatus::Offered) out.push_back(id);
    return out;
}

std::vector<std::string> MissionBoard::active() const
{
    std::vector<std::string> out;
    for (const auto& [id, state] : states_)
        if (state.status == MissionStatus::Active) out.push_back(id);
    return out;
}

std::vector<std::string> MissionBoard::completed() const
{
    std::vector<std::string> out;
    for (const auto& [id, state] : states_)
        if (state.status == MissionStatus::Completed) out.push_back(id);
    return out;
}

} // namespace starforge::gameplay
