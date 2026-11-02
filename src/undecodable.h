#pragma once

// What a loader does with an object it can't decode, as the store's
// StoreOptions say (store.h, Undecodable).

#include <osrs/cache/store.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace osrs::cache {

// Undecodable::Throw: throws `error` with the object named first.
// Undecodable::Skip: reports the object to onSkipped and returns, and the
// loader leaves the object out.
void undecodable(const Store& store, std::string_view type, std::int64_t id, const std::runtime_error& error);

// Reports an object a loader leaves out whatever the options say.
void reportSkipped(const Store& store, std::string_view type, std::int64_t id, const std::runtime_error& error);

// decode()'s result, or nothing for an object undecodable() skips.
template <class Decode>
auto decodeOrSkip(const Store& store, std::string_view type, std::int64_t id, Decode&& decode)
    -> std::optional<std::remove_cvref_t<decltype(decode())>> {
    try {
        return decode();
    } catch (const std::runtime_error& error) {
        undecodable(store, type, id, error);
        return std::nullopt;
    }
}

} // namespace osrs::cache
