#include <osrs/cache/worldmap.h>

#include "buffer.h"
#include "undecodable.h"

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace osrs::cache {

namespace {

constexpr int kDetailsGroup = 0;
constexpr int kCompositeGroup = 1;
constexpr int kIntermapLinkScript = 1705;
// The script the intermap links call to move the map; the position each
// link moves it to is the int pushed just before.
constexpr int kIntermapJumpScript = 1706;

// RuneLite WorldMapLoader.load0 to load3, keyed as its loadType keys them.
WorldMapSection decodeSection(Buffer& in) {
    const int type = in.u8();
    switch (type) {
    case 0: {
        WorldMapType1 section;
        section.plane = in.u8();
        section.numberOfPlanes = in.u8();
        section.xLowerLeft = in.u16();
        section.yLowerLeft = in.u16();
        section.xLowerRight = in.u16();
        section.yUpperLeft = in.u16();
        section.xUpperLeft = in.u16();
        section.yLowerRight = in.u16();
        section.xUpperRight = in.u16();
        section.yUpperRight = in.u16();
        return section;
    }
    case 1: {
        WorldMapType2 section;
        section.plane = in.u8();
        section.numberOfPlanes = in.u8();
        section.xLow = in.u16();
        section.yLow = in.u16();
        section.xHigh = in.u16();
        section.yHigh = in.u16();
        return section;
    }
    case 2: {
        WorldMapType3 section;
        section.oldPlane = in.u8();
        section.numberOfPlanes = in.u8();
        section.oldX = in.u16();
        section.chunkOldXLow = in.u8();
        section.chunkOldXHigh = in.u8();
        section.oldY = in.u16();
        section.chunkOldYLow = in.u8();
        section.chunkOldYHigh = in.u8();
        section.newX = in.u16();
        section.chunkNewXLow = in.u8();
        section.chunkNewXHigh = in.u8();
        section.newY = in.u16();
        section.chunkNewYLow = in.u8();
        section.chunkNewYHigh = in.u8();
        return section;
    }
    case 3: {
        WorldMapType0 section;
        section.plane = in.u8();
        section.numberOfPlanes = in.u8();
        section.xLow = in.u16();
        section.chunkXLow = in.u8();
        section.yLow = in.u16();
        section.chunkYLow = in.u8();
        section.xHigh = in.u16();
        section.chunkXHigh = in.u8();
        section.yHigh = in.u16();
        section.chunkYHigh = in.u8();
        return section;
    }
    default:
        throw std::runtime_error("world map section of unknown type " + std::to_string(type));
    }
}

// RuneLite WorldMapLoader.load.
WorldMapDefinition decodeWorldMap(std::span<const std::uint8_t> bytes, int fileId) {
    Buffer in(bytes);
    WorldMapDefinition map;
    map.fileId = fileId;
    map.safeName = in.string();
    map.name = in.string();
    map.position = WorldPoint::unpack(in.i32());
    map.emptyTileColor = in.i32();
    map.backgroundColor = in.i32();
    in.u8(); // RuneLite doesn't keep it
    map.isSurface = in.u8() == 1;
    map.defaultZoom = in.u8();
    const int sections = in.u8();
    for (int i = 0; i < sections; ++i) map.sections.push_back(decodeSection(in));
    return map;
}

// The part of RuneLite WorldMapDataLoader.loadMapSquare and loadZone both
// read, after the kind byte.
template <class Data>
void decodeSource(Buffer& in, Data& data, int expectedKind) {
    const int kind = in.u8();
    if (kind != expectedKind) {
        throw std::runtime_error("expected world map data kind " + std::to_string(expectedKind) + ", got " + std::to_string(kind));
    }
    data.minLevel = in.u8();
    data.levels = in.u8();
    data.sourceSquareX = in.u16();
    data.sourceSquareZ = in.u16();
}

// Only the format before the one WorldMapDataLoader calls rev238 has them.
template <class Data>
void decodeGroup(Buffer& in, Data& data, bool rev238) {
    if (rev238) return;
    data.groupId = in.bigSmartOrMinusOne();
    data.fileId = in.bigSmartOrMinusOne();
}

// RuneLite WorldMapDataLoader.loadMapSquare.
MapSquareDefinition decodeMapSquare(Buffer& in, bool rev238) {
    MapSquareDefinition square;
    decodeSource(in, square, 0);
    square.displaySquareX = in.u16();
    square.displaySquareZ = in.u16();
    decodeGroup(in, square, rev238);
    return square;
}

// RuneLite WorldMapDataLoader.loadZone.
ZoneDefinition decodeZone(Buffer& in, bool rev238) {
    ZoneDefinition zone;
    decodeSource(in, zone, 1);
    zone.sourceZoneX = in.u8();
    zone.sourceZoneZ = in.u8();
    zone.displaySquareX = in.u16();
    zone.displaySquareZ = in.u16();
    zone.displayZoneX = in.u8();
    zone.displayZoneZ = in.u8();
    decodeGroup(in, zone, rev238);
    return zone;
}

std::map<int, std::vector<std::uint8_t>> requireFiles(const Store& store, int group, const char* what) {
    std::optional<std::map<int, std::vector<std::uint8_t>>> files = store.files(Index::WorldMap, group);
    if (!files) throw std::runtime_error(std::string("the cache has no world map ") + what);
    return std::move(*files);
}

// As WorldMapManager finds it: by name if the index names its groups.
int worldMapGroup(const Store& store, const char* name, int unnamed) {
    const IndexInfo* index = store.index(Index::WorldMap);
    if (index == nullptr || !index->named) return unnamed;
    const std::optional<int> group = store.findGroup(Index::WorldMap, name);
    if (!group) throw std::runtime_error(std::string("the world map index names no group ") + name);
    return *group;
}

// What RuneLite's ScriptLoader reads of a client script that the intermap
// links need: each instruction's int operand, and the first switch's cases
// in the order it reads them.
struct ScriptOperands {
    std::vector<std::int32_t> intOperands;
    std::vector<std::pair<std::int32_t, std::int32_t>> firstSwitch;
};

// RuneLite ScriptLoader.load, and its configureForRevision: the client
// script index's revisions after 1773753883 add the long locals and
// arguments to the trailer.
ScriptOperands decodeScriptOperands(std::span<const std::uint8_t> bytes, std::int32_t scriptIndexRevision) {
    const bool rev237 = scriptIndexRevision > 1773753883;
    if (bytes.size() < 2) throw std::runtime_error("client script shorter than its trailer");
    const int switchLength = Buffer(bytes.subspan(bytes.size() - 2)).u16();
    const std::size_t trailer = 2 + static_cast<std::size_t>(switchLength) + (rev237 ? 16 : 12);
    if (trailer > bytes.size()) throw std::runtime_error("client script shorter than its trailer");
    const std::size_t end = bytes.size() - trailer;

    Buffer in(bytes.subspan(end));
    const std::int32_t instructionCount = in.i32();
    if (instructionCount < 0) throw std::runtime_error("client script with a negative instruction count");
    in.skip(rev237 ? 12 : 8); // the locals' and arguments' counts
    ScriptOperands script;
    const int switches = in.u8();
    if (switches == 0) throw std::runtime_error("client script without a switch");
    for (int s = 0; s < switches; ++s) {
        for (int count = in.u16(); count > 0; --count) {
            const std::int32_t key = in.i32();
            const std::int32_t jump = in.i32();
            if (s > 0) continue;
            // A LinkedHashMap's put: a repeated key keeps its place and takes
            // the later jump.
            const auto known = std::find_if(script.firstSwitch.begin(), script.firstSwitch.end(), [&](const auto& c) { return c.first == key; });
            if (known != script.firstSwitch.end()) {
                known->second = jump;
            } else {
                script.firstSwitch.emplace_back(key, jump);
            }
        }
    }

    // Opcodes as RuneLite's script/Opcodes names them.
    constexpr int kSconst = 3;
    constexpr int kReturn = 21;
    constexpr int kPopInt = 38;
    constexpr int kPopObject = 39;
    constexpr int kLconst = 61;
    constexpr int kPushNull = 63;
    script.intOperands.assign(static_cast<std::size_t>(instructionCount), 0);
    // An instruction may run on into the trailer, as RuneLite reads it.
    Buffer code(bytes);
    if (code.u8() != 0) code.string(); // the rest of the script's name
    for (std::size_t i = 0; code.position() < end; ++i) {
        const int opcode = code.u16();
        if (i >= script.intOperands.size()) throw std::runtime_error("client script with more instructions than it counts");
        std::int32_t& operand = script.intOperands[i];
        switch (opcode) {
        case kSconst: code.string(); break;
        case kReturn:
        case kPopInt:
        case kPopObject:
        case kPushNull: operand = code.u8(); break;
        case kLconst: code.i64(); break;
        default: operand = opcode < 100 ? code.i32() : code.u8(); break;
        }
    }
    return script;
}

} // namespace

WorldPoint WorldPoint::unpack(std::int32_t packed) {
    if (packed == -1) return {-1, -1, -1};
    return {packed >> 14 & 16383, packed & 16383, packed >> 28 & 3};
}

WorldPoint WorldMapElementDefinition::worldPosition() const {
    if (!offset) return {position.x, position.y, 0};
    return {position.x + offset->x, position.y + offset->y, offset->plane};
}

std::optional<WorldPoint> WorldMapCompositeDefinition::worldOffset(WorldPoint displayed) const {
    // Java's division and masking, which agree with C++'s for -1, the only
    // negative a position holds.
    const int squareX = displayed.x / 64;
    const int squareZ = displayed.y / 64;
    const int zoneX = (displayed.x & 63) / 8;
    const int zoneZ = (displayed.y & 63) / 8;
    std::optional<WorldPoint> offset;
    // At most one of each matches, as no two are shown at the same place.
    for (const MapSquareDefinition& square : mapSquares) {
        if (square.displaySquareX == squareX && square.displaySquareZ == squareZ) {
            offset = WorldPoint{(square.sourceSquareX - square.displaySquareX) * 64, (square.sourceSquareZ - square.displaySquareZ) * 64,
                                square.minLevel};
        }
    }
    for (const ZoneDefinition& zone : zones) {
        if (zone.displaySquareX == squareX && zone.displaySquareZ == squareZ && zone.displayZoneX == zoneX && zone.displayZoneZ == zoneZ) {
            offset = WorldPoint{(zone.sourceSquareX - zone.displaySquareX) * 64 + (zone.sourceZoneX - zone.displayZoneX) * 8,
                                (zone.sourceSquareZ - zone.displaySquareZ) * 64 + (zone.sourceZoneZ - zone.displayZoneZ) * 8, zone.minLevel};
        }
    }
    return offset;
}

// RuneLite WorldMapCompositeLoader.load, configured by
// configureForRevision.
WorldMapCompositeDefinition decodeWorldMapComposite(std::span<const std::uint8_t> bytes, std::int32_t worldMapRevision) {
    const bool rev238 = worldMapRevision > 425;
    Buffer in(bytes);
    WorldMapCompositeDefinition composite;

    // insert, not emplace(x, z): MSVC 19.51 at /O2 /Ob2 keeps the duplicates
    // emplace should drop here.
    std::set<std::pair<int, int>> squaresShown;
    for (int count = in.u16(); count > 0; --count) {
        const MapSquareDefinition square = decodeMapSquare(in, rev238);
        if (squaresShown.insert({square.displaySquareX, square.displaySquareZ}).second) composite.mapSquares.push_back(square);
    }
    std::set<std::array<int, 4>> zonesShown;
    for (int count = in.u16(); count > 0; --count) {
        const ZoneDefinition zone = decodeZone(in, rev238);
        if (zonesShown.insert({zone.displaySquareX, zone.displaySquareZ, zone.displayZoneX, zone.displayZoneZ}).second) {
            composite.zones.push_back(zone);
        }
    }
    // RuneLite WorldMapElementLoader.load.
    for (int count = in.u16(); count > 0; --count) {
        WorldMapElementDefinition& element = composite.elements.emplace_back();
        element.areaDefinitionId = in.bigSmartOrMinusOne();
        element.position = WorldPoint::unpack(in.i32());
        element.membersOnly = in.u8() == 1;
    }
    for (WorldMapElementDefinition& element : composite.elements) element.offset = composite.worldOffset(element.position);
    return composite;
}

std::vector<WorldMapDefinition> loadWorldMapDefinitions(const Store& store) {
    std::vector<WorldMapDefinition> maps;
    for (const auto& [file, bytes] : requireFiles(store, worldMapGroup(store, "details", kDetailsGroup), "details")) {
        try {
            maps.push_back(decodeWorldMap(bytes, file));
        } catch (const std::runtime_error& error) {
            undecodable(store, "world map", file, error);
        }
    }
    return maps;
}

std::vector<WorldMapCompositeDefinition> loadWorldMapComposites(const Store& store) {
    const std::int32_t revision = store.index(Index::WorldMap)->revision;
    std::vector<WorldMapCompositeDefinition> composites;
    for (const auto& [file, bytes] : requireFiles(store, worldMapGroup(store, "compositemap", kCompositeGroup), "composite maps")) {
        try {
            composites.push_back(decodeWorldMapComposite(bytes, revision));
            composites.back().fileId = file;
        } catch (const std::runtime_error& error) {
            undecodable(store, "composite map", file, error);
        }
    }
    return composites;
}

std::vector<WorldMapElementDefinition> loadWorldMapElements(const Store& store) {
    std::vector<WorldMapElementDefinition> elements;
    for (const WorldMapCompositeDefinition& composite : loadWorldMapComposites(store)) {
        elements.insert(elements.end(), composite.elements.begin(), composite.elements.end());
    }
    return elements;
}

// RuneLite WorldMapManager.load's intermap links.
std::map<WorldPoint, WorldPoint> loadIntermapLinks(const Store& store) {
    const IndexInfo* scripts = store.index(Index::ClientScripts);
    const std::optional<std::map<int, std::vector<std::uint8_t>>> files = store.files(Index::ClientScripts, kIntermapLinkScript);
    if (scripts == nullptr || !files || !files->contains(0)) throw std::runtime_error("the cache lacks the intermap links' client script");
    // Skipped, the script leaves no links.
    return decodeOrSkip(store, "intermap link script", kIntermapLinkScript, [&] {
        ScriptOperands script = decodeScriptOperands(files->at(0), scripts->revision);
        std::vector<WorldPoint> ends;
        for (std::size_t i = 1; i < script.intOperands.size(); ++i) {
            if (script.intOperands[i] == kIntermapJumpScript) ends.push_back(WorldPoint::unpack(script.intOperands[i - 1]));
        }
        // The cases in the order their code runs in, which is the order of
        // the ends.
        std::stable_sort(script.firstSwitch.begin(), script.firstSwitch.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        if (script.firstSwitch.size() > ends.size()) throw std::runtime_error("more starts than ends");
        std::map<WorldPoint, WorldPoint> links;
        for (std::size_t i = 0; i < script.firstSwitch.size(); ++i) links.insert_or_assign(WorldPoint::unpack(script.firstSwitch[i].first), ends[i]);
        return links;
    }).value_or(std::map<WorldPoint, WorldPoint>{});
}

} // namespace osrs::cache
