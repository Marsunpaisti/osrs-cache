#include <osrs/cache/scripts.h>

#include "buffer.h"
#include "undecodable.h"

#include <algorithm>
#include <stdexcept>

namespace osrs::cache {

namespace {

// The opcodes whose operands differ from the rest (RuneLite script/Opcodes).
constexpr int kSconst = 3;
constexpr int kReturn = 21;
constexpr int kPopInt = 38;
constexpr int kPopObject = 39;
constexpr int kLconst = 61;
constexpr int kPushNull = 63;

} // namespace

// RuneLite ScriptLoader.load: the instructions from the start, after the
// script's name, and a trailer at the end: the counts, then the switch
// tables, then the tables' length in two bytes.
ScriptDefinition decodeScript(std::int32_t id, std::span<const std::uint8_t> data, std::int32_t indexRevision) {
    const bool rev237 = indexRevision > 1773753883;
    if (data.size() < 2) throw std::runtime_error("script shorter than its switch tables' length");
    const std::size_t switchLength = static_cast<std::size_t>(data[data.size() - 2] << 8 | data[data.size() - 1]);
    const std::size_t trailer = 2 + switchLength + (rev237 ? 16 : 12);
    if (trailer > data.size()) throw std::runtime_error("script shorter than its trailer");
    const std::size_t end = data.size() - trailer;

    ScriptDefinition script;
    script.id = id;
    Buffer counts(data.subspan(end));
    const std::int32_t instructionCount = counts.i32();
    script.localIntCount = static_cast<std::uint16_t>(counts.u16());
    script.localObjCount = static_cast<std::uint16_t>(counts.u16());
    if (rev237) script.localLongCount = static_cast<std::uint16_t>(counts.u16());
    script.intArgCount = static_cast<std::uint16_t>(counts.u16());
    script.objArgCount = static_cast<std::uint16_t>(counts.u16());
    if (rev237) script.longArgCount = static_cast<std::uint16_t>(counts.u16());

    script.switches.resize(static_cast<std::size_t>(counts.u8()));
    for (std::vector<SwitchCase>& table : script.switches) {
        for (int count = counts.u16(); count > 0; --count) {
            const std::int32_t value = counts.i32();
            const std::int32_t offset = counts.i32();
            const auto same = std::find_if(table.begin(), table.end(), [&](const SwitchCase& c) { return c.value == value; });
            if (same != table.end()) {
                same->offset = offset;
            } else {
                table.push_back({value, offset});
            }
        }
    }

    if (instructionCount < 0) throw std::runtime_error("script counts fewer than no instructions");
    // A count from a misread trailer would otherwise allocate gigabytes, and fail as std::bad_alloc,
    // which a store that skips undecodable objects doesn't catch. Every instruction takes bytes, so a
    // real count is never more than there are.
    if (static_cast<std::size_t>(instructionCount) > end) throw std::runtime_error("script counts more instructions than it has bytes");
    const auto size = static_cast<std::size_t>(instructionCount);
    script.instructions.resize(size);
    script.intOperands.resize(size);
    script.longOperands.resize(size);
    script.stringOperands.resize(size);

    Buffer in(data);
    in.string(); // the script's name
    // Only where an instruction starts is checked against the trailer, as
    // RuneLite checks it.
    for (std::size_t i = 0; in.position() < end; ++i) {
        const int opcode = in.u16();
        if (i >= size) throw std::runtime_error("script has more instructions than it counts");
        script.instructions[i] = opcode;
        switch (opcode) {
        case kSconst: script.stringOperands[i] = in.string(); break;
        case kReturn:
        case kPopInt:
        case kPopObject:
        case kPushNull: script.intOperands[i] = in.u8(); break;
        case kLconst: script.longOperands[i] = in.i64(); break;
        default: script.intOperands[i] = opcode < 100 ? in.i32() : in.u8(); break;
        }
    }
    return script;
}

std::optional<ScriptDefinition> loadScript(const Store& store, int id) {
    const auto files = store.files(Index::ClientScripts, id);
    if (!files) return std::nullopt;
    const auto file = files->find(0);
    if (file == files->end()) return std::nullopt;
    return decodeOrSkip(store, "script", id, [&] { return decodeScript(id, file->second, store.index(Index::ClientScripts)->revision); });
}

} // namespace osrs::cache
