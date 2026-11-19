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
    messages_.erase(std::remove_if(messages_.begin(), messages_.end(),
                                   [](const CommsMessage& m) { return m.ttl <= 0.0f; }),
                    messages_.end());
}

void CommsLog::clear() noexcept
{
    messages_.clear();
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

} // namespace starforge::game
