# Archetype files and equipment sections

The archetype loader (`Archetype::LoadEquipment`, `LoadSolar`, `LoadShips`, `LoadAsteroids`, `LoadSimples` in Common.dll) reads the "template" records that every game object is created from. This page describes how those files are walked, the keys shared by every record (`Root`), and the sections that describe non-weapon equipment plus `[Motor]` and `[Explosion]`. Weapons are in [archetypes-weapons.md](archetypes-weapons.md), ships/solars/asteroids in [archetypes-ships-solars-asteroids.md](archetypes-ships-solars-asteroids.md), goods/loadouts/constants in [goods-loadouts-constants.md](goods-loadouts-constants.md).

**File location / how it is found.** All files are listed in `freelancer.ini` `[Data]` and resolved against the data path (`GetFileSysPath(.., nullptr)`), see [freelancer-ini.md](freelancer-ini.md). The `[Data]` key selects the loader ([common_dataconfig.cpp:42](../../src/Common/common_dataconfig.cpp#L42)); repeated keys are loaded in file order, and the order matters (see "Cross-record references" below).

| `[Data]` key | Loader | Sections understood |
|---|---|---|
| `equipment` (repeated) | `LoadEquipment` | `Motor`, `Explosion`, the 25 equipment kinds below, `LOD` |
| `solar` | `LoadSolar` | `Solar`, `CollisionGroup`, `Explosion`, `LOD` |
| `ships` (repeated) | `LoadShips` | `Ship`, `CollisionGroup`, `LOD`, `Simple`, `Explosion` |
| `asteroids` | `LoadAsteroids` | `Asteroid`, `AsteroidMine`, `DynamicAsteroid` |
| `explosions` (and `debris`, the same file) | `LoadSimples` | `Simple`, `Explosion` |

Any other section name in these files is skipped without a message ([common_archetypeloader.cpp:453](../../src/Common/common_archetypeloader.cpp#L453)). The files are read as BINI or text by `INI_Reader` ([parsers.md](parsers.md)); section and key names are case-insensitive.

## Value notation used on these pages

| Type | Getter | Notes |
|---|---|---|
| `int` | `get_value_int(i)` | MSVCRT `atoi` of the parameter: decimal only, wraps modulo 2^32, no hex. |
| `float` | `get_value_float(i)` / `value_num(i)` | `ParseNumber` (accepts `0x` hex etc. in text files); stored as `float` unless noted. |
| `bool` | `get_value_bool(i)` | Text files accept exactly `true`/`1`/`false`/`0` (case-insensitive); anything else logs "Invalid bool value" and gives `false`. |
| `str` (whole value) | `get_value_string()` | The entire right-hand side, commas included, as one string. |
| `str[i]` | `get_value_string(i)` | One comma-separated parameter, trimmed. |
| `id` | `CreateID(string)` | 32-bit hash of the text (lower-cased first, text truncated at 255 chars, null gives 0). Records are stored and looked up by this hash, so nicknames are case-insensitive and two names with the same hash collide. Used for every "nickname" and every reference to another record. |
| `cstr` | `StringAlloc(text, flag)` | Text interned in the process string pool; no interpretation. |

"Default" is the value assigned by the record constructor; there is no generic default mechanism. A key that is absent leaves the default, a repeated scalar key keeps the last occurrence. Whether a value is range-checked is stated per key; if nothing is said, it is not validated.

## Loader behaviour (all archetype files)

* A record is created at its `[Section]` header and filled by calling `read()` on every key in order. Keys the record class does not know are offered to the **client companion** object (`ICliObj::ReadUnknown`, client only; the keys it consumes are in [client-world-fx-archetypes.md](client-world-fx-archetypes.md)). Whether an unknown key is reported is decided there; on the server (no companion) it is silently ignored for `Ship`/`Solar` and equipment (`SinglePlayer()` is called and its result dropped), and for `Asteroid`/`DynamicAsteroid`/`Motor`/`Explosion` the result is not examined at all.
* `[LOD]` sections after a record are handed to the companion of the **last successfully inserted** record; they are not read by Common.
* A record needs a `nickname`. Without one the insert fails ("Loaded '%s' has no nickname .. not adding to database"). Duplicate nicknames: `Equipment`, `Asteroid`, `DynamicAsteroid`: the first record wins, the later section is dropped silently. `Ship`: the first wins and a warning "nickname = %s value is not unique" is logged. `Solar`: the existing record is updated from the duplicate through `Root::redefine` plus `toughness` (ids_name, ids_info, hit_pts, explosion_arch, explosion_resistance, mass, rotation_inertia, toughness), the rest of the duplicate is ignored. `Simple`: first wins plus a warning. (The "reload" arm that redefines duplicates is never selected by `DataConfig`.)
* An empty result ("contains no new definitions") logs a warning naming the file for `equipment`, `solar`, `ships`, `asteroids`, `explosions`, `loadouts`.
* `Archetype::LoadEquipment` also creates the shared `BEAM_ARCH` engine archetype after every file, whether or not the file opened.

### `inherit` (equipment, ship, solar, asteroid records)

`inherit = <nickname>` copies a previously loaded record of the same kind into the record being read and must come **before** the keys that should override it. The nickname of the record being read is preserved. Details that differ per kind:

* **Equipment** ([common_archetype_equipment.cpp:66](../../src/Common/common_archetype_equipment.cpp#L66)): the donor is looked up in the equipment database; if the donor's class differs from the record's class nothing is copied (no message). A missing donor logs "'inherit' failed. couldn't find %s". `Armor` donors are not copied at all (that class falls to the empty arm of the copy switch). `Gun`, `ShieldGenerator`, `Munition`, `Mine`, `CounterMeasure`, the droppers and all leaf classes copy their own fields.
* **Ship**, **Solar**, **Asteroid**: handled by the loader before `read()` is called ([common_archetypeloader.cpp:294](../../src/Common/common_archetypeloader.cpp#L294), [:361](../../src/Common/common_archetypeloader.cpp#L361), [:404](../../src/Common/common_archetypeloader.cpp#L404)); see the ship page for exactly which fields are copied. A missing donor logs a warning (Ship, Asteroid) or is silently ignored (Solar).

### Cross-record references

References are resolved when the key is read, not afterwards: `inherit`, `loot_appearance`, `pod_appearance` (equipment) and `inherit` (ships/solars/asteroids) look the target up in what has been loaded so far. A target defined later in the same file, or in a later `[Data]` entry, is not found (the lookup logs `ArchDB::Get(...) failed` and the reference stays empty). References that are only stored as an `id` (`explosion_arch`, `projectile_archetype`, `motor`, `loadout`, ...) are resolved later at use time.

## Keys of every record (`Root::read`)

Every section of every archetype file accepts these ([common_archetype.cpp:468](../../src/Common/common_archetype.cpp#L468)).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | id (`str[0]`) | none (record rejected) | Record key. | [L470](../../src/Common/common_archetype.cpp#L470) |
| `DA_archetype` / `attachment_archetype` | cstr | empty | Path of the visual model file (the shipped data uses `.3db` and `.cmp` files); resolved by the engine, not validated by the loader. Both spellings set the same field. If empty on a Munition the shared `BEAM_ARCH` is used. | [L476](../../src/Common/common_archetype.cpp#L476) |
| `type` | object-type name (`str[0]`) | 0 | One of the object-type names in the table below (case-insensitive). An unknown name logs "unknown OBJECT_TYPE" and gives 0. Constructors preset the type for some classes (see below). | [L482](../../src/Common/common_archetype.cpp#L482) |
| `ids_name` | int | 0 | String-resource id of the display name. | [L488](../../src/Common/common_archetype.cpp#L488) |
| `ids_info` | int | 0 | String-resource id of the infocard. | [L494](../../src/Common/common_archetype.cpp#L494) |
| `explosion_arch` | id | 0 | `[Explosion]` nickname played on destruction. | [L500](../../src/Common/common_archetype.cpp#L500) |
| `explosion_resistance` | float | 1.0 | Scale of explosion damage taken. | [L506](../../src/Common/common_archetype.cpp#L506) |
| `hit_pts` | float | 100.0 (Equipment: 1.0, Shield: 0.0) | Hit points. | [L512](../../src/Common/common_archetype.cpp#L512) |
| `mass` | float | 1.0 | Mass. | [L518](../../src/Common/common_archetype.cpp#L518) |
| `rotation_inertia` | float x3 (`[0]`,`[1]`,`[2]`) | none (flag off) | Moment of inertia X, Y, Z; presence sets the "has rotation inertia" flag. | [L524](../../src/Common/common_archetype.cpp#L524) |
| `phantom_physics` | bool | false | Phantom-physics flag (consumed by the physics code). | [L533](../../src/Common/common_archetype.cpp#L533) |
| `mission_property` | none | - | Recognised and ignored. | [L539](../../src/Common/common_archetype.cpp#L539) |

Constructor-preset object types (a later `type` key overrides them; for a `Munition` a later `seeker` key resets the type again): `LootCrate`, `Commodity` = `LOOT`; `Munition` = `BULLET` (becomes `GUIDED` as soon as `seeker` is `dumb` or `lock`, back to `BULLET` for any other `seeker` value); `Mine` = `MINE`; `Asteroid` = `ASTEROID`; `DynamicAsteroid` = internal value `0x40000000` (no name in the table).

### Object-type names (`type =`)

Table `GObjectTypes` ([common_archetype.cpp:300](../../src/Common/common_archetype.cpp#L300)); the value is the object-type bit mask. Matching is case-insensitive; the first match wins.

| Name | Bits | Name | Bits | Name | Bits |
|---|---|---|---|---|---|
| `NONE` | 0 | `MOON` | 0x1 | `PLANET` | 0x2 |
| `SUN` | 0x4 | `BLACKHOLE` | 0x8 | `SATELLITE` | 0x10 |
| `DOCKING_RING` | 0x20 | `JUMP_GATE` | 0x40 | `TRADELANE_RING` | 0x80 |
| `STATION` | 0x100 | `WAYPOINT` | 0x200 | `AIRLOCK_GATE` | 0x400 |
| `JUMP_HOLE` | 0x800 | `WEAPONS_PLATFORM` | 0x1000 | `DESTROYABLE_DEPOT` | 0x2000 |
| `NON_TARGETABLE` | 0x4000 | `MISSION_SATELLITE` | 0x8000 | `FIGHTER` | 0x10000 |
| `FREIGHTER` | 0x20000 | `GUNBOAT` | 0x40000 | `CRUISER` | 0x80000 |
| `TRANSPORT` | 0x100000 | `CAPITAL` | 0x200000 | `MINING` | 0x400000 |
| `GUIDED` | 0x1000000 | `BULLET` | 0x2000000 | `MINE` | 0x4000000 |
| `LOOT` | 0x10000000 | `ASTEROID` | 0x20000000 | | |

The same table is used by `[CollisionConsts] damage` in `constants.ini` ([goods-loadouts-constants.md](goods-loadouts-constants.md)).

## Keys of every equipment record (`Equipment::read`)

Added by the base class of all 25 equipment kinds ([common_archetype_equipment.cpp:61](../../src/Common/common_archetype_equipment.cpp#L61)). `Root` keys are tried first.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `inherit` | nickname (`str`) | - | See "inherit" above. | [L66](../../src/Common/common_archetype_equipment.cpp#L66) |
| `use_count` | bool | true | Item is counted (stackable) in cargo. | [L311](../../src/Common/common_archetype_equipment.cpp#L311) |
| `units_per_container` | int | 1 | Units per cargo container. | [L317](../../src/Common/common_archetype_equipment.cpp#L317) |
| `volume` | float | 0.0 | Cargo volume. | [L331](../../src/Common/common_archetype_equipment.cpp#L331) |
| `tractored_explosion` | id | 0 | Explosion played when tractored. | [L338](../../src/Common/common_archetype_equipment.cpp#L338) |
| `lootable` | bool | true | Can drop as loot. | [L344](../../src/Common/common_archetype_equipment.cpp#L344) |

(`hit_pts` is tested again in `Equipment::read` but can never be reached because `Root::read` consumes it first.)

## Attached equipment keys (`AttachedEquipment::read`)

Shared by `Shield`, `ShieldGenerator`, `Thruster`, `CargoPod`, `CloakingDevice` and every weapon launcher ([common_archetype_equipment.cpp:454](../../src/Common/common_archetype_equipment.cpp#L454)). Applied after `Equipment` keys.

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `separation_explosion` | id (`str[0]`) | 0 | Explosion when the part breaks off. | [L459](../../src/Common/common_archetype_equipment.cpp#L459) |
| `debris_type` | id (`str[0]`) | 0 | Debris archetype of the part. | [L465](../../src/Common/common_archetype_equipment.cpp#L465) |
| `child_impulse` | float | 0.0 | Impulse given to the detached part. | [L471](../../src/Common/common_archetype_equipment.cpp#L471) |
| `parent_impulse` | float | 0.0 | Impulse given to the parent. | [L477](../../src/Common/common_archetype_equipment.cpp#L477) |
| `HP_child` | cstr (`str[0]`) | empty | Child hardpoint name. | [L483](../../src/Common/common_archetype_equipment.cpp#L483) |
| `toughness` | int | 0 | Toughness rank. | [L489](../../src/Common/common_archetype_equipment.cpp#L489) |

## Equipment sections

The section name selects the class. The table below is the loader's dispatch table ([common_archetypeloader.cpp:27](../../src/Common/common_archetypeloader.cpp#L27)); all names are matched case-insensitively and each section may appear any number of times.

| Section | Class | Reads | Notes |
|---|---|---|---|
| `LootCrate` | LootCrate | Root + Equipment | Object type preset to `LOOT`. No own keys. |
| `Commodity` | Commodity | + `decay_per_second`, `loot_appearance`, `pod_appearance` | Object type preset to `LOOT`. |
| `Power` | Power | + 4 keys | |
| `Engine` | Engine | + 7 keys | |
| `Armor` | Armor | + `hit_pts_scale` | |
| `Shield` | Shield | Root + Equipment + AttachedEquipment | `hit_pts` default is 0. No own keys. |
| `ShieldGenerator` | ShieldGenerator | + 9 keys | |
| `Thruster` | Thruster | + 2 keys | |
| `Gun`, `MineDropper`, `CounterMeasureDropper` | launchers | see [archetypes-weapons.md](archetypes-weapons.md) | |
| `Scanner` | Scanner | + 3 keys | |
| `Light` | Light | + 2 keys | |
| `Tractor` | Tractor | + 2 keys | |
| `CargoPod` | CargoPod | AttachedEquipment keys | No own keys. |
| `AttachedFX` | AttachedFXEquip | Root + Equipment | No own keys (the client companion reads its visual keys). |
| `InternalFX` | InternalFXEquip | + `use_animation` | |
| `TradeLane` | TradeLaneEquip | Root + Equipment | No own keys. |
| `RepairDroid` | RepairDroid | + `repair_rate` | |
| `RepairKit` | RepairKit | + `loot_appearance` | |
| `ShieldBattery` | ShieldBattery | + `loot_appearance` | |
| `CloakingDevice` | CloakingDevice | + 5 keys | |
| `Munition`, `Mine`, `CounterMeasure` | projectiles | see [archetypes-weapons.md](archetypes-weapons.md) | |

Example (synthetic):

```ini
[Engine]
nickname = demo_engine_01
ids_name = 1000
DA_archetype = equipment\models\demo_engine.cmp
hit_pts = 2000
volume = 5
max_force = 30000
power_usage = 20
linear_drag = 0.4
```

### `[Commodity]`  ([L1346](../../src/Common/common_archetype_equipment.cpp#L1346))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `decay_per_second` | float | 0.0 | Hit points lost per second in space; the runtime value is capped at 25 % of `hit_pts`. | [L1351](../../src/Common/common_archetype_equipment.cpp#L1351) |
| `loot_appearance` | equipment nickname | none (the commodity itself) | Equipment whose model is shown as loot. Resolved at read time (see cross-record references). | [helper L145](../../src/Common/common_archetype.cpp#L145), [call L1357](../../src/Common/common_archetype_equipment.cpp#L1357) |
| `pod_appearance` | `CargoPod` nickname | none | Cargo pod record. Stored only if the found record is a `CargoPod`; any other class is ignored silently. | [L1360](../../src/Common/common_archetype_equipment.cpp#L1360) |

### `[Power]`  ([L548](../../src/Common/common_archetype_equipment.cpp#L548))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `capacity` | float | 0.0 | Energy capacity. | [L553](../../src/Common/common_archetype_equipment.cpp#L553) |
| `charge_rate` | float | 0.0 | Recharge rate. | [L559](../../src/Common/common_archetype_equipment.cpp#L559) |
| `thrust_capacity` | float | 0.0 | Thruster energy capacity. | [L565](../../src/Common/common_archetype_equipment.cpp#L565) |
| `thrust_charge_rate` | float | 0.0 | Thruster recharge rate. | [L571](../../src/Common/common_archetype_equipment.cpp#L571) |

### `[Engine]`  ([L622](../../src/Common/common_archetype_equipment.cpp#L622))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `power_usage` | float | 0.0 | Power draw factor. | [L627](../../src/Common/common_archetype_equipment.cpp#L627) |
| `cruise_power_usage` | float | 0.0 | Power draw in cruise. | [L633](../../src/Common/common_archetype_equipment.cpp#L633) |
| `cruise_charge_time` | float | 0.0 | Seconds to charge cruise. | [L639](../../src/Common/common_archetype_equipment.cpp#L639) |
| `max_force` | float | 0.0 | Thrust force. | [L645](../../src/Common/common_archetype_equipment.cpp#L645) |
| `linear_drag` | float | 0.0 | Linear drag added by the engine. | [L651](../../src/Common/common_archetype_equipment.cpp#L651) |
| `indestructible` | bool | false | Engine cannot be destroyed. | [L657](../../src/Common/common_archetype_equipment.cpp#L657) |
| `reverse_fraction` | float | 0.0 | Fraction of thrust available in reverse. | [L663](../../src/Common/common_archetype_equipment.cpp#L663) |

### `[Armor]`  ([L706](../../src/Common/common_archetype_equipment.cpp#L706))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `hit_pts_scale` | float | 1.0 | Multiplier applied to the ship's hit points. | [L711](../../src/Common/common_archetype_equipment.cpp#L711) |

### `[ShieldGenerator]`  ([L865](../../src/Common/common_archetype_equipment.cpp#L865))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `hp_type` | hardpoint-type name (`str[0]`) | `hp_shield_generator` (5) | One entry of the hardpoint-type table in [archetypes-weapons.md](archetypes-weapons.md#hardpoint-type-names). **An unknown name sets the value 7 (`hp_elite_shield_generator`), not the default.** | [L870](../../src/Common/common_archetype_equipment.cpp#L870) |
| `shield_type` | id | 0 | Shield type record (used by the shield-link table of a ship). | [L882](../../src/Common/common_archetype_equipment.cpp#L882) |
| `regeneration_rate` | float | 0.0 | Shield regeneration per second. | [L888](../../src/Common/common_archetype_equipment.cpp#L888) |
| `max_capacity` | float | 0.0 | Shield capacity. | [L894](../../src/Common/common_archetype_equipment.cpp#L894) |
| `constant_power_draw` | float | 0.0 | Power draw while online. | [L900](../../src/Common/common_archetype_equipment.cpp#L900) |
| `rebuild_power_draw` | float | 0.0 | Power draw while rebuilding. | [L906](../../src/Common/common_archetype_equipment.cpp#L906) |
| `offline_rebuild_time` | float | 0.0 | Seconds to rebuild after going offline. | [L912](../../src/Common/common_archetype_equipment.cpp#L912) |
| `offline_threshold` | float | 0.0 | Capacity fraction at which the shield goes offline. | [L918](../../src/Common/common_archetype_equipment.cpp#L918) |
| `hp_shield_type` | list of hardpoint-type names (`str[0..n]`) | empty list | Hardpoint types the generator can be mounted on, one per parameter until the first empty one. A name that is not in the hardpoint-type table is skipped silently. Repeated keys **append**. An empty list means "the built-in 34-entry list" (all `hp_*shield*` types). | [L924](../../src/Common/common_archetype_equipment.cpp#L924) |

### `[Thruster]`  ([L987](../../src/Common/common_archetype_equipment.cpp#L987))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `power_usage` | float | 0.0 | Power draw. | [L993](../../src/Common/common_archetype_equipment.cpp#L993) |
| `max_force` | float | 0.0 | Thrust force. | [L999](../../src/Common/common_archetype_equipment.cpp#L999) |

### `[Scanner]`  ([L1029](../../src/Common/common_archetype_equipment.cpp#L1029))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `power_usage` | float | 0.0 | Stored as the power threshold. | [L1034](../../src/Common/common_archetype_equipment.cpp#L1034) |
| `range` | float | 1000.0 | Radar range. | [L1040](../../src/Common/common_archetype_equipment.cpp#L1040) |
| `cargo_scan_range` | float | 0.0 | Cargo scan range. | [L1046](../../src/Common/common_archetype_equipment.cpp#L1046) |

### `[Light]`  ([L1086](../../src/Common/common_archetype_equipment.cpp#L1086))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `always_on` | bool | true | Light is permanently lit. | [L1091](../../src/Common/common_archetype_equipment.cpp#L1091) |
| `docking_light` | bool | false | Light is a docking light. | [L1097](../../src/Common/common_archetype_equipment.cpp#L1097) |

### `[Tractor]`  ([L1117](../../src/Common/common_archetype_equipment.cpp#L1117))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `max_length` | float | 0.0 | Maximum beam length. | [L1121](../../src/Common/common_archetype_equipment.cpp#L1121) |
| `reach_speed` | float | 100.0 | Speed at which the beam extends. | [L1126](../../src/Common/common_archetype_equipment.cpp#L1126) |

### `[InternalFX]`  ([L1197](../../src/Common/common_archetype_equipment.cpp#L1197))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `use_animation` | cstr (whole value) | empty | Animation script played when the equipment is used. | [L1202](../../src/Common/common_archetype_equipment.cpp#L1202) |

### `[RepairDroid]`  ([L1230](../../src/Common/common_archetype_equipment.cpp#L1230))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `repair_rate` | float | 0.0 | Repair rate. | [L1235](../../src/Common/common_archetype_equipment.cpp#L1235) |

### `[RepairKit]` and `[ShieldBattery]`  ([L1404](../../src/Common/common_archetype_equipment.cpp#L1404), [L1428](../../src/Common/common_archetype_equipment.cpp#L1428))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `loot_appearance` | equipment nickname | none (the item itself) | Equipment whose model is shown as loot (resolved at read time). | [helper L145](../../src/Common/common_archetype.cpp#L145), calls [L1409](../../src/Common/common_archetype_equipment.cpp#L1409), [L1433](../../src/Common/common_archetype_equipment.cpp#L1433) |

### `[CloakingDevice]`  ([L1272](../../src/Common/common_archetype_equipment.cpp#L1272))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `power_usage` | float | 0.0 | Power draw. | [L1277](../../src/Common/common_archetype_equipment.cpp#L1277) |
| `cloakin_time` | float | 0.0 | Seconds to cloak. | [L1283](../../src/Common/common_archetype_equipment.cpp#L1283) |
| `cloakout_time` | float | 0.0 | Seconds to decloak. | [L1289](../../src/Common/common_archetype_equipment.cpp#L1289) |
| `cloakin_fx` | id | 0 | Effect played when cloaking. | [L1295](../../src/Common/common_archetype_equipment.cpp#L1295) |
| `cloakout_fx` | id | 0 | Effect played when decloaking. | [L1301](../../src/Common/common_archetype_equipment.cpp#L1301) |

## `[Motor]` (in equipment files)

Defines a missile/torpedo motor referenced by `motor =` of a `Munition`. Parsed in `LoadEquipment` only ([common_archetypeloader.cpp:144](../../src/Common/common_archetypeloader.cpp#L144), [common_archetype_misc.cpp:113](../../src/Common/common_archetype_misc.cpp#L113)). A duplicate nickname updates the existing motor's `accel`, `lifetime`, `delay`.

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `nickname` | id (whole value) | none | Motor key. | [L117](../../src/Common/common_archetype_misc.cpp#L117) |
| `accel` | float | 15.0 | Acceleration. | [L123](../../src/Common/common_archetype_misc.cpp#L123) |
| `lifetime` | float | 1.5 | Burn time in seconds. | [L129](../../src/Common/common_archetype_misc.cpp#L129) |
| `delay` | float | 0.0 | Delay before ignition. | [L135](../../src/Common/common_archetype_misc.cpp#L135) |

## `[Explosion]` (equipment, solar, ship and explosion files)

Explosion records are stored by nickname in a map ([common_archetypeloader.cpp:723](../../src/Common/common_archetypeloader.cpp#L723), [common_archetype_misc.cpp:173](../../src/Common/common_archetype_misc.cpp#L173)). Only the **first** `nickname` in a section counts (later ones are ignored); keys that appear before it are ignored. If the nickname already exists, the existing record is reused and the new keys overwrite its fields (a reset happens only on reload). Unknown keys are ignored.

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `nickname` | id (whole value) | none | Explosion key. | [loader L733](../../src/Common/common_archetypeloader.cpp#L733) |
| `radius` | float | 0.0 | Damage radius. | [L175](../../src/Common/common_archetype_misc.cpp#L175) |
| `impulse` | float | 0.0 | Impulse applied to objects in range. | [L180](../../src/Common/common_archetype_misc.cpp#L180) |
| `hull_damage` | float | 0.0 | Hull damage. | [L185](../../src/Common/common_archetype_misc.cpp#L185) |
| `energy_damage` | float | 0.0 | Energy damage. | [L190](../../src/Common/common_archetype_misc.cpp#L190) |
| `lifetime` | float `[0]`, optional float `[1]` | 0.0 / 10000.0 | `[0]` = minimum life; `[1]` = maximum life, defaults to `[0]` when given as `lifetime = x` (empty second parameter). | [L195](../../src/Common/common_archetype_misc.cpp#L195) |
| `effect` | id `[0]`, optional float `[1]` | - / -1.0 | Effect nickname and optional strength (-1 when omitted). Repeated keys append. | [L204](../../src/Common/common_archetype_misc.cpp#L204) |
| `process` | `none` \| `shatter` \| `disappear` | `disappear` | What happens to the exploding object (stored as 2 / 0 / 1). Case-insensitive; any other value leaves the previous setting. | [L215](../../src/Common/common_archetype_misc.cpp#L215) |
| `debris_type` | id `[0]`, optional int `[1]` | - / count 0 | Debris archetype and how many. Repeated keys append. | [L228](../../src/Common/common_archetype_misc.cpp#L228) |
| `num_child_pieces` | int (truncated float) | 0 | Number of child pieces. | [L238](../../src/Common/common_archetype_misc.cpp#L238) |
| `debris_impulse` | float | 5.0 | Impulse given to debris. | [L244](../../src/Common/common_archetype_misc.cpp#L244) |
| `innards_debris_object` | id (whole value) | - | Interior debris object. Repeated keys append. | [L249](../../src/Common/common_archetype_misc.cpp#L249) |
| `innards_debris_radius` | float | 0.0 | Spawn radius of interior debris. | [L254](../../src/Common/common_archetype_misc.cpp#L254) |
| `innards_debris_start_time` | float | -1.0 | Start time of interior debris. | [L259](../../src/Common/common_archetype_misc.cpp#L259) |
| `innards_debris_num` | float | 0.0 | Number of interior debris pieces. | [L264](../../src/Common/common_archetype_misc.cpp#L264) |

## `[Simple]` records

`[Simple]` (in `ships\shiparch.ini` and `fx\explosions.ini`) is a plain `Root` record: it accepts the `Root` keys above only (plus whatever the client companion reads). Duplicates: the first wins and a warning is logged ([common_archetypeloader.cpp:163](../../src/Common/common_archetypeloader.cpp#L163)).
