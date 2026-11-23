#include "game_simulation.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace starforge::game
{

namespace
{

float length(const RawVec3f& v) noexcept
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

RawVec3f normalized(RawVec3f v) noexcept
{
    const float len = length(v);
    if (len <= 1e-6f) return RawVec3f{};
    return RawVec3f{v.x / len, v.y / len, v.z / len};
}

float dot(const RawVec3f& a, const RawVec3f& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr float kEngageRadius = 140.0f;
constexpr float kDisengageRadius = 70.0f;
constexpr float kNeutralTravelRadius = 2200.0f;

} // namespace

GameSimulation::GameSimulation()
{
    // Default trade station: a small outpost at the planet.
    configureMarket("station-a", "ore", 20, 40);
    configureMarket("station-a", "water", 12, 30);
    configureMarket("station-b", "ore", 24, 0);

    // Default contract: deliver ore to station-b; unlocks a longer jump.
    gameplay::MissionDef haul;
    haul.id = "haul-ore";
    haul.title = "Ore Haul";
    haul.description = "Deliver 10 ore to station-b";
    haul.reward = 500;
    haul.unlock = "jump2";
    haul.objectiveCommodity = "ore";
    haul.objectiveAmount = 10;
    haul.dockStation = "station-b";
    defineMission(std::move(haul));
}

void GameSimulation::refreshNpcInfo() noexcept
{
    npcInfo_.clear();
    npcInfo_.reserve(npcs_.size());
    for (const auto& npc : npcs_)
    {
        npcInfo_.push_back(NpcInfo{.id = npc.id,
                                   .position = npc.position,
                                   .alive = npc.alive,
                                   .hostile = npc.hostile,
                                   .engaging = npc.engaging,
                                   .shield = npc.damage.shield,
                                   .hull = npc.damage.hull});
    }
}

void GameSimulation::spawnHostile()
{
    npcs_.clear();
    NpcState hostile;
    hostile.id = "hostile-1";
    hostile.name = "Pirate Raider";
    hostile.relation = gameplay::ui_relation::Relation::Hostile;
    hostile.alive = true;
    hostile.hostile = true;
    hostile.engaging = true;
    hostile.cooldown = 1.0f;
    hostile.phase = 0.0f;
    hostile.position = RawVec3f{0.0f, 30.0f, -260.0f}; // ahead of the ship
    hostile.damage = gameplay::DamageState{};
    hostile.damage.shieldMax = hostile.damage.shield = 60.0f;
    hostile.damage.hullMax = hostile.damage.hull = 80.0f;
    npcs_.push_back(std::move(hostile));
    targetId_.clear();
    refreshNpcInfo();
}

void GameSimulation::spawnNpcs(int count)
{
    struct SpawnDef
    {
        const char* id;
        const char* name;
        gameplay::ui_relation::Relation relation;
        bool hostile;
        RawVec3f position;
    };
    // A mix of hostile, allied and neutral ships spread across the sector.
    // Only hostiles attack; the others exercise relation-aware targeting.
    // `Relation` drives HUD colour AND shape everywhere so friend/foe is
    // readable without relying on colour alone.
    static const SpawnDef defs[] = {
        {"hostile-1", "Pirate Raider", gameplay::ui_relation::Relation::Hostile, true,
         {0.0f, 30.0f, -260.0f}},
        {"trader-1", "Free Trader", gameplay::ui_relation::Relation::Neutral, false,
         {-180.0f, 30.0f, -420.0f}},
        {"patrol-1", "Patrol Interceptor", gameplay::ui_relation::Relation::Hostile, true,
         {280.0f, -40.0f, -700.0f}},
        {"trader-2", "Ore Hauler", gameplay::ui_relation::Relation::Neutral, false,
         {-120.0f, 90.0f, -980.0f}},
        {"scout-1", "Wingman", gameplay::ui_relation::Relation::Ally, false,
         {420.0f, 20.0f, -1250.0f}},
        {"hauler-1", "Convoy Hauler", gameplay::ui_relation::Relation::Neutral, false,
         {-560.0f, -30.0f, -1500.0f}},
    };

    npcs_.clear();
    const int n = std::clamp(count, 0, static_cast<int>(std::size(defs)));
    for (int i = 0; i < n; ++i)
    {
        const auto& def = defs[i];
        NpcState npc;
        npc.id = def.id;
        npc.name = def.name;
        npc.relation = def.relation;
        npc.hostile = def.hostile;
        npc.alive = true;
        npc.engaging = def.hostile;
        npc.cooldown = def.hostile ? 1.0f + static_cast<float>(i) * 0.4f : 0.0f;
        npc.phase = static_cast<float>(i) * 1.7f;
        npc.position = def.position;
        npc.damage = gameplay::DamageState{};
        npc.damage.shieldMax = npc.damage.shield = def.hostile ? 60.0f : 40.0f;
        npc.damage.hullMax = npc.damage.hull = def.hostile ? 80.0f : 60.0f;
        // Neutral ships cruise along a gentle heading, wrapping back toward
        // the sector centre once they drift too far away.
        npc.velocity = normalized(RawVec3f{def.position.x * 0.1f, 0.0f, -1.0f});
        npcs_.push_back(std::move(npc));
    }
    targetId_.clear();
    refreshNpcInfo();
}

bool GameSimulation::hostileAlive() const noexcept
{
    for (const auto& info : npcInfo_)
        if (info.alive && info.hostile) return true;
    return false;
}

HostileInfo GameSimulation::hostile() const noexcept
{
    for (const auto& info : npcInfo_)
        if (info.alive && info.hostile) return info;
    for (const auto& info : npcInfo_)
        if (info.alive) return info;
    return HostileInfo{};
}

std::vector<GameEvent> GameSimulation::step(float dt, const RawVec3f& shipForward,
                                            bool firePrimary, bool fireSecondary,
                                            const RawVec3f& aimOrigin,
                                            const RawVec3f& aimDirection)
{
    std::vector<GameEvent> events;
    if (dt <= 0.0f) return events;

    primaryCooldown_ = std::max(0.0f, primaryCooldown_ - dt);
    secondaryCooldown_ = std::max(0.0f, secondaryCooldown_ - dt);
    gameplay::tickDamage(player_, dt, 2.0f);

    // --- Advance every NPC ---
    for (std::size_t npcIndex = 0; npcIndex < npcs_.size(); ++npcIndex)
    {
        auto& npc = npcs_[npcIndex];
        if (!npc.alive) continue;

        npc.cooldown = std::max(0.0f, npc.cooldown - dt);
        npc.evadeTimer = std::max(0.0f, npc.evadeTimer - dt);
        gameplay::tickDamage(npc.damage, dt, 3.0f);

        const float distance = length(npc.position);
        npc.phase += dt;

        if (npc.hostile)
        {
            // Engage / orbit / evade state machine (M14 hostile behaviour).
            if (npc.evadeTimer > 0.0f) npc.engaging = false; // break off when hit
            else if (distance > kEngageRadius) npc.engaging = true;
            else if (distance < kDisengageRadius) npc.engaging = false;

            const RawVec3f toward = normalized(RawVec3f{-npc.position.x, -npc.position.y,
                                                        -npc.position.z});
            RawVec3f right{toward.z, 0.0f, -toward.x};
            // Directly above/below the player: cross with the vertical gives
            // a zero vector, which killed lateral motion and NaN'd heading.
            if (right.x * right.x + right.z * right.z < 1.0e-6f)
                right = RawVec3f{1.0f, 0.0f, 0.0f};
            right = normalized(right);
            // Slower, wider orbits so a pilot can actually line up a shot.
            const float speed = npc.engaging ? 24.0f : 32.0f;
            const float strafe = std::sin(npc.phase * 0.8f) * 12.0f;
            const float climb = std::sin(npc.phase * 0.53f + 1.2f) * 7.0f;
            const float radial = npc.engaging ? speed : -speed;
            npc.velocity = RawVec3f{
                toward.x * radial + right.x * strafe,
                toward.y * radial + climb,
                toward.z * radial + right.z * strafe};

            // Keep hostiles in the player's forward arc: steer gently toward
            // a standoff point ahead of the ship instead of drifting behind
            // where they cannot be seen or targeted.
            const float sideOffset = npcIndex % 2 == 0 ? 45.0f : -45.0f;
            const RawVec3f desired{
                shipForward.x * 190.0f + right.x * sideOffset,
                shipForward.y * 190.0f + climb * 0.5f,
                shipForward.z * 190.0f + right.z * sideOffset};
            const RawVec3f toDesired = normalized(RawVec3f{
                desired.x - npc.position.x,
                desired.y - npc.position.y,
                desired.z - npc.position.z});
            npc.velocity = RawVec3f{
                npc.velocity.x + toDesired.x * 14.0f,
                npc.velocity.y + toDesired.y * 14.0f,
                npc.velocity.z + toDesired.z * 14.0f};
        }
        else
        {
            // Neutral ships travel through the sector; they never fire.
            npc.engaging = false;
            if (distance > kNeutralTravelRadius)
                npc.velocity = normalized(RawVec3f{-npc.position.x, -npc.position.y,
                                                   -npc.position.z});
            const RawVec3f heading = normalized(npc.velocity);
            const float speed = 30.0f;
            const float bob = std::sin(npc.phase * 0.7f) * 5.0f;
            npc.velocity = RawVec3f{heading.x * speed, heading.y * speed + bob,
                                    heading.z * speed};
        }

        npc.position = RawVec3f{npc.position.x + npc.velocity.x * dt,
                                npc.position.y + npc.velocity.y * dt,
                                npc.position.z + npc.velocity.z * dt};

        // Only hostile ships engage the player with weapons.
        if (npc.hostile && distance < 400.0f && npc.cooldown <= 0.0f)
        {
            npc.cooldown = 1.5f;
            auto result = gameplay::applyDamage(player_, 8.0f, gameplay::Subsystem::Hull);
            if (result.destroyed)
                events.push_back(GameEvent{GameEvent::Type::PlayerDestroyed, 0.0f, "player",
                                           npc.id});
            if (result.shieldAbsorbed > 0.0f)
                events.push_back(GameEvent{GameEvent::Type::PlayerShieldHit,
                                           result.shieldAbsorbed, "shield", npc.id});
            if (result.hullDamage > 0.0f)
                events.push_back(GameEvent{GameEvent::Type::PlayerHullHit,
                                           result.hullDamage, "hull", npc.id});
        }
    }

    // --- Player weapons -------------------------------------------------
    // A shot ALWAYS registers (audio, tracers, recoil) while the trigger is
    // held, even with no target or with the locked ship out of range. Damage
    // is a separate test: the victim must be hostile, alive, inside weapon
    // range AND physically under the reticle. A lock only prioritises which
    // qualified ship is chosen - it never grants a hit by itself, so firing
    // away from a locked target cannot damage it.
    constexpr float kWeaponRange = 600.0f;
    constexpr float kAimCos = 0.996f; // ~5 degrees of reticle tolerance
    const auto aim = normalized(aimDirection);
    const auto toNpc = [&](const NpcState& npc) -> RawVec3f {
        return RawVec3f{npc.position.x - aimOrigin.x,
                        npc.position.y - aimOrigin.y,
                        npc.position.z - aimOrigin.z};
    };
    const auto qualifies = [&](const NpcState& npc) -> bool {
        if (!npc.alive || !npc.hostile) return false; // friendlies never take fire
        const auto offset = toNpc(npc);
        const float d = length(offset);
        return d <= kWeaponRange && dot(aim, normalized(offset)) > kAimCos;
    };

    std::size_t target = npcs_.size();
    if (!targetId_.empty())
    {
        // A lock narrows the candidate set to that ship; it still has to be
        // hostile, in range and actually under the crosshair.
        for (std::size_t i = 0; i < npcs_.size(); ++i)
            if (npcs_[i].id == targetId_ && qualifies(npcs_[i]))
            {
                target = i;
                break;
            }
    }
    else
    {
        float bestDistance = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < npcs_.size(); ++i)
        {
            const auto& npc = npcs_[i];
            if (!qualifies(npc)) continue;
            const float d = length(toNpc(npc));
            if (d < bestDistance)
            {
                bestDistance = d;
                target = i;
            }
        }
    }

    auto fire = [&](bool held, float& cooldown, float interval, float damage, const char* label) {
        if (!held || cooldown > 0.0f) return;
        cooldown = interval;
        const char* victimId = target < npcs_.size() ? npcs_[target].id.c_str() : "";
        events.push_back(GameEvent{GameEvent::Type::WeaponFired, damage, label, victimId});
        if (target >= npcs_.size()) return; // shot went wide / out of range
        auto& npc = npcs_[target];
        auto result = gameplay::applyDamage(npc.damage, damage, gameplay::Subsystem::Hull);
        const float dealt = result.shieldAbsorbed + result.hullDamage;
        if (dealt > 0.0f) npc.evadeTimer = 2.0f; // reactive evasion
        events.push_back(GameEvent{GameEvent::Type::HostileHit, dealt, "npc", npc.id});
        if (result.destroyed)
        {
            npc.alive = false;
            events.push_back(GameEvent{GameEvent::Type::HostileDestroyed,
                                       0.0f, "npc", npc.id});
        }
    };
    fire(firePrimary, primaryCooldown_, 0.15f, 8.0f, "primary");
    fire(fireSecondary, secondaryCooldown_, 1.0f, 30.0f, "secondary");

    refreshNpcInfo();
    return events;
}

void GameSimulation::repairPlayer() noexcept
{
    player_ = gameplay::DamageState{};
}

void GameSimulation::updateDocking(double distance, double speed, const std::string& nearbyStation)
{
    docking_.update(distance, speed, nearbyStation);
}

bool GameSimulation::dock(const std::string& station, const std::string& slot)
{
    return docking_.requestDock(station, slot);
}

void GameSimulation::undock()
{
    docking_.undock();
}

bool GameSimulation::isDocked() const noexcept
{
    return docking_.state() == gameplay::DockState::Docked;
}

int GameSimulation::cargoCount(const std::string& commodity) const
{
    auto it = economy_.cargo.items.find(commodity);
    return it != economy_.cargo.items.end() ? it->second : 0;
}

int GameSimulation::stock(const std::string& station, const std::string& commodity) const
{
    auto sit = economy_.stations.find(station);
    if (sit == economy_.stations.end()) return 0;
    auto mit = sit->second.stock.find(commodity);
    return mit != sit->second.stock.end() ? mit->second : 0;
}

void GameSimulation::configureMarket(const std::string& station, const std::string& commodity,
                                     int price, int initialStock)
{
    auto& market = economy_.stations[station];
    market.setPrice(commodity, price);
    market.stock[commodity] = initialStock;
}

bool GameSimulation::buy(const std::string& station, const std::string& commodity, int amount)
{
    auto result = economy_.buy(station, commodity, amount);
    if (!result)
    {
        lastTradeError_ = result.error();
        return false;
    }
    lastTradeError_.clear();
    return true;
}

bool GameSimulation::sell(const std::string& station, const std::string& commodity, int amount)
{
    auto result = economy_.sell(station, commodity, amount);
    if (!result)
    {
        lastTradeError_ = result.error();
        return false;
    }
    lastTradeError_.clear();
    return true;
}

void GameSimulation::defineMission(gameplay::MissionDef def)
{
    missions_.define(std::move(def));
}

bool GameSimulation::acceptMission(const std::string& id)
{
    return missions_.accept(id).has_value();
}

bool GameSimulation::deliverMission(const std::string& id, const std::string& commodity,
                                    int amount, const std::string& station)
{
    auto result = missions_.deliver(id, commodity, amount, station);
    return result.has_value() && *result;
}

bool GameSimulation::completeMission(const std::string& id, const std::string& station)
{
    auto result = missions_.complete(id, station);
    if (!result) return false;
    economy_.credits += result->credits;
    progression_.applyUnlock(result->unlock);
    progression_.setCompletedMissions(progression_.completedMissions() + 1);
    jump_.setRangeLy(progression_.jumpRangeLy());
    return true;
}

int GameSimulation::completedMissions() const noexcept
{
    return progression_.completedMissions();
}

bool GameSimulation::missionActive(const std::string& id) const noexcept
{
    const auto* state = missions_.state(id);
    return state && state->status == gameplay::MissionStatus::Active;
}

bool GameSimulation::missionCompleted(const std::string& id) const noexcept
{
    const auto* state = missions_.state(id);
    return state && state->status == gameplay::MissionStatus::Completed;
}

void GameSimulation::addAuthored(gameplay::ContentPlacement placement)
{
    layered_.addAuthored(std::move(placement));
}

void GameSimulation::setPlayerMod(const std::string& targetId, gameplay::PlayerModType type)
{
    layered_.setPlayerMod(targetId, type);
}

std::size_t GameSimulation::resolveContent(const gameplay::CellKey& cell,
                                          std::vector<gameplay::ContentPlacement> generated) const
{
    return layered_.resolve(cell, std::move(generated)).size();
}

bool GameSimulation::jump(const std::string& targetSystem, float distanceLy)
{
    return jump_.jump(targetSystem, distanceLy).has_value();
}

} // namespace starforge::game
