#include "hot_reload.hpp"

namespace starforge::asset
{

std::vector<std::string> HotReloadCoordinator::poll()
{
    lastEvents_ = watcher_.poll();
    std::size_t marked = 0;
    for (const auto& event : lastEvents_)
        if (event.change == FileChange::Modified && manager_.markDirty(event.path))
            ++marked;

    if (marked == 0) return {};
    auto failures = manager_.reloadDirty();
    reloadCount_ += marked >= failures.size() ? marked - failures.size() : 0;
    return failures;
}

} // namespace starforge::asset
