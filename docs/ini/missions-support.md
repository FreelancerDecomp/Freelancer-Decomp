# Mission support tables: RTC scripts, voices, generic scripts, NPC records, story data

Supporting INI files read by `Content.dll` for the mission layer: bar RTC scripts (`[CharacterEncounter]`), named story NPCs, generic bar scripts and their per-voice exclusions, voice line durations and voice properties, the player toughness (rank) table, the base NPC offer records (`bribe`/`know`/`rumor`) and the story stage names. Mission scripts themselves are in [missions.md](missions.md).

## File location / how it is found

All files are opened with `OpenContentDataIni(relative path)` ([ContentDataPath.cpp:17](../../src/Content/ContentDataPath.cpp#L17)), i.e. relative to the Data directory from `freelancer.ini` ([freelancer-ini.md](freelancer-ini.md)). The fixed-name tables are loaded once at start-up by `LoadContentGameTables` ([Encounters.cpp:495](../../src/Content/Encounters.cpp#L495)) in this order: `audio\voices_space_male.ini`, `audio\voices_space_female.ini`, `audio\voices_recognizable.ini` (each read twice: once for `[Voice]`/`[Sound]`, once for `[mVoiceProp]`), `missions\empathy.ini`, `missions\voice_properties.ini`, `missions\formations.ini`, `missions\pilots_*.ini`, `missions\npcships*.ini`, `missions\faction_prop.ini`, `equipment\commodities_per_faction.ini`, `missions\shipclasses.ini`, `ships\shiparch.ini` (second pass), `missions\lootprops.ini`, `scripts\GCS\GCSExcl.ini`, `missions\PTough.ini`, `missions\RankDiff.ini`, `missions\news.ini`. The open result is not tested: a missing file just loads nothing. Entries covered by this document: voices, voice_properties, GCSExcl, PTough; the rest are described in [ai-and-encounters.md](ai-and-encounters.md), [random-missions.md](random-missions.md) and [systems-and-bases.md](systems-and-bases.md). `missions\specific_npc.ini` is loaded by the `StoryLauncher` controller ([StoryLauncher.cpp:41](../../src/Content/StoryLauncher.cpp#L41)) and `scripts\gcs\genericScripts.ini` by the base-support controller ([BaseSupport.cpp:211](../../src/Content/BaseSupport.cpp#L211)).

Parsing is by `INI_Reader` (see [parsers.md](parsers.md)); keys and headers are case-insensitive, repeated keys are all delivered.

## RTC scripts (`[CharacterEncounter]` files)

A "real-time cutscene" script drives a bar character: where it stands, which Thorn scripts it plays and what it offers. Files are started by `Act_AddRTC` and by the story ctor (`Mission_01b`: `missions\m01a\M001a_s006x_Li01_02_nrml.ini`, `missions\m01a\rtc_success.ini`). Parser: `FCharacterEncounter::Load` ([RtcScripts.cpp:43](../../src/Content/RtcScripts.cpp#L43)). If the file cannot be opened nothing is loaded.

### `[CharacterEncounter]`

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `Location` | 0: base nickname<br>1: room name | str (31)<br>str (63) | unset | Base and room the script belongs to (`pub::GetBaseID`, `pub::GF::FindLocation`). Later `relocate_player`/`start_room` default their base to this one. The script is only started if this base is the player's current base. | [L63](../../src/Content/RtcScripts.cpp#L63) |
| `greet`, `walkup`, `action`, `offer`, `decision`, `accept`, `decline`, `reject` | 0: Thorn script name | str (63) | none | Script played at that stage of the encounter. Internal type codes: `greet` 0, `walkup` 1, `action` 2, `offer` 3, `decision` 4, `accept` 5, `decline`/`reject` 6 (both words give 6). The first matching entry of a type is used. | [L75](../../src/Content/RtcScripts.cpp#L75) |
| `autoplay` | none | - | false | Presence of the key (any value) turns autoplay on. | [L92](../../src/Content/RtcScripts.cpp#L92) |
| `mission_text_id` | 0: string id | int | 0 | Text of the mission the character offers. | [L96](../../src/Content/RtcScripts.cpp#L96) |
| `nooffer_text_id` | 0: string id | int | 0 | Text shown when there is no offer. | [L102](../../src/Content/RtcScripts.cpp#L102) |
| `relocate_player` | 0: room name or `Spaceflight`<br>1: base nickname (optional) | str (63)<br>str | 0 | Where the player is moved when the script runs; `Spaceflight` (case-insensitive) = launch to space. | [L106](../../src/Content/RtcScripts.cpp#L106) |
| `start_room` | 0: room name<br>1: base nickname (optional) | str (63)<br>str | 0 | Room the script starts in. | [L123](../../src/Content/RtcScripts.cpp#L123) |

Any other key is ignored (but still creates and discards an action record).

### `[Reserve]`

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `spot` | 0: placement name | str | none | A character placement ("spot") in the script's room that is reserved for the scene (`pub::GF::GetCharacterPlacementByName`); names that do not resolve are dropped. Repeatable. | [L152](../../src/Content/RtcScripts.cpp#L152) |

### `[Char]`

One section per character role.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `actor` | 0: role name | str (31) | empty | Role name. Unless an `npc` was already given, a specific NPC (from `specific_npc.ini`) with the same nickname becomes the character. | [L172](../../src/Content/RtcScripts.cpp#L172) |
| `spot` | 0: placement name | str | -1 | Placement the role stands at (the result of the lookup is not tested). | [L185](../../src/Content/RtcScripts.cpp#L185) |
| `npc` | 0: specific NPC nickname | id | none | Selects the character from `specific_npc.ini`; replaces any earlier one. An unknown nickname is ignored. | [L191](../../src/Content/RtcScripts.cpp#L191) |
| `fidget` | 0: fidget script name | str | empty | Idle animation script. | [L199](../../src/Content/RtcScripts.cpp#L199) |

## `specific_npc.ini`

`missions\specific_npc.ini` defines the named story characters; read once by `FStoryLauncher` ([StoryLauncher.cpp:36](../../src/Content/StoryLauncher.cpp#L36)). Every `[NPC]` section becomes a character description via `LoadNpcCharacterDesc` ([NpcShips.cpp:228](../../src/Content/NpcShips.cpp#L228)); a repeated nickname keeps the first. The same keys are used for the character records of `mBases.ini` (`[GF_NPC]`, [systems-and-bases.md](systems-and-bases.md)). A record is "complete" only when its body part, voice and individual name are non-zero; the loader result is not tested, so incomplete records are stored anyway.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0 | id | empty | Key of the record. | [NpcShips.cpp:196](../../src/Content/NpcShips.cpp#L196) |
| `base_appr` | 0: costume nickname | str | none | Starts from a named costume (`pub::GetCostumeID`); applied only if the name resolves. Later `head`/`body`/... override single parts. | [L162](../../src/Content/NpcShips.cpp#L162) |
| `head` | 0: body-part nickname | str | 0 | Head part (`pub::GF::GetBodyPart`, type 1). | [L170](../../src/Content/NpcShips.cpp#L170) |
| `body` | 0 | str | 0 | Body part (type 0). | [L175](../../src/Content/NpcShips.cpp#L175) |
| `lefthand`, `righthand` | 0 | str | 0 | Hand parts (types 2 and 3). | [L180](../../src/Content/NpcShips.cpp#L180) |
| `accessory` | 0: accessory nickname | str | none | Appends an accessory (repeatable, no bound check). | [L190](../../src/Content/NpcShips.cpp#L190) |
| `voice` | 0: voice nickname | str | 0 | `pub::GetVoiceID`. | [L203](../../src/Content/NpcShips.cpp#L203) |
| `individual_name` | 0: string id | int | 0 | Display name. | [L208](../../src/Content/NpcShips.cpp#L208) |
| `affiliation` | 0: faction nickname | str | `0xFFFFFFFF` | Reputation group. | [L213](../../src/Content/NpcShips.cpp#L213) |

## Generic bar scripts (`genericScripts.ini`)

`scripts\gcs\genericScripts.ini` lists the Thorn scripts bar characters pick for idle/"fidget" behavior. Read by `LoadGenericScripts` ([GenericScripts.cpp:82](../../src/Content/GenericScripts.cpp#L82)) once per base-support controller. Only `[GenericScripts]` sections are read; the state `set_*` values are reset to "unset" at the start of every section.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `set_gender` | 0 | str | unset (2) | `male` (case-insensitive) = 0; any other word (e.g. `female`) = 1. | [L100](../../src/Content/GenericScripts.cpp#L100) |
| `set_posture` | 0 | str | unset (2) | `stand` (case-insensitive) = 0; any other word (e.g. `sit`) = 1. | [L104](../../src/Content/GenericScripts.cpp#L104) |
| `set_segment` | 0: segment name | str | unset (0x42) | Must be one of the 66 names below (case-insensitive); an unknown name leaves the segment unset. | [L108](../../src/Content/GenericScripts.cpp#L108) |
| `script` | 0: Thorn script file name | str | none | Adds the script to the list of (posture, gender, segment) set by the preceding `set_*` lines. Repeatable. **All three `set_*` keys must come first**: with an unset/unknown value the code indexes beyond its table (undefined behavior, as in retail). Names are not checked for existence. | [L121](../../src/Content/GenericScripts.cpp#L121) |

Valid `set_segment` names ([GenericScripts.cpp:38](../../src/Content/GenericScripts.cpp#L38)): `FIDGET`, `A01aG`, `A02aG`, `A02cG`, `A02dG`, `A02eB`, `A03aG`, `A04aG`, `C01aR`, `D01aN`, `D01bG`, `D01cG`, `D01dG`, `D01fG`, `D01fGm`, `D01hG`, `D01kG`, `D01kGm`, `D02aP`, `D02aPm`, `D02bP`, `D02dP`, `D02eP`, `D03aN`, `D04aG`, `D04bB`, `D05aP`, `D05bP`, `D06aG`, `D07aG`, `D07bG`, `F01aB`, `F01bB`, `F01dG`, `F01eG`, `F01fG`, `F01jG`, `F02aP`, `F03aR`, `F03bR`, `F03cR`, `F03dR`, `F04aG`, `F04dG`, `F04fG`, `F04gP`, `F05aG`, `F05bG`, `F05cG`, `F06aP`, `F07aP`, `F07bP`, `F07cP`, `F08dR`, `F08bR`, `F08cR`, `F09bP`, `F10aR`, `F10bR`, `F10bRm`, `W01cG`, `W01bB`, `W02aF`, `W03bB`, `W04bP`, `W04cP`. The set is a fixed table in the code; `FIDGET` is the pool used for idle fidgets.

## `GCSExcl.ini`

`scripts\GCS\GCSExcl.ini` lists scripts a given voice must not be given. Read by `LoadGcsExclusions` ([GenericScripts.cpp:726](../../src/Content/GenericScripts.cpp#L726)); only `[GCS_Exclusions]` sections are read.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `voice` | 0: voice nickname | str | - | Starts (or continues) the exclusion list of this voice (`pub::GetVoiceID`). An empty name leaves the previous voice's id in use. | [L747](../../src/Content/GenericScripts.cpp#L747) |
| `script` | 0: script name | str | - | Script excluded for the current voice (repeatable). Comparison against `genericScripts.ini` names is case-insensitive and whole-name. A `script` before any `voice` writes through an invalid iterator (retail behavior; real files start with `voice`). | [L761](../../src/Content/GenericScripts.cpp#L761) |

## Voices: `audio\voices_*.ini`, `missions\voice_properties.ini`

### Voice line durations (`voices_space_male.ini`, `voices_space_female.ini`, `voices_recognizable.ini`)

Read by `LoadVoiceLines` ([Voices.cpp:28](../../src/Content/Voices.cpp#L28)) and sorted by (voice, message) so that comm lengths can be looked up. The three files share the voice table.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `[Voice]` `nickname` | 0: voice nickname | str | previous voice (0 at file start) | Selects the voice to which the following `[Sound]` sections belong (`pub::GetVoiceID`). | [L35](../../src/Content/Voices.cpp#L35) |
| `[Sound]` `msg` | 0: message nickname | nick | 0 | Voice message id (`pub::GetNicknameId`). | [L49](../../src/Content/Voices.cpp#L49) |
| `[Sound]` `duration` | 0: seconds | float | -1.0 | Length of the line. A `[Sound]` section whose duration stayed exactly -1.0 is dropped. | [L51](../../src/Content/Voices.cpp#L51) |

### Voice properties (`[mVoiceProp]`)

Read from the same three voice files **and** from `missions\voice_properties.ini` by `LoadVoiceProperties` ([RandomMissionGen.cpp:1693](../../src/Content/RandomMissionGen.cpp#L1693)). A section for a voice that already has a record updates that record; flags and counts are merged.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `voice` | 0: voice nickname | str | none | Selects the record. **Must be the first key** of the section: later keys write through the record that `voice` created (a section without `voice` dereferences a null record, as retail). | [L1711](../../src/Content/RandomMissionGen.cpp#L1711) |
| `gender` | 0 | str | no flag | `male` (case-insensitive) sets flag 0x1; `female` (**case-sensitive**) sets 0x2; other words do nothing. | [L1740](../../src/Content/RandomMissionGen.cpp#L1740) |
| `supports_roles` | 0...: role words | str | none | Case-sensitive words, each ORs flags: `combat_lev_1` 0x4000, `combat_lev_2` 0x4, `combat_lev_3` 0xc004, `scan` 0x8, `hail_hailee` 0x10, `hail_dock` 0x20, `dockrequest_dock` 0x40, `dockrequest_docker` 0x80, `behavior` 0x100, `misc_chatter` 0x200, `fidget` 0x400, `combat_dock` 0x800, `random_mission_ship` 0x1000, `mission_commission` 0x2000. Other words are ignored. | [L1749](../../src/Content/RandomMissionGen.cpp#L1749) |
| `permutation_count` | 0: message nickname<br>1: count | nick<br>int | none | How many variants (permutations) of that message exist for the voice; a repeated nickname keeps the first count. | [L1786](../../src/Content/RandomMissionGen.cpp#L1786) |

## `PTough.ini`

`missions\PTough.ini` maps a player's net worth to his rank ("toughness"). Read by `LoadPlayerToughnessScale` ([Support.cpp:133](../../src/Content/Support.cpp#L133)): only the first `[PlayerToughnessScale]` section is read.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `ptough_graph_pt` | 0: experience (net worth)<br>1: toughness (rank) | int, int | none | One graph point. Repeatable. The experience is forced to be non-decreasing (a smaller value is replaced by the previous maximum); the toughness is clamped to 1..40 (0x28). In multiplayer the table is extended past the last point by repeated `x + x/8` steps until it exceeds 999,999,999, with toughness equal to the table index. | [L150](../../src/Content/Support.cpp#L150) |

## Base NPC records (`bribe`, `know`, `rumor`)

These keys appear inside the `[GF_NPC]` sections of `missions\mBases.ini` (the section and its other keys - `misn`, `room`, appearance keys - belong to [systems-and-bases.md](systems-and-bases.md)); the parsers are in [BaseNpcRecords.cpp](../../src/Content/BaseNpcRecords.cpp), the loop in `LoadBaseNpc` ([BaseDB.cpp:55](../../src/Content/BaseDB.cpp#L55)). A record whose `Parse` returns false is **dropped silently**. Reputation levels (`level` below) index the threshold table 0.0, 0.2, 0.4, 0.6 ([BaseNpcRecords.cpp:37](../../src/Content/BaseNpcRecords.cpp#L37)): the player's feeling towards the NPC's faction must be at least that value.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `bribe` | 0: faction nickname<br>1: cost<br>2: text id | str<br>int<br>int | - | The NPC offers to bribe the named faction's hostility away for `cost`. Dropped if the faction is unknown or the cost is 0. Offered only while the player's feeling towards the faction is below 0.6. Value 2 is the offer text (string-table id). | [L40](../../src/Content/BaseNpcRecords.cpp#L40) |
| `know` | 0: text id A<br>1: rumor/info id<br>2: price<br>3: level | int | - | The NPC sells information. Dropped if `level >= 4` (unsigned). Offered while the feeling reaches the level and the player does not yet know the item (rumor id in value 1). Value 2 is the price; values 0 and 1 are string-table ids. | [L72](../../src/Content/BaseNpcRecords.cpp#L72) |
| `rumor`, `rumor_type2` | 0: from stage<br>1: to stage<br>2: level<br>3...: rumor ids (until the first empty value) | str<br>str<br>int<br>int | see text | A rumor the NPC may tell between two story stages (names from [stage names](#story-stage-names), case-insensitive). A stage name that is not in the table reuses the previous lookup result (value 0: an indeterminate value; value 1: the result of value 0). `level` is **not validated** (an index >= 4 reads outside the threshold table). At most 8 ids fit in the record, but the loop has no bound check. The record always loads. Weight: 0x40 for a rumor bounded on both ends (from stage > 0 and to stage < 42), else `(level + 1) * 5`. | [L82](../../src/Content/BaseNpcRecords.cpp#L82) |

## Story stage names

Names used by `rumor`/`rumor_type2` and by `RankDiff.ini` (`rank_diff`); the table is `GContentStageNames` ([RandomMissionDB.cpp:64](../../src/Content/RandomMissionDB.cpp#L64)). Values are the story stage numbers (the same numbers appear in `MissionNum` of the save game). Matching is case-insensitive.

| Name | Value | Name | Value | Name | Value |
|---|---|---|---|---|---|
| `base_0_rank` | 0 | `mission_01a_loaded` | 1 | `mission_01a_accepted` | 2 |
| `mission_01b_loaded` | 3 | `mission_01b_accepted` | 4 | `freetime_01_02` | 5 |
| `mission_02_loaded` | 7 | `mission_02_accepted` | 8 | `freetime_02_03` | 9 |
| `mission_03_loaded` | 10 | `mission_03_accepted` | 11 | `freetime_03_04` | 12 |
| `mission_04_loaded` | 13 | `mission_04_accepted` | 14 | `freetime_04_05` | 15 |
| `mission_05_loaded` | 16 | `mission_05_accepted` | 17 | `freetime_05_06` | 18 |
| `mission_06_loaded` | 19 | `mission_06_accepted` | 20 | `freetime_06_07` | 21 |
| `mission_07_loaded` | 22 | `mission_07_accepted` | 23 | `freetime_07_08` | 24 |
| `mission_08_loaded` | 25 | `mission_08_accepted` | 26 | `freetime_08_09` | 27 |
| `mission_09_loaded` | 28 | `mission_09_accepted` | 29 | `freetime_09_10` | 30 |
| `mission_10_loaded` | 31 | `mission_10_accepted` | 32 | `freetime_10_11` | 33 |
| `mission_11_loaded` | 34 | `mission_11_accepted` | 35 | `freetime_11_12` | 36 |
| `mission_12_loaded` | 37 | `mission_12_accepted` | 38 | `freetime_12_13` | 39 |
| `mission_13_loaded` | 40 | `mission_13_accepted` | 41 | `mission_end` | 42 |

(The value 6 is unused.)
