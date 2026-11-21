#include "gameplay/economy.hpp"

#include <cstdint>
#include <limits>

namespace starforge::gameplay
{

int CargoHold::used() const noexcept
{
    std::int64_t total = 0;
    for (const auto& [commodity, amount] : items)
    {
        (void)commodity;
        total += static_cast<std::int64_t>(amount);
    }
    if (total > static_cast<std::int64_t>(std::numeric_limits<int>::max()))
    {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(std::max<std::int64_t>(0, total));
}

bool CargoHold::canAdd(const std::string& commodity, int amount) const
{
    (void)commodity;
    if (amount < 0) return false;
    const std::int64_t total = static_cast<std::int64_t>(used()) + static_cast<std::int64_t>(amount);
    return total <= static_cast<std::int64_t>(capacity);
}

bool CargoHold::add(const std::string& commodity, int amount)
{
    if (amount <= 0) return false;
    if (!canAdd(commodity, amount)) return false;
    auto& slot = items[commodity];
    const std::int64_t next = static_cast<std::int64_t>(slot) + static_cast<std::int64_t>(amount);
    if (next > static_cast<std::int64_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }
    slot = static_cast<int>(next);
    return true;
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
    const std::int64_t cost64 = static_cast<std::int64_t>(price) * static_cast<std::int64_t>(amount);
    if (cost64 > static_cast<std::int64_t>(std::numeric_limits<int>::max()) ||
        cost64 > static_cast<std::int64_t>(credits))
    {
        return std::unexpected("insufficient credits");
    }
    const int cost = static_cast<int>(cost64);

    credits -= cost;
    if (!cargo.add(commodity, amount))
    {
        credits += cost;
        return std::unexpected("failed to add cargo");
    }
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
    auto priceIt = market.prices.find(commodity);
    if (priceIt == market.prices.end())
        return std::unexpected("commodity not traded at " + station + ": " + commodity);
    const int price = priceIt->second;
    if (price <= 0) return std::unexpected("commodity has no price");
    const std::int64_t revenue64 = static_cast<std::int64_t>(price) * static_cast<std::int64_t>(amount);
    if (revenue64 > static_cast<std::int64_t>(std::numeric_limits<int>::max()) ||
        revenue64 > static_cast<std::int64_t>(std::numeric_limits<int>::max() - credits))
    {
        return std::unexpected("trade revenue overflows credits");
    }

    auto cargoIt = cargo.items.find(commodity);
    if (cargoIt == cargo.items.end() || cargoIt->second < amount)
    {
        return std::unexpected("insufficient cargo");
    }

    const int revenue = static_cast<int>(revenue64);
    if (!cargo.remove(commodity, amount)) return std::unexpected("insufficient cargo");
    credits += revenue;
    market.stock[commodity] += amount;
    return TradeResult{revenue, amount, commodity};
}

} // namespace starforge::gameplay
