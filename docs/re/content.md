# Content.dll — campaign and world game logic

`Content.dll` contains the game logic on top of `Server.dll`: story missions and scripts, random
missions, space and base population, NPC radio chatter (GCS), pilot behaviour, base data
(`mbases.ini`), player state. It is not a DACOM component but a set of Server "controllers"; this document
is for whoever ports or edits `src/Content`. Line-by-line facts and statuses are in decomp-tracker
(module `Content.dll`, image `0x06ea0000`; the listing lives in `DLLS/BIN/Content.dll.{txt,c}`, not in
`EXE/Disasm/`).

Mission script formats: [content_mission_script.md](content_mission_script.md) (state
machine, `[Trigger]`) and [content_mission_opcodes.md](content_mission_opcodes.md) (arguments of
`Cnd_*`/`Act_*`).

## How the module is loaded

- There is no `RegisterComponent`, no `"1.11_…"` IID strings, no `FDUMP`. Only `stricmp` and `DACOM_Acquire`
  (creation of `"3DMathEngine"`) are imported from DACOM. Exports (identical to retail):
  `CreateInstance`, `DestroyInstance`, class `IDLL` (constructors, destructor, `operator=`, vftable).
- It is loaded by **Server.dll**: `IServerImpl::Startup` reads `freelancer.ini` `[Initial SP DLLs]` /
  `[Initial MP DLLs]` (lines `DLL = <file>, <name>, <priority>`) and for each calls
  `pub::Controller::Create(file, name, nullptr, priority)` → `LoadLibrary` →
  `GetProcAddress("CreateInstance")` → `CreateInstance(controllerId, name, parms)`. Content itself calls
  `Create("Content.dll", …)` for `Story` and the missions (the module path comes from `GetModuleFileNameA`).
- `CreateInstance` is a `stricmp` chain over **23 names**; an unknown name yields `0`, and Server does
  `DestroyInstance(0)` + `FreeLibrary`. This is how `AISandbox` and `TestAutomation` from the retail
  `freelancer.ini` behave: these names exist in no retail module (dead lines), there is nothing to implement.
- A controller object inherits `IDLL`: slot 0 is `HandleMessage(msg, payload)` (pure virtual in the
  base), slot 1 is the destructor (`DestroyInstance` calls it with `1`). `controllerId` is a 30-bit counter.
- The priority determines the order in the controller queue: `HIGHEST=0 … LOWEST=4`. The tick rate is set by
  `pub::Controller::SetHeartbeatInterval` (seconds): the tick message is `0x20`; an interval `< 0`
  (default `-1`) disables the tick, an interval `> 0` accumulates `dt` and sends `0x20` with
  `float* dt` in the payload.

## Controllers

| Name | SP/MP | Priority (ini) | Heartbeat | Role |
|---|---|---|---|---|
| `GameSupport` | both | HIGHEST | 1.0 s | Loads all data tables (see below), player state, statistics, save/load of `[mPlayer]`, system changes, best paths |
| `SpaceSupport` | both | NORMAL | 1.0 s | Gates, docks, docking queues, ship creation/outfitting, tradelane, combat reputation |
| `BaseSupport` | both | NORMAL | none | Bases: GCS scripts of bartenders/traders (`scripts\gcs\genericScripts.ini`), news, GF object selection |
| `SpacePop` | both | LOWEST | 3.0 s | Space populator (encounters) |
| `BasePop` | both | LOWEST | none | Base populator (room NPCs) |
| `StoryLauncher` | SP only | BELOW_NORMAL | none | Reads `missions\specific_npc.ini`; on message `7` creates `Story` |
| `Story` | SP | (created by code, NORMAL) | 5.0 s | Campaign: `[StoryInfo]`, chapter number, launches `Mission_*` |
| `Mission_01a`, `Mission_01b`, `Mission_02` … `Mission_13` | SP | — | set by the constructor (0.033 s for `01a`) | Story missions: each has its own class, constructor and object size (0x440 for 01a, 0x330 for 01b, 0x32C for the rest) |
| `GenericMission` | SP | — | — | Story mission from an arbitrary INI (path is `CreateParms+0xC`) |
| `DestroyMission` | both | — | 1.0 s | Random "destroy" mission (offer at a base; 0x29C bytes) |

Besides those listed, `[Initial SP DLLs]` / `[Initial MP DLLs]` in retail also contain the dead
`AISandbox` and `TestAutomation` (see above); `Story`, `Mission_*`, `GenericMission` and `DestroyMission`
are not in the lists and are created only on demand. The literal `"Mission_01a"` occurs in
Content once — in `CreateInstance`: the first `Mission = Mission_01a` comes from the
`NewPlayer.fl` template (see below).

Besides slot 0, Content hands Server/Common descendants of their classes — virtual calls go
**from** Common/Server into Content: `StateMachine`/`MessageHandler`/`SMEventData` (random missions,
GCS), `CommReferrable`, `pub::AI::BaseOp` and `BaseHint` (directives and guides), `pub::AI::Personality`,
`pub::Reputation::Enumerator`, `pub::System::ZoneEnumerator`. Hence "no callers inside
Content" proves nothing.

## Server → Content messages

Broadcast: `controllerId & 0x3FFFFFFF == 0` — to all loaded controllers.
Numbers from Server (`IServerImpl::*`), where the source is known:

| msg | Event | Handled by |
|---:|---|---|
| 0 | player equipment | GameSupport |
| 1 / 2 | `BaseEnter` / `BaseExit` | GameSupport, BaseSupport |
| 4 / 5 | save `[mPlayer]` to `FILE*` / load from `INI_Reader*` (payload `{playerId, FILE*|INI_Reader*}`) | GameSupport, Story, missions |
| 6 | `PlayerLaunch` | missions |
| 7 | player login (`Login`, character change, MP logout) | StoryLauncher |
| 8 | MP logout | GameSupport |
| 0xB | `CommComplete` | SpaceSupport |
| 0x16 / 0x17 | tradelane entry | GameSupport, SpacePop (0x16) |
| 0x1B | `GFObjSelect` | BaseSupport |
| 0x20 | heartbeat (`float dt`) | all with an interval `> 0` |
| 0x25 | gate/jump assignment | GameSupport, SpaceSupport |
| 0x29 / 0x2A | `LocationEnter` / `LocationExit` (payload `{locationId, playerId}`) | GameSupport, BaseSupport, BasePop |
| 0x31 | `RequestBestPath` | GameSupport |
| 0x32 / 0x36 | terminal/dock; tail of `PlayerLaunch` | SpaceSupport, SpacePop (0x36) |
| 0x33 | `RequestPlayerStats` | GameSupport |
| 0x37 | zeroing of `Players[id]` | GameSupport |
| 0x39 | object creation (ship/equipment) | GameSupport, SpaceSupport |
| 0x3A / 0x3B | system change | GameSupport; 0x3B — SpacePop |
| 0x40 / 0x41 | save load/store after `Reputation` | GameSupport |

Messages `0x1000` and above are Content's internal messages between controllers (`_SendMessage` to a
specific id): `0x1000` activates a mission's initial triggers, `0x1001` is the combat state
(`CSNType`), `0x100E` is "ship in sights", `0x1010`, `0x100D`, etc. Some messages are handled by none of
the controllers.

## Story and campaign

- `StoryLauncher` reads `[NPC]` from `missions\specific_npc.ini` (named story NPCs) and on
  message `7` calls `pub::Controller::Create(<Content.dll path>, "Story", {playerId}, NORMAL)`.
  There is no `StoryLauncher` in MP.
- `Story` stores the mission name (default `"No_Mission"`), the chapter number `MissionNum` (0…42),
  `delta_worth` (the wealth required for the next chapter), `ship_bought`. Save format:

  ```
  [StoryInfo]
  Mission = <Mission_NN | GenericMission [path]>
  MissionNum = <n>
  delta_worth = <number>
  ship_bought = <0|1>
  ```

  Load (message 5): creates a controller from the `Mission` field and sends it messages `5`, `0x1000`.
- The first mission of a new SP save: the template `EXE\NewPlayer.fl` (encrypted `FLS1`) contains
  `[StoryInfo] Mission = Mission_01a, MissionNum = 1, delta_worth = -1`; `Server.dll`
  (`PlayerDB::create_restart_file`) copies it into the new save. `InitialWorld.ini` has nothing to do
  with the story.
- Automatic launch of the next chapter (`Story` heartbeat): when `delta_worth ≤ wealth` and with no
  live mission, the numbers 5, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39 launch respectively
  `Mission_02` … `Mission_13`. The remaining transitions are made by the `Story` message handler from the
  mission completion code (`01a`→`01b` immediately; `M06→M07` and `M09→M13` partly immediately, the rest wait for
  money); `MissionNum = 42` means the campaign is over.

Details of the mission state machine: [content_mission_script.md](content_mission_script.md).

## Random missions and populators

- `DestroyMission` is created by the base controller when an offer is accepted (the controller name is taken from the
  offer object) or from the `[RandomMission]` section of a save; the type name is taken from `mbases.ini`
  (`mission_type = DestroyMission`).
  It does not inherit the story trigger machine: the only common ancestor is the base mission controller
  (slot 0: destroy the controller by flag), after which come `StateMachineGroup`, the generator
  (a `DecisionNode`/`DataNode` tree, "vignettes"), runtime automata and encounter parameters
  (`SPParams`/`TLParams`).
- **SpacePop** handles only `0x16` (tradelane), `0x20` (heartbeat), `0x36` (player launch),
  `0x3B` (system change). The tick walks the live systems/zones; for each zone with loaded
  `EncounterParameters` that is not yet "saturated" it builds a formation: zone selection over three rings (10 m,
  2000 m, 4000 m) and three spawn passes. The "population enabled" flag defaults to `true`.
- Encounter data: in `universe\systems\<sys>\<sys>.ini` the section `[EncounterParameters]`
  (`nickname`, `filename = missions\encounters\*.ini`); the file contains `[EncounterFormation]`
  (`ship_by_class`, `pilot_job`, `formation_by_class`, `behavior`, `arrival`, `permutation`) and
  `[Creation]`. The names of ship/pilot/formation classes are resolved in the tables that
  GameSupport loads.
- **BasePop**, on message `0x29`, chooses the room's NPCs: from GF placement names like `zg\npc\m\a\…`
  and the faction pool in `mbases.ini` it assembles characters (roles `trader`, `shipdealer`, `equipment`,
  `bartender`), and on `0x2A` it destroys them.

## GameSupport data

On creation GameSupport reads (paths relative to `pub::GetDataPath()`): `audio\voices_space_*.ini`,
`audio\voices_recognizable.ini`, `missions\empathy.ini`, `voice_properties.ini`, `formations.ini`
(`[Formation]`), `pilots_population.ini`, `pilots_story.ini`, `npcships.ini`, `npcships_test.ini`
(always), `faction_prop.ini`, `equipment\commodities_per_faction.ini`, `shipclasses.ini`,
`ships\shiparch.ini`, `lootprops.ini`, `scripts\GCS\GCSExcl.ini`, `PTough.ini`,
`RankDiff.ini`, `news.ini`; `mbases.ini` (`[MBase]`, `[BaseFaction]`, bar NPCs, offers) is
a separate pass. Player save (`[mPlayer]`): `locked_gate`, `vnpc`, `rumor`, `can_dock`,
`dock_exception`, `can_tl`, `tlr_exception` and statistics; `[RandomMission]` recreates the controller;
`[locked_gates]` (`locked_gate`, `npc_locked_gate`) is written by SpaceSupport. `DynBaseInfo`/`DynSysInfo`
are read on load (`0x40`).

## AI contract: `pub::AI::ContentCallback`

Content does not import `IBehaviorManager::set_content_callback`: it is called by Server from
`pub::AI::SubmitState` (type 0: state graph, `DirectiveCallback`, `ContentCallback`, `Personality`).
Two Content AI classes inherit `ContentCallback` (ten slots each: `follow_event`,
`tether_object_event`, `lead_object_event`, `ship_in_sights_event`, `request_flee_destination`,
`information_request`, `scan_state_change`, `combat_state_notification`, `combat_drift_event`,
`get_creation_type`): the system `[Object]` object (overrides only
`combat_state_notification` → message `0x1001`) and the encounter ship (overrides nine slots;
`get_creation_type` reads an object field). The remaining slots are Common no-ops (`get_creation_type` = 3).
Directives are built by a factory by type (`Idle`, `Delay`, `Goto`, `Trail`, `Follow`, `Guide`, `Face`, `Dock`,
`InstantTradelane`, `Launch`, `WaitForPlayerManeuver`, `TractorBeam`, `Cancel`); pilot personalities
are read from `pilots_*.ini` (`Personality` and the nested `*UseStruct`/`*ReactionStruct`/`JobStruct`).

## Translation unit map

In retail Content is built from 65 `.obj` files; their boundaries (by the shared header of static IDs
`CreateID("any")`, `"house_fighters"`, …) became the "TU" numbers in source comments. The tail
`06fa9000`–`06fb2221` is CRT, `__ehhandler`s and EH funclets (their status follows the parent).

| TU | Addresses | Subsystem | `src/Content/` |
|---|---|---|---|
| 0–14 | `06ea1000`–`06ea5c10` | `GenericMission`, `Mission_01a` … `Mission_13` | `StoryMissions.cpp`, `ContentStaticIds.h` |
| 15, 16 | `06ea5c10`–`06ea7750` | `Story`, `StoryLauncher` | `Story.cpp`, `StoryLauncher.cpp` |
| 17, 22 | `06ea7750`–`06eb2670`, `06ed3d90`–`06ed4220` | `mbases.ini` base, GF_NPC records, ranks | `BaseDB.*`, `BaseNpcRecords.cpp` |
| 18, 52 | `06eb2670`–`06eb3ec0`, `06f5a2e0`–`06f5c760` | `BasePop`, `SpacePop` | `Populators.*` |
| 19 | `06eb3ec0`–`06eb6c00` | `BaseSupport`, news | `BaseSupport.*` |
| 20 | `06eb6c00`–`06eb7360` | space object base | `NpcBase.*` |
| 21 | `06eb7360`–`06ed3d90` | mission trigger engine (`[Trigger]`, `Cnd_*`/`Act_*`) | `MissionScript.*`, `MissionCnd.*`, `MissionAct.*` |
| 23 | `06ed4220`–`06ed5390` | `[CharacterEncounter]`, RTC scenes | `RtcScripts.*` |
| 24 | `06ed5390`–`06ed5a70` | solar NPCs (`[Object]`) | `NpcCreate.*` |
| 25 | `06ed5a70`–`06eeea30` | GCS (radio chatter) | `Gcs.*` |
| 26 | `06eeea30`–`06eef190` | `CreateInstance`/`DestroyInstance` | `Content.cpp` |
| 27 | `06eef190`–`06ef2070` | `DestroyMission` | `DestroyMission.cpp` |
| 28 | `06ef2070`–`06ef3630` | mission dialogs | `MissionDialogs.*` |
| 29, 30, 32 | `06ef3630`–`06efb350`, `06efe100`–`06efe8e0` | encounter parameters, `formations.ini` | `EncounterParams.*`, `Encounters.*` |
| 31, 59, 60, 62 | `06efb350`–`06efe100`, `06f802b0`–`06f84060`, `06f94d10`–`06f97360` | `SPParams`/`TLParams`, `SP_Attack`/`TL_Attack`, `KillableSolar` | `RandomEncounters.*` |
| 33 | `06efe8e0`–`06f00670` | `GameSupport` | `GameSupport.cpp` |
| 34 | `06f00670`–`06f03fa0` | GenericScripts, `GCS_Exclusions` | `GenericScripts.*` |
| 35 | `06f03fa0`–`06f056d0` | loot properties | `LootProps.*` |
| 36 | `06f056d0`–`06f05d00` | `StateMachine` message router | `ContentMessageRouter.*` |
| 37 | `06f05d00`–`06f0e000` | guides (`MB_DOCKGUIDE`, `goto`, `patrol_path`, …) | `Guides.*`, `GotoGuides.cpp`, `PatrolGuides.cpp` |
| 38 | `06f0e000`–`06f24080` | mission object: `HandleMessage`, `ParseMissionIni`, saving | `MissionObject.*`, `MissionSave.cpp` |
| 39 | `06f24080`–`06f24670` | data path (`pub::GetDataPath`) | `ContentDataPath.cpp` |
| 40 | `06f24670`–`06f28f60` | `[ObjList]` directives | `MissionObjList.*`, `ObjListDirective.h` |
| 41 | `06f28f60`–`06f31190` | system graph, jump paths | `SystemConnections.*` |
| 42, 43 | `06f31190`–`06f34ef0` | `npcships.ini`, NPC names, NPC ship | `NpcShips.*`, `NpcNames.*`, `NpcShip.*` |
| 44 | `06f34ef0`–`06f36f10` | cargo, faction goods | `Cargo.*` |
| 45 | `06f36f10`–`06f46940` | pilot personalities, patrol paths | `PilotPersonalities.*`, `PatrolPaths.*` |
| 46 | `06f46940`–`06f4a780` | player state | `PlayerState.cpp`, `ContentPlayerState.h` |
| 47, 49, 51, 63 | `06f4a780`–`06f4ed80`, `06f55e20`–`06f56f50`, `06f57cb0`–`06f5a2e0`, `06f97360`–`06fa75c0` | RankDiff, bonus loot, ship classes, random mission generator | `RandomMission*.*` |
| 48 | `06f4ed80`–`06f55e20` | faction properties | `FactionProps.*` |
| 50 | `06f56f50`–`06f57cb0` | NPC ship base (directives, damage) | `BehaviorCmds.*` |
| 54 | `06f6b840`–`06f6fa90` | access/docking control (`dock_disallowed`) | `DockQueues.*` |
| 53 | `06f5c760`–`06f6b840` | `SpaceSupport`, docking queues, spawn, voices | `SpaceSupport.*`, `DockQueues.*`, `SpawnPlacement.cpp`, `SpacePaths.*`, `Voices.*` |
| 55 | `06f6fa90`–`06f6fd90` | tradelane requests | `TradelaneRequests.*` |
| 56 | `06f6fd90`–`06f70dd0` | combat reputation | `CombatReputation.*` |
| 57, 61 | `06f70dd0`–`06f71fc0`, `06f84060`–`06f94d10` | random mission automata and runtime | `RandomMissionRuntime/Machine/Triggers.*` |
| 58, 64 | `06f71fc0`–`06f802b0`, `06fa75c0`–`06fa9000` | `SystemInfo`, zones, shared STL helpers | `SystemInfo.*`, `Zones.cpp` |

Shared STL instantiations (TU64, TU17 map nodes) are, in the port, ordinary containers at the point of
use rather than separate functions.

## Rules for working on the port

- One retail class — one port class; no shared bases that retail lacks; VC6 STL layouts are
  not reproduced. Headers of the base classes `StateMachine`/`BaseOp`/`Personality` come only from
  `src/include/common*.h` and `server.h`; the module's public ABI is `src/include/content.h`.
- Data parsing goes through `INI_Reader` and `pub::GetDataPath`, as in retail (the module must contain no private `fopen` parsers
  and no port logs).
- Status and gaps per function are in decomp-tracker; violations of these rules that are still live are recorded in
  [deviations.md](deviations.md).
