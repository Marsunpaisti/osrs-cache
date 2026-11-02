// The small config types, the DB tables and the game values, on the copy of
// the game cache OSRS_CACHE_DIR names (cache_tests.cpp says which), skipped
// when it is missing; and the UTF-8 decoding GameValLoader relies on.

#include <doctest/doctest.h>

#include <osrs/cache/configs.h>

#include "../src/java_utf8.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string_view>

using namespace osrs::cache;

namespace {

const Store* store() {
    static const std::unique_ptr<Store> opened = []() -> std::unique_ptr<Store> {
        std::error_code missing;
        if (!std::filesystem::exists(std::filesystem::path(OSRS_CACHE_DIR) / "main_file_cache.dat2", missing)) return nullptr;
        return std::make_unique<Store>(OSRS_CACHE_DIR);
    }();
    return opened.get();
}

std::string utf8(std::string_view bytes) {
    return decodeUtf8AsJava({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
}

} // namespace

TEST_CASE("a DB table's column indices find its rows, which leave out the columns it gives defaults for") {
    if (store() == nullptr) return;
    const std::vector<DbTableDefinition> tables = loadDbTableDefinitions(*store());
    const std::vector<DbRowDefinition> rows = loadDbRowDefinitions(*store());
    const auto indices = loadDbTableIndices(*store());

    // Table 0 holds the quests; Jagex names its columns.
    const std::vector<GameValDefinition> tableNames = loadGameValDefinitions(*store(), GameValGroup::DbTables);
    REQUIRE_FALSE(tableNames.empty());
    CHECK(tableNames[0].name == "quest");
    CHECK(tableNames[0].files.at(0) == "id");
    CHECK(tableNames[0].files.at(2) == "displayname");
    CHECK(tableNames[0].files.at(3) == "release_type");

    // The index of column 0 (file 1 of the table's group) finds quest 123 in row 0.
    CHECK(indices.at({0, 0}).tupleIndexes.at(0).at(123) == std::vector<std::int32_t>{0});
    const std::vector<std::int32_t>& all = indices.at({0, -1}).tupleIndexes.at(0).at(0);
    CHECK(std::find(all.begin(), all.end(), 0) != all.end());

    REQUIRE(rows[0].id == 0);
    const DbRowDefinition& animalMagnetism = rows[0];
    CHECK(animalMagnetism.tableId == 0);
    REQUIRE(animalMagnetism.columns.at(0).has_value());
    CHECK(animalMagnetism.columns[0]->values == DbColumnValues{123});
    REQUIRE(animalMagnetism.columns.at(2).has_value());
    CHECK(animalMagnetism.columns[2]->types == DbColumnTypes{ScriptVarType::String});
    CHECK(animalMagnetism.columns[2]->values == DbColumnValues{std::string("Animal Magnetism")});

    // A column of tuples of three values, and a defaulted one the row leaves out.
    CHECK(animalMagnetism.columns.at(9)->values == DbColumnValues{12, 12, 2006});
    CHECK_FALSE(animalMagnetism.columns.at(3).has_value());
    REQUIRE(tables[0].id == 0);
    CHECK(tables[0].columns.at(3)->defaultValues == DbColumnValues{2});
}

TEST_CASE("an enum whose data is only its end opcode is left out, as RuneLite's EnumLoader leaves it") {
    if (store() == nullptr) return;
    // Enums 0 to 12 are only the end opcode.
    CHECK(store()->index(Index::Configs)->groups.at(static_cast<int>(ConfigGroup::Enums)).fileIds.front() == 0);
    const std::vector<EnumDefinition> enums = loadEnumDefinitions(*store());
    REQUIRE_FALSE(enums.empty());
    const EnumDefinition& first = enums.front();
    CHECK(first.id == 13);
    CHECK(first.keyType == ScriptVarType::Obj);
    CHECK(first.valueType == ScriptVarType::Obj);
    CHECK(first.keys.size() == 11);
    REQUIRE(first.intValues.has_value());
    CHECK(first.intValues->size() == first.keys.size());
    CHECK_FALSE(first.stringValues.has_value());
}

TEST_CASE("an item's game value name") {
    if (store() == nullptr) return;
    const std::vector<GameValDefinition> items = loadGameValDefinitions(*store(), GameValGroup::Items);
    const auto whip = std::find_if(items.begin(), items.end(), [](const GameValDefinition& value) { return value.id == 4151; });
    REQUIRE(whip != items.end());
    CHECK(whip->name == "abyssal_whip");
    CHECK(whip->files.empty());
}

TEST_CASE("names decode as Java decodes UTF-8, as RuneLite's GameValLoader decodes them") {
    CHECK(utf8("Zuk\xC3\xA9") == "Zuk\xC3\xA9");
    // A surrogate's encoding is one replacement, where Unicode's practice
    // would make it three.
    CHECK(utf8("a\xED\xA0\x80" "b") == "a\xEF\xBF\xBD" "b");
    // A sequence cut short is one replacement, and the byte that cuts it
    // decodes on its own.
    CHECK(utf8("\xE1\x80" "A") == "\xEF\xBF\xBD" "A");
    CHECK(utf8("\xF0\x90\x80") == "\xEF\xBF\xBD");
    // Lead bytes no character starts with are replaced one by one.
    CHECK(utf8("\xC0\x80") == "\xEF\xBF\xBD\xEF\xBF\xBD");
}
