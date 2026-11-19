#include "file_watcher.hpp"

#include <utility>

namespace starforge::asset
{

FileWatcher::Entry FileWatcher::stat(const std::string& path) noexcept
{
    Entry entry;
    std::error_code ec;
    entry.exists = std::filesystem::exists(path, ec);
    if (!entry.exists || ec) return entry;
    entry.lastWrite = std::filesystem::last_write_time(path, ec);
    if (ec) return entry;
    entry.size = std::filesystem::file_size(path, ec);
    return entry;
}

bool FileWatcher::watch(const std::string& path)
{
    const Entry current = stat(path);
    if (!current.exists) return false;
    entries_[path] = current;
    return true;
}

bool FileWatcher::unwatch(const std::string& path) noexcept
{
    return entries_.erase(path) != 0;
}

bool FileWatcher::isWatching(const std::string& path) const noexcept
{
    return entries_.find(path) != entries_.end();
}

void FileWatcher::snapshot() noexcept
{
    for (auto& [path, entry] : entries_) entry = stat(path);
}

std::vector<FileEvent> FileWatcher::poll()
{
    std::vector<FileEvent> changes;
    for (auto& [path, previous] : entries_)
    {
        const Entry current = stat(path);
        if (current.exists && !previous.exists)
            changes.push_back({path, FileChange::Created});
        else if (!current.exists && previous.exists)
            changes.push_back({path, FileChange::Removed});
        else if (current.exists && previous.exists &&
                 (current.lastWrite != previous.lastWrite || current.size != previous.size))
            changes.push_back({path, FileChange::Modified});
        previous = current;
    }
    return changes;
}

} // namespace starforge::asset
