#pragma once

// Music and sound effects. Music tracks (index 6) and jingles (index 11) are
// converted to standard MIDI files, and the synthesized sound effects (index
// 4) decoded, as RuneLite's cache library does it (net.runelite:cache 1.13.5:
// definitions/loaders/TrackLoader, and definitions/loaders/sound
// SoundEffectLoader and SoundEffect1Loader to SoundEffect3Loader). The sound
// effects' fields have the names the deobfuscated client gives them where
// RuneLite has none; each notes RuneLite's field.

#include <osrs/cache/store.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace osrs::cache {

// A music track or jingle (`index` Index::MusicTracks or
// Index::MusicJingles) as a standard MIDI file, byte for byte RuneLite's
// TrackLoader's: its tracks' end and tempo events always carry their 0xFF
// status, where the client's conversion leaves it to running status.
// Nothing if the index has no such group or the group no file 0. Throws
// std::runtime_error if it doesn't convert, unless the store skips such
// objects (store.h).
std::optional<std::vector<std::uint8_t>> loadMidi(const Store& store, Index index, int id);

// How a value moves over a sound: from start to end through segments
// (RuneLite SoundEffect2Definition, the client's SoundEnvelope).
struct SoundEnvelope {
    // RuneLite's field1087: the waveform, for an envelope that drives an
    // oscillator.
    int form = 0;
    // RuneLite's field1088 and field1089.
    std::int32_t start = 0;
    std::int32_t end = 0;
    // Where each segment ends, as a fraction of the sound out of 65536, and
    // the level it reaches (RuneLite's field1086 and field1090; field1092
    // their count). An envelope the cache gives no segments keeps these.
    std::vector<int> durations{0, 65535};
    std::vector<int> phases{0, 65535};
};

// RuneLite SoundEffect3Definition, the client's AudioFilter.
struct SoundFilter {
    // How many pairs of poles each of its two directions has (RuneLite's
    // field1155).
    std::array<int, 2> pairs{};
    // RuneLite's field1156.
    std::array<int, 2> unity{};
    // [direction][0 at the sound's start, 1 at its end][pair] (RuneLite's
    // field1154 and field1159).
    std::array<std::array<std::array<int, 4>, 2>, 2> phases{};
    std::array<std::array<std::array<int, 4>, 2>, 2> magnitudes{};
};

// RuneLite SoundEffect1Definition, the client's Instrument.
struct Instrument {
    // RuneLite's field1181 and field1173.
    SoundEnvelope pitch;
    SoundEnvelope volume;
    // Each pair is present or absent together: RuneLite's field1174 and
    // field1193, field1183 and field1192, field1178 and field1175.
    std::optional<SoundEnvelope> pitchModifier;
    std::optional<SoundEnvelope> pitchModifierAmplitude;
    std::optional<SoundEnvelope> volumeMultiplier;
    std::optional<SoundEnvelope> volumeMultiplierAmplitude;
    std::optional<SoundEnvelope> release;
    std::optional<SoundEnvelope> attack;
    // Up to five oscillators, the first with volume 0 ending them (RuneLite's
    // field1180, field1179 and field1177).
    std::array<int, 5> oscillatorVolumes{};
    std::array<int, 5> oscillatorPitches{};
    std::array<int, 5> oscillatorDelays{};
    // RuneLite's field1187 and field1184.
    int delayTime = 0;
    int delayDecay = 100;
    // In milliseconds (RuneLite's field1176 and field1188).
    int duration = 500;
    int offset = 0;
    // RuneLite's field1186 and field1182.
    SoundEnvelope filterEnvelope;
    SoundFilter filter;
};

// RuneLite SoundEffectDefinition.
struct SoundEffect {
    // RuneLite's field1008.
    std::array<std::optional<Instrument>, 10> instruments;
    // The part that repeats when the sound loops, in milliseconds (RuneLite's
    // field1006 and field1009).
    int loopStart = 0;
    int loopEnd = 0;
};

// Nothing if the cache has no such sound effect, or its group no file 0.
// Throws std::runtime_error if it doesn't decode, unless the store skips
// such objects (store.h).
std::optional<SoundEffect> loadSoundEffect(const Store& store, int id);

} // namespace osrs::cache
