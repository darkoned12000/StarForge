// M16.7 (T1): communications log and text entry are pure CPU.
#include <doctest/doctest.h>

#include "ui_comms.hpp"

using namespace starforge::game;

TEST_CASE("M16.7: comms log keeps newest messages and expires old ones")
{
    CommsLog log;
    log.push(CommsChannel::Combat, "hostile", "lock lost", 5.0f);
    log.push(CommsChannel::Trade, "station-a", "bought 5 ore", 2.0f);
    REQUIRE(log.count() == 2);
    CHECK(log.messages().back().channel == CommsChannel::Trade);

    log.tick(1.0f);
    CHECK(log.count() == 2);
    log.tick(1.5f); // trade message (ttl 2) expires
    CHECK(log.count() == 1);
    CHECK(log.messages().front().sender == "hostile");
    log.tick(10.0f);
    CHECK(log.count() == 0);

    // Ring buffer never exceeds capacity and keeps the most recent.
    for (std::size_t i = 0; i < CommsLog::kCapacity + 5; ++i)
        log.push(CommsChannel::System, "sys", std::to_string(i), 30.0f);
    CHECK(log.count() == CommsLog::kCapacity);
    CHECK(log.messages().back().text == std::to_string(CommsLog::kCapacity + 4));
    log.clear();
    CHECK(log.count() == 0);
}

TEST_CASE("M16.7: text entry submits and clears")
{
    TextEntry entry;
    CHECK_FALSE(entry.active());
    CHECK(entry.submit().empty());

    entry.begin(">");
    CHECK(entry.active());
    CHECK(entry.prompt() == ">");
    entry.append("hello");
    entry.append(" world");
    CHECK(entry.text() == "hello world");
    entry.backspace();
    CHECK(entry.text() == "hello worl");

    CHECK(entry.submit() == "hello worl");
    CHECK(entry.text().empty());
    CHECK(entry.active()); // still capturing after submit

    entry.append("!");
    CHECK(entry.text() == "!");
    entry.end();
    CHECK_FALSE(entry.active());
    entry.append("ignored");
    CHECK(entry.text().empty());
}

TEST_CASE("M16 polish: comms keeps a permanent scrollable history")
{
    CommsLog log{false}; // non-expiring
    for (int i = 0; i < 20; ++i)
        log.push(CommsChannel::Local, "npc", "line" + std::to_string(i), 1.0f);
    log.tick(100.0f);
    CHECK(log.count() == 20); // TTL does not erase permanent history

    const auto [first, last] = log.visibleRange(5);
    CHECK(first == 15);
    CHECK(last == 20);
    CHECK(log.messages()[last - 1].text == "line19");

    log.scrollBack(3);
    const auto scrolled = log.visibleRange(5);
    CHECK(scrolled.first == 12);
    CHECK(scrolled.second == 17);
    CHECK(log.scrolledUp());
    log.scrollForward(1);
    CHECK(log.scrollOffset() == 2);
    log.scrollToBottom();
    CHECK_FALSE(log.scrolledUp());
    CHECK(log.visibleRange(5).first == 15);

    // Toasts still expire on TTL.
    CommsLog toasts{true};
    toasts.push(CommsChannel::Combat, "", "HIT", 1.0f);
    toasts.tick(2.0f);
    CHECK(toasts.count() == 0);
}

TEST_CASE("M16 polish: NPC chat replies are deterministic and non-empty")
{
    CHECK_FALSE(npcReply("trader-1").empty());
    CHECK(npcReply("trader-1") == npcReply("trader-1"));
}
