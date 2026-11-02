#include <osrs/cache/store.h>

#include "buffer.h"
#include "undecodable.h"

// bzlib.h includes windows.h on Windows; keep its min and max macros out.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <bzlib.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <stdexcept>
#include <string>

namespace osrs::cache {

namespace {

constexpr int kSectorSize = 520;
constexpr int kIndexEntrySize = 6;
constexpr int kReferenceIndex = 255;
constexpr std::string_view kFlatExtension = ".flatcache";

[[noreturn]] void fail(int index, int group, const std::string& what) {
    throw std::runtime_error("cache group " + std::to_string(index) + "/" + std::to_string(group) + ": " + what);
}

[[noreturn]] void failContainer(const std::string& what) { throw std::runtime_error("cache container: " + what); }

std::uint32_t crcOf(std::span<const std::uint8_t> bytes) {
    return static_cast<std::uint32_t>(crc32(0, bytes.data(), static_cast<uInt>(bytes.size())));
}

std::vector<std::uint8_t> gunzip(std::span<const std::uint8_t> in, std::size_t size) {
    std::vector<std::uint8_t> out(size);
    z_stream stream{};
    // 16 + MAX_WBITS: a gzip header and trailer around the deflate stream.
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) failContainer("zlib won't start");
    stream.next_in = const_cast<Bytef*>(in.data());
    stream.avail_in = static_cast<uInt>(in.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    const int result = inflate(&stream, Z_FINISH);
    const uLong produced = stream.total_out;
    inflateEnd(&stream);
    if (result != Z_STREAM_END || produced != size) failContainer("gzip data doesn't decompress");
    return out;
}

std::vector<std::uint8_t> bunzip(std::span<const std::uint8_t> in, std::size_t size) {
    // The cache leaves out the stream header, which is always "BZh1".
    std::vector<char> stream = {'B', 'Z', 'h', '1'};
    stream.insert(stream.end(), in.begin(), in.end());
    std::vector<std::uint8_t> out(size);
    unsigned int produced = static_cast<unsigned int>(size);
    if (BZ2_bzBuffToBuffDecompress(reinterpret_cast<char*>(out.data()), &produced, stream.data(),
                                   static_cast<unsigned int>(stream.size()), 0, 0) != BZ_OK
        || produced != size) {
        failContainer("bzip2 data doesn't decompress");
    }
    return out;
}

std::uint32_t bigEndian32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} << 24 | std::uint32_t{p[1]} << 16 | std::uint32_t{p[2]} << 8 | p[3];
}

void putBigEndian32(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value >> 24);
    p[1] = static_cast<std::uint8_t>(value >> 16);
    p[2] = static_cast<std::uint8_t>(value >> 8);
    p[3] = static_cast<std::uint8_t>(value);
}

// RuneLite util/Xtea.decrypt: 32 rounds on each whole 8-byte block, big
// endian; the bytes past the last whole block stay as they are.
void xteaDecrypt(std::span<std::uint8_t> data, const XteaKey& key) {
    constexpr std::uint32_t kGoldenRatio = 0x9E3779B9;
    constexpr int kRounds = 32;
    const auto k = [&](std::uint32_t i) { return static_cast<std::uint32_t>(key[i & 3]); };
    for (std::size_t block = 0; block + 8 <= data.size(); block += 8) {
        std::uint32_t v0 = bigEndian32(&data[block]);
        std::uint32_t v1 = bigEndian32(&data[block + 4]);
        std::uint32_t sum = kGoldenRatio * kRounds;
        for (int round = 0; round < kRounds; ++round) {
            v1 -= ((v0 << 4 ^ v0 >> 5) + v0) ^ (sum + k(sum >> 11));
            sum -= kGoldenRatio;
            v0 -= ((v1 << 4 ^ v1 >> 5) + v1) ^ (sum + k(sum));
        }
        putBigEndian32(&data[block], v0);
        putBigEndian32(&data[block + 4], v1);
    }
}

// Java's Integer.parseInt: an optional sign, then decimal digits, in range.
std::int32_t parseJavaInt(std::string_view text) {
    std::string_view digits = text;
    if (!digits.empty() && digits.front() == '+') digits.remove_prefix(1);
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (digits.empty() || digits.front() == '+' || error != std::errc() || end != digits.data() + digits.size()
        || value < INT32_MIN || value > INT32_MAX) {
        throw std::runtime_error("not an int: \"" + std::string(text) + "\"");
    }
    return static_cast<std::int32_t>(value);
}

// Java's Base64.getDecoder(): the basic alphabet, padding optional but
// correct where present, nothing after it.
std::vector<std::uint8_t> decodeBase64(std::string_view text) {
    const auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::size_t length = text.find('=');
    if (length == std::string_view::npos) length = text.size();
    const std::string_view padding = text.substr(length);
    const std::size_t tail = length % 4;
    if (tail == 1 || (!padding.empty() && (tail == 0 || padding.size() != 4 - tail || padding.find_first_not_of('=') != std::string_view::npos))) {
        throw std::runtime_error("malformed base64");
    }
    std::vector<std::uint8_t> out;
    out.reserve(length / 4 * 3 + 2);
    std::uint32_t bits = 0;
    int count = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const int v = value(text[i]);
        if (v < 0) throw std::runtime_error("malformed base64");
        bits = bits << 6 | static_cast<std::uint32_t>(v);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out.push_back(static_cast<std::uint8_t>(bits >> count));
        }
    }
    return out;
}

// RuneLite fs/ArchiveFiles.loadContents: the files' bytes come in chunks,
// and the trailer gives each file's size in each chunk as deltas.
std::map<int, std::vector<std::uint8_t>> split(const std::vector<std::uint8_t>& data, const std::vector<int>& fileIds,
                                               int index, int group) {
    std::map<int, std::vector<std::uint8_t>> files;
    if (fileIds.size() == 1) {
        files.emplace(fileIds.front(), data);
        return files;
    }
    if (data.empty()) fail(index, group, "no file table");
    const std::size_t chunks = data.back();
    const std::size_t tableSize = chunks * fileIds.size() * 4;
    if (tableSize + 1 > data.size()) fail(index, group, "file table ends early");
    const std::size_t tableStart = data.size() - 1 - tableSize;
    Buffer table(std::span(data).subspan(tableStart, tableSize));
    std::vector<std::vector<std::size_t>> chunkSizes(fileIds.size(), std::vector<std::size_t>(chunks));
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
        std::int64_t size = 0;
        for (std::size_t file = 0; file < fileIds.size(); ++file) {
            size += table.i32();
            if (size < 0) fail(index, group, "negative file size");
            chunkSizes[file][chunk] = static_cast<std::size_t>(size);
        }
    }
    std::vector<std::vector<std::uint8_t>> contents(fileIds.size());
    std::size_t offset = 0;
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
        for (std::size_t file = 0; file < fileIds.size(); ++file) {
            const std::size_t size = chunkSizes[file][chunk];
            if (size > tableStart - offset) fail(index, group, "file data overruns the table");
            contents[file].insert(contents[file].end(), data.begin() + offset, data.begin() + offset + size);
            offset += size;
        }
    }
    for (std::size_t file = 0; file < fileIds.size(); ++file) files.emplace(fileIds[file], std::move(contents[file]));
    return files;
}

// RuneLite index/IndexData.load: protocols 5 to 7.
IndexInfo parseReferenceTable(const std::vector<std::uint8_t>& data) {
    Buffer in(data);
    IndexInfo table;
    table.protocol = in.u8();
    if (table.protocol < 5 || table.protocol > 7) throw std::runtime_error("unsupported reference table protocol " + std::to_string(table.protocol));
    if (table.protocol >= 6) table.revision = in.i32();
    const int flags = in.u8();
    constexpr int kNamed = 1;
    constexpr int kSized = 4;
    if ((flags & ~(kNamed | kSized)) != 0) throw std::runtime_error("unknown reference table flags " + std::to_string(flags));
    table.named = (flags & kNamed) != 0;
    table.sized = (flags & kSized) != 0;
    const auto count = [&] { return table.protocol >= 7 ? in.bigSmart() : in.u16(); };

    std::vector<GroupInfo> groups(count());
    int last = 0;
    for (GroupInfo& group : groups) group.id = last += count();
    if (table.named) {
        for (GroupInfo& group : groups) group.nameHash = in.i32();
    }
    for (GroupInfo& group : groups) group.crc = static_cast<std::uint32_t>(in.i32());
    if (table.sized) {
        for (GroupInfo& group : groups) {
            group.compressedSize = in.i32();
            group.decompressedSize = in.i32();
        }
    }
    for (GroupInfo& group : groups) group.revision = in.i32();
    for (GroupInfo& group : groups) group.fileIds.resize(count());
    for (GroupInfo& group : groups) {
        int lastFile = 0;
        for (int& file : group.fileIds) file = lastFile += count();
    }
    if (table.named) {
        for (GroupInfo& group : groups) {
            group.fileNameHashes.resize(group.fileIds.size());
            for (std::int32_t& hash : group.fileNameHashes) hash = in.i32();
        }
    }
    for (GroupInfo& group : groups) {
        const int id = group.id;
        table.groups.emplace(id, std::move(group));
    }
    return table;
}

// RuneLite fs/flat/FlatStorage.load, for one index: key=value lines, the
// index's own keys first, then from each "id" line that group's keys; a run
// of "file=<id>=<name hash>" lines becomes the group's files when the next
// other key, or the end, comes. RuneLite's fs/Index defaults stand for keys a
// file leaves out.
template <class Table>
Table parseFlat(std::istream& in) {
    Table table;
    IndexInfo& info = table.info;
    info.protocol = 6;
    info.named = true;
    GroupInfo* group = nullptr;
    std::optional<std::vector<std::pair<int, std::int32_t>>> files;
    const auto takeFiles = [&] {
        if (group == nullptr) throw std::runtime_error("files before any group");
        group->fileIds.clear();
        group->fileNameHashes.clear();
        for (const auto& [id, nameHash] : *files) {
            group->fileIds.push_back(id);
            group->fileNameHashes.push_back(nameHash);
        }
        files.reset();
    };
    std::string line;
    for (int lineNo = 1; std::getline(in, line); ++lineNo) {
        try {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::size_t split = line.find('=');
            if (split == std::string::npos) throw std::runtime_error("no '='");
            const std::string_view key = std::string_view(line).substr(0, split);
            const std::string_view value = std::string_view(line).substr(split + 1);
            if (key == "file") {
                const std::size_t fileSplit = value.find('=');
                if (fileSplit == std::string_view::npos) throw std::runtime_error("no '=' in file");
                if (!files) files.emplace();
                files->emplace_back(parseJavaInt(value.substr(0, fileSplit)), parseJavaInt(value.substr(fileSplit + 1)));
                continue;
            }
            if (files) takeFiles();
            if (key == "id") {
                const int id = parseJavaInt(value);
                const auto [added, isNew] = info.groups.try_emplace(id);
                if (!isNew) throw std::runtime_error("group " + std::to_string(id) + " already exists");
                group = &added->second;
                group->id = id;
                continue;
            }
            if (group == nullptr) {
                if (key == "protocol") {
                    info.protocol = parseJavaInt(value);
                    continue;
                }
                if (key == "revision") {
                    info.revision = parseJavaInt(value);
                    continue;
                }
                if (key == "compression") {
                    info.compression = parseJavaInt(value);
                    continue;
                }
                if (key == "crc") {
                    info.crc = static_cast<std::uint32_t>(parseJavaInt(value));
                    continue;
                }
                if (key == "named") {
                    // Java's Boolean.parseBoolean.
                    std::string lower(value);
                    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    info.named = lower == "true";
                    continue;
                }
            } else {
                if (key == "namehash") {
                    group->nameHash = parseJavaInt(value);
                    continue;
                }
                if (key == "revision") {
                    group->revision = parseJavaInt(value);
                    continue;
                }
                if (key == "crc") {
                    group->crc = static_cast<std::uint32_t>(parseJavaInt(value));
                    continue;
                }
                if (key == "compression") {
                    group->compression = parseJavaInt(value);
                    continue;
                }
                if (key == "contents") {
                    table.containers[group->id] = decodeBase64(value);
                    continue;
                }
            }
            throw std::runtime_error("unknown key \"" + std::string(key) + "\"");
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("line " + std::to_string(lineNo) + ": " + error.what());
        }
    }
    if (files) takeFiles();
    return table;
}

} // namespace

Container decompressContainer(std::span<const std::uint8_t> stored, const std::optional<XteaKey>& key) {
    Buffer in(stored);
    Container container;
    container.compression = in.u8();
    const std::int32_t length = in.i32();
    if (length < 0) failContainer("negative length");
    if (container.compression > 2) failContainer("unknown compression " + std::to_string(container.compression));
    // A compressed body starts with its decompressed length, which the
    // encryption and the checksum cover too.
    const std::size_t bodySize = static_cast<std::size_t>(length) + (container.compression == 0 ? 0 : 4);
    if (in.remaining() < bodySize) failContainer("container ends early");
    container.crc = crcOf(stored.first(5 + bodySize));
    std::vector<std::uint8_t> body(stored.begin() + 5, stored.begin() + 5 + static_cast<std::ptrdiff_t>(bodySize));
    in.skip(bodySize);
    if (key) xteaDecrypt(body, *key);
    if (container.compression == 0) {
        container.data = std::move(body);
    } else {
        const std::int32_t size = static_cast<std::int32_t>(bigEndian32(body.data()));
        if (size < 0) failContainer("negative decompressed length");
        const std::span<const std::uint8_t> compressed = std::span(body).subspan(4);
        container.data = container.compression == 1 ? bunzip(compressed, size) : gunzip(compressed, size);
    }
    if (in.remaining() >= 4) {
        container.revision = in.i32();
    } else if (in.remaining() >= 2) {
        container.revision = in.u16();
    }
    return container;
}

void reportSkipped(const Store& store, std::string_view type, std::int64_t id, const std::runtime_error& error) {
    if (store.options().onSkipped) store.options().onSkipped(SkippedObject{type, id, error.what()});
}

void undecodable(const Store& store, std::string_view type, std::int64_t id, const std::runtime_error& error) {
    if (store.options().undecodable == Undecodable::Throw) {
        throw std::runtime_error(std::string(type) + " " + std::to_string(id) + ": " + error.what());
    }
    reportSkipped(store, type, id, error);
}

std::int32_t djb2(std::string_view name) {
    std::uint32_t hash = 0;
    for (const char c : name) hash = static_cast<std::uint8_t>(c) + ((hash << 5) - hash);
    return static_cast<std::int32_t>(hash);
}

std::optional<std::filesystem::path> clientCacheDirectory() {
#ifdef _WIN32
    wchar_t* local = nullptr;
    std::size_t length = 0;
    std::optional<std::filesystem::path> directory;
    if (_wdupenv_s(&local, &length, L"LOCALAPPDATA") == 0 && local != nullptr) {
        directory = std::filesystem::path(local) / "Jagex" / "Old School Runescape" / "data";
    }
    std::free(local);
    return directory;
#else
    return std::nullopt;
#endif
}

Store::Store(const std::filesystem::path& directory, StoreOptions options) : directory_(directory), options_(std::move(options)) {
    if (std::filesystem::is_regular_file(directory / "main_file_cache.dat2")) return;
    std::error_code unreadable;
    for (const auto& entry : std::filesystem::directory_iterator(directory, unreadable)) {
        const std::string name = entry.path().filename().string();
        if (!name.ends_with(kFlatExtension)) continue;
        std::int32_t id = -1;
        try {
            id = parseJavaInt(std::string_view(name).substr(0, name.size() - kFlatExtension.size()));
        } catch (const std::runtime_error&) {
        }
        if (id < 0 || id > 0xFF) throw std::runtime_error("flat cache file " + entry.path().string() + " isn't named by an index id");
        flatIndices_.push_back(id);
    }
    if (flatIndices_.empty()) throw std::runtime_error("no game cache in " + directory.string());
    std::sort(flatIndices_.begin(), flatIndices_.end());
    // RuneLite's Store.addIndex refuses an index twice, as "5" and "05" name.
    if (std::adjacent_find(flatIndices_.begin(), flatIndices_.end()) != flatIndices_.end()) {
        throw std::runtime_error("flat cache in " + directory.string() + " names an index twice");
    }
    flat_ = true;
}

std::vector<Index> Store::indices() const {
    std::vector<Index> ids;
    if (flat_) {
        for (const int id : flatIndices_) ids.push_back(static_cast<Index>(id));
        return ids;
    }
    // RuneLite's DiskStorage.init: index 255 holds an entry for each index.
    std::error_code missing;
    const std::uintmax_t size = std::filesystem::file_size(directory_ / "main_file_cache.idx255", missing);
    for (std::uintmax_t id = 0; !missing && id < size / kIndexEntrySize && id < kReferenceIndex; ++id) ids.push_back(static_cast<Index>(id));
    return ids;
}

const Store::Table* Store::table(int index) const {
    const std::lock_guard lock(mutex_);
    const auto [known, added] = tables_.try_emplace(index);
    if (added) {
        try {
            if (flat_) {
                if (std::binary_search(flatIndices_.begin(), flatIndices_.end(), index)) {
                    const std::filesystem::path file = directory_ / (std::to_string(index) + std::string(kFlatExtension));
                    std::ifstream in(file, std::ios::binary);
                    if (!in) throw std::runtime_error("can't open " + file.string());
                    known->second = parseFlat<Table>(in);
                }
            } else if (const std::optional<std::vector<std::uint8_t>> stored = container(kReferenceIndex, index)) {
                const Container unpacked = decompressContainer(*stored);
                Table table;
                table.info = parseReferenceTable(unpacked.data);
                table.info.crc = unpacked.crc;
                table.info.compression = unpacked.compression;
                known->second = std::move(table);
            }
        } catch (const std::runtime_error& error) {
            tables_.erase(known);
            throw std::runtime_error("reference table of index " + std::to_string(index) + " in " + directory_.string() + ": " + error.what());
        }
    }
    return known->second ? &*known->second : nullptr;
}

std::vector<int> Store::groups(Index index) const {
    std::vector<int> ids;
    if (const IndexInfo* info = this->index(index)) {
        for (const auto& [id, group] : info->groups) ids.push_back(id);
    }
    return ids;
}

const IndexInfo* Store::index(Index index) const {
    const Table* found = table(static_cast<int>(index));
    return found ? &found->info : nullptr;
}

std::optional<int> Store::findGroup(Index index, std::string_view name) const {
    const IndexInfo* info = this->index(index);
    if (info == nullptr || !info->named) return std::nullopt;
    const std::int32_t hash = djb2(name);
    for (const auto& [id, group] : info->groups) {
        if (group.nameHash == hash) return id;
    }
    return std::nullopt;
}

// RuneLite Archive.decompress: the checksum must be the reference table's.
// RuneLite also takes the container's revision trailer as the group's
// revision where they differ; the reference table stays as the cache gives it.
std::optional<std::vector<std::uint8_t>> Store::contents(Index index, int group, const std::optional<XteaKey>& key) const {
    const int indexId = static_cast<int>(index);
    const IndexInfo* info = this->index(index);
    if (info == nullptr) return std::nullopt;
    const auto entry = info->groups.find(group);
    if (entry == info->groups.end()) return std::nullopt;
    const std::optional<std::vector<std::uint8_t>> stored = container(indexId, group);
    if (!stored) return std::nullopt;
    Container unpacked;
    try {
        unpacked = decompressContainer(*stored, key);
    } catch (const std::runtime_error& error) {
        fail(indexId, group, error.what());
    }
    if (unpacked.crc != entry->second.crc) fail(indexId, group, "checksum differs from the reference table's");
    return std::move(unpacked.data);
}

std::optional<std::map<int, std::vector<std::uint8_t>>> Store::filesWith(Index index, int group,
                                                                        const std::optional<XteaKey>& key) const {
    const IndexInfo* info = this->index(index);
    if (info == nullptr) return std::nullopt;
    const auto entry = info->groups.find(group);
    if (entry == info->groups.end() || entry->second.fileIds.empty()) return std::nullopt;
    const std::optional<std::vector<std::uint8_t>> data = contents(index, group, key);
    if (!data) return std::nullopt;
    return split(*data, entry->second.fileIds, static_cast<int>(index), group);
}

std::optional<std::map<int, std::vector<std::uint8_t>>> Store::files(Index index, int group) const {
    return filesWith(index, group, std::nullopt);
}

std::optional<std::map<int, std::vector<std::uint8_t>>> Store::files(Index index, int group, const XteaKey& key) const {
    return filesWith(index, group, key);
}

// A flat cache's from its table; a disk cache's as RuneLite's
// fs/jagex/IndexFile.read and DataFile.read read it: the index entry gives
// the size and first sector; each 520-byte sector starts with a header naming
// its group, its place in the chain, the next sector and its index.
std::optional<std::vector<std::uint8_t>> Store::container(int index, int group) const {
    if (flat_) {
        const Table* found = table(index);
        if (found == nullptr) return std::nullopt;
        const auto stored = found->containers.find(group);
        if (stored == found->containers.end()) return std::nullopt;
        return stored->second;
    }
    std::ifstream idx(directory_ / ("main_file_cache.idx" + std::to_string(index)), std::ios::binary);
    if (!idx) return std::nullopt;
    std::uint8_t entry[kIndexEntrySize];
    idx.seekg(static_cast<std::streamoff>(group) * kIndexEntrySize);
    if (!idx.read(reinterpret_cast<char*>(entry), kIndexEntrySize)) return std::nullopt;
    const std::size_t size = std::size_t{entry[0]} << 16 | std::size_t{entry[1]} << 8 | entry[2];
    std::int64_t sector = std::int64_t{entry[3]} << 16 | std::int64_t{entry[4]} << 8 | entry[5];
    // RuneLite's IndexFile.read takes either as no entry.
    if (size == 0 || sector == 0) return std::nullopt;

    std::ifstream dat(directory_ / "main_file_cache.dat2", std::ios::binary);
    dat.seekg(0, std::ios::end);
    // The last sector may be partial, so it counts as well.
    const std::int64_t lastSector = static_cast<std::int64_t>(dat.tellg()) / kSectorSize;
    const bool wideId = group > 0xFFFF;
    const std::size_t headerSize = wideId ? 10 : 8;
    std::vector<std::uint8_t> out;
    out.reserve(size);
    std::uint8_t buffer[kSectorSize];
    for (int part = 0; out.size() < size; ++part) {
        if (sector <= 0 || sector > lastSector) fail(index, group, "sector out of range");
        dat.seekg(sector * kSectorSize);
        const std::size_t length = std::min(size - out.size(), kSectorSize - headerSize);
        if (!dat.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(headerSize + length))) fail(index, group, "unreadable sector");
        Buffer header(std::span(buffer, headerSize));
        const int storedGroup = wideId ? header.i32() : header.u16();
        const int storedPart = header.u16();
        const std::int64_t next = header.u24();
        const int storedIndex = header.u8();
        if (storedGroup != group || storedPart != part || storedIndex != index) fail(index, group, "broken sector chain");
        out.insert(out.end(), buffer + headerSize, buffer + headerSize + length);
        sector = next;
    }
    return out;
}

} // namespace osrs::cache
