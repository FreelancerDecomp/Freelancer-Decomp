# Story script opcodes: `Cnd_*` and `Act_*`

A reference of the arguments of all `Cnd_*` (condition) and `Act_*` (action) keys in the `[Trigger]` section of the story
missions (`DATA\MISSIONS\M01A\m01a.ini` … `M13\m13.ini`). The design of the trigger engine and the
`[Trigger]` format are in [content_mission_script.md](content_mission_script.md); the implementation is the classes
`src/Content/MissionCnd.*` and `MissionAct.*` (one class per key, with string parsing and
execution). The tables were derived from the `Content.dll` listing; where they disagree, the listing and the source are right.

Argument notation: `sN` — `get_value_string(N)`, `iN` — integer, `fN` — floating-point number,
`bN` — boolean. Optional parameters are checked as "not set". Key names are compared
case-insensitively. Objects in arguments are a nickname (`CreateID`) or a label from the sections
`[MsnShip]`/`[MsnSolar]`/…; the player is `Player`.

Common values:

- Reputation (`REP_*`): `REP_HOSTILE_MAXIMUM`, `REP_HOSTILE_THRESHOLD`, `REP_NEUTRAL_HOSTILE`,
  `REP_NEUTRAL`, `REP_NEUTRAL_FRIENDLY`, `REP_FRIEND_THRESHOLD`, `REP_FRIEND_MAXIMUM`; otherwise a number.
- Distance (`Cnd_DistShip`, `Cnd_DistVec`): `inside` / `outside`; check frequency `TICK_AWAY` /
  `TICK_TOWARDS` / `TICK_ALWAYS`.
- `no_params` means "no arguments".

## Conditions (44)

| Key | Arguments | Purpose |
|---|---|---|
| `Cnd_BaseEnter` | `s0` base \| `ANY` | The player entered a base (`GetBaseID`) |
| `Cnd_BaseExit` | same | The player left a base |
| `Cnd_CargoScanned` | `s0` scanner, `s1` target (neither empty) | Cargo scan of a pair of objects. INI: `Player, PEscort1` |
| `Cnd_CharSelect` | `s0` NPC, `s1` room, `s2` base | Character selection in a base room (`FindLocation`). INI: `juni, Bar, Li01_01_Base` |
| `Cnd_CommComplete` | `s0` comm id (`GetNicknameId`); opt. `s1` | The communication/overlay finished playing. INI: `DX_M01_0700_KING` |
| `Cnd_CmpToPlane` | `s0` inside\|outside, `s1` object, `f2…f5` plane | Object on one side of a plane (not found in the SP INI) |
| `Cnd_Destroyed` | `s0` object `CreateID`; opt. `i1` count; `s2` `EXPLODE`\|`SILENT`; `s3` `ALL`\|`ALL_IGNORE_LANDING` | Destruction. INI: `escort, 1, EXPLODE` |
| `Cnd_DistCircle` | `s0` who, `s1` ring/object | Inside an object's radius (`GetLocation`/`GetRadius`). INI: `Player, Br05_track_ring_2` |
| `Cnd_DistShip` | `s0` inside\|outside, `s1` A, `s2` B, `f3` dist.; opt. tick | Ship-to-ship/object distance. INI: `inside, Player, Li01_Trade_Lane_Ring_146, 2000` |
| `Cnd_DistVec` | `s0` inside\|outside, `s1` object, `f2,f3,f4` xyz, `f5` r; opt. tick | Distance to a point. INI: `inside, transport1, 28465, 165, -64500, 7500` |
| `Cnd_DistVecLbl` | `s0` inside\|outside\|any, `s1` A, `s2` label `CreateID`, `f3…` | Like DistVec, the anchor is a label (absent from m0N.ini; `Cnd_DistVecLbL` is present) |
| `Cnd_EncLaunched` | `s0` encounter nickname `CreateID` | A random/story encounter has been launched |
| `Cnd_HasMsn` | `s0` `yes` \| `no` | Whether the player has an active offer/mission. INI: `yes` |
| `Cnd_HealthDec` | `s0` object, `f1` HP fraction threshold; opt. `s2` | HP dropped below the threshold. INI: `prisonship, 0.04` |
| `Cnd_InSpace` | `b0` | Player in space vs on a base. INI: `true` |
| `Cnd_InTradelane` | `b0`, opt. `s1` ring | In (or not in) a tradelane. INI: `false, Lambda4` |
| `Cnd_InZone` | `b0`, `s1` who; opt. `s2` `OBJECT_ZONE`\|`EXCLUSION_ZONE`\|`NEBULA_ZONE`\|`DUST_ZONE` | In a zone. INI: `false, Player, OBJECT_ZONE` |
| `Cnd_JumpgateAct` | `s0` gate `GetNicknameId` | Jump gate activation |
| `Cnd_JumpInComplete` | `s0` system \| `ANY` (`GetSystemID`) | Jump into a system completed |
| `Cnd_LaunchComplete` | `s0` who (`GetNicknameId`); opt. `s1` | Launch from a base/dock completed. INI: `Player` |
| `Cnd_LocEnter` | `s0` room, `s1` base | Entering a base room. INI: `Bar, Li01_01_Base` |
| `Cnd_LocExit` | same | Leaving a room. INI: `Deck2, Li01_03_base` |
| `Cnd_LootAcquired` | `s0` loot `CreateID`, `s1` who; opt. `i2` | Loot picked up. INI: `ashcroft_pod, Player` |
| `Cnd_MsnResponse` | `s0` `accept` \| `reject` | Response to a mission offer |
| `Cnd_NPCSystemEnter` | `s0` system or `ANY`; then up to 50 NPC names (`s1…`); `Player` is not put in the list but is always counted as a participant | An NPC entered a system. INI: `Li03, King, Player` |
| `Cnd_NPCSystemExit` | `s0` system \| `ANY`; opt. NPC list | An NPC left a system |
| `Cnd_PlayerLaunch` | none (`no_params`) | The player launched into space |
| `Cnd_PlayerManeuver` | `s0` type (`BehaviorTypes::get_behavior_id`); opt. `s1` target; token `inverse` | Player maneuver. INI: `Dock, Li01_Trade_Lane_Ring_146` |
| `Cnd_PopUpDialog` | `s0` `YES`\|`NO`\|`LATER`\|`CLOSE` | A popup closed with the given answer |
| `Cnd_ProjHit` | `s0` target, `i1` hit count; opt. `s2` shooter | Projectile hit. INI: `r_cruiser, 1, o_hostile1` |
| `Cnd_ProjHitShipToLbl` | `s0` ship, `i1` hits, opt. `s2` label `CreateID` | A ship's hit on a label. INI: `us, 3, base_hostile1` |
| `Cnd_RTCDone` | `s0` RTC ini path | The RTC scene ended |
| `Cnd_RumorHeard` | like CharSelect (NPC / room / base) | Rumor heard (absent from m0N.ini) |
| `Cnd_SpaceEnter` | system \| `no_params` | The player entered the space of a system |
| `Cnd_SpaceExit` | same | The player left space (to a base / jump) |
| `Cnd_SystemEnter` | `s0` dest \| `ANY`; opt. `s1` prev | Entering a system. INI: `Li02, Li01` |
| `Cnd_SystemExit` | same | Leaving a system. INI: `ANY` |
| `Cnd_TetherBroke` | `s0` A, `s1` B, `f2` dist, `f3` time; opt. `f4` | Tether/escort link broken. INI: `juni, Player, 1500, 30, 2500` |
| `Cnd_Timer` | `f0` seconds; opt. `f1` — upper bound (a random value between `f0` and `f1`); opt. `s2` `RUN_ON_BASE` / `RUN_IN_SPACE_ONLY` | Timer; `s2` sets whether it counts on a base or only in space. INI usually `5` |
| `Cnd_TLEntered` | `s0` who, `s1` ring A, `s2` ring B (`GetNicknameId`) | Entering a tradelane |
| `Cnd_TLExited` | `s0` who; `s1` ring or `ANY`; opt. `s2` second ring | Leaving a tradelane. INI: `Player, Li01_Trade_Lane_Ring_141` |
| `Cnd_True` | none | Always true when the trigger is activated (message `0x1000`) |
| `Cnd_WatchTrigger` | `s0` nickname of another trigger; `s1` its state (`ON` in the INI is just a state nickname) | Waits until another trigger enters the given state. INI: `com_0380, ON` |
| `Cnd_WatchVibe` | `s0`, `s1` objects; `s2` threshold (`REP_*` or a number); opt. `s3` comparison `lt` / `gt` / `eq` | Reputation of A towards B against a threshold. INI: `dexter, Player, REP_HOSTILE_THRESHOLD, eq` |

## Actions (85)

| Key | Arguments | Purpose |
|---|---|---|
| `Act_ActTrig` | `s0` trigger nickname | Enables another `[Trigger]` |
| `Act_AddAmbient` | `s0` `.thn` path; `s1` room; `s2` base | Adds an ambient THN to a base room. INI: `scripts\Bases\…thn, Equipment, Li01_01_Base` |
| `Act_AddRTC` | `s0` RTC path; opt. `s1` `repeatable` | Queues an RTC |
| `Act_AdjAcct` | `i0` credit delta | Changes the player's account. INI: `750` |
| `Act_AdjHealth` | `s0` object; `f1` HP fraction | Sets the object's relative health. INI: `escort, 0.3` |
| `Act_CallThorn` | `s0` `.thn` path; opt. `s1` camera object (default `Player`) or three numbers (position) | Plays a THN scene. INI: `missions\m01a\m01a_fp7_01.thn, fp7base` |
| `Act_ChangeState` | `s0` state token `FAIL` / `SUCCEED` / `NO_STATE`; opt. `i1` message ids | Changes `MissionStateNum` (+ ids for NN). INI: `FAIL, 21960` |
| `Act_Cloak` | `s0` object, `b1` | Cloak. INI: `fp7base_attacker1, true` |
| `Act_DeactTrig` | `s0` trigger | Disables a trigger |
| `Act_DebugMsg` | the whole value string | Debug message |
| `Act_Destroy` | `s0` object; opt. `s1` `EXPLODE` / `SILENT` | `pub::SpaceObj::Destroy`. INI: `fp7base_escort1, SILENT` |
| `Act_DisableEnc` | `s0` encounter | Disables an encounter (absent from m0N) |
| `Act_DisableFriendlyFire` | list of object names (up to the first empty parameter) | Removes friendly fire from the listed objects. INI: `navy_patrol1` |
| `Act_DisableTradelane` | `s0` ring `GetNicknameId` | Disables a tradelane |
| `Act_DockRequest` | `s0` dock | Docking request (absent from m0N) |
| `Act_EnableEnc` | `s0` encounter | Enables an encounter |
| `Act_EnableManeuver` | `s0`; `b1` | Allows/forbids a maneuver |
| `Act_EtherComm` | `s0` voice; `i1`; `s2` target; `s3` comm; `i4` (1…10); `s5` head (`NO_HEAD`); `s6` body; opt. accessories | Ether communication (the INI key is often `Act_Ethercomm`). INI: `M01a_newark, 196773, Player, DX_M01_0140_…` |
| `Act_ForceLand` | `s0` base → `GetBaseID` | Forced landing |
| `Act_GCSClamp` | `b0` | GCS clamp (group AI). INI: `true` |
| `Act_GiveMB` | `s0` ship, `s1` metabehavior `CreateID` | Gives a metabehavior (absent from m0N) |
| `Act_GiveNNObjs` | `no_params` | Delivers the accumulated list of NN objectives |
| `Act_GiveObjList` | `s0` object; `s1` objective list | Attaches an objective list to a ship. INI: `fp7base_escort1, ol_fp7_goto_convoy1` |
| `Act_HostileClamp` | `b0` | World hostility clamp. INI: `true` |
| `Act_Invulnerable` | `s0` object, `b1`; opt. `b2`, `f3` HP. INI: `escort, true, false, 0.99` | Invulnerability |
| `Act_Jumper` | `s0` object; `b1` | Jumper flag (like the `[MsnShip]` key) |
| `Act_LightFuse` | `s0` object, `s1` fuse; opt. `f2`. INI: `fp7base, freeport7_main_fuse1` | Lights a fuse |
| `Act_LockDock` | `s0` who; `s1` dock; `s2` `lock` / `unlock` | Locks or opens a dock |
| `Act_LockManeuvers` | `b0` | `pub::AI::lock_maneuvers`. INI: `true` |
| `Act_MarkObj` | `s0` object, `i1` flag. INI: `escort, 1` | HUD/map marker |
| `Act_MovePlayer` | `f0,f1,f2` position; `f3` | Teleports the player |
| `Act_NagClamp` | `b0` | Global clamp of nag lines |
| `Act_NagDistLeaving` | `s0` nag id `CreateID`; `s1` who; `s2` target or `position`; ids/dist.; `NAG_IN_COMBAT`\|`NAG_OUT_OF_COMBAT`\|`NAG_ALWAYS` | "You are leaving" nag. INI: `std, escort, escort, 21970, 4000, NAG_IN_COMBAT` |
| `Act_NagDistTowards` | `s0` `OBJ` / `POS`; `s1` nag id; `s2` who; for `OBJ` — `s3` target, for `POS` — coordinates; then opt. ids, distance, `NAG_IN_COMBAT` / `NAG_OUT_OF_COMBAT` / `NAG_ALWAYS` | "You are approaching" nag |
| `Act_NagGreet` | `s0` nag id; `s1` object | Greeting nag (`nag_greet_combat` / `nag_greet_normal`). INI: `greet, escort` |
| `Act_NagOff` | `s0` nag id | Turns a nag off. INI: `greet` |
| `Act_NNPath` | `i0`, `i1` ids; then either `s2` object, or `f2,f3,f4` position and opt. `s5` system | Path in the navigator |
| `Act_NNIds` | `i0` ids; opt. `s1` `OBJECTIVE`\|`HISTORY`\|`OBJECTIVE_HISTORY` | `pub::Player::DisplayMissionMessage`. INI: `30000, HISTORY` |
| `Act_PlayerCanDock` | `b0`; opt. dock list (`s1…`) | Permission for the player to dock |
| `Act_PlayerCanTradelane` | `b0`; opt. ring list (`s1…`) | Permission for the player to use tradelanes |
| `Act_PlayerEnemyClamp` | `i0`; `i1` | `pub::AI::set_player_enemy_clamp`: limit on the number of enemies on the player |
| `Act_PlayerForm` | `s0` formation `CreateID` | Player in a formation (absent from m0N) |
| `Act_PlayMusic` | `s0` `no_params` or up to four cues (`none` — skip); `s4` fade or `none`; `s5` flag or `none` | Music |
| `Act_PlayNN` | `s0` NN nickname | Play an NN clip (absent from m0N) |
| `Act_PlaySoundEffect` | `s0` effect `GetNicknameId`. INI: `depot_wallah_srtc` | Sound |
| `Act_PilotParams` | `s0` ship, `s1` pilot set | Changes the AI pilot (absent from m0N) |
| `Act_PobjIdle` | `no_params` | Idle player-object. The INI key is often `Act_PObjIdle` |
| `Act_PopUpDialog` | `i0`, `i1` ids; opt. `s2` `YES_NO` / `YES_NO_LATER` / `CLOSE` | `pub::Player::PopUpDialog` |
| `Act_SetPriority` | `s0` label `CreateID`, `s1` `NORMAL`\|`ALWAYS_EXECUTE` | Objective priority. INI: `lbl_friends, NORMAL` |
| `Act_RandomPop` | `b0` | Turns random population on/off. INI: `false` |
| `Act_RandomPopSphere` | `f0,f1,f2` centre; `f3` radius; `s4` `on` / `off` | Random pop exclusion sphere: `on` removes the sphere, `off` adds it |
| `Act_RelocateForm` | formation + pos (+ ornt) | Moves a formation (absent from m0N) |
| `Act_RelocateShip` | ship + xyz. INI: `fp7_transport1, 100000, 102000, 100000` | Moves a ship |
| `Act_RemoveAmbient` | `s0` `.thn` path | Removes an ambient (`pub::GF::AmbientScriptDestroy`) |
| `Act_RemoveCargo` | `s0` good; opt. `i1`. INI: `ashcroft_pod` | Removes cargo |
| `Act_RemoveRTC` | `s0` RTC path | Removes an RTC |
| `Act_RepChangeRequest` | `s0` system; then up to 50 reputation groups | Reputation change request |
| `Act_RevertCam` | none (`no_params`) | Camera back (`pub::Player::RevertCamera`) |
| `Act_RpopAttClamp` | `b0` | Random pop attack clamp |
| `Act_RpopTLAttacksEnabled` | `b0` | Random pop attacks on tradelanes (absent from m0N) |
| `Act_Save` | `s0` nickname; opt. `i1` | `pub::Save` and a mission message |
| `Act_SendComm` | `s0` speaker; `s1` listener (`Player` is a separate branch); `s2` comm; opt. `i3` (1…10); opt. `s4` voice; opt. `i5`; opt. `s6` head (`NO_HEAD`), `s7` body and then accessories | Communication (radio line) |
| `Act_SetFlee` | `s0` `CLEAR` / `SET_POS` / `SET_OBJ`; `s1` object; for `SET_OBJ` — target and numbers, for `SET_POS` — coordinates | Flee behaviour |
| `Act_SetInitialPlayerPos` | `f0,f1,f2` position; `f3…f6` orientation | Player's starting pose (`SetInitialPos`/`SetInitialOrnt`) |
| `Act_SetLifeTime` | `s0` object, `f1` sec. INI: `harmony, 100000` (the INI key is often `Act_SetLifetime`) | Object lifetime |
| `Act_SetNNHidden` | `s0` objective (`all` — all); `b1` | Hide an NN objective |
| `Act_SetNNObj` | `s0` objective; opt. `s1` `HISTORY` / `OBJECTIVE` / `OBJECTIVE_HISTORY` | Current NN objective |
| `Act_SetNNState` | `s0` obj `CreateID`; `s1` `ACTIVE`\|`INACTIVE`\|`COMPLETE`\|`FAILED` (INI: `active`) | NN state |
| `Act_SetOffer` | `i0` ids | Offer text (`DisplayMissionMessage`). INI: `21805` |
| `Act_SetOrient` | `s0` object, `f1…f4` quaternion. INI: `PEscort1, 1, 0, 0, 0` | Orientation |
| `Act_SetRep` | `s0` who, `s1` group (`GetReputationGroup`); `f2` or `REP_*` | Reputation. INI: `Player, li_n_grp, 0.91` |
| `Act_SetShipAndLoadout` | `s0` ship (`none` — do not change); `s1` loadout (`none` — do not change) | Player's ship and loadout |
| `Act_SetTitle` | `i0` ids. INI: `21800` | Mission title |
| `Act_SetVibe` | `s0`, `s1` objects; `s2` `REP_*` or a number; opt. `s3` second threshold | Ship-to-ship vibe |
| `Act_SetVibeLbl` | `s0`, `s1` labels/objects; `s2` `REP_*` or a number; opt. `s3` | Label-to-label/object vibe |
| `Act_SetVibeLblToShip` | `s0` label; `s1` ship; `s2` `REP_*` or a number; opt. `s3` | Label-to-ship vibe (the INI has `Act_SetVibeLbltoShip`) |
| `Act_SetVibeShipToLbl` | `s0` ship; `s1` label; `s2` `REP_*` or a number; opt. `s3` | Ship-to-label vibe |
| `Act_SetVibeOfferBaseHack` | `s0` solar | Vibe towards the offer's base. INI: `dock_ring_Li01_01` |
| `Act_SpawnFormation` | `s0` `[MsnFormation]` nickname; opt. `f1,f2,f3` position; opt. `f4…f7` orientation | Spawns a formation |
| `Act_SpawnLoot` | `s0` `[MsnLoot]` nickname; opt. `f1,f2,f3` position; opt. `f4…f7` orientation | Spawns loot |
| `Act_SpawnShip` | `s0` `[MsnShip]` nickname; opt. `s1` system; opt. `f2,f3,f4` position; opt. `f5…f8` quaternion (normalised) | Spawns a ship |
| `Act_SpawnShipRel` | `s0` nickname; `s1` system; `s2` anchor; `s3` `DIRECTIONAL` / `POINT` / `RELATIVE`; `f4,f5,f6`; opt. `f7`, `f8` | Spawn relative to another object |
| `Act_SpawnSolar` | `s0` `[MsnSolar]` nickname; opt. `f1,f2,f3` position; opt. `f4…f7` orientation | Spawns a solar. INI: `fp7base` |
| `Act_StartDialog` | `s0` dialog nickname; opt. `i1` (1…10) | Starts a `[Dialog]`. INI: `FP7_Rendezvous_transport` |
| `Act_StaticCam` | `f0…f2` position; `f3…f6` orientation; opt. `f7,f8` | Static camera (`SetCamera` + `SetInvincible`) |

## World object sections in `m0N.ini`

These sections (not `Cnd`/`Act`) are read at the start of parsing the file; the `Act_Spawn*` spawners refer to them
by nickname.

- `[MsnLoot]`: `nickname`, `archetype`, `string_id`, `position`, `rel_pos_obj`, `rel_pos_offset`,
  `orientation`, `velocity`, `angular_velocity`, `equip_amount`, `health`, `label`, `can_jettison`.
- `[MsnSolar]`: `nickname`, `position`, `orientation`, `faction`, `string_id`, `pilot`, `archetype`,
  `loadout`, `system`, `base`, `voice`, `radius`, `label`, `costume`, `visit`.
- `[MsnShip]` / `[MsnFormation]`: `nickname`, `npc`, `random_name`, `label`, `init_objectives`,
  `init_metabehavior`, `jumper`, `cargo`, `hit_points`, `radius`, `system`, `position`, `rel_pos`,
  `orientation`, `arrival_obj`, `formation`, `ship`, etc.
- `[MsnRandEnc]`: `nickname`, `encounter_type` (`TL_Attack` / `SP_Attack`, …).
- `[Mission]`: `mission_title`, `mission_offer`, `reward`, `npc_ship_file`.

## Keys absent from the campaign `m0N.ini` files

The engine accepts them, but they do not occur in `m01a.ini` … `m13.ini`: `Cnd_CmpToPlane`,
`Cnd_DistVecLbl`, `Cnd_EncLaunched`, `Cnd_JumpgateAct`, `Cnd_NPCSystemExit`, `Cnd_RumorHeard`;
`Act_DebugMsg`, `Act_DisableEnc`, `Act_DockRequest`, `Act_EnableEnc`, `Act_GiveMB`, `Act_PilotParams`,
`Act_PlayNN`, `Act_PlayerForm`, `Act_RelocateForm`, `Act_RepChangeRequest`, `Act_RpopTLAttacksEnabled`,
`Act_SetFlee`, `Act_SpawnShipRel`.
