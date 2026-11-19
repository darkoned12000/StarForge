#include "gameplay/economy.hpp"

namespace starforge::gameplay
{

int CargoHold::used() const noexcept
{
    int total = 0;
    for (const auto& [commodity, amount] : items)
    {
        (void)commodity;
        total += amount;
    }
    return total;
}

bool CargoHold::canAdd(const std::string& commodity, int amount) const
{
    (void)commodity;
    return amount >= 0 && used() + amount <= capacity;
}

void CargoHold::add(const std::string& commodity, int amount)
{
    if (amount <= 0) return;
    items[commodity] += amount;
}

bool CargoHold::remove(const std::string& commodity, int amount)
{
    if (amount <= 0) return true;
    auto it = items.find(commodity);
    if (it == items.end() || it->second < amount) return false;
    it->second -= amount;
    if (it->second == 0) items.erase(it);
    return true;
}

int Market::priceOf(const std::string& commodity) const
{
    auto it = prices.find(commodity);
    return it != prices.end() ? it->second : 0;
}

void Market::setPrice(const std::string& commodity, int price)
{
    prices[commodity] = price;
}

std::expected<TradeResult, std::string>
EconomyState::buy(const std::string& station, const std::string& commodity, int amount)
{
    if (amount <= 0) return std::unexpected("trade amount must be positive");
    auto stationIt = stations.find(station);
    if (stationIt == stations.end()) return std::unexpected("unknown station: " + station);
    auto& market = stationIt->second;
    auto stockIt = market.stock.find(commodity);
    if (stockIt == market.stock.end())
        return std::unexpected("commodity not traded at " + station + ": " + commodity);
    if (stockIt->second < amount) return std::unexpected("insufficient stock");
    if (!cargo.canAdd(commodity, amount)) return std::unexpected("cargo capacity exceeded");

    const int price = market.priceOf(commodity);
    if (price <= 0) return std::unexpected("commodity has no price");
    const int cost = price * amount;
    if (cost > credits) return std::unexpected("insufficient credits");

    credits -= cost;
    cargo.add(commodity, amount);
    stockIt->second -= amount;
    return TradeResult{-cost, amount, commodity};
}

std::expected<TradeResult, std::string>
EconomyState::sell(const std::string& station, const std::string& commodity, int amount)
{
    if (amount <= 0) return std::unexpected("trade amount must be positive");
    auto stationIt = stations.find(station);
    if (stationIt == stations.end()) return std::unexpected("unknown station: " + station);
    auto& market = stationIt->second;
    if (market.prices.find(commodity) == market.prices.end())
        return std::unexpected("commodity not traded at " + station + ": " + commodity);
    if (!cargo.remove(commodity, amount)) return std::unexpected("insufficient cargo");

    const int price = market.priceOf(commodity);
    const int revenue = price * amount;
    credits += revenue;
    market.stock[commodity] += amount;
    return TradeResult{revenue, amount, commodity};
}

} // namespace starforge::gameplay
