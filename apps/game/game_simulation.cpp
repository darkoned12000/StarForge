#include "game_simulation.hpp"

#include <algorithm>
#include <cmath>
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

void GameSimulation::spawnHostile()
{
    hostile_.alive = true;
    hostile_.engaging = true;
    hostile_.cooldown = 1.0f;
    hostile_.phase = 0.0f;
    hostile_.position = RawVec3f{0.0f, 30.0f, -260.0f}; // ahead of the ship
    hostile_.velocity = RawVec3f{};
    hostile_.damage = gameplay::DamageState{};
    hostile_.damage.shieldMax = hostile_.damage.shield = 60.0f;
    hostile_.damage.hullMax = hostile_.damage.hull = 80.0f;
}

HostileInfo GameSimulation::hostile() const noexcept
{
    return HostileInfo{
        .position = hostile_.position,
        .alive = hostile_.alive,
        .engaging = hostile_.engaging,
        .shield = hostile_.damage.shield,
        .hull = hostile_.damage.hull,
    };
}

std::vector<GameEvent> GameSimulation::step(float dt, const RawVec3f& shipForward,
                                            bool firePrimary, bool fireSecondary)
{
    std::vector<GameEvent> events;
    if (dt <= 0.0f) return events;

    primaryCooldown_ = std::max(0.0f, primaryCooldown_ - dt);
    secondaryCooldown_ = std::max(0.0f, secondaryCooldown_ - dt);
    gameplay::tickDamage(player_, dt, 4.0f);

    if (!hostile_.alive) return events;

    hostile_.cooldown = std::max(0.0f, hostile_.cooldown - dt);
    hostile_.evadeTimer = std::max(0.0f, hostile_.evadeTimer - dt);
    gameplay::tickDamage(hostile_.damage, dt, 3.0f);

    // Engage / orbit / evade state machine (M14 hostile behaviour). The
    // strafe + vertical component makes it circle the player instead of
    // bouncing in a single straight line.
    const float distance = length(hostile_.position);
    if (hostile_.evadeTimer > 0.0f) hostile_.engaging = false; // break off when hit
    else if (distance > 140.0f) hostile_.engaging = true;
    else if (distance < 70.0f) hostile_.engaging = false;

    hostile_.phase += dt;
    const RawVec3f toward = normalized(RawVec3f{-hostile_.position.x, -hostile_.position.y,
                                                -hostile_.position.z});
    // Right vector = toward x world-up (degenerate only when directly above).
    RawVec3f right{RawVec3f{toward.z, 0.0f, -toward.x}};
    right = normalized(right);
    const float speed = hostile_.engaging ? 42.0f : 55.0f;
    const float strafe = std::sin(hostile_.phase * 0.8f) * 26.0f;
    const float climb = std::sin(hostile_.phase * 0.53f + 1.2f) * 14.0f;
    const float radial = hostile_.engaging ? speed : -speed;
    hostile_.velocity = RawVec3f{
        toward.x * radial + right.x * strafe,
        toward.y * radial + climb,
        toward.z * radial + right.z * strafe};
    hostile_.position = RawVec3f{hostile_.position.x + hostile_.velocity.x * dt,
                                 hostile_.position.y + hostile_.velocity.y * dt,
                                 hostile_.position.z + hostile_.velocity.z * dt};

    // Hostile fires at the player when in range and cooldown allows.
    if (distance < 400.0f && hostile_.cooldown <= 0.0f)
    {
        hostile_.cooldown = 1.2f;
        auto result = gameplay::applyDamage(player_, 12.0f, gameplay::Subsystem::Hull);
        if (result.shieldAbsorbed > 0.0f)
            events.push_back(GameEvent{GameEvent::Type::PlayerShieldHit, result.shieldAbsorbed, "shield"});
        if (result.hullDamage > 0.0f)
            events.push_back(GameEvent{GameEvent::Type::PlayerHullHit, result.hullDamage, "hull"});
    }

    // Player weapons: range + forward-cone test, cooldown per trigger.
    const RawVec3f aim = normalized(hostile_.position);
    const bool inFront = dot(shipForward, aim) > 0.72f;
    const bool inRange = distance < 600.0f;
    auto fire = [&](bool held, float& cooldown, float interval, float damage, const char* label) {
        if (!held || cooldown > 0.0f || !inRange || !inFront) return;
        cooldown = interval;
        events.push_back(GameEvent{GameEvent::Type::WeaponFired, damage, label});
        auto result = gameplay::applyDamage(hostile_.damage, damage, gameplay::Subsystem::Hull);
        const float dealt = result.shieldAbsorbed + result.hullDamage;
        if (dealt > 0.0f) hostile_.evadeTimer = 2.0f; // reactive evasion
        events.push_back(GameEvent{GameEvent::Type::HostileHit, dealt, "hostile"});
        if (result.destroyed)
        {
            hostile_.alive = false;
            events.push_back(GameEvent{GameEvent::Type::HostileDestroyed, 0.0f, "hostile"});
        }
    };
    fire(firePrimary, primaryCooldown_, 0.15f, 8.0f, "primary");
    fire(fireSecondary, secondaryCooldown_, 1.0f, 30.0f, "secondary");

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
