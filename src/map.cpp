#include <osrs/cache/map.h>

#include "buffer.h"
#include "undecodable.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace osrs::cache {

namespace {

// RuneLite region/HeightCalc, under its own MIT licence
// (LICENSE-runelite.txt): the client's height noise for tiles the cache gives
// no height.
class HeightNoise {
public:
    HeightNoise() {
        // The client's cosine table, of 2048 steps to a turn.
        const double radian = (360.0 / 2048.0) * (std::numbers::pi / 180.0);
        for (int i = 0; i < 2048; ++i) cos_[i] = static_cast<int>(65536.0 * std::cos(i * radian));
    }

    int height(int x, int y) const {
        int n = interpolated(x + 45365, y + 91923, 4) - 128 + (interpolated(10294 + x, y + 37821, 2) - 128 >> 1)
                + (interpolated(x, y, 1) - 128 >> 2);
        n = 35 + static_cast<int>(static_cast<double>(n) * 0.3);
        return std::clamp(n, 10, 60);
    }

private:
    // Java's int arithmetic, which wraps.
    static int noise(int x, int y) {
        std::uint32_t n = static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(y) * 57u;
        n ^= n << 13;
        const std::uint32_t mixed = n * (n * n * 15731u + 789221u) + 1376312589u;
        return static_cast<int>(mixed & 0x7FFFFFFFu) >> 19 & 255;
    }

    static int smoothed(int x, int y) {
        const int corners = noise(x - 1, y - 1) + noise(x + 1, y - 1) + noise(x - 1, y + 1) + noise(x + 1, y + 1);
        const int sides = noise(x - 1, y) + noise(x + 1, y) + noise(x, y - 1) + noise(x, y + 1);
        return noise(x, y) / 4 + sides / 8 + corners / 16;
    }

    int interpolate(int a, int b, int x, int frequency) const {
        const int f = 65536 - cos_[1024 * x / frequency] >> 1;
        return (f * b >> 16) + (a * (65536 - f) >> 16);
    }

    int interpolated(int x, int y, int frequency) const {
        const int intX = x / frequency;
        const int fracX = x & (frequency - 1);
        const int intY = y / frequency;
        const int fracY = y & (frequency - 1);
        const int south = interpolate(smoothed(intX, intY), smoothed(intX + 1, intY), fracX, frequency);
        const int north = interpolate(smoothed(intX, intY + 1), smoothed(intX + 1, intY + 1), fracX, frequency);
        return interpolate(south, north, fracY, frequency);
    }

    std::array<int, 2048> cos_{};
};

// RuneLite MapLoader.loadTerrain: per tile, attributes until 0, or until 1
// and the tile's height above the plane below.
MapDefinition decodeTerrain(int regionId, std::span<const std::uint8_t> bytes) {
    MapDefinition map;
    map.regionX = regionId >> 8;
    map.regionY = regionId & 0xFF;
    // [plane][x][y] is the order the cache stores the tiles in.
    map.tiles.assign(4 * 64 * 64, MapTile{});
    Buffer in(bytes);
    for (MapTile& tile : map.tiles) {
        for (;;) {
            const int attribute = in.u16();
            if (attribute == 0) break;
            if (attribute == 1) {
                tile.height = static_cast<std::uint8_t>(in.u8());
                break;
            }
            if (attribute <= 49) {
                tile.overlayAttribute = static_cast<std::uint8_t>(attribute);
                tile.overlay = static_cast<std::uint16_t>(in.u16());
                tile.overlayShape = static_cast<std::uint8_t>((attribute - 2) / 4);
                tile.overlayRotation = static_cast<std::uint8_t>((attribute - 2) & 3);
            } else if (attribute <= 81) {
                tile.settings = static_cast<std::uint8_t>(attribute - 49);
            } else {
                tile.underlay = static_cast<std::uint16_t>(attribute - 81);
            }
        }
    }
    return map;
}

// RuneLite LocationsLoader.loadLocations: object ids and packed positions,
// each as deltas.
LocationsDefinition decodeLocations(int regionId, std::span<const std::uint8_t> bytes) {
    LocationsDefinition locations;
    locations.regionX = regionId >> 8;
    locations.regionY = regionId & 0xFF;
    Buffer in(bytes);
    std::int32_t id = -1;
    for (int idDelta = in.unsignedIntSmart(); idDelta != 0; idDelta = in.unsignedIntSmart()) {
        id += idDelta;
        int position = 0;
        for (int positionDelta = in.unsignedShortSmart(); positionDelta != 0; positionDelta = in.unsignedShortSmart()) {
            position += positionDelta - 1;
            const int attributes = in.u8();
            locations.locations.push_back(Location{id, static_cast<std::uint8_t>(attributes >> 2), static_cast<std::uint8_t>(attributes & 3),
                                                   static_cast<std::uint8_t>(position >> 12 & 3), static_cast<std::uint8_t>(position >> 6 & 63),
                                                   static_cast<std::uint8_t>(position & 63)});
        }
    }
    return locations;
}

std::string regionName(char prefix, int regionId) {
    return prefix + std::to_string(regionId >> 8) + "_" + std::to_string(regionId & 0xFF);
}

// RuneLite RegionLoader reads a group's data as Archive.decompress gives it,
// so a group the reference table lists without data on disk throws there.
std::vector<std::uint8_t> namedGroup(const Store& store, int group, const std::optional<XteaKey>& key) {
    std::optional<std::vector<std::uint8_t>> data = store.contents(Index::Maps, group, key);
    if (!data) throw std::runtime_error("maps group " + std::to_string(group) + " has no data");
    return std::move(*data);
}

std::vector<std::uint8_t> numberedFile(const Store& store, int regionId, int file) {
    std::optional<std::map<int, std::vector<std::uint8_t>>> files = store.files(Index::Maps, regionId);
    if (!files || !files->contains(file)) {
        throw std::runtime_error("maps group " + std::to_string(regionId) + " has no file " + std::to_string(file));
    }
    return std::move(files->at(file));
}

} // namespace

std::optional<MapDefinition> loadMapDefinition(const Store& store, int regionId) {
    const IndexInfo* maps = store.index(Index::Maps);
    if (maps == nullptr) return std::nullopt;
    if (maps->named) {
        const std::optional<int> group = store.findGroup(Index::Maps, regionName('m', regionId));
        if (!group) return std::nullopt;
        const std::vector<std::uint8_t> bytes = namedGroup(store, *group, std::nullopt);
        return decodeOrSkip(store, "map", regionId, [&] { return decodeTerrain(regionId, bytes); });
    }
    if (!maps->groups.contains(regionId)) return std::nullopt;
    const std::vector<std::uint8_t> bytes = numberedFile(store, regionId, 0);
    return decodeOrSkip(store, "map", regionId, [&] { return decodeTerrain(regionId, bytes); });
}

std::optional<LocationsDefinition> loadLocationsDefinition(const Store& store, int regionId, const XteaKeyProvider& keys) {
    const IndexInfo* maps = store.index(Index::Maps);
    if (maps == nullptr) return std::nullopt;
    if (maps->named) {
        const std::optional<int> group = store.findGroup(Index::Maps, regionName('l', regionId));
        if (!group || !keys) return std::nullopt;
        const std::optional<XteaKey> key = keys(regionId);
        if (!key) return std::nullopt;
        const std::vector<std::uint8_t> bytes = namedGroup(store, *group, key);
        return decodeOrSkip(store, "locations", regionId, [&] { return decodeLocations(regionId, bytes); });
    }
    if (!maps->groups.contains(regionId)) return std::nullopt;
    const std::vector<std::uint8_t> bytes = numberedFile(store, regionId, 1);
    return decodeOrSkip(store, "locations", regionId, [&] { return decodeLocations(regionId, bytes); });
}

// RuneLite Region.loadTerrain: a tile without a height lies 240 above the
// plane below, or on plane 0 at the client's noise.
Region loadRegion(int regionId, const MapDefinition& terrain, const LocationsDefinition* locations) {
    static const HeightNoise noise;
    const int baseX = ((regionId >> 8) & 0xFF) << 6;
    const int baseY = (regionId & 0xFF) << 6;
    Region region;
    region.id = regionId;
    region.tiles.assign(4 * 64 * 64, TerrainTile{});
    for (int plane = 0; plane < 4; ++plane) {
        for (int x = 0; x < 64; ++x) {
            for (int y = 0; y < 64; ++y) {
                const MapTile& from = terrain.tile(plane, x, y);
                TerrainTile& tile = region.tiles[(plane * 64 + x) * 64 + y];
                const int below = plane == 0 ? 0 : region.tiles[((plane - 1) * 64 + x) * 64 + y].height;
                if (!from.height) {
                    tile.height = plane == 0 ? -noise.height(baseX + x + 0xe3b7b, baseY + y + 0x87cce) * 8 : below - 240;
                } else {
                    tile.height = below - (*from.height == 1 ? 0 : *from.height) * 8;
                }
                tile.underlay = static_cast<std::uint16_t>(from.underlay & 0x7FFF);
                tile.overlay = static_cast<std::uint16_t>(from.overlay & 0x7FFF);
                tile.overlayShape = from.overlayShape;
                tile.overlayRotation = from.overlayRotation;
                tile.settings = from.settings;
            }
        }
    }
    if (locations != nullptr) region.locations = locations->locations;
    return region;
}

std::optional<Region> loadRegion(const Store& store, int regionId) {
    const auto files = store.files(Index::Maps, regionId);
    if (!files || !files->contains(0) || !files->contains(1)) return std::nullopt;
    return decodeOrSkip(store, "region", regionId, [&] {
        const MapDefinition terrain = decodeTerrain(regionId, files->at(0));
        const LocationsDefinition locations = decodeLocations(regionId, files->at(1));
        return loadRegion(regionId, terrain, &locations);
    });
}

} // namespace osrs::cache
