# Common.dll — shared game data and object library

`Common.dll` is the shared runtime of the client and the server: identifiers, INI reading, loading of all
game tables (archetypes, goods, universe, reputation, costumes, rooms), the object model of ships and
equipment, a wrapper over the physics, and the AI contract. This document is for whoever ports or edits
the module: it describes the architecture and data formats, not individual functions — line-by-line facts
and the status of every function live in decomp-tracker (module `Common.dll`).

Related documents: [common_archetype.md](common_archetype.md) (archetypes and client companions),
[common_physys.md](common_physys.md) (physics), [cross_dll_protocol.md](cross_dll_protocol.md);
the `IBehaviorManager`/`IObjInspect` interfaces are declared in `src/include/common_iface.h`.

## Linking and conventions

- It is not a DACOM component: `RegisterComponent` is not called, and it is not in `[Libraries]` of
  `bin/DACOM.INI`. `freelancer.exe`, `Server.dll`, `Content.dll`, `rpclocal.dll`, `RemoteClient.dll`,
  `FLServer.exe` link `Common.lib` through an ordinary import. Retail exports ~3860 names (the exe takes
  ~830 of them); the port's export set is checked against `dumpbin -exports EXE\Common.dll`.
- Free functions are `__cdecl`, class methods are `__thiscall`; DACOM `__stdcall` is not used here. The
  public ABI lives flat in `src/include/common_<subsystem>.h` (`COMMON_API`); what is shared only between
  several TUs of one subsystem lives in `src/Common/common_<subsystem>_internal.h`.
- The port does not reproduce VC6 STL layouts or the retail heap: containers are modern, and field offsets
  are kept wherever other modules read them (checked with `static_assert`; offsets past containers are
  correct only with the `std::` size in Debug and `_ITERATOR_DEBUG_LEVEL=1` in Release — see
  `src/Directory.Build.targets`).
- DllMain initialises nothing (`DisableThreadLibraryCalls`). The real startup is the calls
  `Async::Startup`, `Timing::init`, `DataConfig::startup`, `PhySys::Startup` from the exe/Server.

## Source layout

One translation unit per retail class (or a tight group that retail keeps in one `.cpp`).

| Subsystem | `src/Common/` files |
|---|---|
| Time, async, heap, errors | `common_timing`, `common_async`, `common_heap`, `common_errorhandler`, `common_constants` (`ReadConstants`), `common_misc` (paths, `SinglePlayer`, math, `DllMain`) |
| IDs and INI | `common_id`, `common_ini`, `common_textfile` (`FileMap`, `LineParser32`, `TextFile32`), `common_stringcache`, `common_fmtstr` |
| Data loading | `common_dataconfig`, `common_archetypeloader`, `common_goodinfo`, `common_loadout`, `common_equipdesc`, `common_basedata`, `common_baseproperties`, `common_roomdata`, `common_roominfo`, `common_universe*`, `common_reputation`, `common_vibe`, `common_costume`, `common_fuses`, `common_newchar`, `common_rtcslider`, `common_pubstategraph` |
| Archetypes | `common_archetype`, `common_archetype_ships/_equipment/_weapons/_misc`, `common_phyarch` |
| Objects | `common_cobject`, `common_engineobject`, `common_csimple`, `common_ceqobj`, `common_cship`, `common_csolar`, `common_cloot`, `common_cmine`, `common_cprojectile`, `common_cattachment`, `common_carchgroup`, `common_casteroid*`, `common_hierarchy`, `common_hardpointsummary`, `common_basewatcher` |
| Equipment | `common_cequip` (`CEquip`, manager, traversal) and one file per class: `common_ce*.cpp`, `common_cattachedequip`, `common_tractorarm`, `common_csteering`, `common_laneaction`, `common_launchaction` |
| Physics | `common_physys*`, `common_physicalsystem`, `common_phycontroller`, `common_phycollisionstate`, `common_animdb`, `common_csys`; IVP is in `src/IVP/` |
| AI and the `pub::` contract | `common_behavior*`, `common_*behavior.cpp`, `common_*idea.cpp`, `common_systemmanager*`, `common_targetmanager`, `common_pathfind`, `common_aisteer`, `common_pub`, `common_iobjinspectimpl`, `common_actiondb` |
| Text and RDL | `common_rect`, `common_textrender`, `common_rdlnodes`, `common_renderdisplaylist`, `common_rdlio` |
| Network-adjacent | `common_marshalbuffer`, `common_statemachine`, `common_watchable`, `common_mpcritsec`, `common_playeraccount`, `common_exclusionzone`, `common_deadreckonedvector`, `common_remotephysics`, `common_tradeoffer` |

## Data loading: `DataConfig`

`DataConfig::startup(iniPath, ICliObjFactory*, ProgressCB*)` is the orchestrator: it opens
`freelancer.ini`, the `[Data]` section, and calls a loader for every key. **The load order is the order of
lines in the INI** (the dispatcher is a chain of `is_value`, case-insensitive); retail puts `solar` before
`universe` because `Universe` looks at the `OBJECT_TYPE` of solars. A return of `0` is success, `1` means
`ProgressCB` demanded that loading be aborted. A repeated `startup` only increments the reference count.
`ProgressCB` is called for every `[Data]` value before the dispatcher (through it the client loads `HUD=`,
fonts, sounds — keys that Common itself does not know); the client passes an `ICliObjFactory`, Server passes
`nullptr`. After a successful `open`, `AI\state_graph.db` is always loaded.

| `[Data]` key | Loader | Note |
|---|---|---|
| `universe` | `Universe::Startup` | relative path, `GetFileSysPath` inside |
| `constants` | `ReadConstants` | same |
| `equipment`, `solar`, `ships`, `asteroids`, `explosions` | `Archetype::LoadEquipment/LoadSolar/LoadShips/LoadAsteroids/LoadSimples` | several files per key; zero new records is a warning, not an abort |
| `loadouts` | `Loadout::Load` | |
| `bodyparts`, `costumes` | `LoadBodypartDescriptions`, `LoadCostumeDescriptions` | |
| `bases` | `BaseDataList_load` | key without a file: walks the `Universe` bases |
| `goods`, `markets` | `GoodList_load`, `BaseDataList_load_market_data` | |
| `PetalDB` | `PetalDB_load` | |
| `Groups` | `Reputation::Startup` | |
| `fuses` | `FuseDB::AddFuseINIFile` | only remembers the path |
| `WeaponModDB` | loading of `[WeaponType]` | |
| `NewCharDB`, `RtcSlider` | `NewChar::TheDB->Load`, `RtcSlider::load` | |

DataConfig ignores the remaining keys (`fonts`, `sounds`, `voices`, `effects`, `HUD`, …). The `shutdown`
order is the reverse: state graph, loadouts, archetypes, explosions, costumes, goods, Petal, bases,
universe, reputation, fuse, WeaponMod, NewChar.

## Identifiers

- **`CreateID(nickname)`** is the main nickname hash. `CFLIDMaker` (a singleton, `m_pTheMaker`) lazily
  builds a table of 256 words and computes a CRC-16-IBM extended to 30 bits (32-bit register, width 30,
  polynomial `0x8005`, reflect-in); the result is then byte-swapped, shifted and gets the high bit:
  `id | 0x80000000`. The nickname is lower-cased (ASCII only in the port).
  Examples: `li_elite` → `0xB746B147`, `Li01` → `0x83A78E4F`, `ge_fighter` → `0x80410B80`,
  `player` → `0x8ABCE14E`, empty string → `0x80000000`, `nullptr` → `0`.
  Dynamic object IDs (`AllocateObjectID`) live in 30 bits (`& 0x3FFFFFFF`).
- **It is not `DACOM_CRC::GetCRC32`**: that hash (dacom.dll) is needed only for archetypes/hardpoints.
- **`MakeId(name)`** is the 16-bit faction hash: CRC-16-CCITT (`0x1021`, initial value `0xFFFF`) over the
  lower-cased string up to 127 bytes; used in `Reputation`. `MakeLocationID(base, name)` is
  `CreateID("%X_%s")`.
- `ID_String` is a single `unsigned` (0 = empty); `get_string` is not a reverse hash but `0x<hex>` from a
  rotating buffer of 4 strings.

## INI: `INI_Reader`, `FileMap`, BINI

`INI_Reader` reads both text INI and binary INI (`'BINI'`, 12-byte header; parameters are
type + 4 bytes: 0 — byte, 1 — int32, 2 — float, 3 — offset in the string table). `open` detects the format
by itself; `open_memory` always treats the buffer as text. Section and key names are compared
case-insensitively. A value's parameters are a comma-separated list (`get_value_string(i)`); an empty
parameter is checked with `is_value_empty(i)`. The pointer from `get_value_string` is valid only until the
next `read_value()` — loaders must copy strings immediately (costumes and rooms broke on this). Retail
writes into several 0x100-byte buffers without bounds; the port keeps whole strings and truncates only
beyond large limits (see the tracker entry). The field layout of `INI_Reader` is retail's (the text half
at `+0`, the BINI half at `+0x1428`, the format flag at `+0x1564`), but the sizes differ.

## File system and paths

`GetFileSysPath(dest, relOrAbs, IFileSystem*)`: `nullptr` → empty string; a path starting with `\` or with
`X:` is copied as is (`/` does not count as absolute); otherwise the file system root (`DALib::Data`, unless
one is passed) is taken through slot `+0x84` (`GetFileSysPath(char*, unsigned)`) and joined with
`_makepath`. Nearby: `GetFullFilename`, `GetUserDataPath` (`Documents\My Games\Freelancer`),
`GetUserLocalDataPath` (`%LOCALAPPDATA%\Freelancer`), `GetScreenShotPath` (`Pictures\FreelancerShots`),
`DAEngine_create_filesystem/release_filesystem/open_filesystem` (a child `IFileSystem` of `Data`; for
`.utf`/`.ale` the DOS implementation hands the file over to the UTF implementation).

## Archetypes

See [common_archetype.md](common_archetype.md) for details. In short: `Archetype::Load*` parse the INI
into `ArchDB` databases by `CreateID(nickname)`; every object gets a client companion (`ICliObj`) from
`ICliObjFactory`; the mesh is loaded later through `Root::load`. The `.sur` file next to the mesh carries
the physical parts (`PhyArch`): `"vers"`/float 2.0, then for each part an id and the chunks `surf` (IVP
shell), `exts` (min/max), `!fxd`, `hpid` (hardpoint list).

## Goods, loadouts, bases, markets, constants

- **Goods** (`[Good]`): `GoodInfo` by `CreateID(nickname)` and `ArchId`; categories `Commodity`,
  `Equipment`, `ShipHull`, `Ship`; prices (`price`, `good_*`/`bad_*`), `hull`, `addon` (`low|mid|high`),
  `combinable`, `free_ammo`, `ids_name`/`ids_info`. A duplicate nickname aborts the block.
- **Loadouts** (`[Loadout]`): `nickname`, `equip` (equipment on a hardpoint), `cargo` (arch, count,
  hardpoint); `EquipDesc` instance identifiers (0x20 bytes) are issued starting from `0x21`; combinable
  cargo is summed.
- **Bases / markets**: `BaseDataList` is built by walking the `Universe` bases; the sections
  `[Base]`/`[BaseInfo]` (`start_room`, `price_variance`, `ship_repair_cost`, default 0.33) and `[Room]`
  (`nickname`, `file`; the room ID is `CreateID("%X_%s")` of the base ID); the market is `[BaseGood]`
  (`base`, parameters 0–6, a record with default values is skipped).
- **Constants** (`ReadConstants`): the sections `Constants`, `EngineEquipConsts`, `ShieldEquipConsts`,
  `ThrusterEquipConsts`, `PhySysConsts`, `CommConsts`, `CollisionConsts` (`damage`,
  `max_impact_speed`). An unknown key is the warning "Unknown Constant definition".

## Universe

`Universe::Startup(universe.ini)`: creates a child file system on the directory of `universe.ini` (via
`DALib::Dacom`), reads the sections `[Time]` (`seconds_per_day` → `DaysPerSecond`), `[Base]`
(`nickname`, `file`, `system`, `strid_name`, `BGCS_base_run_by`, `ship_*`, `terrain_*`,
`autosave_forbidden`) and `[System]` (`nickname`, `file`, `pos`/`real_pos`, `NavMapScale`, `visit`,
`msg_id_prefix`, `strid_name`, `ids_info`). For every system its `file=` is opened (relative to the
universe directory): the sections `[Zone]` (`nickname`, `pos`, `rotate`, `shape`
sphere/ellipsoid/box/cylinder/ring/mesh, `size`, `edge_fraction`, `property_flags`, `drag_modifier`,
`damage`, `interference`, `music`, `spacedust`, `path_label`, `visit`, `ids_name`/`ids_info`) and
`[Object]` (`nickname`, `archetype`, `pos`, `base`, `dock_with`, `goto`, `reputation`, `next_ring`/
`prev_ring`, `star`, `visit`). `goto` puts the object into the gate map (`get_gate_system`); the gate type
is taken from the `OBJECT_TYPE` of the solar archetype. The zones of a system are gathered into a BSP
partition (`ISpatialPartition`, `common_spatial.cpp`, built in `SYSTEM::BuildZoneTree`). Queries:
`get_system/get_base/get_zone(+_id)`, `GetFirst/NextSystem/Base`, `first_zone/next_zone`.

## Reputation, costumes

- **Reputation** (`Groups = InitialWorld.ini`): `[Group]` sections; the group key is `MakeId(nickname)`
  (`TString<16>`), followed by `ids_name`, `ids_info`, `ids_short_name`; the relations `rep = float, nickname`
  are read not by `Startup` but by `Reputation::LoadFeelings` (called by Server when loading a world/save).
  The self-relation is always 1.0. `IsStoryFaction` is a fixed list `fc_ln_grp … fc_uk_grp`. Dynamic
  `Reputation::Vibe` records (stride 0x90) are freed by `CleanUpDatabases`.
- **Costumes**: `bodyparts.ini` (types `Body/Head/LeftHand/RightHand`, `[Skeleton] sex`,
  `[Accessory]`) and `costumes.ini` (`[Costume]` with `nickname`, parts and `accessory`). The shared
  `Costume` stores IDs in the order head, body, lefthand, righthand; the internal maps and
  `costume::Parts` use body, head, lefthand, righthand: at the boundary the first two are swapped
  (Server, Content and the client use the same contract).

## Rooms and bases

`RoomData` (`[Room]`/`[Room_Info]`, `[Camera]`, `[Sound]`, `[CharacterPlacement]`,
`[PlayerShipPlacement]`, `[ForSaleShipPlacement]`, `[Hotspot]`, `[FlashlightSet]`) reads the room INI
lazily (`load()` once); the set of room objects comes from the Thorn scene (`set_script`): entity types
COMPOUND=1, DEFORMABLE=2, CAMERA=3, LIGHT=5, SETPOINT=7. A setpoint name is
`zs|zg _ pc|npc|rpc|none _ segment _ zone [_ suffix]` (for example `zs_pc_MainHall_00_Desk`).
Obsolete sections and keys (`mesh`, `background_script*`, `[Light]`, `[Fog]`, …) are accepted with a
warning. `ThornScriptLoad` obtains an `IScriptEngine` by `"1.11_IScriptEngine"` — the only IID that Common
requests itself. `HardpointSummary` uses `DALib::Engine` slots to enumerate the hardpoints of a compound
mesh.

## Fuse, Petal, WeaponMod

- **FuseDB** in Common is only the base classes `Fuse`/`FuseAction` and a list of paths
  (`m_FuseINIFiles`); the fuse files themselves are read by the consumers: the client (`client_fuse.*`)
  and Server.
- **PetalDB** (`[ObjectTable]`): the category is determined by the value name (`room`=0, `cart`=5,
  `prop`=6), parameter 0 is the nickname, 1 the mesh, then up to 16 additional ones;
  `PetalInterfaceDatabase::get_category` accepts the case-sensitive
  `Room/Spaceship/Solar/Asteroid/Equipment/Equipment Cart/Prop`.
- **WeaponModDB** (`[WeaponType]`): `nickname`, then `shield_mod = shield_nick, float`; a lookup returns
  1.0 on a miss.

## NewChar, RtcSlider, StateGraph

- **NewCharDB** (`NewChar::TheDB`): the sections `[Faction]` (`nickname`, `rep_group`, `base`, `package`,
  `pilot`), `[Package]` (`nickname`, `strid_name/desc`, `ship`, `loadout`, `money`), `[Pilot]`
  (`nickname`, `body`, `comm`, `*.anim`, `voice`, `thumb`); four maps by `CreateID`, on a duplicate the
  first one wins. Consumers: `Server.dll` (character creation from the `NewCharacter.fl` template) and
  the client selection screen.
- **RtcSlider** (`[RtcSlider]`): `set = name, threshold` / `set_less`; `is_enabled(name)` compares the
  threshold with the slider; `get_fps_cap` is 20, 30 or 200 depending on `fps_20_cap`/`fps_30_cap`.
- **`pub::StateGraph`** (`AI\state_graph.db`, text): the number of graphs, the number of behaviours (must
  be 21), then per record a name, a type `LEADER`/`ESCORT` and a 21×21 matrix.

## Text and RDL

`FmtStr` is a linked list of values with type tags (0x21 nested `FmtStr`, 0x4E block of 5 dwords, 0x42
base, 0x46 group, 0x47 good, 0x49 installation, 0x4C loot, 0x4F object, 0x52 reputation instance, 0x53
system, 0x5A zone, 0x64 int, 0x73 string) with a `flatten/unflatten` format for the network.
`RenderDisplayList` is a list of `RDLNode` nodes (`TextNode`, `TRANode`, `PositionNode`, `JustifyNode`,
`ImageNode`, `StyleNode`, `ClipNode`, `RDLRefNode`, …) that are executed on `TextRenderContext`/`DeviceTRC`
(GDI); styles are the `StyleCollection` registry. `BinaryRDLReader/Writer` and `XMLReader/Writer` are
abstract interfaces.

## Objects and equipment

Hierarchy: `CObject` (per-class pool, `Alloc/Find`) → `CSimple` → `CEqObj` → `CShip`/`CSolar`;
`CProjectile` → `CMunition`/`CGuided`/`CBeam`/`CMine`/`CCounterMeasure`; `CLoot`, `CAsteroid`.
`EngineObject` ties an object to an engine instance (EngBase). Equipment: `CEquip` →
`CInternalEquip` (power, engine, scanner, armour, tradelane) / `CExternalEquip` → `CAttachedEquip`
(`CELauncher` → `CEGun`, `CEShieldGenerator`, `CEThruster`, `CECargoPod`, …). `CAttachedEquip::Connect`
chooses `CNonPhysAttachment` (the hardpoint is in the parent's list) or `CPhysAttachment` (a physical body
on IVP). Equipment traversal is `CEquipManager::Traverse` by a type mask (external first, then internal).
`Reputation::Vibe` defines the directed relation of objects: the relation of A to B and of B to A may
differ; the HUD and `USER_CLOSEST_ENEMY` take the relation of the contact to the player.
Scanner: `CSimple::ScannerInterference` stores the immersion in a zone, and the resulting interference is
given by `get_scanner_interference()` (with the zone coefficient and an upper limit) — it is used by the
scanner range, the radar and the HUD.

## AI and the `pub::` contract

`IBehaviorManager` (behaviours, ideas, `SystemManager`) and `pub::AI::*` (the `Personality` structures,
the `Directive*Op` operations, `ContentCallback`/`DirectiveCallback`) are the contract between Common,
Server and Content; it is declared in `src/include/common_pub.h` and `common_iface.h`. Content supplies its
own `ContentCallback` (see [content.md](content.md)), Server calls `set_content_callback` from
`pub::AI::SubmitState`.

## Network-adjacent and miscellaneous

There is no transport in Common (it is in `rpclocal`/`RemoteClient`/`Server`). There are `MarshalBuffer`
(a serialisation buffer of 0x18, put/get of primitives, strings, `FmtStr`, costume), `StateMachine*`/
`MessageHandler` (a two-state machine and a message router on `IMessageRouter`), `Watchable`/`BaseWatcher`,
`MPCritSec`, `CPlayerAccount` (an MD5 account key in the registry), `Exclusion::*ZoneMgr` (exclusion zones
of asteroids and nebulae), `CDeadReckonedVector` and `CRemotePhysicsSimulation`.
`SinglePlayer()` is a flag with the initial value "single player"; `SetMultiPlayer(bool)` clears it.
`Timing::*` is a QPC clock (`read`, `seconds`, global time) and `Timing::Delta` (frame delta, 0x28 bytes,
no vtable; when QPC and `GetTickCount` diverge by more than 50 ms, `GetTickCount` is used).

## Known gaps

- Physics: IVP is not fully ported (see [common_physys.md](common_physys.md)).
- Some AI behaviours and equipment subsystems are marked `partial`/`simplified` in the tracker; where
  they disagree with this document, the listing `EXE\Disasm\Common.dll.txt` is right, then the tracker.
- The current list of per-function gaps is in decomp-tracker and in [deviations.md](deviations.md).
