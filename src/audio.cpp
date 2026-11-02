#include <osrs/cache/audio.h>

#include "buffer.h"
#include "undecodable.h"

#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace osrs::cache {

namespace {

// The cache's packed music: each track's event kinds, one byte each with the
// channel change in the top four bits, ended by 7; then every event's delta
// time; then each kind of value in a section of its own, delta-coded. The
// last three bytes are the track count and the MIDI division.
//
// The packed form's event kinds (RuneLite TrackLoader's JAG_ constants), by
// their low four bits, except tempo, which is the whole byte.
constexpr int kNoteOn = 0;
constexpr int kNoteOff = 1;
constexpr int kControlChange = 2;
constexpr int kPitchBend = 3;
constexpr int kChannelPressure = 4;
constexpr int kPolyPressure = 5;
constexpr int kProgramChange = 6;
constexpr int kEndOfTrack = 7;
constexpr int kTempo = 23;

// The MIDI controllers whose values have sections of their own.
constexpr int kBankSelect = 0;
constexpr int kModulationWheel = 1;
constexpr int kChannelVolume = 7;
constexpr int kPan = 10;
constexpr int kBankSelect2 = 32;
constexpr int kModulationWheel2 = 33;
constexpr int kChannelVolume2 = 39;
constexpr int kPan2 = 42;
constexpr int kDamperPedal = 64;
constexpr int kPortamento = 65;
constexpr int kNonRegisteredLsb = 98;
constexpr int kNonRegisteredMsb = 99;
constexpr int kRegisteredLsb = 100;
constexpr int kRegisteredMsb = 101;
constexpr int kAllSoundOff = 120;
constexpr int kResetAllControllers = 121;
constexpr int kAllNotesOff = 123;

bool isCommand(int controller) {
    return controller == kDamperPedal || controller == kPortamento || controller == kAllSoundOff || controller == kResetAllControllers ||
           controller == kAllNotesOff;
}

// RuneLite InputStream.readVarInt: big-endian groups of seven bits, the top
// bit set on all but the last.
int readVarInt(Buffer& in) {
    int value = 0;
    int part = in.i8();
    for (; part < 0; part = in.i8()) value = (value | (part & 127)) << 7;
    return value | part;
}

// RuneLite's OutputStream as TrackLoader uses it, sized as RuneLite sizes it:
// a write grows it by doubling, but skipping past its capacity throws.
class MidiWriter {
public:
    explicit MidiWriter(int capacity) : capacity_(capacity) {}

    std::size_t size() const { return bytes_.size(); }

    void byte(int value) {
        reserve(1);
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }
    void u16(int value) {
        reserve(2);
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }
    void u32(std::uint32_t value) {
        reserve(4);
        for (int shift = 24; shift >= 0; shift -= 8) bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
    }
    // RuneLite OutputStream.writeVarInt.
    void varInt(int value) {
        const auto v = static_cast<std::uint32_t>(value);
        if ((v & ~0x7Fu) != 0) {
            if ((v & ~0x3FFFu) != 0) {
                if ((v & ~0x1FFFFFu) != 0) {
                    if ((v & ~0xFFFFFFFu) != 0) byte(static_cast<int>(v >> 28 | 128));
                    byte(static_cast<int>(v >> 21 | 128));
                }
                byte(static_cast<int>(v >> 14 | 128));
            }
            byte(static_cast<int>(v >> 7 | 128));
        }
        byte(static_cast<int>(v & 127));
    }
    // Room for a length written later.
    void skip4() {
        if (bytes_.size() + 4 > static_cast<std::size_t>(capacity_)) throw std::runtime_error("music track larger than RuneLite sizes it");
        bytes_.resize(bytes_.size() + 4);
    }
    // RuneLite OutputStream.writeLengthFromMark: the length of the last
    // `length` bytes, into the four before them.
    void lengthFromMark(std::size_t length) {
        const std::size_t at = bytes_.size() - length - 4;
        const auto value = static_cast<std::uint32_t>(length);
        for (int i = 0; i < 4; ++i) bytes_[at + i] = static_cast<std::uint8_t>(value >> (24 - 8 * i));
    }

    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    void reserve(std::size_t count) {
        while (count > static_cast<std::size_t>(capacity_) - bytes_.size()) capacity_ *= 2;
    }

    std::vector<std::uint8_t> bytes_;
    int capacity_;
};

// RuneLite TrackLoader.load.
std::vector<std::uint8_t> packedTrackToMidi(std::span<const std::uint8_t> bytes) {
    // The packed form's bytes by index, as Java's signed bytes.
    const auto at = [&](int index) -> int {
        if (index < 0 || static_cast<std::size_t>(index) >= bytes.size()) throw std::runtime_error("music track ends early");
        return static_cast<std::int8_t>(bytes[static_cast<std::size_t>(index)]);
    };
    if (bytes.size() < 3) throw std::runtime_error("music track shorter than its trailer");
    Buffer trailer(bytes.subspan(bytes.size() - 3));
    const int tracks = trailer.u8();
    const int division = trailer.u16();

    // RuneLite's estimate of the MIDI file's size, which it sizes its
    // output by.
    int estimate = 14 + tracks * 10;
    int tempos = 0;
    int controlChanges = 0;
    int noteOns = 0;
    int noteOffs = 0;
    int pitchBends = 0;
    int channelPressures = 0;
    int polyPressures = 0;
    int programChanges = 0;
    Buffer in(bytes);
    for (int track = 0; track < tracks; ++track) {
        int lastKind = -1;
        for (;;) {
            const int status = in.u8();
            // As RuneLite has it: the whole byte against the last one's low
            // four bits.
            if (status != lastKind) ++estimate;
            const int kind = lastKind = status & 15;
            if (status == kEndOfTrack) break;
            if (status == kTempo) {
                ++tempos;
            } else if (kind == kNoteOn) {
                ++noteOns;
            } else if (kind == kNoteOff) {
                ++noteOffs;
            } else if (kind == kControlChange) {
                ++controlChanges;
            } else if (kind == kPitchBend) {
                ++pitchBends;
            } else if (kind == kChannelPressure) {
                ++channelPressures;
            } else if (kind == kPolyPressure) {
                ++polyPressures;
            } else if (kind == kProgramChange) {
                ++programChanges;
            } else {
                throw std::runtime_error("music track event of unknown kind " + std::to_string(status));
            }
        }
    }
    estimate += 5 * tempos;
    estimate += 2 * (noteOns + noteOffs + controlChanges + pitchBends + polyPressures);
    estimate += channelPressures + programChanges;
    const std::size_t deltaTimes = in.position();
    const int events = tracks + tempos + controlChanges + noteOns + noteOffs + pitchBends + channelPressures + polyPressures + programChanges;
    for (int i = 0; i < events; ++i) readVarInt(in);
    estimate += static_cast<int>(in.position() - deltaTimes);

    int controllerIndex = static_cast<int>(in.position());
    int modulationWheels = 0;
    int modulationWheels2 = 0;
    int channelVolumes = 0;
    int channelVolumes2 = 0;
    int pans = 0;
    int pans2 = 0;
    int nonRegisteredMsbs = 0;
    int nonRegisteredLsbs = 0;
    int registeredMsbs = 0;
    int registeredLsbs = 0;
    int commands = 0;
    int others = 0;
    int controller = 0;
    for (int i = 0; i < controlChanges; ++i) {
        controller = (controller + in.u8()) & 127;
        if (controller == kBankSelect || controller == kBankSelect2) {
            // Bank selects take their values from the program changes'
            // section.
            ++programChanges;
        } else if (controller == kModulationWheel) {
            ++modulationWheels;
        } else if (controller == kModulationWheel2) {
            ++modulationWheels2;
        } else if (controller == kChannelVolume) {
            ++channelVolumes;
        } else if (controller == kChannelVolume2) {
            ++channelVolumes2;
        } else if (controller == kPan) {
            ++pans;
        } else if (controller == kPan2) {
            ++pans2;
        } else if (controller == kNonRegisteredMsb) {
            ++nonRegisteredMsbs;
        } else if (controller == kNonRegisteredLsb) {
            ++nonRegisteredLsbs;
        } else if (controller == kRegisteredMsb) {
            ++registeredMsbs;
        } else if (controller == kRegisteredLsb) {
            ++registeredLsbs;
        } else if (!isCommand(controller)) {
            ++others;
        } else {
            ++commands;
        }
    }

    // Where each section starts.
    const auto section = [&](int length) {
        const int start = static_cast<int>(in.position());
        in.skip(static_cast<std::size_t>(length));
        return start;
    };
    int commandIndex = section(commands);
    int polyPressureIndex = section(polyPressures);
    int channelPressureIndex = section(channelPressures);
    int pitchBendHighIndex = section(pitchBends);
    int modulationWheelIndex = section(modulationWheels);
    int channelVolumeIndex = section(channelVolumes);
    int panIndex = section(pans);
    int keyIndex = section(noteOns + noteOffs + polyPressures);
    int noteOnVelocityIndex = section(noteOns);
    int otherIndex = section(others);
    int noteOffVelocityIndex = section(noteOffs);
    int modulationWheel2Index = section(modulationWheels2);
    int channelVolume2Index = section(channelVolumes2);
    int pan2Index = section(pans2);
    int programChangeIndex = section(programChanges);
    int pitchBendLowIndex = section(pitchBends);
    int nonRegisteredMsbIndex = section(nonRegisteredMsbs);
    int nonRegisteredLsbIndex = section(nonRegisteredLsbs);
    int registeredMsbIndex = section(registeredMsbs);
    int registeredLsbIndex = section(registeredLsbs);
    int tempoIndex = section(tempos * 3);

    MidiWriter out(estimate);
    out.u32(0x4D546864); // MThd
    out.u32(6);
    out.u16(tracks > 1 ? 1 : 0);
    out.u16(tracks);
    out.u16(division);

    Buffer times(bytes);
    times.skip(deltaTimes);
    int statusIndex = 0;
    int channel = 0;
    int key = 0;
    int noteOnVelocity = 0;
    int noteOffVelocity = 0;
    int pitchBend = 0;
    int channelPressure = 0;
    int polyPressure = 0;
    std::array<int, 128> controllerValues{};
    controller = 0;
    for (int track = 0; track < tracks; ++track) {
        out.u32(0x4D54726B); // MTrk
        out.skip4();
        const std::size_t trackStart = out.size();
        int lastKind = -1;
        for (;;) {
            out.varInt(readVarInt(times));
            const int status = at(statusIndex++) & 255;
            // Running status: a channel event's status byte only when it
            // changes. RuneLite compares the whole byte against the last
            // one's low four bits, as the client does.
            const bool changed = status != lastKind;
            const int kind = lastKind = status & 15;
            if (status == kEndOfTrack) {
                // The client writes the 0xFF only when changed, which makes
                // broken MIDI files, so RuneLite always writes it.
                out.byte(0xFF);
                out.byte(0x2F);
                out.byte(0);
                out.lengthFromMark(out.size() - trackStart);
                break;
            }
            if (status == kTempo) {
                out.byte(0xFF); // as for the end of a track
                out.byte(0x51);
                out.byte(3);
                for (int i = 0; i < 3; ++i) out.byte(at(tempoIndex++));
                continue;
            }
            channel ^= status >> 4;
            if (kind == kNoteOn) {
                if (changed) out.byte(0x90 + channel);
                key += at(keyIndex++);
                noteOnVelocity += at(noteOnVelocityIndex++);
                out.byte(key & 127);
                out.byte(noteOnVelocity & 127);
            } else if (kind == kNoteOff) {
                if (changed) out.byte(0x80 + channel);
                key += at(keyIndex++);
                noteOffVelocity += at(noteOffVelocityIndex++);
                out.byte(key & 127);
                out.byte(noteOffVelocity & 127);
            } else if (kind == kControlChange) {
                if (changed) out.byte(0xB0 + channel);
                controller = (controller + at(controllerIndex++)) & 127;
                out.byte(controller);
                int delta = 0;
                if (controller == kBankSelect || controller == kBankSelect2) {
                    delta = at(programChangeIndex++);
                } else if (controller == kModulationWheel) {
                    delta = at(modulationWheelIndex++);
                } else if (controller == kModulationWheel2) {
                    delta = at(modulationWheel2Index++);
                } else if (controller == kChannelVolume) {
                    delta = at(channelVolumeIndex++);
                } else if (controller == kChannelVolume2) {
                    delta = at(channelVolume2Index++);
                } else if (controller == kPan) {
                    delta = at(panIndex++);
                } else if (controller == kPan2) {
                    delta = at(pan2Index++);
                } else if (controller == kNonRegisteredMsb) {
                    delta = at(nonRegisteredMsbIndex++);
                } else if (controller == kNonRegisteredLsb) {
                    delta = at(nonRegisteredLsbIndex++);
                } else if (controller == kRegisteredMsb) {
                    delta = at(registeredMsbIndex++);
                } else if (controller == kRegisteredLsb) {
                    delta = at(registeredLsbIndex++);
                } else if (!isCommand(controller)) {
                    delta = at(otherIndex++);
                } else {
                    delta = at(commandIndex++);
                }
                controllerValues[static_cast<std::size_t>(controller)] += delta;
                out.byte(controllerValues[static_cast<std::size_t>(controller)] & 127);
            } else if (kind == kPitchBend) {
                if (changed) out.byte(0xE0 + channel);
                pitchBend += at(pitchBendLowIndex++);
                pitchBend += at(pitchBendHighIndex++) * 128;
                out.byte(pitchBend & 127);
                out.byte(pitchBend >> 7 & 127);
            } else if (kind == kChannelPressure) {
                if (changed) out.byte(0xD0 + channel);
                channelPressure += at(channelPressureIndex++);
                out.byte(channelPressure & 127);
            } else if (kind == kPolyPressure) {
                if (changed) out.byte(0xA0 + channel);
                key += at(keyIndex++);
                polyPressure += at(polyPressureIndex++);
                out.byte(key & 127);
                out.byte(polyPressure & 127);
            } else if (kind == kProgramChange) {
                if (changed) out.byte(0xC0 + channel);
                out.byte(at(programChangeIndex++));
            } else {
                throw std::runtime_error("music track event of unknown kind " + std::to_string(status));
            }
        }
    }
    return out.take();
}

// RuneLite's way of marking what's optional: it reads a byte and, unless
// it's 0, steps back so that what follows reads it again.
bool present(Buffer& in) {
    Buffer ahead = in;
    if (ahead.u8() != 0) return true;
    in = ahead;
    return false;
}

// RuneLite SoundEffect2Loader.method1144.
void decodeSegments(Buffer& in, SoundEnvelope& envelope) {
    const int count = in.u8();
    envelope.durations.assign(static_cast<std::size_t>(count), 0);
    envelope.phases.assign(static_cast<std::size_t>(count), 0);
    for (int i = 0; i < count; ++i) {
        envelope.durations[static_cast<std::size_t>(i)] = in.u16();
        envelope.phases[static_cast<std::size_t>(i)] = in.u16();
    }
}

// RuneLite SoundEffect2Loader.load.
SoundEnvelope decodeEnvelope(Buffer& in) {
    SoundEnvelope envelope;
    envelope.form = in.u8();
    envelope.start = in.i32();
    envelope.end = in.i32();
    decodeSegments(in, envelope);
    return envelope;
}

// RuneLite SoundEffect3Loader.load: the filter, and the segments of its
// envelope if it changes over the sound.
SoundFilter decodeFilter(Buffer& in, SoundEnvelope& envelope) {
    SoundFilter filter;
    const int pairs = in.u8();
    filter.pairs = {pairs >> 4, pairs & 15};
    if (pairs == 0) return filter;
    filter.unity = {in.u16(), in.u16()};
    const int changing = in.u8();
    // RuneLite's arrays hold four pairs a direction, and throw past them.
    const auto checked = [](int pair) {
        if (pair >= 4) throw std::runtime_error("sound filter with more than four pairs");
        return static_cast<std::size_t>(pair);
    };
    for (std::size_t direction = 0; direction < 2; ++direction) {
        for (int pair = 0; pair < filter.pairs[direction]; ++pair) {
            filter.phases[direction][0][checked(pair)] = in.u16();
            filter.magnitudes[direction][0][checked(pair)] = in.u16();
        }
    }
    for (std::size_t direction = 0; direction < 2; ++direction) {
        for (int pair = 0; pair < filter.pairs[direction]; ++pair) {
            const std::size_t p = checked(pair);
            if ((changing & 1 << direction * 4 << pair) != 0) {
                filter.phases[direction][1][p] = in.u16();
                filter.magnitudes[direction][1][p] = in.u16();
            } else {
                filter.phases[direction][1][p] = filter.phases[direction][0][p];
                filter.magnitudes[direction][1][p] = filter.magnitudes[direction][0][p];
            }
        }
    }
    if (changing != 0 || filter.unity[1] != filter.unity[0]) decodeSegments(in, envelope);
    return filter;
}

// RuneLite SoundEffect1Loader.load.
Instrument decodeInstrument(Buffer& in) {
    Instrument instrument;
    instrument.pitch = decodeEnvelope(in);
    instrument.volume = decodeEnvelope(in);
    if (present(in)) {
        instrument.pitchModifier = decodeEnvelope(in);
        instrument.pitchModifierAmplitude = decodeEnvelope(in);
    }
    if (present(in)) {
        instrument.volumeMultiplier = decodeEnvelope(in);
        instrument.volumeMultiplierAmplitude = decodeEnvelope(in);
    }
    if (present(in)) {
        instrument.release = decodeEnvelope(in);
        instrument.attack = decodeEnvelope(in);
    }
    for (std::size_t i = 0; i < 10; ++i) {
        const int volume = in.unsignedShortSmart();
        if (volume == 0) break;
        // RuneLite reads up to ten into arrays of five, and throws past them.
        if (i >= instrument.oscillatorVolumes.size()) throw std::runtime_error("instrument with more than five oscillators");
        instrument.oscillatorVolumes[i] = volume;
        instrument.oscillatorPitches[i] = in.shortSmart();
        instrument.oscillatorDelays[i] = in.unsignedShortSmart();
    }
    instrument.delayTime = in.unsignedShortSmart();
    instrument.delayDecay = in.unsignedShortSmart();
    instrument.duration = in.u16();
    instrument.offset = in.u16();
    instrument.filter = decodeFilter(in, instrument.filterEnvelope);
    return instrument;
}

// RuneLite SoundEffectLoader.load.
SoundEffect decodeSoundEffect(std::span<const std::uint8_t> bytes) {
    Buffer in(bytes);
    SoundEffect effect;
    for (std::optional<Instrument>& instrument : effect.instruments) {
        if (present(in)) instrument = decodeInstrument(in);
    }
    effect.loopStart = in.u16();
    effect.loopEnd = in.u16();
    return effect;
}

} // namespace

std::optional<std::vector<std::uint8_t>> loadMidi(const Store& store, Index index, int id) {
    const std::optional<std::map<int, std::vector<std::uint8_t>>> files = store.files(index, id);
    if (!files) return std::nullopt;
    const auto file = files->find(0);
    if (file == files->end()) return std::nullopt;
    return decodeOrSkip(store, index == Index::MusicJingles ? "jingle" : "track", id, [&] { return packedTrackToMidi(file->second); });
}

std::optional<SoundEffect> loadSoundEffect(const Store& store, int id) {
    const std::optional<std::map<int, std::vector<std::uint8_t>>> files = store.files(Index::SoundEffects, id);
    if (!files) return std::nullopt;
    const auto file = files->find(0);
    if (file == files->end()) return std::nullopt;
    return decodeOrSkip(store, "sound effect", id, [&] { return decodeSoundEffect(file->second); });
}

} // namespace osrs::cache
