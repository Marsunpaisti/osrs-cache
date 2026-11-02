// Checks osrs_cache against RuneLite's cache library, the decoder it ports:
// both decode the same cache, and every object's fields are compared.
//
//   java -cp "<build>/_deps/runelite-cache-1.13.5/*" RuneLiteOracle.java <cache dir> <osrs_cache_dump> [--xtea <keys.json>] [type[:key,...] ...]
//
// With no types, it checks every type below. A type with keys (item:4151,
// interface:149.0) prints every difference those objects have instead of a
// summary. With --count in place of osrs_cache_dump, it only counts what
// RuneLite decodes. Java 11 or later runs it as it is, without compiling it
// first.
//
// The cache is a disk cache, or a flat one (RuneLite's FlatStorage) in a
// directory without main_file_cache.dat2. --xtea gives the keys of a cache
// that encrypts its map locations, in RuneLite's XteaKeyManager format; both
// sides read them, osrs_cache_dump from the file OSRS_CACHE_XTEA names.
//
// Both sides write each object as "@key" (its parts joined by dots) and then
// lines of `path=value`, RuneLite's from its definition objects by
// reflection, osrs_cache_dump's by hand (oracle/lines.h), so the dumper is
// where osrs_cache's types are mapped onto RuneLite's fields and their Java
// representations. Objects are streamed in
// ascending key order and compared one at a time, as the models and maps
// don't fit in memory together.

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.Deque;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.Set;
import java.util.TreeMap;
import java.util.concurrent.Callable;
import net.runelite.cache.ConfigType;
import net.runelite.cache.IndexType;
import net.runelite.cache.ItemManager;
import net.runelite.cache.WorldMapManager;
import net.runelite.cache.definitions.FramemapDefinition;
import net.runelite.cache.definitions.ItemDefinition;
import net.runelite.cache.definitions.LocationsDefinition;
import net.runelite.cache.definitions.MapDefinition;
import net.runelite.cache.definitions.ScriptDefinition;
import net.runelite.cache.definitions.SpriteDefinition;
import net.runelite.cache.definitions.TextureDefinition;
import net.runelite.cache.definitions.WorldMapElementDefinition;
import net.runelite.cache.definitions.loaders.AreaLoader;
import net.runelite.cache.definitions.loaders.DBRowLoader;
import net.runelite.cache.definitions.loaders.DBTableIndexLoader;
import net.runelite.cache.definitions.loaders.DBTableLoader;
import net.runelite.cache.definitions.loaders.EnumLoader;
import net.runelite.cache.definitions.loaders.FontLoader;
import net.runelite.cache.definitions.loaders.FrameLoader;
import net.runelite.cache.definitions.loaders.FramemapLoader;
import net.runelite.cache.definitions.loaders.GameValLoader;
import net.runelite.cache.definitions.loaders.HealthBarLoader;
import net.runelite.cache.definitions.loaders.HitSplatLoader;
import net.runelite.cache.definitions.loaders.InterfaceLoader;
import net.runelite.cache.definitions.loaders.InventoryLoader;
import net.runelite.cache.definitions.loaders.KitLoader;
import net.runelite.cache.definitions.loaders.ModelLoader;
import net.runelite.cache.definitions.loaders.NpcLoader;
import net.runelite.cache.definitions.loaders.ObjectLoader;
import net.runelite.cache.definitions.loaders.OverlayLoader;
import net.runelite.cache.definitions.loaders.ParamLoader;
import net.runelite.cache.definitions.loaders.ScriptLoader;
import net.runelite.cache.definitions.loaders.SequenceLoader;
import net.runelite.cache.definitions.loaders.SpotAnimLoader;
import net.runelite.cache.definitions.loaders.SpriteLoader;
import net.runelite.cache.definitions.loaders.StructLoader;
import net.runelite.cache.definitions.loaders.TextureLoader;
import net.runelite.cache.definitions.loaders.TrackLoader;
import net.runelite.cache.definitions.loaders.UnderlayLoader;
import net.runelite.cache.definitions.loaders.VarbitLoader;
import net.runelite.cache.definitions.loaders.WorldMapCompositeLoader;
import net.runelite.cache.definitions.loaders.WorldMapLoader;
import net.runelite.cache.definitions.loaders.sound.SoundEffectLoader;
import net.runelite.cache.fs.Archive;
import net.runelite.cache.fs.FSFile;
import net.runelite.cache.fs.Index;
import net.runelite.cache.fs.Store;
import net.runelite.cache.fs.flat.FlatStorage;
import net.runelite.cache.region.Position;
import net.runelite.cache.region.Region;
import net.runelite.cache.region.RegionLoader;
import net.runelite.cache.script.disassembler.Disassembler;
import net.runelite.cache.util.KeyProvider;
import net.runelite.cache.util.XteaKeyManager;

public class RuneLiteOracle
{
	// How many differing objects, and differences in each, a summary shows.
	private static final int SHOWN_OBJECTS = 5;
	private static final int SHOWN_LINES = 12;
	private static final int SHOWN_PATTERNS = 15;

	// A field that refers to a definition compared on its own is written as
	// that definition's id alone.
	private static final Map<String, String> REFERENCES = Map.of("FrameDefinition.framemap", "id");
	// Back-references, which would recurse forever.
	private static final Set<String> SKIPPED = Set.of("Archive.index");
	// Classes written on one line, as {field=value;...} sorted, because there
	// are millions of them: a region's map has 16,384 tiles.
	private static final Set<String> COMPACT = Set.of("net.runelite.cache.definitions.MapDefinition$Tile");

	private interface Load
	{
		Object call() throws Exception;
	}

	private static final class Entry
	{
		final long[] key;
		final Load value;

		Entry(long[] key, Load value)
		{
			this.key = key;
			this.value = value;
		}
	}

	private interface Source
	{
		// In ascending key order.
		Iterator<Entry> entries(Store store) throws Exception;
	}

	// A disassembled script, as lines so that differences point at one.
	static final class Text
	{
		final String[] lines;

		Text(String text)
		{
			lines = text.split("\n", -1);
		}
	}

	private static final Map<String, Source> TYPES = new LinkedHashMap<>();

	// The --xtea key file, or null.
	private static String xteaKeys;

	static
	{
		TYPES.put("index", store ->
		{
			List<Entry> entries = new ArrayList<>();
			for (Index index : store.getIndexes())
			{
				entries.add(new Entry(key(index.getId()), () -> index));
			}
			return sorted(entries);
		});

		config("underlay", ConfigType.UNDERLAY, (archive, id, data) -> new UnderlayLoader().load(id, data));
		config("kit", ConfigType.IDENTKIT, (archive, id, data) -> new KitLoader().load(id, data));
		config("overlay", ConfigType.OVERLAY, (archive, id, data) -> new OverlayLoader().load(id, data));
		config("inventory", ConfigType.INV, (archive, id, data) -> new InventoryLoader().load(id, data));
		// As ObjectManager and NpcManager configure them.
		config("object", ConfigType.OBJECT, (archive, id, data) -> new ObjectLoader().configureForRevision(archive.getRevision()).load(id, data));
		config("enum", ConfigType.ENUM, (archive, id, data) -> new EnumLoader().load(id, data));
		config("npc", ConfigType.NPC, (archive, id, data) -> new NpcLoader().configureForRevision(archive.getRevision()).load(id, data));
		TYPES.put("item", store ->
		{
			// ItemManager links noted, bought and placeholder items to the
			// items they copy.
			ItemManager items = new ItemManager(store);
			items.load();
			items.link();
			List<Entry> entries = new ArrayList<>();
			for (ItemDefinition item : items.getItems())
			{
				entries.add(new Entry(key(item.id), () -> item));
			}
			return sorted(entries);
		});
		config("param", ConfigType.PARAMS, (archive, id, data) -> new ParamLoader().load(data));
		config("sequence", ConfigType.SEQUENCE, (archive, id, data) -> new SequenceLoader().load(id, data));
		config("spotanim", ConfigType.SPOTANIM, (archive, id, data) -> new SpotAnimLoader().load(id, data));
		config("varbit", ConfigType.VARBIT, (archive, id, data) -> new VarbitLoader().load(id, data));
		config("hitsplat", ConfigType.HITSPLAT, (archive, id, data) -> new HitSplatLoader().load(data));
		config("healthbar", ConfigType.HEALTHBAR, (archive, id, data) -> new HealthBarLoader().load(id, data));
		config("struct", ConfigType.STRUCT, (archive, id, data) -> new StructLoader().load(id, data));
		config("area", ConfigType.AREA, (archive, id, data) -> new AreaLoader().load(data, id));
		config("dbrow", ConfigType.DBROW, (archive, id, data) -> new DBRowLoader().load(id, data));
		config("dbtable", ConfigType.DBTABLE, (archive, id, data) -> new DBTableLoader().load(id, data));

		TYPES.put("framemap", store -> perGroup(store, IndexType.SKELETONS, (archive, file, data) -> new FramemapLoader().load(archive.getArchiveId(), data)));
		TYPES.put("frame", store ->
		{
			Map<Integer, FramemapDefinition> framemaps = new HashMap<>();
			return perFile(store, IndexType.ANIMATIONS, (archive, file, data) ->
			{
				int framemapId = (data[0] & 0xFF) << 8 | data[1] & 0xFF;
				FramemapDefinition framemap = framemaps.get(framemapId);
				if (framemap == null)
				{
					framemap = new FramemapLoader().load(framemapId, fileContents(store, IndexType.SKELETONS, framemapId, 0));
					framemaps.put(framemapId, framemap);
				}
				return new FrameLoader().load(framemap, file, data);
			});
		});
		TYPES.put("interface", store ->
		{
			// As InterfaceManager configures it.
			InterfaceLoader loader = new InterfaceLoader().configureForRevision(store.getIndex(IndexType.INTERFACES).getRevision());
			return perFile(store, IndexType.INTERFACES, (archive, file, data) -> loader.load((archive.getArchiveId() << 16) + file, data));
		});
		TYPES.put("soundeffect", store -> perGroup(store, IndexType.SOUNDEFFECTS, (archive, file, data) -> new SoundEffectLoader().load(data)));
		TYPES.put("map", store -> regions(store, (loader, id) -> loader.loadMapDef(id)));
		TYPES.put("locations", store -> regions(store, (loader, id) -> loader.loadLocDef(id)));
		TYPES.put("region", store -> regions(store, (loader, id) ->
		{
			// RegionLoader.loadRegionFromArchive, without keeping every region.
			MapDefinition map = loader.loadMapDef(id);
			if (map == null)
			{
				return null;
			}
			Region region = new Region(id);
			region.loadTerrain(map);
			LocationsDefinition locations = loader.loadLocDef(id);
			if (locations != null)
			{
				region.loadLocations(locations);
			}
			return region;
		}));
		TYPES.put("track", store -> perGroup(store, IndexType.MUSIC_TRACKS, (archive, file, data) -> new TrackLoader().load(data)));
		TYPES.put("jingle", store -> perGroup(store, IndexType.MUSIC_JINGLES, (archive, file, data) -> new TrackLoader().load(data)));
		TYPES.put("model", store -> perGroup(store, IndexType.MODELS, (archive, file, data) -> new ModelLoader().load(archive.getArchiveId(), data)));
		TYPES.put("sprite", store -> new Groups(store, IndexType.SPRITES)
		{
			List<Entry> load(Archive archive, byte[] stored) throws IOException
			{
				// As SpriteManager loads them: the group's whole contents.
				SpriteDefinition[] frames = new SpriteLoader().load(archive.getArchiveId(), archive.decompress(stored));
				List<Entry> entries = new ArrayList<>();
				for (int frame = 0; frame < frames.length; ++frame)
				{
					SpriteDefinition sprite = frames[frame];
					entries.add(new Entry(key(archive.getArchiveId(), frame), () -> sprite));
				}
				return entries;
			}
		});
		TYPES.put("texture", store -> filesOf(store, store.getIndex(IndexType.TEXTURES).getArchive(0), (archive, file, data) -> new TextureLoader().load(file, data)));
		TYPES.put("texturepixels", store -> filesOf(store, store.getIndex(IndexType.TEXTURES).getArchive(0), (archive, file, data) ->
		{
			// The pixels method2680 makes at brightness 0.8 and 128 by 128,
			// from each texture's first sprite frames.
			TextureDefinition texture = new TextureLoader().load(file, data);
			texture.method2680(0.8, 128, (spriteId, frame) ->
			{
				try
				{
					return new SpriteLoader().load(spriteId, groupContents(store, IndexType.SPRITES, spriteId))[frame];
				}
				catch (IOException e)
				{
					throw new IllegalStateException(e);
				}
			});
			return texture;
		}));
		TYPES.put("script", store ->
		{
			// As WorldMapManager configures it.
			ScriptLoader loader = new ScriptLoader().configureForRevision(store.getIndex(IndexType.CLIENTSCRIPT).getRevision());
			return perGroup(store, IndexType.CLIENTSCRIPT, (archive, file, data) -> loader.load(archive.getArchiveId(), data));
		});
		TYPES.put("disassembly", store ->
		{
			ScriptLoader loader = new ScriptLoader().configureForRevision(store.getIndex(IndexType.CLIENTSCRIPT).getRevision());
			return perGroup(store, IndexType.CLIENTSCRIPT, (archive, file, data) ->
			{
				ScriptDefinition script = loader.load(archive.getArchiveId(), data);
				return new Text(new Disassembler().disassemble(script));
			});
		});
		TYPES.put("font", store -> perGroup(store, IndexType.FONTS, (archive, file, data) -> new FontLoader().load(data)));
		TYPES.put("worldmap", store ->
		{
			// The details group, found as WorldMapManager finds the composite map.
			Index index = store.getIndex(IndexType.WORLDMAP);
			Archive details = index.isNamed() ? index.findArchiveByName("details") : index.getArchive(WorldMapManager.DETAILS_ID);
			return filesOf(store, details, (archive, file, data) -> new WorldMapLoader().load(data, file));
		});
		TYPES.put("worldmapcomposite", store ->
		{
			// As WorldMapManager loads it.
			Index index = store.getIndex(IndexType.WORLDMAP);
			Archive composite = index.isNamed() ? index.findArchiveByName("compositemap") : index.getArchive(WorldMapManager.COMPOSITEMAP_ID);
			WorldMapCompositeLoader loader = new WorldMapCompositeLoader().configureForRevision(index.getRevision());
			return filesOf(store, composite, (archive, file, data) -> loader.load(data));
		});
		TYPES.put("worldmapelement", store ->
		{
			// WorldMapManager's elements: every composite map's, in order.
			WorldMapManager manager = new WorldMapManager(store);
			manager.load();
			List<Entry> entries = new ArrayList<>();
			List<WorldMapElementDefinition> elements = manager.getElements();
			for (int i = 0; i < elements.size(); ++i)
			{
				WorldMapElementDefinition element = elements.get(i);
				entries.add(new Entry(key(i), () -> element));
			}
			return entries.iterator();
		});
		TYPES.put("intermaplink", store ->
		{
			// WorldMapManager's links between maps, which a hash map holds,
			// keyed by where each starts; each object is where it ends.
			WorldMapManager manager = new WorldMapManager(store);
			manager.load();
			List<Entry> entries = new ArrayList<>();
			for (Map.Entry<Position, Position> link : manager.getIntermapLinks().entrySet())
			{
				Position start = link.getKey();
				Position end = link.getValue();
				entries.add(new Entry(key(start.getX(), start.getY(), start.getZ()), () -> end));
			}
			return sorted(entries);
		});
		// As DBTableIndexManager loads them: file 0 is the master index, file n column n - 1.
		TYPES.put("dbtableindex", store -> perFile(store, IndexType.DBTABLEINDEX, (archive, file, data) -> new DBTableIndexLoader().load(archive.getArchiveId(), file - 1, data)));
		TYPES.put("gameval", store -> perFile(store, IndexType.GAMEVALS, (archive, file, data) -> new GameValLoader().load(archive.getArchiveId(), file, data)));
	}

	public static void main(String[] args) throws Exception
	{
		if (args.length < 2)
		{
			System.err.println("usage: RuneLiteOracle <cache dir> <osrs_cache_dump> [type[:key,...] ...]");
			System.exit(2);
		}
		File cache = new File(args[0]);
		String dumper = args[1];
		Map<String, List<long[]>> requested = new LinkedHashMap<>();
		for (int i = 2; i < args.length; ++i)
		{
			if (args[i].equals("--xtea") && i + 1 < args.length)
			{
				xteaKeys = args[++i];
				continue;
			}
			String[] parts = args[i].split(":", 2);
			if (!TYPES.containsKey(parts[0]))
			{
				System.err.println("unknown type " + parts[0] + "; known: " + String.join(" ", TYPES.keySet()));
				System.exit(2);
			}
			List<long[]> keys = new ArrayList<>();
			if (parts.length == 2)
			{
				for (String key : parts[1].split(","))
				{
					keys.add(parseKey(key));
				}
			}
			requested.put(parts[0], keys);
		}
		if (requested.isEmpty())
		{
			for (String type : TYPES.keySet())
			{
				requested.put(type, new ArrayList<>());
			}
		}

		boolean counting = dumper.equals("--count");
		Set<String> dumped = counting ? TYPES.keySet() : dumpedTypes(dumper);
		boolean failed = false;
		boolean flat = !new File(cache, "main_file_cache.dat2").exists();
		try (Store store = flat ? new Store(new FlatStorage(cache)) : new Store(cache))
		{
			store.load();
			for (Map.Entry<String, List<long[]>> type : requested.entrySet())
			{
				if (counting)
				{
					count(store, type.getKey());
					continue;
				}
				if (!dumped.contains(type.getKey()))
				{
					System.out.printf(Locale.ROOT, "%-18s not dumped by osrs_cache_dump yet%n", type.getKey());
					failed = true;
					continue;
				}
				failed |= !check(store, args[0], dumper, type.getKey(), type.getValue());
			}
		}
		System.exit(failed ? 1 : 0);
	}

	private static void count(Store store, String type) throws Exception
	{
		long start = System.nanoTime();
		int objects = 0;
		int errors = 0;
		long lines = 0;
		String firstError = null;
		for (Iterator<Entry> entries = TYPES.get(type).entries(store); entries.hasNext(); )
		{
			Entry entry = entries.next();
			List<String> flattened = flatten(entry.value);
			if (flattened == null)
			{
				continue;
			}
			++objects;
			lines += flattened.size();
			if (flattened.contains("!error"))
			{
				++errors;
				if (firstError == null)
				{
					firstError = keyString(entry.key);
				}
			}
		}
		System.out.printf(Locale.ROOT, "%-18s %7d objects, %9d lines, %d throw%s (%.1f s)%n", type, objects, lines, errors,
			firstError == null ? "" : " (first: " + firstError + ")", (System.nanoTime() - start) / 1e9);
	}

	// The types osrs_cache_dump writes, one a line.
	private static Set<String> dumpedTypes(String dumper) throws Exception
	{
		Process process = new ProcessBuilder(dumper, "--types").redirectError(ProcessBuilder.Redirect.INHERIT).start();
		Set<String> types = new java.util.HashSet<>();
		try (BufferedReader in = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8)))
		{
			for (String line = in.readLine(); line != null; line = in.readLine())
			{
				types.add(line.trim());
			}
		}
		if (process.waitFor() != 0)
		{
			throw new IllegalStateException(dumper + " --types failed");
		}
		return types;
	}

	// Whether osrs_cache agrees with RuneLite on every object of the type.
	private static boolean check(Store store, String cache, String dumper, String type, List<long[]> only) throws Exception
	{
		long start = System.nanoTime();
		List<String> command = new ArrayList<>(List.of(dumper, cache, type));
		for (long[] key : only)
		{
			command.add(keyString(key));
		}
		ProcessBuilder builder = new ProcessBuilder(command).redirectError(ProcessBuilder.Redirect.INHERIT);
		if (xteaKeys != null)
		{
			builder.environment().put("OSRS_CACHE_XTEA", xteaKeys);
		}
		Process process = builder.start();
		Ours ours = new Ours(new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8), 1 << 20));
		Iterator<Entry> theirs = TYPES.get(type).entries(store);

		int compared = 0;
		int differing = 0;
		List<String> onlyTheirs = new ArrayList<>();
		List<String> onlyOurs = new ArrayList<>();
		List<String> shown = new ArrayList<>();
		Map<String, Integer> patterns = new TreeMap<>();
		Entry their = next(theirs);
		long[] ourKey = ours.next();
		while (their != null || ourKey != null)
		{
			int order = their == null ? 1 : ourKey == null ? -1 : compare(their.key, ourKey);
			boolean wanted = only.isEmpty() || contains(only, order <= 0 ? their.key : ourKey);
			if (order < 0)
			{
				// Objects RuneLite's loader returns null for, as RegionLoader
				// does for a region without a map, don't exist.
				if (wanted && flatten(their.value) != null)
				{
					onlyTheirs.add(keyString(their.key));
				}
				their = next(theirs);
				continue;
			}
			if (order > 0)
			{
				if (wanted)
				{
					onlyOurs.add(keyString(ourKey));
				}
				ours.lines();
				ourKey = ours.next();
				continue;
			}
			List<String> theirLines = wanted ? flatten(their.value) : null;
			List<String> ourLines = ours.lines();
			if (wanted)
			{
				if (theirLines == null)
				{
					onlyOurs.add(keyString(ourKey));
				}
				else
				{
					++compared;
					Map<String, String[]> differences = differences(theirLines, ourLines);
					if (!differences.isEmpty())
					{
						++differing;
						Set<String> seen = new java.util.HashSet<>();
						for (String path : differences.keySet())
						{
							String pattern = path.replaceAll("\\[\\d+\\]", "[*]").replaceAll("\\{[^}]*\\}", "{*}");
							if (seen.add(pattern))
							{
								patterns.merge(pattern, 1, Integer::sum);
							}
						}
						if (!only.isEmpty() || shown.size() < SHOWN_OBJECTS)
						{
							shown.add(describe(type, ourKey, differences, only.isEmpty() ? SHOWN_LINES : Integer.MAX_VALUE));
						}
					}
				}
			}
			their = next(theirs);
			ourKey = ours.next();
		}
		int exit = process.waitFor();
		if (exit != 0)
		{
			System.out.printf(Locale.ROOT, "%-18s osrs_cache_dump failed with exit code %d%n", type, exit);
			return false;
		}

		boolean agrees = differing == 0 && onlyTheirs.isEmpty() && onlyOurs.isEmpty();
		System.out.printf(Locale.ROOT, "%-18s %s: %d compared, %d differ, %d only in RuneLite, %d only in osrs_cache (%.1f s)%n",
			type, agrees ? "agrees" : "DIFFERS", compared, differing, onlyTheirs.size(), onlyOurs.size(), (System.nanoTime() - start) / 1e9);
		if (!onlyTheirs.isEmpty())
		{
			System.out.println("  only in RuneLite: " + firstOf(onlyTheirs));
		}
		if (!onlyOurs.isEmpty())
		{
			System.out.println("  only in osrs_cache: " + firstOf(onlyOurs));
		}
		if (!patterns.isEmpty())
		{
			System.out.println("  differing fields, by how many objects differ in them:");
			patterns.entrySet().stream()
				.sorted(Map.Entry.<String, Integer>comparingByValue().reversed())
				.limit(SHOWN_PATTERNS)
				.forEach(p -> System.out.printf(Locale.ROOT, "    %7d  %s%n", p.getValue(), p.getKey()));
		}
		for (String object : shown)
		{
			System.out.print(object);
		}
		return agrees;
	}

	// osrs_cache_dump's output: "@key" starts each object, "path=value" lines follow.
	private static final class Ours
	{
		private final BufferedReader in;
		private String pending;

		Ours(BufferedReader in) throws IOException
		{
			this.in = in;
			pending = in.readLine();
		}

		long[] next()
		{
			if (pending == null)
			{
				return null;
			}
			if (!pending.startsWith("@"))
			{
				throw new IllegalStateException("osrs_cache_dump wrote a line outside an object: " + pending);
			}
			return parseKey(pending.substring(1));
		}

		List<String> lines() throws IOException
		{
			List<String> lines = new ArrayList<>();
			for (pending = in.readLine(); pending != null && !pending.startsWith("@"); pending = in.readLine())
			{
				lines.add(pending);
			}
			return lines;
		}
	}

	// RuneLite's object as lines, or null if there is no object: a throw is
	// written as a line of its own, as osrs_cache_dump writes one.
	private static List<String> flatten(Load load)
	{
		Object value;
		try
		{
			value = load.call();
		}
		catch (Exception | AssertionError e)
		{
			return new ArrayList<>(List.of("!error"));
		}
		if (value == null)
		{
			return null;
		}
		List<String> lines = new ArrayList<>();
		flatten("", value, lines, 0);
		return lines;
	}

	static void flatten(String path, Object value, List<String> out, int depth)
	{
		if (value == null)
		{
			return;
		}
		if (depth > 32)
		{
			throw new IllegalStateException("nested too deep at " + path);
		}
		Class<?> type = value.getClass();
		if (isScalar(value))
		{
			out.add(path + "=" + scalar(value));
		}
		else if (type.isArray() && type.getComponentType().isPrimitive())
		{
			StringBuilder line = new StringBuilder(path).append("=[");
			for (int i = 0, n = Array.getLength(value); i < n; ++i)
			{
				line.append(i == 0 ? "" : ",").append(scalar(Array.get(value, i)));
			}
			out.add(line.append(']').toString());
		}
		else if (type.isArray())
		{
			for (int i = 0, n = Array.getLength(value); i < n; ++i)
			{
				flatten(path + "[" + i + "]", Array.get(value, i), out, depth + 1);
			}
		}
		else if (COMPACT.contains(type.getName()))
		{
			List<String> fields = new ArrayList<>();
			for (Field field : fields(type))
			{
				try
				{
					flatten(field.getName(), field.get(value), fields, depth + 1);
				}
				catch (IllegalAccessException e)
				{
					throw new IllegalStateException(e);
				}
			}
			Collections.sort(fields);
			out.add(path + "={" + String.join(";", fields) + "}");
		}
		else if (value instanceof Set)
		{
			// A hash set's order isn't the cache's, so each element is one
			// line of its own fields, sorted, which differences() matches by
			// those fields.
			for (Object element : (Set<?>) value)
			{
				List<String> fields = new ArrayList<>();
				flatten("", element, fields, depth + 1);
				Collections.sort(fields);
				out.add(path + "{}={" + String.join(";", fields) + "}");
			}
		}
		else if (value instanceof Collection)
		{
			int i = 0;
			for (Object element : (Collection<?>) value)
			{
				flatten(path + "[" + i++ + "]", element, out, depth + 1);
			}
		}
		else if (value instanceof Map)
		{
			for (Map.Entry<?, ?> entry : ((Map<?, ?>) value).entrySet())
			{
				flatten(path + "{" + scalar(entry.getKey()) + "}", entry.getValue(), out, depth + 1);
			}
		}
		else
		{
			for (Field field : fields(type))
			{
				String name = field.getDeclaringClass().getSimpleName() + "." + field.getName();
				if (SKIPPED.contains(name))
				{
					continue;
				}
				String child = path.isEmpty() ? field.getName() : path + "." + field.getName();
				Object fieldValue;
				try
				{
					fieldValue = field.get(value);
				}
				catch (IllegalAccessException e)
				{
					throw new IllegalStateException(e);
				}
				String reference = REFERENCES.get(name);
				if (reference != null && fieldValue != null)
				{
					try
					{
						Field id = fieldValue.getClass().getDeclaredField(reference);
						id.setAccessible(true);
						out.add(child + "=" + scalar(id.get(fieldValue)));
					}
					catch (ReflectiveOperationException e)
					{
						throw new IllegalStateException(e);
					}
					continue;
				}
				flatten(child, fieldValue, out, depth + 1);
			}
		}
	}

	private static final Map<Class<?>, List<Field>> FIELDS = new HashMap<>();

	// The instance fields of the class and its superclasses.
	private static List<Field> fields(Class<?> type)
	{
		return FIELDS.computeIfAbsent(type, t ->
		{
			List<Field> fields = new ArrayList<>();
			for (Class<?> c = t; c != null && c != Object.class; c = c.getSuperclass())
			{
				for (Field field : c.getDeclaredFields())
				{
					int modifiers = field.getModifiers();
					// Transient fields too: RuneLite marks the values it computes
					// transient to keep them out of its JSON, not out of the
					// definition.
					if (!Modifier.isStatic(modifiers) && !field.isSynthetic())
					{
						field.setAccessible(true);
						fields.add(field);
					}
				}
			}
			return fields;
		});
	}

	private static boolean isScalar(Object value)
	{
		return value instanceof Number || value instanceof Boolean || value instanceof Character
			|| value instanceof String || value instanceof Enum;
	}

	// The value as osrs_cache_dump writes it (oracle/lines.h).
	static String scalar(Object value)
	{
		if (value instanceof Float)
		{
			return String.format(Locale.ROOT, "f%08x", Float.floatToRawIntBits((Float) value));
		}
		if (value instanceof Double)
		{
			return String.format(Locale.ROOT, "d%016x", Double.doubleToRawLongBits((Double) value));
		}
		if (value instanceof Character)
		{
			return Integer.toString((Character) value);
		}
		if (value instanceof String)
		{
			return quote((String) value);
		}
		if (value instanceof Enum)
		{
			return ((Enum<?>) value).name();
		}
		return value.toString();
	}

	static String quote(String text)
	{
		StringBuilder quoted = new StringBuilder("\"");
		for (int i = 0; i < text.length(); ++i)
		{
			char c = text.charAt(i);
			switch (c)
			{
				case '\\':
					quoted.append("\\\\");
					break;
				case '"':
					quoted.append("\\\"");
					break;
				case '\n':
					quoted.append("\\n");
					break;
				case '\r':
					quoted.append("\\r");
					break;
				case '\t':
					quoted.append("\\t");
					break;
				default:
					if (c < 0x20)
					{
						quoted.append(String.format(Locale.ROOT, "\\u%04x", (int) c));
					}
					else
					{
						quoted.append(c);
					}
			}
		}
		return quoted.append('"').toString();
	}

	// By path: RuneLite's value and osrs_cache's, null where a side has none.
	private static Map<String, String[]> differences(List<String> theirs, List<String> ours)
	{
		Map<String, String[]> byPath = new TreeMap<>();
		for (String line : theirs)
		{
			put(byPath, line, 0);
		}
		for (String line : ours)
		{
			put(byPath, line, 1);
		}
		byPath.values().removeIf(values -> values[0] != null && values[0].equals(values[1]));
		return byPath;
	}

	private static void put(Map<String, String[]> byPath, String line, int side)
	{
		int split = line.indexOf('=');
		String path = split < 0 ? line : line.substring(0, split);
		String value = split < 0 ? "" : line.substring(split + 1);
		if (path.endsWith("{}"))
		{
			// The elements of a set share its path and come in hash order, so
			// each is keyed by its fields instead, path{fields}, with how many
			// times it occurs as its value.
			String[] values = byPath.computeIfAbsent(path.substring(0, path.length() - 2) + value, p -> new String[2]);
			values[side] = Integer.toString(values[side] == null ? 1 : Integer.parseInt(values[side]) + 1);
			return;
		}
		byPath.computeIfAbsent(path, p -> new String[2])[side] = value;
	}

	private static String describe(String type, long[] key, Map<String, String[]> differences, int limit)
	{
		StringBuilder text = new StringBuilder(String.format(Locale.ROOT, "  %s %s: %d fields differ%n", type, keyString(key), differences.size()));
		int shown = 0;
		for (Map.Entry<String, String[]> difference : differences.entrySet())
		{
			if (shown++ == limit)
			{
				text.append(String.format(Locale.ROOT, "    ... and %d more%n", differences.size() - limit));
				break;
			}
			String[] values = difference.getValue();
			text.append(String.format(Locale.ROOT, "    %s%n      RuneLite:   %s%n      osrs_cache: %s%n", difference.getKey(),
				abbreviate(values[0]), abbreviate(values[1])));
		}
		return text.toString();
	}

	private static String abbreviate(String value)
	{
		if (value == null)
		{
			return "(none)";
		}
		return value.length() > 300 ? value.substring(0, 300) + "... (" + value.length() + " characters)" : value;
	}

	// Sources

	private interface ConfigLoad
	{
		Object load(Archive archive, int id, byte[] data) throws Exception;
	}

	private interface FileLoad
	{
		Object load(Archive archive, int file, byte[] data) throws Exception;
	}

	private static void config(String name, ConfigType type, ConfigLoad load)
	{
		TYPES.put(name, store ->
		{
			Archive archive = store.getIndex(IndexType.CONFIGS).getArchive(type.getId());
			return filesOf(store, archive, (a, file, data) -> load.load(a, file, data));
		});
	}

	// Each file of one group, keyed by file id.
	private static Iterator<Entry> filesOf(Store store, Archive archive, FileLoad load) throws IOException
	{
		List<Entry> entries = new ArrayList<>();
		for (FSFile file : archive.getFiles(store.getStorage().loadArchive(archive)).getFiles())
		{
			byte[] data = file.getContents();
			entries.add(new Entry(key(file.getFileId()), () -> load.load(archive, file.getFileId(), data)));
		}
		return sorted(entries);
	}

	// File 0 of each group of the index, keyed by group id.
	private static Iterator<Entry> perGroup(Store store, IndexType type, FileLoad load)
	{
		if (store.getIndex(type) == null)
		{
			return Collections.emptyIterator();
		}
		return new Groups(store, type)
		{
			List<Entry> load(Archive archive, byte[] stored) throws IOException
			{
				FSFile file = archive.getFiles(stored).findFile(0);
				byte[] data = file == null ? null : file.getContents();
				return List.of(new Entry(key(archive.getArchiveId()), () -> data == null ? null : load.load(archive, 0, data)));
			}
		};
	}

	// Each file of each group of the index, keyed by group and file ids.
	private static Iterator<Entry> perFile(Store store, IndexType type, FileLoad load)
	{
		if (store.getIndex(type) == null)
		{
			return Collections.emptyIterator();
		}
		return new Groups(store, type)
		{
			List<Entry> load(Archive archive, byte[] stored) throws IOException
			{
				List<Entry> entries = new ArrayList<>();
				for (FSFile file : archive.getFiles(stored).getFiles())
				{
					byte[] data = file.getContents();
					entries.add(new Entry(key(archive.getArchiveId(), file.getFileId()), () -> load.load(archive, file.getFileId(), data)));
				}
				entries.sort((a, b) -> compare(a.key, b.key));
				return entries;
			}
		};
	}

	// Reads the index's groups one at a time, in ascending id order.
	private abstract static class Groups implements Iterator<Entry>
	{
		private final Store store;
		private final Iterator<Archive> archives;
		private final Deque<Entry> pending = new ArrayDeque<>();

		Groups(Store store, IndexType type)
		{
			this.store = store;
			List<Archive> sorted = new ArrayList<>(store.getIndex(type).getArchives());
			sorted.sort(Comparator.comparingInt(Archive::getArchiveId));
			archives = sorted.iterator();
		}

		abstract List<Entry> load(Archive archive, byte[] stored) throws IOException;

		public boolean hasNext()
		{
			while (pending.isEmpty() && archives.hasNext())
			{
				Archive archive = archives.next();
				try
				{
					pending.addAll(load(archive, store.getStorage().loadArchive(archive)));
				}
				catch (IOException | RuntimeException e)
				{
					pending.add(new Entry(key(archive.getArchiveId()), () ->
					{
						throw e;
					}));
				}
			}
			return !pending.isEmpty();
		}

		public Entry next()
		{
			if (!hasNext())
			{
				throw new NoSuchElementException();
			}
			return pending.removeFirst();
		}
	}

	private interface RegionLoad
	{
		Object load(RegionLoader loader, int id) throws Exception;
	}

	// Every region id; ids without a map load as null and are left out.
	private static Iterator<Entry> regions(Store store, RegionLoad load) throws IOException
	{
		KeyProvider keys = region -> null;
		if (xteaKeys != null)
		{
			XteaKeyManager manager = new XteaKeyManager();
			try (FileInputStream in = new FileInputStream(xteaKeys))
			{
				manager.loadKeys(in);
			}
			keys = manager;
		}
		RegionLoader loader = new RegionLoader(store, keys);
		List<Entry> entries = new ArrayList<>();
		for (int id = 0; id < 1 << 15; ++id)
		{
			int region = id;
			entries.add(new Entry(key(region), () -> load.load(loader, region)));
		}
		return entries.iterator();
	}

	private static byte[] groupContents(Store store, IndexType type, int group) throws IOException
	{
		Archive archive = store.getIndex(type).getArchive(group);
		return archive.decompress(store.getStorage().loadArchive(archive));
	}

	private static byte[] fileContents(Store store, IndexType type, int group, int file) throws IOException
	{
		Archive archive = store.getIndex(type).getArchive(group);
		return archive.getFiles(store.getStorage().loadArchive(archive)).findFile(file).getContents();
	}

	// Keys

	private static long[] key(long... parts)
	{
		return parts;
	}

	private static long[] parseKey(String text)
	{
		String[] parts = text.split("\\.");
		long[] key = new long[parts.length];
		for (int i = 0; i < parts.length; ++i)
		{
			key[i] = Long.parseLong(parts[i]);
		}
		return key;
	}

	private static String keyString(long[] key)
	{
		StringBuilder text = new StringBuilder();
		for (long part : key)
		{
			text.append(text.length() == 0 ? "" : ".").append(part);
		}
		return text.toString();
	}

	private static int compare(long[] a, long[] b)
	{
		for (int i = 0; i < Math.min(a.length, b.length); ++i)
		{
			if (a[i] != b[i])
			{
				return Long.compare(a[i], b[i]);
			}
		}
		return Integer.compare(a.length, b.length);
	}

	private static boolean contains(List<long[]> keys, long[] key)
	{
		for (long[] k : keys)
		{
			if (Arrays.equals(k, key))
			{
				return true;
			}
		}
		return false;
	}

	private static Iterator<Entry> sorted(List<Entry> entries)
	{
		entries.sort((a, b) -> compare(a.key, b.key));
		return entries.iterator();
	}

	private static Entry next(Iterator<Entry> entries)
	{
		return entries.hasNext() ? entries.next() : null;
	}

	private static String firstOf(List<String> keys)
	{
		return String.join(" ", keys.subList(0, Math.min(keys.size(), 20))) + (keys.size() > 20 ? " ..." : "");
	}
}
