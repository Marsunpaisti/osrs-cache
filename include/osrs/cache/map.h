#pragma once

// The maps index (5): each region's terrain and the objects placed on it,
// decoded as RuneLite's cache library decodes them (net.runelite:cache
// 1.13.5: region/RegionLoader, definitions/loaders MapLoader and
// LocationsLoader, region/Region and HeightCalc for the heights). The terrain
// names underlay and overlay definitions (osrs/cache/definitions.h). A region
// is 64 by 64 tiles on 4 planes; its id is (regionX << 8) | regionY, and its
// south-west tile is at (regionX * 64, regionY * 64).
//
// The cache holds a region in one of two layouts, as RuneLite's RegionLoader
// reads them: a maps index that names its groups has a group "m<x>_<y>" for the
// terrain and "l<x>_<y>" for the objects, the latter encrypted with a key of
// the region's own; one that doesn't has a group numbered by region id, the
// terrain in file 0 and the objects in file 1, unencrypted.

#include <osrs/cache/store.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace osrs::cache {

struct TerrainTile {
    // The height of the tile's south-west corner, as the client keeps it:
    // negative going up, 128 to a tile, from the cache or, where the cache has
    // none, the client's height noise (RuneLite Region.getTileHeight).
    std::int32_t height = 0;
    // Underlay and overlay definition ids plus one; 0 for none. 15 bits, as
    // RuneLite's Region gives them.
    std::uint16_t underlay = 0;
    std::uint16_t overlay = 0;
    // The overlay's shape (RuneLite's overlay path, 0 for the whole tile) and
    // its quarter turns.
    std::uint8_t overlayShape = 0;
    std::uint8_t overlayRotation = 0;
    // Bit 1 blocks the floor; bit 2 on plane 1 makes the tile a bridge, whose
    // planes are drawn one lower.
    std::uint8_t settings = 0;
};

// An object placed on the map (RuneLite's Location).
struct Location {
    std::int32_t object = 0;
    // RuneLite's type: 0 to 3 walls, 4 to 8 wall decorations, 9 to 21 other
    // objects and roofs, 22 floor decorations.
    std::uint8_t shape = 0;
    // Quarter turns: west, north, east, south.
    std::uint8_t rotation = 0;
    std::uint8_t plane = 0;
    // From the region's south-west corner.
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

struct Region {
    int id = 0;
    // [plane][x][y], 4 * 64 * 64 tiles.
    std::vector<TerrainTile> tiles;
    std::vector<Location> locations;

    const TerrainTile& tile(int plane, int x, int y) const { return tiles[(plane * 64 + x) * 64 + y]; }
};

// A terrain tile as the cache stores it (RuneLite MapDefinition.Tile), before
// Region derives heights from it.
struct MapTile {
    // The tile's height above the plane below, in units of 8 (1 standing for
    // 0); nothing where the client derives it.
    std::optional<std::uint8_t> height;
    // The attribute that gave the overlay, 2 to 49, carrying its shape and
    // rotation; 0 for none (RuneLite's attrOpcode).
    std::uint8_t overlayAttribute = 0;
    std::uint8_t settings = 0;
    // Definition ids plus one, all 16 bits as stored.
    std::uint16_t overlay = 0;
    std::uint8_t overlayShape = 0;
    std::uint8_t overlayRotation = 0;
    std::uint16_t underlay = 0;
};

// A region's terrain (RuneLite MapDefinition).
struct MapDefinition {
    int regionX = 0;
    int regionY = 0;
    // [plane][x][y], 4 * 64 * 64 tiles.
    std::vector<MapTile> tiles;

    const MapTile& tile(int plane, int x, int y) const { return tiles[(plane * 64 + x) * 64 + y]; }
};

// The objects placed on a region (RuneLite LocationsDefinition).
struct LocationsDefinition {
    int regionX = 0;
    int regionY = 0;
    std::vector<Location> locations;
};

// The key a region's objects are encrypted with; nothing if it isn't known
// (RuneLite util/KeyProvider).
using XteaKeyProvider = std::function<std::optional<XteaKey>(int regionId)>;

// Nothing if the cache lacks the region's terrain or its objects. Throws
// std::runtime_error if either doesn't decode, unless the store skips such
// objects (store.h). Reads only a maps index that
// numbers its groups by region.
std::optional<Region> loadRegion(const Store& store, int regionId);

// RuneLite RegionLoader.loadMapDef: nothing if the cache has no terrain group
// for the region. Throws std::runtime_error if, in a region's numbered
// group, there is no file 0, or if it doesn't decode unless the store skips
// such objects (store.h).
std::optional<MapDefinition> loadMapDefinition(const Store& store, int regionId);

// RuneLite RegionLoader.loadLocDef: nothing if the cache has no objects group
// for the region or, where they are encrypted, `keys` has no key for it.
// Throws std::runtime_error if, in a region's numbered group, there is no
// file 1, or if they don't decode unless the store skips such objects
// (store.h). A wrong key leaves the group unreadable, which throws either
// way.
std::optional<LocationsDefinition> loadLocationsDefinition(const Store& store, int regionId,
                                                           const XteaKeyProvider& keys = nullptr);

// RuneLite RegionLoader.loadRegion(id, map, locs): the heights Region derives
// from the terrain, and the objects if there are any.
Region loadRegion(int regionId, const MapDefinition& terrain, const LocationsDefinition* locations);

} // namespace osrs::cache
