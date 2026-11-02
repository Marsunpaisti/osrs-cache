// The music and sound effects of osrs/cache/audio.h, as RuneLite's
// TrackDefinition and sound/SoundEffect*Definition classes hold them.

#include "lines.h"

#include <osrs/cache/audio.h>

#include <exception>

namespace osrs::cache::oracle {

namespace {

// Each group's file 0, keyed by group id: what loads, or error() for what
// throws.
template <class Load, class Write>
void perGroup(const Store& store, Index index, Lines& out, Load load, Write write) {
    for (const int group : store.groups(index)) {
        try {
            const auto loaded = load(group);
            if (!loaded) continue;
            out.object({group});
            write(*loaded);
        } catch (const std::exception&) {
            out.object({group});
            out.error();
        }
    }
}

void music(const Store& store, Lines& out, Index index) {
    perGroup(store, index, out, [&](int group) { return loadMidi(store, index, group); },
             [&](const std::vector<std::uint8_t>& midi) {
                 out.integers("midi", std::vector<std::int8_t>(midi.begin(), midi.end()));
             });
}

void tracks(const Store& store, Lines& out) { music(store, out, Index::MusicTracks); }
void jingles(const Store& store, Lines& out) { music(store, out, Index::MusicJingles); }

// RuneLite SoundEffect2Definition.
void envelope(Lines& out, std::string_view path, const SoundEnvelope& e) {
    out.integer(field(path, "field1087"), e.form);
    out.integer(field(path, "field1088"), e.start);
    out.integer(field(path, "field1089"), e.end);
    out.integer(field(path, "field1092"), static_cast<std::int64_t>(e.durations.size()));
    out.integers(field(path, "field1086"), e.durations);
    out.integers(field(path, "field1090"), e.phases);
    // RuneLite's synthesis state, which loading leaves 0.
    for (const char* name : {"field1085", "field1091", "field1093", "field1094", "field1095"}) out.integer(field(path, name), 0);
}

void envelope(Lines& out, std::string_view path, const std::optional<SoundEnvelope>& e) {
    if (e) envelope(out, path, *e);
}

// RuneLite SoundEffect3Definition's [2][2][4] arrays.
void cube(Lines& out, std::string_view path, const std::array<std::array<std::array<int, 4>, 2>, 2>& values) {
    for (std::size_t i = 0; i < 2; ++i) {
        for (std::size_t j = 0; j < 2; ++j) out.integers(at(at(path, i), j), std::span<const int>(values[i][j]));
    }
}

void instrument(Lines& out, std::string_view path, const Instrument& in) {
    envelope(out, field(path, "field1181"), in.pitch);
    envelope(out, field(path, "field1173"), in.volume);
    envelope(out, field(path, "field1174"), in.pitchModifier);
    envelope(out, field(path, "field1193"), in.pitchModifierAmplitude);
    envelope(out, field(path, "field1183"), in.volumeMultiplier);
    envelope(out, field(path, "field1192"), in.volumeMultiplierAmplitude);
    envelope(out, field(path, "field1178"), in.release);
    envelope(out, field(path, "field1175"), in.attack);
    out.integers(field(path, "field1180"), std::span<const int>(in.oscillatorVolumes));
    out.integers(field(path, "field1179"), std::span<const int>(in.oscillatorPitches));
    out.integers(field(path, "field1177"), std::span<const int>(in.oscillatorDelays));
    out.integer(field(path, "field1187"), in.delayTime);
    out.integer(field(path, "field1184"), in.delayDecay);
    out.integer(field(path, "field1176"), in.duration);
    out.integer(field(path, "field1188"), in.offset);
    envelope(out, field(path, "field1186"), in.filterEnvelope);
    const std::string filter = field(path, "field1182");
    out.integers(field(filter, "field1155"), std::span<const int>(in.filter.pairs));
    out.integers(field(filter, "field1156"), std::span<const int>(in.filter.unity));
    cube(out, field(filter, "field1154"), in.filter.phases);
    cube(out, field(filter, "field1159"), in.filter.magnitudes);
}

void soundEffects(const Store& store, Lines& out) {
    perGroup(store, Index::SoundEffects, out, [&](int group) { return loadSoundEffect(store, group); },
             [&](const SoundEffect& effect) {
                 out.integer("field1006", effect.loopStart);
                 out.integer("field1009", effect.loopEnd);
                 for (std::size_t i = 0; i < effect.instruments.size(); ++i) {
                     if (effect.instruments[i]) instrument(out, at("field1008", i), *effect.instruments[i]);
                 }
             });
}

const Type track("track", tracks);
const Type jingle("jingle", jingles);
const Type soundeffect("soundeffect", soundEffects);

} // namespace

} // namespace osrs::cache::oracle
