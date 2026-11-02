#pragma once

// The small config types (enums, params, varbits, structs, inventories and
// world map areas), the game's database (DB tables, their rows and their
// column indices) and the names Jagex gives game values, decoded as
// RuneLite's loaders decode them (net.runelite:cache 1.13.5,
// definitions/loaders), keeping every field RuneLite's definitions hold.
//
// Unlike definitions.h, an opcode a decoder doesn't know is skipped without
// reading anything after it, as RuneLite's loaders for these types skip it;
// a definition then fails only if its data ends early.

#include <osrs/cache/definitions.h>
#include <osrs/cache/store.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace osrs::cache {

// The type of a script value, as enums, params and DB columns declare it
// (RuneLite util/ScriptVarType). Its constants are RuneLite's, in its order.
enum class ScriptVarType : std::uint8_t {
    Integer,
    Boolean,
    Seq,
    Colour,
    LocShape,
    Component,
    Idkit,
    Midi,
    NamedObj,
    Synth,
    Stat,
    Coordgrid,
    Graphic,
    FontMetrics,
    Enum,
    Jingle,
    Loc,
    Model,
    Npc,
    Obj,
    String,
    SpotAnim,
    Inv,
    Texture,
    Category,
    Char,
    MapSceneIcon,
    MapElement,
    Hitmark,
    Struct,
    DbRow,
    DbTable,
    Varp,
};

struct ScriptVarTypeInfo {
    // The id DB columns and params (opcode 8) declare the type by.
    int id;
    // The character enums and params (opcode 1) declare the type by, as a
    // byte of ISO-8859-1.
    std::uint8_t keyChar;
    // RuneLite's constant, "INTEGER", and its full name, "integer".
    std::string_view name;
    std::string_view fullName;
};

const ScriptVarTypeInfo& scriptVarTypeInfo(ScriptVarType type);
// Nothing for an id or character RuneLite's ScriptVarType doesn't know.
std::optional<ScriptVarType> scriptVarTypeForId(int id);
std::optional<ScriptVarType> scriptVarTypeForKeyChar(int keyChar);

// The type of a DB column index's values (RuneLite util/BaseVarType), by its
// id.
enum class BaseVarType : std::uint8_t {
    Integer = 0,
    Long = 1,
    String = 2,
};

// RuneLite's constant: "INTEGER", "LONG" or "STRING".
std::string_view baseVarTypeName(BaseVarType type);

// A table from keys to values, which scripts look values up in.
struct EnumDefinition {
    std::int32_t id = 0;
    // Nothing when the enum doesn't say, or names a type RuneLite doesn't
    // know.
    std::optional<ScriptVarType> keyType;
    std::optional<ScriptVarType> valueType;
    // The values of a key the enum lacks.
    std::string defaultString = "null";
    std::int32_t defaultInt = 0;
    std::int64_t defaultLong = 0;
    // The entries: each key's value is at its index in the values vector the
    // enum gave its entries as, which is as long as `keys`. A values vector
    // is present only if the entries were given in its kind, which may be
    // with no entries at all.
    std::vector<std::int32_t> keys;
    std::optional<std::vector<std::string>> stringValues;
    std::optional<std::vector<std::int32_t>> intValues;
    std::optional<std::vector<std::int64_t>> longValues;
};

// What a param holds, and its value for a definition that doesn't set it.
// RuneLite's ParamDefinition has no id; `id` is the param's file id.
struct ParamDefinition {
    std::int32_t id = 0;
    std::optional<ScriptVarType> type;
    bool members = true;
    std::int32_t defaultInt = 0;
    std::optional<std::string> defaultString;
    std::int64_t defaultLong = 0;
};

// Bits leastSignificantBit to mostSignificantBit, inclusive, of a varp.
struct VarbitDefinition {
    std::int32_t id = 0;
    // RuneLite's index.
    std::uint16_t varp = 0;
    std::uint8_t leastSignificantBit = 0;
    std::uint8_t mostSignificantBit = 0;
};

// A bundle of params, which enums and DB rows refer to.
struct StructDefinition {
    std::int32_t id = 0;
    Params params;
};

struct InventoryDefinition {
    std::int32_t id = 0;
    // Its slots.
    std::uint16_t size = 0;
    Params params;
};

// An element the world map draws, as a map icon with its label. The fields
// whose meaning isn't known keep RuneLite's names.
struct AreaDefinition {
    std::int32_t id = 0;
    std::optional<std::int32_t> spriteId;
    std::optional<std::int32_t> field3294;
    std::optional<std::string> name;
    // 0xRRGGBB.
    std::uint32_t textColor = 0;
    std::uint8_t textScale = 0;
    std::optional<std::uint16_t> category;
    // Opcodes 10 to 14, RuneLite's field3298.
    MenuOptions options;
    // Opcode 15: two shorts and a byte for each of a count, and a count of
    // ints. Empty without it, as with counts of 0.
    std::vector<std::int16_t> field3300;
    std::vector<std::int32_t> field3292;
    std::vector<std::int8_t> field3309;
    std::optional<std::string> field3308;
};

// A DB column's declared type: the types of each of its tuples' values, in
// order; nothing for an id RuneLite's ScriptVarType doesn't know.
using DbColumnTypes = std::vector<std::optional<ScriptVarType>>;

// A DB column's values: its tuples one after another, each as many values
// as its DbColumnTypes has types. A value whose type is String is a string;
// any other is read as a 4-byte integer, as RuneLite reads it.
using DbColumnValues = std::vector<std::variant<std::int32_t, std::string>>;

struct DbRowColumn {
    DbColumnTypes types;
    DbColumnValues values;
};

// A row of a DB table.
struct DbRowDefinition {
    std::int32_t id = 0;
    std::int32_t tableId = 0;
    // By column id, as many as the row says the table has; nothing for a
    // column the row doesn't set.
    std::vector<std::optional<DbRowColumn>> columns;
};

struct DbTableColumn {
    DbColumnTypes types;
    // The values a row that doesn't set the column has; nothing if the
    // table gives none.
    std::optional<DbColumnValues> defaultValues;
};

// A DB table: its columns' types and defaults.
struct DbTableDefinition {
    std::int32_t id = 0;
    // By column id; nothing for an id the table doesn't declare.
    std::vector<std::optional<DbTableColumn>> columns;
};

// A value a DB column index looks rows up by, of BaseVarType Integer, Long
// or String.
using DbIndexValue = std::variant<std::int32_t, std::int64_t, std::string>;

// The rows of a DB table by the values of one of its columns (RuneLite
// DBTableIndex), for each value of the column's tuples.
struct DbTableIndex {
    std::int32_t tableId = 0;
    // -1 for the table's master index, which holds all its rows.
    std::int32_t columnId = 0;
    // By position in a tuple: the values' type, and the rows that hold each
    // value there. The type is nothing for an id RuneLite's BaseVarType
    // doesn't know, which decodes only when no value follows it.
    std::vector<std::optional<BaseVarType>> tupleTypes;
    std::vector<std::map<DbIndexValue, std::vector<std::int32_t>>> tupleIndexes;
};

// The groups of the game values index (RuneLite GameValLoader's constants).
enum class GameValGroup : std::uint8_t {
    Items = 0,
    Npcs = 1,
    Inventories = 2,
    Varps = 3,
    Varbits = 4,
    Objects = 6,
    Animations = 7,
    SpotAnims = 8,
    DbRows = 9,
    DbTables = 10,
    Jingles = 11,
    Sprites = 12,
    InterfacesLegacy = 13,
    Interfaces = 14,
    Varcs = 15,
};

// Jagex's name for one game value, as "abyssal_whip" for item 4151.
struct GameValDefinition {
    GameValGroup group = GameValGroup::Items;
    // The value's id: an item id, an NPC id, a DB table id.
    std::int32_t id = 0;
    std::string name;
    // A DB table's columns' names by column id; an interface's components'
    // names by component id. Empty for the other groups.
    std::map<std::int32_t, std::string> files;
};

// Every definition of a kind, by ascending id. Throw std::runtime_error if
// the cache lacks the group, or if a definition doesn't decode unless the
// store skips such objects (store.h). An enum whose
// data is only its end opcode is left out, as RuneLite's EnumLoader leaves it.
std::vector<EnumDefinition> loadEnumDefinitions(const Store& store);
std::vector<ParamDefinition> loadParamDefinitions(const Store& store);
std::vector<VarbitDefinition> loadVarbitDefinitions(const Store& store);
std::vector<StructDefinition> loadStructDefinitions(const Store& store);
std::vector<InventoryDefinition> loadInventoryDefinitions(const Store& store);
std::vector<AreaDefinition> loadAreaDefinitions(const Store& store);
// As RuneLite's DBRowManager and DBTableManager, none if the cache lacks the
// group.
std::vector<DbRowDefinition> loadDbRowDefinitions(const Store& store);
std::vector<DbTableDefinition> loadDbTableDefinitions(const Store& store);

// Every DB table index by table id and column id, the column -1 for a
// table's master index, as RuneLite's DBTableIndexManager indexes them (it
// keys each by its group and file, file 0 the master index and file n
// column n - 1). None if the cache lacks the index. Throws
// std::runtime_error if one doesn't decode, unless the store skips such
// objects (store.h), which reports it by (table << 16) + file.
std::map<std::pair<std::int32_t, std::int32_t>, DbTableIndex> loadDbTableIndices(const Store& store);

// The group's game values, by ascending id; none if the cache lacks the
// group. Throws std::runtime_error if one doesn't decode, unless the store
// skips such objects (store.h), which reports it by (group << 16) + id.
std::vector<GameValDefinition> loadGameValDefinitions(const Store& store, GameValGroup group);

} // namespace osrs::cache
