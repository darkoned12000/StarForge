#pragma once

// M15 gameplay simulation (15.1-15.5 + 15.0.5 combat slice).
// Pure CPU, no ECS/Vulkan/SDL, so it can be driven headlessly by tests and
// by the game loop. Uses the internal gameplay library (game layer) and
// plain-data transforms from the raw game bridge.

#include "game_bridge.hpp"

#include "gameplay/authored.hpp"
#include "gameplay/damage.hpp"
#include "gameplay/docking.hpp"
#include "gameplay/economy.hpp"
#include "gameplay/jumping.hpp"
#include "gameplay/missions.hpp"
#include "gameplay/progression.hpp"

#include <string>
#include <vector>

namespace starforge::game
{

struct GameEvent
{
    enum class Type
    {
        None,
        HostileSpawned,
        WeaponFired,
        PlayerShieldHit,
        PlayerHullHit,
        HostileHit,
        HostileDestroyed,
        Docked,
        Undocked,
        Traded,
        MissionAccepted,
        MissionCompleted,
        Jumped,
    };
    Type type = Type::None;
    float amount = 0.0f;
    std::string text;
    // Id of the NPC that produced a HostileHit / HostileDestroyed event.
    std::string sourceId;
};

// Plain-data view of an NPC. `hostile` ships attack the player; neutral ships
// simply travel through the sector and are useful for exercising targeting.
struct NpcInfo
{
    std::string id;
    RawVec3f position{0.0f, 0.0f, 0.0f}; // ship-relative metres
    bool alive = false;
    bool hostile = true;
    bool engaging = false;
    float shield = 0.0f;
    float hull = 0.0f;
};

// Kept as an alias so existing call sites/tests that speak "hostile" keep
// compiling while the simulation supports a mix of hostile and neutral NPCs.
using HostileInfo = NpcInfo;

// Owns all M15 gameplay state. The game loop feeds flight state and fires
// input edges; headless tests drive the exact same API.
class GameSimulation
{
public:
    GameSimulation();

    // --- NPCs (M16 polish) ---
    // Spawns a set of `count` ships travelling through the sector. A mix of
    // hostile (attack the player) and neutral (travel only) ships is created
    // so targeting can be exercised against moving, non-attacking contacts.
    void spawnNpcs(int count = 5);
    [[nodiscard]] const std::vector<NpcInfo>& npcs() const noexcept { return npcInfo_; }

    // Legacy single-hostile API (used by the original combat tests). Calling
    // spawnHostile() replaces the NPC set with a single aggressive ship.
    void spawnHostile();
    [[nodiscard]] bool hostileAlive() const noexcept;
    [[nodiscard]] HostileInfo hostile() const noexcept;

    // Which NPC the player's weapons are aimed at (empty = no explicit lock;
    // weapons then fall back to the nearest hostile in the forward cone).
    void setTargetId(std::string id) noexcept { targetId_ = std::move(id); }
    [[nodiscard]] const std::string& targetId() const noexcept { return targetId_; }

    // One fixed simulation step. `shipForward` is in the ship-relative frame
    // (ship at origin). Returns events produced this step.
    std::vector<GameEvent> step(float dt, const RawVec3f& shipForward,
                                bool firePrimary, bool fireSecondary);

    // --- M15.1 damage / shields ---
    [[nodiscard]] float playerShield() const noexcept { return player_.shield; }
    [[nodiscard]] float playerHull() const noexcept { return player_.hull; }
    [[nodiscard]] bool playerWrecked() const noexcept { return player_.wreck; }
    void repairPlayer() noexcept;

    // --- M15.2 cargo / stations / docking ---
    void updateDocking(double distance, double speed, const std::string& nearbyStation);
    [[nodiscard]] bool dock(const std::string& station, const std::string& slot);
    void undock();
    [[nodiscard]] bool isDocked() const noexcept;
    [[nodiscard]] int credits() const noexcept { return economy_.credits; }
    [[nodiscard]] int cargoUsed() const noexcept { return economy_.cargo.used(); }
    [[nodiscard]] int cargoCount(const std::string& commodity) const;
    [[nodiscard]] int stock(const std::string& station, const std::string& commodity) const;
    void configureMarket(const std::string& station, const std::string& commodity,
                         int price, int initialStock);

    [[nodiscard]] bool buy(const std::string& station, const std::string& commodity, int amount);
    [[nodiscard]] bool sell(const std::string& station, const std::string& commodity, int amount);
    [[nodiscard]] std::string lastTradeError() const noexcept { return lastTradeError_; }

    // --- M15.3 missions / progression ---
    void defineMission(gameplay::MissionDef def);
    [[nodiscard]] bool acceptMission(const std::string& id);
    [[nodiscard]] bool deliverMission(const std::string& id, const std::string& commodity,
                                      int amount, const std::string& station);
    [[nodiscard]] bool completeMission(const std::string& id, const std::string& station);
    [[nodiscard]] int completedMissions() const noexcept;
    [[nodiscard]] float jumpRangeLy() const noexcept { return progression_.jumpRangeLy(); }

    // --- M15.4 authored overrides + jumping ---
    void addAuthored(gameplay::ContentPlacement placement);
    void setPlayerMod(const std::string& targetId, gameplay::PlayerModType type);
    [[nodiscard]] std::size_t resolveContent(const gameplay::CellKey& cell,
                                             std::vector<gameplay::ContentPlacement> generated) const;
    [[nodiscard]] bool jump(const std::string& targetSystem, float distanceLy);
    [[nodiscard]] std::string currentSystem() const noexcept { return jump_.currentSystem(); }

private:
    struct NpcState
    {
        std::string id;
        RawVec3f position{};
        RawVec3f velocity{};
        bool alive = false;
        bool hostile = true;
        bool engaging = true;
        float cooldown = 0.0f;
        float phase = 0.0f;      // drives orbit/strafe/travel manoeuvres
        float evadeTimer = 0.0f; // set when hit -> break off and evade
        gameplay::DamageState damage;
    };

    // Refresh the plain-data view after mutating the internal NPC states.
    void refreshNpcInfo() noexcept;

    gameplay::DamageState player_;
    std::vector<NpcState> npcs_;
    std::vector<NpcInfo> npcInfo_;
    std::string targetId_;
    float primaryCooldown_ = 0.0f;
    float secondaryCooldown_ = 0.0f;
    gameplay::EconomyState economy_;
    gameplay::MissionBoard missions_;
    gameplay::Progression progression_;
    gameplay::JumpDrive jump_;
    gameplay::DockingController docking_;
    gameplay::LayeredGeneration layered_;
    std::string lastTradeError_;
};

} // namespace starforge::game
