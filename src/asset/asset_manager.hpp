#pragma once

// M17.1: CPU-only asset registry. Typed handles, path dedup, refcounts,
// byte accounting and a dirty/reload queue. No Vulkan or engine math types
// so it can be driven headlessly by the T1 suite.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace starforge::asset
{

enum class AssetType
{
    Unknown = 0,
    Mesh,
    Texture,
    Audio,
    Scene,
};

struct AssetHandle
{
    std::uint64_t id = 0;
    [[nodiscard]] bool valid() const noexcept { return id != 0; }
    bool operator==(const AssetHandle&) const noexcept = default;
};

struct AssetInfo
{
    std::string path;
    AssetType type = AssetType::Unknown;
    std::size_t bytes = 0;
    std::uint32_t refCount = 0;
    bool loaded = false;
    std::string error;
};

// Type-erased loader. On success it returns the payload and fills `outBytes`;
// on failure it returns nullptr and fills `outError`.
using AssetLoader = std::function<std::shared_ptr<void>(const std::string& path,
                                                        std::size_t& outBytes,
                                                        std::string& outError)>;

using ReloadCallback = std::function<void(AssetHandle handle, bool success)>;

class AssetManager
{
public:
    // Register/replace the loader used for a given asset type.
    void registerLoader(AssetType type, AssetLoader loader);

    // Loads (or dedups) an asset and gives the caller one reference.
    // Returns an error if no loader is registered or loading fails.
    [[nodiscard]] std::expected<AssetHandle, std::string>
    load(AssetType type, const std::string& path);

    // Finds an already-loaded asset by path without loading it.
    [[nodiscard]] AssetHandle find(const std::string& path) const noexcept;

    // Reference management. Dropping the last reference unloads the payload.
    bool retain(AssetHandle handle) noexcept;
    void release(AssetHandle handle) noexcept;

    [[nodiscard]] void* getRaw(AssetHandle handle) const noexcept;
    template <class T>
    [[nodiscard]] T* get(AssetHandle handle) const noexcept
    {
        return static_cast<T*>(getRaw(handle));
    }

    [[nodiscard]] const AssetInfo* info(AssetHandle handle) const noexcept;

    // Hot-reload support: mark a path dirty, then reload all dirty assets
    // (handles stay stable). Returns the paths that failed to reload.
    bool markDirty(const std::string& path) noexcept;
    [[nodiscard]] std::vector<std::string> dirtyPaths() const;
    [[nodiscard]] std::vector<std::string> reloadDirty();

    // Called after each reload attempt (successful or not).
    void onReload(ReloadCallback callback) { reloadCallback_ = std::move(callback); }

    [[nodiscard]] std::size_t count() const noexcept { return records_.size(); }
    [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t reloadCount() const noexcept { return reloadCount_; }
    void clear() noexcept;

    [[nodiscard]] static std::string normalizePath(const std::string& path);

private:
    struct Record
    {
        AssetInfo info;
        AssetHandle handle;
        AssetType type = AssetType::Unknown;
        std::shared_ptr<void> payload;
    };

    [[nodiscard]] auto findRecord(AssetHandle handle)
        -> std::unordered_map<std::uint64_t, Record>::iterator;
    [[nodiscard]] auto findRecord(AssetHandle handle) const
        -> std::unordered_map<std::uint64_t, Record>::const_iterator;
    void unload(Record& record) noexcept;

    std::unordered_map<std::string, std::uint64_t> byPath_;
    std::unordered_map<std::uint64_t, Record> records_;
    std::map<AssetType, AssetLoader> loaders_;
    std::unordered_set<std::string> dirty_;
    ReloadCallback reloadCallback_;
    std::uint64_t nextId_ = 1;
    std::size_t bytes_ = 0;
    std::size_t reloadCount_ = 0;
};

} // namespace starforge::asset
