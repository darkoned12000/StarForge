#include "content/player_delta.hpp"
#include "content/hash.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace starforge::content
{

namespace
{

// Serialize helpers (little-endian, byte-by-byte for platform independence).
void putU32LE(std::uint8_t*& p, std::uint32_t v) noexcept
{
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    p += 4;
}

void putU64LE(std::uint8_t*& p, std::uint64_t v) noexcept
{
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    p[4] = static_cast<std::uint8_t>((v >> 32) & 0xFF);
    p[5] = static_cast<std::uint8_t>((v >> 40) & 0xFF);
    p[6] = static_cast<std::uint8_t>((v >> 48) & 0xFF);
    p[7] = static_cast<std::uint8_t>((v >> 56) & 0xFF);
    p += 8;
}

void putI64LE(std::uint8_t*& p, std::int64_t v) noexcept
{
    putU64LE(p, static_cast<std::uint64_t>(v));
}

void putF32LE(std::uint8_t*& p, float v) noexcept
{
    putU32LE(p, std::bit_cast<std::uint32_t>(v));
}

void putF64LE(std::uint8_t*& p, double v) noexcept
{
    putU64LE(p, std::bit_cast<std::uint64_t>(v));
}

// Deserialize helpers.
std::uint32_t getU32LE(const std::uint8_t*& p) noexcept
{
    std::uint32_t v = static_cast<std::uint32_t>(p[0])
                    | (static_cast<std::uint32_t>(p[1]) << 8)
                    | (static_cast<std::uint32_t>(p[2]) << 16)
                    | (static_cast<std::uint32_t>(p[3]) << 24);
    p += 4;
    return v;
}

std::uint64_t getU64LE(const std::uint8_t*& p) noexcept
{
    std::uint64_t v = static_cast<std::uint64_t>(p[0])
                    | (static_cast<std::uint64_t>(p[1]) << 8)
                    | (static_cast<std::uint64_t>(p[2]) << 16)
                    | (static_cast<std::uint64_t>(p[3]) << 24)
                    | (static_cast<std::uint64_t>(p[4]) << 32)
                    | (static_cast<std::uint64_t>(p[5]) << 40)
                    | (static_cast<std::uint64_t>(p[6]) << 48)
                    | (static_cast<std::uint64_t>(p[7]) << 56);
    p += 8;
    return v;
}

std::int64_t getI64LE(const std::uint8_t*& p) noexcept
{
    return static_cast<std::int64_t>(getU64LE(p));
}

float getF32LE(const std::uint8_t*& p) noexcept
{
    return std::bit_cast<float>(getU32LE(p));
}

double getF64LE(const std::uint8_t*& p) noexcept
{
    return std::bit_cast<double>(getU64LE(p));
}

// Record layout (all little-endian, fixed 84-byte stride):
//   cellX: int64 (8) | cellY: int64 (8) | cellZ: int64 (8)   -> 24
//   level: uint32 (4) | padding: uint32 (4)                  -> 32
//   objectId: uint64 (8)                                     -> 40
//   type: uint8 (1) | objectType: uint8 (1) | padding: 6     -> 48
//   scale: float (4) | subtype: uint32 (4) | properties: uint32 (4) -> 60
//   localX: double (8) | localY: double (8) | localZ: double (8)    -> 84
constexpr std::size_t kHeaderSize = 16; // magic(4) + version(4) + uint64 record count
constexpr std::size_t kRecordSize = 84;

} // namespace

std::uint64_t PlayerDelta::key() const noexcept
{
    DeltaKey k{cellX, cellY, cellZ, level, objectId};
    DeltaKeyHash h;
    return h(k);
}

void DeltaStore::apply(const PlayerDelta& delta)
{
    const DeltaKey k{delta.cellX, delta.cellY, delta.cellZ, delta.level, delta.objectId};
    const CellKey ck{delta.cellX, delta.cellY, delta.cellZ, delta.level};

    auto it = deltas_.find(k);
    if (it == deltas_.end())
    {
        cellIndex_[ck].push_back(k);
    }
    deltas_[k] = delta;
}

std::vector<PlayerDelta> DeltaStore::deltasForCell(
    std::int64_t cx, std::int64_t cy, std::int64_t cz, int level) const
{
    std::vector<PlayerDelta> result;
    const CellKey ck{cx, cy, cz, level};
    auto it = cellIndex_.find(ck);
    if (it == cellIndex_.end()) return result;
    result.reserve(it->second.size());
    for (const DeltaKey& k : it->second)
    {
        auto dit = deltas_.find(k);
        if (dit != deltas_.end())
            result.push_back(dit->second);
    }
    return result;
}

bool DeltaStore::isRemoved(
    std::int64_t cx, std::int64_t cy, std::int64_t cz, int level,
    std::uint64_t objectId) const
{
    const DeltaKey k{cx, cy, cz, level, objectId};
    auto it = deltas_.find(k);
    if (it == deltas_.end()) return false;
    return it->second.type == DeltaType::Removed;
}

const PlayerDelta* DeltaStore::getDelta(
    std::int64_t cx, std::int64_t cy, std::int64_t cz, int level,
    std::uint64_t objectId) const
{
    const DeltaKey k{cx, cy, cz, level, objectId};
    auto it = deltas_.find(k);
    if (it == deltas_.end()) return nullptr;
    return &it->second;
}

std::vector<std::uint8_t> DeltaStore::serialize() const
{
    // Canonical order: sort by full key so save bytes do not depend on hash-map
    // iteration order (and therefore on insertion order or stdlib version).
    std::vector<DeltaKey> keys;
    keys.reserve(deltas_.size());
    for (const auto& [k, v] : deltas_)
    {
        (void)v;
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());

    std::vector<std::uint8_t> data;
    data.reserve(kHeaderSize + keys.size() * kRecordSize);
    data.resize(kHeaderSize);
    std::uint8_t* p = data.data();
    *p++ = 'S'; *p++ = 'F'; *p++ = 'D'; *p++ = 'L';
    putU32LE(p, 1);
    p += 4;
    putU64LE(p, static_cast<std::uint64_t>(keys.size()));
    p += 8;

    for (const DeltaKey& k : keys)
    {
        auto it = deltas_.find(k);
        if (it == deltas_.end()) continue;
        const PlayerDelta& v = it->second;

        const size_t offset = data.size();
        data.resize(offset + kRecordSize);
        p = data.data() + offset;

        putI64LE(p, v.cellX);
        putI64LE(p, v.cellY);
        putI64LE(p, v.cellZ);
        putU32LE(p, static_cast<std::uint32_t>(v.level));
        putU32LE(p, 0); // padding
        putU64LE(p, v.objectId);
        *p++ = static_cast<std::uint8_t>(v.type);
        *p++ = static_cast<std::uint8_t>(v.objectType);
        for (int i = 0; i < 6; ++i) *p++ = 0; // padding
        putF32LE(p, v.scale);
        putU32LE(p, v.subtype);
        putU32LE(p, v.properties);
        putF64LE(p, v.localX);
        putF64LE(p, v.localY);
        putF64LE(p, v.localZ);
    }

    return data;
}

std::size_t DeltaStore::deserialize(const std::vector<std::uint8_t>& data)
{
    clear();
    if (data.size() < kHeaderSize) return 0;

    const std::uint8_t* const begin = data.data();
    const std::uint8_t* p = begin;

    // Check magic signature
    if (p[0] != 'S' || p[1] != 'F' || p[2] != 'D' || p[3] != 'L') return 0;
    p += 4;

    const std::uint32_t version = getU32LE(p);
    if (version != 1) return 0;

    std::uint64_t count = getU64LE(p);

    // Never trust a corrupt count: cap it by the remaining payload.
    const std::uint64_t available = (data.size() - kHeaderSize) / kRecordSize;
    if (count > available) count = available;

    std::size_t loaded = 0;
    for (std::uint64_t i = 0; i < count; ++i)
    {
        const std::uint8_t* const recordStart = p;
        if (static_cast<std::size_t>(recordStart - begin) + kRecordSize > data.size())
            break;

        PlayerDelta delta;
        delta.cellX = getI64LE(p);
        delta.cellY = getI64LE(p);
        delta.cellZ = getI64LE(p);
        const std::uint32_t levelRaw = getU32LE(p);
        (void)getU32LE(p); // padding
        delta.objectId = getU64LE(p);

        const std::uint8_t typeByte = *p++;
        const std::uint8_t objectTypeByte = *p++;
        p += 6; // padding

        const float scale = getF32LE(p);
        const std::uint32_t subtype = getU32LE(p);
        const std::uint32_t properties = getU32LE(p);
        const double localX = getF64LE(p);
        const double localY = getF64LE(p);
        const double localZ = getF64LE(p);

        // Always advance exactly one record, even for rejected records.
        p = recordStart + kRecordSize;

        if (levelRaw > static_cast<std::uint32_t>(kMaxScaleLevel)) continue;
        if (typeByte > static_cast<std::uint8_t>(DeltaType::Added)) continue;
        if (objectTypeByte > static_cast<std::uint8_t>(ContentType::Nebula)) continue;

        if (!std::isfinite(scale) || !std::isfinite(localX) ||
            !std::isfinite(localY) || !std::isfinite(localZ))
            continue;

        delta.level = static_cast<int>(levelRaw);
        delta.type = static_cast<DeltaType>(typeByte);
        delta.objectType = static_cast<ContentType>(objectTypeByte);
        delta.scale = scale;
        delta.subtype = subtype;
        delta.properties = properties;
        delta.localX = localX;
        delta.localY = localY;
        delta.localZ = localZ;

        apply(delta);
        ++loaded;
    }

    return loaded;
}

ContentChunk applyDeltas(const ContentChunk& generated, const DeltaStore& store)
{
    auto deltas = store.deltasForCell(
        generated.cellX, generated.cellY, generated.cellZ, generated.level);

    if (deltas.empty())
    {
        return generated;
    }

    ContentChunk result = generated;

    std::unordered_map<std::uint64_t, const PlayerDelta*> byId;
    byId.reserve(deltas.size() * 2);
    for (const auto& d : deltas)
    {
        byId[d.objectId] = &d;
    }

    std::vector<ContentObject> filtered;
    filtered.reserve(result.objects.size() + deltas.size());

    std::unordered_set<std::uint64_t> presentIds;
    presentIds.reserve(result.objects.size() + deltas.size());

    for (const auto& obj : result.objects)
    {
        auto it = byId.find(obj.objectId);
        if (it != byId.end())
        {
            const PlayerDelta& d = *it->second;
            if (d.type == DeltaType::Removed)
            {
                continue;
            }
            if (d.type == DeltaType::Modified)
            {
                ContentObject modified = obj;
                modified.scale = d.scale;
                modified.subtype = d.subtype;
                modified.properties = d.properties;
                modified.localX = d.localX;
                modified.localY = d.localY;
                modified.localZ = d.localZ;
                presentIds.insert(modified.objectId);
                filtered.push_back(std::move(modified));
                continue;
            }
        }

        presentIds.insert(obj.objectId);
        filtered.push_back(obj);
    }

    for (const auto& d : deltas)
    {
        if (d.type != DeltaType::Added) continue;
        // Do not create a second object with an id that already exists.
        if (!presentIds.insert(d.objectId).second) continue;

        ContentObject added;
        added.objectId = d.objectId;
        added.type = d.objectType;
        added.scale = d.scale;
        added.subtype = d.subtype;
        added.properties = d.properties;
        added.localX = d.localX;
        added.localY = d.localY;
        added.localZ = d.localZ;
        filtered.push_back(std::move(added));
    }

    result.objects = std::move(filtered);
    return result;
}

} // namespace starforge::content
