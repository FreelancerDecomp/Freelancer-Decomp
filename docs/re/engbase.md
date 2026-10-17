# EngBase.dll

`EngBase.dll` is the animation and scene core: archetypes and instance trees, their transforms, animation channels, attachment points (hardpoints). It underlies `IEngine`, `IAnimation2`, `IChannel`, `IHardpoint`, `IEngineComponent`. This document describes the architecture and shared formats; statuses, addresses and evidence for functions and slots live in decomp-tracker (listing `EXE/Disasm/engbase.dll.txt`). The port is `src/EngBase/` (`EngBase.cpp`, `EngBaseMixer.cpp`); the public interfaces are in `src/include/engine/engbase.h`.

`DALib::Startup` creates `Engine`, then `LoadEngineComponents` reads the `[Engine]` section through dacom's built-in INI parser (`luaprofile` is not involved), aggregates the listed components and builds the list of `IEngineComponent`. EngBase calls back into other DLLs through `I3DMathEngine` (x86math) and `IFileSystem` (ReadFile).

## Components and their registration

`DllMain` registers four factories (after `RegisterComponent` the factory is `Release`d): `Animation` (priority `0x40000000`), `ChannelArchetype` (`0x80000000`), `Hardpoint` (`0x80000000`), `Engine` (`0x40000000`). A factory is a `0xC`-byte object `{vtable, refcount, name}`.

| Component | AGGDESC | Object |
|-----------|---------|--------|
| Animation | `0x18` | `calloc(0x60)`: facets `IAnimation2` (+0), `IAggregateComponent` (+4), `IChannel` (+8), `IRootData` (+0xC); controlling unknown +0x50, inner +0x54 |
| Hardpoint | `0x18` | `calloc(0x44)`: `IHardpoint` (+0), `IEngineComponent` (+4), controlling +0x34, inner +0x38 |
| Engine | `0x08` | `calloc(0x64)`: `IEngine` (+0), refcount +0x60 |
| ChannelArchetype | `0x08` | `operator new(0x20)`, refcount +0x1C, no inner; not in `[Engine]`, created when the `"Channel"` node is read |

The extended `0x18` descriptor for components from `[Engine]`: `+4` factory name (string token), `+8` controlling unknown (= the Engine itself), `+0xC` `void**` for the inner (the factory writes the inner there), `+0x10` the value after `=` (may be NULL), `+0x14` `ISystemContainer`. Animation/Hardpoint factory: if `desc+8 == 0`, the controlling unknown is the object's own inner, otherwise the inner is written to `*desc+0xC`. The hook after the constructor always returns 0.

Inner `QueryInterface` of Animation: `IAnimation2` and `IAnimation` -> +0, `IChannel` -> +8, `IRootData` -> +0xC, `IAggregateComponent` -> +4; Hardpoint: `IHardpoint` -> +0, `IEngineComponent` -> +4, `IAggregateComponent` -> +4 (the same facet). Facets offset from the start of the object are implemented by `this -= N` thunks.

## `load_engine_components` (`IEngine` slot 3)

1. If already initialised, exit; a null `ISystemContainer` produces a NOTICE through `FDUMP`.
2. QI on `ICOManager` for `"1.11_IProfileParser"`; failure gives ERROR and `-1` (then Hardpoint/Animation in `DALib` will be null).
3. `OpenSection("Engine", 3)`, then `ReadLine` by index (buffer 0x100). An empty section is success. Retail does not call `CloseSection`.
4. Each line: name token (+ the value after `=`), AGGDESC `0x18`, `CreateInstance`, NOTICE "aggregation of '%s' [%s] returned %s". The inner (`*Outer`) is added to the list of **all** components (+0x4C), which is the list the Engine's `QueryInterface` walks; it is the inner that goes in, otherwise QI would recurse back into the Engine.
5. Pass 2: QI `IAggregateComponent` on each node and `Initialize`; on failure, WARNING and the node is dropped from the list (name and pointer are zeroed, retail does not `Release`).
6. Pass 3: for the remaining nodes, QI `IEngineComponent` (immediate `Release`); on success the node goes into the separate list at +0x58 (non-owning pointer). Hardpoint implements `IEngineComponent`, Animation does not.

The composition of `[Engine]` is in `bin/DACOM.INI` (Animation, Hardpoint, Deformable, RenderManager, FLAppearance, VMeshWire). List node `0x3C`: Next, Prev, name (up to +0x38), pointer to the component (+0x38).

`set_search_path`: creates `SearchPath` (`{size 8, "SearchPath"}`) and calls `ISearchPath::SetPath(path)`; retail pushes one more stack argument before the call that does not belong to this frame, the port passes two (`this, path`), as the ReadFile side requires.

## Archetypes and the instance hierarchy

- An archetype is identified by the CRC of its name (`FArchetypeName{Text, Crc}`; `Crc == 0` makes the lookup return `-1`). The archetype map is `std::map<unsigned long, FArchetypeHandle*>` keyed by CRC; handle `0x38`: name +4, CRC +0x10, `RefCount` +0x14, children +0x1C. The free-block pools (`{size, head, limit 0x40, count}`) for handles (0x38), instances (0xEC) and own transforms (0x48) repeat retail.
- `create_compound_archetype`: when `Crc == 0` the name is generated as `anonymous-%d` until the CRC is unique. Loading a compound archetype (slot 12) creates a nested `FileSystem` with `DataPath = "Cmpnd"` and reads `Root`/`Part*`/`Cons` through nested UTF file systems of ReadFile; if `Cmpnd` fails, the node is loaded as a `.3db` leaf through `IEngineComponent`.
- Instance tree: `IEngine::create_joint`/`destroy_joint` link parent and child, maintain the map of hierarchy **roots** (`std::map<long, FEngineInstance*>`, keyed by handle), the lookup cache and hit counters. A lone instance is not inserted into the map. World computation (`FUN_0661f250`): the joint's local matrix is multiplied by the parent's world matrix through `I3DMathEngine`; the world is written only to `transform+4`.
- There are exactly seven joint types, all **FK** (Fixed, Prismatic, Revolute, Cylindrical, Spherical, Translation, Loose); **EngBase has no IK solver** (neither strings nor code in the listing). The angular parameters `0.5` in the jump table are a half-angle, not a degree conversion.
- `IEngine::EnumerateArchetypeChildren` calls the callback as cdecl `(parentHandle, childHandle, 0, context)`; `freelancer.exe` (`ArchLod`) depends on this order. Do not change it without checking the listing.
- The string `".."` at `DAT_0662a03c` (the listing shows only the first byte) is the `OpenNode` argument with which retail climbs back out of the `Hardpoints/<type>` hierarchy directory and while walking the animation maps; `"."` does not go up a level (see [readfile.md](readfile.md)).

## Hardpoint

Holds a `std::map<int handle, std::list<record>>` with an iterator cache and counters (+0x08 / +0x0C). A record: Crc, a `_strdup`+`_strlwr` name, a `0x48` block (position, orientation, axis, min/max). The point type is read from the `Hardpoints/Fixed|Revolute|Prismatic` directory; enumeration uses `"*"` outside and `"*.*"` inside, with no `.`/`..` filter. Connecting (`IHardpoint` slot 6) computes the transform through `I3DMathEngine` (orientation B is transposed) and creates a joint with `IEngine::create_joint`; the destructor reports "dangling hardpoints" through `FDUMP`.

## ChannelArchetype (format)

| Offset | Field |
|----------|------|
| +0x04 | frame count |
| +0x08 | rate (`> 0` is uniform, `< 0` means a timestamp at the start of each frame) |
| +0x0C | flags: 1 scalar, 2 position, 4 quaternion, 8 scalar only |
| +0x10 | frame block |
| +0x14 | stride (from the flags; +4 for timestamps) |
| +0x18 | duration |

Frames with flags `& 0x1F0` are unpacked (0x10 zero position, 0x20 identity quaternion, 0x40 three shorts with `W = sqrt(1-n)`, 0x80 axis-angle, 0x100 four shorts, scale `1/2(3-|q|^2)`) into the shared realloc pool `DAT_0662b104` (the old block is not freed, as in retail); uncompressed ones use `malloc`. Quaternions are **WXYZ** everywhere. The `"Event map"` node is skipped when a script is loaded (EngBase has no events); `"Root height"` is read through the search handle.

## Animation and the mixer

- Playback: `std::map<long, FAnimPlayback>` (`Animation+0x14`); the playback identifier starts at `0xED` from the counter `Animation+0x10`. The `IAnimation2` methods that take a "script" (duration, `DestroyScript`, `FilterObjects`) work on the playback identifier. Animation names are a list sorted by CRC.
- Mixer: `Animation+0x3C` is a map keyed by binding, holding lists of nodes of types leaf (0x18), blend (0x48) and overlay (0x34); nodes are ticked through the node's own vtable. Time step is `Dt*Weight`; the scale of the type-6 family is `Scale*Field_0x30`, the weight is not part of it; the easing table index (256 values `(1-cos(i*pi/255))*0.5`) is `ftol(floor(InvDuration*Time*255))` by the blend node's time. Details of the numeric paths are in the tracker cards.
- Passing over the end of a clip (wrap): `LoopFlags & 2` and the time position outside `[0, Duration]` are taken into account; step = `(EndA - Now) + (L*F - EndB)`.

## Deviations and accepted gaps

- Containers are modern `std::map`/`std::list`/`std::set`, not MSVC6 trees; the archetype name is a `_strdup`, retail uses `basic_string`.
- Some bodies (the mixer paths `0x0661b120...0x0661d8d0`, the bounding-volume cache `0x0661ed90`, the Sphere node size) are marked in the tracker as not fully checked.
- `IEngine` slot 34 (bounding volume): without `I3DMathEngine` the vectors are copied without transformation.
- Parts served by other modules in the project: the nested UTF `CreateInstance` is ReadFile; the mesh part is `IEngineComponent` of the nested FS; the post-spawn of the Thorn menu is the client.
- For `IEngine` slots 41/43 and `IEngineComponent` 7-12 retail is a no-op, not a hole in the port; slot 57 always returns `-1`.
