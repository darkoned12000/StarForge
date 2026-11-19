// M17.1 (T1): CPU asset registry — dedup, refcounts, reload queue.
#include <doctest/doctest.h>

#include "asset/asset_manager.hpp"

#include <memory>
#include <string>

using namespace starforge::asset;

namespace
{
struct Payload
{
    int value = 0;
};

AssetLoader intLoader()
{
    return [](const std::string& path, std::size_t& bytes, std::string& error)
        -> std::shared_ptr<void> {
        if (path.find("bad") != std::string::npos)
        {
            error = "load failed";
            return nullptr;
        }
        bytes = 128;
        return std::make_shared<Payload>(static_cast<int>(path.size()));
    };
}
} // namespace

TEST_CASE("M17.1: asset manager dedups by normalized path and refcounts")
{
    AssetManager manager;
    manager.registerLoader(AssetType::Mesh, intLoader());

    const auto a = manager.load(AssetType::Mesh, "assets/./ship.glb");
    REQUIRE(a.has_value());
    CHECK(manager.count() == 1);
    CHECK(manager.info(*a)->path == "assets/ship.glb");
    CHECK(manager.info(*a)->refCount == 1);
    CHECK(manager.info(*a)->bytes == 128);
    CHECK(manager.bytes() == 128);
    CHECK(manager.get<Payload>(*a)->value == std::string("assets/./ship.glb").size());

    // Same normalized path dedups and adds a reference, not a new record.
    const auto b = manager.load(AssetType::Mesh, "assets/ship.glb");
    REQUIRE(b.has_value());
    CHECK(*a == *b);
    CHECK(manager.count() == 1);
    CHECK(manager.info(*a)->refCount == 2);

    manager.release(*a);
    CHECK(manager.count() == 1); // one reference remains
    CHECK(manager.info(*b)->refCount == 1);
    CHECK(manager.find("assets/ship.glb") == *b);

    manager.release(*b);
    CHECK(manager.count() == 0);
    CHECK(manager.bytes() == 0);
    CHECK_FALSE(manager.find("assets/ship.glb").valid());
}

TEST_CASE("M17.1: retention, missing loaders and failed loads")
{
    AssetManager manager;
    CHECK_FALSE(manager.load(AssetType::Mesh, "x.glb").has_value()); // no loader

    manager.registerLoader(AssetType::Mesh, intLoader());
    const auto bad = manager.load(AssetType::Mesh, "bad.glb");
    CHECK_FALSE(bad.has_value());
    CHECK(manager.count() == 0);
    CHECK(manager.bytes() == 0);

    const auto handle = manager.load(AssetType::Mesh, "good.glb");
    REQUIRE(handle.has_value());
    CHECK(manager.retain(*handle));
    CHECK(manager.info(*handle)->refCount == 2);
    CHECK_FALSE(manager.retain(AssetHandle{9999}));
    manager.release(*handle);
    manager.release(*handle);
    manager.release(*handle); // extra release is harmless
    CHECK(manager.count() == 0);
}

TEST_CASE("M17.1: dirty queue reloads in place with stable handles and callbacks")
{
    AssetManager manager;
    manager.registerLoader(AssetType::Mesh, intLoader());
    const auto handle = manager.load(AssetType::Mesh, "live.glb");
    REQUIRE(handle.has_value());

    int successes = 0, failures = 0;
    manager.onReload([&](AssetHandle h, bool ok) {
        CHECK(h == *handle);
        ok ? ++successes : ++failures;
    });

    CHECK(manager.markDirty("live.glb"));
    CHECK(manager.dirtyPaths().size() == 1);
    CHECK(manager.reloadDirty().empty());
    CHECK(manager.reloadCount() == 1);
    CHECK(successes == 1);
    CHECK(manager.info(*handle)->loaded);

    // Reload failures keep the old payload and report the error.
    // The loader fails for paths containing "bad", so swap the path key by
    // marking the same handle dirty after re-registering a failing loader.
    manager.registerLoader(AssetType::Mesh,
        [](const std::string&, std::size_t&, std::string& error) -> std::shared_ptr<void> {
            error = "offline";
            return nullptr;
        });
    CHECK(manager.markDirty("live.glb"));
    const auto failed = manager.reloadDirty();
    REQUIRE(failed.size() == 1);
    CHECK(failed.front() == "live.glb");
    CHECK(failures == 1);
    CHECK(manager.get<Payload>(*handle) != nullptr); // old payload survived

    manager.clear();
    CHECK(manager.count() == 0);
    CHECK(manager.dirtyPaths().empty());
}
