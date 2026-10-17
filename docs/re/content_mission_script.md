# Story mission machine (`[Trigger]`, `Cnd_*`, `Act_*`)

The campaign story missions (`Mission_01a` … `Mission_13`, `GenericMission`) are INI scripts
`DATA\MISSIONS\M01A\m01a.ini` … `M13\m13.ini`, executed by one and the same trigger engine.
This document describes the script format and the engine's structure (TU21/TU38 in [content.md](content.md)); the arguments
of each opcode are in [content_mission_opcodes.md](content_mission_opcodes.md). Code:
`src/Content/MissionScript.*`, `MissionCnd.*`, `MissionAct.*`, `MissionObject.*`, `MissionSave.cpp`,
`MissionDialogs.*`, `RtcScripts.*`.

## Mission controller

The mission class is a descendant of `IDLL` with a 31-slot table: slot 0 — receiving a controller message (if the
completion flags are set, it calls `pub::Controller::Destroy`), slot 1 — destructor, 2–6 — lookup and
creation of mission objects by label/id, 18 — binding the player, 21 — the **mission message dispatcher**
(heartbeat, save `4`, load `5`, `0x1000` — activation of the initial triggers, player events),
25 — INI parsing, 26 — parsing the `State` token, 23/24 — success/failure messages
(`pub::Player::DisplayMissionMessage`), 28 — broadcasting `0x100F` to the triggers, 30 — tracking the
mission state and the callback into `Story`. The descendants (`Mission_*`) differ in the destructor and, for `01a`/`01b`,
in the slot 21 message handler. Each constructor opens its own INI (`Missions\M0N\m0N.ini`,
`GenericMission` — the path from `CreateParms`) through `pub::GetDataPath` + `INI_Reader` and calls the parser.

`DestroyMission` (a random mission) shares only the base controller with the story missions
(slot 0, the object maps); it has no triggers — it has a `StateMachineGroup` and a generator.

## INI parsing

The sections of `m0N.ini` are processed in this order:

1. world object sections (in the `MissionObject` base): `[MsnLoot]`, `[MsnSolar]`, `[MsnShip]` /
   `[MsnFormation]`, `[MsnRandEnc]`, `[Mission]` (`mission_title`, `mission_offer`, `reward`,
   `npc_ship_file`), `[ObjList]`, `[MetaBehavior]`, `[NNObjective]`, `[Dialog]`;
2. `[Trigger]` — triggers;
3. `[NPC]` — the mission's named NPCs;
4. `[Dialog]` — dialogue lines (`line = …`, the speaker `Player` is the player).

Loading a save (message 5) additionally reads `[MissionState]` (`rtc`, `ambi_scene`),
`[TriggerSave]`, `[NNObjective]`, `[BStoryMissionDone]`.

### The `[Trigger]` section

```
[Trigger]
nickname = general_triggers
system   = Li01
Cnd_True = no_params
Act_ActTrig = king_dies
Act_NagDistLeaving = std, escort, escort, 21970, 4000, NAG_IN_COMBAT

[Trigger]
nickname = king_dies
system   = Li01
Cnd_Destroyed = escort, 1, EXPLODE
Act_ChangeState = FAIL, 21960
```

- There is no `cond` key: the condition is a `Cnd_*` key (one per trigger), the actions are `Act_*` keys (any number,
  in the order written). The other keys: `nickname` (id = `CreateID`), `system`, `repeatable`,
  `InitState`. Key names are compared case-insensitively (`Act_Ethercomm` ≡ `Act_EtherComm`);
  unknown keys are silently skipped.
- Triggers are kept in a map by id; the active ones are in a list. Trigger state: `ACTIVE`, `INACTIVE`
  (the default), `COMPLETE`. Message `0x1000` activates the triggers with `InitState = ACTIVE`;
  `Act_ActTrig`/`Act_DeactTrig` enable and disable a trigger by nickname; `Cnd_WatchTrigger`
  waits for another trigger.
- A condition subscribes to a set of dispatcher message ids (for example `Cnd_Destroyed` — to the destruction
  of an object). When `Matches` is true, the `Act_*` list is executed; repeatability is set by the key
  `repeatable`. Deferred actions (`Act_ActTrig` with a delay, `Cnd_Timer`) tick on message `0x20`.
- The `State` value of `Act_ChangeState` is the token `FAIL`, `SUCCEED` or `NO_STATE`
  (the numbers 0, 1, 2; anything else is `-1`). It changes `MissionStateNum`; slot 30 shows the
  success/failure message according to the value and reports the completion code to `Story`.
- There are 44 `Cnd_*` and 85 `Act_*` opcodes; each is a separate class (`MissionCnd.*`, `MissionAct.*`) with
  string parsing (`Parse`) and execution.

## Chapter numbers

`MissionNum` (a `Story` field, 0…42): the mission constructor writes two numbers — the current number and the "completion
code" that `Story` receives on success:

| Mission | Number | Code | Mission | Number | Code |
|---|---:|---:|---|---:|---:|
| `Mission_01a` | 1 | 2 | `Mission_07` | 22 | 23 |
| `Mission_01b` | 4 | 4 | `Mission_08` | 25 | 26 |
| `Mission_02` | 7 | 8 | `Mission_09` | 28 | 29 |
| `Mission_03` | 10 | 11 | `Mission_10` | 31 | 32 |
| `Mission_04` | 13 | 14 | `Mission_11` | 34 | 35 |
| `Mission_05` | 16 | 17 | `Mission_12` | 37 | 38 |
| `Mission_06` | 19 | 20 | `Mission_13` | 40 | 41 |

The `Story` message handler picks the continuation by the completion code: code 2 (`01a`) → chapters 3 and 4
and an immediate launch of `Mission_01b`; code 4 (`01b`) → "wait for money" (chapter 5); codes 8, 11, 14, 17, 26 →
the next number (9, 12, 15, 18, 27) waiting for money; codes 20, 29, 32, 35, 38 → the next chapter
and an immediate launch of `Mission_07`, `10`, `11`, `12`, `13` respectively; code 23 → chapter 24 (wait);
code 41 → chapter 42 (the campaign is over). "Money" is the `delta_worth` threshold from a table indexed by the number
(indices 5, 9, 12, 15, 18, 24, 27 — 3500, 5200, 7000, 10000, 16000, 36000, 30000; indices 21, 30,
33, 36, 39, 42 — 0); the `Story` heartbeat launches `Mission_02` … `Mission_13` from the "money" numbers
5, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39 (see [content.md](content.md)).
Within a single mission the script "stages" are trigger nicknames and `MissionStateNum`, not `MissionNum`
values.
