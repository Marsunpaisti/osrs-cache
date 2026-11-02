#pragma once

// Decodes UTF-8 as Java's new String(bytes, StandardCharsets.UTF_8) does,
// for the loaders RuneLite writes that way.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace osrs::cache {

// Valid UTF-8 as it is; each maximal invalid subsequence, as Unicode defines
// it, becomes U+FFFD, except that Java takes 0xED as the lead of any three
// bytes, as 0xE1 is, and replaces a whole surrogate's encoding (0xED 0xA0 to
// 0xBF, then any continuation byte) with one U+FFFD.
inline std::string decodeUtf8AsJava(std::span<const std::uint8_t> bytes) {
    std::string text;
    text.reserve(bytes.size());
    for (std::size_t i = 0; i < bytes.size();) {
        const std::uint8_t lead = bytes[i];
        if (lead < 0x80) {
            text += static_cast<char>(lead);
            ++i;
            continue;
        }
        // How many continuation bytes follow the lead, and the range the
        // first of them must be in to rule out overlong forms and code
        // points past U+10FFFF.
        int follow = 0;
        std::uint8_t low = 0x80;
        std::uint8_t high = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF) {
            follow = 1;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            follow = 2;
            if (lead == 0xE0) low = 0xA0;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            follow = 3;
            if (lead == 0xF0) low = 0x90;
            if (lead == 0xF4) high = 0x8F;
        }
        std::size_t end = i + 1;
        bool valid = follow > 0;
        for (int k = 0; k < follow; ++k, ++end, low = 0x80, high = 0xBF) {
            valid = end < bytes.size() && bytes[end] >= low && bytes[end] <= high;
            if (!valid) break;
        }
        const bool surrogate = valid && lead == 0xED && bytes[i + 1] >= 0xA0;
        if (valid && !surrogate) {
            text.append(reinterpret_cast<const char*>(bytes.data() + i), end - i);
        } else {
            text += "\xEF\xBF\xBD";
        }
        i = end;
    }
    return text;
}

} // namespace osrs::cache
