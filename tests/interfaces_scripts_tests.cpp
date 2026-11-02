// Interface components and client scripts decoded from bytes built here, for
// what the pinned cache doesn't show: the formats before revision 237 and
// RuneLite's edge cases. RuneLiteOracle.java checks the cache's own.

#include <doctest/doctest.h>

#include <osrs/cache/interfaces.h>
#include <osrs/cache/scripts.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace osrs::cache;

namespace {

struct Bytes {
    std::vector<std::uint8_t> data;

    Bytes& u8(int value) {
        data.push_back(static_cast<std::uint8_t>(value));
        return *this;
    }
    Bytes& u16(int value) { return u8(value >> 8).u8(value); }
    Bytes& u24(int value) { return u8(value >> 16).u16(value & 0xFFFF); }
    Bytes& i32(std::int32_t value) { return u16(static_cast<int>(static_cast<std::uint32_t>(value) >> 16)).u16(value & 0xFFFF); }
    Bytes& string(std::string_view text) {
        for (const char c : text) u8(c);
        return u8(0);
    }
};

constexpr int kScriptRevision237 = 1773753884;
constexpr int kInterfaceRevision237 = 1773137433;

// iconst 7, return, then the trailer, in revision 237's format or the one
// before.
std::vector<std::uint8_t> twoInstructionScript(bool rev237) {
    Bytes b;
    b.u8(0); // no name
    b.u16(0).i32(7);
    b.u16(21).u8(0);
    b.i32(2);
    b.u16(1).u16(2); // int and object locals
    if (rev237) b.u16(3); // long locals
    b.u16(4).u16(5); // int and object arguments
    if (rev237) b.u16(6); // long arguments
    b.u8(0); // no switch tables
    b.u16(1); // the tables' length, their count's byte included
    return b.data;
}

// An IF3 model component, file 7 of interface 3: its model id 70000 in
// revision 237's format, or none in the one before. Its on-load listener
// runs script 1234 with a string, then an argument of a type RuneLite's
// loader doesn't know, which has no bytes.
std::vector<std::uint8_t> modelComponent(bool rev237) {
    Bytes b;
    b.u8(0xFF).u8(6).u16(0);
    b.u16(0).u16(0).u16(32).u16(32);
    b.u8(0).u8(0).u8(0).u8(0); // size and position modes
    b.u16(5); // parent: file 5
    b.u8(0);
    if (rev237) {
        b.i32(70000);
    } else {
        b.u16(0xFFFF);
    }
    b.u16(0).u16(0); // 2D offset
    b.u16(0).u16(0).u16(0).u16(100); // rotation and zoom
    b.u16(0xFFFF); // no animation
    b.u8(0).u16(0); // not orthogonal, then two bytes RuneLite skips
    b.u24(0).string("").u8(0); // click mask, name, no actions
    b.u8(0).u8(0).u8(0).string(""); // drag settings, target verb
    b.u8(3).u8(0).i32(1234).u8(1).string("x").u8(2);
    for (int i = 1; i < 18; ++i) b.u8(0); // the other listeners
    b.u8(0).u8(0).u8(0); // no triggers
    return b.data;
}

// An IF1 container with one script of these values.
std::vector<std::uint8_t> if1Container(const std::vector<int>& script) {
    Bytes b;
    b.u8(0).u8(0).u16(0); // container, no menu, no content type
    b.u16(0).u16(0).u16(32).u16(32);
    b.u8(0).u16(0xFFFF).u16(0xFFFF); // opacity, no parent, no hovered sibling
    b.u8(0); // no alternates
    b.u8(1).u16(static_cast<int>(script.size()));
    for (const int value : script) b.u16(value);
    b.u16(0).u8(0); // scroll height, shown
    return b.data;
}

} // namespace

TEST_CASE("a script counts long locals and arguments only in revision 237's format") {
    const ScriptDefinition current = decodeScript(9, twoInstructionScript(true), kScriptRevision237);
    CHECK(current.localObjCount == 2);
    CHECK(current.localLongCount == 3);
    CHECK(current.objArgCount == 5);
    CHECK(current.longArgCount == 6);

    const ScriptDefinition older = decodeScript(9, twoInstructionScript(false), kScriptRevision237 - 1);
    CHECK(older.localObjCount == 2);
    CHECK(older.localLongCount == 0);
    CHECK(older.objArgCount == 5);
    CHECK(older.longArgCount == 0);

    for (const ScriptDefinition& script : {current, older}) {
        CHECK(script.instructions == std::vector<std::int32_t>{0, 21});
        CHECK(script.intOperands == std::vector<std::int32_t>{7, 0});
    }
}

TEST_CASE("a script disassembles with its jump targets labelled and each switch's table after it") {
    Bytes b;
    b.u8(0);
    b.u16(0).i32(7); // iconst 7
    b.u16(60).i32(0); // switch on table 0
    b.u16(6).i32(1); // jump to instruction 4
    b.u16(3).string("a \"b\""); // sconst
    b.u16(5).i32(9); // an opcode RuneLite doesn't name
    b.u16(21).u8(0); // return
    b.i32(6).u16(0).u16(0).u16(0).u16(0).u16(0).u16(0);
    // One table in which 7 repeats: it keeps its first place and its last offset.
    b.u8(1).u16(3).i32(7).i32(1).i32(8).i32(3).i32(7).i32(2);
    b.u16(1 + 2 + 3 * 8);
    const ScriptDefinition script = decodeScript(9, b.data, kScriptRevision237);

    REQUIRE(script.switches.size() == 1);
    REQUIRE(script.switches[0].size() == 2);
    CHECK(script.switches[0][0].value == 7);
    CHECK(script.switches[0][0].offset == 2);
    CHECK(script.switches[0][1].value == 8);
    CHECK(script.switches[0][1].offset == 3);

    CHECK(disassemble(script) ==
          ".id                       9\n"
          ".int_arg_count            0\n"
          ".long_arg_count           0\n"
          ".obj_arg_count            0\n"
          "   iconst                 7\n"
          "   switch                \n"
          "      7: LABEL4\n"
          "      8: LABEL5\n"
          "   jump                   LABEL4\n"
          "   sconst                 \"a \\\"b\\\"\"\n"
          "LABEL4:\n"
          "   005                    9\n"
          "LABEL5:\n"
          "   return                \n");
}

TEST_CASE("disassembling throws where RuneLite's disassembler throws") {
    ScriptDefinition script;
    script.instructions = {6};
    script.intOperands = {1};
    script.longOperands = {0};
    script.stringOperands = {std::nullopt};
    CHECK_THROWS_AS(disassemble(script), std::out_of_range); // a jump past the end

    script.instructions = {60};
    script.intOperands = {0};
    CHECK_THROWS_AS(disassemble(script), std::out_of_range); // a switch without its table
}

TEST_CASE("an IF3 model component's model id is 16 bits before revision 237 and 32 after") {
    const std::int32_t id = (3 << 16) + 7;
    const InterfaceDefinition current = decodeInterface(id, modelComponent(true), kInterfaceRevision237);
    CHECK(current.modelId == 70000);
    const InterfaceDefinition older = decodeInterface(id, modelComponent(false), kInterfaceRevision237 - 1);
    CHECK(older.modelId == -1);

    for (const InterfaceDefinition& iface : {current, older}) {
        CHECK(iface.isIf3);
        CHECK(iface.parentId == (3 << 16) + 5);
        REQUIRE(iface.onLoadListener.size() == 3);
        CHECK(std::get<std::int32_t>(iface.onLoadListener[0]) == 1234);
        CHECK(std::get<std::string>(iface.onLoadListener[1]) == "x");
        CHECK(std::holds_alternative<std::monostate>(iface.onLoadListener[2]));
        CHECK(iface.hasListener);
        CHECK(iface.onMouseOverListener.empty());
    }
}

TEST_CASE("an IF1 script pads its last instruction's missing operands with 0, and an unknown opcode throws") {
    // varp 7, then varp_testbit, which takes two operands, with none left.
    const InterfaceDefinition iface = decodeInterface(0, if1Container({5, 7, 13}), kInterfaceRevision237);
    CHECK_FALSE(iface.isIf3);
    REQUIRE(iface.clientScripts.size() == 1);
    const std::vector<ClientScript1Instruction>& script = iface.clientScripts[0];
    REQUIRE(script.size() == 2);
    CHECK(script[0].opcode == ClientScript1Opcode::Varp);
    CHECK(script[0].operands == std::vector<std::int32_t>{7});
    CHECK(script[1].opcode == ClientScript1Opcode::VarpTestbit);
    CHECK(script[1].operands == std::vector<std::int32_t>{0, 0});

    CHECK_THROWS_AS(decodeInterface(0, if1Container({0xFFFF}), kInterfaceRevision237), std::runtime_error);
}
