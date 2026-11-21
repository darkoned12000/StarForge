#include "hot_reload.hpp"

namespace starforge::asset
{

std::vector<std::string> HotReloadCoordinator::poll()
{
    lastEvents_ = watcher_.poll();
    for (const auto& event : lastEvents_)
    {
        if (event.change == FileChange::Modified || event.change == FileChange::Created)
            (void)manager_.markDirty(event.path);
    }

    const auto before = manager_.reloadCount();
    auto failures = manager_.reloadDirty();
    const auto after = manager_.reloadCount();
    reloadCount_ += after >= before ? after - before : 0;
    return failures;
}

} // namespace starforge::asset
