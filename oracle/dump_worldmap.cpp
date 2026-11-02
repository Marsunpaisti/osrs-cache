// The world map of osrs/cache/worldmap.h, as RuneLite's WorldMapDefinition,
// WorldMapCompositeDefinition and WorldMapManager hold it.

#include "lines.h"

#include <osrs/cache/worldmap.h>

#include <algorithm>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace osrs::cache::oracle {

namespace {

void point(Lines& out, std::string_view path, const WorldPoint& p) {
    out.integer(field(path, "x"), p.x);
    out.integer(field(path, "y"), p.y);
    out.integer(field(path, "z"), p.plane);
}

void section(Lines& out, std::string_view path, const WorldMapSection& section) {
    const auto put = [&](std::string_view name, int value) { out.integer(field(path, name), value); };
    std::visit(
        [&](const auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, WorldMapType0>) {
                put("plane", s.plane);
                put("numberOfPlanes", s.numberOfPlanes);
                put("xLow", s.xLow);
                put("chunk_xLow", s.chunkXLow);
                put("yLow", s.yLow);
                put("chunk_yLow", s.chunkYLow);
                put("xHigh", s.xHigh);
                put("chunk_xHigh", s.chunkXHigh);
                put("yHigh", s.yHigh);
                put("chunk_yHigh", s.chunkYHigh);
            } else if constexpr (std::is_same_v<T, WorldMapType1>) {
                put("plane", s.plane);
                put("numberOfPlanes", s.numberOfPlanes);
                put("xLowerLeft", s.xLowerLeft);
                put("yLowerLeft", s.yLowerLeft);
                put("xLowerRight", s.xLowerRight);
                put("yUpperLeft", s.yUpperLeft);
                put("xUpperLeft", s.xUpperLeft);
                put("yLowerRight", s.yLowerRight);
                put("xUpperRight", s.xUpperRight);
                put("yUpperRight", s.yUpperRight);
            } else if constexpr (std::is_same_v<T, WorldMapType2>) {
                put("plane", s.plane);
                put("numberOfPlanes", s.numberOfPlanes);
                put("xLow", s.xLow);
                put("yLow", s.yLow);
                put("xHigh", s.xHigh);
                put("yHigh", s.yHigh);
            } else {
                put("oldPlane", s.oldPlane);
                put("numberOfPlanes", s.numberOfPlanes);
                put("oldX", s.oldX);
                put("chunk_oldXLow", s.chunkOldXLow);
                put("chunk_oldXHigh", s.chunkOldXHigh);
                put("oldY", s.oldY);
                put("chunk_oldYLow", s.chunkOldYLow);
                put("chunk_oldYHigh", s.chunkOldYHigh);
                put("newX", s.newX);
                put("chunk_newXLow", s.chunkNewXLow);
                put("chunk_newXHigh", s.chunkNewXHigh);
                put("newY", s.newY);
                put("chunk_newYLow", s.chunkNewYLow);
                put("chunk_newYHigh", s.chunkNewYHigh);
            }
        },
        section);
}

void worldMaps(const Store& store, Lines& out) {
    for (const WorldMapDefinition& map : loadWorldMapDefinitions(store)) {
        out.object({map.fileId});
        out.integer("fileId", map.fileId);
        out.string("safeName", map.safeName);
        out.string("name", map.name);
        point(out, "position", map.position);
        out.integer("emptyTileColor", map.emptyTileColor);
        out.integer("backgroundColor", map.backgroundColor);
        out.boolean("isSurface", map.isSurface);
        out.integer("defaultZoom", map.defaultZoom);
        for (std::size_t i = 0; i < map.sections.size(); ++i) section(out, at("regionList", i), map.sections[i]);
    }
}

// The fields MapSquareDefinition and ZoneDefinition share.
template <class Data>
void source(Lines& out, const Data& data) {
    out.integer("minLevel", data.minLevel);
    out.integer("levels", data.levels);
    out.integer("sourceSquareX", data.sourceSquareX);
    out.integer("sourceSquareZ", data.sourceSquareZ);
    out.integer("displaySquareX", data.displaySquareX);
    out.integer("displaySquareZ", data.displaySquareZ);
    out.integer("groupId", data.groupId);
    out.integer("fileId", data.fileId);
}

void element(Lines& out, std::string_view path, const WorldMapElementDefinition& element) {
    point(out, field(path, "position"), element.position);
    if (element.offset) point(out, field(path, "offset"), *element.offset);
    out.integer(field(path, "areaDefinitionId"), element.areaDefinitionId);
    out.boolean(field(path, "membersOnly"), element.membersOnly);
}

void composites(const Store& store, Lines& out) {
    for (const WorldMapCompositeDefinition& composite : loadWorldMapComposites(store)) {
        out.object({composite.fileId});
        for (const MapSquareDefinition& square : composite.mapSquares) {
            out.setElement("mapSquareDefinitions", [&](Lines& element) { source(element, square); });
        }
        for (const ZoneDefinition& zone : composite.zones) {
            out.setElement("zoneDefinitions", [&](Lines& element) {
                source(element, zone);
                element.integer("sourceZoneX", zone.sourceZoneX);
                element.integer("sourceZoneZ", zone.sourceZoneZ);
                element.integer("displayZoneX", zone.displayZoneX);
                element.integer("displayZoneZ", zone.displayZoneZ);
            });
        }
        for (std::size_t i = 0; i < composite.elements.size(); ++i) {
            element(out, at("worldMapElementDefinitions", i), composite.elements[i]);
        }
    }
}

void elements(const Store& store, Lines& out) {
    const std::vector<WorldMapElementDefinition> all = loadWorldMapElements(store);
    for (std::size_t i = 0; i < all.size(); ++i) {
        out.object({static_cast<std::int64_t>(i)});
        element(out, "", all[i]);
    }
}

void intermapLinks(const Store& store, Lines& out) {
    const std::map<WorldPoint, WorldPoint> links = loadIntermapLinks(store);
    std::vector<std::pair<WorldPoint, WorldPoint>> byStart(links.begin(), links.end());
    // Keyed by the start's x, y and plane, the oracle's order for them.
    std::sort(byStart.begin(), byStart.end(), [](const auto& a, const auto& b) {
        return std::tie(a.first.x, a.first.y, a.first.plane) < std::tie(b.first.x, b.first.y, b.first.plane);
    });
    for (const auto& [start, end] : byStart) {
        out.object({start.x, start.y, start.plane});
        point(out, "", end);
    }
}

const Type worldmap("worldmap", worldMaps);
const Type worldmapcomposite("worldmapcomposite", composites);
const Type worldmapelement("worldmapelement", elements);
const Type intermaplink("intermaplink", intermapLinks);

} // namespace

} // namespace osrs::cache::oracle
