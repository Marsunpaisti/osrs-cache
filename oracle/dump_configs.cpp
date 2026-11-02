// The small config types, the DB tables and the game values of
// osrs/cache/configs.h, as RuneLite's definitions hold them.

#include "lines.h"

#include <osrs/cache/configs.h>

namespace osrs::cache::oracle {

namespace {

void scriptVarType(Lines& out, std::string_view path, const std::optional<ScriptVarType>& type) {
    if (type) out.enumeration(path, scriptVarTypeInfo(*type).name);
}

// A Map<Integer, Object> of Integer, Long and String values.
void params(Lines& out, const Params& values) {
    for (const auto& [id, value] : values) {
        const std::string path = entry("params", std::to_string(id));
        if (const auto* number = std::get_if<std::int64_t>(&value)) {
            out.integer(path, *number);
        } else {
            out.string(path, std::get<std::string>(value));
        }
    }
}

// A ScriptVarType[][] and an Object[][] of Integer and String values, by
// column id.
void columnTypes(Lines& out, std::string_view path, std::size_t column, const DbColumnTypes& types) {
    for (std::size_t i = 0; i < types.size(); ++i) scriptVarType(out, at(at(path, column), i), types[i]);
}

void columnValues(Lines& out, std::string_view path, std::size_t column, const DbColumnValues& values) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        const std::string element = at(at(path, column), i);
        if (const auto* number = std::get_if<std::int32_t>(&values[i])) {
            out.integer(element, *number);
        } else {
            out.string(element, std::get<std::string>(values[i]));
        }
    }
}

void enums(const Store& store, Lines& out) {
    for (const EnumDefinition& definition : loadEnumDefinitions(store)) {
        out.object({definition.id});
        out.integer("id", definition.id);
        scriptVarType(out, "keyType", definition.keyType);
        scriptVarType(out, "valType", definition.valueType);
        out.string("defaultString", definition.defaultString);
        out.integer("defaultInt", definition.defaultInt);
        out.integer("defaultLong", definition.defaultLong);
        out.integer("size", static_cast<std::int64_t>(definition.keys.size()));
        // RuneLite sets its keys with whichever values the entries came as.
        if (definition.stringValues || definition.intValues || definition.longValues) out.integers("keys", definition.keys);
        if (definition.intValues) out.integers("intVals", *definition.intValues);
        if (definition.longValues) out.integers("longVals", *definition.longValues);
        if (definition.stringValues) {
            for (std::size_t i = 0; i < definition.stringValues->size(); ++i) out.string(at("stringVals", i), (*definition.stringValues)[i]);
        }
    }
}

void paramDefinitions(const Store& store, Lines& out) {
    for (const ParamDefinition& definition : loadParamDefinitions(store)) {
        out.object({definition.id});
        scriptVarType(out, "type", definition.type);
        out.boolean("isMembers", definition.members);
        out.integer("defaultInt", definition.defaultInt);
        if (definition.defaultString) out.string("defaultString", *definition.defaultString);
        out.integer("defaultLong", definition.defaultLong);
    }
}

void varbits(const Store& store, Lines& out) {
    for (const VarbitDefinition& definition : loadVarbitDefinitions(store)) {
        out.object({definition.id});
        out.integer("id", definition.id);
        out.integer("index", definition.varp);
        out.integer("leastSignificantBit", definition.leastSignificantBit);
        out.integer("mostSignificantBit", definition.mostSignificantBit);
    }
}

void structs(const Store& store, Lines& out) {
    for (const StructDefinition& definition : loadStructDefinitions(store)) {
        out.object({definition.id});
        out.integer("id", definition.id);
        params(out, definition.params);
    }
}

void inventories(const Store& store, Lines& out) {
    for (const InventoryDefinition& definition : loadInventoryDefinitions(store)) {
        out.object({definition.id});
        out.integer("id", definition.id);
        out.integer("size", definition.size);
        params(out, definition.params);
    }
}

void areas(const Store& store, Lines& out) {
    for (const AreaDefinition& definition : loadAreaDefinitions(store)) {
        out.object({definition.id});
        out.integer("id", definition.id);
        out.integer("spriteId", definition.spriteId.value_or(-1));
        out.integer("field3294", definition.field3294.value_or(-1));
        if (definition.name) out.string("name", *definition.name);
        out.integer("textColor", static_cast<std::int32_t>(definition.textColor));
        out.integer("textScale", definition.textScale);
        out.integer("category", definition.category ? *definition.category : -1);
        for (std::size_t i = 0; i < definition.options.size(); ++i) {
            if (definition.options[i]) out.string(at("field3298", i), *definition.options[i]);
        }
        // RuneLite leaves the three null without opcode 15.
        if (!definition.field3300.empty() || !definition.field3292.empty()) {
            out.integers("field3300", definition.field3300);
            out.integers("field3292", definition.field3292);
            out.integers("field3309", definition.field3309);
        }
        if (definition.field3308) out.string("field3308", *definition.field3308);
    }
}

void dbRows(const Store& store, Lines& out) {
    for (const DbRowDefinition& definition : loadDbRowDefinitions(store)) {
        out.object({definition.id});
        out.integer("id", definition.id);
        out.integer("tableId", definition.tableId);
        for (std::size_t column = 0; column < definition.columns.size(); ++column) {
            if (!definition.columns[column]) continue;
            columnTypes(out, "columnTypes", column, definition.columns[column]->types);
            columnValues(out, "columnValues", column, definition.columns[column]->values);
        }
    }
}

void dbTables(const Store& store, Lines& out) {
    for (const DbTableDefinition& definition : loadDbTableDefinitions(store)) {
        out.object({definition.id});
        out.integer("id", definition.id);
        for (std::size_t column = 0; column < definition.columns.size(); ++column) {
            const std::optional<DbTableColumn>& entry = definition.columns[column];
            if (!entry) continue;
            columnTypes(out, "types", column, entry->types);
            if (entry->defaultValues) columnValues(out, "defaultColumnValues", column, *entry->defaultValues);
        }
    }
}

void dbTableIndices(const Store& store, Lines& out) {
    // RuneLite keys each by its group and file, the column id plus one.
    for (const auto& [key, index] : loadDbTableIndices(store)) {
        out.object({index.tableId, index.columnId + 1});
        if (out.skipping()) continue;
        out.integer("tableId", index.tableId);
        out.integer("columnId", index.columnId);
        for (std::size_t tuple = 0; tuple < index.tupleTypes.size(); ++tuple) {
            if (index.tupleTypes[tuple]) out.enumeration(at("tupleTypes", tuple), baseVarTypeName(*index.tupleTypes[tuple]));
            for (const auto& [value, rows] : index.tupleIndexes[tuple]) {
                std::string text;
                if (const auto* string = std::get_if<std::string>(&value)) {
                    text = Lines::quote(*string);
                } else if (const auto* integer = std::get_if<std::int32_t>(&value)) {
                    text = std::to_string(*integer);
                } else {
                    text = std::to_string(std::get<std::int64_t>(value));
                }
                const std::string path = entry(at("tupleIndexes", tuple), text);
                for (std::size_t row = 0; row < rows.size(); ++row) out.integer(at(path, row), rows[row]);
            }
        }
    }
}

void gameVals(const Store& store, Lines& out) {
    for (const int group : store.groups(Index::GameVals)) {
        for (const GameValDefinition& value : loadGameValDefinitions(store, static_cast<GameValGroup>(group))) {
            out.object({group, value.id});
            out.integer("gameValId", group);
            out.integer("id", value.id);
            out.string("name", value.name);
            for (const auto& [id, name] : value.files) out.string(entry("files", std::to_string(id)), name);
        }
    }
}

const Type enumType("enum", enums);
const Type paramType("param", paramDefinitions);
const Type varbitType("varbit", varbits);
const Type structType("struct", structs);
const Type inventoryType("inventory", inventories);
const Type areaType("area", areas);
const Type dbRowType("dbrow", dbRows);
const Type dbTableType("dbtable", dbTables);
const Type dbTableIndexType("dbtableindex", dbTableIndices);
const Type gameValType("gameval", gameVals);

} // namespace

} // namespace osrs::cache::oracle
