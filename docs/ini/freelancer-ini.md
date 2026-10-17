# freelancer.ini

`freelancer.ini` is the master configuration file of the game. It names the data folder, the resource DLLs, the server and list-server settings, the error-log channels and, in its `[Data]` section, every other INI file that the client and the server load at start-up.

**File location / how it is found.** The client opens `Freelancer.ini` in its current working directory (the name is compiled in; no command-line switch changes it). The dedicated server (`FLServer.exe`) opens `freelancer.ini` in the directory of its executable and, additionally, a file called `SrvLocal-freelancer.ini` beside it, which goes through the same reader. In single-player the server runs inside the client process and is handed the client's path, so one file serves both. The file is text and is read with `INI_Reader` (see [parsers.md](parsers.md): case-insensitive names, `;` comments, repeated keys delivered in order). The dev copy used by this repository is `bin\freelancer.ini`; the Debug and ASan builds copy it into their own output folder.

## Which code reads which section

| Section | Read by | Notes |
|---|---|---|
| `[Freelancer]` | client `LoadStartupConfig`, client `CLoginMgr::LoadConfig`, server `CFLServerApp::ReadFreelancerIni`, `Server_ReadWorldIni` | one key each, see below |
| `[Startup]` | client `LoadStartupConfig` | intro movies |
| `[ListServer]` | client `CLoginMgr::LoadConfig`, server `ReadFreelancerIni` | list-server (GUN) address |
| `[Login]` | client `CLoginMgr::LoadConfig` | optional auto-login values |
| `[Server]` | server `ReadFreelancerIni` | read and discarded |
| `[Initial SP DLLs]` / `[Initial MP DLLs]` | server `Server_LoadInitialDlls` | controllers to start |
| `[Resources]` | client `LoadClientIniData`, server `ReadFreelancerIni` | resource DLLs |
| `[Data]` | `DataConfig::startup` (both sides), client `LoadClientIniData` and `LoadStartupConfig` | all other INI files |
| `[Error]` | client `ErrorHandler::activate` | log channels |

Sections not listed here (for example the commented-out `[;Display]` block of the retail file) are not read from `freelancer.ini`. Display options come from `PerfOptions.ini` (see [client-ui-config.md](client-ui-config.md)).

## `[Freelancer]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `data path` | path (directory) | client `..\Data`; server: none (the server only sets up the data file system if the key exists) | Root folder of the game data. The whole parameter string is used, up to 259 characters on the client. The key is `Data path` in the server code, and names are case-insensitive. A relative value is interpreted by the OS against the process working directory. | [startup_config.cpp](../../src/freelancer/Client/startup_config.cpp), [FLServer.cpp](../../src/FLServer/FLServer.cpp) |
| `local_server` | string | `Server.dll` | DLL that implements the in-process (single-player) server; the dev file uses `rpclocal.dll`. Truncated to 31 characters. Multiplayer always loads `RemoteServer.dll` regardless of this key. | [login_mgr.cpp](../../src/freelancer/Client/login_mgr.cpp), [server_connect.cpp](../../src/freelancer/Client/server_connect.cpp) |
| `initial_world` | path (relative to Data path) | empty | World description file (normally `InitialWorld.ini`, see [saves-and-initial-world.md](saves-and-initial-world.md)). **First occurrence wins**; the reader stops scanning once found. Used at server start-up (multiplayer) and when a restart file is created. | [IServerImpl.cpp](../../src/Server/IServerImpl.cpp) |
| `AppGUID` | string `{GUID}` | none | Application GUID for the list-server protocol, parsed with `ParseGUID`. Applied only if the key is present. | [login_mgr.cpp](../../src/freelancer/Client/login_mgr.cpp), [FLServer.cpp](../../src/FLServer/FLServer.cpp) |

## `[Startup]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `movie_file` | path (repeated) | none | Intro movies, played in order at engine start. Only the **first three** occurrences are kept (counted over the whole file); the rest are ignored. The path is joined as `<data path>\<value>`. | [startup_config.cpp](../../src/freelancer/Client/startup_config.cpp), [engine_init.cpp](../../src/freelancer/Client/engine_init.cpp) |

## `[ListServer]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `hostname` | string | client `FLListServer2`; server `localhost` | Host name or IP of the list server ("GUN") that multiplayer servers report to and clients query. Not validated. | [login_mgr.cpp](../../src/freelancer/Client/login_mgr.cpp), [ServerControl.cpp](../../src/FLServer/ServerControl.cpp) |
| `port` | int | client `28810` (0x708a); server `39304` (0x9988) | List-server port. Read as a float and truncated to an integer; not range-checked. | same |

## `[Login]`

Optional; absent from the shipped files.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `name` | wide string | `SinglePlayer` | Account name for the login manager. | [login_mgr.cpp](../../src/freelancer/Client/login_mgr.cpp) |
| `pwd` | wide string | empty | Account password, stored as plain text in the file. | same |
| `char` | wide string | empty | Character name to select. | same |

## `[Server]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `death_penalty` | int | n/a | The server reader tests for this key and **discards it** (retail does the same). The shipped comment describes it as a percentage of cargo lost on death in multiplayer, but nothing in the code consumes the value. | [FLServer.cpp](../../src/FLServer/FLServer.cpp) |
| `name`, `description` | string | n/a | Commented out in the dev file; not read from this file. | n/a |

## `[Initial SP DLLs]` and `[Initial MP DLLs]`

The server reads the section that matches the current mode (single-player or multiplayer); the other is skipped. Each `DLL` line creates one *controller*: a plug-in object that receives server messages. All the shipped entries point into `Content.dll`.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `path` | path (directory) | empty | Directory prepended to relative DLL names (`.` in the dev file). Kept for the rest of the session. | [IServerImpl.cpp](../../src/Server/IServerImpl.cpp), [server_pub.cpp](../../src/Server/server_pub.cpp) |
| `DLL` | `[dll file, instance name, priority]` (repeated) | none | One controller per line: the DLL to load, the name passed to its `CreateInstance`, and its priority. `Content.dll` recognises 23 instance names; the shipped `StoryLauncher`, `GameSupport`, `SpaceSupport`, `BaseSupport`, `SpacePop` and `BasePop` are among them, while `AISandbox` and `TestAutomation` are not, so those two lines create nothing (the half-created controller is removed and no error is raised). | same |
| priority (third parameter of `DLL`) | enum | previous line's value, initially `HIGHEST` | `HIGHEST`=0, `ABOVE_NORMAL`=1, `NORMAL`=2, `BELOW_NORMAL`=3, `LOWEST`=4, compared case-insensitively. An unknown or empty word keeps the priority of the **previous** `DLL` line. Controllers are kept sorted by this number, lowest first. | same |

## `[Resources]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `DLL` | string (repeated) | none | Resource DLL loaded with `LoadLibrary` (string tables, infocards). The order defines the resource-module index the game uses to look up string IDs, so it must not be changed casually. On the client a failed load logs an error but still reserves its index; the server registers the returned handle directly. | [dataconfig_progress.cpp](../../src/freelancer/Client/dataconfig_progress.cpp), [FLServer.cpp](../../src/FLServer/FLServer.cpp) |

## `[Error]`

Configures the log channels of the error handler (`FDUMP` output). Read once by the client at start-up.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `log` | `[where, kind, severity]` (repeated) | none | One output channel per line. **where**: `$Window` (message box), `$Text` (debugger output), or a file name (the file is created/truncated for each run). **kind**: number or char literal such as `'c'`, default `-1` = all kinds. **severity**: default `100`; messages whose severity number is greater than this limit are dropped (lower number = more severe; the shipped comment lists 0 fatal, 1 error, 2 warning, 3 notice, 4+ trace). Kind and severity are parsed as numbers, so `65540`, `'I'` and `0x10004` are all accepted. | [common_errorhandler.cpp](../../src/Common/common_errorhandler.cpp), [main.cpp](../../src/freelancer/Client/main.cpp) |

After this section the client also opens a fixed channel `FLSpew.txt` in the per-user local data folder (kind -1, severity 7); it is not configured by the INI file. Each channel keeps only the **first 127 characters** of its `where` string. The one-letter kinds in the shipped comment (`R` render, `n` network, `w` warning, `i` instance creation, `r` reputation, `a` archetypes, `c` controllers, `m` missions, `s` second thread, `f` file I/O, `e` explosion/fuse, `d` damage, `A` reload console) come from that comment and from the call sites that raise them; the reader does not check them.

## `[Data]`

Every key of `[Data]` names a file (or, for `bases`, a loading step). The values are file names relative to the Data path. **The order of the lines is the load order**, and several loaders depend on it: archetype ID tables are shared between client and server and must stay in the same order on both, `solar` must come before `universe`, and `effects = fx\effect_types.ini` and `beam_effects.ini` must come before the other effect files. Key names are case-insensitive. Keys that neither dispatcher knows are silently ignored.

Loading happens in two passes over the same section:

1. `DataConfig::startup` ([common_dataconfig.cpp](../../src/Common/common_dataconfig.cpp)) runs on **both** client and server. It handles the keys marked *both* below. It is reference-counted: a second call only increments the counter and does not re-read the file. After the loop it also loads the binary `AI\state_graph.db` from the Data folder (not an INI file). On the client a progress callback sees every value first and can abort the load.
2. The client then runs `LoadClientIniData` ([dataconfig_progress.cpp](../../src/freelancer/Client/dataconfig_progress.cpp)) for the keys marked *client*. `gate_tunnels` and `jump_effect` are captured earlier by `LoadStartupConfig` and loaded by their own modules later.

| Key | Count in dev file | Side | What the file holds | Loader | Documented in |
|---|---|---|---|---|---|
| `solar` | 1 | both | Solar-object archetypes (planets, stations, gates, ...) | `Archetype::LoadSolar` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `universe` | 1 | both | `universe.ini`: the list of star systems and bases and the files that describe them | `Universe::Startup` | [world-data.md](world-data.md) |
| `constants` | 1 | both | Global gameplay constants | `ReadConstants` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `equipment` | 7 | both | Equipment archetypes (lights, engines, weapons, shields, tractors and so on); every file may contain any equipment section kind. A file that yields no new definitions logs a warning. | `Archetype::LoadEquipment` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `ships` | 2 | both | Ship archetypes (`shiparch.ini`, `rtc_shiparch.ini`) | `Archetype::LoadShips` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `asteroids` | 1 | both | Asteroid archetypes (`solar\asteroidarch.ini`) | `Archetype::LoadAsteroids` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `explosions` | 1 | both | Explosion and debris archetypes (`fx\explosions.ini`) | `Archetype::LoadSimples` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `loadouts` | 4 | both | Default ship and solar loadouts | `Loadout::Load` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `bodyparts` | 1 | both | Character body-part definitions | `LoadBodypartDescriptions` | [world-data.md](world-data.md) |
| `costumes` | 1 | both | Character costume definitions | `LoadCostumeDescriptions` | [world-data.md](world-data.md) |
| `bases` | 1, **no value** | both | Not a file: the key's position sets *when* the base files named by `universe.ini` are loaded. | `BaseDataList_load` | [world-data.md](world-data.md) |
| `goods` | 5 | both | Trade goods and prices (commodities, equipment, ships) | `GoodList_load` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `markets` | 3 | both | What each base sells (misc, ships, commodities) | `BaseDataList_load_market_data` | [world-data.md](world-data.md) |
| `PetalDB` | 1 | both | Petal (interface) database | `PetalDB_load` | [world-data.md](world-data.md) |
| `Groups` | 1 | both | Faction groups and their reputation matrix: points at `InitialWorld.ini` (the same file as `initial_world`) | `Reputation::Startup` | [saves-and-initial-world.md](saves-and-initial-world.md) |
| `fuses` | 16 | both | Fuse definitions (scripted effect sequences); queued here, parsed later by the server and client fuse databases | `FuseDB::AddFuseINIFile` | [world-data.md](world-data.md) |
| `WeaponModDB` | 1 | both | Weapon-modifier table | `WeaponModDB_load` | [world-data.md](world-data.md) |
| `NewCharDB` | 1 | both | New-character table: factions, packages and pilots offered at character creation | `NewChar::Database::Load` | [world-data.md](world-data.md) |
| `RtcSlider` | 1 (spelled `rtcslider`) | both | Slider table for real-time-cutscene scripts | `RtcSlider::load` | [world-data.md](world-data.md) |
| `fonts` | 1 | client | Font definitions | `LoadFonts` | [client-ui-config.md](client-ui-config.md) |
| `rich_fonts` | 1 | client | Rich-text font definitions | `LoadRichFontsFromIni` | [client-ui-config.md](client-ui-config.md) |
| `sounds` | 7 | client | Sound archetype tables (ambience, sfx, engines, interface, music, story, gameflow) | `LoadClientSoundArchIni` | [client-audio-misc.md](client-audio-misc.md) |
| `voices` | 18 | client | Voice profiles and dialogue line tables | `LoadClientVoicesIni` | [client-audio-misc.md](client-audio-misc.md) |
| `effects` | 12 | client | Visual-effect type tables and effect libraries; `effect_types.ini` and `beam_effects.ini` first | `EffectArchLoadIni` | [client-world-fx.md](client-world-fx.md) |
| `effect_shapes` | 1 | client | Texture-shape table for effects; queued and parsed at the end of engine start-up | `AddEffectShapesFile` | [client-audio-misc.md](client-audio-misc.md) |
| `debris` | 1 | client | Debris archetypes: normally the same file as `explosions` | `LoadDebrisArchFile` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `intro` | 1 | client | Intro screen configuration (`[Images]` section) | `LoadIntroConfigFromIni` | [client-ui-config.md](client-ui-config.md) |
| `HUD` | 1 | client | HUD element list; the path is stored and the elements are built later | `SetHudIniPath` | [client-ui-config.md](client-ui-config.md) |
| `igraph` | 1 | client | Named keyframe curves (`[IGraph]` sections) that light sources refer to by nickname | `GraphLibraryLoad` | [client-audio-misc.md](client-audio-misc.md) |
| `gate_tunnels` | 1 | client | Jump-gate tunnel effect parameters | `GateTunnelIniLoad` | [client-world-fx.md](client-world-fx.md) |
| `jump_effect` | 1 | client | Jump effect parameters | `JumpEffectIniLoad` | [client-world-fx.md](client-world-fx.md) |

The client passes an object factory to `DataConfig::startup`, so the archetype files marked *both* produce client-side subclasses with extra per-record keys (read through `ReadUnknown`); the server passes none. See [archetypes-and-equipment.md](archetypes-and-equipment.md).

### Keys in the retail file that the port ignores

The retail `freelancer.ini` carries a few more `[Data]` lines that the port's dispatchers do not know and therefore skip: `fonts_dir` (directory for in-process font files), `concave` (`concave.ini`) and `stars` (`solar\StarArch.ini`). The star-arch file is nevertheless loaded, from a **fixed name** (`solar\StarArch.ini`) in the space-scene code rather than through the `stars` key. After the `[Data]` pass the client also loads the fixed-name `audio\SoundCFG.ini` (see [client-audio-misc.md](client-audio-misc.md)).

## Example

The dev copy `bin\freelancer.ini`, shortened (`...` marks omitted lines):

```ini
[Freelancer]
data path = ..\DATA
local_server = rpclocal.dll
initial_world = InitialWorld.ini      ;relative to Data path
AppGUID = {A690F026-26F0-4e57-ACA0-ECF868E48D21}

[Startup]
movie_file = movies\MGS_Logo_Final.wmv
...
[ListServer]
hostname = gun.fllistserver.com
port = 2300

[Initial SP DLLs]
path = .
DLL = Content.dll, GameSupport, HIGHEST
DLL = Content.dll, SpaceSupport, NORMAL
...
DLL = Content.dll, BasePop, LOWEST

[Resources]
DLL = InfoCards.dll
...
[Data]
       solar = solar\solararch.ini
    universe = universe\universe.ini
   constants = constants.ini
...
       bases                        ; no file name, the key sets the load order
     markets = equipment\market_misc.ini
...
[Error]
log = freelancer_messages.log,-1
log = $Window,65540
log = $Text, 65536, 5
log = $Text, 'c'
```

Differences between the dev copy and the retail file: the dev copy uses `path = .` in the two DLL sections (retail `..\dlls\bin`), points `hostname` at a community list server, and leaves out the commented `[;Display]` block, `fonts_dir`, `concave`, `stars` and the commented milestone-snapshot lines. The `[Error]` section of the dev copy adds a first `log` line that writes every message to `freelancer_messages.log`.

## See also

* [parsers.md](parsers.md) for the reader grammar and path resolution.
* [server.md](server.md) for the server's view of this file, including `SrvLocal-freelancer.ini` and the start-up sequence.
* [client-ui-config.md](client-ui-config.md) for the client's own keys of the same file.
* [world-data.md](world-data.md) for the `[Data]` dispatcher and the `[Error]` routing as implemented in Common.
* [engine-dacom.md](engine-dacom.md) for `DACOM.INI`, which is a different file with a different parser.
