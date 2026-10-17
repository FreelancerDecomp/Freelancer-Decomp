# INI files that are found through other INI files

Most INI files of the game are not listed anywhere in code or in `freelancer.ini`: their names are written *inside other data files*, and the reader opens whatever name it finds. This page lists those files, the key that names them, and the code that opens them. The keys and sections inside each file are described in the page that owns the subject.

**How they are found.** The referencing key holds a path. It is resolved relative to the Data folder (see "How file paths are found" in [parsers.md](parsers.md)) unless the table says otherwise; the one exception is the system files named in `universe.ini`, which are relative to the folder of `universe.ini`. Nothing validates that the target exists beyond the reader's own `open` result: a file that cannot be opened is skipped silently (a few readers log an error). Files whose names are literals in the source are *not* in this table; they are described with their subject (see [freelancer-ini.md](freelancer-ini.md) for the `[Data]` list, and the sibling pages for fixed-name files).

## Data-driven references

| File(s) | Named by | Key | Opened by |
|---|---|---|---|
| System files `Systems\<Sys>\<Sys>.ini` | `universe.ini`, each `[System]` section | `file` (relative to the folder of `universe.ini`) | `Universe` loader in [common_universe.cpp](../../src/Common/common_universe.cpp) (`LoadSystemFile`, `LoadSystemObjectPool`); the server re-reads them in [server_classes.cpp](../../src/Server/server_classes.cpp) (`StarSystem::load`) and [server_pub.cpp](../../src/Server/server_pub.cpp); Content in [SystemInfo.cpp](../../src/Content/SystemInfo.cpp) (`FSystemInfo::LoadFromReader`); the client in [star_sys.cpp](../../src/freelancer/Client/star_sys.cpp) (`StarSysLoadIni`) |
| Base files `Universe\Systems\<Sys>\Bases\<base>.ini` | `universe.ini`, each `[Base]` section | `file` | `BaseData::read_from_ini` in [common_basedata.cpp](../../src/Common/common_basedata.cpp); server copy in [server_pub.cpp](../../src/Server/server_pub.cpp) |
| Room files `...\Bases\Rooms\*.ini` | a base file, each `[Room]` section (with `nickname`) | `file` | `RoomData::read_from_ini` in [common_roomdata.cpp](../../src/Common/common_roomdata.cpp). A `[Room]` needs both `nickname` and `file`, otherwise it is dropped; other keys in the block log "Invalid ini data". |
| Asteroid-field files `solar\asteroids\*.ini` | a system file, each `[Asteroids]` section (the key `file`, with `zone` naming the zone it fills) | `file` | server `SrvAsteroidSystem::load` / `FSrvAsteroidField::load` in [server_classes.cpp](../../src/Server/server_classes.cpp); client `CliAsteroidLoadFieldIni` in [cli_asteroid.cpp](../../src/freelancer/Client/cli_asteroid.cpp); Common `CAsteroidField::load` in [common_casteroidfield.cpp](../../src/Common/common_casteroidfield.cpp) |
| Nebula files `solar\nebula\*.ini` | a system file, each `[Nebula]` section (each `file` creates one nebula; `zone` names its zone) | `file` | `FNebulaEffect::Load` in [cli_nebula.cpp](../../src/freelancer/Client/cli_nebula.cpp) (client only) |
| Shape / texture-panel tables (for example `universe\heavens\shapes.ini`, `solar\asteroids\*_shapes.ini`) | a system file, an asteroid-field file or a nebula file, `[TexturePanels]` section (repeated) | `file` | `FTexturePanels::Load` in [texture_panels.cpp](../../src/freelancer/Client/texture_panels.cpp), which feeds `FShapeDB::Load` in [texture_shapes.cpp](../../src/freelancer/Client/texture_shapes.cpp); the system and field/nebula readers call `ShapeDB::Load` directly with `GetFullFilename` |
| Ring texture panels `solar\rings\*.ini` | a system file, an `[Object]` section | `ring = <zone>, <file>` (two parameters) | `FClientSolarGameobj::ReadIni` in [client_gameobj.cpp](../../src/freelancer/Client/client_gameobj.cpp) |
| Cockpit files `cockpits\<nation>\*.ini` | `ships\shiparch.ini`, `[Ship]` | `cockpit` | the client stores the name in [client_cliobj.cpp](../../src/freelancer/Client/client_cliobj.cpp) (`FCliShip::ReadUnknown`); [cockpit_load.cpp](../../src/freelancer/Client/cockpit_load.cpp) resolves it against the Data path when the player's ship is created and hands it to the four camera loaders. A ship without the key logs a warning and uses `cockpits\cockpit.ini`. |
| Encounter-parameter files `missions\encounters\*.ini` | a system file, each `[EncounterParameters]` section | two consecutive lines, `nickname` (encounter ID) then `filename` | `FSystemInfo::LoadFromReader` in [SystemInfo.cpp](../../src/Content/SystemInfo.cpp) via `OpenContentDataIni`, which constructs `FEncounterParameters` ([EncounterParams.cpp](../../src/Content/EncounterParams.cpp)). The two lines are read **positionally**; the key names are not checked. They are loaded only by the full system load (`FSystemInfo::LoadFull`), not by the lighter `Load(false)` used for path queries. |
| NPC ship tables `missions\m##\npcships.ini` | a mission file, `[Mission]` | `npc_ship_file` | `LoadMissionHeaderValues` in [MissionObject.cpp](../../src/Content/MissionObject.cpp) via `OpenContentDataIni`, then `LoadNpcShipArchs` |
| Real-time-cutscene ("RTC") scripts `missions\m##\*_nrml.ini`, `*_offer.ini`, `*_reoffer.ini` | a mission file, trigger lines | `Act_AddRTC = <path>[, repeatable]`, `Act_RemoveRTC = <path>`, `Cnd_RTCDone = <path>` | `FCharacterEncounter::Load` in [RtcScripts.cpp](../../src/Content/RtcScripts.cpp), started from [MissionScript.cpp](../../src/Content/MissionScript.cpp) (`StartRtcScript`); a script is also re-created from the `rtc` lines of a saved `[MissionState]` |
| Story mission files (for example `Missions\M01a\m01a.ini`) and generic missions | the story controllers, or for a generic mission the creation parameters (a save records them as the second value of `Mission` in `[StoryInfo]`) | path passed to the mission constructor | `FMissionScript::LoadMissionIni` in [MissionScript.cpp](../../src/Content/MissionScript.cpp), see [missions.md](missions.md) |
| HUD shape table `interface\HUD\hud_txm.ini` | `interface\hud.ini`, `[HUD]` | `shapes` | `LoadHudElementsFromIni` in [hud_element_list.cpp](../../src/freelancer/Client/hud_element_list.cpp), which feeds `FShapeDB::Load` |
| Retail-dev DACOM includes `FL_Dev.ini`, `FL_DevOnlyLibs.ini` | `DACOM.INI` / `dacomsrv.ini` | `@include <file>` | the DACOM parser, [dacom.cpp](../../src/dacom/dacom.cpp). A missing include is ignored silently. The dev `bin\dacomsrv.ini` still starts with `@include FL_Dev.ini`, a file that does not exist in `bin`. |

Each target file is read with the ordinary `INI_Reader` (text or BINI), except the DACOM includes. The files that every one of the rows above produces are listed here only by the key that names them; the layout of the files themselves is described in:

* system, base and room files: [world-data.md](world-data.md) (Common's view) and [systems-and-bases.md](systems-and-bases.md) (Content's view)
* encounter parameters: [ai-and-encounters.md](ai-and-encounters.md)
* mission files, NPC ship tables and RTC scripts: [missions.md](missions.md)
* asteroid fields, nebulae, rings: [client-world-fx.md](client-world-fx.md)
* shape and texture-panel tables: [client-audio-misc.md](client-audio-misc.md)
* cockpit files: [client-ui-config.md](client-ui-config.md)

## Fixed-name files that are generated at run time

Three tables are *written* by Content and read back by name: `Universe\systems_shortest_path.ini`, `Universe\shortest_legal_path.ini` and `Universe\shortest_illegal_path.ini`. Content writes them with a hard-coded relative path `..\data\Universe\...` (relative to the working directory, not to the configured data path) when it rebuilds its system-connection tables, and reads them with `OpenContentDataIni`. See [systems-and-bases.md](systems-and-bases.md).

## Files with no reader in the source

A scan of the retail data set found INI files for which no reader was located in `src`: `concave.ini` (in the Data root), `MISSIONS\factionsets.ini`, `MISSIONS\mshipprops.ini` and `INTERFACE\HUD\hudtarget.ini`. Their names do not occur as literals in the source and no data file names them. This only shows the port does not read them; it has not been checked whether the retail executable does (the retail `freelancer.ini` lists `concave.ini`, see [freelancer-ini.md](freelancer-ini.md)).

## What is not an INI reader

Several modules mention `.ini` in their sources only as part of other words (for example `.Init(`); none of them reads configuration:

* Engine and support libraries without any INI access: `ximage`, `x86math`, `ReadFile`, `SoundStreamer`, `flmaterials`, `Thorn`, `IVP`, `qhull`, `zlib`, `imeui`, `movie`, `Editor`, `MemoryDiagnostics`.
* Networking: `RemoteClient`, `RemoteServer`, `RemoteCommon` and `rpclocal` take their settings from the `SStartupInfo` structure the client or server fills in, not from files.
* `luaprofile.dll` implements the `IProfileParser` interface over Lua globals; it does not parse INI text.
* The Win32 profile functions (`GetPrivateProfileString` and relatives) are not used anywhere in the project.
* `bin\flconfigdatabase.txt` (RP8 hardware-quirk list) and the generated lists in `src\Server\retail_exports.txt` are not INI files.

Modules that read `DACOM.INI` through the DACOM parser rather than `INI_Reader` (`dacom`, `EngBase`, `RendComp`, `RP8`, `Shading`, `SoundManager`, `system`, `alchemy`, `Deformable2`) are covered in [engine-dacom.md](engine-dacom.md).
