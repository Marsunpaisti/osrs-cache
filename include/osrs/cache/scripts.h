#pragma once

// Client scripts (index 12), the bytecode the client runs for its
// interfaces, decoded as RuneLite's cache library decodes them
// (net.runelite:cache 1.13.5, definitions/loaders/ScriptLoader), and written
// out as rs2asm text as its script/disassembler/Disassembler writes them.

#include <osrs/cache/store.h>

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace osrs::cache {

// One case of a switch table: a switch on `value` continues at the
// instruction after the switch plus `offset`.
struct SwitchCase {
    std::int32_t value = 0;
    std::int32_t offset = 0;
};

// RuneLite's ScriptDefinition. Instruction i is opcode instructions[i] with
// the operand that opcode reads: stringOperands[i] for sconst, longOperands[i]
// for lconst, intOperands[i] for the rest; the operands it doesn't read are 0
// or nothing.
struct ScriptDefinition {
    std::int32_t id = 0;
    std::vector<std::int32_t> instructions;
    std::vector<std::int32_t> intOperands;
    std::vector<std::int64_t> longOperands;
    std::vector<std::optional<std::string>> stringOperands;
    std::uint16_t intArgCount = 0;
    std::uint16_t longArgCount = 0;
    std::uint16_t objArgCount = 0;
    std::uint16_t localIntCount = 0;
    std::uint16_t localLongCount = 0;
    std::uint16_t localObjCount = 0;
    // A switch instruction's int operand picks its table. Cases keep the
    // cache's order, and a value the cache repeats keeps its first place with
    // its last offset, as RuneLite's LinkedHashMap keeps them.
    std::vector<std::vector<SwitchCase>> switches;
};

// Decodes a script, its group's file 0. `indexRevision` is the client
// scripts index's revision (IndexInfo::revision), which picks the format as
// ScriptLoader.configureForRevision does: after 1773753883, revision 237's,
// with long locals and arguments. Throws std::runtime_error if it doesn't
// decode.
ScriptDefinition decodeScript(std::int32_t id, std::span<const std::uint8_t> data, std::int32_t indexRevision);

// Nothing if the cache has no such script. Throws std::runtime_error if it
// doesn't decode, as script 0 of the 2026-11-01 cache doesn't, unless the
// store skips such objects (store.h).
std::optional<ScriptDefinition> loadScript(const Store& store, int id);

// The opcode's rs2asm name: RuneLite's script/Opcodes constant, lower-cased
// (script/Instructions). Nothing for an opcode it doesn't name.
std::optional<std::string_view> instructionName(int opcode);

// RuneLiteOpcodes.RUNELITE_EXECUTE: the opcode RuneLite's client injects
// into scripts to call back into RuneLite. The game's scripts don't use it.
inline constexpr int kRuneLiteExecuteOpcode = 6599;

// As instructionName, with kRuneLiteExecuteOpcode named runelite_callback
// (script/RuneLiteInstructions).
std::optional<std::string_view> runeLiteInstructionName(int opcode);

// The script as RuneLite's Disassembler writes it: a header of its id and
// argument counts, then one line per instruction, named by
// instructionName() or as its opcode in three digits, each jump target
// labelled. `symbols` names int operands, as Disassembler(Map) does with the
// map inverted; empty, it disassembles as Disassembler() does. Throws
// std::out_of_range where RuneLite's throws: for a jump or switch case
// outside the script, or a switch whose table is missing.
std::string disassemble(const ScriptDefinition& script, const std::map<std::int32_t, std::string>& symbols = {});

} // namespace osrs::cache
