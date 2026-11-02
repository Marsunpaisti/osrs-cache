# osrs_cache

Reads Old School RuneScape's game cache as the official client stores it on
disk (`main_file_cache.dat2` and its `.idx` files), and decodes everything
RuneLite's cache library (`net.runelite:cache` 1.13.5) reads from it, field
for field. C++20, read-only, no network.

This folder is self-contained: copy it into another project as it is.

## Adding it

```cmake
add_subdirectory(osrs_cache)
target_link_libraries(your_target PRIVATE osrs_cache)
```

Configuring it fetches zlib 1.3.1 and bzip2 1.0.8, pinned by checksum, into
the build tree. Configured on its own (`cmake -S . -B build`), it also
builds `osrs_cache_tests`; `-DOSRS_CACHE_DIR=<cache directory>` points them at
a cache, and they pass without running when it's unset. Their expected values
are the game's as of the cache they were written against (2026-11-01), so a
later cache fails some of them.

## After a game update: choose what an unreadable object does

Any weekly update can add an opcode or change a format. A definition in
`definitions.h` with an opcode this library doesn't know can't be read past
it, so that object fails to decode; the types in `configs.h` read past one,
as RuneLite does, and fail only if their data then ends early. What happens
to an object that fails is chosen when the cache is opened:

```cpp
// The default: the whole load throws, naming the object. For offline tools
// and scripts, which should stop until the library is updated.
osrs::cache::Store store(directory);

// Leave the object out and report it. For a live client, which should keep
// working with everything else.
osrs::cache::Store store(directory, {osrs::cache::Undecodable::Skip,
    [](const osrs::cache::SkippedObject& skipped) { /* log skipped.type, skipped.id, skipped.error */ }});
```

With `Skip`, a loader of many objects (`loadItemDefinitions`) leaves the
object out, and a loader of one (`loadModel`) returns nothing for it, as if
the cache lacked it. Either way, a group that can't be read at all (a
damaged file, or one the client is writing) throws. RuneLite does neither:
it logs the opcode and reads on, so the rest of that object comes out
garbage. `loadFrames` always leaves out the frames it can't decode, as
RuneLite fails on them too, and reports them.

## Reading

| Header | What it reads |
|---|---|
| `osrs/cache/store.h` | `Store`: a cache's indices and groups, from the client's files or RuneLite's flat format; each index's reference table (revisions, names, sizes); groups by name; XTEA-encrypted groups; `clientCacheDirectory()`, where the official client keeps its cache on Windows |
| `osrs/cache/definitions.h` | items, NPCs and objects with their menu options, sequences, kits, spot animations, underlays and overlays, hitsplats and health bars |
| `osrs/cache/configs.h` | enums, params, varbits, structs, inventories, areas, database tables, rows and indices, and game values |
| `osrs/cache/interfaces.h` | interface components, in both formats |
| `osrs/cache/scripts.h` | client scripts, and their disassembly as RuneLite writes it |
| `osrs/cache/map.h` | each region's map and placed objects, raw and as RuneLite's `Region` holds them, with encrypted locations given their keys |
| `osrs/cache/collision.h` | static collision flags built from the maps (not in RuneLite) |
| `osrs/cache/model.h`, `frames.h` | models with their normals and texture coordinates, frame maps and animation frames |
| `osrs/cache/skeletal.h` | skeletal animations (not in RuneLite) |
| `osrs/cache/sprites.h` | sprites, fonts, and textures with their pixels |
| `osrs/cache/worldmap.h` | the world map: its areas, composite maps, elements and intermap links |
| `osrs/cache/audio.h` | music tracks and jingles as MIDI files, and sound effects |

The functions throw `std::runtime_error` on damaged data. A `Store` reads
each index's reference table once, on first use, and the groups themselves
on every call. A group the game client rewrites while a `Store` is open no
longer matches its checksum and throws, so open a new `Store` after the
client updates its cache.

Where RuneLite picks a format by the cache's revision, so does this library.
The older formats are ported but only unit-tested, as the current cache has
none. Deliberate departures from RuneLite, each noted where it's made:
object offsets are read signed, as the client reads them; an unknown opcode
in a `definitions.h` type fails its object (above) instead of being read
past; and animation frames of
more than 500 transforms decode, where RuneLite's fail.

## Checked against RuneLite

`-DOSRS_CACHE_ORACLE=ON` (Java 11 or later) fetches RuneLite's cache library
1.13.5 and the jars it runs on, pinned by checksum, and builds
`osrs_cache_dump` and the `osrs_cache_oracle` test. The oracle
(`oracle/RuneLiteOracle.java`) has RuneLite decode the same cache, each type
loaded as RuneLite's managers load it, and compares every object's fields with
this library's. On the 2026-11-01 cache all 41 types agree, including where
RuneLite fails to decode an object; the storage and region types also agree
on RuneLite's flat copy of it and on a 2026-03-18 cache from the OpenRS2
archive, whose maps are named and encrypted (`--xtea <keys.json>`). After a
game update, run it on the new cache, with a newer RuneLite if Jagex changed
a format, to see what changed:

```
java -cp "<build>/_deps/runelite-cache-1.13.5/*" oracle/RuneLiteOracle.java <cache> <build>/Release/osrs_cache_dump.exe [type[:key,...] ...]
```

## Licences

Most of the code is ported from RuneLite's cache library
(`LICENSE-runelite.txt`, BSD 2-Clause and one MIT file), and the skeletal
animations from rs-map-viewer (`LICENSE-rs-map-viewer.txt`, BSD 2-Clause).
zlib and bzip2 come under their own licences. The game cache is Jagex's and is
not part of this library.
