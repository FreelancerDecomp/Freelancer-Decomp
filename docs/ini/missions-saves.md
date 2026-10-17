# Save-game sections written and read by Content.dll (story, missions, player state)

When the player saves in single player, the `Story` controller, the active story mission, the mission object, the random-mission controller and the Content player state each append their own sections to the `.fl` save file; on load the same classes re-read them. This document lists the sections, keys and their exact formats as the code writes and reads them. The surrounding save-file container and the sections written by the server/common layer (`[Player]`, `[Group]`, `[Time]`, ...) are described in [saves-and-initial-world.md](saves-and-initial-world.md).

## File location / how it is found

The `.fl` save game is INI text in the user's save directory (see [saves-and-initial-world.md](saves-and-initial-world.md)); it is passed to the controllers as a `FILE*` on save (message `PlayerSave`, only when `pub::SinglePlayer()` is true) and as an open, rewindable `INI_Reader*` on load (message `PlayerLoad`). Each loader first remembers the reader state (`get_state`), rewinds (`reset`), scans the whole file, and restores the state afterwards, so section order in the file does not matter to them. Parsing is by `INI_Reader` (see [parsers.md](parsers.md)).


## `[StoryInfo]`

Written and read by the `Story` controller ([Story.cpp:331](../../src/Content/Story.cpp#L331), [Story.cpp:345](../../src/Content/Story.cpp#L345)).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `ship_bought` | 0 | bool (`true`/`false`) | false | The player has bought a new ship (reputation hack flag). | [L399](../../src/Content/Story.cpp#L399) |
| `Mission` | 0: controller name<br>1: path (only `GenericMission`) | str | `No_Mission` | Name of the story mission controller to create on load: `Mission_01a`, `Mission_01b`, `Mission_02` .. `Mission_13`, `GenericMission`, or `No_Mission` (no mission). For `GenericMission` value 1 is the mission script path (see [missions.md](missions.md#file-location--how-it-is-found)). **The writer emits only value 0**, so a saved `GenericMission` loses its path. The name is not validated; an unknown name creates no controller. | [L362](../../src/Content/Story.cpp#L362) |
| `MissionNum` | 0: story stage number | int | 0 | Current story stage (0..42; see [stage names](missions-support.md#story-stage-names)). Triggers sending the `PlayerLoad` and `MissionStart` messages to the story mission. Written only if a mission is active or the stage is not 0. | [L376](../../src/Content/Story.cpp#L376) |
| `delta_worth` | 0 | float | -1.0 | Cash+asset worth needed before the next story mission starts (`-1` = none). | [L394](../../src/Content/Story.cpp#L394) |
| `debug` | 0 | bool | false | Story debug flag. | [L403](../../src/Content/Story.cpp#L403) |

Written format: `[StoryInfo]`, `ship_bought = true|false`, optional `Mission = <name>`, `MissionNum = <n>`, `delta_worth = <float>`, then `debug = 0|1`.

## `[Mission01aSave]`, `[Mission01bSave]`

Only these two story missions write a section of their own ([StoryMissions.cpp:136](../../src/Content/StoryMissions.cpp#L136), [StoryMissions.cpp:252](../../src/Content/StoryMissions.cpp#L252)); the other missions rely on `[MissionState]`.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `MissionStateNum` | 0 | int | - | The mission's state (`FAIL`/`SUCCEED`/`NO_STATE` codes). In `Mission_01a` it is written only after the player entered the first base and, on load, sets the current and previous state and re-sends objectives unless it is 3. In `Mission_01b` it is read after the script re-creates the post-acceptance state. | [Mission01a L157](../../src/Content/StoryMissions.cpp#L157), [Mission01b L296](../../src/Content/StoryMissions.cpp#L296) |

## `[mPlayer]`

Content player state ([PlayerState.cpp:259](../../src/Content/PlayerState.cpp#L259) writes, [Support.cpp:284](../../src/Content/Support.cpp#L284) reads).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `locked_gate` | 0: gate id | uint | none | Registers a gate lock for this player (repeatable). | [Support.cpp:299](../../src/Content/Support.cpp#L299) |
| `vnpc` | 0..3 | uint | none | A virtual NPC record `base id, npc id, ?, ?` (read as four unsigned numbers; the writer appends `; <base nickname>, <npc name>` as a comment after the numbers). | [Support.cpp:304](../../src/Content/Support.cpp#L304) |
| `rumor` | 0, 1 | uint | none | A rumor the player has heard: two unsigned numbers (roles not named in the source). | [Support.cpp:333](../../src/Content/Support.cpp#L333) |
| `can_dock` | 0 | bool | per-state default | Player docking allowed (`Act_PlayerCanDock`). | [Support.cpp:313](../../src/Content/Support.cpp#L313) |
| `dock_exception` | 0: object id | uint | none | Exception to `can_dock` (repeatable). | [Support.cpp:317](../../src/Content/Support.cpp#L317) |
| `can_tl` | 0 | bool | per-state default | Trade lanes allowed (`Act_PlayerCanTradelane`). | [Support.cpp:322](../../src/Content/Support.cpp#L322) |
| `tlr_exception` | 0, 1: ring ids | uint | none | Exception pair (repeatable). | [Support.cpp:326](../../src/Content/Support.cpp#L326) |
| `ship_type_killed` | 0: ship type id, 1: count | uint, int | none | Kill statistic per ship archetype (repeatable). | [Support.cpp:75](../../src/Content/Support.cpp#L75) |
| `rm_completed` / `rm_aborted` / `rm_failed` | 0: random-mission type (0..255), 1: count | uchar, int | none | Random-mission statistics per mission type (repeatable). | [Support.cpp:82](../../src/Content/Support.cpp#L82) |
| `total_cash_earned` | 0 | float | 0 | Statistic. | [Support.cpp:103](../../src/Content/Support.cpp#L103) |
| `total_time_played` | 0 | float | 0 | Statistic (seconds). | [Support.cpp:108](../../src/Content/Support.cpp#L108) |
| `sys_visited` / `base_visited` / `holes_visited` | 0: id | uint | none | Visited system / base / jump hole ids; duplicates are ignored (repeatable). | [Support.cpp:113](../../src/Content/Support.cpp#L113) |

Unknown keys of `[mPlayer]` are handed to the statistics reader and ignored. After loading, the player rank and "money needed for next rank" are recalculated from the toughness table ([missions-support.md](missions-support.md#ptoughini)).

## `[locked_gates]`

Written by `SaveContentGateLocks` and read by `LoadContentGateLocks` ([SpaceSupport.cpp:1312](../../src/Content/SpaceSupport.cpp#L1312), [SpaceSupport.cpp:1335](../../src/Content/SpaceSupport.cpp#L1335)).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `locked_gate` | 0: gate id | uint | none | Gate locked for everybody (registered for player 0). | [L1320](../../src/Content/SpaceSupport.cpp#L1320) |
| `npc_locked_gate` | 0: gate id | uint | none | Gate locked for NPCs. | [L1325](../../src/Content/SpaceSupport.cpp#L1325) |

The section is only written when at least one lock exists.

## `[MissionState]` and the script's state

Written by `FMissionScript::SaveMissionState` ([MissionScript.cpp:1696](../../src/Content/MissionScript.cpp#L1696)) between `[MissionState]` and `[BStoryMissionDone]` ([MissionScript.cpp:1058](../../src/Content/MissionScript.cpp#L1058)); read by `LoadMissionState` ([MissionScript.cpp:1751](../../src/Content/MissionScript.cpp#L1751)). The loader stops at the `[BStoryMissionDone]` header. All keys are in the one `[MissionState]` section.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `mission_accepted` | 0 | bool/int (`1`/`0` written) | - | Whether the player has this mission as his live mission; `true` on load re-joins the mission as a group and notifies the story. | [L1808](../../src/Content/MissionScript.cpp#L1808) |
| `att_clamp` | 0 | bool (`true`/`false`) | - | Random-population attack clamp. | [L1825](../../src/Content/MissionScript.cpp#L1825) |
| `tradelane_attacks` | 0 | bool | - | Trade-lane attacks enabled. | [L1831](../../src/Content/MissionScript.cpp#L1831) |
| `scan_clamp` | 0 | bool | - | Scan clamp. | [L1836](../../src/Content/MissionScript.cpp#L1836) |
| `gcs_clamp` | 0 | bool | - | GCS clamp. | [L1841](../../src/Content/MissionScript.cpp#L1841) |
| `random_pop` | 0 | bool | - | Random population enabled. | [L1846](../../src/Content/MissionScript.cpp#L1846) |
| `hostile_clamp` | 0 | bool | - | Hostile clamp (written with a trailing space: `hostile_clamp = false `). | [L1851](../../src/Content/MissionScript.cpp#L1851) |
| `msn_offer` | 0: string id | int | - | Mission offer text id; applied only when non-zero. | [L1859](../../src/Content/MissionScript.cpp#L1859) |
| `msn_title` | 0: string id | int | - | Mission title text id; applied only when non-zero. | [L1864](../../src/Content/MissionScript.cpp#L1864) |
| `nnpath` | 0: travel string id, 1: target string id, 2,3,4: x,y,z, 5: label, 6: system id | int, int, float, str, uint | - | The active NN path (`Act_NNPath`); written only while active. Hides all objectives when read. Value 5 is applied when non-empty. | [L1869](../../src/Content/MissionScript.cpp#L1869) |
| `rtc` | 0: script path, 1: repeatable | str (127), bool | - | An RTC script that was running; on load the first `rtc` line releases the scripts started by the mission constructor, then each line is queued as a not-yet-started script with its repeatable flag. One line is written per unfinished script. | [L1766](../../src/Content/MissionScript.cpp#L1766) |
| `ambi_scene` | 0: path, 1: room id, 2: base id | str, uint, uint | - | An ambient scene started by `Act_AddAmbient`. | [L1783](../../src/Content/MissionScript.cpp#L1783) |
| `friendly_fire_excused` | 0: label | str (47) | - | A label from `Act_DisableFriendlyFire` (repeatable). | [L1800](../../src/Content/MissionScript.cpp#L1800) |
| `story_cue` | - | int | - | **Written but never read**: the loader has no handler for it. | [L1737](../../src/Content/MissionScript.cpp#L1737) |

The `[MissionState]` loader also collects the `[NNObjective]` sections that follow it (only after a `[MissionState]` header was seen), clearing the objective list first; their grammar is the same as in mission scripts ([missions.md](missions.md#nnobjective)) with numeric `nickname`/`state`.

### `[TriggerSave]`

One section per active trigger, or a single section when `Act_Save` is pending. Written at [MissionScript.cpp:1718](../../src/Content/MissionScript.cpp#L1718) and [L1742](../../src/Content/MissionScript.cpp#L1742); read at [L1889](../../src/Content/MissionScript.cpp#L1889).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `trigger` | 0: trigger id (numeric `CreateID`) | uint | - | The trigger that was active. The first `trigger` line of the file discards the script's initial active-trigger set; each one is then activated after the load. Following lines (in the same section) are handed to that trigger's condition `Load` function. | [L1893](../../src/Content/MissionScript.cpp#L1893) |
| *condition state* | see [missions-conditions.md](missions-conditions.md) | | | One line written by the active condition: `Cnd_Timer = <elapsed>`, `Cnd_Destroyed = <count>`, `Cnd_DistShip = <timer>, <distance>`, `Cnd_DistVec = ...`, `Cnd_DistVecLbl = <timer>`, `Cnd_LootAcquired = <count>`, `Cnd_ProjHit = <count>`, `Cnd_ProjHitShipToLbl = <count>`, `Cnd_TetherBroke = <a>, <b>`, `Cnd_NPCSystemEnter = <label>,<id>, ...`, `Cnd_NPCSystemExit = ...`. Conditions not listed write nothing. | |

The `trigger` value is read three times with `get_value_int(0)` (each read parses the same string); a numeric id above `INT_MAX` is therefore read through the MSVCRT-compatible `atoi`.

## Mission object sections (ships, solars, vibes)

`FMissionObject::SaveMission` ([MissionSave.cpp:700](../../src/Content/MissionSave.cpp#L700)) writes a snapshot of the live mission; `LoadMissionSave` ([MissionSave.cpp:889](../../src/Content/MissionSave.cpp#L889)) recreates the respawn tables from it. For every ship the writer emits first an optional `[MetaBehavior]` (the guide's source line, written verbatim as a format string) or `[MObjective]` (the directives' source lines, see [missions-objlist.md](missions-objlist.md)), then `[MsnShipSave]`; per group `[MsnWingSave]`; per random-encounter group `[MsnRandEncSave]`.

| Section | Key / entry | Values | Type | Meaning | Source |
|---|---|---|---|---|---|
| `[MsnShipSave]` | `nickname` | 0 | str (47) | Ship name. | [L922](../../src/Content/MissionSave.cpp#L922) |
| | `npc` | 0: `[NPC]` id | uint | Character record id (numeric hash). | [L924](../../src/Content/MissionSave.cpp#L924) |
| | `random_name`, `jumper`, `original_jump_flag` | 0 | bool (`0`/`1`) | Flags. | [L926](../../src/Content/MissionSave.cpp#L926) |
| | `label` | 0: label id | int | One label hash per line (repeatable). | [L932](../../src/Content/MissionSave.cpp#L932) |
| | `ol_priority` | 0 | int | Directive priority. | [L934](../../src/Content/MissionSave.cpp#L934) |
| | `hit_pts` | 0 | int | Current hit points. | [L936](../../src/Content/MissionSave.cpp#L936) |
| | `radius` | 0 | float | Ship radius. | [L938](../../src/Content/MissionSave.cpp#L938) |
| `[MsnWingSave]` | `nickname` | 0 | str | Group name; takes the `[MsnShipSave]` sections that precede it. | [L1029](../../src/Content/MissionSave.cpp#L1029) |
| | `position` | 0,1,2 | float | Position (written when no arrival object). | [L1033](../../src/Content/MissionSave.cpp#L1033) |
| | `orientation` | 0..8 | float | 3x3 rotation matrix, row-major. | [L1039](../../src/Content/MissionSave.cpp#L1039) |
| | `arrival_obj` | 0 | uint | Arrival object id (written instead of position for jumpers still outside). | [L1043](../../src/Content/MissionSave.cpp#L1043) |
| | `formation` | 0 | uint | Formation id hash. | [L1047](../../src/Content/MissionSave.cpp#L1047) |
| | `ship` | 0 | str | Written (one per ship) but **not read** by the loader. | [L749](../../src/Content/MissionSave.cpp#L749) |
| `[MsnRandEncSave]` | `rand_enc_owner` | 0 | uint | Owning `[MsnRandEnc]` id. | [L961](../../src/Content/MissionSave.cpp#L961) |
| | `position`, `orientation`, `ol_priority`, `hit_pts` | as above | | Leader placement. | [L965](../../src/Content/MissionSave.cpp#L965) |
| | `num_ships` | 0 | int | Ship count in the group. | [L983](../../src/Content/MissionSave.cpp#L983) |
| | `ship_type` | 0...: class ids, then `END` | uint | Up to 32 ship class ids per line; reading stops at the first value that is exactly `END`. One line per choice. | [L987](../../src/Content/MissionSave.cpp#L987) |
| | `names` | 0...: names, then `END` | str (47) | Up to 32 ship names, terminated by `END`. | [L1005](../../src/Content/MissionSave.cpp#L1005) |
| `[MsnSolarSave]` | `solar_name` | 0 | str (47) | A spawned `[MsnSolar]` to respawn (repeatable). | [L1058](../../src/Content/MissionSave.cpp#L1058) |
| `[MarkObj]` | `name` | 0 | str (47) | An object marked by `Act_MarkObj` (repeatable). | [L1070](../../src/Content/MissionSave.cpp#L1070) |
| `[Invulnerables]` | `invulnerable` | 0: name, 1: duration, 2: flag | str, float, bool | Written `name, %f, 0|1`. | [L1082](../../src/Content/MissionSave.cpp#L1082) |
| `[Decloaked]` | `decloaked` | 0 | str (47) | A decloaked object name (written as `decloaked= name`). | [L1098](../../src/Content/MissionSave.cpp#L1098) |
| `[MsnVibeInfo]` | `vibe` | 0: name A or `empty`, 1: name B or `empty`, 2: rep A, 3: rep B, 4: attitude | str, str, int, int, float | One attitude relation between two parties; `empty` means the party is a reputation group (rep A / rep B) instead of an object. | [L1110](../../src/Content/MissionSave.cpp#L1110) |
| `[RandomEncounter]` | `nickname`, `enabled`, `encounter_type` and the encounter parameter keys | | | Written by `SaveRandEncs` as `nickname = <name>`, `enabled = <0|1>, <count>`, then the encounter parameter dump; read by `LoadRandEncsSave`: `enabled` updates every `[MsnRandEnc]` of that name (not the name `RandomMission`); `encounter_type` (`TL_Attack` / `SP_Attack`, case-sensitive here) creates a new encounter only for the nickname `RandomMission`. Parameter keys: see [ai-and-encounters.md](ai-and-encounters.md). | [L1122](../../src/Content/MissionSave.cpp#L1122) |
| `[NNObjective]` | as in mission scripts | | | Objectives, written by `SaveNNObjectives` at the end of `SaveMission` ([MissionObject.cpp:915](../../src/Content/MissionObject.cpp#L915)): `nickname = <numeric id>`, `type = <n>, <values>`, `state = <n>`, `hidden = 0|1`. | [MissionObject.cpp:839](../../src/Content/MissionObject.cpp#L839) |

## `[RandomMission]`

Written by the random-mission controller (`DestroyMission`) and read by the Content player state; see [random-missions.md](random-missions.md#randommission-save-section).
