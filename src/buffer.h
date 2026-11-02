#pragma once

// Reads the cache's big-endian encodings (RuneLite io/InputStream). Every
// read checks the bounds and throws std::runtime_error past the end, so a
// damaged definition fails instead of reading garbage.

#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace osrs::cache {

class Buffer {
public:
    explicit Buffer(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::size_t position() const { return pos_; }
    std::size_t remaining() const { return bytes_.size() - pos_; }
    void skip(std::size_t count) { take(count); }

    int u8() { return *take(1); }
    int i8() { return static_cast<std::int8_t>(*take(1)); }
    int u16() {
        const std::uint8_t* p = take(2);
        return p[0] << 8 | p[1];
    }
    int i16() { return static_cast<std::int16_t>(u16()); }
    int u24() {
        const std::uint8_t* p = take(3);
        return p[0] << 16 | p[1] << 8 | p[2];
    }
    std::int32_t i32() {
        const std::uint8_t* p = take(4);
        return static_cast<std::int32_t>(std::uint32_t{p[0]} << 24 | std::uint32_t{p[1]} << 16 | std::uint32_t{p[2]} << 8 | p[3]);
    }
    std::int64_t i64() {
        const std::uint64_t high = static_cast<std::uint32_t>(i32());
        return static_cast<std::int64_t>(high << 32 | static_cast<std::uint32_t>(i32()));
    }

    float f32() { return std::bit_cast<float>(i32()); }

    // One byte less 64 below 128, else two less 49152.
    int shortSmart() { return peek() < 128 ? u8() - 64 : u16() - 0xC000; }
    // Two bytes below 32768, else four with the top bit cleared.
    int bigSmart() { return (peek() & 0x80) == 0 ? u16() : i32() & 0x7FFFFFFF; }
    // As bigSmart, with 32767 for -1.
    int bigSmartOrMinusOne() {
        if ((peek() & 0x80) != 0) return i32() & 0x7FFFFFFF;
        const int value = u16();
        return value == 32767 ? -1 : value;
    }
    // One byte below 128, else two less 32768.
    int unsignedShortSmart() { return peek() < 128 ? u8() : u16() - 0x8000; }
    int unsignedShortSmartMinusOne() { return peek() < 128 ? u8() - 1 : u16() - 0x8001; }
    // A sum of unsignedShortSmarts, each 32767 adding to the next.
    int unsignedIntSmart() {
        int total = 0;
        int part = unsignedShortSmart();
        for (; part == 32767; part = unsignedShortSmart()) total += 32767;
        return total + part;
    }

    // A zero-terminated Windows-1252 string, as UTF-8. Bytes 128 to 159 map
    // as RuneLite's InputStream.readString maps them, '?' where it has none.
    std::string string() {
        static constexpr char16_t kHigh[32] = {
            0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
            0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
            0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
        std::string text;
        for (int c = u8(); c != 0; c = u8()) {
            char32_t code = static_cast<char32_t>(c);
            if (c >= 128 && c < 160) code = kHigh[c - 128] != 0 ? kHigh[c - 128] : U'?';
            if (code < 0x80) {
                text += static_cast<char>(code);
            } else if (code < 0x800) {
                text += static_cast<char>(0xC0 | code >> 6);
                text += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                text += static_cast<char>(0xE0 | code >> 12);
                text += static_cast<char>(0x80 | (code >> 6 & 0x3F));
                text += static_cast<char>(0x80 | (code & 0x3F));
            }
        }
        return text;
    }

private:
    int peek() const {
        if (pos_ >= bytes_.size()) throw std::runtime_error("cache data ends early");
        return bytes_[pos_];
    }
    const std::uint8_t* take(std::size_t count) {
        if (count > bytes_.size() - pos_) throw std::runtime_error("cache data ends early");
        const std::uint8_t* p = bytes_.data() + pos_;
        pos_ += count;
        return p;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
};

} // namespace osrs::cache
