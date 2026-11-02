// Client scripts (osrs/cache/scripts.h), as RuneLite's
// definitions/ScriptDefinition holds them, and their disassembly, as
// RuneLiteOracle.java's Text holds it: the lines of the text.

#include "lines.h"

#include <osrs/cache/scripts.h>

#include <exception>

namespace osrs::cache::oracle {

namespace {

void script(Lines& out, const ScriptDefinition& script) {
    out.integer("id", script.id);
    out.integers("instructions", script.instructions);
    out.integers("intOperands", script.intOperands);
    out.integers("longOperands", script.longOperands);
    for (std::size_t i = 0; i < script.stringOperands.size(); ++i) {
        if (script.stringOperands[i]) out.string(at("stringOperands", i), *script.stringOperands[i]);
    }
    out.integer("intArgCount", script.intArgCount);
    out.integer("longArgCount", script.longArgCount);
    out.integer("objArgCount", script.objArgCount);
    out.integer("localIntCount", script.localIntCount);
    out.integer("localLongCount", script.localLongCount);
    out.integer("localObjCount", script.localObjCount);
    for (std::size_t i = 0; i < script.switches.size(); ++i) {
        const std::string table = at("switches", i);
        for (const SwitchCase& c : script.switches[i]) out.integer(entry(table, std::to_string(c.value)), c.offset);
    }
}

// Text splits on every newline, keeping the empty line after the last.
void disassembly(Lines& out, const ScriptDefinition& script) {
    const std::string text = disassemble(script);
    std::size_t line = 0;
    for (std::size_t start = 0;; ++line) {
        const std::size_t end = text.find('\n', start);
        out.string(at("lines", line), std::string_view(text).substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
}

// As RuneLiteOracle.java loads them: file 0 of each group, keyed by group,
// with the revision it configures ScriptLoader with.
void scripts(const Store& store, Lines& out, void (*write)(Lines&, const ScriptDefinition&)) {
    const IndexInfo* index = store.index(Index::ClientScripts);
    if (index == nullptr) return;
    for (const int group : store.groups(Index::ClientScripts)) {
        std::optional<std::map<int, std::vector<std::uint8_t>>> files;
        try {
            files = store.files(Index::ClientScripts, group);
        } catch (const std::exception&) {
            out.object({group});
            out.error();
            continue;
        }
        if (!files || !files->contains(0)) continue;
        out.object({group});
        if (out.skipping()) continue;
        // Neither writer writes a line before it can throw.
        try {
            write(out, decodeScript(group, files->at(0), index->revision));
        } catch (const std::exception&) {
            out.error();
        }
    }
}

const Type scriptType("script", [](const Store& store, Lines& out) { scripts(store, out, script); });
const Type disassemblyType("disassembly", [](const Store& store, Lines& out) { scripts(store, out, disassembly); });

} // namespace

} // namespace osrs::cache::oracle
