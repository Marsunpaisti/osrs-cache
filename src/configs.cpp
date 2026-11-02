// Ports RuneLite's EnumLoader, ParamLoader, VarbitLoader, StructLoader,
// InventoryLoader, AreaLoader, DBRowLoader, DBTableLoader,
// DBTableIndexLoader and GameValLoader (definitions/loaders), with
// util/ScriptVarType and util/BaseVarType.

#include <osrs/cache/configs.h>

#include "buffer.h"
#include "undecodable.h"
#include "java_utf8.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace osrs::cache {

namespace {

constexpr std::array<ScriptVarTypeInfo, 33> kScriptVarTypes = {{
    {0, 'i', "INTEGER", "integer"},
    {1, '1', "BOOLEAN", "boolean"},
    {6, 'A', "SEQ", "seq"},
    {7, 'C', "COLOUR", "colour"},
    {8, 'H', "LOC_SHAPE", "locshape"},
    {9, 'I', "COMPONENT", "component"},
    {10, 'K', "IDKIT", "idkit"},
    {11, 'M', "MIDI", "midi"},
    {13, 'O', "NAMEDOBJ", "namedobj"},
    {14, 'P', "SYNTH", "synth"},
    {17, 'S', "STAT", "stat"},
    {22, 'c', "COORDGRID", "coordgrid"},
    {23, 'd', "GRAPHIC", "graphic"},
    {25, 'f', "FONTMETRICS", "fontmetrics"},
    {26, 'g', "ENUM", "enum"},
    {28, 'j', "JINGLE", "jingle"},
    {30, 'l', "LOC", "loc"},
    {31, 'm', "MODEL", "model"},
    {32, 'n', "NPC", "npc"},
    {33, 'o', "OBJ", "obj"},
    {36, 's', "STRING", "string"},
    {37, 't', "SPOTANIM", "spotanim"},
    {39, 'v', "INV", "inv"},
    {40, 'x', "TEXTURE", "texture"},
    {41, 'y', "CATEGORY", "category"},
    {42, 'z', "CHAR", "char"},
    // RuneLite writes these as '£', 'µ', '×', 'Ð' and 'Ø'.
    {55, 0xA3, "MAPSCENEICON", "mapsceneicon"},
    {59, 0xB5, "MAPELEMENT", "mapelement"},
    {62, 0xD7, "HITMARK", "hitmark"},
    {73, 'J', "STRUCT", "struct"},
    {74, 0xD0, "DBROW", "dbrow"},
    {118, 0xD8, "DBTABLE", "dbtable"},
    {209, '7', "VARP", "varp"},
}};
static_assert(kScriptVarTypes.size() == static_cast<std::size_t>(ScriptVarType::Varp) + 1, "one entry per ScriptVarType, in its order");

using Files = std::map<int, std::vector<std::uint8_t>>;

Files configFiles(const Store& store, ConfigGroup group, const char* kind) {
    std::optional<Files> files = store.files(Index::Configs, static_cast<int>(group));
    if (!files) throw std::runtime_error(std::string("the cache has no ") + kind + " definitions");
    return std::move(*files);
}

// Decodes each file by `decode(definition, opcode, buffer)` until opcode 0.
template <class Definition, class Decode>
std::vector<Definition> decodeAll(const Store& store, const Files& files, const char* kind, Decode&& decode) {
    std::vector<Definition> definitions;
    definitions.reserve(files.size());
    for (const auto& [id, bytes] : files) {
        Definition& definition = definitions.emplace_back();
        definition.id = id;
        Buffer in(bytes);
        try {
            for (int opcode = in.u8(); opcode != 0; opcode = in.u8()) decode(definition, opcode, in);
        } catch (const std::runtime_error& error) {
            definitions.pop_back();
            undecodable(store, kind, id, error);
        }
    }
    return definitions;
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

// RuneLite io/InputStream.readVarInt2: 7 bits a byte, least significant
// first, the top bit set on every byte but the last. Java masks a shift to
// 5 bits, so a sixth byte's bits land low again.
std::int32_t readVarInt2(Buffer& in) {
    std::uint32_t value = 0;
    int bits = 0;
    int read = 0;
    do {
        read = in.u8();
        value |= static_cast<std::uint32_t>(read & 0x7F) << (bits & 31);
        bits += 7;
    } while (read > 127);
    return static_cast<std::int32_t>(value);
}

// A count Java sizes an array or a collection by, which throws when negative.
std::int32_t readVarInt2Count(Buffer& in) {
    const std::int32_t count = readVarInt2(in);
    if (count < 0) throw std::runtime_error("negative count " + std::to_string(count));
    return count;
}

// RuneLite EnumLoader.processOp.
void decodeEnum(EnumDefinition& definition, int opcode, Buffer& in) {
    switch (opcode) {
    case 1: definition.keyType = scriptVarTypeForKeyChar(in.u8()); break;
    case 2: definition.valueType = scriptVarTypeForKeyChar(in.u8()); break;
    case 3: definition.defaultString = in.string(); break;
    case 4: definition.defaultInt = in.i32(); break;
    case 5: case 6: case 7: {
        const int size = in.u16();
        std::vector<std::int32_t> keys(size);
        std::vector<std::string> strings;
        std::vector<std::int32_t> ints;
        std::vector<std::int64_t> longs;
        for (int i = 0; i < size; ++i) {
            keys[i] = in.i32();
            if (opcode == 5) {
                strings.push_back(in.string());
            } else if (opcode == 6) {
                ints.push_back(in.i32());
            } else {
                longs.push_back(in.i64());
            }
        }
        definition.keys = std::move(keys);
        if (opcode == 5) definition.stringValues = std::move(strings);
        if (opcode == 6) definition.intValues = std::move(ints);
        if (opcode == 7) definition.longValues = std::move(longs);
        break;
    }
    case 8: definition.defaultLong = in.i64(); break;
    default: break;
    }
}

// RuneLite ParamLoader.load.
void decodeParam(ParamDefinition& definition, int opcode, Buffer& in) {
    switch (opcode) {
    case 1: definition.type = scriptVarTypeForKeyChar(in.u8()); break;
    case 2: definition.defaultInt = in.i32(); break;
    case 4: definition.members = false; break;
    case 5: definition.defaultString = in.string(); break;
    case 7: definition.defaultLong = in.i64(); break;
    case 8: definition.type = scriptVarTypeForId(in.u8()); break;
    default: break;
    }
}

// RuneLite VarbitLoader.load.
void decodeVarbit(VarbitDefinition& definition, int opcode, Buffer& in) {
    if (opcode != 1) return;
    definition.varp = static_cast<std::uint16_t>(in.u16());
    definition.leastSignificantBit = static_cast<std::uint8_t>(in.u8());
    definition.mostSignificantBit = static_cast<std::uint8_t>(in.u8());
}

// RuneLite StructLoader.decodeValues.
void decodeStruct(StructDefinition& definition, int opcode, Buffer& in) {
    if (opcode == 249) definition.params = readParams(in);
}

// RuneLite InventoryLoader.load.
void decodeInventory(InventoryDefinition& definition, int opcode, Buffer& in) {
    if (opcode == 2) {
        definition.size = static_cast<std::uint16_t>(in.u16());
    } else if (opcode == 249) {
        definition.params = readParams(in);
    }
}

std::optional<std::int32_t> orNothing(int value) {
    if (value == -1) return std::nullopt;
    return value;
}

// RuneLite AreaLoader.processOpcode.
void decodeArea(AreaDefinition& definition, int opcode, Buffer& in) {
    switch (opcode) {
    case 1: definition.spriteId = orNothing(in.bigSmartOrMinusOne()); break;
    case 2: definition.field3294 = orNothing(in.bigSmartOrMinusOne()); break;
    case 3: definition.name = in.string(); break;
    case 4: definition.textColor = static_cast<std::uint32_t>(in.u24()); break;
    case 5: case 23: in.skip(3); break;
    case 6: definition.textScale = static_cast<std::uint8_t>(in.u8()); break;
    case 7: case 8: case 28: case 29: case 30: in.skip(1); break;
    case 10: case 11: case 12: case 13: case 14: definition.options[opcode - 10] = in.string(); break;
    case 15: {
        const int count = in.u8();
        definition.field3300.assign(2 * static_cast<std::size_t>(count), 0);
        for (std::int16_t& value : definition.field3300) value = static_cast<std::int16_t>(in.i16());
        in.skip(4);
        definition.field3292.assign(in.u8(), 0);
        for (std::int32_t& value : definition.field3292) value = in.i32();
        definition.field3309.assign(count, 0);
        for (std::int8_t& value : definition.field3309) value = static_cast<std::int8_t>(in.i8());
        break;
    }
    case 17: definition.field3308 = in.string(); break;
    case 18: case 25: in.bigSmartOrMinusOne(); break;
    case 19: definition.category = static_cast<std::uint16_t>(in.u16()); break;
    case 21: case 22: case 24: in.skip(4); break;
    default: break;
    }
}

// RuneLite DBRowLoader and DBTableLoader: a count, then each type's
// ScriptVarType id.
DbColumnTypes readColumnTypes(Buffer& in) {
    DbColumnTypes types(in.u8());
    for (std::optional<ScriptVarType>& type : types) type = scriptVarTypeForId(in.unsignedShortSmart());
    return types;
}

// RuneLite DBTableLoader.decodeColumnFields.
DbColumnValues readColumnValues(Buffer& in, const DbColumnTypes& types) {
    const int tuples = in.unsignedShortSmart();
    DbColumnValues values;
    values.reserve(static_cast<std::size_t>(tuples) * types.size());
    for (int tuple = 0; tuple < tuples; ++tuple) {
        for (const std::optional<ScriptVarType>& type : types) {
            if (type == ScriptVarType::String) {
                values.emplace_back(in.string());
            } else {
                values.emplace_back(in.i32());
            }
        }
    }
    return values;
}

// Java's ArrayIndexOutOfBoundsException, for a column id past the count the
// definition gave.
void checkColumn(int column, std::size_t columns) {
    if (static_cast<std::size_t>(column) >= columns) {
        throw std::runtime_error("column " + std::to_string(column) + " of " + std::to_string(columns));
    }
}

// RuneLite DBRowLoader.decode.
void decodeDbRow(DbRowDefinition& definition, int opcode, Buffer& in) {
    if (opcode == 3) {
        definition.columns.assign(in.u8(), std::nullopt);
        for (int column = in.u8(); column != 255; column = in.u8()) {
            DbColumnTypes types = readColumnTypes(in);
            checkColumn(column, definition.columns.size());
            DbColumnValues values = readColumnValues(in, types);
            definition.columns[column] = DbRowColumn{std::move(types), std::move(values)};
        }
    } else if (opcode == 4) {
        definition.tableId = readVarInt2(in);
    }
}

// RuneLite DBTableLoader.decode. A column given twice keeps its first
// defaults if the second gives none, as RuneLite keeps them.
void decodeDbTable(DbTableDefinition& definition, int opcode, Buffer& in) {
    if (opcode != 1) return;
    definition.columns.assign(in.u8(), std::nullopt);
    for (int setting = in.u8(); setting != 255; setting = in.u8()) {
        const int column = setting & 0x7F;
        DbColumnTypes types = readColumnTypes(in);
        checkColumn(column, definition.columns.size());
        std::optional<DbTableColumn>& entry = definition.columns[column];
        if (!entry) entry.emplace();
        entry->types = std::move(types);
        if ((setting & 0x80) != 0) entry->defaultValues = readColumnValues(in, entry->types);
    }
}

std::optional<BaseVarType> baseVarTypeForId(int id) {
    if (id < 0 || id > 2) return std::nullopt;
    return static_cast<BaseVarType>(id);
}

// RuneLite DBTableIndexLoader.decode.
DbTableIndex decodeDbTableIndex(std::int32_t table, std::int32_t column, const std::vector<std::uint8_t>& bytes) {
    DbTableIndex index;
    index.tableId = table;
    index.columnId = column;
    Buffer in(bytes);
    for (std::int32_t tuple = readVarInt2Count(in); tuple > 0; --tuple) {
        const std::optional<BaseVarType> type = baseVarTypeForId(in.u8());
        index.tupleTypes.push_back(type);
        std::map<DbIndexValue, std::vector<std::int32_t>>& rowsByValue = index.tupleIndexes.emplace_back();
        for (std::int32_t values = readVarInt2Count(in); values > 0; --values) {
            DbIndexValue value;
            if (type == BaseVarType::Integer) {
                value = in.i32();
            } else if (type == BaseVarType::Long) {
                value = in.i64();
            } else if (type == BaseVarType::String) {
                value = in.string();
            } else {
                // Java's switch on the null an unknown id gives throws.
                throw std::runtime_error("a value of no BaseVarType");
            }
            std::vector<std::int32_t> rows;
            for (std::int32_t count = readVarInt2Count(in); count > 0; --count) rows.push_back(readVarInt2(in));
            rowsByValue[std::move(value)] = std::move(rows);
        }
    }
    return index;
}

// RuneLite GameValLoader.load.
GameValDefinition decodeGameVal(GameValGroup group, std::int32_t id, const std::vector<std::uint8_t>& bytes) {
    GameValDefinition value;
    value.group = group;
    value.id = id;
    Buffer in(bytes);
    switch (group) {
    case GameValGroup::DbTables:
        in.u8();
        value.name = in.string();
        // Each column's name after a byte that is anything but 0, which ends them.
        for (std::int32_t column = 0; in.u8() != 0; ++column) value.files[column] = in.string();
        break;
    case GameValGroup::InterfacesLegacy: {
        value.name = in.string();
        // Each component's name after the low byte of its id. The last 0xFF
        // ends them; an earlier 0xFF is a component's low byte, after which
        // the ids go on 256 higher.
        const auto last = std::find(bytes.rbegin(), bytes.rend(), std::uint8_t{0xFF});
        if (last == bytes.rend()) throw std::runtime_error("no 0xFF ends the components");
        const std::size_t end = static_cast<std::size_t>(last.base() - bytes.begin());
        for (std::int32_t high = 0;;) {
            const int low = in.u8();
            if (low == 0xFF && in.position() == end) break;
            value.files[high | low] = in.string();
            if (low == 0xFF) high += 0x100;
        }
        break;
    }
    case GameValGroup::Interfaces:
        value.name = in.string();
        for (int component = in.u16(); component != 0xFFFF; component = in.u16()) value.files[component] = in.string();
        break;
    default: value.name = decodeUtf8AsJava(bytes); break;
    }
    return value;
}

} // namespace

const ScriptVarTypeInfo& scriptVarTypeInfo(ScriptVarType type) { return kScriptVarTypes[static_cast<std::size_t>(type)]; }

std::optional<ScriptVarType> scriptVarTypeForId(int id) {
    for (std::size_t i = 0; i < kScriptVarTypes.size(); ++i) {
        if (kScriptVarTypes[i].id == id) return static_cast<ScriptVarType>(i);
    }
    return std::nullopt;
}

std::optional<ScriptVarType> scriptVarTypeForKeyChar(int keyChar) {
    for (std::size_t i = 0; i < kScriptVarTypes.size(); ++i) {
        if (kScriptVarTypes[i].keyChar == keyChar) return static_cast<ScriptVarType>(i);
    }
    return std::nullopt;
}

std::string_view baseVarTypeName(BaseVarType type) {
    switch (type) {
    case BaseVarType::Integer: return "INTEGER";
    case BaseVarType::Long: return "LONG";
    case BaseVarType::String: return "STRING";
    }
    return {};
}

std::vector<EnumDefinition> loadEnumDefinitions(const Store& store) {
    Files files = configFiles(store, ConfigGroup::Enums, "enum");
    std::erase_if(files, [](const auto& file) { return file.second.size() == 1 && file.second[0] == 0; });
    return decodeAll<EnumDefinition>(store, files, "enum", decodeEnum);
}

std::vector<ParamDefinition> loadParamDefinitions(const Store& store) {
    return decodeAll<ParamDefinition>(store, configFiles(store, ConfigGroup::Params, "param"), "param", decodeParam);
}

std::vector<VarbitDefinition> loadVarbitDefinitions(const Store& store) {
    return decodeAll<VarbitDefinition>(store, configFiles(store, ConfigGroup::Varbits, "varbit"), "varbit", decodeVarbit);
}

std::vector<StructDefinition> loadStructDefinitions(const Store& store) {
    return decodeAll<StructDefinition>(store, configFiles(store, ConfigGroup::Structs, "struct"), "struct", decodeStruct);
}

std::vector<InventoryDefinition> loadInventoryDefinitions(const Store& store) {
    return decodeAll<InventoryDefinition>(store, configFiles(store, ConfigGroup::Inventories, "inventory"), "inventory", decodeInventory);
}

std::vector<AreaDefinition> loadAreaDefinitions(const Store& store) {
    return decodeAll<AreaDefinition>(store, configFiles(store, ConfigGroup::Areas, "area"), "area", decodeArea);
}

std::vector<DbRowDefinition> loadDbRowDefinitions(const Store& store) {
    const std::optional<Files> files = store.files(Index::Configs, static_cast<int>(ConfigGroup::DbRows));
    if (!files) return {};
    return decodeAll<DbRowDefinition>(store, *files, "DB row", decodeDbRow);
}

std::vector<DbTableDefinition> loadDbTableDefinitions(const Store& store) {
    const std::optional<Files> files = store.files(Index::Configs, static_cast<int>(ConfigGroup::DbTables));
    if (!files) return {};
    return decodeAll<DbTableDefinition>(store, *files, "DB table", decodeDbTable);
}

std::map<std::pair<std::int32_t, std::int32_t>, DbTableIndex> loadDbTableIndices(const Store& store) {
    std::map<std::pair<std::int32_t, std::int32_t>, DbTableIndex> indices;
    for (const int table : store.groups(Index::DbTableIndices)) {
        const std::optional<Files> files = store.files(Index::DbTableIndices, table);
        if (!files) continue;
        // One left out is reported with the id (table << 16) + file.
        for (const auto& [file, bytes] : *files) {
            try {
                indices.emplace(std::pair{table, file - 1}, decodeDbTableIndex(table, file - 1, bytes));
            } catch (const std::runtime_error& error) {
                undecodable(store, "DB table index", (std::int64_t{table} << 16) + file, error);
            }
        }
    }
    return indices;
}

std::vector<GameValDefinition> loadGameValDefinitions(const Store& store, GameValGroup group) {
    const std::optional<Files> files = store.files(Index::GameVals, static_cast<int>(group));
    if (!files) return {};
    std::vector<GameValDefinition> values;
    values.reserve(files->size());
    // One left out is reported with the id (group << 16) + id.
    for (const auto& [id, bytes] : *files) {
        try {
            values.push_back(decodeGameVal(group, id, bytes));
        } catch (const std::runtime_error& error) {
            undecodable(store, "game value", (std::int64_t{static_cast<int>(group)} << 16) + id, error);
        }
    }
    return values;
}

} // namespace osrs::cache
