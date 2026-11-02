// osrs_cache_dump: writes what osrs_cache decodes from a cache as the lines
// RuneLiteOracle.java compares with RuneLite's (lines.h).
//
//   osrs_cache_dump <cache dir> <type> [key ...]   every object of the type, or those
//   osrs_cache_dump --types                        the types it writes, one a line
//
// Each type is written by a file of its own (dump_*.cpp), which registers it
// with a Type object.

#include "lines.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <exception>
#include <map>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace osrs::cache::oracle {

namespace {

std::map<std::string, Dumper>& types() {
    static std::map<std::string, Dumper> registered;
    return registered;
}

char hexDigit(unsigned value) { return "0123456789abcdef"[value & 15]; }

std::string hex(std::uint64_t bits, int digits) {
    std::string text(static_cast<std::size_t>(digits), '0');
    for (int i = digits - 1; i >= 0; --i, bits >>= 4) text[static_cast<std::size_t>(i)] = hexDigit(static_cast<unsigned>(bits));
    return text;
}

} // namespace

Type::Type(const char* name, Dumper dumper) { types().emplace(name, dumper); }

void Lines::object(std::initializer_list<std::int64_t> key) {
    std::string text = "@";
    for (const std::int64_t part : key) {
        if (text.size() > 1) text += '.';
        text += std::to_string(part);
    }
    skipping_ = false;
    if (!only_.empty() && std::find(only_.begin(), only_.end(), std::string_view(text).substr(1)) == only_.end()) {
        skipping_ = true;
        return;
    }
    line(text);
}

void Lines::float32(std::string_view path, float value) { line(path, "f" + hex(std::bit_cast<std::uint32_t>(value), 8)); }

void Lines::float64(std::string_view path, double value) { line(path, "d" + hex(std::bit_cast<std::uint64_t>(value), 16)); }

void Lines::booleans(std::string_view path, const std::vector<bool>& values) {
    std::string text = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) text += ',';
        text += values[i] ? "true" : "false";
    }
    line(path, text + "]");
}

void Lines::compact(std::string_view path, const std::function<void(Lines&)>& fill) {
    if (skipping_) return;
    std::vector<std::string> fields;
    {
        Lines element(&fields);
        fill(element);
    }
    std::sort(fields.begin(), fields.end());
    std::string text = "{";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) text += ';';
        text += fields[i];
    }
    line(path, text + "}");
}

std::string Lines::quote(std::string_view utf8) {
    std::string text = "\"";
    for (const char c : utf8) {
        switch (c) {
        case '\\': text += "\\\\"; break;
        case '"': text += "\\\""; break;
        case '\n': text += "\\n"; break;
        case '\r': text += "\\r"; break;
        case '\t': text += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                text += "\\u" + hex(static_cast<unsigned char>(c), 4);
            } else {
                text += c;
            }
        }
    }
    return text + "\"";
}

void Lines::line(std::string_view path, std::string_view value) {
    std::string text;
    text.reserve(path.size() + 1 + value.size());
    text.append(path).append(1, '=').append(value);
    line(text);
}

void Lines::line(std::string_view text) {
    if (skipping_) return;
    if (collected_ != nullptr) {
        collected_->emplace_back(text);
        return;
    }
    buffer_.append(text).append(1, '\n');
    if (buffer_.size() > (1 << 20)) flush();
}

void Lines::flush() {
    if (out_ != nullptr && !buffer_.empty()) std::fwrite(buffer_.data(), 1, buffer_.size(), out_);
    buffer_.clear();
}

std::string field(std::string_view path, std::string_view name) {
    if (path.empty()) return std::string(name);
    std::string text(path);
    return text.append(1, '.').append(name);
}

std::string at(std::string_view path, std::size_t index) { return std::string(path) + "[" + std::to_string(index) + "]"; }

std::string entry(std::string_view path, std::string_view key) { return std::string(path) + "{" + std::string(key) + "}"; }

} // namespace osrs::cache::oracle

int main(int argc, char** argv) {
    using namespace osrs::cache::oracle;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc == 2 && std::string_view(argv[1]) == "--types") {
        for (const auto& [name, dumper] : types()) std::printf("%s\n", name.c_str());
        return 0;
    }
    if (argc < 3) {
        std::fprintf(stderr, "usage: osrs_cache_dump <cache dir> <type> [key ...] | --types\n");
        return 2;
    }
    const auto found = types().find(argv[2]);
    if (found == types().end()) {
        std::fprintf(stderr, "osrs_cache_dump writes no type %s\n", argv[2]);
        return 2;
    }
    try {
        const osrs::cache::Store store(argv[1]);
        Lines lines(stdout, std::vector<std::string>(argv + 3, argv + argc));
        found->second(store, lines);
    } catch (const std::exception& error) {
        std::fflush(stdout);
        std::fprintf(stderr, "osrs_cache_dump %s: %s\n", argv[2], error.what());
        return 1;
    }
    return 0;
}
