#pragma once

#include <expected>
#include <map>
#include <string>

namespace starforge::gameplay
{

struct CargoHold
{
    std::map<std::string, int> items;
    int capacity = 100;

    [[nodiscard]] int used() const noexcept;
    [[nodiscard]] bool canAdd(const std::string& commodity, int amount) const;
    void add(const std::string& commodity, int amount);
    [[nodiscard]] bool remove(const std::string& commodity, int amount);
};

struct Market
{
    std::map<std::string, int> stock;
    std::map<std::string, int> prices; // credits per unit

    [[nodiscard]] int priceOf(const std::string& commodity) const;
    void setPrice(const std::string& commodity, int price);
};

struct TradeResult
{
    int creditsDelta = 0;
    int quantity = 0;
    std::string commodity;
};

// Economic state. Player changes are kept here (a sparse overlay) rather than
// mutating generated base chunks.
struct EconomyState
{
    int credits = 1000;
    CargoHold cargo;
    std::map<std::string, Market> stations;

    [[nodiscard]] std::expected<TradeResult, std::string>
    buy(const std::string& station, const std::string& commodity, int amount);
    [[nodiscard]] std::expected<TradeResult, std::string>
    sell(const std::string& station, const std::string& commodity, int amount);
};

} // namespace starforge::gameplay
