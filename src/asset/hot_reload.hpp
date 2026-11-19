#pragma once

// M17.2: ties the polling watcher to the asset manager. Watched paths are
// marked dirty on change and immediately reloaded, with stable handles.

#include "asset_manager.hpp"
#include "file_watcher.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace starforge::asset
{

class HotReloadCoordinator
{
public:
    explicit HotReloadCoordinator(AssetManager& manager) noexcept : manager_(manager) {}

    // Watch a path that is already loaded in the manager.
    bool watch(const std::string& path) { return watcher_.watch(path); }
    bool unwatch(const std::string& path) noexcept { return watcher_.unwatch(path); }
    [[nodiscard]] std::size_t watchedCount() const noexcept { return watcher_.watchedCount(); }
    [[nodiscard]] const FileWatcher& watcher() const noexcept { return watcher_; }

    // Pump file changes, mark affected assets dirty and reload them.
    // Returns the paths that failed to reload.
    [[nodiscard]] std::vector<std::string> poll();

    // Events observed on the last poll (for HUD/status reporting).
    [[nodiscard]] const std::vector<FileEvent>& lastEvents() const noexcept { return lastEvents_; }
    [[nodiscard]] std::size_t reloadCount() const noexcept { return reloadCount_; }

private:
    AssetManager& manager_;
    FileWatcher watcher_;
    std::vector<FileEvent> lastEvents_;
    std::size_t reloadCount_ = 0;
};

} // namespace starforge::asset
