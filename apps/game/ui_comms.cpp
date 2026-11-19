#include "ui_comms.hpp"

#include <algorithm>

namespace starforge::game
{

void CommsLog::push(CommsChannel channel, std::string sender, std::string text, float ttl)
{
    messages_.push_back(CommsMessage{channel, std::move(sender), std::move(text), ttl});
    if (messages_.size() > kCapacity)
        messages_.erase(messages_.begin(), messages_.begin() + (messages_.size() - kCapacity));
}

void CommsLog::tick(float dt) noexcept
{
    if (dt <= 0.0f) return;
    for (auto& message : messages_) message.ttl -= dt;
    if (!expireAfterTtl_) return;
    messages_.erase(std::remove_if(messages_.begin(), messages_.end(),
                                   [](const CommsMessage& m) { return m.ttl <= 0.0f; }),
                    messages_.end());
}

void CommsLog::clear() noexcept
{
    messages_.clear();
    scrollOffset_ = 0;
}

std::pair<std::size_t, std::size_t>
CommsLog::visibleRange(std::size_t windowSize) const noexcept
{
    const std::size_t n = messages_.size();
    if (n == 0 || windowSize == 0) return {0, 0};
    const std::size_t maxOffset = n > windowSize ? n - windowSize : 0;
    const std::size_t offset = std::min(scrollOffset_, maxOffset);
    const std::size_t end = n - offset;
    const std::size_t begin = end > windowSize ? end - windowSize : 0;
    return {begin, end};
}

void TextEntry::begin(std::string prompt) noexcept
{
    active_ = true;
    text_.clear();
    prompt_ = std::move(prompt);
}

void TextEntry::end() noexcept
{
    active_ = false;
    text_.clear();
}

void TextEntry::append(const std::string& typed)
{
    if (active_) text_ += typed;
}

void TextEntry::backspace() noexcept
{
    if (active_ && !text_.empty()) text_.pop_back();
}

std::string TextEntry::submit()
{
    if (!active_) return {};
    std::string result = text_;
    text_.clear();
    return result;
}

std::string npcReply(const std::string& npcId)
{
    static const char* replies[] = {
        "copy that, standing by",
        "affirmative, clear skies",
        "roger, adjusting course",
        "no copy, say again",
        "copy, good hunting out there",
    };
    std::size_t hash = 0;
    for (const char c : npcId) hash = hash * 131u + static_cast<unsigned char>(c);
    return replies[hash % (sizeof(replies) / sizeof(replies[0]))];
}

} // namespace starforge::game
