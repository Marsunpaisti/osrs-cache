#pragma once

// The game's cache as it lies on disk: main_file_cache.dat2, the data, and
// main_file_cache.idx<N>, where each index's groups start in it. Index 255
// holds each index's reference table: its group ids, the files in each group
// and each group's checksum. A group is stored as one container, compressed
// with gzip or bzip2 or not at all, and encrypted with XTEA where the game
// encrypted it (older caches' map locations); a group of several files packs
// them into one buffer.
//
// The format follows RuneLite's cache library (net.runelite:cache 1.13.5,
// fs/jagex/DiskStorage, fs/flat/FlatStorage, index/IndexData, fs/Container,
// fs/Archive, fs/ArchiveFiles, util/Xtea). It reads but never writes.

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace osrs::cache {

// The cache's indices, as RuneLite's IndexType names them.
enum class Index : std::uint8_t {
    Frames = 0,
    Skeletons = 1,
    Configs = 2,
    Interfaces = 3,
    SoundEffects = 4,
    Maps = 5,
    MusicTracks = 6,
    Models = 7,
    Sprites = 8,
    Textures = 9,
    Binary = 10,
    MusicJingles = 11,
    ClientScripts = 12,
    Fonts = 13,
    MusicSamples = 14,
    MusicPatches = 15,
    WorldMapGeography = 18,
    WorldMap = 19,
    WorldMapGround = 20,
    DbTableIndices = 21,
    SkeletalAnimations = 22,
    GameVals = 24,
};

// The groups of the configs index, as RuneLite's ConfigType names them.
enum class ConfigGroup : std::uint16_t {
    Underlays = 1,
    Kits = 3,
    Overlays = 4,
    Inventories = 5,
    Objects = 6,
    Enums = 8,
    Npcs = 9,
    Items = 10,
    Params = 11,
    Sequences = 12,
    SpotAnims = 13,
    Varbits = 14,
    VarClientStrings = 15,
    VarPlayers = 16,
    VarClients = 19,
    Hitsplats = 32,
    HealthBars = 33,
    Structs = 34,
    Areas = 35,
    DbRows = 38,
    DbTables = 39,
};

// A group's entry in its index's reference table (RuneLite fs/Archive).
struct GroupInfo {
    int id = 0;
    // The djb2 hash of the group's name, if its index names its groups.
    std::int32_t nameHash = 0;
    std::uint32_t crc = 0;
    // If the index records sizes.
    std::int32_t compressedSize = 0;
    std::int32_t decompressedSize = 0;
    std::int32_t revision = 0;
    std::vector<int> fileIds;
    // Each file's name hash, in fileIds' order, if the index names its groups
    // or the cache is flat, which records them for every index.
    std::vector<std::int32_t> fileNameHashes;
    // The compression of the group's container (as Container::compression)
    // where a flat cache records it; 0 in a disk cache, whose reference table
    // doesn't give it. RuneLite sets its Archive's when it decompresses the
    // group; this stays as the cache gives it.
    int compression = 0;
};

// An index's reference table (RuneLite index/IndexData, fs/Index).
struct IndexInfo {
    int protocol = 0;
    // 0 before protocol 6. Some of RuneLite's loaders pick their format by
    // it, or by a group's revision.
    std::int32_t revision = 0;
    bool named = false;
    // A flat cache doesn't record it, so it's false there.
    bool sized = false;
    // The checksum and compression of the container index 255 holds the
    // table in; a flat cache records them in its place.
    std::uint32_t crc = 0;
    int compression = 0;
    std::map<int, GroupInfo> groups;
};

// The hash the cache names groups and files by (RuneLite util/Djb2).
std::int32_t djb2(std::string_view name);

// An XTEA key: four ints, as RuneLite's util/Xtea and the published key
// files give them.
using XteaKey = std::array<std::int32_t, 4>;

// A group as it is stored, unpacked (RuneLite fs/Container).
struct Container {
    // 0 none, 1 bzip2, 2 gzip (RuneLite fs/jagex/CompressionType).
    int compression = 0;
    // The group's revision, from the trailer some containers carry.
    std::optional<std::int32_t> revision;
    // Of the stored bytes less the trailer, before decryption: what the
    // reference table's checksum is compared with.
    std::uint32_t crc = 0;
    // Decrypted and decompressed.
    std::vector<std::uint8_t> data;
};

// RuneLite Container.decompress: `key` decrypts the body, and the
// decompressed length ahead of it, for a container stored encrypted. It
// decrypts with any key it's given, all zeros too. Throws std::runtime_error
// if the container is damaged or doesn't decompress, as with a wrong key.
Container decompressContainer(std::span<const std::uint8_t> stored, const std::optional<XteaKey>& key = std::nullopt);

// Where the official client keeps its cache:
// %LOCALAPPDATA%\Jagex\Old School Runescape\data. Nothing if LOCALAPPDATA is
// unset, or off Windows.
std::optional<std::filesystem::path> clientCacheDirectory();

// What the loaders do with an object they can't decode: one with an opcode
// this library doesn't know, which a game update can add, or one whose data
// ends early. A group that can't be read at all (Store::files throws) fails
// every loader that needs it either way.
enum class Undecodable {
    // Throw std::runtime_error naming the object, failing the whole load: for
    // offline tools, which should stop and be updated.
    Throw,
    // Leave the object out, as if the cache lacked it (a loader of one
    // object returns nothing), and report it to StoreOptions::onSkipped: for
    // a live client, which should keep working with everything else.
    // RuneLite's loaders instead log an unknown opcode and read on, so the
    // rest of that object is garbage.
    Skip,
};

// An object a loader left out: its type as the loader names it ("item",
// "model"), its id, and why it didn't decode. The views are valid only
// during the onSkipped call: copy what is kept.
struct SkippedObject {
    std::string_view type;
    std::int64_t id = 0;
    std::string_view error;
};

struct StoreOptions {
    Undecodable undecodable = Undecodable::Throw;
    // Called for each object a loader leaves out, from the loader's thread,
    // so concurrently when loaders on several threads share the store; may
    // be empty. loadFrames leaves out the frames it can't decode, and
    // reports them, whatever `undecodable` says (frames.h).
    std::function<void(const SkippedObject&)> onSkipped;
};

class Store {
public:
    // A disk cache (main_file_cache.dat2), or else a flat one: RuneLite's
    // fs/flat/FlatStorage, an <index>.flatcache text file per index holding
    // its reference table and its groups' containers in base64. Throws
    // std::runtime_error if the directory holds neither, or a .flatcache file
    // isn't named by an index id. Each index's reference table is read the
    // first time the index is used, a flat index's containers with it, and
    // throws std::runtime_error then if it doesn't parse.
    explicit Store(const std::filesystem::path& directory, StoreOptions options = {});

    const StoreOptions& options() const { return options_; }

    // The indices the cache has, ascending, as RuneLite's Store lists them:
    // a disk cache's are those index 255 has room for, with or without a
    // reference table.
    std::vector<Index> indices() const;

    // The ids of the groups an index has, ascending; none if it has no
    // reference table.
    std::vector<int> groups(Index index) const;

    // The index's reference table; nothing if it has none.
    const IndexInfo* index(Index index) const;

    // The group the index names `name` (RuneLite Index.findArchiveByName);
    // nothing if the index doesn't name its groups or has no such group.
    std::optional<int> findGroup(Index index, std::string_view name) const;

    // A group's files by file id; nothing if the index has no such group.
    // Throws std::runtime_error if the group is damaged: a broken sector
    // chain, a checksum that differs from the reference table's (as when the
    // client was writing it) or a container that doesn't decompress.
    // Thread-safe: each call opens the files it reads, or reads a flat
    // cache's containers held in memory.
    std::optional<std::map<int, std::vector<std::uint8_t>>> files(Index index, int group) const;
    // As above, the container decrypted with `key` (RuneLite
    // Archive.getFiles with keys).
    std::optional<std::map<int, std::vector<std::uint8_t>>> files(Index index, int group, const XteaKey& key) const;

    // A group's contents as one buffer, before it is split into files
    // (RuneLite Archive.decompress); nothing if the index has no such group
    // or the cache holds no container for it. Throws as files() does.
    std::optional<std::vector<std::uint8_t>> contents(Index index, int group,
                                                      const std::optional<XteaKey>& key = std::nullopt) const;

private:
    struct Table {
        IndexInfo info;
        // A flat cache's containers by group id; a disk cache's stay on disk.
        std::map<int, std::vector<std::uint8_t>> containers;
    };

    // The index's reference table, read on first use; nothing if it has
    // none.
    const Table* table(int index) const;
    // The stored container; nothing if the cache has none for the group.
    std::optional<std::vector<std::uint8_t>> container(int index, int group) const;
    std::optional<std::map<int, std::vector<std::uint8_t>>> filesWith(Index index, int group,
                                                                      const std::optional<XteaKey>& key) const;

    std::filesystem::path directory_;
    StoreOptions options_;
    // A flat cache's index ids; empty for a disk cache.
    std::vector<int> flatIndices_;
    bool flat_ = false;
    mutable std::mutex mutex_;
    // By index; an empty optional for an index without a reference table.
    mutable std::map<int, std::optional<Table>> tables_;
};

} // namespace osrs::cache
