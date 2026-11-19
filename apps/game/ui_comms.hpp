#pragma once

// M16.7: pure-CPU communications log + text entry state. No backend types.
#include <cstddef>
#include <string>
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

class CommsLog
{
public:
    static constexpr std::size_t kCapacity = 24;
    static constexpr float kDefaultTtl = 12.0f;

    void push(CommsChannel channel, std::string sender, std::string text,
              float ttl = kDefaultTtl);
    void tick(float dt) noexcept;
    void clear() noexcept;

    [[nodiscard]] const std::vector<CommsMessage>& messages() const noexcept { return messages_; }
    [[nodiscard]] std::size_t count() const noexcept { return messages_.size(); }

private:
    std::vector<CommsMessage> messages_;
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

} // namespace starforge::game
