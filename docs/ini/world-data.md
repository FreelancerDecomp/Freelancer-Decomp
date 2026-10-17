# World-data INI files read by Common.dll

This document lists the INI-style files that `Common.dll` itself parses when the game world is built: the `[Data]` dispatcher, `universe.ini` and the system files it points at, base and room files, market lists, costumes and body parts, new-character templates, fuse and petal queues, the weapon-modifier table, reputation groups, the real-time-cutscene performance slider and the error-log routing. For every file the sections, keys, value types, defaults and repeat behaviour are given as the source code reads them.

**File location / how it is found.** Every file below is reached from the `[Data]` section of `freelancer.ini` (see [freelancer-ini.md](freelancer-ini.md)) except `[Error]`, which sits in `freelancer.ini` itself. `DataConfig::startup` walks `[Data]` in file order and, for each key it knows, resolves the value with `GetFileSysPath(..., nullptr)` (a relative value is joined to the root of the `[Freelancer] data path` file system, a value starting with `\` or having `:` as its second character is used as is). Most files are BINI in the retail data and plain text in mods; `INI_Reader` accepts both (see [parsers.md](parsers.md)). Key and section names are compared case-insensitively. A key that appears twice is delivered twice, in file order; each reader below says whether the later value overrides, is ignored or appends. "Not validated" means the code applies no range or enumeration check.

Conventions used in the tables: `id` = text hashed with `CreateID` (32-bit nickname hash); `MakeId` = the 16-bit hash used for faction/group names; "whole value" = `get_value_string()` without index, i.e. all comma-separated parameters joined (commas included); `[n]` = comma-separated parameter number n (`get_value_*(n)`), counted from 0.

Examples in this document are synthetic and are not copied from the game data.

---

## 1. `[Data]` dispatcher (`DataConfig`)

`DataConfig::startup(IniPath, Factory, Progress)` opens `freelancer.ini` (path supplied by the caller), reads every `[Data]` section it finds (several are allowed), and for each key in order optionally calls an application progress callback with the whole value (a non-zero answer aborts the load), then dispatches. Unknown keys are ignored (the client has a second dispatcher for UI/effect keys, see [client-world-fx.md](client-world-fx.md) and [client-ui-config.md](client-ui-config.md)). Afterwards `AI\state_graph.db` (a binary file, not INI) is loaded. A second `startup` call only increments a reference count and returns.

Because the keys are processed in file order, the order matters: `universe` must precede `bases` (the base list is built from the universe's bases), and `goods` and `bases` must precede `markets` (market lines look up good and base nicknames at load time).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `universe` | path | none | `universe.ini`; passed unresolved to `Universe::Startup`, which resolves it against the data path. The directory of this file becomes the root for the `file =` values of `[System]` blocks (section 2). | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L46) |
| `constants` | path | none | Constants file (`ReadConstants`); contents described in [data-driven-files.md](data-driven-files.md). | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L51) |
| `equipment`, `solar`, `ships`, `asteroids`, `explosions`, `loadouts` | path (repeatable) | none | Archetype files; each repeated key loads one more file. A warning "contains no new definitions" is logged when the loader reports zero new entries. See [archetypes-and-equipment.md](archetypes-and-equipment.md). (`explosions` goes to the simple-archetype loader.) | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L55) |
| `bodyparts` / `costumes` | path | none | Section 6. | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L96) |
| `bases` | none | none | The value is not read. Presence triggers `BaseDataList_load()`: one `BaseData` is built per base known to the universe, each reading its own base file (section 3). The retail file notes "bases has no filename but the key specifies the load order". | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L106) |
| `goods` | path | none | Goods list; see [archetypes-and-equipment.md](archetypes-and-equipment.md). | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L110) |
| `markets` | path (repeatable) | none | Market file, section 4. Each repeated key adds one more file to the same base-data list. | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L114) |
| `PetalDB` | path | none | Section 8. | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L118) |
| `Groups` | path | none | `InitialWorld.ini` as reputation-group list, section 9. (The dev file spells the key `groups`.) | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L122) |
| `fuses` | path (repeatable) | none | Each value is only queued (`FuseDB::AddFuseINIFile`); Common does not open the file. The client and server fuse loaders read the queue, see section 10. | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L135) |
| `WeaponModDB` | path | none | Section 11. | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L140) |
| `NewCharDB` | path | none | Section 7. | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L145) |
| `RtcSlider` | path | none | Section 12. (The dev file spells the key `rtcslider`.) | [common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp#L151) |

Example (synthetic):

```ini
[Data]
universe = universe\universe.ini
goods    = equipment\goods.ini
bases
markets  = equipment\market_a.ini
markets  = equipment\market_b.ini
```

---

## 2. `universe.ini` and the system files (`Universe`)

`Universe::Startup(path)` is reference counted: the file is only parsed when the count is zero. It resolves `path` against the data path, creates a DOS file system rooted at the directory of `universe.ini` (the "universe directory"), then parses the file. Zero systems after parsing logs "could not create universe". The universe tables (systems, bases, zones, gates) are cleared before every real load.

Header names tested while walking `universe.ini`: `Time`, `System`, `Base`. Any other header is skipped silently.

### 2.1 `[Time]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `seconds_per_day` | float | 60 (built-in days-per-second is 1/60) | `[0]`; the stored days-per-second value is `1.0 / seconds_per_day`. Not validated (0 divides by zero). Only the first occurrence in the block is used (the loop stops after it). | [common_universe.cpp](../../src/Common/common_universe.cpp#L145) |

### 2.2 `[System]` (one block per star system)

Repeated blocks add systems; a block whose `nickname` hashes to an id that already exists, or to an invalid id (`id & 0x3fffffff == 0`), is rejected with an error and not inserted. After the block, a system whose `msg_id_prefix` is empty is reported as an error unless its nickname is one of `St01 St02 St03 St02b St02c St03b Tut Ew05 FP7_system Ew06` (exact, case-sensitive).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | id + string | none | Whole value. Hashed with `CreateID`; the text is also kept. Must come before `file` (the zones read from the system file take the nickname known at that moment as their system id). | [common_universe.cpp](../../src/Common/common_universe.cpp#L165) |
| `msg_id_prefix` | string | empty | Whole value, truncated to 63 characters (fixed `TString<64>`). Used by the space GCS voice-line lookup. Read by `CommReferrable::read_comm_ini_data`. | [common_watchable.cpp](../../src/Common/common_watchable.cpp#L72) |
| `file` | path | none | System file, relative to the **directory of `universe.ini`** (not the data path). The file is opened and parsed immediately, in the middle of the block (section 2.4). A file that cannot be opened is skipped silently. | [common_universe.cpp](../../src/Common/common_universe.cpp#L183) |
| `strid_name` | int | 0 | `[0]`; string-resource id of the display name. Not validated. | same |
| `ids_info` | int | 0 | `[0]`; infocard resource id. | same |
| `visit` | int | 0 | `[0]` cast to an 8-bit value (visit flags). | same |
| `NavMapScale` | float | 0 | `[0]`; navigation-map scale. | same |
| `links` | none | n/a | Recognised and ignored (the value is not read). | same |
| `pos` | float x,y[,z] | previous block's position | `[0]`,`[1]` required, `[2]` optional (0.0 when empty). Map position of the system. | same |
| `real_pos` | float x,y,z | see `pos` | `[0..2]`; writes the same position field as `pos`, so whichever of the two appears last in the block wins. | same |

The position of a block that has no `pos`/`real_pos` is whatever the previous `[System]` block left (retail leaves the field unconstructed; the port carries it over).

### 2.3 `[Base]` (universe base list)

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | id + string | none | Whole value; hashed. An invalid id or a duplicate id is an error and the block is dropped. | [common_universe.cpp](../../src/Common/common_universe.cpp#L865) |
| `strid_name` | int | 0 | `[0]`. | same |
| `file` | path | none | Base file, **relative to the data path** (read later by `BaseData::read_from_ini`, section 3). | same |
| `system` | id | 0 | Whole value, hashed: system the base belongs to. | same |
| `BGCS_base_run_by` | string | empty | Whole value (faction that runs the base, for the BGCS voice system). | same |
| `ship_sml_01..03`, `ship_mdm_01..03`, `ship_lrg_01..03` | id | 0 | Whole value, hashed: ship nickname placed in the base's small/medium/large ship slots 1..3. | same |
| `terrain_tiny`, `terrain_sml`, `terrain_mdm`, `terrain_lrg`, `terrain_dyna_01`, `terrain_dyna_02` | id | 0 | Whole value, hashed: terrain/background solar nicknames per size class. | same |
| `autosave_forbidden` | bool | false | `[0]`; text form accepts exactly `true`/`1`/`false`/`0` (case-insensitive); anything else logs "Invalid bool value" and is false. | same |

### 2.4 System file (`Systems\Xx01\Xx01.ini`, as read by Common)

`LoadSystemFile` opens the file named by `[System] file =` and makes three passes:

1. every `[Object]` section is read for the reputation "vibe" table (`LoadObjectVibesFromSystemIni`, table below);
2. the reader is rewound and `[Zone]`, `[Music]` and `[Object]` sections are read for universe data;
3. other headers are ignored by Common (they belong to the server, the Content module and the client, see [systems-and-bases.md](systems-and-bases.md)).

**`[Zone]`** (one per zone; read by `ReadZoneValues`). A zone with an empty nickname id is dropped; a second zone with an equal id is silently discarded.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname`, `name` | id | 0 | Either key; hashed (text is not kept). | [common_universe.cpp](../../src/Common/common_universe.cpp#L349) |
| `pos` | float x,y,z | zone constructor value | `[0..2]`. | same |
| `rotate` | float x,y,z | zone constructor value | `[0..2]` Euler angles, converted with `EulerMatrix`. | same |
| `shape` | enum | invalid | `[0]`, case-insensitive: `sphere`, `ellipsoid`, `BOX`, `CYLINDER`, `RING`, `MESH`. Any other text logs "invalid ZoneShape" and stores the invalid shape. | [common_universe.cpp](../../src/Common/common_universe.cpp#L293) |
| `size` | float x[,y[,z]] | y,z = 0 | `[0]` required; `[1]`,`[2]` default to 0.0 when empty. | same |
| `mesh` | string | none | `[0]`; archetype (mesh) name loaded with `Archetype::Load`, used by `MESH` shapes. | same |
| `edge_fraction` | float | zone constructor value | `[0]`, clamped into 0.0..1.0 (below 0 gives 0, 1 or more gives 1). | same |
| `property_flags` | int | 0 | `[0]` OR-ed into the zone's property bit field (repeats accumulate). | same |
| `property_fog_color` | float r,g,b | zone constructor value | `[0..2]` in 0..255; each divided by 255. | same |
| `drag_modifier` | float | 1.0 | `[0]`. A value below 1.0 logs an error naming the zone and file and is stored as 1.0. A value above 1.0 also sets property bit `0x80000`. | same |
| `damage` | float | 0 | `[0]`; a value above 0 sets bit `0x40000`. | same |
| `interference` | float | 0 | `[0]`; any non-zero value sets bit `0x100000`. | same |
| `music` | id | none | Whole value, hashed; sets bit `0x400000`. | same |
| `spacedust` | string | none | Whole value; space-dust effect name; sets bit `0x200000`. | same |
| `spacedust_maxparticles` | int | 0 | `[0]`. | same |
| `faction` | id + float | none | Repeatable. `[0]` faction nickname (`MakeId`), `[1]` weight. Each line appends one pair to the zone's population list. | same |
| `path_label` | flag | false | Presence only. | same |
| `visit` | int | 0 | `[0]`, OR-ed with `0x20` by the zone setter. | same |
| `ids_name`, `ids_info` | int | 0 | `[0]` resource ids. | same |

**`[Music]`**: `space` (whole value, hashed) sets the system's default space music id. Other keys are ignored. If `space` repeats, the last one wins.

**`[Object]`** (as read during the universe load, `ReadSystemFileObject`). Used to build gate connections and to attach a base to its space object. Other keys are ignored here.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | id | carried from the previous `[Object]` | `[0]`. | [common_universe.cpp](../../src/Common/common_universe.cpp#L702) |
| `base` | nickname | none | Whole value; looked up in the base table. Unknown base logs "get_base failed". | same |
| `ids_name` | int | 0 | `[0]`; compared with the base's `strid_name`: only when they are equal does the object record itself as the base's space object. | same |
| `archetype` | id | 0 | `[0]`; used to classify the object as jump gate / jump hole / airlock (from the solar archetype's object type) when `goto` is present. | same |
| `pos` | float x,y,z | -0.1, -0.1, -0.1 | `[0..2]`. | same |
| `visit` | int | 0 | `[0]`, 8-bit. | same |
| `goto` | id | none | `[0]` target system nickname; each occurrence records a gate edge (first insertion per nickname wins); one connection record is added to the system for every `[Object]` that contains a `goto`. | same |

**`[Object]` as read for the reputation table** (`LoadOneObjectSection`). The loop stops reading a section as soon as `ids_name`, `nickname` and `group` are all set. A section without `nickname` is skipped.

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `nickname` | id | none | `CreateID` of the whole value. | [common_cobjdb.cpp](../../src/Common/common_cobjdb.cpp#L54) |
| `ids_name` | int | 0 | `[0]`. | same |
| `infocard_ids`, `ids_info` | int | 0 | Either spelling, `[0]`. | same |
| `group` | string | empty | Whole value, truncated to 15 characters (`TString<16>`): reputation group name. | same |
| `archetype` | id | none | Solar archetype; supplies the rank (toughness) of the vibe. | same |

**`[Object]` pool** (`LoadSystemObjectPool`, a separate loader that fills a shared list of records (capacity 300, grows as needed) for other modules). Keys read: `nickname`, `next_ring`, `prev_ring` (strings), `pos` (x,y,z), `archetype` (hashed), `ids_name` (int), `ids_info` / `ids_info_card` / `infocard_ids` (int, any spelling), `goto`, `dock_with` (hashed), `reputation` (string), `visit` (8-bit), `base` (nickname looked up in the base table), `star` (presence only; forces `visit = 1`). Source: [common_universe.cpp](../../src/Common/common_universe.cpp#L565).

---

## 3. Base files (`BaseData`)

One file per universe base, named by the `[Base] file =` value in `universe.ini` and resolved against the data path. A base whose `file` is empty is reported as an error. If the file cannot be opened, an error with the resolved path is logged. Headers other than `Base`/`BaseInfo`/`Room` log "Invalid ini block".

**`[Base]` or `[BaseInfo]`** (`read_Base_block`). Unknown keys log "Invalid ini data" (the block is still processed).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | ignored | n/a | Skipped without checking. | [common_basedata.cpp](../../src/Common/common_basedata.cpp#L167) |
| `start_room` | id | 0 | Whole value. The stored start-room id is `CreateID("<BASEID in hex>_<value>")`, i.e. the room nickname made unique per base. Missing (id stays 0) logs "No start room in base ini file". | same |
| `price_variance` | float | 0.0 | `[0]`. Not validated. | same |
| `ship_repair_cost` | float | 0.33 | `[0]`. Not validated. | same |

**`[Room]`** (`read_Room_block`, repeatable, one block per room).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | string | none | Whole value. Room id = `CreateID("<BASEID in hex>_<nickname>")`. | [common_basedata.cpp](../../src/Common/common_basedata.cpp#L206) |
| `file` | path | none | Room file (section 5), relative to the data path. | same |

A room block missing `nickname` or `file` logs an error and leaves an uninitialised `RoomData` in the base's room list (retail behaviour); any other key logs "Invalid ini data". Room files themselves are read lazily by `RoomData::load` the first time the room is entered.

Example (synthetic):

```ini
[BaseInfo]
start_room = Deck
price_variance = 0.1
ship_repair_cost = 0.25

[Room]
nickname = Deck
file = rooms\xx01_deck.ini
```

---

## 4. Market files (`[Data] markets`)

`BaseDataList::load_market_data` reads one file per `markets =` key into the per-base market maps. A file that cannot be opened logs an error and is skipped. Any header other than `BaseGood` logs a warning and is skipped. Inside a block, every key other than `base` and `MarketGood` logs "Unknown ini data".

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `base` | nickname | none | Whole value. Selects the base the following `MarketGood` lines belong to. An unknown base nickname logs a warning and leaves the previously selected base unchanged (a block starts with no base selected, so lines before a valid `base` are dropped). | [common_basedata.cpp](../../src/Common/common_basedata.cpp#L386) |
| `MarketGood` | positional list | n/a | Repeatable. `MarketGood = good, level, rep, min_stock, max_stock, type, price_mult[, ignored]`: see below. Lines for unknown goods, with no selected base, or with every value at its default are dropped. A later line for the same good replaces the earlier entry. | same |

`MarketGood` parameters (all after `[0]` optional; an empty parameter keeps the default):

| Index | Meaning | Type | Default | Notes |
|---|---|---|---|---|
| 0 | good nickname | string | none | Looked up with `GoodList::find_by_nickname`; an unknown good drops the line (no message). |
| 1 | required level (`MinLevel`) | float | 0.0 | |
| 2 | required reputation (`MinRep`) | float | -1.0 | |
| 3 | minimum inventory | int | 0 | |
| 4 | maximum inventory | int | 0 | |
| 5 | transaction type | int | 0 | Stored cast to `TransactionType`; not validated. Values used by consumers: 0 buy-and-sell, 1 demand only (base buys), 2 sale only (base sells). |
| 6 | price multiplier | float | 1.0 | Stored price = `(int)(good base price * multiplier)`. |
| 7 | read and discarded | float | n/a | |

The line is kept only if at least one of: min inventory, max inventory, type, multiplier, level, rep differs from its default.

---

## 5. Room files (`RoomData`)

Opened by `RoomData::read_from_ini(path)` (data-path relative; an unopenable file is silently an empty room). `enumerate_CharacterPlacement_ini` reopens the same file and hands every `[CharacterPlacement]` block to a callback (used by the client's NPC placement). Headers are matched case-insensitively. Many sections and keys are *obsolete*: they are recognised and only log a warning or error (shown as "obsolete" below).

Header dispatch:

| Header | Handling | Source |
|---|---|---|
| `Room`, `Room_Info` | room information block (5.1) | [common_roomdata.cpp](../../src/Common/common_roomdata.cpp#L590) |
| `Room_Sound`, `Sound` | sound block (5.3) | same |
| `Camera` | camera block (5.2) | same |
| `CharacterPlacement` | start-script binding (5.4) | same |
| `PlayerShipPlacement`, `ForSaleShipPlacement` | ship placements (5.5) | same |
| `Hotspot` | clickable areas (5.6) | same |
| `FlashlightSet`, `FlashlightLine` | flashlight groups (5.7) | same |
| `Cloud`, `Fog`, `GoodsCartPlacement`, `RepairRobotPlacement`, `GoodsPilePlacement`, `ShipPlacement`, `MonitorPlacement`, `Effect`, `AvailableShip`, `Billboard`, `Light`, `Lighting` | obsolete: warning only, contents ignored | same |
| anything else | ignored silently | same |

### 5.1 `[Room]` / `[Room_Info]`

Defaults come from `RoomData::clear()`: both loop flags true, ambient colour (1,1,1), no shadow/glare light, default camera (FOV 35, near 0.25, far 1e6).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `set_script` | path | none | Whole value; a Thorn scene script (`.thn`, not INI) loaded with `ThornScriptLoad`. Its entities supply the room's props, lights, cameras and setpoints (placements for characters whose setpoint names follow the `zs`/`zg` convention). No script means no props at all. | [common_roomdata.cpp](../../src/Common/common_roomdata.cpp#L1024) |
| `animation` | string | none | Repeatable. Appends a looping animation name. At most 32 are stored; a 33rd is not bounds-checked in retail. | same |
| `animation_oneshot` | string | none | Repeatable. As `animation`, played once. | same |
| `goodscart_script` | path[, priority] | none | `[0]` script, optional `[1]` priority name (hashed; passed through `RtcSlider::validate_setting`, which accepts everything). | same |
| `cutoff_shadow_z` | float | off | `[0]`; presence turns the cut-off on. | same |
| `extra_shadow_height_offset` | float | 0.0 | `[0]`. | same |
| `scene` | list | none | Repeatable ambient script: `scene = [all,] ambient\|traffic\|once, script.thn [, priority]`. A leading `all` (case-insensitive) is dropped. After that exactly 2 or 3 values are required (else an error is logged and the line ignored). Type: `ambient` = 1, `traffic` = 2 (logged as obsolete but stored), `once` = 3; any other word logs an error and the line is ignored. Third value = priority name (hashed). Values are copied into 256-byte buffers without a length check. | [common_roomdata.cpp](../../src/Common/common_roomdata.cpp#L1139) |
| `mesh`, `background_script`, `background_script_oneshot`, `mission_vendor_type`, `goodscart_script_fighter`, `goodscart_script_elite`, `goodscart_script_freighter`, `is_outside` | any | n/a | Obsolete: warning only. | same |
| `news_material` | any | n/a | Obsolete: logged as an **error**. | same |

### 5.2 `[Camera]`

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `name` | string | none | Camera name chosen for the room. The names `debug_detailtest`, `debug_straight_up`, `debug_anim` install fixed built-in poses; any other name selects the first camera of that name found in the `set_script` scene. | [common_roomdata.cpp](../../src/Common/common_roomdata.cpp#L1215) |
| `index`, `near_plane`, `far_plane` | any | n/a | Obsolete: warning only. | same |

### 5.3 `[Room_Sound]` / `[Sound]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `music` | id[, word] | none | `[0]` music id (hashed). `[1]`, if present and equal to `oneshot` (case-insensitive), turns looping off; any other `[1]` leaves looping on. | [common_roomdata.cpp](../../src/Common/common_roomdata.cpp#L1240) |
| `ambient` | id[, word] | none | As `music`, for the ambient sound. | same |

### 5.4 `[CharacterPlacement]`

Does not create a placement; it gives an existing setpoint-derived placement its start script.

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `name` | string | none | Setpoint name (case-insensitive match; the last match wins). Missing logs an error. | [common_roomdata.cpp](../../src/Common/common_roomdata.cpp#L1267) |
| `start_script` | string | none | Script name stored on the placement (replaces an earlier one). | same |
| `zoom_in`, `zoom_out` | any | n/a | Obsolete: logged as errors. | same |

If `name` matches no setpoint an error "refers to setpoint ... which does not exist" is logged.

### 5.5 `[PlayerShipPlacement]` / `[ForSaleShipPlacement]`

Every block appends one placement (even an empty one).

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `name` | string | none | Setpoint name; its frame and name are copied into the placement. An unknown setpoint is an error. Both headers. | [common_roomdata.cpp](../../src/Common/common_roomdata.cpp#L1324) |
| `landing_script` | string | none | Player-ship only. | same |
| `launching_script` | string | none | Player-ship only. | same |

### 5.6 `[Hotspot]`

Every block appends one hotspot (button), even an empty one.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `name` | string | none | Hotspot name. | [common_roomdata.cpp](../../src/Common/common_roomdata.cpp#L1390) |
| `behavior` | enum | -1 | Case-insensitive; only the first token (split at `,` `;` `/` space) is compared. Values: `ExitDoor`=1, `NewsVendor`=2, `MissionVendor`=3, `Launch`=4, `StartDealer`=5, `FrontDesk`=6, `StartShipDealer`=7, `StartEquipDealer`=8, `MoveRight`=9, `MoveLeft`=10, `VirtualRoom`=11, `Repair`=12. An unknown word stores -1 without any message. | same |
| `room_switch` | string | none | Room to switch to. | same |
| `virtual_room` | string | none | Virtual room name. | same |
| `set_virtual_room` | string | none | Virtual room to set. | same |
| `state_send` | int | 0 | `[0]` truncated; OR-ed into the send mask (repeats accumulate). | same |
| `state_read` | int | 0 | `[0]` truncated; OR-ed into the read mask. | same |
| `state_transition` | string | none | Repeatable; the first four fill four slots in order; a fifth and later are dropped. | same |
| `zoom_in`, `zoom_out` | any | n/a | Obsolete: logged as errors. | same |

### 5.7 `[FlashlightSet]` / `[FlashlightLine]`

Each block appends a flashlight group (`Mode` 1 for `FlashlightSet`, 2 for `FlashlightLine`).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `color` | float r,g,b | 0 | `[0..2]` used as given. | [common_roomdata.cpp](../../src/Common/common_roomdata.cpp#L53) |
| `icolor` | float r,g,b | 0 | `[0..2]`, each divided by 255; same destination as `color`, last one wins. | same |
| `scale`, `gap`, `blink`, `endpause` | float | 0 | `[0]`. | same |
| `hardpoint` | string | none | Repeatable; appends a hardpoint name. `hardpoint` and `setpoint` share one counter and a 64-slot array that is not bounds-checked. | same |
| `setpoint` | string | none | Repeatable; appends a setpoint name (shares the counter with `hardpoint`). | same |
| `numlights` | int | 0 | `[0]` truncated. Accepted in `[FlashlightLine]` only. | same |

---

## 6. Costumes: `bodyparts` and `costumes`

### 6.1 `[Data] bodyparts` (`CostumeDescriptions::load_bodyparts`)

An unopenable file logs an error. The file is a sequence of sections; the loader remembers the most recent sex and the most recent `[DetailSwitchTable]`, which apply to the body-part sections that follow them.

| Section | Handling | Source |
|---|---|---|
| `[Skeleton]` | Resets the remembered sex to `none`, then reads `sex` and `nomotion`. | [common_costume.cpp](../../src/Common/common_costume.cpp#L422) |
| `[Animations]` | `anim` and `preload` names (below). | same |
| `[PetalAnimations]` | Only `anim` is read; each section is stored as one separate list. | same |
| `[DetailSwitchTable]` | Switch table (6.2). Each section appends a table and becomes the "current" table. | same |
| `[Accessory]` | Accessory definition (6.3). | same |
| `[Body]`, `[Head]`, `[LeftHand]`, `[RightHand]` | Body-part definition (6.4); header compared case-insensitively. Any other header logs "Unknown header ... in bodyparts.ini". | same |

| Section.Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `[Skeleton] sex` | enum | `none` | `none` (0), `male` (1), `female` (2), case-insensitive. An unknown word leaves the previous value (0 after the header reset). | [common_costume.cpp](../../src/Common/common_costume.cpp#L452) |
| `[Skeleton] nomotion` | any | n/a | Obsolete: warning only. | same |
| `[Animations] anim` | string | none | Repeatable; adds an animation name to the global list unless already present (case-insensitive). | [common_costume.cpp](../../src/Common/common_costume.cpp#L472) |
| `[Animations] preload` | string | none | Repeatable; adds to the preload list (same de-duplication). | same |
| `[PetalAnimations] anim` | string | none | Repeatable, de-duplicated within the section. `preload` is not accepted here. | [common_costume.cpp](../../src/Common/common_costume.cpp#L498) |

### 6.2 `[DetailSwitchTable]`

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `switch` | float, float | none | Repeatable `switch = distance, level`; appended to the table in file order. Retail requires the keys in descending order (the evaluator assumes it); an empty table faults at use. Not validated. | [common_costume.cpp](../../src/Common/common_costume.cpp#L309) |
| `fovx` | float | 40 (stored as 20.0 = half) | `[0]` full horizontal FOV in degrees; stored halved. | same |
| `sizex` | float | 1280 | `[0]` reference screen width in pixels. | same |

### 6.3 `[Accessory]` (inside `bodyparts`)

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `nickname` | string / number | empty | Whole value. Id = the text parsed as a number if it parses to non-zero, otherwise `CreateID(text)`. An accessory without a nickname is still stored under the id of the empty string. | [common_costume.cpp](../../src/Common/common_costume.cpp#L379) |
| `mesh` | string | empty | Mesh name. | same |
| `hardpoint` | string | empty | Hardpoint on the accessory. | same |
| `body_hardpoint` | string | empty | Hardpoint on the character it attaches to. | same |
| `material_library` | string | none | Repeatable; each line appends one material library. | same |

A second accessory with an equal id keeps the first.

### 6.4 `[Body]`, `[Head]`, `[LeftHand]`, `[RightHand]`

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `nickname` | string / number | none | Id as for accessories. | [common_costume.cpp](../../src/Common/common_costume.cpp#L334) |
| `mesh` | string | none | Mesh name. A part with an empty `nickname` or empty `mesh` is dropped silently. | same |
| `multilevel` | any | n/a | Obsolete: warning only. | same |

A part with an id already present in the same type keeps the first one.

### 6.5 `[Data] costumes` (`CostumeDescriptions::load_costumes`)

An unopenable file logs an error. Headers other than `Costume` log a warning and are skipped. The block's key/value pairs, in order:

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | string / number | 0 | Id as for accessories. A block without `nickname` is inserted with id 0. | [common_costume.cpp](../../src/Common/common_costume.cpp#L551) |
| `accessory` | nickname | none | Repeatable; up to 8 accessories (the port drops the ninth; retail overruns the record). An unknown accessory logs a warning. | same |
| `Body`, `Head`, `LeftHand`, `RightHand` | nickname | none | The key is the body-part type (compared case-insensitively); the value is the nickname of a part of that type defined in `bodyparts`. An unknown type logs "Invalid part type"; an unknown part logs a warning. The first part with a non-zero sex fixes the costume's gender. | same |

A costume whose id already exists keeps the first definition. Warning texts always print an empty costume name (retail quirk).

---

## 7. New-character templates (`[Data] NewCharDB`)

Read by `NewChar::Database::Load`. An unopenable file logs an error and returns false. A record whose id already exists is ignored (the first stays). All ids are `CreateID` hashes.

**`[Faction]`**

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `nickname` | id | 0 | Whole value. | [common_newchar.cpp](../../src/Common/common_newchar.cpp#L28) |
| `rep_group` | string | empty | Reputation group the new player starts in. | same |
| `base` | nickname | none | Repeatable; each value adds a selectable home base (the record `{CreateID(name), name}` goes into a global table used for `%%Home_Base%%`). | same |
| `package` | id | none | Repeatable; adds a package nickname to the faction's set. | same |
| `pilot` | id | none | Repeatable; adds a pilot nickname to the faction's set. | same |

**`[Package]`**

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `nickname` | id | 0 | Whole value. | same |
| `strid_name` | int | previous package's value (0 for the first) | `[0]` converted from float by truncation. | same |
| `strid_desc` | int | previous package's value | as above. | same |
| `ship` | string | empty | Ship (hull) nickname. | same |
| `loadout` | string | empty | Loadout nickname. | same |
| `money` | int | previous package's value | `[0]` truncated; starting credits. | same |

A `[Package]` that omits `strid_name`, `strid_desc` or `money` inherits the previous package's value (retail leaves the stack variables untouched).

**`[Pilot]`**

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `nickname` | id | 0 | Whole value. | same |
| `body` | string | empty | Body costume nickname. | same |
| `body.anim` | list | empty | Comma list of animation names (every parameter up to the first empty one); a repeated key replaces the list. | same |
| `comm` | string | empty | Comm (head) costume nickname. | same |
| `comm.anim` | list | empty | As `body.anim`. | same |
| `voice` | string | empty | Voice nickname. | same |
| `thumb` | string | empty | Thumbnail image name. | same |

Any other header or key is ignored.

---

## 8. Petal database (`[Data] PetalDB`)

`PetalInterfaceDatabase::load_from_ini`. An unopenable file logs an error. Only the `ObjectTable` header is read; other headers are skipped. The key name selects the category (case-insensitive): `room` (category 0), `cart` (equipment cart), `prop`. Any other key logs "Invalid category", but its value is still parsed and then dropped.

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `room`, `cart`, `prop` | list | none | Repeatable `key = nickname, mesh [, extra ...]`. `[0]` nickname (hashed), `[1]` mesh name, `[2..]` up to 16 extra names (extras beyond 16 are dropped in the port; retail overruns). A line missing nickname or mesh logs "Invalid PetalDB entry" but is still stored. The first record per nickname within a category stays. | [common_fuses.cpp](../../src/Common/common_fuses.cpp#L159) |

---

## 9. Reputation groups (`[Data] Groups`, `InitialWorld.ini`)

`Reputation::Startup(path)` reads only the group *definitions* from the text file `InitialWorld.ini` (also the new-game seed, see [saves-and-initial-world.md](saves-and-initial-world.md)). Only `[Group]` headers are read; other headers are ignored. The block is parsed **by position, not by key name** apart from two checks:

1. the first value line, whatever its key, is the group nickname (up to 15 characters; id = `MakeId(nickname)`);
2. the second value line, whatever its key, is `ids_name` (`[0]`, integer);
3. the third value line is `ids_info` if its key is `ids_info`, otherwise `ids_info = 0` (the line is consumed either way);
4. the fourth value line is `ids_short_name` if its key is `ids_short_name`, otherwise 0.

| Position | Expected key | Type | Default | Meaning | Source |
|---|---|---|---|---|---|
| 1 | `nickname` | string (<= 15 chars) | empty | Group nickname; hashed with the 16-bit `MakeId`. A group with an id already present reuses the existing record. | [common_reputation.cpp](../../src/Common/common_reputation.cpp#L118) |
| 2 | `ids_name` | int | n/a | `[0]` truncated from float. | same |
| 3 | `ids_info` | int | 0 | `[0]`. | same |
| 4 | `ids_short_name` | int | 0 | `[0]`. | same |

Remaining lines of the block (for example `rep`) are ignored by `Startup`. The `rep` lines are read later by `Reputation::LoadFeelings` when a save game or the initial world is loaded (called by the server loader with an already open reader):

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `rep` | float, nickname | none | Repeatable `rep = value, other_group`: the group's feeling toward `other_group` (`MakeId`). A pair already present keeps its first value. A `[Group]` block with no `rep` line logs "save game has corrupted reputation data"; a group name that matches no loaded group skips its lines. | [common_reputation.cpp](../../src/Common/common_reputation.cpp#L208) |

The saver writes the same block shape (`nickname`, then one `rep = <value>, <group>` per relation) through the save-game writer.

---

## 10. Fuse files (`[Data] fuses`)

Common only queues the (resolved) paths in `FuseDB::m_FuseINIFiles`; no file is opened here. The server (`ServerFuseDB::LoadFile`) and the client (`ClientFuseDB::LoadFile`) later open each queued file; the actions they define are described in [server.md](server.md) and [client-world-fx.md](client-world-fx.md). What Common itself contributes to the format are the base-class value readers:

| Section.Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `[fuse] name` | id | 0 | Whole value, hashed (the fuse nickname). | [common_fuses.cpp](../../src/Common/common_fuses.cpp#L538) |
| `[fuse] lifetime` | float | -1.0 | `[0]`, seconds; -1 means unlimited. | same |
| `[fuse] death_fuse` | bool | false | `[0]`; text accepts exactly `true`/`1`/`false`/`0`, anything else logs an error. | same |
| action `at_t` | float[, float] | -1.0 / -1.0 | `[0]` trigger time (seconds from ignition); an optional `[1]` turns it into a random range `[at_t, at_t_end]`, picked once with `rand()`. Consumed by every action subclass through `FuseAction::ReadFuseActionValue`. | [common_fuses.cpp](../../src/Common/common_fuses.cpp#L448) |

---

## 11. Weapon-modifier table (`[Data] WeaponModDB`)

`WeaponModDB_load`. An unopenable file logs an error. Only `[WeaponType]` headers are read; each block starts with no "current" weapon type.

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `nickname` | id | none | Whole value; creates (or reuses) the weapon-type entry and makes it current. | [common_fuses.cpp](../../src/Common/common_fuses.cpp#L257) |
| `shield_mod` | id, float | n/a | Repeatable `shield_mod = shield_type, multiplier`. Ignored while no `nickname` has been seen in the block. The first value per shield type wins. A pair that is absent at query time yields a multiplier of 1.0. | same |

Example (synthetic): `shield_mod = shield_type_a, 0.75`.

---

## 12. Real-time-cutscene performance slider (`[Data] RtcSlider`)

`RtcSlider::load(path)`. An unopenable file logs an error. A header other than `RtcSlider` logs a warning and is skipped. A value line whose key is neither `set` nor `set_less` logs a warning.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `set` | name, float | none | Repeatable `set = feature, threshold`. Both parameters required (else a warning, line ignored). The feature is enabled when the slider value is greater than or equal to the threshold. The first definition of a name wins. | [common_rtcslider.cpp](../../src/Common/common_rtcslider.cpp#L59) |
| `set_less` | name, float | none | As `set`, but the feature is enabled when the slider value is strictly below the threshold. | same |

A feature name with no entry is reported as a warning at query time and treated as enabled. `fps_20_cap` and `fps_30_cap` are looked up by name for the frame-rate cap (20 fps, then 30 fps, else 200). Room files may reference setting names as priorities (section 5); no check is made that a name exists.

---

## 13. `[Error]` log routing (`freelancer.ini`)

`ErrorHandler::activate(ini_path, assertHandler)` installs the diagnostic dump hook and reads the `Error` section of the client's `freelancer.ini` (the client calls it from `WinMain` right after the start-up configuration; the server does not call it). A missing file or section simply leaves no log channels. Only the key `log` is read.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `log` | list | none | Repeatable `log = where[, kind[, severity]]`, one log channel per line (channels are pushed to the front of the list, except `$Window` channels, which are appended). `[0]` where: `$Text` (debugger output), `$Window` (message box, case-insensitive match on the `$` names) or any other text = file name, opened with `"wb"` (truncated). Longer than 127 characters is cut. `[1]` kind, default -1 (all kinds): a number or a quoted character such as `'c'` (character literals are accepted by the number parser); it is compared with the message flags shifted right by 4. `[2]` severity limit, default 100: messages whose severity (the low nibble of the message flags; 1 = error, 2 = warning, 3 = notice in the flags the code uses) is numerically above the limit are dropped. Neither `[1]` nor `[2]` is validated. A channel that cannot be activated (file cannot be opened) is dropped. | [common_errorhandler.cpp](../../src/Common/common_errorhandler.cpp#L381) |

Example (synthetic):

```ini
[Error]
log = messages.log, -1
log = $Window, -1, 1
log = $Text, 65536, 5
```

Besides `[Error]`, the client opens one extra fixed channel `FLSpew.txt` in the per-user local data folder (kind -1, severity 7) after reading the section ([main.cpp](../../src/freelancer/Client/main.cpp#L661)).

---

## 14. Keys present in the dev `bin\freelancer.ini` vs. what Common reads

The dev `[Data]` section also contains keys that Common's dispatcher does **not** handle (`effects`, `effect_shapes`, `intro`, `gate_tunnels`, `HUD`, `jump_effect`, `igraph`); those belong to the client dispatcher. Keys that Common handles and are present in the dev file: `universe`, `constants`, `equipment`, `solar`, `ships`, `asteroids`, `explosions`, `loadouts`, `bodyparts`, `costumes`, `bases`, `goods`, `markets`, `PetalDB`, `groups`, `fuses`, `WeaponModDB`, `NewCharDB`, `rtcslider`. The dev `[Error]` section contains only `log` lines.

## See also

* [parsers.md](parsers.md): `INI_Reader` grammar and number parsing.
* [freelancer-ini.md](freelancer-ini.md): the file that holds `[Data]` and `[Error]`.
* [systems-and-bases.md](systems-and-bases.md), [archetypes-and-equipment.md](archetypes-and-equipment.md), [data-driven-files.md](data-driven-files.md), [saves-and-initial-world.md](saves-and-initial-world.md).
