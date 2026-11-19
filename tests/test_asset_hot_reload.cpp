// M17.2 (T1): polling watcher + hot reload coordination.
#include <doctest/doctest.h>

#include "asset/hot_reload.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

using namespace starforge::asset;

namespace
{
struct TextPayload
{
    std::string text;
};

std::filesystem::path tempDir()
{
    static const auto dir = std::filesystem::temp_directory_path() / "starforge_m17_watch";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void writeFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}
} // namespace

TEST_CASE("M17.2: watcher detects create, modify and remove")
{
    const auto path = tempDir() / "watched.txt";
    std::filesystem::remove(path);
    FileWatcher watcher;
    CHECK_FALSE(watcher.watch(path.string())); // does not exist yet
    writeFile(path, "first");
    REQUIRE(watcher.watch(path.string()));
    CHECK(watcher.isWatching(path.string()));
    CHECK(watcher.poll().empty());

    // Change size to guarantee the poll notices on coarse timestamp FSes.
    writeFile(path, "second version is longer");
    auto events = watcher.poll();
    REQUIRE(events.size() == 1);
    CHECK(events[0].path == path.string());
    CHECK(events[0].change == FileChange::Modified);
    CHECK(watcher.poll().empty()); // edge, not level

    std::filesystem::remove(path);
    events = watcher.poll();
    REQUIRE(events.size() == 1);
    CHECK(events[0].change == FileChange::Removed);

    CHECK(watcher.unwatch(path.string()));
    CHECK(watcher.watchedCount() == 0);
}

TEST_CASE("M17.2: hot reload keeps the handle and swaps the payload")
{
    const auto path = tempDir() / "asset.txt";
    writeFile(path, "v1");

    AssetManager manager;
    manager.registerLoader(AssetType::Texture,
        [](const std::string& p, std::size_t& bytes, std::string& error) -> std::shared_ptr<void> {
            std::ifstream file(p, std::ios::binary);
            if (!file)
            {
                error = "missing file";
                return nullptr;
            }
            auto payload = std::make_shared<TextPayload>();
            payload->text = std::string(std::istreambuf_iterator<char>(file), {});
            bytes = payload->text.size();
            return payload;
        });

    const auto handle = manager.load(AssetType::Texture, path.string());
    REQUIRE(handle.has_value());
    REQUIRE(manager.get<TextPayload>(*handle)->text == "v1");

    HotReloadCoordinator reload(manager);
    REQUIRE(reload.watch(path.string()));
    CHECK(reload.poll().empty());

    writeFile(path, "version-two-longer");
    const auto failures = reload.poll();
    CHECK(failures.empty());
    CHECK(reload.reloadCount() == 1);
    CHECK(reload.lastEvents().size() == 1);
    CHECK(manager.get<TextPayload>(*handle)->text == "version-two-longer");
    CHECK(manager.info(*handle)->bytes == std::string("version-two-longer").size());
    CHECK(manager.info(*handle)->loaded);

    std::filesystem::remove(path);
}
