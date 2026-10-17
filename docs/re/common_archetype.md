# Archetypes: `Archetype::Load*`, `ArchDB`, `ICliObj`

An archetype is a game-data record (ship, solar, equipment, asteroid, explosion) that
`Common.dll` reads from INI files during `DataConfig::startup` and keeps in databases keyed by
`CreateID(nickname)`. This document describes the file formats, the database insertion rules and
the contract with the client "companions" (`ICliObj`) that crosses the `Common.dll` <->
`freelancer.exe` boundary. Code:
`src/include/common_archetype.h`, `src/Common/common_archetype*.cpp`, `common_archetypeloader.cpp`;
the client side is in [freelancer_cliobj.md](freelancer_cliobj.md).

## Classes

```
Root                      base record (nickname, DA_archetype, type, hit_pts, mass, ...)
 ├─ EqObj                 object with equipment (CollisionGroup, shield_link, docking_sphere, fuse)
 │   ├─ Ship              + CommReferrable (multiple inheritance), hp_type
 │   └─ Solar
 ├─ Equipment             volume, use_count, units_per_container, lootable, ...
 │   ├─ LootCrate, Commodity, Power, Scanner, Light, Tractor, Armor, RepairDroid,
 │   │  RepairKit, ShieldBattery, TradeLaneEquip, AttachedFX/InternalFX
 │   ├─ AttachedEquipment → Launcher → Gun; MineDropper, CounterMeasureDropper,
 │   │                      ShieldGenerator, Thruster, CloakingDevice, CargoPod, Shield, Engine
 │   └─ Projectile → Munition, Mine, CounterMeasure
 ├─ Asteroid, DynamicAsteroid   (in the database under Root, not under Equipment)
 └─ Simple                ("simple" explosion/effect objects)
MotorData                 not a Root: nickname, accel, lifetime, delay (separate database)
Explosion                 not a Root: separate database
```

Each class reports `get_class_type()` (the `AClass*` enumeration in the header; 0 for `Root`, 1 for `EqObj`,
6 for `Equipment`, 8 for `LootCrate`, and so on per the table). The virtual slots of `Root` are: destructor, `read(INI_Reader&)`,
`redefine(Root const&)`, `load()` (mesh), `get_class_type()`, `free_resources()`. `create_instance` and
`is_loaded` are non-virtual; `Root::EngineIndex == -1` means "mesh not loaded".

## `ArchDB` databases

One database per kind (`GShipDb`, `GSolarDb`, `GAsteroidDb`, `GDynamicAsteroidDb`, `GSimpleDb`,
`GEquipmentDb`, `GMotorDb`, `GExplosionDb`); the key is `unsigned` = `CreateID(nickname)` = `Root::Nickname`.
Two same-named records of different kinds do not conflict: a ship and a gun with the same nickname live in different databases.

| Situation on insert | Result |
|---|---|
| No `nickname` (id 0) | error `Loaded '%s' has no nickname .. not adding to database.`, the record is deleted |
| New id | the record stays in the database, `Bind` is called on the companion |
| Duplicate, `bReload == false` (how DataConfig always calls it) | warning `... value is not unique. Aborted second version.`, the new record is deleted |
| Duplicate, `bReload == true` | the existing record receives `redefine(new)`, the new one is deleted |

On a miss, `Get*` (`GetShip`, `GetEquipment`, ...) writes `ArchDB::Get(%u|%s) failed` and returns
`nullptr`; `GetExplosion` and the internal `Find` are silent. For network ID compression a vector
"large ID <-> 16-bit small ID" is maintained (`LargeIDToSmallID`/`SmallIDToLargeID`): Simple, Ship, Solar,
Asteroid, DynamicAsteroid and Motor enter it, but Equipment and Explosion do not; the vector
is not cleared by `Archetype::Free`.

## Loaders

All are `__cdecl` in `namespace Archetype`; they return the **increase in the number of records** in the corresponding
databases (0 means the file did not open or is empty; DataConfig only warns in that case).

| Loader | Typical files | Section headers |
|---|---|---|
| `LoadEquipment` | `equipment\*_equip.ini` | `[Motor]`, `[Explosion]`, `[LootCrate]`, `[Commodity]`, `[Power]`, `[Engine]`, `[Armor]`, `[Shield]`, `[ShieldGenerator]`, `[Thruster]`, `[Gun]`, `[MineDropper]`, `[CounterMeasureDropper]`, `[Scanner]`, `[Light]`, `[Tractor]`, `[CargoPod]`, `[AttachedFX]`, `[InternalFX]`, `[TradeLane]`, `[RepairDroid]`, `[RepairKit]`, `[ShieldBattery]`, `[CloakingDevice]`, `[Munition]`, `[Mine]`, `[CounterMeasure]`, `[LOD]` |
| `LoadSolar` | `solar\solararch.ini` | `[Solar]`, `[CollisionGroup]`, `[Explosion]`, `[LOD]` |
| `LoadShips` | `ships\shiparch.ini`, `rtc_shiparch.ini` | `[Ship]`, `[CollisionGroup]`, `[LOD]`, `[Simple]`, `[Explosion]` |
| `LoadAsteroids` | `solar\asteroidarch.ini` | `[Asteroid]`, `[AsteroidMine]` (the same class), `[DynamicAsteroid]` |
| `LoadSimples` | `fx\explosions.ini` | `[Simple]`, `[Explosion]` |

Record block (Ship/Solar/Asteroid/Equipment): (1) if a factory exists, `ICliObjFactory::Create(TypeId)`
yields a companion; (2) the object is created and given the companion; (3) values are read in a loop:
the key `inherit = <nickname>` copies the donor (looked up in the same database; a miss produces the warning
`'inherit' failed. couldn't find %s`) and reading continues; other keys go to `read()`, and keys
the record does not know go to `ICliObj::ReadUnknown`; (4) insertion into the database. `[CollisionGroup]` belongs
to the last Ship/Solar, `[LOD]` to the companion of the last leaf, `[Explosion]` to `LoadExplosion`.
If `read()` returned `false`, `SinglePlayer()` is called (a log side effect); the section is inserted anyway.

Common keys of `Root::read`: `nickname`, `DA_archetype`/`attachment_archetype` (mesh path),
`type` (`OBJECT_TYPE` bit mask, 29 names), `ids_name`, `ids_info`, `explosion_arch`,
`explosion_resistance`, `hit_pts`, `mass`, `rotation_inertia`, `phantom_physics`, `mission_property`.
`EqObj`: `docking_camera`, `docking_sphere`, `fuse`, `shield_link` (nick, hardpoint, generator hardpoint...),
`nomad`. `[CollisionGroup]`: `obj`, `type` (19 names: Fin, ..., Starboard_Arm), `hit_pts`, `separable`,
`destroy_parent`, `root_health_proxy`, `parent_impulse`/`child_impulse`, `mass`, `rotation_inertia`,
`separation_explosion`, `debris_type`, `explosion_resistance`, `dmg_hp`/`dmg_obj`, `fuse`,
`linked_equip`. Groups receive IDs starting at 4 (the counter starts at 3, incremented with `++` before being handed out).
`[Motor]`: `nickname`, `accel`, `lifetime`, `delay` (defaults 15 / 1.5 / 0).
`[Explosion]` (`Explosion`): `nickname`, `radius`, `impulse`, `hull_damage`, `energy_damage`,
`process` (`shatter`/`disappear`/`none`), `lifetime`, `effect` (id [, strength]), `num_child_pieces`,
`debris_impulse`, `innards_debris_*`, `debris_type`.

## The `ICliObjFactory` / `ICliObj` contract

The client factory is the single `ICliObjFactory` object (in the exe, `src/freelancer/Client/client_cliobj.*`)
with one slot `Create(int TypeId, ICliObj** Out)`; Server passes `nullptr`, in which case `Root::Companion == nullptr`
and all companion branches are skipped. The companion is the user-facing part of a record (materials, LOD,
hit effects, camera settings); the `ICliObj` slots are: `AddRef`, `Release`, `ReadUnknown`, `Lod`
(`[LOD]`), `Redefine`, `Bind` (after insertion into the database), `FreeResources`, `Inherit`, `Load`, then
the unnamed `Slot09...13` (destructor, cache clearing). Type identifiers:

| TypeId | Section | TypeId | Section |
|---:|---|---:|---|
| 0 | `[Simple]` | 0x15 | `[Light]` |
| 2 | `[Solar]` | 0x16 | `[Tractor]` |
| 3 | `[Ship]` | 0x17 | `[AttachedFX]` |
| 4 | `[Asteroid]`/`[AsteroidMine]` | 0x18 | `[InternalFX]` |
| 5 | `[DynamicAsteroid]` | 0x19 | `[RepairDroid]` |
| 8 | `[LootCrate]` | 0x1A | `[RepairKit]` |
| 9 | `[CargoPod]` | 0x1B | `[ShieldBattery]` |
| 0xA | `[Commodity]` | 0x1C | `[CloakingDevice]` |
| 0xB | `[Power]` | 0x1D | `[TradeLane]` |
| 0xC | `[Engine]` | 0x1F | `[Munition]` |
| 0xD | `[Shield]` | 0x20 | `[Mine]` |
| 0xE | `[ShieldGenerator]` | 0x21 | `[CounterMeasure]` |
| 0xF | `[Thruster]` | 0x22 | `[Armor]` |
| 0x11 | `[Gun]` | 0x12/0x13 | `[MineDropper]`/`[CounterMeasureDropper]` |
| 0x14 | `[Scanner]` | | |

`[Motor]` and `[Explosion]` do not call the factory. The companion is copied/inherited together with the record
(`Root::operator=` does `AddRef` on the new one and `Release` on the old one — `inherit =` broke for
equipment when the pointer was assigned without `AddRef`).

## Mesh and physical parts

`Root::load` → `Archetype::Load(path)` puts the mesh into the `DALib::Engine` index (key is `DACOM_CRC::GetCRC32`
of the path) and records `EngineIndex`; then (except for `.sph`) `PhyArch::LoadSurfaces` reads the neighbouring `.sur`
(format in [common.md](common.md), section "Archetypes"). `Munition::load` with an empty `DA_archetype`
uses the shared beam archetype `BEAM_ARCH` (`LoadBeamArchIdx` at the end of `LoadEquipment`).
`Archetype::Free` walks the databases, frees the records and releases `BEAM_ARCH`; `FreeExplosions` clears the explosions.

## Known deviations

- Donor copying on `inherit =` is done by the `operator=` of the leaf classes (`EqObj`, `Ship`,
  `Solar`, the `Equipment` family: `AttachedFXEquip`, `Launcher`, `Munition`, ...), not by the common
  `Root::operator=`.
- The stores for `fuse`/`docking_sphere`/`shield_link`/`CollisionGroup`/the physical-parts map are
  modern containers (often by pointer in the slot of an old VC6 list), not VC6 structures.
- Per-function status and specific gaps are in decomp-tracker (`Common.dll`).
