#pragma once

// M17.2: polling file watcher (no platform-specific APIs). Tracks last-write
// time, size and existence for each watched path. CPU-only.

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace starforge::asset
{

enum class FileChange
{
    Created,
    Modified,
    Removed,
};

struct FileEvent
{
    std::string path;
    FileChange change = FileChange::Modified;
};

class FileWatcher
{
public:
    // Starts watching an existing path. Returns false if it does not exist.
    bool watch(const std::string& path);
    bool unwatch(const std::string& path) noexcept;
    [[nodiscard]] bool isWatching(const std::string& path) const noexcept;
    [[nodiscard]] std::size_t watchedCount() const noexcept { return entries_.size(); }

    // Captures the current state without producing events.
    void snapshot() noexcept;

    // Compares current state to the last snapshot and returns the changes.
    [[nodiscard]] std::vector<FileEvent> poll();

private:
    struct Entry
    {
        std::filesystem::file_time_type lastWrite{};
        std::uintmax_t size = 0;
        bool exists = false;
    };

    [[nodiscard]] static Entry stat(const std::string& path) noexcept;

    std::unordered_map<std::string, Entry> entries_;
};

} // namespace starforge::asset
