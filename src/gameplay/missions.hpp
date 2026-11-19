#pragma once

#include <expected>
#include <map>
#include <string>
#include <vector>

namespace starforge::gameplay
{

enum class MissionStatus
{
    Offered,
    Active,
    Completed,
    Failed,
};

struct MissionDef
{
    std::string id;
    std::string title;
    std::string description;
    int reward = 0;
    std::string unlock;             // empty = no unlock, else a progression key
    std::string objectiveCommodity; // cargo that must be delivered
    int objectiveAmount = 0;
    std::string dockStation; // station where delivery/completion happens
};

struct MissionState
{
    std::string id;
    MissionStatus status = MissionStatus::Offered;
    int delivered = 0;
};

struct MissionReward
{
    int credits = 0;
    std::string unlock;
};

// A board of mission definitions and per-player mission state. Fully CPU-side;
// the game layer maps accept/complete results to economy credits and unlocks.
class MissionBoard
{
public:
    void define(MissionDef def);
    [[nodiscard]] bool isDefined(const std::string& id) const noexcept;

    [[nodiscard]] std::expected<void, std::string> offer(const std::string& id);
    [[nodiscard]] std::expected<void, std::string> accept(const std::string& id);
    [[nodiscard]] std::expected<void, std::string> fail(const std::string& id);

    // Deliver cargo toward the objective. Returns true when the objective is
    // fully satisfied, false when more is still required.
    [[nodiscard]] std::expected<bool, std::string>
    deliver(const std::string& id, const std::string& commodity, int amount,
            const std::string& station);

    // Completes a satisfied mission and yields its reward + unlock.
    [[nodiscard]] std::expected<MissionReward, std::string>
    complete(const std::string& id, const std::string& station);

    [[nodiscard]] const MissionState* state(const std::string& id) const noexcept;
    [[nodiscard]] const MissionDef* definition(const std::string& id) const noexcept;
    [[nodiscard]] std::vector<std::string> offered() const;
    [[nodiscard]] std::vector<std::string> active() const;
    [[nodiscard]] std::vector<std::string> completed() const;

private:
    std::map<std::string, MissionDef> defs_;
    std::map<std::string, MissionState> states_;
};

} // namespace starforge::gameplay
