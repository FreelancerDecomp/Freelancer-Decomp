# AI personalities, encounters, factions, NPC ships and loot

This page documents the INI-style tables that the Content module (`Content.dll`, source in `src/Content/`) reads to drive NPC behaviour: pilot personalities, formations, encounter parameter files, random encounters, faction properties, NPC ship archetypes and loot tables. All files use the common `INI_Reader` grammar and exist as binary (BINI) or plain-text INI; the reader makes no difference between the two (see [parsers.md](parsers.md)).

**File location / how it is found.** Every file on this page is opened with `OpenContentDataIni(relativePath, reader)` ([ContentDataPath.cpp](../../src/Content/ContentDataPath.cpp#L17)): the relative name (backslash separators) is joined to the Data directory, which is the `data path` entry of `freelancer.ini` section `[Freelancer]` (see [freelancer-ini.md](freelancer-ini.md)); no `[Data]` key lists these files, the names are literals in code. The fixed-name tables are loaded once, when the `GameSupport` controller is constructed, by `LoadContentGameTables` ([Encounters.cpp](../../src/Content/Encounters.cpp#L495)). A file that cannot be opened is not reported: the loader runs on an unopened reader and finds no sections. Encounter parameter files are the exception: their names come from the system files (see [systems-and-bases.md](systems-and-bases.md)).

## Conventions used by every table below

* Section names, key names and enumerated words are compared case-insensitively unless a row says "exact" (those use `strcmp`).
* Unknown keys and unknown sections are skipped silently. There is no range validation unless a row says so; where the code does not check a value this page writes "not validated".
* **Repeated sections** are all delivered in file order; whether a later one wins is stated per file ("first definition wins" = a later section with the same nickname is dropped).
* Types: `string`, `int` (`atoi`), `float`, `bool` (the INI_Reader rule: text `true`/`1` or `false`/`0`, anything else logs an error and reads as false), `id` (a nickname that is hashed with `CreateID` and later matched against other tables by that hash, so a misspelled name is never reported). "Value N" means the N-th comma-separated parameter, counted from 0.
* A missing parameter reads as `""` / `0`, so "Default" below is the value the destination holds when the key is absent.

| File (relative to Data path) | Section(s) | Loader | Sibling page |
|---|---|---|---|
| `missions\pilots_population.ini`, `missions\pilots_story.ini` | 16 block sections + `[Pilot]` | [PilotPersonalities.cpp](../../src/Content/PilotPersonalities.cpp#L995) | this page |
| `missions\formations.ini` | `[Formation]` | [Encounters.cpp](../../src/Content/Encounters.cpp#L138) | this page |
| encounter parameter files named by system files | `[EncounterFormation]`, `[Creation]` | [EncounterParams.cpp](../../src/Content/EncounterParams.cpp#L368) | this page |
| `[MsnRandEnc]` of mission files, `[RandomEncounter]` of save data | populator keys | [RandomEncounters.cpp](../../src/Content/RandomEncounters.cpp#L1140) | this page, [missions.md](missions.md) |
| `RandomMissions\KillableSolars.ini`, `RandomMissions\VignetteCriticalLoot.ini` | `[KillableSolar]`, `[critical_loot]` | [RandomEncounters.cpp](../../src/Content/RandomEncounters.cpp#L58) | this page, [random-missions.md](random-missions.md) |
| `missions\faction_prop.ini` | `[FactionProps]` | [FactionProps.cpp](../../src/Content/FactionProps.cpp#L727) | this page |
| `equipment\commodities_per_faction.ini` | `[FactionGood]` | [FactionProps.cpp](../../src/Content/FactionProps.cpp#L887) | this page |
| `missions\empathy.ini` | `[RepChangeEffects]` | [FactionProps.cpp](../../src/Content/FactionProps.cpp#L922) | this page |
| `missions\npcships.ini`, `missions\npcships_test.ini`, per-mission `npc_ship_file` | `[NPCShipArch]` | [NpcShips.cpp](../../src/Content/NpcShips.cpp#L42) | this page |
| `missions\specific_npc.ini` | `[NPC]` | [NpcShips.cpp](../../src/Content/NpcShips.cpp#L160) | this page |
| `missions\lootprops.ini` | `[mLootProps]`, `[PhantomLoot]` | [LootProps.cpp](../../src/Content/LootProps.cpp#L52) | this page |

---

## 1. Pilot personalities: `missions\pilots_population.ini`, `missions\pilots_story.ini`

Both files are read by the same function, `LoadPilotPersonalities` ([PilotPersonalities.cpp:995](../../src/Content/PilotPersonalities.cpp#L995)), population file first, story file second. A file is a flat list of sections:

* 16 **block sections** that each describe one behaviour module (`[EvadeDodgeBlock]`, `[EvadeBreakBlock]`, `[BuzzHeadTowardBlock]`, `[BuzzPassByBlock]`, `[TrailBlock]`, `[StrafeBlock]`, `[EngineKillBlock]`, `[RepairBlock]`, `[GunBlock]`, `[MineBlock]`, `[MissileBlock]`, `[DamageReactionBlock]`, `[MissileReactionBlock]`, `[CountermeasureBlock]`, `[FormationBlock]`, `[JobBlock]`);
* `[Pilot]` sections that name one block of each kind.

Each block starts from the default values of the matching `pub::AI::Personality` sub-structure (constructors in [common_pub.cpp](../../src/Common/common_pub.cpp#L42); the "Default" columns below are those constructor values), then applies the keys it finds. A block is stored under `CreateID(nickname)`; a block without `nickname`, or with a nickname that is already stored, is discarded (first definition wins, per block kind). Because the files are loaded in a fixed order, a nickname defined in `pilots_population.ini` cannot be redefined in `pilots_story.ini`.

Conventions in the block tables: **deg** = the file value is in degrees and is multiplied by pi/180 on load (the default is given in degrees too); "weight key" = `key = NAME, weight` where `NAME` is one of the listed words (case-insensitive, an unlisted word is ignored) and `weight` is value 1 as float; `trueFalse` = the text rule `TRUE` -> true, `FALSE` -> false, **any other text -> true** (case-insensitive, value 0). Every block also accepts `nickname` (string, hashed id of the block).

### 1.1 `[EvadeDodgeBlock]` ([PilotPersonalities.cpp:126](../../src/Content/PilotPersonalities.cpp#L126), defaults [common_pub.cpp:42](../../src/Common/common_pub.cpp#L42))

| Key | Type | Default | Meaning |
|---|---|---|---|
| `evade_activate_range` | float | 500 | Range at which evasion starts. |
| `evade_dodge_cone_angle` | float deg | 45 | Dodge cone angle. |
| `evade_dodge_cone_angle_variance_percent` | float | 0 | Random variance of the cone angle. |
| `evade_dodge_waggle_axis_cone_angle` | float deg | 0 | Waggle axis cone. |
| `evade_dodge_roll_angle` | float deg | 0 | Roll angle. |
| `evade_dodge_interval_time` | float | 1.0 | Time between dodges. |
| `evade_dodge_interval_time_variance_percent` | float | 0 | Variance of the interval. |
| `evade_dodge_distance` | float | 100 | Dodge distance. |
| `evade_dodge_time` | float | 10 | Dodge duration. |
| `evade_dodge_slide_throttle` | float | 0.6 | Throttle while sliding. |
| `evade_dodge_turn_throttle` | float | 0.6 | Turn throttle. |
| `evade_dodge_corkscrew_roll_throttle` | float | 1.0 | Corkscrew roll throttle. |
| `evade_dodge_corkscrew_roll_flip_direction` | trueFalse | false | Flip the corkscrew roll direction. |
| `evade_dodge_corkscrew_turn_throttle` | float | 0.15 | Corkscrew turn throttle. |
| `evade_dodge_direction_weight` | weight key | all 0 | Names `RIGHT`, `LEFT`, `UP`, `DOWN`. |
| `evade_dodge_style_weight` | weight key | all 0 | Names `WAGGLE`, `WAGGLE_RANDOM`, `SLIDE`, `CORKSCREW`. |

### 1.2 `[EvadeBreakBlock]` ([:193](../../src/Content/PilotPersonalities.cpp#L193), defaults [common_pub.cpp:74](../../src/Common/common_pub.cpp#L74))

| Key | Type | Default | Meaning |
|---|---|---|---|
| `evade_break_time` | float | 3.0 | Break duration. |
| `evade_break_interval_time` | float | 100000 | Time between breaks. |
| `evade_break_afterburner_delay` | float | 0 | Delay before the afterburner is used. |
| `evade_break_afterburner_delay_variance_percent` | float | 0 | Variance of that delay. |
| `evade_break_direction_weight` | weight key | all 0 | `RIGHT`, `LEFT`, `UP`, `DOWN`. |
| `evade_break_roll_throttle` | float | 0 | Roll throttle. |
| `evade_break_turn_throttle` | float | 1.0 | Turn throttle. |
| `evade_break_style_weight` | weight key | all 0 | `SIDEWAYS`, `OUTRUN`, `REVERSE`. |
| `evade_break_attempt_reverse_time` | float | 4.0 | Time spent trying to reverse. |
| `evade_break_reverse_distance` | float | 150 | Reverse distance. |

### 1.3 `[BuzzHeadTowardBlock]` ([:246](../../src/Content/PilotPersonalities.cpp#L246), defaults [common_pub.cpp:99](../../src/Common/common_pub.cpp#L99))

| Key | Type | Default | Meaning |
|---|---|---|---|
| `buzz_head_toward_style_weight` | weight key | all 0 | `STRAIGHT_TO`, `SLIDE`, `WAGGLE`. |
| `buzz_min_distance_to_head_toward` | float | 200 | Minimum distance of the head-toward run. |
| `buzz_min_distance_to_head_toward_variance_percent` | float | 1.0 | Variance. |
| `buzz_max_time_to_head_away` | float | 4.0 | Maximum time before turning away. |
| `buzz_head_toward_engine_throttle` | float | 1.0 | Engine throttle. |
| `buzz_head_toward_turn_throttle` | float | 1.0 | Turn throttle. |
| `buzz_head_toward_roll_throttle` | float | 0 | Roll throttle. |
| `buzz_head_toward_roll_flip_direction` | trueFalse | false | Flip roll direction. |
| `buzz_slide_throttle` | float | 1.0 | Slide throttle. |
| `buzz_slide_interval_time` | float | 2.0 | Slide interval. |
| `buzz_slide_interval_time_variance_percent` | float | 0.4 | Variance. |
| `buzz_dodge_direction_weight` | weight key | all 0 | `RIGHT`, `LEFT`, `UP`, `DOWN`. |
| `buzz_dodge_turn_throttle` | float | 1.0 | Turn throttle while dodging. |
| `buzz_dodge_cone_angle` | float deg | 45 | Dodge cone. |
| `buzz_dodge_cone_angle_variance_percent` | float | 0 | Variance. |
| `buzz_dodge_waggle_axis_cone_angle` | float deg | 0 | Waggle axis cone. |
| `buzz_dodge_roll_angle` | float deg | 0 | Roll angle. |
| `buzz_dodge_interval_time` | float | 2.0 | Interval. |
| `buzz_dodge_interval_time_variance_percent` | float | 0 | Variance. |

### 1.4 `[BuzzPassByBlock]` ([:318](../../src/Content/PilotPersonalities.cpp#L318), defaults [common_pub.cpp:133](../../src/Common/common_pub.cpp#L133))

| Key | Type | Default | Meaning |
|---|---|---|---|
| `buzz_pass_by_style_weight` | weight key | all 0 | `BREAK_AWAY`, `STRAIGHT_BY`, `ENGINE_KILL`. |
| `buzz_distance_to_pass_by` | float | 100 | Pass-by distance. |
| `buzz_pass_by_time` | float | 3.0 | Pass-by duration. |
| `buzz_break_direction_weight` | weight key | all 0 | `RIGHT`, `LEFT`, `UP`, `DOWN`. |
| `buzz_break_direction_cone_angle` | float | 0 | Stored as written, **no degree conversion**. |
| `buzz_break_turn_throttle` | float | 1.0 | Turn throttle. |
| `buzz_pass_by_roll_throttle` | float | 0 | Roll throttle. |
| `buzz_drop_bomb_on_pass_by` | trueFalse | false | Drop a bomb when passing. |

### 1.5 `[TrailBlock]`, `[StrafeBlock]`, `[EngineKillBlock]`, `[RepairBlock]`

| Section | Key | Type | Default | Source |
|---|---|---|---|---|
| `[TrailBlock]` | `trail_lock_cone_angle` | float deg | 30 | [:367](../../src/Content/PilotPersonalities.cpp#L367), [defaults](../../src/Common/common_pub.cpp#L156) |
| | `trail_break_time` | float | 2.5 | |
| | `trail_min_no_lock_time` | float | 4.0 | |
| | `trail_break_roll_throttle` | float | 0 | |
| | `trail_break_afterburner` | trueFalse | false | |
| | `trail_max_turn_throttle` | float | 1.0 | |
| | `trail_distance` | float | 150 | |
| `[StrafeBlock]` | `strafe_run_away_distance` | float | 150 | [:393](../../src/Content/PilotPersonalities.cpp#L393), [defaults](../../src/Common/common_pub.cpp#L173) |
| | `strafe_attack_throttle` | float | 1.0 | |
| | `strafe_turn_throttle` | float | 1.0 | |
| `[EngineKillBlock]` | `engine_kill_search_time` | float | 6.0 | [:411](../../src/Content/PilotPersonalities.cpp#L411), [defaults](../../src/Common/common_pub.cpp#L186) |
| | `engine_kill_face_time` | float | 3.0 | |
| | `engine_kill_use_afterburner` | trueFalse | false | Stored as float 1.0 / 0.0. |
| | `engine_kill_afterburner_time` | float | 0.5 | |
| | `engine_kill_max_target_distance` | float | 700 | |
| `[RepairBlock]` | `use_shield_repair_pre_delay` | float | 2.0 | [:436](../../src/Content/PilotPersonalities.cpp#L436), [defaults](../../src/Common/common_pub.cpp#L201) |
| | `use_shield_repair_post_delay` | float | 3.0 | |
| | `use_shield_repair_at_damage_percent` | float | 0.5 | |
| | `use_hull_repair_pre_delay` | float | 1.0 | |
| | `use_hull_repair_post_delay` | float | 1.0 | |
| | `use_hull_repair_at_damage_percent` | float | 0.5 | |

### 1.6 `[GunBlock]`, `[MineBlock]`, `[MissileBlock]`

| Section | Key | Type | Default | Meaning / allowed values |
|---|---|---|---|---|
| `[GunBlock]` ([:460](../../src/Content/PilotPersonalities.cpp#L460), [defaults](../../src/Common/common_pub.cpp#L223)) | `gun_fire_interval_time` | float | 0 | |
| | `gun_fire_interval_variance_percent` | float | 0.5 | |
| | `gun_fire_burst_interval_time` | float | 1.5 | |
| | `gun_fire_burst_interval_variance_percent` | float | 0.1 | |
| | `auto_turret_burst_interval_variance_percent` | float | 0.1 | |
| | `gun_fire_no_burst_interval_time` | float | 0.1 | |
| | `gun_fire_accuracy_cone_angle` | float deg | 0 | |
| | `gun_fire_accuracy_power` | float | 1.0 | |
| | `gun_fire_accuracy_power_npc` | float | 6.0 | |
| | `gun_target_point_switch_time` | float | 1.5 | |
| | `gun_range_threshold` | float | 1.0 | |
| | `gun_range_threshold_variance_percent` | float | 0.2 | |
| | `auto_turret_interval_time` | float | 0 | |
| | `auto_turret_burst_interval_time` | float | 1.0 | |
| | `auto_turret_no_burst_interval_time` | float | 0 | |
| | `fire_style` | string | `MULTIPLE` | `MULTIPLE` stores 0; **any other text stores 1** (`SINGLE` is the documented word). |
| `[MineBlock]` ([:515](../../src/Content/PilotPersonalities.cpp#L515), [defaults](../../src/Common/common_pub.cpp#L249)) | `mine_launch_interval` | float | 0 | |
| | `mine_launch_cone_angle` | float deg | 22.5 | |
| | `mine_launch_range` | float | 400 | |
| `[MissileBlock]` ([:533](../../src/Content/PilotPersonalities.cpp#L533), [defaults](../../src/Common/common_pub.cpp#L262)) | `missile_launch_range` | float | -1 | |
| | `missile_launch_allow_out_of_range` | trueFalse | false | |
| | `missile_launch_interval_time` | float | 5.0 | |
| | `missile_launch_interval_variance_percent` | float | 0.4 | |
| | `missile_launch_cone_angle` | float deg | 22.5 | |
| | `anti_cruise_missile_min_distance` | float | 150 | |
| | `anti_cruise_missile_max_distance` | float | 2500 | |
| | `anti_cruise_missile_pre_fire_delay` | float | 2.0 | |
| | `anti_cruise_missile_interval_time` | float | 5.0 | |

### 1.7 Reaction and countermeasure blocks

| Section | Key | Type | Default | Source |
|---|---|---|---|---|
| `[DamageReactionBlock]` | `evade_break_damage_trigger_percent` | float | 1.0 | [:563](../../src/Content/PilotPersonalities.cpp#L563), [defaults](../../src/Common/common_pub.cpp#L281) |
| | `evade_dodge_more_damage_trigger_percent` | float | 0.2 | |
| | `drop_mines_damage_trigger_percent` | float | 1.0 | |
| | `drop_mines_damage_trigger_time` | float | 1.0 | |
| | `engine_kill_face_damage_trigger_percent` | float | 1.0 | |
| | `engine_kill_face_damage_trigger_time` | float | 2.0 | |
| | `roll_damage_trigger_percent` | float | 1.0 | |
| | `roll_damage_trigger_time` | float | 1.0 | |
| | `afterburner_damage_trigger_percent` | float | 1.0 | |
| | `afterburner_damage_trigger_time` | float | 1.5 | |
| | `brake_reverse_damage_trigger_percent` | float | 1.0 | |
| | `fire_guns_damage_trigger_percent` | float | 1.0 | |
| | `fire_guns_damage_trigger_time` | float | 1.0 | |
| | `fire_missiles_damage_trigger_percent` | float | 1.0 | |
| | `fire_missiles_damage_trigger_time` | float | 0.5 | |
| `[MissileReactionBlock]` | `evade_missile_distance` | float | 250 | [:606](../../src/Content/PilotPersonalities.cpp#L606), [defaults](../../src/Common/common_pub.cpp#L308) |
| | `evade_break_missile_reaction_time` | float | 0 | |
| | `evade_slide_missile_reaction_time` | float | 0 | |
| | `evade_afterburn_missile_reaction_time` | float | 0 | |
| `[CountermeasureBlock]` | `countermeasure_active_time` | float | 1.0 | read inline in [:1121](../../src/Content/PilotPersonalities.cpp#L1121), [defaults](../../src/Common/common_pub.cpp#L322) |
| | `countermeasure_unactive_time` | float | 0 | |

### 1.8 `[FormationBlock]` ([:627](../../src/Content/PilotPersonalities.cpp#L627), defaults [common_pub.cpp:334](../../src/Common/common_pub.cpp#L334))

| Key | Type | Default | Meaning |
|---|---|---|---|
| `force_attack_formation_active_time` | float | 1.0 | |
| `force_attack_formation_unactive_time` | float | 0 | |
| `break_formation_damage_trigger_percent` | float | 1.0 | |
| `break_formation_damage_trigger_time` | float | 4.0 | |
| `break_apart_formation_damage_trigger_percent` | float | 1.0 | |
| `break_apart_formation_damage_trigger_time` | float | 4.0 | |
| `break_formation_missile_reaction_time` | float | 0 | |
| `break_apart_formation_missile_reaction_time` | float | 4.0 | |
| `break_apart_formation_on_buzz_head_toward` | trueFalse | false | |
| `break_formation_on_buzz_head_toward_time` | float | 4.0 | |
| `regroup_formation_on_buzz_head_toward` | trueFalse | false | |
| `break_apart_formation_on_buzz_pass_by` | trueFalse | false | |
| `break_formation_on_buzz_pass_by_time` | float | 4.0 | |
| `regroup_formation_on_buzz_pass_by` | trueFalse | false | |
| `break_apart_formation_on_evade_dodge` | trueFalse | false | |
| `break_formation_on_evade_dodge_time` | float | 4.0 | |
| `regroup_formation_on_evade_dodge` | trueFalse | false | |
| `break_apart_formation_on_evade_break` | trueFalse | false | |
| `break_formation_on_evade_break_time` | float | 4.0 | |
| `regroup_formation_on_evade_break` | trueFalse | false | |
| `formation_exit_mode` | string | `NOTHING` | `BREAK_AWAY_FROM_CENTER` (0), `BREAK_AWAY_FROM_CENTER_AFTERBURNER` (1), `BRAKE_REVERSE` (2), `OUTRUN` (3), `NOTHING` (5). Any other text leaves the default. |
| `formation_exit_top_turn_break_away_throttle` | float | 1.0 | |
| `formation_exit_roll_outrun_throttle` | float | 0 | |
| `formation_exit_max_time` | float | 0 | |
| `leader_makes_me_tougher` | trueFalse | false | |

### 1.9 `[JobBlock]` ([:772](../../src/Content/PilotPersonalities.cpp#L772), defaults [common_pub.cpp:372](../../src/Common/common_pub.cpp#L372))

A "toughness word" is `EASIEST` (0), `EASY` (1), `EQUAL` (2), `HARD` (3) or `HARDEST` (4); any other text reads as `EQUAL` (2). Ship class words are `FIGHTER`, `FREIGHTER`, `TRANSPORT`, `GUNBOAT`, `CRUISER`, `CAPITAL`, `TRADELANE`, `JUMPGATE`, `WEAPONS_PLATFORM`, `DESTROYABLE_DEPOT`, `SOLAR`, `ANYTHING` (table in the source at [PilotPersonalities.cpp:83](../../src/Content/PilotPersonalities.cpp#L83)).

| Key | Type | Default | Meaning / allowed values |
|---|---|---|---|
| `loot_preference` | string | none (0) | Substring tests on the whole text: contains `LT_ALL` -> 7; else contains `LT_NONE` -> 0; else the OR of `LT_COMMODITIES` (1), `LT_EQUIPMENT` (2), `LT_POTIONS` (4). |
| `loot_flee_threshold` | toughness word | EQUAL (2) | |
| `flee_scene_threat_style` | toughness word | HARD (3) | |
| `flee_when_hull_damaged_percent` | float | 0 | |
| `flee_when_leader_flees_style` | trueFalse | true | |
| `flee_no_weapons_style` | trueFalse | true | |
| `scene_toughness_threshold` | toughness word | EQUAL (2) | |
| `target_toughness_preference` | toughness word | EQUAL (2) | |
| `wait_for_leader_target` | trueFalse | false | |
| `maximum_leader_target_distance` | float | 5000 | |
| `combat_drift_distance` | - | - | Recognised, value discarded. |
| `attack_order` | list of ship class words | one record `ANYTHING`, range 5000, all weapon types | Separators: space, comma, tab. Replaces the default list; each known word adds a record with range 5000 and all weapon types. The word `ANYTHING` adds **two** records (retail behaviour). Unknown words are skipped. |
| `attack_preference` | repeated: class word, range, weapons | see `attack_order` | Value 0 ship class word, value 1 float range, value 2 text containing any of `GUNS` (1), `GUIDED` (2), `UNGUIDED` (4), `TORPEDO` (8), tested on the upper-cased text. `UNGUIDED` also contains `GUIDED`, so it sets both bits 2 and 4. The n-th occurrence fills record n; at most 12 records. An unknown class word leaves that record as it was; the list is always terminated after the record just processed. |
| `attack_subtarget_order` | list of words | `ANYTHING` | Separators space, comma, tab. Words: `ANYTHING` (6), `GUNS` (0), `TURRETS` (1), `LAUNCHERS` (2), `TOWERS` (3), `ENGINES` (4), `HULL` (5); unknown words are skipped. The list is not length-checked. |
| `field_targeting` | string | `ALWAYS` (3) | `NEVER` (0), `LOW_DENSITY` (1), `HIGH_DENSITY` (2), `ALWAYS` (3); any other text -> 1. |
| `force_attack_formation` | trueFalse | false | Also marks the value as "set": a job that sets it overrides the pilot's earlier value, a job that does not leaves it. |
| `allow_player_targeting` | trueFalse | true | |

### 1.10 `[Pilot]` ([:877](../../src/Content/PilotPersonalities.cpp#L877))

A pilot is a record of references (block nicknames), stored under `CreateID(nickname)`; a pilot without `nickname` is dropped and a repeated nickname keeps its first definition.

| Key | Type | Meaning |
|---|---|---|
| `nickname` | id | Pilot id; the key other tables use (`pilot` in NPC ship archetypes, mission and random-encounter files, solar NPC objects). |
| `inherit` | id | Another **already loaded** pilot. At the moment the key is read, every non-empty block reference of that parent overwrites the current ones; the parent's own `inherit` is not followed. Keys placed after `inherit` override the parent, keys placed before it are overwritten. An unknown parent changes nothing. |
| `evade_dodge_id`, `evade_break_id`, `buzz_head_toward_id`, `buzz_pass_by_id`, `trail_id`, `strafe_id`, `engine_kill_id`, `repair_id`, `gun_id`, `mine_id`, `missile_id`, `damage_reaction_id`, `missile_reaction_id`, `countermeasure_id`, `formation_id`, `job_id` | id | Nickname of the block of that kind. A reference to a block that does not exist is ignored when the personality is built (the default block stays). |

Building a personality (`GetPilotPersonality`, [:1217](../../src/Content/PilotPersonalities.cpp#L1217)): every referenced block replaces the corresponding part of a default `Personality`; then, if the pilot has a `job_id`, the inherited pilot's job is applied first and the pilot's own job second. The pilot id also reaches the personality through the `pilot` keys of other files.

---

## 2. Formations: `missions\formations.ini`

Read by `LoadFormation` ([Encounters.cpp:138](../../src/Content/Encounters.cpp#L138)) for every `[Formation]` section. Formations are keyed by `CreateID(nickname)`; **a later section with the same nickname is dropped** (first definition wins). A section without `nickname` is stored under id 0.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `nickname` | id | none | Formation id, referenced by `formation` in encounter files and random-encounter sections. |
| `pos` | 3 floats (x, y, z) | none | One ship slot. Repeat the key once per slot; slots keep file order and index. |
| `pl_pos` | 3 floats (x, y, z) | none | A slot reserved for the player. Shares the slot list with `pos` (the order of the two keys fixes the slot indices). |

Example (synthetic):

```
[Formation]
nickname = example_wedge
pos = 0, 0, 0
pos = -60, 0, -40
pos = 60, 0, -40
```

---

## 3. Encounter parameter files

An encounter parameter file defines what a zone can spawn. The file name is given by the `[EncounterParameters]` section of a system file (second value line, see [systems-and-bases.md](systems-and-bases.md#31-encounterparameters)), the path is relative to the Data path, and the file is parsed by the constructor of `FEncounterParameters` ([EncounterParams.cpp:368](../../src/Content/EncounterParams.cpp#L368)) once per `[EncounterParameters]` entry. The file must contain at least one `[EncounterFormation]` section; without one the code indexes an empty array (undefined behaviour, as in retail). A zone then selects an encounter with `encounter = id, level, chance` (see [systems-and-bases.md](systems-and-bases.md#34-zone-sections-zonescpp-and-encounterscpp)).

### 3.1 `[EncounterFormation]`

Parser: [EncounterParams.cpp:115](../../src/Content/EncounterParams.cpp#L115), defaults set in the constructor at [:93](../../src/Content/EncounterParams.cpp#L93). One section per formation that the encounter can create. Ship lines are collected in file order; `make_class` and `pilot_job` always modify the **most recent** ship line.

| Key | Type | Default | Meaning / allowed values |
|---|---|---|---|
| `ship_by_class` | int min, int max, id class, [int offset] | - | Adds a ship line that is filled from a class of NPC ships. Value 0 minimum count, value 1 maximum count (both read unconditionally, a missing one is 0), value 2 class id: either the nickname of a `[ShipClass]` in `missions\shipclasses.ini` (an NPC ship archetype fits if one of that class's `member` ids is in its `npc_class` list) or directly an `npc_class` word of an archetype (or a mission property of its ship in `shiparch.ini`); see [random-missions.md](random-missions.md), value 3 optional difficulty offset (0 if absent). When the encounter is bound to a zone with level L the class `diff<N>` is added to the line, with `N = max(1, L + offset)`; an archetype fits a line only if it satisfies **every** class of the line, so archetypes are expected to carry the matching `diff<N>` word in their `npc_class`. |
| `ship_by_npc_arch` | int min, int max, id arch | - | Adds a ship line that names one `[NPCShipArch]` nickname directly (section 6). |
| `make_class` | list of ids | - | All values are added (duplicates ignored) to the last ship line's extra class list (accepted as additional classes of the line). Ignored if no ship line precedes. |
| `pilot` | - | - | Recognised and ignored. |
| `pilot_job` | id | empty | Job (a `[JobBlock]` nickname) for the last ship line; ignored if no ship line precedes. |
| `formation` | id | empty | Formation from `formations.ini`. Taken only while neither `formation` nor `formation_by_class` has been set (first one wins). |
| `formation_by_class` | id | empty | Same rule, mutually exclusive with `formation`. |
| `behavior` | string | 0 | Exact (case-sensitive) words: `nothing` (1), `trade` (2), `patrol_path` (3), `wander` (4), `goto_guide` (5), `dock_guide` (6), `patrol_guide` (7). Any other non-empty text -> 1; absent -> 0. Parser: [Guides.cpp:83](../../src/Content/Guides.cpp#L83). |
| `arrival` | list of words | all places allowed | Resets the arrival mask to "none", then each word sets a place: `all`, `tradelane`, `object_all`, `object_docking_ring`, `object_capital`, `object_jump_gate`, `object_station`, `cruise`, `buzz`. A leading `-` makes that word clear instead of set (so it only matters after `all` or `object_all`). Unknown words are ignored; words are cut at 49 characters. |
| `longevity` | int | 24 | |
| `allow_simultaneous_creation` | string | `true` | Exact lower-case `yes`/`true` -> true, `no`/`false` -> false, other text leaves the default. |
| `zone_creation_distance` | int | 1500 | Stored as a double. |
| `times_to_create` | `infinite` or int | infinite (-1) | Exact `infinite`, otherwise `atoi`. |
| `feeling_to_formation` | string, int | mask 7, value -1 | Value 0: `friendly` (mask 1), `hostile` (4), `same` (8), `any` (7); other text leaves the mask. Value 1: int feeling value. Only the first occurrence is evaluated; after it, the other `feeling_*` keys are ignored. |
| `feeling_to_local`, `feeling_to_group`, `explicit_group` | - | - | Recognised (until `feeling_to_formation` has been read) but only reset the feeling value to -1; their values are not used. |

`group_reroll_frequency` (`synch`, `every`, `once`) exists in the parser, but the only call site never passes that key, so the branch is unreachable and the reroll mode stays `every`.

### 3.2 `[Creation]`

Parser: [EncounterParams.cpp:411](../../src/Content/EncounterParams.cpp#L411).

| Key | Type | Default | Meaning |
|---|---|---|---|
| `permutation` | string, int weight, [int reroll interval] | see below | Repeatable. Value 0 is a list of formation indices (0-based position of the `[EncounterFormation]` section in the file) separated by blanks or `+`; a `*` after an index marks that formation as starred (created whenever the zone refreshes). Example `0 + 1* + 2`. Value 1 is the selection weight, read as an **int**. Value 2 (optional int) sets the encounter's reroll interval; the last one given wins. Any character other than blank, `+`, digit or `*` starts a new number (not validated). |

If the file has no `permutation` key, one permutation containing every formation (indices 0..n-1, none starred) with weight 1 is created.

---

## 4. Random encounters

A random encounter is a scripted ambush with a "populator" (when it triggers) and a parameter object (what it creates). It is declared in a **mission file** as an `[MsnRandEnc]` section ([missions.md](missions.md)) and in **save data** as a `[RandomEncounter]` section. In both, `nickname` and `encounter_type` are read by the mission code, `encounter_type` selecting the populator: `TL_Attack` (case-insensitive, attack on a trade lane) or `SP_Attack` (attack in open space); any other value keeps the previous objects. The populator then re-reads the **same section** (the reader state is saved and restored, so the mission loader's own pass is unaffected) with the keys below ([TL: RandomEncounters.cpp:1140](../../src/Content/RandomEncounters.cpp#L1140), [SP: :1469](../../src/Content/RandomEncounters.cpp#L1469)). The same objects write these keys back when a game is saved.

### 4.1 Keys shared by `TL_Attack` and `SP_Attack`

| Key | Type | Default | Meaning |
|---|---|---|---|
| `target_ship_name` | string | empty | Mission-object name of the ship the encounter targets (at most 47 characters). Empty means the player's ship. |
| `attacker_rep_name` | string | -1 | Reputation group nickname of the attackers. |
| `attacker_rep` | int | -1 | Same as a raw group id (saved games). The last of the two keys wins. |
| `pilot` | id | none | Pilot personality nickname for the attackers (section 1). |
| `pilot_id` | int | none | Same as a raw id. |
| `label` | id | none | Repeatable; each value is hashed and added to the label list of the created ships. |
| `formation` | id nickname, float, int, int, then pairs (class id, float) | - | Repeatable. Value 0 is both the entry's nickname and the `formations.ini` formation. Value 1 float formation weight. Values 2 and 3 are the minimum and maximum ship count. From value 4 on, pairs `ship class id, weight`; a class that is unknown or empty is skipped. In `TL_Attack`, a pair whose weight is missing is also skipped. |
| `orientation` | float deg, [float] | 90 deg, 2500 | Value 0 spawn angle in degrees, optional value 1 spawn distance. |
| `radius` | float | 7 | Clearance radius for the spawn point (truncated to int); 0 or less skips the check. |
| `num_forms` | int min, int max | 0, 0 | Number of formations to create; the count for this activation is drawn immediately with `rand()` between min and max inclusive. |

### 4.2 `TL_Attack` only (trade-lane ambush; [RandomEncounters.cpp:1140](../../src/Content/RandomEncounters.cpp#L1140))

| Key | Type | Default | Meaning |
|---|---|---|---|
| `target_tradelane_name` | string | 0 | Nickname of the ring object to attack. |
| `target_tradelane` | int | 0 | Same as a raw object id. |
| `tl_attack_chance` | int | 20 | Percent chance; sets both the working value and the value restored on `reset`. |
| `tl_attack_chance_read_in` | int | 20 | Sets only the restore value. |
| `saved_formation` | int id, float, int, int, pairs | - | Like `formation`, but value 0 is a raw numeric id (used by saved games). |

### 4.3 `SP_Attack` only (open-space ambush; [RandomEncounters.cpp:1469](../../src/Content/RandomEncounters.cpp#L1469))

| Key | Type | Default | Meaning |
|---|---|---|---|
| `activation_type` | string, then floats | `time`, 180 to 300 s | Only the first accepted line counts. `location, x, y, z, range`: triggers when the target is within that range (the range is stored squared, as int; default 2500). `time, minSeconds, maxSeconds`: value 1 is the lower and value 2 the upper bound; the trigger time is picked with `rand()` between the two truncated values. Other words are ignored. Without the key, the first activation check behaves as `time` with bounds 180 and 300 seconds. |
| `formation_position` | 3 floats | none | Repeatable fixed position (weight 1.0) for the formation. |
| `arrival_obj` | int or string | 0 | If `atoi` of the text is non-zero the number is used as an object id, otherwise the text is a nickname. Dock target for the spawned ships. |

### 4.4 `RandomMissions\KillableSolars.ini` - `[KillableSolar]` ([RandomEncounters.cpp:58](../../src/Content/RandomEncounters.cpp#L58))

Loaded by the random-mission generator ([random-missions.md](random-missions.md)); a table of destroyable stations/objects for vignettes. An entry is **dropped** unless it passes the checks in the last paragraph. Entries are stored under `CreateID(nickname)` (first definition wins).

| Key | Type | Default | Meaning / allowed values |
|---|---|---|---|
| `nickname` | id | none | Required. |
| `solar_type` | string | none | One of `big_solar` (0), `defensive_solar` (1), `killable_neutral` (2), `background_solar` (3); unknown words leave it unset and the entry is dropped. |
| `string_id` | int | 0 | Resource id of the display name. |
| `faction` | list of reputation group nicknames, or `all` | none | `all` (case-insensitive) matches every faction; otherwise every value is a group, duplicates ignored. |
| `archetype` | string | none | Archetype nickname (hashed with `GetNicknameId`); required. |
| `loadout` | string | none | Loadout nickname; required. |
| `hitpoints` | int, [int] | none | Minimum and optional maximum (the maximum defaults to the minimum). If the maximum exceeds the archetype's hit points, both are clamped to the archetype's. |
| `difficulty` | float, float | 0, 0 | Minimum and maximum difficulty. |
| `only_allowed_in_zone_type` | list of words | none (no restriction) | `rock`, `debris`, `ice`, `lava`, `crystal` (case-insensitive); other words ignored. |
| `pilot_choices` | int | 1 | |

Validation (entry dropped otherwise): nickname present; known `solar_type`; `faction` given (or `all`); archetype and loadout non-zero; `hitpoints` minimum > 0 and maximum >= minimum; `0 <= difficulty minimum <= maximum <= 100`.

### 4.5 `RandomMissions\VignetteCriticalLoot.ini` - `[critical_loot]` ([RandomEncounters.cpp:256](../../src/Content/RandomEncounters.cpp#L256))

Every section is appended in file order (no duplicate check).

| Key | Type | Default | Meaning |
|---|---|---|---|
| `faction` | list of group nicknames, or `all` | none | As in `[KillableSolar]`. |
| `archetype` | string | 0 | Equipment/commodity nickname (`GetEquipmentID`). |
| `name` | int | 0 | Resource id of the item name. |
| `why_desired` | int, int | 0, 0 | Two resource ids. |
| `type` | list of words | empty | `destroy` (1), `tractor` (2), `return` (3); other words are ignored, duplicates ignored. |

---

## 5. Factions

### 5.1 `missions\faction_prop.ini` - `[FactionProps]` ([FactionProps.cpp:727](../../src/Content/FactionProps.cpp#L727))

One section per faction. A section is stored under the group of the most recent `affiliation` key (the variable is not reset between sections, so a section without `affiliation` reuses the previous section's group, initially 0); two tables are filled (properties and name tables) and a second section for the same group is dropped (first definition wins). Defaults come from the constructor at [FactionProps.cpp:217](../../src/Content/FactionProps.cpp#L217).

| Key | Type | Default | Meaning / allowed values |
|---|---|---|---|
| `affiliation` | string | group 0 | Reputation group nickname of the faction. Also fetches the group's short name for the name tables. Not validated against the groups. |
| `scan_announce` | bool | true | |
| `scan_chance` | float | 1.0 | |
| `scan_for_cargo` | string good nickname, int level | - | Repeatable. Value 0 is a good (hashed to an archetype id), value 1 is `0`, `1` or `2` (anything else is stored as 0). An identical pair is not added twice. |
| `formation` | id, id | - | Repeatable map entry `CreateID(value 0) -> CreateID(value 1)`; a repeated key keeps its first value. |
| `npc_ship` | id | - | Repeatable; an `[NPCShipArch]` nickname (section 6) the faction may fly. |
| `msg_id_prefix` | string | empty | Comm message prefix (shared reader of `CommReferrable`). |
| `mc_costume` | string | 0 | Costume nickname for the faction's bar characters (`GetCostumeID`). |
| `space_costume` | string head, string body, accessories... | - | Repeatable. Value 0 head body part, value 1 body part, values 2.. accessory nicknames. |
| `voice` | string | - | Repeatable voice nickname (`GetVoiceID`). |
| `jump_preference` | string | any (0) | Exact `jumpgate` (1) or `jumphole` (2); any other text -> any. |
| `assassinate_target` | id | - | Repeatable. |
| `Legality` | string | unset | `lawful` or `unlawful`; sets the faction's legality name ids. |
| `nickname_plurality` | string | `singular` | `singular` or `plural`; other text leaves the default. |
| `firstname_male`, `firstname_female`, `lastname`, `formation_desig`, `large_ship_names` | int first, [int last] | empty | Repeatable ranges of consecutive resource ids; a missing second value repeats the first. Each key keeps its own list; the counts add up. |
| `firstname` | - | - | Recognised and ignored. |
| `large_ship_desig` | int | 0 | |
| `rank_desig` | 5 ints | 0 | Values 0-2 are the designation ids for three rank bands, values 3 and 4 the two rank thresholds (rank below threshold 3 uses designation 0, below threshold 4 designation 1, otherwise designation 2). |

### 5.2 `equipment\commodities_per_faction.ini` - `[FactionGood]` ([FactionProps.cpp:887](../../src/Content/FactionProps.cpp#L887))

| Key | Type | Meaning |
|---|---|---|
| `faction` | string | Reputation group nickname; selects the faction whose list is extended. A faction without a `[FactionProps]` entry makes the following `MarketGood` lines fault (no null check, as in retail). |
| `MarketGood` | string | Repeatable; good nickname (`GetGoodID`) appended to the faction's list. `faction` must precede it in the same section. |

### 5.3 `missions\empathy.ini` - `[RepChangeEffects]` ([FactionProps.cpp:922](../../src/Content/FactionProps.cpp#L922))

The file is opened through the path string passed by `LoadContentGameTables`; a missing file leaves the table empty. A section is stored under its `group` (a second section for the same group is dropped); the empathy rates are sorted by rate ascending when the section ends.

| Key | Type | Meaning |
|---|---|---|
| `group` | string | Reputation group nickname the effects apply to. A section without it is stored under group -1. |
| `empathy_rate` | string group, float rate | Repeatable. When the player's reputation with `group` changes by D, the reputation with the named group changes by `D * rate`. |
| `event` | string, float | Per-event rate. Value 0 (exact words, case-insensitive): `object_destruction`, `random_mission_success`, `random_mission_failure`, `random_mission_abortion`; other words are ignored. Value 1 the rate. |

---

## 6. NPC ships and characters

### 6.1 `missions\npcships.ini`, `missions\npcships_test.ini` and per-mission files - `[NPCShipArch]` ([NpcShips.cpp:42](../../src/Content/NpcShips.cpp#L42))

`npcships_test.ini` is read right after `npcships.ini` into the same table. A mission file can name a private file with `npc_ship_file` ([missions.md](missions.md)); it is parsed by the same function into the mission's own table. Entries are stored under `CreateID(nickname)`; the first definition of a nickname wins.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `nickname` | id | none | Archetype id (referenced by `ship_by_npc_arch`, `npc_ship`, `npc_ship_arch`). |
| `ship_archetype` | string | 0 | Ship nickname (`GetShipID`, see [archetypes-and-equipment.md](archetypes-and-equipment.md)). |
| `pilot` | id | empty | Pilot personality nickname (section 1). |
| `loadout` | string | 0 | Loadout nickname (`GetLoadoutID`). |
| `state_graph` | string | -1 | Name of a leader state graph in `AI\state_graph.db` (compared case-insensitively); unknown names give -1. |
| `npc_class` | list of ids | empty | All values are added (duplicates ignored). A `ship_by_class` line (section 3.1) fits this archetype when its class, or one member of the `[ShipClass]` it names, is in this list. |
| `level` | string | 0 | Leading non-digit characters are skipped, then `atoi`; so `d5` reads as 5. |

### 6.2 `missions\specific_npc.ini` - `[NPC]` ([NpcShips.cpp:160](../../src/Content/NpcShips.cpp#L160))

Read by the `Story` launcher controller at construction ([StoryLauncher.cpp:36](../../src/Content/StoryLauncher.cpp#L36)). Stored under `CreateID(nickname)`, first definition wins; the result of the parse is not checked, so a record that lacks a body part, voice or individual name is still inserted (it is merely "incomplete"). The same keys (`ParseValue`) are used for `[GF_NPC]` records of bases ([systems-and-bases.md](systems-and-bases.md#5-missionsmbasesini)).

| Key | Type | Default | Meaning |
|---|---|---|---|
| `nickname` | id | none | Record id. |
| `base_appr` | string | none | Costume nickname (`GetCostumeID`); if it resolves, the whole appearance is copied from it. Later `head`/`body`/... lines overwrite single parts. |
| `head` | string | 0 | Head body part. |
| `body` | string | 0 | Body body part. |
| `lefthand`, `righthand` | string | 0 | Hand body parts. |
| `accessory` | string | - | Repeatable accessory nickname; the count grows per line. |
| `voice` | string | 0 | Voice nickname (`GetVoiceID`). |
| `individual_name` | int | 0 | Resource id of the character's name. |
| `affiliation` | string | -1 | Reputation group nickname. |

### 6.3 `[NPC]` blocks of mission files ([NpcShips.cpp:356](../../src/Content/NpcShips.cpp#L356))

Mission files ([missions.md](missions.md)) declare actors with `[NPC]`; `LoadMsnNpc` reads these keys. The function's "has a nickname" result is not tested by its caller: the record is stored under its nickname id (first definition wins), a record without a nickname is stored under id 0.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `nickname` | id | none | Required. |
| `space_costume` | string head, string body, accessories... | 0 | As in `[FactionProps]`. |
| `voice` | string | 0 | Voice nickname (cut at 79 characters). |
| `npc_ship_arch` | id | empty | `[NPCShipArch]` nickname. |
| `affiliation` | string | -1 | Reputation group nickname. |
| `individual_name` | int | 0 | Resource id. |

### 6.4 Solar NPCs

The `[Object]` sections of system files can turn a solar object into an NPC with the keys `base`, `reputation`, `pilot`, `behavior` and `msg_id_prefix`; they are read by `ParseSolarNpcKey` ([NpcCreate.cpp:23](../../src/Content/NpcCreate.cpp#L23)) and are documented in [systems-and-bases.md](systems-and-bases.md#33-object-sections-solar-npc-keys).

---

## 7. Loot: `missions\lootprops.ini`

The file is read twice, with a `reset()` in between: `[mLootProps]` sections by `LoadLootProps` ([LootProps.cpp:52](../../src/Content/LootProps.cpp#L52)) and `[PhantomLoot]` sections by `LoadPhantomLoot` ([:89](../../src/Content/LootProps.cpp#L89)). Records are appended in file order (no duplicate check); lookups take the first record with the id.

### 7.1 `[mLootProps]`

Describes how many units of a good drop from a destroyed NPC. A good with no record uses the records whose nicknames are exactly `default_commodity` or `default_equipment` (no null check: those two records must exist).

| Key | Type | Default | Meaning |
|---|---|---|---|
| `nickname` | id | none | Good nickname (matched against the good of the dropped cargo). |
| `drop_properties` | float, int, int, int, int, int | 0 for all | Six positional values. Value 0 is a drop chance in percent (stored divided by 100). Values 1-5 are integers consumed by the loot generator ([SpaceSupport.cpp:413](../../src/Content/SpaceSupport.cpp#L413)): value 1 a minimum worth (sum of the players' worth) below which nothing drops, value 2 the divisor applied to the excess worth (0 keeps the item count), value 3 a base amount added to the scaled amount, value 4 a cap on that sum (which is also limited by the carried item count), value 5 a cap on the final count after the chance has been applied. These meanings are inferred from the consumer's arithmetic; the port has no names for them. |

### 7.2 `[PhantomLoot]`

Extra loot added to ships whose reputation rank falls in a range (see the same function in `SpaceSupport.cpp`).

| Key | Type | Default | Meaning |
|---|---|---|---|
| `nickname` | id | none | Id of the dropped item (used as the archetype id). |
| `toughness_range` | int, int | 0, 0 | Inclusive range of the ship's reputation rank that qualifies; a maximum of `-1` means no upper limit. |
| `percent_chance` | float | 0 | Chance in percent (stored divided by 100). |
| `num_to_drop` | int, int | 0, 0 | Minimum and maximum count (`min + rand() * (max - min + 1) / 32768`). |
