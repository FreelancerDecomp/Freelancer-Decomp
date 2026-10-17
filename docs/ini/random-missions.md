# Random missions: tables and vignette generator (Content.dll)

Random ("computer generated") missions are built from a set of INI tables: difficulty and money curves, bonus loot, killable solars, solar formations, the weighted decision tree of mission "vignettes" with their texts and comm sequences, plus ship-class and rank tables. This document describes every key these files accept and the one save-game section the random-mission controller writes.

## File location / how it is found

* All paths are relative to the game Data directory (`[Freelancer] data path` from `freelancer.ini`, see [freelancer-ini.md](freelancer-ini.md)) and are opened with `OpenContentDataIni` ([ContentDataPath.cpp:17](../../src/Content/ContentDataPath.cpp#L17)).
* The seven files under `RandomMissions\` are read by the constructor of the global random-mission generator `FRandomMissionGenerator` ([RandomMissionGen.cpp:311](../../src/Content/RandomMissionGen.cpp#L311)), which is a static object of `Content.dll` (built when the DLL loads). Each file is opened only if it exists; a missing file just leaves its table empty.
* `missions\shipclasses.ini`, the second pass over `ships\shiparch.ini` and `missions\RankDiff.ini` are read by `LoadContentGameTables` ([Encounters.cpp:495](../../src/Content/Encounters.cpp#L495)). The open result is not tested there either.
* Parsing is by `INI_Reader` (see [parsers.md](parsers.md)): case-insensitive keys and headers, repeated keys delivered in file order, positional comma-separated values. Types are as in [missions.md](missions.md#conventions-used-in-the-tables). Faction values are faction (reputation group) nicknames resolved with `pub::Reputation::GetReputationGroup`; archetypes with `pub::GetEquipmentID` / `pub::GetNicknameId`. An unknown name resolves to 0/-1 silently (not validated) unless a row below says otherwise.

| File | Header(s) | Reader |
|---|---|---|
| `RandomMissions\NpcRankToDiff.ini` | `[RankAndFormationSizeToDifficulty]` | [`LoadNpcRankToDifficulty`](../../src/Content/RandomMissionGen.cpp#L169) |
| `RandomMissions\Diff2Money.ini` | `[Diff2Money]` | [generator ctor](../../src/Content/RandomMissionGen.cpp#L326) |
| `RandomMissions\RMLootInfo.ini` | `[Global]`, `[RMBonusLoot]` | [`LoadRmLootInfo`](../../src/Content/RandomMissionDB.cpp#L545) |
| `RandomMissions\SolarFormations.ini` | `[SolarFormation]` | [`LoadSolarFormations`](../../src/Content/RandomMissionDB.cpp#L827) |
| `RandomMissions\KillableSolars.ini` | `[KillableSolar]` | [`LoadKillableSolars`](../../src/Content/RandomEncounters.cpp#L58) |
| `RandomMissions\VignetteCriticalLoot.ini` | `[critical_loot]` | [`LoadVignetteCriticalLoot`](../../src/Content/RandomEncounters.cpp#L256) |
| `RandomMissions\VignetteParams.ini` | `[DecisionNode]`, `[DocumentationNode]`, `[DataNode]` | [`FRandomMissionGenerator::Load`](../../src/Content/RandomMissionGen.cpp#L408) |
| `missions\shipclasses.ini` | `[ShipClass]` | [`LoadShipClasses`](../../src/Content/RandomMissionDB.cpp#L679) |
| `ships\shiparch.ini` (second pass) | `[Ship]` | [`LoadShipMissionProperties`](../../src/Content/RandomMissionDB.cpp#L762) |
| `missions\RankDiff.ini` | `[RankDiffDB]` | [`LoadRankDifficulties`](../../src/Content/RandomMissionDB.cpp#L118) |

## `NpcRankToDiff.ini`

Maps an NPC rank to a list of difficulty values (one per formation size).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `NPCRank` (in `[RankAndFormationSizeToDifficulty]`) | 0: NPC rank<br>1...: difficulty per formation size | int<br>float | - | Appends the values to the list of that rank (a rank that already has a list keeps it and the new values are **appended** after the old ones). Values are read until the first empty one. | [RandomMissionGen.cpp:177](../../src/Content/RandomMissionGen.cpp#L177) |

## `Diff2Money.ini`

Maps a mission difficulty to a base reward.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `Diff2Money` (in `[Diff2Money]`) | 0: difficulty<br>1: money | float<br>int | - | One curve point. A duplicate difficulty keeps the first point. The reward for a difficulty is the linear interpolation between the neighbouring points, truncated, then rounded to "round" money: amounts over 100 keep their two leading digits and, when the leading digit is below 5, the third digit as 5 if it was 5 or more and as 0 otherwise (everything after it becomes 0). A difficulty above the last point, or below the first one, is not defined by the code (returns 0 above, interpolates from (0, 0) below). | [RandomMissionGen.cpp:334](../../src/Content/RandomMissionGen.cpp#L334) |

## `RMLootInfo.ini`

Bonus loot dropped by random-mission NPCs.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `[Global]` `BonusLootDropChance` | 0 | float | 0 (also when the file is missing) | Chance that an NPC drops bonus loot; clamped to 0..1 (a value <= 0 or NaN gives 0, >= 1 gives 1). | [RandomMissionDB.cpp:553](../../src/Content/RandomMissionDB.cpp#L553) |
| `[RMBonusLoot]` `archetype` | 0: equipment nickname | str | 0 | Item dropped. | [L491](../../src/Content/RandomMissionDB.cpp#L491) |
| `num_to_drop` | 0: min<br>1: max (optional) | int | 0, 0 | Quantity range; max defaults to min. | [L495](../../src/Content/RandomMissionDB.cpp#L495) |
| `faction` | 0...: faction nicknames, or `all` | str | none | `all` (case-insensitive, checked on value 0 only) registers the record for every faction; otherwise each listed faction gets its own copy. **A section without `faction` is discarded**, because records are stored per faction. | [L503](../../src/Content/RandomMissionDB.cpp#L503) |
| `difficulty_range` | 0: min<br>1: max | float | 0, 0 | The record is a candidate for mission difficulties inside the closed range. | [L521](../../src/Content/RandomMissionDB.cpp#L521) |
| `weight` | 0 | int (read as `get_value_int`, stored as float) | 0 | Relative drop weight. The drop picks among the candidates (records of faction `all` plus the faction's own) proportionally to weight; if the weights sum to 0 nothing is picked. | [L526](../../src/Content/RandomMissionDB.cpp#L526) |

## `SolarFormations.ini`

Named groups of solar objects (stations, defensive turrets, ...) placed around a battle.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0 | id | 0 | Formation id; a duplicate keeps the first formation. | [RandomMissionDB.cpp:853](../../src/Content/RandomMissionDB.cpp#L853) |
| `type` | 0 | str | -1 | One of `big_solar_formation` (0), `battle_background_formation` (1), `defensive_outpost_formation` (2), `loot_group_formation` (3); other words keep -1. | [L857](../../src/Content/RandomMissionDB.cpp#L857) |
| `pos` | 0: `mandatory` or `optional`<br>1: solar type<br>2,3,4: x,y,z<br>5..8: quaternion W,X,Y,Z (optional) | str<br>str<br>float | - | One solar slot (repeatable, in order). Solar type is one of `big_solar` (0), `defensive_solar` (1), `killable_neutral` (2), `background_solar` (3). **An unknown word in value 0 or 1 silently reuses the value of the previous `pos` line** (the first line in the file starts with 0 / 0). The quaternion is normalised; without it the slot has identity orientation. | [L863](../../src/Content/RandomMissionDB.cpp#L863) |

## `KillableSolars.ini`

Killable solar objects the generator may place, selected by faction, solar type, difficulty and the zone they are in.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` (in `[KillableSolar]`) | 0 | id | empty | Entry id. **Required**: an entry with no nickname is dropped. | [RandomEncounters.cpp:87](../../src/Content/RandomEncounters.cpp#L87) |
| `solar_type` | 0 | str | -1 | `big_solar`, `defensive_solar`, `killable_neutral`, `background_solar` (0-3). **Required** (an unset type drops the entry). | [L91](../../src/Content/RandomEncounters.cpp#L91) |
| `string_id` | 0 | int | 0 | Display name string id. | [L106](../../src/Content/RandomEncounters.cpp#L106) |
| `faction` | 0...: faction nicknames or `all` | str | none | Factions that may use this solar; `all` (case-insensitive, value 0) = any. **Required** (an empty list without `all` drops the entry). | [L110](../../src/Content/RandomEncounters.cpp#L110) |
| `archetype` | 0 | nick | 0 | Solar archetype. **Required** (0 drops the entry). | [L128](../../src/Content/RandomEncounters.cpp#L128) |
| `loadout` | 0 | str | 0 | Loadout. **Required**. | [L132](../../src/Content/RandomEncounters.cpp#L132) |
| `hitpoints` | 0: min<br>1: max (optional) | int | 0 | Hit-point range; max defaults to min. Must satisfy `0 < min <= max` or the entry is dropped; values above the archetype's maximum are replaced by that maximum. | [L136](../../src/Content/RandomEncounters.cpp#L136) |
| `difficulty` | 0: min<br>1: max | float | 0, 0 | Mission difficulty range; must satisfy `0 <= min <= max <= 100` or the entry is dropped. | [L145](../../src/Content/RandomEncounters.cpp#L145) |
| `only_allowed_in_zone_type` | 0...: `rock`, `debris`, `ice`, `lava`, `crystal` | str | 0 (any zone) | Restricts the entry to zones of these kinds (flags 0x40, 0x80, 0x100, 0x200, 0x800). A trailing `all` is compared but has no effect; unknown words are ignored. | [L150](../../src/Content/RandomEncounters.cpp#L150) |
| `pilot_choices` | 0 | int | 1 | Number of pilot variants. | [L169](../../src/Content/RandomEncounters.cpp#L169) |

A duplicate `nickname` keeps the first entry that passed validation.

## `VignetteCriticalLoot.ini`

Loot items that the player is asked to destroy, tractor or return.

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `faction` (in `[critical_loot]`) | 0...: faction nicknames, or `all` | str | none | Offering factions. | [RandomEncounters.cpp:280](../../src/Content/RandomEncounters.cpp#L280) |
| `archetype` | 0 | str | 0 | Loot item (`pub::GetEquipmentID`). | [L297](../../src/Content/RandomEncounters.cpp#L297) |
| `name` | 0: string id | int | 0 | Display name. | [L301](../../src/Content/RandomEncounters.cpp#L301) |
| `why_desired` | 0, 1: string ids | int | 0, 0 | Two texts explaining why the loot is wanted. | [L305](../../src/Content/RandomEncounters.cpp#L305) |
| `type` | 0...: `destroy`, `tractor`, `return` | str | none | Mission kinds the item may be used for (1, 2, 3); other words are ignored. | [L310](../../src/Content/RandomEncounters.cpp#L310) |

No validation is performed; incomplete records are kept as they are.

## `VignetteParams.ini` (the mission decision tree)

The generator builds a binary decision tree of "vignette" nodes; each leaf-to-root path through the tree decides the mission type, texts, comm sequences and difficulty window. Loaded by `FRandomMissionGenerator::Load` ([RandomMissionGen.cpp:408](../../src/Content/RandomMissionGen.cpp#L408)). Every header creates one node: `[DecisionNode]`, `[DocumentationNode]` and `[DataNode]` read the keys below; **any other header still inserts an empty default node** keyed by `node_id` -1. Nodes are stored by `node_id` and a repeated id keeps the first node. After loading the tree is linked: each `child_node` gets the parent id appended to its parent list (a child id with no node of its own creates an empty default node); the root is the smallest node id followed up its first parent; the children of every node with `implemented = false` are disabled with their subtrees; nodes that are implemented and have no children are the leaves.

### Node keys

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `node_id` | 0 | int | -1 | Id of the node (key of the table). | [RandomMissionGen.cpp:1064](../../src/Content/RandomMissionGen.cpp#L1064) |
| `child_node` | 0 | int | none | Id of a child node. Repeatable; a decision node has two children (the first is followed for a `T` decision in a tree path, the second for `F`). The probability of the first child is `1 / child count` (the "first child chance" is not readable from the INI). | [L1069](../../src/Content/RandomMissionGen.cpp#L1069) |
| `implemented` | 0 | bool | true | `false` disables the node and its subtree. | [L1075](../../src/Content/RandomMissionGen.cpp#L1075) |
| `nickname`, `documentation` | 0 | id | empty | Node name (both keys set the same field). The generator recognises the names `random_mission`, `destroy_solars_mission`, `assassinate_mission`, `destroy_ships_mission` and `loot_item_mission` (compared by `CreateID`) as the mission-type markers when it walks a path; other names are only documentation. | [L1080](../../src/Content/RandomMissionGen.cpp#L1080) |
| `weight` | 0 | float | 1.0 | Relative weight of the node when a path through it is picked; an exact 0 (also the default) becomes 1.0. | [L969](../../src/Content/RandomMissionGen.cpp#L969) |
| `difficulty` | 0: min<br>1: max | float | 0, 0 | Mission difficulty window the node applies to. | [L996](../../src/Content/RandomMissionGen.cpp#L996) |
| `offer_group` | 0...: faction nicknames, or `all` | str | none | Factions that may offer missions through this node. `all` (value 0, case-insensitive) = every reputation group. Duplicates removed. | [L1001](../../src/Content/RandomMissionGen.cpp#L1001) |
| `hostile_group` | 0...: faction nicknames, or `all` | str | none | Factions the NPCs of the mission may belong to. | [L1006](../../src/Content/RandomMissionGen.cpp#L1006) |
| `allowable_zone_types` | 0...: `open`, `field`, `exclusion` | str | all three (7) | Zone kinds usable for the battle (flags 1, 2, 4). If the key is present the set is rebuilt from the listed words only (unknown words ignored; a key with no valid word leaves 0). | [L1018](../../src/Content/RandomMissionGen.cpp#L1018) |
| `offer_text` | see below | | | Offer description fragments (repeatable). | [L974](../../src/Content/RandomMissionGen.cpp#L974) |
| `objective_text` | see below | | | Per-objective texts (repeatable). | [L981](../../src/Content/RandomMissionGen.cpp#L981) |
| `failure_text` | 0: string id<br>1...: parameter names | int<br>str | 0 | Text shown when the mission fails. | [L986](../../src/Content/RandomMissionGen.cpp#L986) |
| `reward_text` | 0: string id<br>1...: parameter names | int<br>str | 0 | Text for the reward. | [L991](../../src/Content/RandomMissionGen.cpp#L991) |
| `comm_sequence` | see below | | | Scripted radio chatter (repeatable). | [L1011](../../src/Content/RandomMissionGen.cpp#L1011) |

Any other key in a node section is ignored.

### Text parameters

Words usable after a string id in `offer_text`, `objective_text`, `failure_text` and `reward_text` (`GRmTextParamNames`, case-insensitive; [RandomMissionRuntime.cpp:76](../../src/Content/RandomMissionRuntime.cpp#L76)). Each is substituted into the string (`%s`/`%d` slots, in order) when the text is built: `FSTRING_PARAM_NONE` (0), `OFFER_GROUP` (1), `HOSTILE_GROUP` (2), `BIG_SOLAR` (3), `OTHER_SOLAR` (4), `CRITICAL_LOOT` (5), `TARGET_FULL_NAME` (6), `REWARD_MONEY` (7), `FACTION_REWARD` (8), `FACTION_HIT` (9), `TIME_TO_RETURN_TO_BATTLE` (10), `MISSION_DIFFICULTY` (11), `OFFER_BASE` (12), `TARGET_ZONE` (13), `TARGET_SYSTEM` (14). In `failure_text`/`reward_text`/`objective_text` a word that is not in the table silently repeats the previous parameter (0 for the first); in `offer_text` the first unknown word ends the entry. Which parameters a mission type supports is fixed in code (e.g. a destroy-solars path supports 1-3 and 9-13).

### `offer_text`

`offer_text = <mode>, [singular|plural,] <string id>, PARAM, PARAM, ..., [singular|plural,] <string id>, ...` ([RandomMissionGen.cpp:671](../../src/Content/RandomMissionGen.cpp#L671)).

* Value 0: `append` or `replace` (case-insensitive); anything else keeps `replace`. The flag is stored with the text; its use (append to / replace the text inherited from the parent node) is in the generator's text builder.
* The rest is a list of entries of the form `[singular|plural,] <string id>, PARAM, PARAM, ...`. A token that parses to a **non-zero number** is the entry's string id; the words after it are parameter names (table below) and the first token that is not a parameter name ends the entry and is read again as the start of the next entry (so the next string id, or a `singular`/`plural` word, follows directly). `singular` / `plural` set the entry's form; any other non-numeric word before a string id is skipped.
* When the text is built, an entry is emitted if: form default (neither word) - always; `singular` - its parameters contain `OFFER_GROUP` and the offering faction has a singular nickname (`faction_prop.ini`), or else they contain `HOSTILE_GROUP` and the hostile faction is singular; `plural` - the same tests with "not singular". A `singular`/`plural` entry without a group parameter is never emitted.

### `objective_text`

`objective_text = <objective>, <string id>, PARAM, ...` ([RandomMissionGen.cpp:786](../../src/Content/RandomMissionGen.cpp#L786)). Objective names (case-insensitive) and the numeric objective they select: `guide_to_pre_battle` 1, `guide_to_main_battle` 2, `guide_to_main_from_pre` 3, `kill_big_solar` 4, `kill_other_solars` 5, `return_to_battle` 9, `destroy_loot` 10, `tractor_loot` 11, `return_loot` 12, `jettison_loot` 13, `pb_kill_ships` 19, `pb_kill_big_ships` 20, `pb_dont_kill_ships` 21, `mb_kill_ships` 22, `mb_kill_big_ships` 23, `mb_dont_kill_ships` 24. An unknown objective name is stored under an unusable key (never looked up). A repeated objective re-parses the existing text, i.e. the later line wins.

### `comm_sequence`

`comm_sequence = <trigger>, <target>, <float>, <min delay>, <max delay>, [<speaker>, <text>]...` ([RandomMissionGen.cpp:855](../../src/Content/RandomMissionGen.cpp#L855)).

| Index | Type | Default | Meaning |
|---|---|---|---|
| 0 | str | `NONE` (0) | Trigger (case-insensitive): `NONE` 0, `IN_SPACE_IN_SYSTEM` 1, `IN_SPACE_NOT_IN_SYSTEM` 2, `PLAYER_IN_PRE_BATTLE_SPAWN_RANGE` 3, `SUCCESS_KILLED_ALL_GOING_TO_MAIN` 4, `SUCCESS_NOT_KILLED_ALL_GOING_TO_MAIN` 5, `PLAYER_IN_MAIN_BATTLE_SOLAR_SPAWN_RANGE` 6, `PLAYER_IN_MAIN_BATTLE_SHIP_SPAWN_RANGE` 7, `FIRST_FRIENDLY_SPAWN` 8, `FIRST_DEFENSIVE_SHIP_SPAWN` 9, `FIRST_BIG_SHIP_SPAWN` 10, `LEFT_COMBAT_AREA` 11, `REENTERED_COMBAT_AREA` 12, `GLOBAL_SUCCESS` 13, `GLOBAL_FAIL_COMBAT_RANGE` 14, `GLOBAL_FAIL_DESTROYED_CRITICAL_LOOT` 15, `GLOBAL_FAIL_OTHER` 16. An unknown word keeps the default. |
| 1 | str | `BASE` (1) | Receiver: `NONE` 0, `BASE` 1, `BIG_SOLAR` 2, `BIG_SHIP` 3, `DEFENSIVE_SHIP` 4, `FRIENDLY_SHIP` 5, `PLAYER` 6, `ALL_PLAYERS` 7, `PLAYERS_IN_RANGE` 8, `PLAYERS_OUT_OF_RANGE` 9, `PLAYERS_IN_SYSTEM` 10, `PLAYERS_OUT_OF_SYSTEM` 11. |
| 2 | float | 0 | Stored; its role is not named in the source. |
| 3, 4 | float | 0, 0 | Minimum and maximum delay in seconds. For the four `GLOBAL_*` triggers a maximum above 5.0 is cut to 5.0 and then the minimum is also set to 5.0 if it is not below 5.0. |
| 5... | str, str | - | Pairs `speaker, text`: speaker is a receiver word from the table above (unknown = `BASE`); text is a string kept up to 63 characters (a literal comm line name). Pairs are read until the first empty value. |

## `shipclasses.ini`

Named groups of ship archetypes used by the encounter generator. Read by `LoadShipClasses` ([RandomMissionDB.cpp:679](../../src/Content/RandomMissionDB.cpp#L679)).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` (in `[ShipClass]`) | 0 | id | 0 | Class id; a duplicate class keeps its first definition. | [L692](../../src/Content/RandomMissionDB.cpp#L692) |
| `member` | 0: ship nickname or earlier class nickname | id | none | Adds a member. If the name is the nickname of an **earlier** class that class's members are added instead (flattened). Each member is stored once. Repeatable. | [L696](../../src/Content/RandomMissionDB.cpp#L696) |

## `ships\shiparch.ini` (second pass: mission properties)

The archetype file is read a second time only for its `[Ship]` sections' mission properties (the rest of the file is documented in [archetypes-and-equipment.md](archetypes-and-equipment.md)). Read once ([RandomMissionDB.cpp:762](../../src/Content/RandomMissionDB.cpp#L762)).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0 | str | 0 | Ship archetype (`pub::GetShipID`). | [L777](../../src/Content/RandomMissionDB.cpp#L777) |
| `mission_property` | 0 | str | none | `can_use_berths`, `can_use_med_moors` (stored as the medium-moors property) or `can_use_large_moors` (case-insensitive); other words are ignored. Repeatable; each property is stored once. Tells the generator at which docks the ship can be spawned. | [L781](../../src/Content/RandomMissionDB.cpp#L781) |

## `RankDiff.ini`

Maps story progress to the base difficulty of random missions (single player).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `rank_diff` (in `[RankDiffDB]`) | 0: story stage name<br>1: difficulty | str<br>float | stage of the previous line | Stage names are the ones in [missions-support.md](missions-support.md#story-stage-names) (case-insensitive). **A name that is not in the table reuses the stage of the previous line** (the first line of the file gets an undefined value). The difficulty is clamped to 0..100. The records are sorted by stage after loading. In single player the stage of the player selects the record (or the nearest lower one), and the mission difficulty is then drawn from a window of 1.3689^3 around it. | [RandomMissionDB.cpp:132](../../src/Content/RandomMissionDB.cpp#L132) |

## `[RandomMission]` save section

The random-mission controller (`DestroyMission`) saves itself with `[RandomMission]`, `type = DestroyMission` followed by the keys below ([DestroyMission.cpp:407](../../src/Content/DestroyMission.cpp#L407), [RandomMissionDB.cpp:305](../../src/Content/RandomMissionDB.cpp#L305)); loading is in `LoadContentPlayerState` ([Support.cpp:346](../../src/Content/Support.cpp#L346)) and `FRandomMissionLoader::Load` ([DestroyMission.cpp:659](../../src/Content/DestroyMission.cpp#L659)). On load the mission is rebuilt from these values and `random_seed` (the tree path itself is not saved: the generator is re-seeded with `random_seed` so that the same tree path and texts result).

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `type` | 0: controller name | str (up to 60 chars) | - | Written `DestroyMission`. The section is only processed through its `type` line; the value is used as the controller name for `pub::Controller::Create` (matched with `stricmp` against the Content controller table; `DestroyMission` is the only random-mission controller), while the data is always loaded as destroy-mission data. | [Support.cpp:352](../../src/Content/Support.cpp#L352) |
| `offer_group` | 0 | int | -1 | Offering faction (reputation group id). | [RandomMissionDB.cpp:320](../../src/Content/RandomMissionDB.cpp#L320) |
| `offer_base` | 0 | uint | 0 | Base that offered the mission. | [L325](../../src/Content/RandomMissionDB.cpp#L325) |
| `offer_ids` | 0 | int | 0 | Offer string id. | [L330](../../src/Content/RandomMissionDB.cpp#L330) |
| `reward` | 0 | int | 0 | Cash reward. | [L335](../../src/Content/RandomMissionDB.cpp#L335) |
| `rep_reward` | 0 | float | 0 | Reputation gained. | [L340](../../src/Content/RandomMissionDB.cpp#L340) |
| `rep_hit` | 0 | float | 0 | Reputation lost with the hostile group. | [L345](../../src/Content/RandomMissionDB.cpp#L345) |
| `difficulty` | 0 | float | 0 | Mission difficulty. | [L350](../../src/Content/RandomMissionDB.cpp#L350) |
| `jump_pref` | 0 | int | 0 | Route preference passed to the jump-route search (gate/hole preference). | [L355](../../src/Content/RandomMissionDB.cpp#L355) |
| `enemy_group` | 0 | uint | `0xFFFFFFFF` | Hostile faction. | [DestroyMission.cpp:583](../../src/Content/DestroyMission.cpp#L583) |
| `dest_sys` | 0 | uint | 0 | Destination system id. | [L588](../../src/Content/DestroyMission.cpp#L588) |
| `objective_pos` | 0,1,2 | **int** | 0,0,0 | Objective position; written as `%g,%g,%g` but read back with `get_value_int`, so fractions are truncated. | [L593](../../src/Content/DestroyMission.cpp#L593) |
| `objective_zone` | 0 | uint | 0 | Zone id; added to the global list of used objective zones. | [L600](../../src/Content/DestroyMission.cpp#L600) |
| `random_seed` | 0 | uint | 0 | Seed used to re-create the same vignette. | [L608](../../src/Content/DestroyMission.cpp#L608) |

The first matching key per line wins; unknown keys are ignored. The generator also has a `tree_path = T/F string` reader and writer (`FRmTreePath::Load`/`Save`, [RandomMissionGen.cpp:1115](../../src/Content/RandomMissionGen.cpp#L1115)), but nothing in the port calls them, so `tree_path` does not occur in saves. A section is saved only in single player and only if the mission's first state machine agrees that saving is possible.
