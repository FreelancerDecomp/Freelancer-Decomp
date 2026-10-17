# Per-system and per-base content data (Content.dll)

This page documents the parts of system files and the fixed-name tables that `Content.dll` (source in `src/Content/`) reads to populate star systems and bases: zones with their traffic and encounter settings, jump gates and trade lanes, solar NPCs, the shortest-path tables between systems, the mission-base database (`mBases.ini`) and the bar news. The generic reading of the same system files by the Common, Server and Client modules is documented in [world-data.md](world-data.md); only the keys that Content itself reads are listed here.

**File location / how it is found.** A system file is `Systems\<Xx##>\<Xx##>.ini` inside the universe directory, named by the `file` key of the system's `[system]` section in `universe\universe.ini` (the `universe` key of `[Data]` in `freelancer.ini`, see [freelancer-ini.md](freelancer-ini.md) and [world-data.md](world-data.md)). Content never builds that path itself: it asks the server for a reader on the system file with `pub::BuildSystemReader` ([server_pub.cpp:349](../../src/Server/server_pub.cpp#L349)) and asks it to walk the zones with `pub::System::EnumerateZones` ([server_pub.cpp:6827](../../src/Server/server_pub.cpp#L6827)). All other files on this page are fixed names opened with `OpenContentDataIni` ([ContentDataPath.cpp:17](../../src/Content/ContentDataPath.cpp#L17)): the name is joined to the Data directory (`data path` in `[Freelancer]` of `freelancer.ini`). The grammar, case rules, `get_value_*` accessors and the text/BINI duality are described in [parsers.md](parsers.md); the conventions of the key tables (types, "not validated", "first definition wins", hashed ids) are the same as in [ai-and-encounters.md](ai-and-encounters.md#conventions-used-by-every-table-below).

## 1. Loading order

| Step | Trigger | What is read |
|---|---|---|
| Game tables | `GameSupport` controller constructor | The fixed-name tables of [ai-and-encounters.md](ai-and-encounters.md) and `missions\news.ini` (section 6). |
| Load game | `LoadGame` message with the save-file reader | `LoadDynBaseInfo` rebuilds the base table and reads the `[BaseFaction]` data of every base from `missions\mBases.ini` (`BuildContentBases`, [BaseDB.cpp:415](../../src/Content/BaseDB.cpp#L415)); `LoadDynSysInfo` rebuilds the system table; the `[DynBaseInfo]` / `[DynSysInfo]` sections are scanned (section 7); then the path tables (section 4). |
| Player enters a base | `BaseEnter` message (`FContentBase::Load`, [GameSupport.cpp:562](../../src/Content/GameSupport.cpp#L562)) | The base's `[MVendor]`, `[GF_NPC]` and `[mRoom]` sections of `mBases.ini` (reference counted: read when the first player enters, dropped when the last leaves). |
| System with encounters | `SystemLoaded` message (`FSystemInfo::LoadFull`, [SystemInfo.cpp:365](../../src/Content/SystemInfo.cpp#L365)) | The system file: solar NPCs, `[EncounterParameters]`, `[Object]`, zones with population keys. |
| System without encounters | route planning and random missions (`FSystemInfo::Load(false)`) | The same file with encounters off: only `[System]`/`[SystemInfo]`, `[Object]` and the zone keys that do not belong to the population block. |

`FSystemInfo::LoadFromReader` is reference counted: the file is only read on the 0 to 1 transition.

---

## 2. `universe.ini` and the system file: Content's share

`universe\universe.ini` is read by the Common module ([world-data.md](world-data.md)); Content only opens it once more, in `WriteSystemPathTables` (section 4), to take the `nickname` of every `[System]` section. Of a system file's sections Content reads `[System]`/`[SystemInfo]`, `[EncounterParameters]`, `[Object]` and the zone sections (type `[Zone]`, as chosen by the Common loader); everything else (`[Music]`, `[Ambient]`, `[Background]`, `[Asteroids]`, `[Nebula]` and so on) is of no interest to Content and is documented by [world-data.md](world-data.md) and [client-world-fx.md](client-world-fx.md).

## 3. System files (Content's view)

### 3.1 `[EncounterParameters]`

Read by `FSystemInfo::LoadFromReader` ([SystemInfo.cpp:46](../../src/Content/SystemInfo.cpp#L46)) only when encounters are loaded. The section is read **positionally**: the first value line is taken as the encounter id and the second value line as the file name, whatever their keys are called. Conventionally they are written as follows:

```
[EncounterParameters]
nickname = example_enc_01
filename = missions\encounters\example_enc_01.ini
```

| Line | Key (not checked) | Type | Meaning |
|---|---|---|---|
| 1 | `nickname` | id | Encounter id: `CreateID(value 0)`. Zones refer to it with `encounter = id, level, chance` (section 3.4). |
| 2 | `filename` | string | Path relative to the Data path (copied into a 259-character buffer). The file is opened with `OpenContentDataIni`; if it cannot be opened the section is skipped silently. Format: [ai-and-encounters.md](ai-and-encounters.md#3-encounter-parameter-files). |

A repeated id keeps its slot and replaces the parameters. The parameter objects only live while the zones are enumerated (a zone copies what it needs), so changing the file after the system has been loaded has no effect.

### 3.2 `[System]` / `[SystemInfo]`

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `local_faction` | string | group 0 | Reputation group nickname of the system's local faction. Both header spellings `[System]` and `[SystemInfo]` are accepted. | [SystemInfo.cpp:73](../../src/Content/SystemInfo.cpp#L73) |
| `rpop_solar_detection` | bool | true | Only read from `[SystemInfo]` (by `LoadSolarNpcs`, single player only passes it on to the random-population module): whether solar objects are used to detect the player for random population. | [SystemInfo.cpp:351](../../src/Content/SystemInfo.cpp#L351) |

### 3.3 `[Object]` sections: solar NPC keys

Two independent passes read every `[Object]` section.

**Pass A: jump gates, jump holes and trade-lane rings** (`LoadFromReader`, [SystemInfo.cpp:114](../../src/Content/SystemInfo.cpp#L114)). Keys are processed in file order; **the first `archetype` whose value is not one of the three recognised words ends the section** (its remaining keys are not read, so an object whose archetype is none of the three is not recorded).

| Key | Type | Default | Meaning |
|---|---|---|---|
| `nickname` | string | 0 | Object nickname, converted with `GetNicknameId`. |
| `pos` | 3 floats | 0, 0, 0 | Position. |
| `rotate` | 3 floats | 0, 0, 0 | Euler angles in degrees (values 0, 1, 2 = x, y, z), converted to a rotation matrix. |
| `archetype` | string | none | `Trade_Lane_Ring`, `jumpgate` or `jumphole` (case-insensitive). Others end the section. A ring is recorded with nickname, position, rotation, `prev_ring`, `next_ring`, `reputation`; a gate or hole as a jump (type 1 / 2) with `goto`. |
| `prev_ring`, `next_ring` | string | 0 | Neighbouring ring nicknames. |
| `goto` | string system, string object | 0, 0 | Destination system nickname (value 0) and destination object nickname (value 1). |
| `reputation` | string | uninitialised in retail | Reputation group nickname of the ring. |

**Pass B: solar NPCs** (`LoadSolarNpcs`, [SystemInfo.cpp:311](../../src/Content/SystemInfo.cpp#L311), keys in [NpcCreate.cpp:23](../../src/Content/NpcCreate.cpp#L23)). A solar object becomes an NPC when it has a `nickname` and at least one of: a `behavior`, a `base`, a docking target on the object itself, a `reputation`, or a `msg_id_prefix`.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `nickname` | string | empty | Object nickname (at most 47 characters). |
| `base` | string | none | Base nickname (`GetBaseID`); only the first `base` key of a section is used. The base must already be in the content-base table. |
| `reputation` | string | -1 | Reputation group the solar's reputation is affiliated to. |
| `pilot` | id | none | Pilot personality id ([ai-and-encounters.md](ai-and-encounters.md#1-pilot-personalities-missionspilots_populationini-missionspilots_storyini)); applied only if that pilot exists. |
| `behavior` | string | -1 | Leader state graph name from `AI\state_graph.db` (case-insensitive); unknown names give -1, which stops the NPC from receiving an AI state. |
| `msg_id_prefix` | string | empty | Comm message prefix (at most 63 characters). |

### 3.4 Zone sections (`Zones.cpp` and `Encounters.cpp`)

For every zone the server enumerates, the reader is placed on that zone's section and `ParseSystemZone` ([Zones.cpp:150](../../src/Content/Zones.cpp#L150)) reads the rest of it. Keys it does not know are passed to the population parser `FZonePopulation::ParseValue` ([Encounters.cpp:214](../../src/Content/Encounters.cpp#L214)) (only when encounters are loaded) and otherwise ignored. Zone geometry keys (`pos`, `rotate`, `shape`, `size`, `property_flags` and so on) belong to Common ([world-data.md](world-data.md)).

| Key | Type | Default | Meaning / allowed values |
|---|---|---|---|
| `nickname` | string | none | Zone id, `CreateID(value 0)`. Two name conventions matter (substring search, `strstr`, **case-sensitive**): a name containing `Tradelane` on a zone whose stored shape code is 3 (`BOX` in the Common shape table) and whose x and y sizes are both more than ten times smaller than the z size is marked as a trade-lane zone; a name containing `vignette` marks the zone lawful. |
| `path_label` | string label, ints | none | Patrol/trade path label: value 0 the label id, values 1.. path indices (duplicates removed, first-seen order). The zone then belongs to a path. |
| `usage` | list of words | 0 | `trade` (flag 0x10) and `patrol` (flag 0x01), case-insensitive, any number of values; other words are ignored. Applies to the zone's path label. |
| `mission_eligible` | bool | false | Whether random missions may use the path. |
| `faction` | - | - | Recognised and ignored at zone level (the faction lines that matter follow an `encounter` key, below). |
| `density` | int | 0 | Maximum number of NPC ships near the zone. |
| `repop_time` | float | 10 | Repopulation time in seconds; a value of 0 or less is replaced by 1. |
| `mission_type` | list of words | zone is lawful | Resets the lawful flag, then `lawful` / `unlawful` (case-insensitive, any count) set the matching flag; if neither is present the zone is lawful. |
| `vignette_type` | string | 1 | `field` -> 2, `exclusion` -> 4, any other text -> 1 (case-insensitive). |

A zone is **dropped** when it has no `path_label`, is neither lawful nor unlawful, and (with encounters loaded) has none of the population keys below.

Population keys (read only when encounters are loaded; any of them marks the zone as populated):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `encounter` | id, float, float | none | Repeatable. Value 0 an encounter id from `[EncounterParameters]` (an unknown id is **not** checked and faults), value 1 the difficulty level (truncated to int; it adds the `diff<level>` ship class to the encounter's class lines, see [ai-and-encounters.md](ai-and-encounters.md#31-encounterformation)), value 2 the selection chance (weight among the zone's encounters). Each `encounter` line works on its own copy of the system's parameters. |
| `faction` (after `encounter`) | string, float | none | Directly following lines `faction = group, weight` are consumed by the preceding `encounter` key (the first non-`faction` line ends the run and is processed normally). They give the encounter its faction weights. |
| `density_restriction` | int, id | none | Repeatable; maximum count (value 0) of ships of a type (value 1, hashed id) allowed near the zone. |
| `population_additive` | bool | true | |
| `tradelane_attack` | float | 0 | Trade-lane attack probability for the zone, written as a percentage (stored divided by 100). |
| `max_battle_size` | int | 0 | |
| `relief_time` | float | 15 | Forced to 0 if `repop_time` is negative at that moment. |

---

## 4. Shortest-path tables between systems

Three tables answer "how many jumps from system A to system B": `Universe\systems_shortest_path.ini` (every connection), `Universe\shortest_legal_path.ini` (jump gates only) and `Universe\shortest_illegal_path.ini` (jump holes only). They are read by `LoadSystemPathTables` ([SystemConnections.cpp:340](../../src/Content/SystemConnections.cpp#L340)) on `LoadGame`, and are normally **generated by the game itself**. `InitSystemPathTables` ([SystemConnections.cpp:377](../../src/Content/SystemConnections.cpp#L377)) tests whether `systems_shortest_path.ini` can be opened under the Data path; if it cannot, the tables are computed from the system connections (a search over the connections of each system that skips locked gates) and written out by `WriteSystemPathTables` ([:457](../../src/Content/SystemConnections.cpp#L457)). **The writer opens the files by the working-directory-relative paths `..\data\Universe\<name>.ini` (text mode), not through the Data path**; where they land therefore depends on the process working directory. The writer takes the system names from the `nickname` of every `[System]` section of `Universe\universe.ini` (cut to 15 characters).

Format (each of the three files):

```
[SystemConnections]

Path = from_system, to_system, system_1, system_2

```

One `[SystemConnections]` section per source system, one `Path` line per destination ([SystemConnections.cpp:309](../../src/Content/SystemConnections.cpp#L309)):

| Key | Type | Meaning |
|---|---|---|
| `Path` | string, string, strings... | Value 0 source system nickname, value 1 destination system nickname, values 2.. the systems of the route, each resolved with `GetSystemID`. The route length is what the tables return. |

A section is stored under its last `from`; a section whose `from` resolves to no system id is dropped. A line with any key other than `Path` makes the loader insert an empty route for the previous destination (or for system 0), so no other key should appear. If a table is missing or empty, route lookups find nothing. The first of the three files is the only one tested for existence.

---

## 5. `missions\mBases.ini`

The mission-base database: which factions run a base, which missions its bar offers, and which characters stand in its rooms. The file is read twice by different passes over a sequence of `[MBase]` groups:

```
[MBase]
nickname = example_base
local_faction = example_group
diff = 12
msg_id_prefix = example_prefix

[BaseFaction]
faction = example_group
weight = 1.0
mission_type = DestroyMission, 5, 20, 2.0
npc = example_npc

[MVendor]
num_offers = 2, 5

[GF_NPC]
nickname = example_npc
...

[mRoom]
nickname = Bar
fixture = example_npc, placement_name, fidget_script, role
```

* **Pass 1** (`BuildContentBases`, [BaseDB.cpp:415](../../src/Content/BaseDB.cpp#L415); `FContentBase::LoadFactions`, [:175](../../src/Content/BaseDB.cpp#L175)): for every `[MBase]` the first value line must be `nickname` (base nickname, resolved with `GetBaseID`; read as the first line whatever its key), then the rest of that section, then the `[BaseFaction]` sections up to the next `[MBase]`. Other section kinds are skipped. If the nickname is not a known base the scan only continues when the next section is another `[MBase]`; any other next section ends the whole scan.
* **Pass 2** (`FContentBase::Load`, [:252](../../src/Content/BaseDB.cpp#L252)): the file is opened again, the `[MBase]` whose `nickname` matches (case-insensitive) is found, and `[MVendor]`, `[GF_NPC]` and `[mRoom]` sections are read until the next `[MBase]`. The section kinds the pass does not know (`[BaseFaction]`) are skipped.

### 5.1 `[MBase]` (pass 1, rest of the section after the nickname line)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `local_faction` | string | -1 | Reputation group nickname of the base's local faction. |
| `diff` | int | 10 | Difficulty level of the base. |
| `msg_id_prefix` | string | empty | Comm message prefix. |

### 5.2 `[BaseFaction]` (pass 1)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `faction` | string | -1 | Reputation group nickname. |
| `weight` | float | -1 | Selection weight among the base's factions. |
| `mission_type` | string, float, float, [float] | none | Repeatable. **Value 0 is not read** (the type is always `DestroyMission`); value 1 minimum and value 2 maximum difficulty (not clamped here); optional value 3 weight (default 1.0). The entry is stored only if the build flag for the current game mode is set; both flags are always true in the port. |
| `npc` | id | none | Repeatable; a `[GF_NPC]` nickname of this base that belongs to this faction. After loading, every NPC named here gets this section's faction as affiliation (overriding the NPC's own `affiliation`). Duplicates are ignored. |

### 5.3 `[MVendor]` (pass 2)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `num_offers` | int min, int max | 0, 0 | Number of mission offers shown by the mission vendor. |

### 5.4 `[GF_NPC]` (pass 2, [BaseDB.cpp:55](../../src/Content/BaseDB.cpp#L55))

A bar/base character. Stored under `CreateID(nickname)` (first definition wins). The loader computes whether the record is complete (nickname, a body from `body` or from a `base_appr` costume, a voice and an `individual_name`) but its caller does not test the result, so incomplete records are stored too. These keys are the character-description keys of [ai-and-encounters.md](ai-and-encounters.md#62-missionsspecific_npcini---npc-npcshipscpp160): `nickname`, `base_appr`, `head`, `body`, `lefthand`, `righthand`, `accessory`, `voice`, `individual_name`, `affiliation`. In addition:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `misn` | string, float, float | none | Repeatable mission offer: value 0 mission type text (stored, not validated here), values 1 and 2 minimum and maximum difficulty, each clamped to 0..100. |
| `bribe` | string, int, int | none | Repeatable. Value 0 reputation group nickname, value 1 cost, value 2 text id. Dropped if the group is unknown or the cost is 0. Offered while the player's standing with the group is below 0.6 ([BaseNpcRecords.cpp:41](../../src/Content/BaseNpcRecords.cpp#L41)). |
| `know` | int, int, int, int | none | Repeatable. Value 0 and value 1 are resource ids (value 1 is also the key under which the player's "already heard" record is kept), value 2 the price, value 3 the required reputation tier 0-3. Dropped when value 3 is 4 or more ([BaseNpcRecords.cpp:72](../../src/Content/BaseNpcRecords.cpp#L72)). |
| `rumor`, `rumor_type2` | string, string, int, ints... | none | Repeatable (both keys give the same record kind). Value 0 and value 1 are mission-stage names (first and last stage in which the rumour is told), value 2 the required reputation tier, values 3.. rumour ids (the port stores at most 8). Stage names are matched case-insensitively against a table of 43 names that run `base_0_rank`, `mission_01a_loaded`, `mission_01a_accepted`, ... `freetime_NN_MM`, ... `mission_end`; an unknown name keeps the previous value. Tier values are used as an index into a four-entry threshold table (0.0, 0.2, 0.4, 0.6) and are not validated ([BaseNpcRecords.cpp:82](../../src/Content/BaseNpcRecords.cpp#L82), table at [RandomMissionDB.cpp:64](../../src/Content/RandomMissionDB.cpp#L64)). |
| `room` | string | none | Repeatable; a room (location) nickname of this base (`FindLocation`) where the character may appear. |

### 5.5 `[mRoom]` (pass 2)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `nickname` | string | none | Location nickname of the room inside the base (`FindLocation`). A room whose id resolves to nothing is dropped. |
| `character_density` | int | 0 | Number of characters placed in the room. |
| `fixture` | string npc, string placement, string fidget, string role | none | Repeatable. Value 0 a `[GF_NPC]` nickname (which must appear **earlier** in the file; otherwise the line is ignored), value 1 the name of a character placement in the room's own file (the line is dropped if the room has no such placement), value 2 a fidget script name (empty: a random fidget is chosen), value 3 a free-text role such as `bartender` or `trader`; the role is compared with the role names of RTC scripts and is not validated. |

---

## 6. `missions\news.ini`

Read by `LoadNewsItems` ([BaseSupport.cpp:179](../../src/Content/BaseSupport.cpp#L179)); each `[NewsItem]` section is appended to the list in file order (no duplicate check). Which items a base shows depends on `base` and `rank`.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `base` | string | none | Repeatable; base nickname (`GetBaseID`) the item is shown at; duplicates ignored. |
| `rank` | string, string | stages 0 to 42 | First and last mission-stage name (same 43-name table as for rumours) between which the item is shown. An unknown name keeps the previous stage value. |
| `breaking` | flag | off | Presence alone sets the "breaking" flag; no value is read. |
| `autoselect` | flag | off | Presence alone sets the "autoselect" flag. |
| `icon` | string | category 0 | `critical` (1), `world` (2), `mission` (3), `system` (4), `faction` (5), `universe` (6); other text leaves the category. |
| `category` | int | none | Resource id of the mad-lib template stored in the item's headline slot (first parameter only). |
| `headline` | int | none | Resource id of a mad-lib template stored in the item's text slot. |
| `text` | int, ints... | none | Value 0 the mad-lib resource id, the following values the string ids appended to it (stored in the item's audio-text slot). |
| `logo` | string | none | Logo texture name (allocated once; not validated). |
| `audio` | string | none | Audio name. |

The three template keys are stored in the slots listed above in the port's data structure (slot names in the port may not reflect their in-game role); the key names are exactly as tested by the code.

---

## 7. Save-game sections `[DynBaseInfo]` and `[DynSysInfo]`

On `LoadGame` the Content module receives a reader on the save text and calls `LoadDynBaseInfo` ([BaseDB.cpp:447](../../src/Content/BaseDB.cpp#L447)) and `LoadDynSysInfo` ([SystemInfo.cpp:2074](../../src/Content/SystemInfo.cpp#L2074)). Each first rebuilds the whole base or system table from the universe, then scans for its section. The `nickname` value of the section is resolved and looked up in the table, but the result is discarded: no dynamic state is restored from these sections. The matching save-time functions (`SaveContentBases`, `VisitSystemInfos`) walk their tables with empty bodies, so nothing is written for them either.

| Section | Key | Type | Meaning |
|---|---|---|---|
| `[DynBaseInfo]` | `nickname` | string | Base nickname (`GetBaseID`); first one per section only. |
| `[DynSysInfo]` | `nickname` | string | System nickname (`GetSystemID`); first one per section only. |

Related Content sections of the same save text (`[locked_gates]`, mission and player state) are covered in [missions.md](missions.md).
