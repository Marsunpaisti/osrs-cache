#pragma once

// The world map (index 19): its maps' details, and the composite maps that
// piece the world map's images together from parts of the game's map, decoded
// as RuneLite's cache library decodes them (net.runelite:cache 1.13.5:
// definitions/loaders WorldMapLoader, WorldMapCompositeLoader,
// WorldMapDataLoader and WorldMapElementLoader, and WorldMapManager for the
// elements and the intermap links). The index's groups are unnamed in the
// current cache: the details are group 0 and the composite maps group 1, as
// WorldMapManager finds them.

#include <osrs/cache/store.h>

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace osrs::cache {

// A tile in the world (RuneLite region/Position, whose z is the plane).
struct WorldPoint {
    int x = 0;
    int y = 0;
    int plane = 0;

    // The cache's packing: the plane in bits 28 and 29, x in 14 to 27 and y
    // in 0 to 13; -1 unpacks to -1 for all three.
    static WorldPoint unpack(std::int32_t packed);

    friend auto operator<=>(const WorldPoint&, const WorldPoint&) = default;
};

// The four kinds of section a world map is made of, under RuneLite's names
// for them and their fields: each places part of the game's map on the
// world map. The byte before a section picks its kind: 0 WorldMapType1,
// 1 WorldMapType2, 2 WorldMapType3, 3 WorldMapType0.
struct WorldMapType0 {
    int plane = 0;
    int numberOfPlanes = 0;
    int xLow = 0;
    int chunkXLow = 0;
    int yLow = 0;
    int chunkYLow = 0;
    int xHigh = 0;
    int chunkXHigh = 0;
    int yHigh = 0;
    int chunkYHigh = 0;
};

struct WorldMapType1 {
    int plane = 0;
    int numberOfPlanes = 0;
    int xLowerLeft = 0;
    int yLowerLeft = 0;
    int xLowerRight = 0;
    int yUpperLeft = 0;
    int xUpperLeft = 0;
    int yLowerRight = 0;
    int xUpperRight = 0;
    int yUpperRight = 0;
};

struct WorldMapType2 {
    int plane = 0;
    int numberOfPlanes = 0;
    int xLow = 0;
    int yLow = 0;
    int xHigh = 0;
    int yHigh = 0;
};

struct WorldMapType3 {
    int oldPlane = 0;
    int numberOfPlanes = 0;
    int oldX = 0;
    int chunkOldXLow = 0;
    int chunkOldXHigh = 0;
    int oldY = 0;
    int chunkOldYLow = 0;
    int chunkOldYHigh = 0;
    int newX = 0;
    int chunkNewXLow = 0;
    int chunkNewXHigh = 0;
    int newY = 0;
    int chunkNewYLow = 0;
    int chunkNewYHigh = 0;
};

using WorldMapSection = std::variant<WorldMapType0, WorldMapType1, WorldMapType2, WorldMapType3>;

// One world map, such as the surface or a dungeon: a file of the details
// group (RuneLite WorldMapDefinition).
struct WorldMapDefinition {
    int fileId = 0;
    // The name scripts know the map by, as "main" for the surface.
    std::string safeName;
    std::string name;
    // Where the map opens.
    WorldPoint position;
    std::int32_t emptyTileColor = 0;
    std::int32_t backgroundColor = 0;
    bool isSurface = false;
    int defaultZoom = 0;
    // RuneLite's regionList.
    std::vector<WorldMapSection> sections;
};

// Every world map, by ascending file id. Throws std::runtime_error if the
// cache lacks them, or if one doesn't decode unless the store skips such
// objects (store.h).
std::vector<WorldMapDefinition> loadWorldMapDefinitions(const Store& store);

// A 64 by 64 tile map square of the game's map shown at another square of
// the world map's image (RuneLite MapSquareDefinition). Squares are counted
// in tiles / 64.
struct MapSquareDefinition {
    int minLevel = 0;
    int levels = 0;
    int sourceSquareX = 0;
    int sourceSquareZ = 0;
    int displaySquareX = 0;
    int displaySquareZ = 0;
    // Only in the composite maps' format before the world map index's
    // revision 426; 0 after, -1 for none before.
    int groupId = 0;
    int fileId = 0;
};

// An 8 by 8 tile zone of a map square shown elsewhere (RuneLite
// ZoneDefinition). Zones are counted in (tiles % 64) / 8.
struct ZoneDefinition {
    int minLevel = 0;
    int levels = 0;
    int sourceSquareX = 0;
    int sourceSquareZ = 0;
    int sourceZoneX = 0;
    int sourceZoneZ = 0;
    int displaySquareX = 0;
    int displaySquareZ = 0;
    int displayZoneX = 0;
    int displayZoneZ = 0;
    // As MapSquareDefinition's.
    int groupId = 0;
    int fileId = 0;
};

// An icon or label on the world map: an area definition at a place on the
// world map's image (RuneLite WorldMapElementDefinition).
struct WorldMapElementDefinition {
    WorldPoint position;
    // What takes the position from the image back to the world, from the
    // zone or map square shown there; nothing where none is shown.
    std::optional<WorldPoint> offset;
    int areaDefinitionId = 0;
    bool membersOnly = false;

    // Where the element is in the world (RuneLite getWorldPosition): on
    // plane 0 where there is no offset.
    WorldPoint worldPosition() const;
};

// A file of the composite maps group (RuneLite WorldMapCompositeDefinition).
struct WorldMapCompositeDefinition {
    int fileId = 0;
    // RuneLite keeps these in hash sets, equal by where they're shown: a
    // square or zone shown where an earlier one is shown is dropped, as the
    // set drops it. In the cache's order otherwise.
    std::vector<MapSquareDefinition> mapSquares;
    std::vector<ZoneDefinition> zones;
    std::vector<WorldMapElementDefinition> elements;

    // The offset from a place on the world map's image to the world, from
    // the zone shown there, else the map square; nothing if neither is
    // (RuneLite calculateWorldOffset).
    std::optional<WorldPoint> worldOffset(WorldPoint displayed) const;
};

// A composite map file's contents, in the format the world map index's
// revision picks, as WorldMapManager picks it (Store::index(Index::WorldMap)
// gives the revision). Throws std::runtime_error if it doesn't decode.
WorldMapCompositeDefinition decodeWorldMapComposite(std::span<const std::uint8_t> bytes, std::int32_t worldMapRevision);

// Every composite map, by ascending file id. Throws std::runtime_error if
// the cache lacks them, or if one doesn't decode unless the store skips such
// objects (store.h).
std::vector<WorldMapCompositeDefinition> loadWorldMapComposites(const Store& store);

// Every composite map's elements, by file and then in the file's order
// (RuneLite WorldMapManager.getElements).
std::vector<WorldMapElementDefinition> loadWorldMapElements(const Store& store);

// Where each of the world map's links between maps (a dungeon entrance
// drawn on the surface, say) takes the map: from the link's position to
// the one it shows (RuneLite WorldMapManager.getIntermapLinks). They live in
// client script 1705: its switch's cases are the starts and the position
// pushed before each call to script 1706 the ends. Throws
// std::runtime_error if the script is missing, or if it doesn't decode
// unless the store skips such objects (store.h); skipped, there are no links.
std::map<WorldPoint, WorldPoint> loadIntermapLinks(const Store& store);

} // namespace osrs::cache
