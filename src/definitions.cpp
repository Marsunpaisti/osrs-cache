#include <osrs/cache/definitions.h>

#include "buffer.h"
#include "undecodable.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace osrs::cache {

namespace {

// The archive revisions RuneLite's loaders switch formats at
// (NpcLoader.REV_210_NPC_ARCHIVE_REV, ObjectLoader.REV_220_OBJ_ARCHIVE_REV,
// SequenceLoader.REV_220_SEQ_ARCHIVE_REV and REV_226_SEQ_ARCHIVE_REV).
constexpr std::int32_t kNpcRev210 = 1493;
constexpr std::int32_t kObjectRev220 = 1673;
constexpr std::int32_t kSequenceRev220 = 1141;
constexpr std::int32_t kSequenceRev226 = 1268;

// As Java's equalsIgnoreCase for the ASCII letters the cache's option names use.
bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

[[noreturn]] void unknownOpcode(int opcode) { throw std::runtime_error("unknown opcode " + std::to_string(opcode)); }

// The definition `decodeOp(definition, opcode, buffer)` builds from each
// opcode until opcode 0.
template <class Definition, class DecodeOp>
Definition decodeOpcodes(std::int32_t id, std::span<const std::uint8_t> bytes, DecodeOp&& decodeOp) {
    Definition definition;
    definition.id = id;
    Buffer in(bytes);
    for (int opcode = in.u8(); opcode != 0; opcode = in.u8()) decodeOp(definition, opcode, in);
    return definition;
}

// Each element of the config group, decoded by `decode(id, bytes)`.
template <class Decode>
auto loadAll(const Store& store, ConfigGroup group, const char* kind, Decode&& decode) {
    using Definition = std::invoke_result_t<Decode&, std::int32_t, std::span<const std::uint8_t>>;
    const auto files = store.files(Index::Configs, static_cast<int>(group));
    if (!files) throw std::runtime_error(std::string("the cache has no ") + kind + " definitions");
    std::vector<Definition> definitions;
    definitions.reserve(files->size());
    for (const auto& [id, bytes] : *files) {
        try {
            definitions.push_back(decode(id, bytes));
        } catch (const std::runtime_error& error) {
            undecodable(store, kind, id, error);
        }
    }
    return definitions;
}

// The revision of the config group, which RuneLite's managers configure its
// loaders by (Archive.getRevision).
std::int32_t groupRevision(const Store& store, ConfigGroup group) {
    const IndexInfo* configs = store.index(Index::Configs);
    if (configs == nullptr) return 0;
    const auto found = configs->groups.find(static_cast<int>(group));
    return found != configs->groups.end() ? found->second.revision : 0;
}

// RuneLite io/InputStream.readParams.
Params readParams(Buffer& in) {
    Params params;
    for (int count = in.u8(); count > 0; --count) {
        const int type = in.u8();
        const std::int32_t id = in.u24();
        if (type == 1) {
            params[id] = in.string();
        } else if (type == 2) {
            params[id] = in.i64();
        } else {
            params[id] = std::int64_t{in.i32()};
        }
    }
    return params;
}

// RuneLite io/InputStream.readSigned24BitInt.
std::int32_t signed24(Buffer& in) {
    const std::int32_t value = in.u24();
    return value > 0x7FFFFF ? value - 0x1000000 : value;
}

// A u16, 0xFFFF being none.
std::optional<std::int32_t> optionalU16(Buffer& in) {
    const int value = in.u16();
    return value == 0xFFFF ? std::nullopt : std::optional<std::int32_t>(value);
}

// RuneLite's readBigSmart2, -1 being none.
std::optional<std::int32_t> optionalBigSmart(Buffer& in) {
    const int value = in.bigSmartOrMinusOne();
    return value == -1 ? std::nullopt : std::optional<std::int32_t>(value);
}

// RuneLite EntityOpsLoader and EntityOpsDefinition: opcodes shared by
// objects, items and NPCs. An option named "Hidden" leaves the one the
// definition had.
void decodeOp(EntityOps& ops, std::size_t index, Buffer& in) {
    std::string text = in.string();
    if (equalsIgnoringCase(text, "Hidden")) return;
    if (ops.ops.size() <= index) ops.ops.resize(index + 1);
    ops.ops[index] = std::move(text);
}

void decodeSubOp(EntityOps& ops, Buffer& in) {
    const std::size_t index = static_cast<std::size_t>(in.u8());
    EntityOps::SubOp subOp;
    subOp.subId = static_cast<std::uint16_t>(in.u8());
    subOp.text = in.string();
    if (ops.subOps.size() <= index) ops.subOps.resize(index + 1);
    ops.subOps[index].push_back(std::move(subOp));
}

// The condition and text after a conditional op's index and sub id.
EntityOps::ConditionalOp readConditionalOp(Buffer& in) {
    EntityOps::ConditionalOp op;
    op.varp = static_cast<std::uint16_t>(in.u16());
    op.varbit = static_cast<std::uint16_t>(in.u16());
    op.min = in.i32();
    op.max = in.i32();
    op.text = in.string();
    return op;
}

void decodeConditionalOp(EntityOps& ops, Buffer& in) {
    const std::size_t index = static_cast<std::size_t>(in.u8());
    EntityOps::ConditionalOp op = readConditionalOp(in);
    if (ops.conditionalOps.size() <= index) ops.conditionalOps.resize(index + 1);
    ops.conditionalOps[index].push_back(std::move(op));
}

void decodeConditionalSubOp(EntityOps& ops, Buffer& in) {
    const std::size_t index = static_cast<std::size_t>(in.u8());
    EntityOps::ConditionalSubOp subOp;
    subOp.subId = static_cast<std::uint16_t>(in.u16());
    subOp.op = readConditionalOp(in);
    if (ops.conditionalSubOps.size() <= index) ops.conditionalSubOps.resize(index + 1);
    ops.conditionalSubOps[index][subOp.subId].push_back(std::move(subOp));
}

// What an NPC or object turns into (objects' opcodes 77 and 92, NPCs' 106
// and 118): a varbit and a varp, a default for the second form, then the
// definitions by value.
void readTransforms(Buffer& in, bool withDefault, std::optional<std::int32_t>& varbit, std::optional<std::int32_t>& varp,
                    std::vector<std::int32_t>& transforms) {
    varbit = optionalU16(in);
    varp = optionalU16(in);
    const std::int32_t fallback = withDefault ? optionalU16(in).value_or(-1) : -1;
    transforms.resize(static_cast<std::size_t>(in.u8()) + 2);
    for (std::size_t i = 0; i + 1 < transforms.size(); ++i) transforms[i] = optionalU16(in).value_or(-1);
    transforms.back() = fallback;
}

// Opcodes 40 and 41 of every definition with models: a count, then each find
// and its replace.
Swaps readSwaps(Buffer& in) {
    Swaps swaps;
    const int count = in.u8();
    swaps.find.resize(count);
    swaps.replace.resize(count);
    for (int i = 0; i < count; ++i) {
        swaps.find[i] = static_cast<std::uint16_t>(in.u16());
        swaps.replace[i] = static_cast<std::uint16_t>(in.u16());
    }
    return swaps;
}

// A count, then each model id: 2 bytes each in the older opcodes, 4 in the
// newer.
std::vector<std::int32_t> readModels(Buffer& in, bool wide) {
    std::vector<std::int32_t> models(in.u8());
    for (std::int32_t& model : models) model = wide ? in.i32() : in.u16();
    return models;
}

// RuneLite ObjectLoader.processOp and post.
ObjectDefinition decodeObject(std::int32_t id, std::span<const std::uint8_t> bytes, std::int32_t revision) {
    const bool rev220SoundData = revision >= kObjectRev220;
    std::optional<std::uint8_t> wallOrDoor;
    std::optional<std::uint8_t> supportsItems;
    ObjectDefinition object = decodeOpcodes<ObjectDefinition>(id, bytes, [&](ObjectDefinition& object, int opcode, Buffer& in) {
        switch (opcode) {
        case 1: case 6: {
            // Each model with its shape; none leaves the models as they were.
            const int count = in.u8();
            if (count == 0) break;
            object.models.resize(count);
            object.modelShapes.resize(count);
            for (int i = 0; i < count; ++i) {
                object.models[i] = opcode == 6 ? in.i32() : in.u16();
                object.modelShapes[i] = static_cast<std::uint8_t>(in.u8());
            }
            break;
        }
        case 2: object.name = in.string(); break;
        case 5: case 7: {
            std::vector<std::int32_t> models = readModels(in, opcode == 7);
            if (models.empty()) break;
            object.models = std::move(models);
            object.modelShapes.clear();
            break;
        }
        case 14: object.sizeX = static_cast<std::uint8_t>(in.u8()); break;
        case 15: object.sizeY = static_cast<std::uint8_t>(in.u8()); break;
        case 17:
            object.interactType = 0;
            object.blocksProjectile = false;
            break;
        case 18: object.blocksProjectile = false; break;
        case 19: wallOrDoor = static_cast<std::uint8_t>(in.u8()); break;
        case 21: object.contouredGround = 0; break;
        case 22: object.mergeNormals = true; break;
        case 23: object.modelClipped = true; break;
        case 24: object.animation = optionalU16(in); break;
        case 27: object.interactType = 1; break;
        case 28: object.decorDisplacement = static_cast<std::uint8_t>(in.u8()); break;
        case 29: object.ambient = static_cast<std::int8_t>(in.i8()); break;
        case 30: case 31: case 32: case 33: case 34: decodeOp(object.ops, opcode - 30, in); break;
        case 39: object.contrast = static_cast<std::int16_t>(in.i8() * 25); break;
        case 40: object.recolours = readSwaps(in); break;
        case 41: object.retextures = readSwaps(in); break;
        case 42: object.fullRecolor = static_cast<std::uint16_t>(in.u16()); break;
        case 61: object.category = static_cast<std::uint16_t>(in.u16()); break;
        case 62: object.mirrored = true; break;
        case 64: object.shadow = false; break;
        case 65: object.modelSizeX = static_cast<std::uint16_t>(in.u16()); break;
        case 66: object.modelSizeHeight = static_cast<std::uint16_t>(in.u16()); break;
        case 67: object.modelSizeY = static_cast<std::uint16_t>(in.u16()); break;
        case 68: object.mapSceneId = in.u16(); break;
        case 69: object.blockingMask = static_cast<std::int8_t>(in.i8()); break;
        case 70: object.offsetX = static_cast<std::int16_t>(in.i16()); break;
        case 71: object.offsetHeight = static_cast<std::int16_t>(in.i16()); break;
        case 72: object.offsetY = static_cast<std::int16_t>(in.i16()); break;
        case 73: object.obstructsGround = true; break;
        case 74: object.hollow = true; break;
        case 75: supportsItems = static_cast<std::uint8_t>(in.u8()); break;
        case 77: case 92:
            readTransforms(in, opcode == 92, object.transformVarbit, object.transformVarp, object.transforms);
            break;
        case 78:
            object.ambientSoundId = in.u16();
            object.ambientSoundDistance = static_cast<std::uint8_t>(in.u8());
            if (rev220SoundData) object.ambientSoundRetain = static_cast<std::uint8_t>(in.u8());
            break;
        case 79:
            object.ambientSoundChangeTicksMin = static_cast<std::uint16_t>(in.u16());
            object.ambientSoundChangeTicksMax = static_cast<std::uint16_t>(in.u16());
            object.ambientSoundDistance = static_cast<std::uint8_t>(in.u8());
            if (rev220SoundData) object.ambientSoundRetain = static_cast<std::uint8_t>(in.u8());
            object.ambientSoundIds.resize(in.u8());
            for (std::uint16_t& sound : object.ambientSoundIds) sound = static_cast<std::uint16_t>(in.u16());
            break;
        case 81: object.contouredGround = in.u8() * 256; break;
        case 82: object.mapAreaId = in.u16(); break;
        case 89: object.randomizeAnimStart = true; break;
        case 90: object.deferAnimChange = true; break;
        case 91: object.soundDistanceFadeCurve = static_cast<std::uint8_t>(in.u8()); break;
        case 93:
            object.soundFadeInCurve = static_cast<std::uint8_t>(in.u8());
            object.soundFadeInDuration = static_cast<std::uint16_t>(in.u16());
            object.soundFadeOutCurve = static_cast<std::uint8_t>(in.u8());
            object.soundFadeOutDuration = static_cast<std::uint16_t>(in.u16());
            break;
        case 94: object.unknown1 = true; break;
        case 95: object.soundVisibility = static_cast<std::uint8_t>(in.u8()); break;
        case 96: object.raise = static_cast<std::uint8_t>(in.u8()); break;
        case 97: case 98: {
            // As 77 and 92, with signed 24-bit definitions that keep 0xFFFF.
            object.transformVarbit = optionalU16(in);
            object.transformVarp = optionalU16(in);
            const std::int32_t fallback = opcode == 98 ? signed24(in) : -1;
            object.transforms.resize(static_cast<std::size_t>(in.u8()) + 2);
            for (std::size_t i = 0; i + 1 < object.transforms.size(); ++i) object.transforms[i] = signed24(in);
            object.transforms.back() = fallback;
            break;
        }
        case 100: decodeSubOp(object.ops, in); break;
        case 101: decodeConditionalOp(object.ops, in); break;
        case 102: decodeConditionalSubOp(object.ops, in); break;
        case 249: object.params = readParams(in); break;
        default: unknownOpcode(opcode);
        }
    });
    if (wallOrDoor) {
        object.wallOrDoor = *wallOrDoor;
    } else {
        const bool shape10 = !object.models.empty() && (object.modelShapes.empty() || object.modelShapes[0] == 10);
        const bool anyOp = std::any_of(object.ops.ops.begin(), object.ops.ops.end(), [](const auto& op) { return op.has_value(); });
        object.wallOrDoor = shape10 || anyOp ? 1 : 0;
    }
    object.supportsItems = supportsItems.value_or(object.interactType != 0 ? 1 : 0);
    return object;
}

// RuneLite ItemLoader.decodeValues and post.
ItemDefinition decodeItem(std::int32_t id, std::span<const std::uint8_t> bytes) {
    ItemDefinition item = decodeOpcodes<ItemDefinition>(id, bytes, [](ItemDefinition& item, int opcode, Buffer& in) {
        switch (opcode) {
        case 1: item.inventoryModel = in.u16(); break;
        case 2: item.name = in.string(); break;
        case 3: item.examine = in.string(); break;
        case 4: item.zoom2d = static_cast<std::uint16_t>(in.u16()); break;
        case 5: item.xan2d = static_cast<std::uint16_t>(in.u16()); break;
        case 6: item.yan2d = static_cast<std::uint16_t>(in.u16()); break;
        case 7: item.xOffset2d = static_cast<std::int16_t>(in.i16()); break;
        case 8: item.yOffset2d = static_cast<std::int16_t>(in.i16()); break;
        case 9: item.unknown1 = in.string(); break;
        case 11: item.stackable = 1; break;
        case 12: item.cost = in.i32(); break;
        case 13: item.wearPos1 = static_cast<std::int8_t>(in.i8()); break;
        case 14: item.wearPos2 = static_cast<std::int8_t>(in.i8()); break;
        case 15: item.tradeable = false; break;
        case 16: item.members = true; break;
        case 23:
            item.maleModels[0] = in.u16();
            item.maleOffset = static_cast<std::uint8_t>(in.u8());
            break;
        case 24: item.maleModels[1] = in.u16(); break;
        case 25:
            item.femaleModels[0] = in.u16();
            item.femaleOffset = static_cast<std::uint8_t>(in.u8());
            break;
        case 26: item.femaleModels[1] = in.u16(); break;
        case 27: item.wearPos3 = static_cast<std::int8_t>(in.i8()); break;
        case 30: case 31: case 32: case 33: case 34: decodeOp(item.groundOps, opcode - 30, in); break;
        case 35: case 36: case 37: case 38: case 39: item.inventoryOptions[opcode - 35] = in.string(); break;
        case 40: item.recolours = readSwaps(in); break;
        case 41: item.retextures = readSwaps(in); break;
        case 42: item.shiftClickDropIndex = static_cast<std::int8_t>(in.i8()); break;
        case 43: {
            // An option, then its sub-options until a 0, each by its index
            // plus 1; RuneLite drops those outside its five by twenty.
            const int option = in.u8();
            const bool valid = option < 5;
            if (valid && item.inventorySubOptions[option].empty()) item.inventorySubOptions[option].resize(20);
            for (int subOption = in.u8() - 1; subOption != -1; subOption = in.u8() - 1) {
                std::string text = in.string();
                if (valid && subOption < 20) item.inventorySubOptions[option][subOption] = std::move(text);
            }
            break;
        }
        case 44: item.inventoryModel = in.i32(); break;
        case 45:
            item.maleModels[0] = in.i32();
            item.maleOffset = static_cast<std::uint8_t>(in.u8());
            break;
        case 46: item.maleModels[1] = in.i32(); break;
        case 47: item.maleModels[2] = in.i32(); break;
        case 48:
            item.femaleModels[0] = in.i32();
            item.femaleOffset = static_cast<std::uint8_t>(in.u8());
            break;
        case 49: item.femaleModels[1] = in.i32(); break;
        case 50: item.femaleModels[2] = in.i32(); break;
        case 51: item.maleHeadModels[0] = in.i32(); break;
        case 52: item.maleHeadModels[1] = in.i32(); break;
        case 53: item.femaleHeadModels[0] = in.i32(); break;
        case 54: item.femaleHeadModels[1] = in.i32(); break;
        case 65: item.geTradeable = true; break;
        case 75: item.weight = static_cast<std::int16_t>(in.i16()); break;
        case 78: item.maleModels[2] = in.u16(); break;
        case 79: item.femaleModels[2] = in.u16(); break;
        case 90: item.maleHeadModels[0] = in.u16(); break;
        case 91: item.femaleHeadModels[0] = in.u16(); break;
        case 92: item.maleHeadModels[1] = in.u16(); break;
        case 93: item.femaleHeadModels[1] = in.u16(); break;
        case 94: item.category = static_cast<std::uint16_t>(in.u16()); break;
        case 95: item.zan2d = static_cast<std::uint16_t>(in.u16()); break;
        case 97: item.notedId = in.u16(); break;
        case 98: item.notedTemplate = in.u16(); break;
        case 99: item.fullRecolor = static_cast<std::uint16_t>(in.u16()); break;
        case 100: case 101: case 102: case 103: case 104:
        case 105: case 106: case 107: case 108: case 109:
            if (item.countItems.empty()) {
                item.countItems.resize(10);
                item.countSizes.resize(10);
            }
            item.countItems[opcode - 100] = static_cast<std::uint16_t>(in.u16());
            item.countSizes[opcode - 100] = static_cast<std::uint16_t>(in.u16());
            break;
        case 110: item.resizeX = static_cast<std::uint16_t>(in.u16()); break;
        case 111: item.resizeY = static_cast<std::uint16_t>(in.u16()); break;
        case 112: item.resizeZ = static_cast<std::uint16_t>(in.u16()); break;
        case 113: item.ambient = static_cast<std::int8_t>(in.i8()); break;
        case 114: item.contrast = static_cast<std::int8_t>(in.i8()); break;
        case 115: item.team = static_cast<std::uint8_t>(in.u8()); break;
        case 139: item.boughtId = in.u16(); break;
        case 140: item.boughtTemplate = in.u16(); break;
        case 148: item.placeholderId = in.u16(); break;
        case 149: item.placeholderTemplate = in.u16(); break;
        case 160: item.stackable = 2; break;
        case 161:
            item.holdingWhitelist.resize(in.u16());
            for (std::uint16_t& held : item.holdingWhitelist) held = static_cast<std::uint16_t>(in.u16());
            break;
        case 200: decodeSubOp(item.groundOps, in); break;
        case 201: decodeConditionalOp(item.groundOps, in); break;
        case 202: decodeConditionalSubOp(item.groundOps, in); break;
        case 249: item.params = readParams(in); break;
        case 251: item.bronzeman = true; break;
        default: unknownOpcode(opcode);
        }
    });
    if (item.stackable == 1) item.weight = 0;
    return item;
}

// The look RuneLite's link methods copy from an item's template: its
// inventory model and how it's drawn.
void copyInventoryLook(ItemDefinition& item, const ItemDefinition& from) {
    item.inventoryModel = from.inventoryModel;
    item.zoom2d = from.zoom2d;
    item.xan2d = from.xan2d;
    item.yan2d = from.yan2d;
    item.zan2d = from.zan2d;
    item.xOffset2d = from.xOffset2d;
    item.yOffset2d = from.yOffset2d;
}

// RuneLite ItemDefinition.linkNote.
void linkNote(ItemDefinition& item, const ItemDefinition& notedTemplate, const ItemDefinition& unnoted) {
    copyInventoryLook(item, notedTemplate);
    item.recolours = notedTemplate.recolours;
    item.retextures = notedTemplate.retextures;
    item.name = unnoted.name;
    item.members = unnoted.members;
    item.cost = unnoted.cost;
    item.stackable = 1;
}

// RuneLite ItemDefinition.linkBought.
void linkBought(ItemDefinition& item, const ItemDefinition& boughtTemplate, const ItemDefinition& bought) {
    copyInventoryLook(item, boughtTemplate);
    item.recolours = bought.recolours;
    item.retextures = bought.retextures;
    item.name = bought.name;
    item.members = bought.members;
    item.stackable = bought.stackable;
    item.maleModels = bought.maleModels;
    item.femaleModels = bought.femaleModels;
    item.maleHeadModels = bought.maleHeadModels;
    item.femaleHeadModels = bought.femaleHeadModels;
    item.team = bought.team;
    item.groundOps = bought.groundOps;
    // Cleared first, as RuneLite replaces the array before copying, which
    // matters for an item bought as itself.
    item.inventoryOptions = MenuOptions{};
    for (std::size_t i = 0; i < 4; ++i) item.inventoryOptions[i] = bought.inventoryOptions[i];
    item.inventoryOptions[4] = "Discard";
    item.cost = 0;
}

// RuneLite ItemDefinition.linkPlaceholder.
void linkPlaceholder(ItemDefinition& item, const ItemDefinition& placeholderTemplate, const ItemDefinition& placeholder) {
    copyInventoryLook(item, placeholderTemplate);
    item.recolours = placeholderTemplate.recolours;
    item.retextures = placeholderTemplate.retextures;
    item.stackable = placeholderTemplate.stackable;
    item.name = placeholder.name;
    item.cost = 0;
    item.members = false;
    item.tradeable = false;
    item.geTradeable = false;
}

// RuneLite NpcLoader.decodeValues and post. Opcode 111 is decoded as
// RuneLite's rev233 decodes it, which configureForRevision leaves on.
NpcDefinition decodeNpc(std::int32_t id, std::span<const std::uint8_t> bytes, std::int32_t revision) {
    const bool rev210HeadIcons = revision >= kNpcRev210;
    std::optional<std::int32_t> footprintSize;
    NpcDefinition npc = decodeOpcodes<NpcDefinition>(id, bytes, [&](NpcDefinition& npc, int opcode, Buffer& in) {
        switch (opcode) {
        case 1: case 61: npc.models = readModels(in, opcode == 61); break;
        case 2: npc.name = in.string(); break;
        case 12: npc.size = static_cast<std::uint8_t>(in.u8()); break;
        case 13: npc.standAnimation = in.u16(); break;
        case 14: npc.walkAnimation = in.u16(); break;
        case 15: npc.idleRotateLeftAnimation = in.u16(); break;
        case 16: npc.idleRotateRightAnimation = in.u16(); break;
        case 17:
            npc.walkAnimation = in.u16();
            npc.rotate180Animation = in.u16();
            npc.rotateLeftAnimation = in.u16();
            npc.rotateRightAnimation = in.u16();
            break;
        case 18: npc.category = static_cast<std::uint16_t>(in.u16()); break;
        case 30: case 31: case 32: case 33: case 34: {
            const std::size_t index = static_cast<std::size_t>(opcode - 30);
            decodeOp(npc.ops, index, in);
            if (index < npc.ops.ops.size()) npc.options[index] = npc.ops.ops[index];
            break;
        }
        case 40: npc.recolours = readSwaps(in); break;
        case 41: npc.retextures = readSwaps(in); break;
        case 42: npc.fullRecolor = static_cast<std::uint16_t>(in.u16()); break;
        case 60: case 62: npc.chatheadModels = readModels(in, opcode == 62); break;
        case 74: case 75: case 76: case 77: case 78: case 79:
            npc.stats[opcode - 74] = static_cast<std::uint16_t>(in.u16());
            break;
        case 93: npc.minimapVisible = false; break;
        case 95: npc.combatLevel = in.u16(); break;
        case 97: npc.widthScale = static_cast<std::uint16_t>(in.u16()); break;
        case 98: npc.heightScale = static_cast<std::uint16_t>(in.u16()); break;
        case 99: npc.renderPriority = 1; break;
        case 100: npc.ambient = static_cast<std::int8_t>(in.i8()); break;
        case 101: npc.contrast = static_cast<std::int8_t>(in.i8()); break;
        case 102:
            if (!rev210HeadIcons) {
                // One sprite of RuneLite's defaultHeadIconArchive, which
                // nothing configures from -1.
                npc.headIconArchiveIds = {-1};
                npc.headIconSpriteIndex = {static_cast<std::int16_t>(in.u16())};
            } else {
                // A sprite group and frame for each set bit of the bitfield.
                const int bits = in.u8();
                npc.headIconArchiveIds.clear();
                npc.headIconSpriteIndex.clear();
                for (int bit = 0; bits >> bit != 0; ++bit) {
                    const bool set = (bits >> bit & 1) != 0;
                    npc.headIconArchiveIds.push_back(set ? in.bigSmartOrMinusOne() : -1);
                    npc.headIconSpriteIndex.push_back(set ? static_cast<std::int16_t>(in.unsignedShortSmartMinusOne()) : std::int16_t{-1});
                }
            }
            break;
        case 103: npc.rotationSpeed = static_cast<std::uint16_t>(in.u16()); break;
        case 106: case 118:
            readTransforms(in, opcode == 118, npc.transformVarbit, npc.transformVarp, npc.transforms);
            break;
        case 107: npc.interactable = false; break;
        case 109: npc.rotationFlag = false; break;
        case 111: npc.renderPriority = 2; break;
        case 114: npc.runAnimation = in.u16(); break;
        case 115:
            npc.runAnimation = in.u16();
            npc.runRotate180Animation = in.u16();
            npc.runRotateLeftAnimation = in.u16();
            npc.runRotateRightAnimation = in.u16();
            break;
        case 116: npc.crawlAnimation = in.u16(); break;
        case 117:
            npc.crawlAnimation = in.u16();
            npc.crawlRotate180Animation = in.u16();
            npc.crawlRotateLeftAnimation = in.u16();
            npc.crawlRotateRightAnimation = in.u16();
            break;
        case 122: npc.isFollower = true; break;
        case 123: npc.lowPriorityFollowerOps = true; break;
        case 124: npc.height = static_cast<std::uint16_t>(in.u16()); break;
        case 126: footprintSize = in.u16(); break;
        case 129: npc.unknown1 = true; break;
        case 130: npc.idleAnimRestart = true; break;
        case 145: npc.canHideForOverlap = true; break;
        case 146: npc.overlapTintHsl = static_cast<std::uint16_t>(in.u16()); break;
        case 147: npc.zbuf = false; break;
        case 249: npc.params = readParams(in); break;
        case 251: decodeSubOp(npc.ops, in); break;
        case 252: decodeConditionalOp(npc.ops, in); break;
        case 253: decodeConditionalSubOp(npc.ops, in); break;
        default: unknownOpcode(opcode);
        }
    });
    // In float, as RuneLite computes it.
    npc.footprintSize = footprintSize.value_or(static_cast<std::int32_t>(0.4F * static_cast<float>(npc.size * 128)));
    return npc;
}

// RuneLite SequenceLoader.readFrameSound.
std::optional<SequenceDefinition::Sound> readFrameSound(Buffer& in, bool rev220FrameSounds, bool rev226) {
    SequenceDefinition::Sound sound;
    int id = 0;
    int loops = 0;
    if (!rev220FrameSounds) {
        // The id, the loops and the location packed in 3 bytes.
        const int bits = in.u24();
        sound.location = static_cast<std::uint8_t>(bits & 15);
        id = bits >> 8;
        loops = bits >> 4 & 7;
    } else {
        id = in.u16();
        if (rev226) sound.weight = static_cast<std::uint8_t>(in.u8());
        loops = in.u8();
        sound.location = static_cast<std::uint8_t>(in.u8());
        sound.retain = static_cast<std::uint8_t>(in.u8());
    }
    if (id < 1 || loops < 1) return std::nullopt;
    sound.id = id;
    sound.loops = static_cast<std::uint8_t>(loops);
    return sound;
}

// RuneLite SequenceLoader.decodeValues, in the format configureForRevision
// picks for the revision. Before revision 226, opcode 13 held frame sounds
// by index and 14 to 16 what 13 to 15 hold after it, so opcode 16 then
// never reaches verticalOffset.
SequenceDefinition decodeSequence(std::int32_t id, std::span<const std::uint8_t> bytes, std::int32_t revision) {
    const bool rev220FrameSounds = revision > kSequenceRev220;
    const bool rev226 = revision > kSequenceRev226;
    const int animMayaIdOpcode = rev226 ? 13 : 14;
    const int frameSoundsOpcode = rev226 ? 14 : 15;
    const int animMayaRangeOpcode = rev226 ? 15 : 16;
    return decodeOpcodes<SequenceDefinition>(id, bytes, [&](SequenceDefinition& sequence, int opcode, Buffer& in) {
        if (opcode == 13 && !rev226) {
            const int count = in.u8();
            for (int i = 0; i < count; ++i) {
                sequence.frameSounds.push_back({static_cast<std::uint16_t>(i), readFrameSound(in, rev220FrameSounds, rev226)});
            }
            return;
        }
        if (opcode == animMayaIdOpcode) {
            sequence.animMayaId = in.i32();
            return;
        }
        if (opcode == frameSoundsOpcode) {
            for (int count = in.u16(); count > 0; --count) {
                const auto frame = static_cast<std::uint16_t>(in.u16());
                sequence.frameSounds.push_back({frame, readFrameSound(in, rev220FrameSounds, rev226)});
            }
            return;
        }
        if (opcode == animMayaRangeOpcode) {
            sequence.animMayaStart = static_cast<std::uint16_t>(in.u16());
            sequence.animMayaEnd = static_cast<std::uint16_t>(in.u16());
            return;
        }
        switch (opcode) {
        case 1: {
            const int frames = in.u16();
            sequence.frameLengths.resize(frames);
            for (std::uint16_t& length : sequence.frameLengths) length = static_cast<std::uint16_t>(in.u16());
            // Each frame id's low halves, then its high halves.
            sequence.frameIds.resize(frames);
            for (std::int32_t& frame : sequence.frameIds) frame = in.u16();
            for (std::int32_t& frame : sequence.frameIds) frame += in.u16() << 16;
            break;
        }
        case 2: sequence.frameStep = static_cast<std::uint16_t>(in.u16()); break;
        case 3: {
            const int count = in.u8();
            sequence.interleaveLeave.resize(static_cast<std::size_t>(count) + 1);
            for (int i = 0; i < count; ++i) sequence.interleaveLeave[i] = in.u8();
            sequence.interleaveLeave.back() = 9999999;
            break;
        }
        case 4: sequence.stretches = true; break;
        case 5: sequence.forcedPriority = static_cast<std::uint8_t>(in.u8()); break;
        case 6: sequence.leftHandItem = in.u16(); break;
        case 7: sequence.rightHandItem = in.u16(); break;
        case 8: sequence.maxLoops = static_cast<std::uint8_t>(in.u8()); break;
        case 9: sequence.precedenceAnimating = static_cast<std::uint8_t>(in.u8()); break;
        case 10: sequence.priority = static_cast<std::uint8_t>(in.u8()); break;
        case 11: sequence.replyMode = static_cast<std::uint8_t>(in.u8()); break;
        case 12: {
            const int frames = in.u8();
            sequence.chatFrameIds.resize(frames);
            for (std::int32_t& frame : sequence.chatFrameIds) frame = in.u16();
            for (std::int32_t& frame : sequence.chatFrameIds) frame += in.u16() << 16;
            break;
        }
        case 16: sequence.verticalOffset = static_cast<std::int8_t>(in.i8()); break;
        case 17:
            sequence.animMayaMasks.assign(256, false);
            for (int count = in.u8(); count > 0; --count) sequence.animMayaMasks[in.u8()] = true;
            break;
        case 18: sequence.debugName = in.string(); break;
        case 19: sequence.soundsCrossWorldView = true; break;
        default: unknownOpcode(opcode);
        }
    });
}

// RuneLite KitLoader.load.
KitDefinition decodeKit(std::int32_t id, std::span<const std::uint8_t> bytes) {
    return decodeOpcodes<KitDefinition>(id, bytes, [](KitDefinition& kit, int opcode, Buffer& in) {
        switch (opcode) {
        case 1: kit.bodyPart = static_cast<std::uint8_t>(in.u8()); break;
        case 2: case 5: kit.models = readModels(in, opcode == 5); break;
        case 3: kit.selectable = false; break;
        case 40: kit.recolours = readSwaps(in); break;
        case 41: kit.retextures = readSwaps(in); break;
        default: {
            // Chat head models: 2 bytes in 60 to 69, 4 in 70 to 79, into
            // RuneLite's five, which throws for an index past them.
            if (opcode < 60 || opcode >= 80) unknownOpcode(opcode);
            const std::size_t index = static_cast<std::size_t>(opcode % 10);
            const std::int32_t model = opcode < 70 ? in.u16() : in.i32();
            if (index >= kit.chatheadModels.size()) throw std::runtime_error("chat head model " + std::to_string(index) + " of 5");
            kit.chatheadModels[index] = model;
        }
        }
    });
}

// RuneLite SpotAnimLoader.decodeValues.
SpotAnimDefinition decodeSpotAnim(std::int32_t id, std::span<const std::uint8_t> bytes) {
    return decodeOpcodes<SpotAnimDefinition>(id, bytes, [](SpotAnimDefinition& spotAnim, int opcode, Buffer& in) {
        switch (opcode) {
        case 1: spotAnim.model = in.u16(); break;
        case 2: spotAnim.animation = in.u16(); break;
        case 3: spotAnim.model = in.i32(); break;
        case 4: spotAnim.resizeX = static_cast<std::uint16_t>(in.u16()); break;
        case 5: spotAnim.resizeY = static_cast<std::uint16_t>(in.u16()); break;
        case 6: spotAnim.rotation = static_cast<std::uint16_t>(in.u16()); break;
        case 7: spotAnim.ambient = static_cast<std::uint8_t>(in.u8()); break;
        case 8: spotAnim.contrast = static_cast<std::uint8_t>(in.u8()); break;
        case 9: spotAnim.debugName = in.string(); break;
        case 10: spotAnim.clickable = false; break;
        case 40: spotAnim.recolours = readSwaps(in); break;
        case 41: spotAnim.retextures = readSwaps(in); break;
        case 42: spotAnim.fullRecolor = static_cast<std::uint16_t>(in.u16()); break;
        default: unknownOpcode(opcode);
        }
    });
}

// RuneLite HitSplatLoader.load.
HitsplatDefinition decodeHitsplat(std::int32_t id, std::span<const std::uint8_t> bytes) {
    return decodeOpcodes<HitsplatDefinition>(id, bytes, [](HitsplatDefinition& hitsplat, int opcode, Buffer& in) {
        switch (opcode) {
        case 1: hitsplat.font = optionalBigSmart(in); break;
        case 2: hitsplat.textColour = static_cast<std::uint32_t>(in.u24()); break;
        case 3: hitsplat.leftSprite = optionalBigSmart(in); break;
        case 4: hitsplat.leftSprite2 = optionalBigSmart(in); break;
        case 5: hitsplat.backgroundSprite = optionalBigSmart(in); break;
        case 6: hitsplat.rightSprite = optionalBigSmart(in); break;
        case 7: hitsplat.scrollToOffsetX = static_cast<std::int16_t>(in.i16()); break;
        case 8:
            // RuneLite's readString2: a 0, then the string.
            if (in.u8() != 0) throw std::runtime_error("format string without its leading 0");
            hitsplat.format = in.string();
            break;
        case 9: hitsplat.displayCycles = static_cast<std::uint16_t>(in.u16()); break;
        case 10: hitsplat.scrollToOffsetY = static_cast<std::int16_t>(in.i16()); break;
        case 11: hitsplat.fadeStartCycle = 0; break;
        case 12: hitsplat.useDamage = static_cast<std::uint8_t>(in.u8()); break;
        case 13: hitsplat.textOffsetY = static_cast<std::int16_t>(in.i16()); break;
        case 14: hitsplat.fadeStartCycle = static_cast<std::uint16_t>(in.u16()); break;
        case 17: case 18:
            readTransforms(in, opcode == 18, hitsplat.transformVarbit, hitsplat.transformVarp, hitsplat.transforms);
            break;
        default: unknownOpcode(opcode);
        }
    });
}

// RuneLite HealthBarLoader.decodeValues.
HealthBarDefinition decodeHealthBar(std::int32_t id, std::span<const std::uint8_t> bytes) {
    return decodeOpcodes<HealthBarDefinition>(id, bytes, [](HealthBarDefinition& bar, int opcode, Buffer& in) {
        switch (opcode) {
        case 1: in.skip(2); break;
        case 2: bar.field3277 = static_cast<std::uint8_t>(in.u8()); break;
        case 3: bar.field3278 = static_cast<std::uint8_t>(in.u8()); break;
        case 4: bar.field3283 = 0; break;
        case 5: bar.field3275 = static_cast<std::uint16_t>(in.u16()); break;
        case 6: in.skip(1); break;
        case 7: bar.frontSprite = optionalBigSmart(in); break;
        case 8: bar.backSprite = optionalBigSmart(in); break;
        case 11: bar.field3283 = static_cast<std::uint16_t>(in.u16()); break;
        case 14: bar.healthScale = static_cast<std::uint8_t>(in.u8()); break;
        case 15: bar.padding = static_cast<std::uint8_t>(in.u8()); break;
        default: unknownOpcode(opcode);
        }
    });
}

// RuneLite UnderlayDefinition.calculateHsl and OverlayDefinition.calculateHsl,
// which differ only in how they scale the hue: the underlay's by its hue
// multiplier, returned in `hueMultiplier`.
Hsl calculateHsl(std::uint32_t rgb, int* hueMultiplier) {
    const double r = static_cast<double>(rgb >> 16 & 255) / 256.0;
    const double g = static_cast<double>(rgb >> 8 & 255) / 256.0;
    const double b = static_cast<double>(rgb & 255) / 256.0;
    const double min = std::min({r, g, b});
    const double max = std::max({r, g, b});
    double hue = 0.0;
    double saturation = 0.0;
    const double lightness = (max + min) / 2.0;
    if (min != max) {
        saturation = lightness < 0.5 ? (max - min) / (max + min) : (max - min) / (2.0 - max - min);
        if (r == max) {
            hue = (g - b) / (max - min);
        } else if (g == max) {
            hue = 2.0 + (b - r) / (max - min);
        } else {
            hue = 4.0 + (r - g) / (max - min);
        }
    }
    hue /= 6.0;
    Hsl hsl;
    hsl.saturation = std::clamp(static_cast<int>(saturation * 256.0), 0, 255);
    hsl.lightness = std::clamp(static_cast<int>(lightness * 256.0), 0, 255);
    if (hueMultiplier != nullptr) {
        *hueMultiplier = std::max(1, static_cast<int>(saturation * (lightness > 0.5 ? 1.0 - lightness : lightness) * 512.0));
        hsl.hue = static_cast<int>(static_cast<double>(*hueMultiplier) * hue);
    } else {
        hsl.hue = static_cast<int>(256.0 * hue);
    }
    return hsl;
}

// RuneLite UnderlayLoader.load.
UnderlayDefinition decodeUnderlay(std::int32_t id, std::span<const std::uint8_t> bytes) {
    UnderlayDefinition underlay = decodeOpcodes<UnderlayDefinition>(id, bytes, [](UnderlayDefinition& underlay, int opcode, Buffer& in) {
        if (opcode != 1) unknownOpcode(opcode);
        underlay.rgb = static_cast<std::uint32_t>(in.u24());
    });
    underlay.hsl = calculateHsl(underlay.rgb, &underlay.hueMultiplier);
    return underlay;
}

// RuneLite OverlayLoader.load.
OverlayDefinition decodeOverlay(std::int32_t id, std::span<const std::uint8_t> bytes) {
    OverlayDefinition overlay = decodeOpcodes<OverlayDefinition>(id, bytes, [](OverlayDefinition& overlay, int opcode, Buffer& in) {
        switch (opcode) {
        case 1: overlay.rgb = static_cast<std::uint32_t>(in.u24()); break;
        case 2: overlay.texture = static_cast<std::uint8_t>(in.u8()); break;
        case 5: overlay.hideUnderlay = false; break;
        case 7: overlay.secondaryRgb = static_cast<std::uint32_t>(in.u24()); break;
        default: unknownOpcode(opcode);
        }
    });
    overlay.hsl = calculateHsl(overlay.rgb, nullptr);
    if (overlay.secondaryRgb) overlay.secondaryHsl = calculateHsl(*overlay.secondaryRgb, nullptr);
    return overlay;
}

} // namespace

std::optional<std::int64_t> numberParam(const Params& params, std::int32_t id) {
    const auto found = params.find(id);
    if (found == params.end()) return std::nullopt;
    const auto* number = std::get_if<std::int64_t>(&found->second);
    return number != nullptr ? std::optional(*number) : std::nullopt;
}

std::vector<ObjectDefinition> loadObjectDefinitions(const Store& store) {
    const std::int32_t revision = groupRevision(store, ConfigGroup::Objects);
    return loadAll(store, ConfigGroup::Objects, "object",
                   [&](std::int32_t id, std::span<const std::uint8_t> bytes) { return decodeObject(id, bytes, revision); });
}

std::vector<ItemDefinition> loadItemDefinitions(const Store& store) {
    std::vector<ItemDefinition> items = loadAll(store, ConfigGroup::Items, "item", decodeItem);
    // RuneLite ItemManager.link, in its order: its HashMap of items iterates
    // by ascending id while the ids stay below its capacity, as the game's
    // do, and each item reads the items it's made from as linked so far. A
    // template or source that doesn't exist leaves the item as decoded, where
    // RuneLite would fail.
    const auto find = [&](const std::optional<std::int32_t>& id) -> const ItemDefinition* {
        if (!id) return nullptr;
        const auto found = std::lower_bound(items.begin(), items.end(), *id,
                                            [](const ItemDefinition& item, std::int32_t wanted) { return item.id < wanted; });
        return found != items.end() && found->id == *id ? &*found : nullptr;
    };
    for (ItemDefinition& item : items) {
        const ItemDefinition* notedTemplate = find(item.notedTemplate);
        const ItemDefinition* unnoted = find(item.notedId);
        if (notedTemplate != nullptr && unnoted != nullptr) linkNote(item, *notedTemplate, *unnoted);
        const ItemDefinition* boughtTemplate = find(item.boughtTemplate);
        const ItemDefinition* bought = find(item.boughtId);
        if (boughtTemplate != nullptr && bought != nullptr) linkBought(item, *boughtTemplate, *bought);
        const ItemDefinition* placeholderTemplate = find(item.placeholderTemplate);
        const ItemDefinition* placeholder = find(item.placeholderId);
        if (placeholderTemplate != nullptr && placeholder != nullptr) linkPlaceholder(item, *placeholderTemplate, *placeholder);
    }
    return items;
}

std::vector<NpcDefinition> loadNpcDefinitions(const Store& store) {
    const std::int32_t revision = groupRevision(store, ConfigGroup::Npcs);
    return loadAll(store, ConfigGroup::Npcs, "NPC",
                   [&](std::int32_t id, std::span<const std::uint8_t> bytes) { return decodeNpc(id, bytes, revision); });
}

std::vector<SequenceDefinition> loadSequenceDefinitions(const Store& store) {
    // RuneLite's SequenceLoader decodes the current format unless configured,
    // as a revision after both its switches.
    return loadAll(store, ConfigGroup::Sequences, "sequence", [](std::int32_t id, std::span<const std::uint8_t> bytes) {
        return decodeSequence(id, bytes, kSequenceRev226 + 1);
    });
}

std::vector<KitDefinition> loadKitDefinitions(const Store& store) {
    return loadAll(store, ConfigGroup::Kits, "kit", decodeKit);
}

std::vector<SpotAnimDefinition> loadSpotAnimDefinitions(const Store& store) {
    return loadAll(store, ConfigGroup::SpotAnims, "spot animation", decodeSpotAnim);
}

std::vector<UnderlayDefinition> loadUnderlayDefinitions(const Store& store) {
    return loadAll(store, ConfigGroup::Underlays, "underlay", decodeUnderlay);
}

std::vector<OverlayDefinition> loadOverlayDefinitions(const Store& store) {
    return loadAll(store, ConfigGroup::Overlays, "overlay", decodeOverlay);
}

std::vector<HitsplatDefinition> loadHitsplatDefinitions(const Store& store) {
    return loadAll(store, ConfigGroup::Hitsplats, "hitsplat", decodeHitsplat);
}

std::vector<HealthBarDefinition> loadHealthBarDefinitions(const Store& store) {
    return loadAll(store, ConfigGroup::HealthBars, "health bar", decodeHealthBar);
}

NpcDefinition decodeNpcDefinition(std::int32_t id, std::span<const std::uint8_t> bytes, std::int32_t revision) {
    return decodeNpc(id, bytes, revision);
}

ObjectDefinition decodeObjectDefinition(std::int32_t id, std::span<const std::uint8_t> bytes, std::int32_t revision) {
    return decodeObject(id, bytes, revision);
}

SequenceDefinition decodeSequenceDefinition(std::int32_t id, std::span<const std::uint8_t> bytes, std::int32_t revision) {
    return decodeSequence(id, bytes, revision);
}

std::uint16_t packHsl(Hsl hsl) {
    int saturation = hsl.saturation;
    for (const int above : {179, 192, 217, 243}) {
        if (hsl.lightness > above) saturation /= 2;
    }
    return static_cast<std::uint16_t>((hsl.hue / 4 << 10) + (saturation / 32 << 7) + hsl.lightness / 2);
}

} // namespace osrs::cache
