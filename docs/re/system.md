# system.dll — SystemContainer

`system.dll` is a DACOM aggregate that owns the components of the `[System]` section of `DACOM.INI` (RP8, sound, textures, Alchemy and others). `DALib::Startup` creates one instance and obtains the engine interfaces from it through `QueryInterface`. This document describes the aggregation contract, the object layout and the load order. The port is `src/system/system.cpp`; slot addresses and function statuses are in decomp-tracker (listing `EXE/Disasm/system.dll.txt`; Ghidra did not decompile `LoadSystemComponents`, read the body in the `.txt`).

The DLL has no named PE exports: only `DllMain`, which registers the `"SystemContainer"` factory with `ICOManager` at priority `0x40000000`. The factory answers QI only for `"1.11_IComponentFactory"` (case-sensitive comparison, including the NUL).

## Creation descriptor (`0x14` bytes)

`CreateInstance` requires size `0x14` and the name `"SystemContainer"`; anything else gives `0xFFFFFFFD`. Fields: `+8` is the outer (controlling) `IUnknown`, `+0xC` is the `IUnknown**` for the inner one (written if an outer one is given), `+0x10` is reserved (if it equals `"SystemContainer"`, creation refuses with `-1`). Out of memory gives `0xFFFFFFFC`. This is `FAGGDESC14` from `src/include/dacom.h` (the flat two-field `AGGDESC` is for ordinary factories); for other components (ILua) the `+8` field may mean something else, so `system.cpp` has its own local `FSystemAggDesc`.

## Object (`0x28` bytes, `calloc`)

| Offset | Field |
|----------|------|
| `+0x00` | vptr of `ISystemContainer` / `IAggregateComponent` / `ISystemComponent` (one pointer for all three) |
| `+0x04` | vptr of `IDAConnectionPointContainer` |
| `+0x08` / `+0x0C` | head / tail of the child component list |
| `+0x10` | inner non-delegating `IUnknown` (vptr, counter, back reference) |
| `+0x1C` | controlling unknown (the inner one by default) |
| `+0x20` | `bAggregated` (an outer unknown is set) |
| `+0x24` | `bLoaded` (after a successful `LoadSystemComponents`) |

QI for `ISystemContainer`, `IAggregateComponent`, `ISystemComponent` returns the same pointer (offset 0), for `IDAConnectionPointContainer` offset 4; an unknown IID is forwarded to the children's inner unknowns, otherwise `0xFFFFFFFD` and `*out = 0`. A child node (`0x14`): `Next`, the inner `IUnknown*`, the created object, the cached `ISystemComponent*` and `IAggregateComponent*` (without AddRef).

## Interface contracts

- `IAggregateComponent::Initialize` / `Shutdown` walk the children; if `Initialize` fails, the child is removed from the list. The string `ISystemComponent` does not occur in the children of any retail DLL (they implement only `IAggregateComponent`), so `Shutdown` on the FL load path is a no-op; `DALib::Shutdown` releases Pipeline/Sound and calls `ICOManager::Shutdown`.
- `ISystemContainer`: `LoadSystemComponents`, `UnloadSystemComponents`, `AddComponent(AGGDESC*)`.
- `IDAConnectionPointContainer`: two methods with two pointer arguments each; they are **not** `Advise`/`Unadvise` and not OLE `EnumConnectionPoints`. Both iterate over the children and stop at the first success; defaults: the lookup gives `-1`, the second gives `1`. No child has such a facet and nothing in FL calls them; the names `FindConnectionPoint` / `EnumConnectionPoints` are taken from the arity, and the second method is not proven beyond its arity.

## Loading components

`DALib::Startup` creates the container (`CreateInstance("SystemContainer", 0x14)`), calls `Initialize` (there are no children yet), takes `ISystemContainer` and calls `LoadSystemComponents`: QI on `ICOManager` for `"1.11_IProfileParser"` (dacom's built-in parser, see [dacom.md](dacom.md)), `OpenSection("System", 3)`, then `ReadLine` by index; for each non-empty line `CreateInstance(name)` with the container as the outer unknown and a NOTICE through `FDUMP` ("Loading '%s' [%s] returned %s"). If the container is not aggregated, its `Initialize` is called at the end (it initialises the children). Retail does not call `CloseSection`. If QI/`OpenSection` fails, the function returns OK but creates no children and does not set `bLoaded`; `DALib` then gets a null Pipeline/Sound, and the cause is not in system.dll.

The composition of the `[System]` section is in `bin/DACOM.INI` (VMeshLibrary, DirectX8, SoundManager, SoundStreamer, TextureLibrary, MaterialAnimation, MaterialBatcher, Alchemy, FxRuntime).

## What the port repeats literally

The port reproduces retail behaviour without any guards of its own: no null checks where retail has none (child QI, container forwarding, factory), `Tail` stays stale after a failed `Initialize`, `Shutdown` returns the same EAX value as retail; `AddComponent` restores `Outer`/`InnerOut` on success too; `DllMain` `DETACH` is a no-op. There are no known discrepancies. In `bin/DACOM.INI` the library is given by an absolute path into `bin\`.
