# Mission script files (Content.dll)

This document describes the INI-style mission scripts that `Content.dll` loads when a story mission, a "GenericMission", or a scripted scene starts: the `[Mission]` header, the mission-local NPC/ship/solar/loot tables, objectives, dialogs, meta-behaviors and the `[Trigger]` engine. Trigger conditions (`Cnd_*`), trigger actions (`Act_*`) and the `[ObjList]` directives have their own files (see [Related files](#related-files)).

Everything below was checked against the ported source in `src/Content/`. Where the code does not validate a value this is stated; allowed-value lists are only given where the code compares against them.

## File location / how it is found

* Mission script paths are **relative to the game Data directory** (the `[Freelancer] data path` directory from `freelancer.ini`, see [freelancer-ini.md](freelancer-ini.md)). They are opened with `OpenContentDataIni(RelativePath, Reader)` ([ContentDataPath.cpp:17](../../src/Content/ContentDataPath.cpp#L17)): `pub::GetDataPath()->GetFileSysPath(DataDir)`, then `_makepath(FullPath, nullptr, DataDir, RelativePath, nullptr)` and `INI_Reader::open(FullPath, false)`. A missing file is not an error at this level; several callers ignore the return value and simply load nothing.
* Story missions have hard-coded paths ([StoryMissions.cpp](../../src/Content/StoryMissions.cpp)):

  | Mission controller | Path (relative to Data) |
  |---|---|
  | `Mission_01a` | `Missions\M01a\m01a.ini` ([L100](../../src/Content/StoryMissions.cpp#L100)) |
  | `Mission_01b` | `Missions\M01b\m01b.ini` ([L201](../../src/Content/StoryMissions.cpp#L201)) |
  | `Mission_02` ... `Mission_13` | `Missions\M02\m02.ini` ... `Missions\M13\m13.ini` ([L312-L435](../../src/Content/StoryMissions.cpp#L312)) |
  | `GenericMission` | taken from the save game: the second value of `Mission = GenericMission, <path>` in `[StoryInfo]` ([Story.cpp:368](../../src/Content/Story.cpp#L368)); the path is passed through unchanged and is not validated |

* RTC scripts (`Act_AddRTC` targets, `missions\m01a\M001a_s006x_Li01_02_nrml.ini`, `missions\m01a\rtc_success.ini`) are a different file type, see [missions-support.md](missions-support.md#rtc-scripts-characterencounter-files).
* Parsing is done by `INI_Reader` (Common.dll, text or BINI): keys and headers are case-insensitive, repeated keys are all delivered in file order, values are comma separated and indexed from 0. See [parsers.md](parsers.md). Mission scripts are read **positionally** (`get_value_*(index)`), not by name, for every trigger entry.

## Conventions used in the tables

* **Index** `0`, `1`, ... is the position of the comma-separated value on the line. `empty(i)` means `is_value_empty(i)` (value absent or blank).
* **Types**: `int` = `get_value_int` (an `atoi`-style parse; `12.7` becomes `12`), `float` = `get_value_float` (the engine's `ParseNumber`; see [parsers.md](parsers.md)), `bool` = `get_value_bool` (exactly `true`/`1`/`false`/`0`, anything else logs "Invalid bool value" and reads as false), `str` = `get_value_string`, `id` = a string hashed with `CreateID` (case-insensitive hash), `nick` = a string resolved by `pub::GetNicknameId` (same hash, used for solar/object nicknames defined elsewhere).
* **Label** = a mission-local name. A label is resolved at run time (`GetLabelObjectId` / `GetLabelShipId`) against, in order of use: the literal `Player`, `[MsnShip]`/`[MsnFormation]` ship nicknames, `[MsnSolar]` nicknames, `[MsnLoot]` nicknames, and every extra `label =` value attached to those records. Several records may share one label; actions and conditions with a group label then apply to every member.
* **TString<N>** = fixed buffers: a name longer than `N-1` characters is silently truncated (`TString<0x30>` = 47 chars, `TString<0x20>` = 31, `TString<0x40>` = 63, `TString<0x80>` = 127).
* "Default" is the value the object has when the key is absent (constructor/static initializer); the INI parser has no default mechanism of its own.

## Load order inside a mission file

`FMissionScript::LoadMission` ([MissionScript.cpp:954](../../src/Content/MissionScript.cpp#L954)) first calls the base `FMissionObject::LoadMission` ([MissionObject.cpp:4139](../../src/Content/MissionObject.cpp#L4139)), which walks every header in file order and handles the headers in the table below; then it resets the reader and walks the file a second time for `[Trigger]`, `[NPC]` and `[Dialog]` (costume pre-registration). Unknown headers are skipped silently by both passes.

| Header | Pass | Meaning | Section |
|---|---|---|---|
| `[Mission]` | 1 | title/offer text ids, reward, NPC ship archetype file | [below](#mission) |
| `[MsnLoot]` | 1 | a loot crate the mission can spawn | [below](#msnloot) |
| `[MsnSolar]` | 1 | a mission solar object | [below](#msnsolar) |
| `[MsnShip]`, `[MsnFormation]` | 1 | a mission ship or formation | [below](#msnship--msnformation) |
| `[MsnRandEnc]` | 1 | a random attack encounter owned by the mission | [below](#msnrandenc) |
| `[ObjList]` | 1 | a named list of AI directives | [missions-objlist.md](missions-objlist.md) |
| `[MetaBehavior]` | 1 | a named list of guide behaviors | [below](#metabehavior) |
| `[Dialog]` | 1, 2 | scripted comm dialog | [below](#dialog) |
| `[NNObjective]` | 1 | a navigation-network (NN) objective text entry | [below](#nnobjective) |
| `[NPC]` | 2 | mission NPC character record | [below](#npc) |
| `[Trigger]` | 2 | one trigger | [below](#trigger) |

Sections of the **save game** that these classes also read/write (`[MissionState]`, `[TriggerSave]`, `[MsnShipSave]`, ...) are in [missions-saves.md](missions-saves.md).

## `[Mission]`

Read by `LoadMissionHeaderValues` ([MissionObject.cpp:3729](../../src/Content/MissionObject.cpp#L3729)). May appear several times; each occurrence updates the same mission object.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `mission_title` | 0: string-table id | int | none (text unset) | Builds the mission title `FmtStr` (`begin_mad_lib(id)`); not validated. | [MissionObject.cpp:3733](../../src/Content/MissionObject.cpp#L3733) |
| `mission_offer` | 0: string-table id | int | none | Builds the mission offer text. | [MissionObject.cpp:3738](../../src/Content/MissionObject.cpp#L3738) |
| `reward` | 0: money | int | 0 | Cash reward stored in the mission; split over the group when the mission succeeds. | [MissionObject.cpp:3743](../../src/Content/MissionObject.cpp#L3743) |
| `npc_ship_file` | 0: path relative to Data | str | none | Opens that file with `OpenContentDataIni` and loads `[NPCShipArch]` records into the mission-local NPC ship table (same format as `missions\npcships.ini`, see [ai-and-encounters.md](ai-and-encounters.md)). The open result is not tested. | [MissionObject.cpp:3747](../../src/Content/MissionObject.cpp#L3747) |

## `[NPC]`

Mission character record (name, appearance, voice, ship archetype). Read by `LoadMsnNpc` ([NpcShips.cpp:356](../../src/Content/NpcShips.cpp#L356)) from pass 2; a record is stored under the id of its `nickname` (unique insert: a repeated nickname keeps the first record). The same keys without `space_costume`/`npc_ship_arch` are used by `missions\specific_npc.ini` (see [missions-support.md](missions-support.md#specific_npcini)).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0: name | id | empty (record is then ignored by the loader's return value) | Key of the record; also the name `[MsnShip] npc =` refers to. | [NpcShips.cpp:367](../../src/Content/NpcShips.cpp#L367) |
| `space_costume` | 0: head part, 1: body part, 2...: accessories | str | zeros | Costume shown in space comms. Names are resolved by `pub::GF::GetBodyPart` (head with type 1, body with type 0) and `pub::GF::GetAccessory`; accessories are read until the first empty value (no bound check). | [NpcShips.cpp:373](../../src/Content/NpcShips.cpp#L373) |
| `voice` | 0: voice nickname | str | 0 | `pub::GetVoiceID`; names longer than 79 characters are truncated. | [NpcShips.cpp:385](../../src/Content/NpcShips.cpp#L385) |
| `npc_ship_arch` | 0: `[NPCShipArch]` nickname | id | cleared | Ship/pilot template from `npcships.ini` (or the mission's `npc_ship_file`). | [NpcShips.cpp:399](../../src/Content/NpcShips.cpp#L399) |
| `affiliation` | 0: faction nickname | str | `0xFFFFFFFF` | `pub::Reputation::GetReputationGroup`. | [NpcShips.cpp:403](../../src/Content/NpcShips.cpp#L403) |
| `individual_name` | 0: string-table id | int | 0 | Display name id of the character. | [NpcShips.cpp:407](../../src/Content/NpcShips.cpp#L407) |

## `[MsnLoot]`

One loot crate definition. Each header adds a record; keys are applied to the newest record by `LoadMissionLootValues` ([MissionObject.cpp:3541](../../src/Content/MissionObject.cpp#L3541)). Spawned by `Act_SpawnLoot`.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0: name | str (47 chars) | empty | Record name; its `CreateID` is also added to the record's label list. | [L3545](../../src/Content/MissionObject.cpp#L3545) |
| `archetype` | 0: equipment nickname | str | 0 | `pub::GetEquipmentID`. | [L3552](../../src/Content/MissionObject.cpp#L3552) |
| `string_id` | 0: id | int | 0 | Name string id of the crate. | [L3560](../../src/Content/MissionObject.cpp#L3560) |
| `position` | 0,1,2: x,y,z | float | 0,0,0 | World position. | [L3564](../../src/Content/MissionObject.cpp#L3564) |
| `rel_pos_obj` | 0: label | str (47) | empty | Position is relative to this object; setting the key also sets the relative-position flag (even for an empty value). | [L3570](../../src/Content/MissionObject.cpp#L3570) |
| `rel_pos_offset` | 0,1,2: x,y,z | float | 0,0,0 | Offset from `rel_pos_obj`. | [L3576](../../src/Content/MissionObject.cpp#L3576) |
| `orientation` | 0..3: W,X,Y,Z quaternion | float | identity | Normalised before use (a zero quaternion is not guarded). | [L3582](../../src/Content/MissionObject.cpp#L3582) |
| `velocity` | 0,1,2 | float | 0 | Initial linear velocity. | [L3589](../../src/Content/MissionObject.cpp#L3589) |
| `angular_velocity` | 0,1,2 | float | 0 | Initial angular velocity. | [L3595](../../src/Content/MissionObject.cpp#L3595) |
| `equip_amount` | 0: count | int | 0 | Units in the crate. | [L3601](../../src/Content/MissionObject.cpp#L3601) |
| `health` | 0: scale | float | 0 | Hit-point scale. | [L3605](../../src/Content/MissionObject.cpp#L3605) |
| `label` | 0: label | id | none | Adds a label (may repeat). | [L3609](../../src/Content/MissionObject.cpp#L3609) |
| `can_jettison` | 0 | bool | record flag "cannot jettison" = 0 (i.e. can be jettisoned) | `false` marks the crate as not jettisonable. | [L3613](../../src/Content/MissionObject.cpp#L3613) |

The loot record constructor also sets "can AI tractor" = 1 and "mission loot" = 0 ([MissionObject.cpp:3937](../../src/Content/MissionObject.cpp#L3937)); neither is readable from the INI.

## `[MsnSolar]`

A solar object the mission places in space. Parsed by `LoadMissionSolarValues` ([MissionObject.cpp:3646](../../src/Content/MissionObject.cpp#L3646)); spawned by `Act_SpawnSolar` or at mission start.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0: name | str (47) | empty | Name; its hash is also added as a label. | [L3650](../../src/Content/MissionObject.cpp#L3650) |
| `position` | 0,1,2 | float | 0,0,0 | Position. | [L3657](../../src/Content/MissionObject.cpp#L3657) |
| `orientation` | 0..3: W,X,Y,Z | float | identity | Quaternion, normalised. | [L3667](../../src/Content/MissionObject.cpp#L3667) |
| `faction` | 0: faction nickname | str | `0xFFFFFFFF` | `pub::Reputation::GetReputationGroup`. | [L3671](../../src/Content/MissionObject.cpp#L3671) |
| `string_id` | 0: id | int | none | Display name string id. | [L3675](../../src/Content/MissionObject.cpp#L3675) |
| `pilot` | 0: pilot personality nickname | id | not set | Looked up in the pilot personality table (`pilots_*.ini`); the "has pilot" flag is set only when the lookup succeeds. | [L3680](../../src/Content/MissionObject.cpp#L3680) |
| `archetype` | 0: solar archetype nickname | nick | 0 | `pub::GetNicknameId`. | [L3687](../../src/Content/MissionObject.cpp#L3687) |
| `loadout` | 0: loadout nickname | str | 0 | `pub::GetLoadoutID`. | [L3691](../../src/Content/MissionObject.cpp#L3691) |
| `system` | 0: system nickname | str | 0 | `pub::GetSystemID`. | [L3695](../../src/Content/MissionObject.cpp#L3695) |
| `base` | 0: base nickname | str | 0 | `pub::GetBaseID`. | [L3699](../../src/Content/MissionObject.cpp#L3699) |
| `voice` | 0: voice nickname | str | 0 | `pub::GetVoiceID`. | [L3703](../../src/Content/MissionObject.cpp#L3703) |
| `radius` | 0 | float | 150.0 | Radius used for placement/visibility checks. | [L3707](../../src/Content/MissionObject.cpp#L3707) |
| `label` | 0: label | id | none | Extra label (may repeat). | [L3711](../../src/Content/MissionObject.cpp#L3711) |
| `costume` | 0: head, 1: body, 2...: accessories | str | zeros | Same grammar as `[NPC] space_costume`. | [L3715](../../src/Content/MissionObject.cpp#L3715) |
| `visit` | 0 | str | 0 | Only the exact word `unknowable` (case-insensitive) has an effect: it sets the solar's visit flag to `0x80`. Anything else is ignored. | [L3719](../../src/Content/MissionObject.cpp#L3719) |

## `[MsnShip]` / `[MsnFormation]`

Both headers use the same parser (`LoadMissionShipValues`, [MissionObject.cpp:3764](../../src/Content/MissionObject.cpp#L3764)): each header creates a **group** record; ship-level keys write to a template ship slot that is the group's last ship. A `formation =` or `ship =` key *removes* that template slot and turns the group into a formation / reference group, after which later ship-level keys are not meaningful (the code does not guard this and writes through a null slot, as retail does; do not put ship-level keys after `ship =` / `formation =`).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0: name | str (47) | empty | Name of the group and of the template ship; its hash is added to the ship's labels. | [L3771](../../src/Content/MissionObject.cpp#L3771) |
| `npc` | 0: `[NPC]` nickname | id | cleared | Character record of the pilot. | [L3781](../../src/Content/MissionObject.cpp#L3781) |
| `random_name` | 0 | bool | false | Randomise the pilot name. | [L3785](../../src/Content/MissionObject.cpp#L3785) |
| `label` | 0: label | id | none | Adds a label (may repeat). | [L3789](../../src/Content/MissionObject.cpp#L3789) |
| `init_objectives` | 0: `[ObjList]` nickname | id | none | Initial directive list. Ignored if `init_metabehavior` was already set on this ship. | [L3793](../../src/Content/MissionObject.cpp#L3793) |
| `init_metabehavior` | 0: `[MetaBehavior]` nickname | id | none | Initial guide. Ignored if `init_objectives` was already set. | [L3799](../../src/Content/MissionObject.cpp#L3799) |
| `jumper` | 0 | bool | false | Ship arrives through a jump gate (also copied to the "original jump flag"). | [L3805](../../src/Content/MissionObject.cpp#L3805) |
| `cargo` | 0: good nickname, 1: count | str, float | none | `pub::GetGoodID`; an unknown good skips the whole line. Count is `ftol(float)`. May repeat (appends). | [L3811](../../src/Content/MissionObject.cpp#L3811) |
| `hit_points` | 0 | int | -1 (use archetype) | Sets both the start and the respawn hit points. | [L3824](../../src/Content/MissionObject.cpp#L3824) |
| `radius` | 0 | float | 7.0 | Ship radius used for spawn spacing. | [L3830](../../src/Content/MissionObject.cpp#L3830) |
| `system` | 0: system nickname | str | not set | Only stored if the system exists in the content system table. | [L3834](../../src/Content/MissionObject.cpp#L3834) |
| `position` | 0,1,2 | float | 0,0,0 | Spawn position of the group. | [L3846](../../src/Content/MissionObject.cpp#L3846) |
| `rel_pos` | 0: heading (degrees), 1: label, 2: range | float, str (47), float | heading 0, range 2500.0 | Spawn relative to another object; sets the relative-position flag. The heading is multiplied by pi/180. Value 2 only overrides the range when not empty. | [L3856](../../src/Content/MissionObject.cpp#L3856) |
| `orientation` | 0..3: W,X,Y,Z | float | identity | Sets the "has orientation" flag. | [L3867](../../src/Content/MissionObject.cpp#L3867) |
| `arrival_obj` | 0: label, 1: id | str (47), int | empty / -1 | Arrive through this object; also clears the radius of every ship slot already present. Value 1 only when not empty. | [L3877](../../src/Content/MissionObject.cpp#L3877) |
| `formation` | 0: formation nickname | id | `CreateID("transport_liberty")` | Formation from `formations.ini` (see [ai-and-encounters.md](ai-and-encounters.md)). Removes the template ship slot. | [L3888](../../src/Content/MissionObject.cpp#L3888) |
| `ship` | 0: nickname of an earlier `[MsnShip]` group | str (47) | none | Pulls the first ship of the named earlier group into this group and **deletes that earlier group**. A name that matches no group is ignored. Removes the template slot. | [L3899](../../src/Content/MissionObject.cpp#L3899) |

## `[MsnRandEnc]`

A random tradelane/space attack encounter owned by the mission. Keys read by `FMissionObject::LoadMission` ([MissionObject.cpp:4168](../../src/Content/MissionObject.cpp#L4168)).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0: name | str (31), id | empty | Name used by `Act_EnableEnc` / `Act_DisableEnc` and by the save game. | [L4176](../../src/Content/MissionObject.cpp#L4176) |
| `encounter_type` | 0: type | str | none | `TL_Attack` (case-insensitive; creates the `TLParams`/`TLPzr` objects) or `SP_Attack` (`SPParams`/`SPPzr`). Any other value keeps the previous objects (which are null on a fresh record, so the section then has no usable encounter). **The populator object reads all remaining keys of the section itself**, so `encounter_type` must come after `nickname`, and all keys that follow belong to the encounter parameter grammar documented in [ai-and-encounters.md](ai-and-encounters.md). | [L4182](../../src/Content/MissionObject.cpp#L4182) |

## `[NNObjective]`

One entry of the "navigation network" objective list shown in the mission journal. Read by `LoadNNObjective` ([MissionObject.cpp:839](../../src/Content/MissionObject.cpp#L839)); the same function loads these sections from the save game. The section **must** contain a valid `type` line; without one the section is dropped.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0: name or number | id | cleared | If value 0 parses (MSVCRT `atoi`) to a non-zero number it is used directly as the objective id (this is how saved games reload); otherwise `CreateID(name)`. Referenced by `Act_SetNNObj`, `Act_SetNNState`, `Act_SetNNHidden`. | [L851](../../src/Content/MissionObject.cpp#L851) |
| `type` | 0: type; 1...: type-specific | see below | none | Creates the objective and parses the remaining values. Value 0 is either a number (first char digit or `-`; `0` skips the line) or a word: `navmarker` = 2, `base` = 1, `ids` = 3, `rep_inst` = 4 (case-insensitive). Anything else skips the line. Types 5-8 exist only as numbers (written by save games; 7 and 8 parse nothing). A number outside 1-8 is **not validated**: no objective is created and the following `Parse` call goes through a null object. | [L859](../../src/Content/MissionObject.cpp#L859) |
| `state` | 0 | int or word | 0 (see note) | A number is the state/type code that is sent to the client; the words `ACTIVE` or `HIDDEN` set the state code to 10 and the hidden flag to false / true. Other words give 10 with the hidden flag unchanged. If `state` is missing the code stays 0 (retail leaves it uninitialised). | [L883](../../src/Content/MissionObject.cpp#L883) |
| `hidden` | 0 | bool | false | Hidden flag of the objective. | [L900](../../src/Content/MissionObject.cpp#L900) |

Type-specific values (value 0 is the type itself):

| `type` | Name | Values | Source |
|---|---|---|---|
| 1 | `base` | 1: base nickname or number (number if it starts with a digit or `-`; else `pub::GetBaseID`), 2: string-table id | [L495](../../src/Content/MissionObject.cpp#L495) |
| 2 | `navmarker` | 1: system nickname or number, 2: string id, 3: int (stored; role not identified in source), 4,5,6: x,y,z (float) | [L542](../../src/Content/MissionObject.cpp#L542) |
| 3 | `ids` | 1: string id | [L588](../../src/Content/MissionObject.cpp#L588) |
| 4 | `rep_inst` | 1: system, 2: string id, 3: int, 4,5,6: x,y,z, 7: label (47 chars) | [L632](../../src/Content/MissionObject.cpp#L632) |
| 5, 6 | (unnamed) | 1: string id, 2: system id (number), 3: rep instance (int), 4,5: ints (marker data), 6,7,8: floats. The three floats are stored in reverse order (value 8 first), so a save written by the game reloads with x and z swapped; this mirrors retail. | [L393](../../src/Content/MissionObject.cpp#L393) |
| 7, 8 | (unnamed) | no values are parsed | [L730](../../src/Content/MissionObject.cpp#L730) |

Positions in types 2 and 4 are `x = value 4`, `y = value 5`, `z = value 6`.

## `[Dialog]`

Scripted comm conversation, started by `Act_StartDialog`. Read by `FMissionDialog::Load` ([MissionDialogs.cpp:19](../../src/Content/MissionDialogs.cpp#L19)). The **first** value line of the section is read unconditionally and must be `nickname`; otherwise the whole section is skipped ([MissionObject.cpp:4252](../../src/Content/MissionObject.cpp#L4252)). A repeated dialog nickname keeps the first dialog.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0: name | id | n/a | Dialog id (`CreateID`), referenced by `Act_StartDialog`. Must be the first line. | [MissionObject.cpp:4259](../../src/Content/MissionObject.cpp#L4259) |
| `system` | 0: system nickname | str | none | System in which the dialog may run. If the player is in another system `Act_StartDialog` fails; without `system` the dialog does nothing. | [MissionDialogs.cpp:64](../../src/Content/MissionDialogs.cpp#L64) |
| `line` | 0: speaker label, 1: target label, 2: comm text nickname | str, str, nick | n/a | One line of the conversation. Lines are grouped by speaker (value 0 and value 1 may be `Player` or a mission name/label; at start time each speaker and target must resolve to a ship or solar in the dialog's system or `Act_StartDialog` fails). Every line remembers the comm id of the previous `line` in the file (0 for the first line of the section); a line whose predecessor id is 0 is a "first line", and the last first-line visited (speakers are visited in the dialog's internal speaker order) is spoken at once (0.5 s, or 1.0 s when the target is a player ship) while the other lines are queued on their speakers. | [MissionDialogs.cpp:24](../../src/Content/MissionDialogs.cpp#L24) |

Pass 2 ([MissionScript.cpp:980](../../src/Content/MissionScript.cpp#L980)) additionally scans every `line` whose value 1 is `Player` (case-insensitive) and registers the costume of the speaker (value 0) with the mission so it can be shown in the player's comm window.

## `[MetaBehavior]`

A named list of "guide" behaviors that can be handed to an NPC (`init_metabehavior =`, `Act_GiveMB`). Read by `LoadMetaBehavior` ([Guides.cpp:166](../../src/Content/Guides.cpp#L166)).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0: name | id | `CreateID("")` | Id of the list; a duplicate id keeps the first list. | [Guides.cpp:171](../../src/Content/Guides.cpp#L171) |
| `system` | - | - | - | Recognised and ignored. | [Guides.cpp:176](../../src/Content/Guides.cpp#L176) |
| `MB_GotoGuide` | see below | | | Creates a goto guide (type 5). Any other key name is ignored. The key name is tested with `stricmp`. | [Guides.cpp:181](../../src/Content/Guides.cpp#L181) |
| `MB_DockGuide` | see below | | | Creates a dock guide (type 6). | [Guides.cpp:186](../../src/Content/Guides.cpp#L186) |

`MB_GotoGuide` ([Guides.cpp:123](../../src/Content/Guides.cpp#L123)):

| Index | Type | Default | Meaning |
|---|---|---|---|
| 0 | str (47) | empty | Target object name. |
| 1,2,3 | float | 0 | First position x,y,z. |
| 4,5,6 | float | 0 | Destination x,y,z. |
| 7 | int | 0 | Range (stored as float; integer parse). |
| 8 | bool | true (internal flag = 1) | Only read when not empty. Role of the flag not identified in the source. |
| 9 | str | priority 0 | Optional. `NORMAL` -> priority 1, `ALWAYS_EXECUTE` -> priority 0, anything else leaves priority 0. |

`MB_DockGuide` ([Guides.cpp:148](../../src/Content/Guides.cpp#L148)):

| Index | Type | Meaning |
|---|---|---|
| 0 | str (47) | Target object name. |
| 1,2,3 | float | First position x,y,z (destination is not written). |
| 4 | nick | Dock target nickname (`pub::GetNicknameId`). |
| 5 | float | Range. |

The whole source line is kept by the guide (`get_line_ptr`) so that the save game can re-emit it under `[MetaBehavior]`.

## `[Trigger]`

The mission's rule engine. **One `[Trigger]` section defines one trigger.** Parsed by `LoadMissionTriggerSection` ([MissionScript.cpp:298](../../src/Content/MissionScript.cpp#L298)) and `LoadTrigger` ([MissionScript.cpp:238](../../src/Content/MissionScript.cpp#L238)). The first `nickname` in the section starts the trigger and the rest of the section is consumed by `LoadTrigger`; a second `nickname` inside the same section is therefore ignored, and keys before `nickname` are ignored (except `State`, which is read and dropped). If a trigger with the same nickname already exists the whole section is skipped.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0: name | id | n/a | Trigger id used by `Act_ActTrig`, `Act_DeactTrig`, `Cnd_WatchTrigger`, `Act_Save`. | [MissionScript.cpp:304](../../src/Content/MissionScript.cpp#L304) |
| `InitState` | 0 | str | `INACTIVE` | `ACTIVE`, `INACTIVE` or `COMPLETE` (case-insensitive); any other word leaves the state `INACTIVE`. Triggers that are `ACTIVE` at load are activated by the mission-start message. | [MissionScript.cpp:275](../../src/Content/MissionScript.cpp#L275) |
| `repeatable` | 0 | bool | false | `true` re-arms the trigger after it fires; `false` has no effect. | [MissionScript.cpp:286](../../src/Content/MissionScript.cpp#L286) |
| `system` | - | - | - | Recognised and ignored. | [MissionScript.cpp:293](../../src/Content/MissionScript.cpp#L293) |
| `State` (in the section, before `nickname`) | 0 | str | - | Evaluated with `MissionStateFromName` and the result discarded. | [MissionScript.cpp:329](../../src/Content/MissionScript.cpp#L329) |
| `Cnd_*` | per condition | | | **The trigger's condition.** Exactly one is used: a later `Cnd_*` line replaces the earlier one (the earlier object is leaked, not deleted). A trigger without `Cnd_*` has no condition: events never wake it, and activating it dereferences the missing condition (not guarded, as in retail), so every trigger must have one. The condition decides which game events wake the trigger (heartbeat, object destroyed, ...). | [conditions](missions-conditions.md) |
| `Act_*` | per action | | | **Actions executed when the condition holds**, in file order. Any number; each line adds one action. | [actions](missions-actions.md) |

Action and condition names are matched with `stricmp`, in a fixed order, the first match wins (lists at [MissionScript.cpp:86](../../src/Content/MissionScript.cpp#L86) and [MissionScript.cpp:143](../../src/Content/MissionScript.cpp#L143)). A key that is neither of those nor one of the entries above is ignored silently. Parameters of a `Cnd_*` or `Act_*` line are read with the positional getters of that entry's `Parse`; extra values are ignored, missing values read as empty/0.

Trigger life cycle (not configurable): when a trigger is activated (`InitState = ACTIVE` at mission start, `Act_ActTrig`) its condition's `OnActivate` runs and the condition is tested once with no event; a true condition completes the trigger at once. After that the condition is tested on each message listed in its event table (heartbeat for polled conditions, a specific message for event conditions). When it matches, all actions run in order and the trigger becomes `COMPLETE` (or `ACTIVE` again if `repeatable`).

## Related files

* [missions-conditions.md](missions-conditions.md) - every `Cnd_*` entry.
* [missions-actions.md](missions-actions.md) - every `Act_*` entry.
* [missions-objlist.md](missions-objlist.md) - `[ObjList]` directives (`Goto*`, `Follow`, `Dock`, ...).
* [missions-saves.md](missions-saves.md) - the sections the mission and story objects write into / read from save games.
* [missions-support.md](missions-support.md) - RTC scripts, generic bar scripts, voices, `specific_npc.ini`, player toughness table, base NPC (bribe/know/rumor) records, story stage names.
* [random-missions.md](random-missions.md) - random mission tables and the vignette generator.
* [ai-and-encounters.md](ai-and-encounters.md) - formations, NPC ship archetypes, pilot personalities, encounter parameters.
