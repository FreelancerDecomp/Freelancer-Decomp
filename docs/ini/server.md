# FLServer.exe and Server.dll configuration

This page lists every place where the dedicated server (`FLServer.exe`) and the game-logic library (`Server.dll`, also used as the local server in single player) read INI-style files or other start-up configuration: `DACOMsrv.ini`, `freelancer.ini` (and its `SrvLocal-` twin), `InitialWorld.ini`, the character save files (`.fl`), new-character templates, fuse files, system/asteroid-field files opened on the server side, and the command line. Engine settings inside `DACOMsrv.ini` are described in [engine-dacom.md](engine-dacom.md); the shared `[Data]` table is in [freelancer-ini.md](freelancer-ini.md); the reader itself is in [parsers.md](parsers.md).

**File location / how it is found.** `FLServer.exe` computes the ini path from its own location: `ServerControl.SetIniPath(<path of FLServer.exe>)` keeps the directory and sets the file names `freelancer.ini` and `SrvLocal-freelancer.ini` in the same directory ([ServerControl.cpp:98](../../src/FLServer/ServerControl.cpp#L98)). `DACOMsrv.ini` is passed to `DALib::Startup(NULL, "DACOMsrv.ini")` ([FLServer.cpp:549](../../src/FLServer/FLServer.cpp#L549)) and resolved by the DACOM loader (current directory). `Server.dll` receives the `freelancer.ini` path from the host in `SStartupInfo.IniPath` ([IServerImpl.cpp:229](../../src/Server/IServerImpl.cpp#L229)); for the single-player local server the host is the game client and the path is the client's `freelancer.ini`.

---

## `DACOMsrv.ini` (FLServer only)

`FLServer.exe` starts the DACOM engine from `DACOMsrv.ini` instead of `DACOM.INI` ([FLServer.cpp:549](../../src/FLServer/FLServer.cpp#L549)); a failure to start returns from `InitInstance` and the server does not run. The file has the same grammar and sections as the client's `DACOM.INI` and is consumed only by the engine modules (`[DACOM]`, `[Libraries]`, `[System]`, `[Engine]`, `[RenderManager]`): see [engine-dacom.md](engine-dacom.md). No server code reads any key from it directly. The dev copy `bin\dacomsrv.ini` starts with `@include FL_Dev.ini`; that file does not exist in the repository and a missing include is silently ignored.

---

## `freelancer.ini` as read by `FLServer.exe`

`CFLServerApp::ReadFreelancerIni` is called twice at start-up, first for `freelancer.ini` and then for `SrvLocal-freelancer.ini` (a missing file is silently skipped), so values in the second file override the first ([FLServer.cpp:554](../../src/FLServer/FLServer.cpp#L554), [reader L757](../../src/FLServer/FLServer.cpp#L757)). Only the sections below are read here; the rest of the file (`[Data]`, `[Initial MP DLLs]`, ...) is read later by `Server.dll`.

| Section | Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `[Freelancer]` | `Data path` | str (whole value) | none | Data directory; opened with `DALib::OpenData` as the game's data root (`DALib::Data`). The dev file spells it `data path`; matching is case-insensitive. | [L765](../../src/FLServer/FLServer.cpp#L769) |
| `[Freelancer]` | `AppGUID` | GUID text `{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}` | all zero | Application GUID of the DirectPlay session. Parsed by `ParseGUID` (the all-zero text gives a zero GUID). | [L770](../../src/FLServer/FLServer.cpp#L774) |
| `[ListServer]` | `hostname` | str (whole value) | `localhost` | Host name or address of the Gun (list) server the server reports to. | [L779](../../src/FLServer/FLServer.cpp#L785) |
| `[ListServer]` | `port` | float truncated to int (`[0]`) | 39304 (0x9988) | Port of the list server. Not range-checked. | [L783](../../src/FLServer/FLServer.cpp#L789) |
| `[Server]` | `death_penalty` | - | - | The key is tested and the answer discarded, as in retail; it has no effect in this port. (`name` and `description` are commented out in the dev file and are not read anywhere; the server name and description come from the start-up dialog.) | [L797](../../src/FLServer/FLServer.cpp#L800) |
| `[Resources]` | `DLL` | str (whole value) | none | Resource DLL loaded with `LoadLibraryA` and registered for string lookup (`AddResourceDll`). Repeated keys load several. | [L804](../../src/FLServer/FLServer.cpp#L807) |

The function returns whether a `Data path` was found; the start-up code does not use the result.

### Other start-up configuration that is not INI

* **Command line** ([FLServerCmdLine.cpp:26](../../src/FLServer/FLServerCmdLine.cpp#L26)). A switch introduced by `-` or `/`; the letter is case-insensitive. A value that is rejected is logged and ignored.

  | Switch | Argument | Effect (default) |
  |---|---|---|
  | `B` | optional path | Write a bandwidth log; without a path `FLServerBWLog-<unix time>.txt`. |
  | `P` | decimal port | Game port (default 14041, 0x36D9). Not range-checked. |
  | `L` | file name | Open a text log file (reports an error if it cannot be created). |
  | `S` | decimal level | Log level, accepted range 1..7 (default 4). |
  | `M` | decimal | Maximum lines kept in the console, 1..100000 (default 100000). |
  | `T` | decimal | Lines trimmed when the console is full, 1 .. `M`-1 (default 200). |
  | `H` | float | Server update rate in updates per second, accepted range 1..60; stored as the period `1/rate` (default period 0.05 s, i.e. 20 per second). |
  | `E` | one character | Add a log category. |
  | `X` | `+` (or none) / `-` | Unmask / mask FPU exceptions (default masked). |
  | `C` | none | Accept the saved server configuration without showing the start-up dialog. (Also falls through into the "unknown switch" message, as in retail.) |

* **`FLServer.cfg`** in the user-data directory is a **binary** file written by the start-up dialog (version 3; server name, description, password as wide strings, flags for new players / internet reporting / PvP, `MaxPlayers` int). Values: max players 1..128 (dialog default 16), server name up to 32 characters, description up to 128, password up to 16 ([ServerConfigDlg.cpp:52](../../src/FLServer/ServerConfigDlg.cpp#L52)). It is not an INI file.
* **`motd.dat`** in the user-data directory: UTF-16 text (at most 0x13FF characters) shown to players on login; read by the server thread ([FLServer.cpp:342](../../src/FLServer/FLServer.cpp#L342)).

---

## `freelancer.ini` as read by `Server.dll`

`IServerImpl::Startup` ([IServerImpl.cpp:202](../../src/Server/IServerImpl.cpp#L202)) runs, in order, with the ini path it was given:

1. `DataConfig::startup(iniPath)` - walks every `[Data]` key ([freelancer-ini.md](freelancer-ini.md)); if it fails the following steps are skipped and `Startup` returns false.
2. `Server_ReadWorldIni(iniPath)` - `[Freelancer] initial_world`.
3. `Server_LoadInitialDlls(iniPath)` - `[Initial SP DLLs]` / `[Initial MP DLLs]`.
4. Multiplayer only: `Server_LoadWorldState()` - reads the initial-world file named in step 2.

### `[Freelancer] initial_world`  ([IServerImpl.cpp:2825](../../src/Server/IServerImpl.cpp#L2825))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `initial_world` | str (whole value, up to 259 chars) | empty | File name of the initial world, relative to the data directory (the dev ini uses `InitialWorld.ini`). The first occurrence wins; later ones are ignored. Used by `Server_LoadWorldState` (multiplayer start-up) and by the restart-file creation (see below). | [L2831](../../src/Server/IServerImpl.cpp#L2831) |

### `[Initial SP DLLs]` and `[Initial MP DLLs]`  ([IServerImpl.cpp:2922](../../src/Server/IServerImpl.cpp#L2922))

The section matching the running mode is used: `[Initial SP DLLs]` when `SinglePlayer()` is true, otherwise `[Initial MP DLLs]`. The other section is skipped. They list the **controller DLLs** (the mission/AI/population modules, all in `Content.dll` in the shipped configuration) in creation order.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `path` | str (whole value, up to 259 chars) | empty | Directory prepended to relative controller DLL names (`pub::DLL_LoadLibrary`). Remains set for the whole session. | [L2950](../../src/Server/IServerImpl.cpp#L2950) |
| `DLL` | `[0]` DLL file, `[1]` exported interface name, `[2]` priority | none | One controller per line; repeated keys create controllers in file order. `[0]` and `[1]` are copied with a 259-character limit, `[2]` with a 31-character limit. `[2]` is one of `HIGHEST` (0), `ABOVE_NORMAL` (1), `NORMAL` (2), `BELOW_NORMAL` (3), `LOWEST` (4), case-insensitive. **A missing or unknown priority keeps the priority of the previous `DLL` line** (HIGHEST/0 for the first line). The priority orders the controllers in the dispatch list. | [L2958](../../src/Server/IServerImpl.cpp#L2958) |

Example (synthetic):

```ini
[Initial MP DLLs]
path = .
DLL = Content.dll, GameSupport, HIGHEST
DLL = Content.dll, SpaceSupport, NORMAL
DLL = Content.dll, SpacePop, LOWEST   ; priority tokens, comments after ';'
```

### `[Time]` and reputation in `InitialWorld.ini` / save files  ([IServerImpl.cpp:117](../../src/Server/IServerImpl.cpp#L117), [:2862](../../src/Server/IServerImpl.cpp#L2862))

`Server_LoadWorldState` (multiplayer) opens `<data directory>\<initial_world>`; `Server_LoadSinglePlayerState` (single player, after a character is selected) uses the open reader of the character save file. Both perform the same three steps: (1) find the first `[Time]` section and take the **first value line of it, whatever its key is called** (`float` `[0]`; the saved games write `seconds = <float>`; default 0 when there is no `[Time]` section) as the server clock base; (2) `Reputation::LoadFeelings` reads the `[Group]` reputation data from the same reader ([saves-and-initial-world.md](saves-and-initial-world.md)); (3) the reader, still open, is passed to all controller DLLs as the `LoadGame` message, so every controller reads its own sections from it ([missions.md](missions.md), [ai-and-encounters.md](ai-and-encounters.md), [saves-and-initial-world.md](saves-and-initial-world.md)). A missing initial-world file is not reported at the open call.

---

## System and base files on the server

* `pub::BuildSystemReader` / `pub::BuildBaseReader` / `pub::System::EnumerateZones` only **open** the system file or base file for the calling controller DLL and hand the reader (or one reader per zone) to it; they read no key themselves ([server_pub.cpp:349](../../src/Server/server_pub.cpp#L349), [:367](../../src/Server/server_pub.cpp#L367), [:6827](../../src/Server/server_pub.cpp#L6827)). The system file path is `universe.ini` `[system] file` made absolute against the universe directory (an unopenable file logs "missing System file"); the base file path is the data directory plus `[Base] file` (no message when it cannot be opened). See [systems-and-bases.md](systems-and-bases.md).
* **Multiplayer system load** (`StarSystem::load`, [server_classes.cpp:6779](../../src/Server/server_classes.cpp#L6779)): the system file is opened and every `[Object]` section is created as a solar via `CSolar::ReadObj` (keys in [archetypes-ships-solars-asteroids.md](archetypes-ships-solars-asteroids.md#system-file-object-sections-through-the-solar-archetypes-csolarreadobj)). In single player the server reuses the solars and asteroid fields the client already built and reads nothing from the system file.

### Asteroid fields on the server

`SrvAsteroidSystem::load` ([server_classes.cpp:5798](../../src/Server/server_classes.cpp#L5798), multiplayer system load only) reads every `[Asteroids]` section of the system file:

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `file` | str (whole value, up to 255 chars) | empty | Asteroid-field file, relative to the data directory. | [L5817](../../src/Server/server_classes.cpp#L5817) |
| `zone` | str (whole value, up to 63 chars) | empty | Nickname of the zone the field fills. | [L5825](../../src/Server/server_classes.cpp#L5825) |

If either value is empty the section is silently skipped. An unknown zone logs "AsteroidField linked to missing Zone (%s)" and the server then dereferences it (retail behaviour).

`FSrvAsteroidField::load` ([server_classes.cpp:5668](../../src/Server/server_classes.cpp#L5668)) then opens the field file and makes a first pass over its `[Field]` section(s), reading these keys as **integers** (the number is parsed as a float and truncated, so `0.5` becomes 0):

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `cube_size` | int | 250 (or the field's current size) | Edge length of a grid cube. | [L5685](../../src/Server/server_classes.cpp#L5685) |
| `fill_dist` | int | 0 | Distance around the player within which cubes are populated. (The value of the file is always applied, so an absent key gives 0.) | [L5687](../../src/Server/server_classes.cpp#L5687) |
| `empty_cube_frequency` | int | 0 | Share of empty cubes; truncated to an integer on the server. | [L5689](../../src/Server/server_classes.cpp#L5689) |

and then re-reads the file from the start with `CAsteroidField::load` for the `[Cube]`, `[Exclusion Zones]` and `[LootableZone]` sections ([archetypes-ships-solars-asteroids.md](archetypes-ships-solars-asteroids.md#asteroid-field-files-casteroidfieldload)).

---

## Fuse files

The repeated `[Data] fuses` keys (dev ini: `fx\fuse.ini` and `fx\fuse_*.ini`) are queued by `DataConfig` and loaded by `ServerFuseDB::LoadFile` in list order ([server_fuse.cpp:1246](../../src/Server/server_fuse.cpp#L1246)); the client loads the same files with its own reader ([client-world-fx-effects.md](client-world-fx-effects.md)). Fuses are referenced by nickname from the `fuse` keys of ship/solar archetypes and collision groups ([archetypes-ships-solars-asteroids.md](archetypes-ships-solars-asteroids.md)).

**Structure.** A file is a series of `[fuse]` sections each followed by *action* sections, until the next `[fuse]` ([ServerFuse::Read, L82](../../src/Server/server_fuse.cpp#L82)). Headers other than `fuse` that appear outside a fuse are skipped. The fuse is stored under its `name`; if a fuse with the same name was already loaded the later one is discarded (first wins). An action section whose header is not in the list below is ignored. Actions are ordered by their trigger time `at_t`.

### `[fuse]`  ([common_fuses.cpp:538](../../src/Common/common_fuses.cpp#L538))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `name` | id (whole value) | 0 | Fuse nickname. | [L551](../../src/Common/common_fuses.cpp#L550) |
| `lifetime` | float | -1.0 | Duration of the fuse in seconds; the burn position advances by `dt / lifetime`. Not validated (a value of 0 or less is used as is). | [L553](../../src/Common/common_fuses.cpp#L552) |
| `death_fuse` | bool | false | The fuse is a death fuse. | [L555](../../src/Common/common_fuses.cpp#L554) |

### Keys of every action section

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `at_t` | float `[0]`, optional float `[1]` | -1.0 (unset) | Trigger position along the fuse timeline (0..1 of the lifetime). With a second value the position is a **random** value between the two, picked when the fuse ignites. | [common_fuses.cpp:448](../../src/Common/common_fuses.cpp#L448) |

### Action sections

Each section reads `at_t` plus the keys below; unknown keys are ignored. Names are case-insensitive unless a comparison is called out as case-sensitive.

| Section | Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `[ignite_fuse]` | `fuse` | id (whole value) | 0 | Fuse to light. | [L216](../../src/Server/server_fuse.cpp#L217) |
| | `fuse_t` | float | 0.0 | Start position given to the lit fuse. | [L218](../../src/Server/server_fuse.cpp#L219) |
| `[destroy_root]` | - | - | - | No own keys; destroys the object. | [L282](../../src/Server/server_fuse.cpp#L282) |
| `[damage_root]` | `damage_type` | `absolute` or other | can kill | `absolute` (case-insensitive) means the damage cannot kill the object; any other text means it can. | [L329](../../src/Server/server_fuse.cpp#L329) |
| | `hitpoints` | int | 0 | Hit points removed. | [L337](../../src/Server/server_fuse.cpp#L337) |
| `[destroy_group]` | `group_name` | cstr | empty | Collision-group name (`obj` of a `[CollisionGroup]`); the text `random` (case-insensitive) picks a random living group. | [L400](../../src/Server/server_fuse.cpp#L400) |
| | `fate` | `debris` \| `disappear` | 0 | Case-sensitive, at most 31 chars. `debris` = 2, `disappear` = 3; any other value leaves 0. | [L404](../../src/Server/server_fuse.cpp#L404) |
| `[damage_group]` | `group_name` | cstr | empty | As above (no `random` handling). | [L517](../../src/Server/server_fuse.cpp#L517) |
| | `damage_type` | `absolute` or other | can kill | `absolute` (case-sensitive here) = cannot kill. | [L521](../../src/Server/server_fuse.cpp#L521) |
| | `hitpoints` | int | 0 | Hit points removed. | [L532](../../src/Server/server_fuse.cpp#L532) |
| `[destroy_hp_attachment]` | `hardpoint` | cstr | empty | Hardpoint whose attachment is destroyed; `random` (case-insensitive) picks one. | [L596](../../src/Server/server_fuse.cpp#L596) |
| | `fate` | `debris` \| `disappear` \| `loot` | 0 | Case-sensitive: `debris` = 2, `disappear` = 3, `loot` = 5; others leave 0. | [L600](../../src/Server/server_fuse.cpp#L600) |
| `[damage_hp_attachment]` | `hardpoint` | cstr | empty | Hardpoint whose attachment is damaged. | [L725](../../src/Server/server_fuse.cpp#L725) |
| | `damage_type` | `absolute` or other | can kill | `absolute` (case-sensitive) = cannot kill. | [L729](../../src/Server/server_fuse.cpp#L729) |
| | `hitpoints` | int | 0 | Hit points removed. | [L739](../../src/Server/server_fuse.cpp#L739) |
| `[impulse]` | `hardpoint` | cstr | empty | Hardpoint the impulse is applied at. | [L809](../../src/Server/server_fuse.cpp#L809) |
| | `force` | float | 0.0 | Impulse magnitude. | [L813](../../src/Server/server_fuse.cpp#L813) |
| | `radius` | float | 0.0 | Radius of effect. | [L817](../../src/Server/server_fuse.cpp#L817) |
| | `damage` | float | 0.0 | Damage dealt. | [L821](../../src/Server/server_fuse.cpp#L821) |
| | `pos_offset` | float x3 | 0, 0, 0 | Offset from the hardpoint. | [L825](../../src/Server/server_fuse.cpp#L825) |
| `[tumble]` | `ang_drag_scale` | float | 1.0 | Scale of angular drag while tumbling. | [L961](../../src/Server/server_fuse.cpp#L961) |
| | `turn_throttle_x`, `_y`, `_z` | float `[0]` low, float `[1]` high | 0..1 | Random range of the turn input on that axis. | [L965](../../src/Server/server_fuse.cpp#L965), [L970](../../src/Server/server_fuse.cpp#L970), [L975](../../src/Server/server_fuse.cpp#L975) |
| | `throttle` | float `[0]` low, float `[1]` high | 0, 0 | Random range of the throttle. | [L980](../../src/Server/server_fuse.cpp#L980) |
| | `engine_kill` | bool | false | Engine is disabled. | [L985](../../src/Server/server_fuse.cpp#L985) |
| `[make_invincible]` | `turn_on` | bool | true | Switch invincibility on or off. | [L1108](../../src/Server/server_fuse.cpp#L1108) |
| `[dump_cargo]` | `origin_hardpoint` | cstr (`[0]`) | empty | Hardpoint cargo is ejected from. | [L1171](../../src/Server/server_fuse.cpp#L1171) |

(The `Tumble` clone of an action keeps the constructor's turn-throttle ranges; this only matters to code that copies fuses.)

---

## Character save files (`.fl`) and templates

Character saves, `Restart.fl`, `AutoSave.fl`, the new-character templates and `NewPlayer.fl` are INI **text** parsed with `INI_Reader::open_memory`; the BINI format is not recognised for them. Details of how the files are written and which controller sections they contain: [saves-and-initial-world.md](saves-and-initial-world.md). This section lists what `Server.dll` itself reads.

**Container.** The first four bytes select the format ([server_savegame.cpp:352](../../src/Server/server_savegame.cpp#L352)): `FLS1` = scrambled text (the rest of the file is XOR-ed byte by byte with a key derived from the string "Gene..." and the byte index, `mask = (key[i & 3] + i) | 0x80`; [server_savefile.cpp:33](../../src/Server/server_savefile.cpp#L33)); `FLSG` = refused (the load fails); anything else = plain text from byte 0.

**Locations.** Character saves are `<hash>.fl` (the hash is derived from the character name) under `<user data>\Accts[\MultiPlayer]\<account directory>`; `Restart.fl` and `AutoSave.fl` are in the same directory. `NewCharacter.fl` (single player), `MPNewCharacter.fl` (multiplayer) and `NewPlayer.fl` are looked up **beside the executable** (`bin\`; the build copies `mpnewcharacter.fl`).

### `[Player]` section keys  ([PlayerData_LoadSaveRecord, server_savegame.cpp:345](../../src/Server/server_savegame.cpp#L345))

Only `[Player]` sections are read (other sections belong to the controller DLLs). Keys marked "repeat" may appear many times. Text between lines whose key is `/*` and `*/` is skipped (nesting is counted). At the start of every `[Player]` section the position, orientation, system, base, location, description, `descrip_strid`, weapon groups and visited list are reset, money is set to 1968 and `interface` to 3.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `name` | hex text or plain text (whole value) | empty | Character name. If every character is a hex digit it is decoded as UTF-16 code units of four hex digits each (`0041` = `A`), otherwise it is read as plain multibyte text. | [L419](../../src/Server/server_savegame.cpp#L419) |
| `description` | as `name` | empty | Save-game description. | [L425](../../src/Server/server_savegame.cpp#L425) |
| `descrip_strid` | int | 0 | String id of a built-in description. | [L431](../../src/Server/server_savegame.cpp#L431) |
| `tstamp` | int `[0]` high, int `[1]` low | 0, 0 | File-time stamp; both required, else both 0. | [L435](../../src/Server/server_savegame.cpp#L435) |
| `ship_archetype` | decimal id **or** nickname (whole value) | none | Ship. A value that parses as a non-zero integer is the archetype id, otherwise it is hashed. Must precede `collision_group` lines. | [L445](../../src/Server/server_savegame.cpp#L445) |
| `system` | system nickname | 0 | Current system; unknown name logs "invalid system name". | [L461](../../src/Server/server_savegame.cpp#L461) |
| `base` | base nickname | 0 | Docked base; unknown name logs an error. | [L471](../../src/Server/server_savegame.cpp#L471) |
| `last_base` | base nickname | not reset | Last base visited; unknown name logs an error. | [L481](../../src/Server/server_savegame.cpp#L481) |
| `location` | int | 0 | Location id inside the base (a "Deck2" location is mapped to "Deck"). | [L491](../../src/Server/server_savegame.cpp#L491) |
| `rank` | int | not reset | Player rank. | [L495](../../src/Server/server_savegame.cpp#L495) |
| `group` or `initial_rep` | group nickname (whole value, 15 chars) | - | Builds the player's relations to every reputation group from the group's feelings; a feeling of 1.0 or more is replaced by 0.95. Clears earlier `house` entries. | [L499](../../src/Server/server_savegame.cpp#L499) |
| `rep_group` | group nickname (`[0]`) | - | The player's own group; an empty value means the empty group. | [L510](../../src/Server/server_savegame.cpp#L510) |
| `house` | float `[0]` feeling, str `[1]` group | - | Adds one reputation entry (repeat). | [L518](../../src/Server/server_savegame.cpp#L518) |
| `voice` | str (whole value, 31 chars) | empty | Voice nickname. | [L527](../../src/Server/server_savegame.cpp#L527) |
| `com_body`, `com_head`, `com_lefthand`, `com_righthand`, `com_accessory`, `com_costume` | str (whole value) | - | Comm-window costume: body/head/hand part names from `bodyparts.ini`, accessory name (repeat), or a whole costume nickname. Unknown parts log warnings. **Matching of these key names is case-sensitive.** `com_hair` logs "hair is no longer a valid bodypart". | [L233](../../src/Server/server_savegame.cpp#L233) |
| `body`, `head`, `lefthand`, `righthand`, `accessory`, `costume`, `hair` | as above | - | The same keys without the `com_` prefix describe the in-base costume (`hair` is warned about and ignored). | [L233](../../src/Server/server_savegame.cpp#L233) |
| `money` | float truncated to int | 1968 | Credits. | [L557](../../src/Server/server_savegame.cpp#L557) |
| `num_kills`, `num_misn_successes`, `num_misn_failures` | float truncated to int | 0 | Statistics. | [L559](../../src/Server/server_savegame.cpp#L559) |
| `pos` | float x3 | 0, 0, 0 | Position in space. | [L565](../../src/Server/server_savegame.cpp#L565) |
| `rotate` | float x3, degrees | identity | Euler angles. | [L569](../../src/Server/server_savegame.cpp#L569) |
| `starting_ring` | id (whole value) | 0 | Trade-lane ring the player starts in. | [L576](../../src/Server/server_savegame.cpp#L576) |
| `equip`, `base_equip` | `[0]` item, optional `[1]` hardpoint, optional `[2]` health | - | Mounted equipment in space / at the base (repeat); same line format as a loadout `equip` ([goods-loadouts-constants.md](goods-loadouts-constants.md#loadout-files-loadout)). | [L578](../../src/Server/server_savegame.cpp#L578), [L580](../../src/Server/server_savegame.cpp#L580) |
| `cargo`, `base_cargo` | `[0]` item, `[1]` count, `[2]` hardpoint, `[3]` health, `[4]` mission flag | - | Cargo in space / at the base (repeat); same format as a loadout `cargo`. | [L582](../../src/Server/server_savegame.cpp#L582), [L584](../../src/Server/server_savegame.cpp#L584) |
| `collision_group`, `base_collision_group` | `[0]` group `obj` name, `[1]` float health | - | Damage state of one ship collision group (repeat). Before `ship_archetype`, or for a name the ship does not have, a warning is logged and the line is skipped. | [L586](../../src/Server/server_savegame.cpp#L586), [L588](../../src/Server/server_savegame.cpp#L588) |
| `wg` | raw line | - | Weapon-group text; each line is appended with a newline (repeat). | [L590](../../src/Server/server_savegame.cpp#L590) |
| `hull_status`, `base_hull_status` | float | not reset | Hull state in space / at the base. | [L594](../../src/Server/server_savegame.cpp#L594) |
| `visit` | int `[0]` id, int `[1]` flags (8 bit) | - | Visited system/base/hole entry (repeat). | [L598](../../src/Server/server_savegame.cpp#L598) |
| `interface` | int | 3 | Interface-state bits. | [L604](../../src/Server/server_savegame.cpp#L604) |
| `log` | `[0]` mad-lib id, `[1]` count, then count x (data, tag, flags) | - | One mission-log entry (repeat). A tag of 0x21 starts a nested entry whose parameters follow in the same line. | [L606](../../src/Server/server_savegame.cpp#L606) |
| `logid` | - | - | Tested and ignored. | [L611](../../src/Server/server_savegame.cpp#L611) |

`PlayerDB::ReadCharacterName` ([server_classes.cpp:3669](../../src/Server/server_classes.cpp#L3669)) reads only `name` (same hex rule, same `/*` ... `*/` comment skipping) of the first `[Player]` section to build the character list; a file that cannot be loaded gives an empty name. After a load the selected character's reader is also given to the controllers as the `PlayerLoad` message.

### New-character templates  ([PlayerData_CreateNewCharacter, server_savegame.cpp:1082](../../src/Server/server_savegame.cpp#L1082))

`NewCharacter.fl` / `MPNewCharacter.fl` is copied section by section into the new character's file: every header and every `key = value` line is rewritten, with parameters re-joined by `, ` and substitutions applied. Placeholders are matched case-insensitively against the **whole parameter**; the ones the code knows are:

| Placeholder | Where | Replaced by |
|---|---|---|
| key `%%Package%%` | as a key name | The chosen package's `ship_archetype` line followed by an `equip = <id>, <hardpoint>` / `cargo = <id>, <count>` line for every item of the package's loadout (items whose archetype does not exist are skipped). |
| `%%Name%%` | value | The character name in hex form. |
| `%%Faction%%` | value | The faction's reputation group. |
| `%%Money%%` | value | The package's starting money. |
| `%%Base_Costume%%`, `%%Comm_Costume%%`, `%%Voice%%` | value | The chosen pilot's body costume, comm costume and voice. |
| `%%Home_System%%`, `%%Home_Base%%` | value | The nickname of the home base's system / the home base. The placeholder text is left unchanged if the base or system is not found. |

The faction/package/pilot/base data comes from the character database listed as `[Data] NewCharDB` ([data-driven-files.md](data-driven-files.md)). If any of the four choices is missing, no file is created; if the template cannot be opened or is a `FLSG` file an empty file is written. A key with no values reprints the previous key's joined value (retail behaviour).

`NewPlayer.fl` is the template of `Restart.fl`: `PlayerDB::create_restart_file` copies it to `Restart.fl` and appends the whole text of the initial-world file ([server_classes.cpp:3506](../../src/Server/server_classes.cpp#L3506)).
