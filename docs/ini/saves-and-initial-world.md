# Initial world, save games and new-character templates

These files hold the *state* of the game rather than its static data: the start-of-game world (`InitialWorld.ini`), the per-character save files (`*.fl`) and the template files from which new characters and restart files are generated. All of them are INI **text** (not the binary BINI format), read with `INI_Reader`.

**File location / how it is found.**

| File | Where | Found by |
|---|---|---|
| `InitialWorld.ini` | Data folder | `freelancer.ini` `[Freelancer] initial_world` (server) and `[Data] Groups` (reputation); see [freelancer-ini.md](freelancer-ini.md) |
| Character saves `<name>.fl` | `<My Documents>\My Games\Freelancer\Accts\<account folder>\` in single-player, `...\Accts\MultiPlayer\<account folder>\` for a multiplayer server | every `*.fl` in an account folder is a character; the single-player account folder is the account name, the multiplayer one is a hash of it |
| `Restart.fl`, `AutoSave.fl` | same folder | reserved file names (see below) |
| `NewCharacter.fl` (single-player), `MPNewCharacter.fl` (multiplayer), `NewPlayer.fl` | **directory of the running executable** | fixed names. The repository's `bin` folder contains `MPNewCharacter.fl` and `NewPlayer.fl`; `NewCharacter.fl` is not present in this checkout. |

## Container format of `.fl` files

A save or template file is one of three things, told apart by its first four bytes (the same three-way test is used wherever a `.fl` file is read):

| First bytes | Meaning |
|---|---|
| `FLS1` | the rest of the file is the INI text, obfuscated with a fixed position-dependent XOR (see [server_savefile.cpp](../../src/Server/server_savefile.cpp)). Every save the game writes is stored this way, in single-player and multiplayer alike. |
| `FLSG` | rejected: the file is treated as unreadable. |
| anything else | the file is plain INI text from byte 0. Hand-written templates such as `MPNewCharacter.fl` use this form. |

After decoding, the text is parsed with `INI_Reader::open_memory`, so everything in [parsers.md](parsers.md) applies (this means `.fl` files can never be BINI). The `[Player]` reader additionally treats the value names `/*` and `*/` as a nested block-comment pair: while the depth is above zero all keys are skipped. Names and descriptions are stored as hexadecimal (see `name`, below).

## `InitialWorld.ini`

The only text INI in the retail `DATA` folder. It describes the world as it is when a new game or a new multiplayer character starts: the faction groups with their mutual reputation, and which jump gates start locked. In a running game the same section names appear inside save files, because a save is written in this format too.

It is read in four places:

| Reader | When | What it reads |
|---|---|---|
| `Reputation::Startup` ([common_reputation.cpp](../../src/Common/common_reputation.cpp)) | at start-up, from `[Data] Groups = ...` (client and server) | the `[Group]` headers: group identity and string IDs |
| `Server_LoadWorldState` ([IServerImpl.cpp](../../src/Server/IServerImpl.cpp)) | multiplayer server start-up; in single-player the same step runs on the loaded character save instead | `[Time]`, then `Reputation::LoadFeelings` (`rep` lines of `[Group]`), then the open reader is handed to all controllers as the *LoadGame* message |
| Content `GameSupport` (message *LoadGame*) | right after the above | `[DynBaseInfo]`, `[locked_gates]`, `[DynSysInfo]` (see below) |
| `PlayerDB::create_restart_file` ([server_classes.cpp](../../src/Server/server_classes.cpp)) | creating `Restart.fl` | the whole file is copied verbatim behind the `NewPlayer.fl` template |

### `[Group]` (repeated, one per faction group)

Group identity is read **positionally** by `Reputation::Startup`: the reader does not look at the key names of the first two lines and then expects the third and fourth by name.

| Line (in this order) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| 1. `nickname` | string (max 15 characters) | none | Group nickname. Its 16-bit group ID is a lower-cased CRC of the name (`MakeId`), so two nicknames with equal CRC would collide. The key name is not checked. | [common_reputation.cpp](../../src/Common/common_reputation.cpp) (`ReadGroupBlock`) |
| 2. `ids_name` | int | 0 | String-resource ID of the group's name. The key name is not checked. | same |
| 3. `ids_info` | int | 0 | String-resource ID of the group's info card. Only taken if this line's key is `ids_info`; otherwise 0 (the line is consumed either way). | same |
| 4. `ids_short_name` | int | 0 | String-resource ID of the short name. Same rule as `ids_info`. | same |
| `rep` (repeated) | `[float, string]` | none | `rep = <feeling>, <other group nickname>`: how this group feels about the other (a signed fraction; not range-checked). **Not read at start-up**; `LoadFeelings` reads them when a world or save is loaded. For each group the first `rep` toward a given other group wins. A group with no `rep` line logs "save game has corrupted reputation data". | `Reputation::LoadFeelings` |

When a **save** is loaded, `LoadFeelings` locates each `[Group]` by its first line (again positionally: the *first value of the block*, whatever its key, is the nickname) and then reads only the `rep` keys. A `[Group]` whose nickname is not already known is skipped.

```ini
[Group]
nickname = grp_a
ids_name = 100001
ids_info = 100002
ids_short_name = 100003
rep = 0.5, grp_a
rep = -0.3, grp_b
```

### `[Time]`

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| (first value) `seconds` | float | 0.0 | Elapsed game time. The reader takes the first value of the section and ignores the key name. `InitialWorld.ini` has no `[Time]`, so a new multiplayer world starts at 0; saves written in single-player contain `seconds = <float>`. | [IServerImpl.cpp](../../src/Server/IServerImpl.cpp) |

### `[locked_gates]`

Jump gates and holes that are closed at the start of the game. Read by `LoadContentGateLocks` ([SpaceSupport.cpp](../../src/Content/SpaceSupport.cpp)) on the *LoadGame* message; the writer in the same file emits the same section into saves.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `locked_gate` | id (repeated) | none | Hashed ID of a gate/hole object, written as a decimal number (values above 2^31 are normal; a trailing `; nickname` comment documents it). Locks the gate for the player. | same |
| `npc_locked_gate` | id (repeated) | none | Same, but the lock applies to NPC ships. | same |

### `[DynBaseInfo]` and `[DynSysInfo]`

Read by `LoadDynBaseInfo` ([BaseDB.cpp](../../src/Content/BaseDB.cpp)) and `LoadDynSysInfo` ([SystemInfo.cpp](../../src/Content/SystemInfo.cpp)): the reader finds the section, takes the first `nickname` key, looks the object up and stops. The looked-up result is not used, and nothing in the port writes these sections (the matching save routines are empty loops, as in retail). Both loaders rebuild their base/system tables from the universe before scanning, so the sections can be absent. See also [systems-and-bases.md](systems-and-bases.md).

## Character save files (`[Player]`)

A character file starts with a `[Player]` section; the single-player writer then appends `[mPlayer]`, mission sections, `[Time]` and `[Group]` blocks. Parsed by `PlayerData_LoadSaveRecord` ([server_savegame.cpp](../../src/Server/server_savegame.cpp)) into a record that is later copied onto the live player. Several `[Player]` sections in one file are all read (the per-section defaults are reset for each) and the last values win. Keys unknown to the reader are skipped. Names are case-insensitive. A `[Player]` section is required: a file without one is rejected.

Per-section defaults set at the start of every `[Player]`: position `0,0,0`, identity orientation, no system/base/location, empty description, `money = 1968`, `interface = 3`, empty weapon groups and visit table.

| Key | Type | Default | Meaning / allowed values | Written by saves |
|---|---|---|---|---|
| `name` | hex wide string | none | Character name. If the text consists only of hexadecimal digits it is decoded as four hex digits per UTF-16 code unit; otherwise it is taken as plain multibyte text. | yes (hex) |
| `description` | hex wide string | empty | Save description, same encoding as `name`. | when given |
| `descrip_strid` | int | 0 | String-resource ID used instead of a text description. | when non-zero |
| `tstamp` | `[int, int]` | 0, 0 | Save time as the high and low 32-bit halves of a Windows FILETIME. Both must be present, otherwise both are 0. | yes |
| `ship_archetype` | id or nickname | none | Ship archetype: a decimal ID, or if that parses as 0 a nickname that is hashed and resolved. The ship's undamaged collision-group list is loaded. Must precede `collision_group`. | yes |
| `system` | string | none | System nickname; unknown names log an error. | yes |
| `base` | string | none | Base nickname the character is docked at; unknown names log an error. | yes |
| `last_base` | string | none | Last visited base; unknown names log an error. | yes |
| `location` | id | 0 | Location ID inside the base (hash); a `Deck2` location is folded back to `Deck` after loading. | yes |
| `rank` | int | n/a | Rank. | yes |
| `money` | float, stored as int | 1968 | Cash. | yes |
| `num_kills` | float, stored as int | n/a | Kill counter. | yes |
| `num_misn_successes` | float, stored as int | n/a | Missions succeeded. | yes |
| `num_misn_failures` | float, stored as int | n/a | Missions failed. | yes |
| `pos` | `[float, float, float]` | 0,0,0 | Position in the system. | yes |
| `rotate` | `[float, float, float]` | identity | Orientation as Euler angles, converted to a matrix. | yes |
| `starting_ring` | id | none | Trade-lane ring nickname, hashed. | no |
| `group` / `initial_rep` | string | none | Group nickname: the character's reputation relations are rebuilt from that group's current feelings toward every group (a feeling of 1.0 or more is clamped to 0.95). Used by templates; clears any `house` lines read earlier. | no |
| `rep_group` | string | `""` | The character's own affiliation group. An empty value resolves the empty name. | yes |
| `house` | `[float, string]` (repeated) | none | `house = <feeling>, <group nickname>`: one relation entry. | yes |
| `voice` | string (max 31 chars) | none | Voice nickname. | yes |
| `<part>` and `com_<part>` | string | none | Costume parts of the worn costume and of the comm (conversation) costume. `<part>` is one of `body`, `head`, `lefthand`, `righthand` (looked up in the body-part table), `accessory` (repeated, looked up in the accessory table), `costume` (a whole costume nickname) or `hair` (obsolete: warns and is ignored). Matching is case-sensitive. A name the tables do not know warns and yields ID 0. | the four parts only |
| `equip` | `[arch, hardpoint, health]` (repeated) | none | Mounted equipment. `arch` is a decimal ID or a nickname; an empty hardpoint makes the item internal; `health` is optional (float). | yes |
| `cargo` | `[arch, count, hardpoint, health, mission]` (repeated) | none | Cargo item; `count` is optional; `mission` is a bool. | yes |
| `base_equip`, `base_cargo` | as `equip`, `cargo` | none | The same lists for the character's equipment as stored at the base. | yes |
| `collision_group` | `[group nickname, health]` (repeated) | none | Damage state of one of the ship's collision groups. Ignored with a warning when it appears before `ship_archetype` or names a group the ship lacks. | yes |
| `base_collision_group` | as above | none | Same, for the base-stored ship. | yes |
| `hull_status` / `base_hull_status` | float | n/a | Hull health of the current / base-stored ship. | yes |
| `wg` | raw line (repeated) | none | Weapon-group definitions: each whole line is appended to the weapon-group text and sent to the client unparsed. | yes |
| `visit` | `[id, int]` (repeated) | none | `visit = <ID>, <flags byte>`: one entry of the visited table (the shipped templates list star systems). | yes |
| `log` | `[id, count, (data, tag, flags)*]` (repeated) | none | One mission-log entry as a nested format string: a mad-lib ID, the number of values, then three ints per value (data, tag, flag byte). Tag `0x21` means the data is itself a nested entry that follows inline. | yes |
| `interface` | int | 3 | Interface state value passed to the client; not validated. | yes |
| `logid` | n/a | n/a | Tested and ignored. | no |

Not validated: ranges of `rank`, `money`, counters and health values; whether a `base` and `location` belong together.

### Sections the single-player server appends

When the server saves in single-player it writes, after `[Player]`: the sections each Content controller adds for the player (the *PlayerSave* message), then `[Time]` and the `[Group]` blocks (`Reputation::Save`: a `nickname` line plus one `rep = <feeling>, <group>` line per relation), then the controllers' *SaveGame* output. On load the same reader is rewound for each consumer, so section order does not matter. The sections below are read by Content; the semantics belong to the mission documents ([missions.md](missions.md), [random-missions.md](random-missions.md)).

| Section | Keys as read | Reader |
|---|---|---|
| `[mPlayer]` | `locked_gate` (id, repeated), `vnpc` `[4 ints]` (repeated), `can_dock` (bool), `dock_exception` (int, repeated), `can_tl` (bool), `tlr_exception` `[int, int]` (repeated), `rumor` `[int, int]` (repeated); the statistics keys `ship_type_killed` `[type, count]`, `rm_completed` / `rm_aborted` / `rm_failed` `[mission type, count]`, `total_cash_earned` (float), `total_time_played` (float), `sys_visited`, `base_visited`, `holes_visited` (id, repeated) | `LoadContentPlayerState`, `LoadContentPlayerStatsValue` in [Support.cpp](../../src/Content/Support.cpp) |
| `[RandomMission]` | `type` (string; selects the random-mission loader, which then reads its own keys) | `LoadContentPlayerState` |
| `[StoryInfo]` | `Mission` `[story mission name, generic-mission file]` (`No_Mission` = none; the second value only for `GenericMission`), `MissionNum` (int), `delta_worth` (float), `ship_bought` (bool), `debug` (bool) | `FStory::OnLoad` in [Story.cpp](../../src/Content/Story.cpp) |
| `[Mission01aSave]`, `[Mission01bSave]` | `MissionStateNum` (int) | [StoryMissions.cpp](../../src/Content/StoryMissions.cpp) |
| `[MissionState]` | `rtc` `[script path, bool]`, `ambi_scene` `[path, room id, base id]`, `friendly_fire_excused` (label), `mission_accepted` (bool), `att_clamp`, `tradelane_attacks`, `scan_clamp`, `gcs_clamp`, `random_pop`, `hostile_clamp` (bool each), `msn_offer`, `msn_title` (int), `nnpath` (compound line). The writer also emits `story_cue`, which no reader consumes. Reading stops at a `[BStoryMissionDone]` marker. | `FMissionScript::LoadMissionState` in [MissionScript.cpp](../../src/Content/MissionScript.cpp) |
| `[TriggerSave]` | `trigger` (id, repeated): an active trigger. After it, the trigger's condition reads its own saved state from the same reader; the condition writers emit lines such as `Cnd_Timer` or `Cnd_DistShip` (see [MissionCnd.cpp](../../src/Content/MissionCnd.cpp)) | same |
| `[NNObjective]` | `nickname` (id), `type` (number or `navmarker`, `base`, `ids`, `rep_inst`, followed by that objective type's own parameters), `state` (number, or `ACTIVE` / `HIDDEN`), `hidden` (bool). Only read after a `[MissionState]` was seen. | `LoadNNObjective` in [MissionObject.cpp](../../src/Content/MissionObject.cpp) |
| `[RandomEncounter]` | `nickname` (string), `enabled` `[bool, npc count]`, `encounter_type` (`TL_Attack` or `SP_Attack`, only for the entry named `RandomMission`; the encounter's own parameters follow) | `FMissionObject::LoadRandEncsSave` in [MissionSave.cpp](../../src/Content/MissionSave.cpp) |
| `[MObjective]`, `[MetaBehavior]` | objective/behaviour lists in the mission-script directive format | `LoadObjList`, `LoadMetaBehavior` |
| `[MsnShipSave]` | `nickname` (string), `npc` (id), `random_name`, `jumper`, `original_jump_flag` (bool), `label` (id, repeated), `ol_priority`, `hit_pts` (int), `radius` (float) | `LoadMissionSave` in [MissionSave.cpp](../../src/Content/MissionSave.cpp) |
| `[MsnWingSave]` | `nickname`, `position` `[3 floats]`, `orientation` `[9 floats]`, `arrival_obj` (id), `formation` (id); the writer's `ship` lines are not read here (the ships come from the preceding `[MsnShipSave]` blocks) | same |
| `[MsnRandEncSave]` | `rand_enc_owner` (id), `position` `[3 floats]`, `orientation` `[9 floats]`, `ol_priority`, `hit_pts`, `num_ships` (int), `ship_type` (up to 32 ids, ended by `END`), `names` (up to 32 names, ended by `END`) | same |
| `[MsnSolarSave]` | `solar_name` (string, repeated) | same |
| `[MarkObj]` | `name` (string, repeated) | same |
| `[Invulnerables]` | `invulnerable` `[name, seconds float, bool]` (repeated) | same |
| `[Decloaked]` | `decloaked` (name, repeated) | same |
| `[MsnVibeInfo]` | `vibe` `[name A, name B, rep A id, rep B id, attitude float]` (repeated); the name `empty` leaves that side unnamed | `FMissionObject::LoadVibeInfos` |

## New-character templates

`PlayerData_CreateNewCharacter` ([server_savegame.cpp](../../src/Server/server_savegame.cpp)) creates a new character by streaming a template (`NewCharacter.fl` in single-player, `MPNewCharacter.fl` in multiplayer) into the new save file, section by section, substituting placeholders on the way. The output file is named `<hash of the character name>.fl` in the account folder and is then encoded like any save. The character's faction, package, home base and pilot come from the `NewCharDB` table (see [world-data.md](world-data.md)); if all four lookups fail, no file is created, and if only some fail the output is an empty file and creation fails.

* Every line is copied as `key = value1, value2, ...`; a line with no value repeats the previous line's joined text (a retail quirk).
* A **value** that starts with `%` and equals one of the placeholders below (case-insensitive) is replaced; anything else is copied unchanged.
* A **key** `%%Package%%` is replaced by a `ship_archetype` line plus one `equip` / `cargo` line per entry of the package's loadout.

| Placeholder | Replaced by |
|---|---|
| `%%Name%%` | the chosen character name, hex-encoded as in `name` |
| `%%Faction%%` | the faction's reputation-group nickname |
| `%%Money%%` | the package's starting cash |
| `%%Base_Costume%%`, `%%Comm_Costume%%` | the pilot's body and comm costume nicknames |
| `%%Voice%%` | the pilot's voice nickname |
| `%%Home_System%%`, `%%Home_Base%%` | the nicknames of the home base's system and of the base (if the lookup fails the placeholder text stays) |

```ini
[Player]
name = %%NAME%%
rank = 0
money = %%MONEY%%
costume = %%BASE_COSTUME%%
com_costume = %%COMM_COSTUME%%
system = %%HOME_SYSTEM%%
base = %%HOME_BASE%%
house = 0.5, grp_a
%%PACKAGE%%
```

## `Restart.fl` and `AutoSave.fl`

* `Restart.fl` is created in an account folder by `PlayerDB::create_restart_file`: the decoded `NewPlayer.fl` template, followed by the full text of the initial-world file (`initial_world`), written as a normal encoded save.
* `AutoSave.fl` is the name under which the single-player auto-save writes the current state; multiplayer auto-save writes the selected character's own file.
* Both names are reserved: they are recognised by name (case-insensitively) when the character list is built.

## Writers

The only code that writes these formats is in the server and Content: `PlayerData_SerializeToFile` ([server_pub.cpp](../../src/Server/server_pub.cpp)) for `[Player]`, `Server_SaveSinglePlayerState` for `[Time]` and `[Group]`, the `Save` routines of the Content controllers for the other sections, and `PlayerData_CreateNewCharacter` and `PlayerDB::create_restart_file` for templates. Every save is written to a text file first and then re-read, XOR-encoded and rewritten with the `FLS1` prefix; if the write reports an error the half-written file is deleted.

## See also

* [parsers.md](parsers.md) for the reader grammar and the `FileMap`/`open_memory` behaviour.
* [server.md](server.md) for the server-side view of character files and templates.
* [world-data.md](world-data.md) for the `NewCharDB` table and the reputation groups.
* [missions.md](missions.md) for the meaning of the mission sections stored in a save.
