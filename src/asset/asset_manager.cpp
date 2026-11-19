#include "asset_manager.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace starforge::asset
{

std::string AssetManager::normalizePath(const std::string& path)
{
    return std::filesystem::path(path).lexically_normal().generic_string();
}

void AssetManager::registerLoader(AssetType type, AssetLoader loader)
{
    loaders_[type] = std::move(loader);
}

std::expected<AssetHandle, std::string>
AssetManager::load(AssetType type, const std::string& path)
{
    const std::string key = normalizePath(path);
    const auto existing = byPath_.find(key);
    if (existing != byPath_.end())
    {
        const auto recordIt = records_.find(existing->second);
        if (recordIt != records_.end())
        {
            Record& record = recordIt->second;
            if (!record.info.loaded)
                return std::unexpected(record.info.error.empty() ? "asset failed to load" : record.info.error);
            ++record.info.refCount;
            return record.handle;
        }
    }

    const auto loaderIt = loaders_.find(type);
    if (loaderIt == loaders_.end() || !loaderIt->second)
        return std::unexpected("no loader registered for asset type");

    std::size_t loadedBytes = 0;
    std::string error;
    auto payload = loaderIt->second(path, loadedBytes, error);
    if (!payload)
        return std::unexpected(error.empty() ? "asset loader returned no payload" : error);

    AssetHandle handle{nextId_++};
    Record record;
    record.info.path = key;
    record.info.type = type;
    record.info.bytes = loadedBytes;
    record.info.refCount = 1;
    record.info.loaded = true;
    record.handle = handle;
    record.type = type;
    record.payload = std::move(payload);

    byPath_[key] = handle.id;
    bytes_ += loadedBytes;
    records_.emplace(handle.id, std::move(record));
    return handle;
}

AssetHandle AssetManager::find(const std::string& path) const noexcept
{
    const auto it = byPath_.find(normalizePath(path));
    if (it == byPath_.end()) return {};
    const auto recordIt = records_.find(it->second);
    if (recordIt == records_.end() || !recordIt->second.info.loaded) return {};
    return recordIt->second.handle;
}

bool AssetManager::retain(AssetHandle handle) noexcept
{
    const auto recordIt = findRecord(handle);
    if (recordIt == records_.end()) return false;
    auto& record = recordIt->second;
    if (!record.info.loaded) return false;
    ++record.info.refCount;
    return true;
}

void AssetManager::release(AssetHandle handle) noexcept
{
    auto it = findRecord(handle);
    if (it == records_.end()) return;
    Record& record = it->second;
    if (record.info.refCount > 0) --record.info.refCount;
    if (record.info.refCount != 0) return;

    unload(record);
    byPath_.erase(record.info.path);
    dirty_.erase(record.info.path);
    records_.erase(it);
}

void AssetManager::unload(Record& record) noexcept
{
    if (!record.info.loaded) return;
    bytes_ = bytes_ >= record.info.bytes ? bytes_ - record.info.bytes : 0;
    record.info.bytes = 0;
    record.info.loaded = false;
    record.payload.reset();
}

void* AssetManager::getRaw(AssetHandle handle) const noexcept
{
    const auto it = findRecord(handle);
    return it == records_.end() ? nullptr : it->second.payload.get();
}

const AssetInfo* AssetManager::info(AssetHandle handle) const noexcept
{
    const auto it = findRecord(handle);
    return it == records_.end() ? nullptr : &it->second.info;
}

bool AssetManager::markDirty(const std::string& path) noexcept
{
    const auto handle = find(path);
    if (!handle.valid()) return false;
    dirty_.insert(normalizePath(path));
    return true;
}

std::vector<std::string> AssetManager::dirtyPaths() const
{
    return {dirty_.begin(), dirty_.end()};
}

std::vector<std::string> AssetManager::reloadDirty()
{
    std::vector<std::string> failures;
    if (dirty_.empty()) return failures;
    const auto paths = dirty_;
    dirty_.clear();
    for (const auto& path : paths)
    {
        const auto pathIt = byPath_.find(path);
        if (pathIt == byPath_.end()) continue;
        const auto recordIt = records_.find(pathIt->second);
        if (recordIt == records_.end()) continue;
        Record& record = recordIt->second;
        const auto loaderIt = loaders_.find(record.type);
        if (loaderIt == loaders_.end() || !loaderIt->second)
        {
            failures.push_back(path);
            if (reloadCallback_) reloadCallback_(record.handle, false);
            continue;
        }

        std::size_t newBytes = 0;
        std::string error;
        auto payload = loaderIt->second(path, newBytes, error);
        if (!payload)
        {
            record.info.error = error;
            failures.push_back(path);
            if (reloadCallback_) reloadCallback_(record.handle, false);
            continue;
        }

        bytes_ = bytes_ >= record.info.bytes ? bytes_ - record.info.bytes : 0;
        record.payload = std::move(payload);
        record.info.bytes = newBytes;
        record.info.loaded = true;
        record.info.error.clear();
        bytes_ += newBytes;
        ++reloadCount_;
        if (reloadCallback_) reloadCallback_(record.handle, true);
    }
    return failures;
}

void AssetManager::clear() noexcept
{
    byPath_.clear();
    records_.clear();
    dirty_.clear();
    bytes_ = 0;
}

auto AssetManager::findRecord(AssetHandle handle)
    -> std::unordered_map<std::uint64_t, Record>::iterator
{
    return records_.find(handle.id);
}

auto AssetManager::findRecord(AssetHandle handle) const
    -> std::unordered_map<std::uint64_t, Record>::const_iterator
{
    return records_.find(handle.id);
}

} // namespace starforge::asset
