#pragma once

// M16.7: pure-CPU communications log + text entry state. No backend types.
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace starforge::game
{

enum class CommsChannel
{
    Local,
    Combat,
    Trade,
    System,
};

struct CommsMessage
{
    CommsChannel channel = CommsChannel::System;
    std::string sender;
    std::string text;
    float ttl = 0.0f; // seconds remaining on screen
};

// A comms log can either expire messages (used for the transient toasts) or
// keep a permanent scrollable history (used for the comms window).
class CommsLog
{
public:
    explicit CommsLog(bool expireAfterTtl = true) noexcept : expireAfterTtl_(expireAfterTtl) {}

    static constexpr std::size_t kCapacity = 200;
    static constexpr float kDefaultTtl = 12.0f;

    void push(CommsChannel channel, std::string sender, std::string text,
              float ttl = kDefaultTtl);
    void tick(float dt) noexcept;
    void clear() noexcept;

    [[nodiscard]] const std::vector<CommsMessage>& messages() const noexcept { return messages_; }
    [[nodiscard]] std::size_t count() const noexcept { return messages_.size(); }

    // --- Scrollback (pure CPU, T1-testable) ---
    void scrollBack(std::size_t lines = 1) noexcept { scrollOffset_ += lines; }
    void scrollForward(std::size_t lines = 1) noexcept
    {
        scrollOffset_ = scrollOffset_ > lines ? scrollOffset_ - lines : 0;
    }
    void scrollToBottom() noexcept { scrollOffset_ = 0; }
    [[nodiscard]] std::size_t scrollOffset() const noexcept { return scrollOffset_; }
    [[nodiscard]] bool scrolledUp() const noexcept { return scrollOffset_ > 0; }

    // [begin, end) range of messages currently visible in a window of
    // `windowSize` lines, newest at the bottom.
    [[nodiscard]] std::pair<std::size_t, std::size_t>
    visibleRange(std::size_t windowSize) const noexcept;

private:
    std::vector<CommsMessage> messages_;
    bool expireAfterTtl_ = true;
    std::size_t scrollOffset_ = 0; // lines scrolled up from the newest
};

class TextEntry
{
public:
    void begin(std::string prompt = "") noexcept;
    void end() noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] const std::string& prompt() const noexcept { return prompt_; }

    void append(const std::string& typed);
    void backspace() noexcept;

    // Returns the committed line and clears the buffer, or empty if idle.
    std::string submit();

private:
    bool active_ = false;
    std::string text_;
    std::string prompt_;
};

// Deterministic canned reply from an NPC, so the comms input box can "chat"
// with sector traffic today and be wired to a real chat AI later.
[[nodiscard]] std::string npcReply(const std::string& npcId);

} // namespace starforge::game
