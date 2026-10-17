# Client archetype companions (`Archetype::ICliObj`)

Every archetype record that `Common.dll` loads on the client gets a "companion" — a client-side
object with data that only `freelancer.exe` needs: materials and LODs, hit effects, ship camera
settings, sounds and equipment lights. The factory and all companions are exe code
(`src/freelancer/Client/client_cliobj.*`, retail `Client\cmn\CliArch.cpp`, `ArchLod.cpp`); only the
`ICliObj` interfaces (`src/include/common_archetype.h`) and `ICliObjFactory`
(`src/include/common_cliobj.h`) cross the exe ↔ Common boundary. The Common-side contract (which
`TypeId` for which section, when which slot is called) is in [common_archetype.md](common_archetype.md).

Not to be confused with the other `Cli*` family in the exe — runtime objects in space (`ClientEquip`,
`CliGun`, `CliEngine`, `SpaceDustPerf`, …, retail `Client\Space\*.cpp`): those are live objects, not
`Root+0x04` companions (see [equip_fx_factory.md](equip_fx_factory.md)).

## Hierarchy

```
ICliObj
├── CliRoot                      base, 0x54 bytes
├── CliShip, CliSolar            0x88: ship camera / DEFAULT_TEXTURE
├── CliProj ── CliMunition       projectiles (0x60 / 0x84)
├── CliAsteroid, CliDynamicAsteroid
└── CliEQEngine, CliEQShieldGenerator, CliEQThruster, CliEQLauncher, CliEQGun, CliEQLight,
    CliEQTractor, CliEQAttachedFX, CliEQInternalFX, CliEQTradeLane, CliEQRepairDroid, CliEQShield
ICliObjFactory ── CliFactory     singleton (DAT_006164a0), slot 0 = Create(TypeId, ICliObj**)
```

Class names are confirmed by RTTI and by `__RTDynamicCast` in `Inherit`. By `TypeId` (list in
common_archetype.md) the factory creates an object of the corresponding size; `CliShip`/`CliSolar` are
0x88, `CliEQEngine` is 0xAC, `CliEQGun` is 0x88, `CliEQLauncher` (mine/countermeasure droppers) is
0x84, `CliEQLight` is 0x98, `CliEQTradeLane` is 0xC8, `CliMunition` is 0x84; the rest range from 0x54
to 0x70. In the port every retail class is its own class (`FCliShip`, `FCliMunition`, `FCliEngine`,
`FCliGun`, `FCliEQLight`, `FCliTradeLane`, …), with no shared `FCliSized<N>` placeholders.

## Base slots (14, vtable `005c7734`)

| Slot | Role |
|---:|---|
| 0, 1 | `AddRef`, `Release` (at zero: `Slot11`, then the destructor) |
| 2 | `ReadUnknown` — a key unknown to `Root::read` |
| 3 | `Lod` — the `[LOD]` section |
| 4 | `Redefine` — copy the donor companion |
| 5 | `Bind` — after the record is inserted into `ArchDB` |
| 6 | `FreeResources` (→ `Slot11`) |
| 7 | `Inherit` — `inherit =`; the base is almost empty, subclasses override it |
| 8 | `Load` — resource loading (during `Root::load`) |
| 9–13 | unnamed: deleting destructor, dropping the cache of resolved IDs and effects, walking the distance records |

Names 2–8 are the port's catalogue (the PE has no strings); the roles are confirmed by the call
sites in `Archetype::Load*`.

## Base (`CliRoot`, 0x54 bytes)

| Field | Contents |
|---|---|
| `+0x04` | head of the `[LOD]` list (0x34 nodes: `obj` hash, up to 8 `LODranges` ranges, `MinSpecLOD`) |
| `+0x08`, `+0x0C` | `DistanceRender` and the `LodPick` cache (default −1) |
| `+0x10` | `CacheString envmap_material` |
| `+0x14`, `+0x18` | `Root*` (after `Bind`) and the reference count |
| `+0x20…+0x3C` | material paths (`{path, libId}`, stride 8) and the vector of resolved material records |
| `+0x40…+0x4C` | `surface_hit_effects` distance records (stride 0x24: threshold, effect IDs, live `EffectArch*`) |
| `+0x50` | state flags (cache built, `Slot10` done, engine side, `Slot12` done, `LodPick` computed) |

Base keys (`ReadUnknown`): `material_library` (path [, number]), `LODranges`, `MinSpecLOD`,
`distance_render`, `envmap_material`, `surface_hit_effects` (threshold + list of effect IDs); an
unknown key yields `false`, and a subclass may continue. A retail quirk reproduced in the port:
recomputing `LodPick` while `Root` is not loaded exits early and does **not** touch the old value.

## Subclasses

- **`CliShip`** (0x88): `camera_offset` (2 floats), `camera_angular_acceleration`,
  `camera_angular_slerp_multiplier`, turn angles (radians after clamping),
  `camera_turn_look_ahead_slerp_amount` (default 0.5), cockpit-ini as a `CacheString`; `Redefine`
  copies the LODs and camera fields, `Inherit` calls the base.
- **`CliSolar`** (0x88): `DEFAULT_TEXTURE` (inline, up to 31 characters).
- **`CliProj` / `CliMunition`**: `const_effect_delay`, `const_effect`, `one_shot_sound`,
  `HP_trail_parent`, the `munition_hit_effect` list and a vector of live `EffectArch*`; `Load` resolves
  effects by ID (`GEffectArchById`), `Slot11` drops them, a live effect is chosen by `PickLiveEffect`.
- **`CliEQ*`**: each class reads its own keys of the equipment section (engine — thrust effects and
  sounds, `Light` — light and glow parameters, `TradeLane` — ring scrolling, `ShieldGenerator` —
  `shield_hit_effects`, `Launcher`/`Gun` load the projectile companion).

## Lifecycle

`DataConfig::startup` → `Archetype::Load*` → `Factory->Create` → `new Root` descendant with a
companion → INI (`ReadUnknown`/`Lod`) → insertion into `ArchDB` → `Bind`; `inherit =` →
`Inherit(donor)`; `Root::load` → `Load(mode)`; `Root::free_resources` → `FreeResources`/`Release`.
The record copy `Root::operator=` must `AddRef` the new companion and `Release` the old one
(otherwise the very first heir destroys the donor's companion).

## Known gaps

- The first load of a VisEffect `.ale` (`LoadAlePath`) belongs to VisEffectArch/alchemy; EffectArch
  only honours the reference-count contract.
- `Inherit`/`Load` of individual `CliEQ*` classes and the names of the `Mine`/`CounterMeasure` leaves
  (`TypeId 0x20/0x21`) are being refined via the tracker.
