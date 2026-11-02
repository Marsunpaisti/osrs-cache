#pragma once

// The lines osrs_cache_dump writes for RuneLiteOracle.java to compare with
// RuneLite's: "@key" starts each object, then "path=value" for each of its
// fields as RuneLite holds them. The two must format values identically:
// - a path is RuneLite's field names joined by dots, "[i]" for an element
//   of an object array or list, "{key}" for a map entry, "{}" for a set's
//   element;
// - integers in decimal, in the width and signedness of RuneLite's field (a
//   Java byte is -128 to 127);
// - booleans true or false; a char as its code;
// - strings quoted, with \\ \" \n \r \t escaped and other control
//   characters as \u00XX;
// - enums by their constant's name;
// - floats and doubles as their bits in hex after f or d;
// - a primitive array on one line, as [a,b,c];
// - a field RuneLite leaves null is written not at all, so an absent field
//   and an empty array differ;
// - a map tile (RuneLite's MapDefinition.Tile, as there are millions) on
//   one line, as {field=value;...} sorted: compact().

#include <osrs/cache/store.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace osrs::cache::oracle {

class Lines {
public:
    explicit Lines(std::FILE* out) : out_(out) {}
    // Writes only the objects with these keys ("12850", "149.0"); all if empty.
    Lines(std::FILE* out, std::vector<std::string> only) : out_(out), only_(std::move(only)) {}
    ~Lines() { flush(); }
    Lines(const Lines&) = delete;
    Lines& operator=(const Lines&) = delete;

    // Starts the next object. Objects must come in ascending key order,
    // compared part by part.
    void object(std::initializer_list<std::int64_t> key);
    // The decoder threw for this object (RuneLite's must throw for it too).
    void error() { line("!error"); }

    // Whether lines are being dropped, for an object the key filter leaves
    // out: a dumper may skip work then.
    bool skipping() const { return skipping_; }

    void integer(std::string_view path, std::int64_t value) {
        if (!skipping_) line(path, std::to_string(value));
    }
    void boolean(std::string_view path, bool value) { line(path, value ? "true" : "false"); }
    void string(std::string_view path, std::string_view utf8) { line(path, quote(utf8)); }
    void enumeration(std::string_view path, std::string_view name) { line(path, name); }
    void float32(std::string_view path, float value);
    void float64(std::string_view path, double value);

    // A primitive array: each element converted to std::int64_t, so pass the
    // values as RuneLite holds them (an int8_t for a Java byte).
    template <class T>
    void integers(std::string_view path, std::span<const T> values) {
        if (skipping_) return;
        std::string text = "[";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i > 0) text += ',';
            text += std::to_string(static_cast<std::int64_t>(values[i]));
        }
        line(path, text + "]");
    }
    template <class T>
    void integers(std::string_view path, const std::vector<T>& values) {
        integers(path, std::span<const T>(values));
    }
    void booleans(std::string_view path, const std::vector<bool>& values);

    // An object on one line: the lines `fill` writes, with paths relative to
    // it, sorted and joined by ';' within braces.
    void compact(std::string_view path, const std::function<void(Lines&)>& fill);
    // An element of a set RuneLite keeps in a hash set, written compact()
    // at path{}, as a hash set's order isn't the cache's.
    void setElement(std::string_view path, const std::function<void(Lines&)>& fill) { compact(std::string(path) + "{}", fill); }

    static std::string quote(std::string_view utf8);

private:
    explicit Lines(std::vector<std::string>* collected) : out_(nullptr), collected_(collected) {}
    void line(std::string_view path, std::string_view value);
    void line(std::string_view text);
    void flush();

    std::FILE* out_;
    std::vector<std::string>* collected_ = nullptr;
    std::vector<std::string> only_;
    bool skipping_ = false;
    std::string buffer_;
};

// Paths: path.name, path[i] and path{key}.
std::string field(std::string_view path, std::string_view name);
std::string at(std::string_view path, std::size_t index);
std::string entry(std::string_view path, std::string_view key);

using Dumper = void (*)(const Store&, Lines&);

// Registers a type osrs_cache_dump writes; a namespace-scope object in the
// file that writes it.
struct Type {
    Type(const char* name, Dumper dumper);
};

} // namespace osrs::cache::oracle
