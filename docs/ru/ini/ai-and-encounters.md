# Личности ИИ, встречи, фракции, NPC-корабли и лут

На этой странице описаны INI-таблицы, которые модуль Content (`Content.dll`, исходники в `src/Content/`) читает для управления поведением NPC: личности пилотов, формации, файлы параметров встреч, случайные встречи, свойства фракций, архетипы NPC-кораблей и таблицы лута. Все файлы используют общую грамматику `INI_Reader` и существуют в бинарном (BINI) или текстовом INI-виде; ридер между ними не различает (см. [parsers.md](parsers.md)).

**Расположение файлов / как они находятся.** Каждый файл на этой странице открывается через `OpenContentDataIni(relativePath, reader)` ([ContentDataPath.cpp](../../../src/Content/ContentDataPath.cpp#L17)): относительное имя (разделители — обратный слеш) присоединяется к каталогу Data, то есть к записи `data path` секции `[Freelancer]` файла `freelancer.ini` (см. [freelancer-ini.md](freelancer-ini.md)); ни один ключ `[Data]` эти файлы не перечисляет, имена — литералы в коде. Таблицы с фиксированными именами загружаются один раз, при конструировании контроллера `GameSupport`, функцией `LoadContentGameTables` ([Encounters.cpp](../../../src/Content/Encounters.cpp#L495)). Файл, который не удалось открыть, не отмечается: загрузчик работает на неоткрытом ридере и не находит секций. Исключение — файлы параметров встреч: их имена приходят из файлов систем (см. [systems-and-bases.md](systems-and-bases.md)).

## Соглашения, общие для всех таблиц ниже

* Имена секций, имена ключей и перечислимые слова сравниваются без учёта регистра, если в строке не сказано «точно» (такие сравниваются через `strcmp`).
* Неизвестные ключи и неизвестные секции пропускаются молча. Проверки диапазонов нет, если в строке не сказано обратное; там, где код значение не проверяет, на этой странице написано «не проверяется».
* **Повторяющиеся секции** доставляются все, в порядке файла; побеждает ли более поздняя, указано для каждого файла отдельно («побеждает первое определение» = более поздняя секция с тем же nickname отбрасывается).
* Типы: `string`, `int` (`atoi`), `float`, `bool` (правило INI_Reader: текст `true`/`1` или `false`/`0`, всё остальное пишет ошибку в лог и читается как false), `id` (nickname, хешируемый через `CreateID` и затем сопоставляемый с другими таблицами по этому хешу, поэтому опечатка в имени никогда не отмечается). «Значение N» означает N-й параметр, разделённый запятыми, считая с 0.
* Отсутствующий параметр читается как `""` / `0`, поэтому «По умолчанию» ниже — это значение, которое хранит приёмник, когда ключа нет.

| Файл (относительно пути Data) | Секции | Загрузчик | Смежная страница |
|---|---|---|---|
| `missions\pilots_population.ini`, `missions\pilots_story.ini` | 16 секций-блоков + `[Pilot]` | [PilotPersonalities.cpp](../../../src/Content/PilotPersonalities.cpp#L995) | эта страница |
| `missions\formations.ini` | `[Formation]` | [Encounters.cpp](../../../src/Content/Encounters.cpp#L138) | эта страница |
| файлы параметров встреч, названные в файлах систем | `[EncounterFormation]`, `[Creation]` | [EncounterParams.cpp](../../../src/Content/EncounterParams.cpp#L368) | эта страница |
| `[MsnRandEnc]` файлов миссий, `[RandomEncounter]` данных сохранения | ключи популятора | [RandomEncounters.cpp](../../../src/Content/RandomEncounters.cpp#L1140) | эта страница, [missions.md](missions.md) |
| `RandomMissions\KillableSolars.ini`, `RandomMissions\VignetteCriticalLoot.ini` | `[KillableSolar]`, `[critical_loot]` | [RandomEncounters.cpp](../../../src/Content/RandomEncounters.cpp#L58) | эта страница, [random-missions.md](random-missions.md) |
| `missions\faction_prop.ini` | `[FactionProps]` | [FactionProps.cpp](../../../src/Content/FactionProps.cpp#L727) | эта страница |
| `equipment\commodities_per_faction.ini` | `[FactionGood]` | [FactionProps.cpp](../../../src/Content/FactionProps.cpp#L887) | эта страница |
| `missions\empathy.ini` | `[RepChangeEffects]` | [FactionProps.cpp](../../../src/Content/FactionProps.cpp#L922) | эта страница |
| `missions\npcships.ini`, `missions\npcships_test.ini`, `npc_ship_file` отдельных миссий | `[NPCShipArch]` | [NpcShips.cpp](../../../src/Content/NpcShips.cpp#L42) | эта страница |
| `missions\specific_npc.ini` | `[NPC]` | [NpcShips.cpp](../../../src/Content/NpcShips.cpp#L160) | эта страница |
| `missions\lootprops.ini` | `[mLootProps]`, `[PhantomLoot]` | [LootProps.cpp](../../../src/Content/LootProps.cpp#L52) | эта страница |

---

## 1. Личности пилотов: `missions\pilots_population.ini`, `missions\pilots_story.ini`

Оба файла читает одна функция, `LoadPilotPersonalities` ([PilotPersonalities.cpp:995](../../../src/Content/PilotPersonalities.cpp#L995)): сначала файл population, затем файл story. Файл — плоский список секций:

* 16 **секций-блоков**, каждая описывает один модуль поведения (`[EvadeDodgeBlock]`, `[EvadeBreakBlock]`, `[BuzzHeadTowardBlock]`, `[BuzzPassByBlock]`, `[TrailBlock]`, `[StrafeBlock]`, `[EngineKillBlock]`, `[RepairBlock]`, `[GunBlock]`, `[MineBlock]`, `[MissileBlock]`, `[DamageReactionBlock]`, `[MissileReactionBlock]`, `[CountermeasureBlock]`, `[FormationBlock]`, `[JobBlock]`);
* секции `[Pilot]`, которые называют по одному блоку каждого вида.

Каждый блок начинается со значений по умолчанию соответствующей подструктуры `pub::AI::Personality` (конструкторы в [common_pub.cpp](../../../src/Common/common_pub.cpp#L42); столбцы «По умолчанию» ниже — это значения из этих конструкторов), затем применяет найденные ключи. Блок сохраняется под `CreateID(nickname)`; блок без `nickname` или с уже сохранённым nickname отбрасывается (побеждает первое определение, отдельно для каждого вида блока). Поскольку файлы загружаются в фиксированном порядке, nickname, определённый в `pilots_population.ini`, нельзя переопределить в `pilots_story.ini`.

Соглашения в таблицах блоков: **deg** = значение в файле задаётся в градусах и при загрузке умножается на pi/180 (значение по умолчанию тоже дано в градусах); «weight key» = `key = NAME, weight`, где `NAME` — одно из перечисленных слов (без учёта регистра, неперечисленное слово игнорируется), а `weight` — значение 1 как float; `trueFalse` = текстовое правило `TRUE` -> true, `FALSE` -> false, **любой другой текст -> true** (без учёта регистра, значение 0). Каждый блок также принимает `nickname` (string, хешированный id блока).

### 1.1 `[EvadeDodgeBlock]` ([PilotPersonalities.cpp:126](../../../src/Content/PilotPersonalities.cpp#L126), значения по умолчанию [common_pub.cpp:42](../../../src/Common/common_pub.cpp#L42))

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `evade_activate_range` | float | 500 | Дальность, с которой начинается уклонение. |
| `evade_dodge_cone_angle` | float deg | 45 | Угол конуса уклонения. |
| `evade_dodge_cone_angle_variance_percent` | float | 0 | Случайный разброс угла конуса. |
| `evade_dodge_waggle_axis_cone_angle` | float deg | 0 | Конус оси виляния. |
| `evade_dodge_roll_angle` | float deg | 0 | Угол крена. |
| `evade_dodge_interval_time` | float | 1.0 | Время между уклонениями. |
| `evade_dodge_interval_time_variance_percent` | float | 0 | Разброс интервала. |
| `evade_dodge_distance` | float | 100 | Дистанция уклонения. |
| `evade_dodge_time` | float | 10 | Длительность уклонения. |
| `evade_dodge_slide_throttle` | float | 0.6 | Тяга при скольжении. |
| `evade_dodge_turn_throttle` | float | 0.6 | Тяга при развороте. |
| `evade_dodge_corkscrew_roll_throttle` | float | 1.0 | Тяга крена при штопоре. |
| `evade_dodge_corkscrew_roll_flip_direction` | trueFalse | false | Инвертировать направление крена при штопоре. |
| `evade_dodge_corkscrew_turn_throttle` | float | 0.15 | Тяга разворота при штопоре. |
| `evade_dodge_direction_weight` | weight key | все 0 | Имена `RIGHT`, `LEFT`, `UP`, `DOWN`. |
| `evade_dodge_style_weight` | weight key | все 0 | Имена `WAGGLE`, `WAGGLE_RANDOM`, `SLIDE`, `CORKSCREW`. |

### 1.2 `[EvadeBreakBlock]` ([:193](../../../src/Content/PilotPersonalities.cpp#L193), значения по умолчанию [common_pub.cpp:74](../../../src/Common/common_pub.cpp#L74))

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `evade_break_time` | float | 3.0 | Длительность отрыва. |
| `evade_break_interval_time` | float | 100000 | Время между отрывами. |
| `evade_break_afterburner_delay` | float | 0 | Задержка перед использованием форсажа. |
| `evade_break_afterburner_delay_variance_percent` | float | 0 | Разброс этой задержки. |
| `evade_break_direction_weight` | weight key | все 0 | `RIGHT`, `LEFT`, `UP`, `DOWN`. |
| `evade_break_roll_throttle` | float | 0 | Тяга крена. |
| `evade_break_turn_throttle` | float | 1.0 | Тяга разворота. |
| `evade_break_style_weight` | weight key | все 0 | `SIDEWAYS`, `OUTRUN`, `REVERSE`. |
| `evade_break_attempt_reverse_time` | float | 4.0 | Время, затрачиваемое на попытку дать задний ход. |
| `evade_break_reverse_distance` | float | 150 | Дистанция заднего хода. |

### 1.3 `[BuzzHeadTowardBlock]` ([:246](../../../src/Content/PilotPersonalities.cpp#L246), значения по умолчанию [common_pub.cpp:99](../../../src/Common/common_pub.cpp#L99))

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `buzz_head_toward_style_weight` | weight key | все 0 | `STRAIGHT_TO`, `SLIDE`, `WAGGLE`. |
| `buzz_min_distance_to_head_toward` | float | 200 | Минимальная дистанция заходного прохода. |
| `buzz_min_distance_to_head_toward_variance_percent` | float | 1.0 | Разброс. |
| `buzz_max_time_to_head_away` | float | 4.0 | Максимальное время до отворота. |
| `buzz_head_toward_engine_throttle` | float | 1.0 | Тяга двигателя. |
| `buzz_head_toward_turn_throttle` | float | 1.0 | Тяга разворота. |
| `buzz_head_toward_roll_throttle` | float | 0 | Тяга крена. |
| `buzz_head_toward_roll_flip_direction` | trueFalse | false | Инвертировать направление крена. |
| `buzz_slide_throttle` | float | 1.0 | Тяга скольжения. |
| `buzz_slide_interval_time` | float | 2.0 | Интервал скольжения. |
| `buzz_slide_interval_time_variance_percent` | float | 0.4 | Разброс. |
| `buzz_dodge_direction_weight` | weight key | все 0 | `RIGHT`, `LEFT`, `UP`, `DOWN`. |
| `buzz_dodge_turn_throttle` | float | 1.0 | Тяга разворота при уклонении. |
| `buzz_dodge_cone_angle` | float deg | 45 | Конус уклонения. |
| `buzz_dodge_cone_angle_variance_percent` | float | 0 | Разброс. |
| `buzz_dodge_waggle_axis_cone_angle` | float deg | 0 | Конус оси виляния. |
| `buzz_dodge_roll_angle` | float deg | 0 | Угол крена. |
| `buzz_dodge_interval_time` | float | 2.0 | Интервал. |
| `buzz_dodge_interval_time_variance_percent` | float | 0 | Разброс. |

### 1.4 `[BuzzPassByBlock]` ([:318](../../../src/Content/PilotPersonalities.cpp#L318), значения по умолчанию [common_pub.cpp:133](../../../src/Common/common_pub.cpp#L133))

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `buzz_pass_by_style_weight` | weight key | все 0 | `BREAK_AWAY`, `STRAIGHT_BY`, `ENGINE_KILL`. |
| `buzz_distance_to_pass_by` | float | 100 | Дистанция пролёта. |
| `buzz_pass_by_time` | float | 3.0 | Длительность пролёта. |
| `buzz_break_direction_weight` | weight key | все 0 | `RIGHT`, `LEFT`, `UP`, `DOWN`. |
| `buzz_break_direction_cone_angle` | float | 0 | Сохраняется как записано, **без перевода из градусов**. |
| `buzz_break_turn_throttle` | float | 1.0 | Тяга разворота. |
| `buzz_pass_by_roll_throttle` | float | 0 | Тяга крена. |
| `buzz_drop_bomb_on_pass_by` | trueFalse | false | Сбросить бомбу при пролёте. |

### 1.5 `[TrailBlock]`, `[StrafeBlock]`, `[EngineKillBlock]`, `[RepairBlock]`

| Секция | Ключ | Тип | По умолчанию | Источник |
|---|---|---|---|---|
| `[TrailBlock]` | `trail_lock_cone_angle` | float deg | 30 | [:367](../../../src/Content/PilotPersonalities.cpp#L367), [значения по умолчанию](../../../src/Common/common_pub.cpp#L156) |
| | `trail_break_time` | float | 2.5 | |
| | `trail_min_no_lock_time` | float | 4.0 | |
| | `trail_break_roll_throttle` | float | 0 | |
| | `trail_break_afterburner` | trueFalse | false | |
| | `trail_max_turn_throttle` | float | 1.0 | |
| | `trail_distance` | float | 150 | |
| `[StrafeBlock]` | `strafe_run_away_distance` | float | 150 | [:393](../../../src/Content/PilotPersonalities.cpp#L393), [значения по умолчанию](../../../src/Common/common_pub.cpp#L173) |
| | `strafe_attack_throttle` | float | 1.0 | |
| | `strafe_turn_throttle` | float | 1.0 | |
| `[EngineKillBlock]` | `engine_kill_search_time` | float | 6.0 | [:411](../../../src/Content/PilotPersonalities.cpp#L411), [значения по умолчанию](../../../src/Common/common_pub.cpp#L186) |
| | `engine_kill_face_time` | float | 3.0 | |
| | `engine_kill_use_afterburner` | trueFalse | false | Сохраняется как float 1.0 / 0.0. |
| | `engine_kill_afterburner_time` | float | 0.5 | |
| | `engine_kill_max_target_distance` | float | 700 | |
| `[RepairBlock]` | `use_shield_repair_pre_delay` | float | 2.0 | [:436](../../../src/Content/PilotPersonalities.cpp#L436), [значения по умолчанию](../../../src/Common/common_pub.cpp#L201) |
| | `use_shield_repair_post_delay` | float | 3.0 | |
| | `use_shield_repair_at_damage_percent` | float | 0.5 | |
| | `use_hull_repair_pre_delay` | float | 1.0 | |
| | `use_hull_repair_post_delay` | float | 1.0 | |
| | `use_hull_repair_at_damage_percent` | float | 0.5 | |

### 1.6 `[GunBlock]`, `[MineBlock]`, `[MissileBlock]`

| Секция | Ключ | Тип | По умолчанию | Значение / допустимые значения |
|---|---|---|---|---|
| `[GunBlock]` ([:460](../../../src/Content/PilotPersonalities.cpp#L460), [значения по умолчанию](../../../src/Common/common_pub.cpp#L223)) | `gun_fire_interval_time` | float | 0 | |
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
| | `fire_style` | string | `MULTIPLE` | `MULTIPLE` сохраняет 0; **любой другой текст сохраняет 1** (документированное слово — `SINGLE`). |
| `[MineBlock]` ([:515](../../../src/Content/PilotPersonalities.cpp#L515), [значения по умолчанию](../../../src/Common/common_pub.cpp#L249)) | `mine_launch_interval` | float | 0 | |
| | `mine_launch_cone_angle` | float deg | 22.5 | |
| | `mine_launch_range` | float | 400 | |
| `[MissileBlock]` ([:533](../../../src/Content/PilotPersonalities.cpp#L533), [значения по умолчанию](../../../src/Common/common_pub.cpp#L262)) | `missile_launch_range` | float | -1 | |
| | `missile_launch_allow_out_of_range` | trueFalse | false | |
| | `missile_launch_interval_time` | float | 5.0 | |
| | `missile_launch_interval_variance_percent` | float | 0.4 | |
| | `missile_launch_cone_angle` | float deg | 22.5 | |
| | `anti_cruise_missile_min_distance` | float | 150 | |
| | `anti_cruise_missile_max_distance` | float | 2500 | |
| | `anti_cruise_missile_pre_fire_delay` | float | 2.0 | |
| | `anti_cruise_missile_interval_time` | float | 5.0 | |

### 1.7 Блоки реакций и контрмер

| Секция | Ключ | Тип | По умолчанию | Источник |
|---|---|---|---|---|
| `[DamageReactionBlock]` | `evade_break_damage_trigger_percent` | float | 1.0 | [:563](../../../src/Content/PilotPersonalities.cpp#L563), [значения по умолчанию](../../../src/Common/common_pub.cpp#L281) |
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
| `[MissileReactionBlock]` | `evade_missile_distance` | float | 250 | [:606](../../../src/Content/PilotPersonalities.cpp#L606), [значения по умолчанию](../../../src/Common/common_pub.cpp#L308) |
| | `evade_break_missile_reaction_time` | float | 0 | |
| | `evade_slide_missile_reaction_time` | float | 0 | |
| | `evade_afterburn_missile_reaction_time` | float | 0 | |
| `[CountermeasureBlock]` | `countermeasure_active_time` | float | 1.0 | читается inline в [:1121](../../../src/Content/PilotPersonalities.cpp#L1121), [значения по умолчанию](../../../src/Common/common_pub.cpp#L322) |
| | `countermeasure_unactive_time` | float | 0 | |

### 1.8 `[FormationBlock]` ([:627](../../../src/Content/PilotPersonalities.cpp#L627), значения по умолчанию [common_pub.cpp:334](../../../src/Common/common_pub.cpp#L334))

| Ключ | Тип | По умолчанию | Значение |
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
| `formation_exit_mode` | string | `NOTHING` | `BREAK_AWAY_FROM_CENTER` (0), `BREAK_AWAY_FROM_CENTER_AFTERBURNER` (1), `BRAKE_REVERSE` (2), `OUTRUN` (3), `NOTHING` (5). Любой другой текст оставляет значение по умолчанию. |
| `formation_exit_top_turn_break_away_throttle` | float | 1.0 | |
| `formation_exit_roll_outrun_throttle` | float | 0 | |
| `formation_exit_max_time` | float | 0 | |
| `leader_makes_me_tougher` | trueFalse | false | |

### 1.9 `[JobBlock]` ([:772](../../../src/Content/PilotPersonalities.cpp#L772), значения по умолчанию [common_pub.cpp:372](../../../src/Common/common_pub.cpp#L372))

«Слово сложности» (toughness word) — это `EASIEST` (0), `EASY` (1), `EQUAL` (2), `HARD` (3) или `HARDEST` (4); любой другой текст читается как `EQUAL` (2). Слова классов кораблей: `FIGHTER`, `FREIGHTER`, `TRANSPORT`, `GUNBOAT`, `CRUISER`, `CAPITAL`, `TRADELANE`, `JUMPGATE`, `WEAPONS_PLATFORM`, `DESTROYABLE_DEPOT`, `SOLAR`, `ANYTHING` (таблица в исходниках: [PilotPersonalities.cpp:83](../../../src/Content/PilotPersonalities.cpp#L83)).

| Ключ | Тип | По умолчанию | Значение / допустимые значения |
|---|---|---|---|
| `loot_preference` | string | нет (0) | Проверки подстроки по всему тексту: содержит `LT_ALL` -> 7; иначе содержит `LT_NONE` -> 0; иначе OR из `LT_COMMODITIES` (1), `LT_EQUIPMENT` (2), `LT_POTIONS` (4). |
| `loot_flee_threshold` | слово сложности | EQUAL (2) | |
| `flee_scene_threat_style` | слово сложности | HARD (3) | |
| `flee_when_hull_damaged_percent` | float | 0 | |
| `flee_when_leader_flees_style` | trueFalse | true | |
| `flee_no_weapons_style` | trueFalse | true | |
| `scene_toughness_threshold` | слово сложности | EQUAL (2) | |
| `target_toughness_preference` | слово сложности | EQUAL (2) | |
| `wait_for_leader_target` | trueFalse | false | |
| `maximum_leader_target_distance` | float | 5000 | |
| `combat_drift_distance` | - | - | Распознаётся, значение отбрасывается. |
| `attack_order` | список слов классов кораблей | одна запись `ANYTHING`, дальность 5000, все типы оружия | Разделители: пробел, запятая, табуляция. Заменяет список по умолчанию; каждое известное слово добавляет запись с дальностью 5000 и всеми типами оружия. Слово `ANYTHING` добавляет **две** записи (поведение retail). Неизвестные слова пропускаются. |
| `attack_preference` | повторяемый: слово класса, дальность, оружие | см. `attack_order` | Значение 0 — слово класса корабля, значение 1 — дальность float, значение 2 — текст, содержащий любое из `GUNS` (1), `GUIDED` (2), `UNGUIDED` (4), `TORPEDO` (8); проверяется на тексте в верхнем регистре. `UNGUIDED` содержит и `GUIDED`, поэтому выставляет оба бита 2 и 4. N-е вхождение заполняет запись n; не более 12 записей. Неизвестное слово класса оставляет эту запись как была; список всегда завершается после только что обработанной записи. |
| `attack_subtarget_order` | список слов | `ANYTHING` | Разделители: пробел, запятая, табуляция. Слова: `ANYTHING` (6), `GUNS` (0), `TURRETS` (1), `LAUNCHERS` (2), `TOWERS` (3), `ENGINES` (4), `HULL` (5); неизвестные слова пропускаются. Длина списка не проверяется. |
| `field_targeting` | string | `ALWAYS` (3) | `NEVER` (0), `LOW_DENSITY` (1), `HIGH_DENSITY` (2), `ALWAYS` (3); любой другой текст -> 1. |
| `force_attack_formation` | trueFalse | false | Также помечает значение как «заданное»: job, который его задаёт, переопределяет более раннее значение пилота, job, который не задаёт, оставляет его. |
| `allow_player_targeting` | trueFalse | true | |

### 1.10 `[Pilot]` ([:877](../../../src/Content/PilotPersonalities.cpp#L877))

Пилот — это запись ссылок (nickname блоков), сохраняемая под `CreateID(nickname)`; пилот без `nickname` отбрасывается, а при повторе nickname сохраняется первое определение.

| Ключ | Тип | Значение |
|---|---|---|
| `nickname` | id | Id пилота; ключ, по которому на него ссылаются другие таблицы (`pilot` в архетипах NPC-кораблей, файлах миссий и случайных встреч, solar-объектах NPC). |
| `inherit` | id | Другой **уже загруженный** пилот. В момент чтения ключа каждая непустая ссылка на блок у этого родителя перезаписывает текущие; собственный `inherit` родителя не отслеживается. Ключи, стоящие после `inherit`, переопределяют родителя, ключи перед ним — перезаписываются. Неизвестный родитель ничего не меняет. |
| `evade_dodge_id`, `evade_break_id`, `buzz_head_toward_id`, `buzz_pass_by_id`, `trail_id`, `strafe_id`, `engine_kill_id`, `repair_id`, `gun_id`, `mine_id`, `missile_id`, `damage_reaction_id`, `missile_reaction_id`, `countermeasure_id`, `formation_id`, `job_id` | id | Nickname блока этого вида. Ссылка на несуществующий блок игнорируется при построении личности (остаётся блок по умолчанию). |

Построение личности (`GetPilotPersonality`, [:1217](../../../src/Content/PilotPersonalities.cpp#L1217)): каждый упомянутый блок заменяет соответствующую часть `Personality` по умолчанию; затем, если у пилота есть `job_id`, сначала применяется job унаследованного пилота, а потом собственный job пилота. Id пилота попадает в личность также через ключи `pilot` других файлов.

---

## 2. Формации: `missions\formations.ini`

Читается функцией `LoadFormation` ([Encounters.cpp:138](../../../src/Content/Encounters.cpp#L138)) для каждой секции `[Formation]`. Формации индексируются по `CreateID(nickname)`; **более поздняя секция с тем же nickname отбрасывается** (побеждает первое определение). Секция без `nickname` сохраняется под id 0.

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `nickname` | id | нет | Id формации, на него ссылается `formation` в файлах встреч и секциях случайных встреч. |
| `pos` | 3 float (x, y, z) | нет | Один слот корабля. Ключ повторяется по разу на слот; слоты сохраняют порядок и индекс файла. |
| `pl_pos` | 3 float (x, y, z) | нет | Слот, зарезервированный для игрока. Делит список слотов с `pos` (порядок двух ключей определяет индексы слотов). |

Пример (синтетический):

```
[Formation]
nickname = example_wedge
pos = 0, 0, 0
pos = -60, 0, -40
pos = 60, 0, -40
```

---

## 3. Файлы параметров встреч

Файл параметров встреч определяет, что может породить зона. Имя файла задаёт секция `[EncounterParameters]` файла системы (вторая строка значения, см. [systems-and-bases.md](systems-and-bases.md#31-encounterparameters)), путь отсчитывается от пути Data, а файл разбирается конструктором `FEncounterParameters` ([EncounterParams.cpp:368](../../../src/Content/EncounterParams.cpp#L368)) по разу на каждую запись `[EncounterParameters]`. Файл обязан содержать хотя бы одну секцию `[EncounterFormation]`; без неё код индексирует пустой массив (неопределённое поведение, как в retail). Затем зона выбирает встречу через `encounter = id, level, chance` (см. [systems-and-bases.md](systems-and-bases.md#34-секции-зон-zonescpp-и-encounterscpp)).

### 3.1 `[EncounterFormation]`

Парсер: [EncounterParams.cpp:115](../../../src/Content/EncounterParams.cpp#L115), значения по умолчанию задаются в конструкторе в [:93](../../../src/Content/EncounterParams.cpp#L93). По одной секции на каждую формацию, которую может создать встреча. Строки кораблей собираются в порядке файла; `make_class` и `pilot_job` всегда изменяют **самую последнюю** строку корабля.

| Ключ | Тип | По умолчанию | Значение / допустимые значения |
|---|---|---|---|
| `ship_by_class` | int min, int max, id class, [int offset] | - | Добавляет строку корабля, заполняемую из класса NPC-кораблей. Значение 0 — минимальное количество, значение 1 — максимальное (оба читаются безусловно, отсутствующее равно 0), значение 2 — id класса: либо nickname секции `[ShipClass]` в `missions\shipclasses.ini` (архетип NPC-корабля подходит, если один из id `member` этого класса есть в его списке `npc_class`), либо непосредственно слово `npc_class` архетипа (или свойство миссии его корабля в `shiparch.ini`); см. [random-missions.md](random-missions.md), значение 3 — необязательное смещение сложности (0, если отсутствует). Когда встреча привязана к зоне с уровнем L, в строку добавляется класс `diff<N>`, где `N = max(1, L + offset)`; архетип подходит строке, только если удовлетворяет **каждому** классу строки, поэтому от архетипов ожидается наличие подходящего слова `diff<N>` в `npc_class`. |
| `ship_by_npc_arch` | int min, int max, id arch | - | Добавляет строку корабля, которая напрямую называет один nickname `[NPCShipArch]` (раздел 6). |
| `make_class` | список id | - | Все значения добавляются (дубликаты игнорируются) в список дополнительных классов последней строки корабля (принимаются как дополнительные классы строки). Игнорируется, если строке корабля ничто не предшествует. |
| `pilot` | - | - | Распознаётся и игнорируется. |
| `pilot_job` | id | пусто | Job (nickname `[JobBlock]`) для последней строки корабля; игнорируется, если строке корабля ничто не предшествует. |
| `formation` | id | пусто | Формация из `formations.ini`. Принимается, только пока не задан ни `formation`, ни `formation_by_class` (побеждает первый). |
| `formation_by_class` | id | пусто | То же правило, взаимоисключающе с `formation`. |
| `behavior` | string | 0 | Точные слова (с учётом регистра): `nothing` (1), `trade` (2), `patrol_path` (3), `wander` (4), `goto_guide` (5), `dock_guide` (6), `patrol_guide` (7). Любой другой непустой текст -> 1; отсутствует -> 0. Парсер: [Guides.cpp:83](../../../src/Content/Guides.cpp#L83). |
| `arrival` | список слов | разрешены все места | Сбрасывает маску прибытия в «ничего», затем каждое слово выставляет место: `all`, `tradelane`, `object_all`, `object_docking_ring`, `object_capital`, `object_jump_gate`, `object_station`, `cruise`, `buzz`. Ведущий `-` заставляет слово не устанавливать, а сбрасывать (поэтому имеет значение только после `all` или `object_all`). Неизвестные слова игнорируются; слова обрезаются до 49 символов. |
| `longevity` | int | 24 | |
| `allow_simultaneous_creation` | string | `true` | Точные слова в нижнем регистре: `yes`/`true` -> true, `no`/`false` -> false, другой текст оставляет значение по умолчанию. |
| `zone_creation_distance` | int | 1500 | Сохраняется как double. |
| `times_to_create` | `infinite` или int | infinite (-1) | Точное `infinite`, иначе `atoi`. |
| `feeling_to_formation` | string, int | маска 7, значение -1 | Значение 0: `friendly` (маска 1), `hostile` (4), `same` (8), `any` (7); другой текст оставляет маску. Значение 1: целое значение отношения. Вычисляется только первое вхождение; после него остальные ключи `feeling_*` игнорируются. |
| `feeling_to_local`, `feeling_to_group`, `explicit_group` | - | - | Распознаются (пока не прочитан `feeling_to_formation`), но лишь сбрасывают значение отношения в -1; их значения не используются. |

`group_reroll_frequency` (`synch`, `every`, `once`) есть в парсере, но единственное место вызова никогда не передаёт этот ключ, поэтому ветка недостижима, а режим повторного броска остаётся `every`.

### 3.2 `[Creation]`

Парсер: [EncounterParams.cpp:411](../../../src/Content/EncounterParams.cpp#L411).

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `permutation` | string, int weight, [int reroll interval] | см. ниже | Повторяемый. Значение 0 — список индексов формаций (0-based позиция секции `[EncounterFormation]` в файле), разделённых пробелами или `+`; `*` после индекса помечает формацию как «звёздочную» (создаётся при каждом обновлении зоны). Пример: `0 + 1* + 2`. Значение 1 — вес выбора, читается как **int**. Значение 2 (необязательный int) задаёт интервал повторного броска встречи; побеждает последнее заданное. Любой символ, кроме пробела, `+`, цифры или `*`, начинает новое число (не проверяется). |

Если в файле нет ключа `permutation`, создаётся одна перестановка, содержащая каждую формацию (индексы 0..n-1, без звёздочек) с весом 1.

---

## 4. Случайные встречи

Случайная встреча — это скриптовая засада с «популятором» (когда она срабатывает) и объектом параметров (что она создаёт). Объявляется в **файле миссии** как секция `[MsnRandEnc]` ([missions.md](missions.md)) и в **данных сохранения** как секция `[RandomEncounter]`. В обоих случаях `nickname` и `encounter_type` читает код миссии, при этом `encounter_type` выбирает популятор: `TL_Attack` (без учёта регистра, нападение на трейдлейн) или `SP_Attack` (нападение в открытом космосе); любое другое значение сохраняет прежние объекты. Затем популятор заново читает **ту же секцию** (состояние ридера сохраняется и восстанавливается, поэтому собственный проход загрузчика миссии не затрагивается) по приведённым ниже ключам ([TL: RandomEncounters.cpp:1140](../../../src/Content/RandomEncounters.cpp#L1140), [SP: :1469](../../../src/Content/RandomEncounters.cpp#L1469)). Те же объекты записывают эти ключи обратно при сохранении игры.

### 4.1 Ключи, общие для `TL_Attack` и `SP_Attack`

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `target_ship_name` | string | пусто | Имя миссионного объекта корабля, на который нацелена встреча (не более 47 символов). Пусто означает корабль игрока. |
| `attacker_rep_name` | string | -1 | Nickname группы репутации нападающих. |
| `attacker_rep` | int | -1 | То же в виде сырого id группы (сохранённые игры). Побеждает последний из двух ключей. |
| `pilot` | id | нет | Nickname личности пилота для нападающих (раздел 1). |
| `pilot_id` | int | нет | То же в виде сырого id. |
| `label` | id | нет | Повторяемый; каждое значение хешируется и добавляется в список меток созданных кораблей. |
| `formation` | id nickname, float, int, int, затем пары (id класса, float) | - | Повторяемый. Значение 0 — одновременно nickname записи и формация из `formations.ini`. Значение 1 — float, вес формации. Значения 2 и 3 — минимальное и максимальное число кораблей. Начиная со значения 4 идут пары `id класса корабля, вес`; неизвестный или пустой класс пропускается. В `TL_Attack` пара без веса также пропускается. |
| `orientation` | float deg, [float] | 90 deg, 2500 | Значение 0 — угол появления в градусах, необязательное значение 1 — дистанция появления. |
| `radius` | float | 7 | Радиус свободного места для точки появления (усекается до int); 0 или меньше пропускает проверку. |
| `num_forms` | int min, int max | 0, 0 | Число создаваемых формаций; число для этой активации выбирается сразу через `rand()` между min и max включительно. |

### 4.2 Только `TL_Attack` (засада на трейдлейне; [RandomEncounters.cpp:1140](../../../src/Content/RandomEncounters.cpp#L1140))

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `target_tradelane_name` | string | 0 | Nickname объекта-кольца, которое атакуют. |
| `target_tradelane` | int | 0 | То же в виде сырого id объекта. |
| `tl_attack_chance` | int | 20 | Шанс в процентах; задаёт и рабочее значение, и значение, восстанавливаемое при `reset`. |
| `tl_attack_chance_read_in` | int | 20 | Задаёт только значение для восстановления. |
| `saved_formation` | int id, float, int, int, пары | - | Как `formation`, но значение 0 — сырой числовой id (используется сохранёнными играми). |

### 4.3 Только `SP_Attack` (засада в открытом космосе; [RandomEncounters.cpp:1469](../../../src/Content/RandomEncounters.cpp#L1469))

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `activation_type` | string, затем float | `time`, от 180 до 300 с | Учитывается только первая принятая строка. `location, x, y, z, range`: срабатывает, когда цель находится в пределах этой дальности (дальность хранится в квадрате, как int; по умолчанию 2500). `time, minSeconds, maxSeconds`: значение 1 — нижняя, значение 2 — верхняя граница; время срабатывания выбирается через `rand()` между двумя усечёнными значениями. Другие слова игнорируются. Без ключа первая проверка активации ведёт себя как `time` с границами 180 и 300 секунд. |
| `formation_position` | 3 float | нет | Повторяемая фиксированная позиция (вес 1.0) для формации. |
| `arrival_obj` | int или string | 0 | Если `atoi` от текста не равен нулю, число используется как id объекта, иначе текст — nickname. Цель стыковки для появившихся кораблей. |

### 4.4 `RandomMissions\KillableSolars.ini` - `[KillableSolar]` ([RandomEncounters.cpp:58](../../../src/Content/RandomEncounters.cpp#L58))

Загружается генератором случайных миссий ([random-missions.md](random-missions.md)); таблица разрушаемых станций/объектов для виньеток. Запись **отбрасывается**, если не проходит проверки из последнего абзаца. Записи сохраняются под `CreateID(nickname)` (побеждает первое определение).

| Ключ | Тип | По умолчанию | Значение / допустимые значения |
|---|---|---|---|
| `nickname` | id | нет | Обязателен. |
| `solar_type` | string | нет | Одно из `big_solar` (0), `defensive_solar` (1), `killable_neutral` (2), `background_solar` (3); неизвестные слова оставляют значение незаданным, и запись отбрасывается. |
| `string_id` | int | 0 | Id ресурса отображаемого имени. |
| `faction` | список nickname групп репутации или `all` | нет | `all` (без учёта регистра) подходит всем фракциям; иначе каждое значение — группа, дубликаты игнорируются. |
| `archetype` | string | нет | Nickname архетипа (хешируется через `GetNicknameId`); обязателен. |
| `loadout` | string | нет | Nickname лодаута; обязателен. |
| `hitpoints` | int, [int] | нет | Минимум и необязательный максимум (максимум по умолчанию равен минимуму). Если максимум превышает очки прочности архетипа, оба значения ограничиваются значением архетипа. |
| `difficulty` | float, float | 0, 0 | Минимальная и максимальная сложность. |
| `only_allowed_in_zone_type` | список слов | нет (без ограничений) | `rock`, `debris`, `ice`, `lava`, `crystal` (без учёта регистра); остальные слова игнорируются. |
| `pilot_choices` | int | 1 | |

Проверка (иначе запись отбрасывается): nickname присутствует; `solar_type` известен; `faction` задан (или `all`); архетип и лодаут ненулевые; минимум `hitpoints` > 0 и максимум >= минимума; `0 <= минимум difficulty <= максимум <= 100`.

### 4.5 `RandomMissions\VignetteCriticalLoot.ini` - `[critical_loot]` ([RandomEncounters.cpp:256](../../../src/Content/RandomEncounters.cpp#L256))

Каждая секция добавляется в порядке файла (проверки дубликатов нет).

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `faction` | список nickname групп или `all` | нет | Как в `[KillableSolar]`. |
| `archetype` | string | 0 | Nickname оборудования/товара (`GetEquipmentID`). |
| `name` | int | 0 | Id ресурса имени предмета. |
| `why_desired` | int, int | 0, 0 | Два id ресурсов. |
| `type` | список слов | пусто | `destroy` (1), `tractor` (2), `return` (3); остальные слова игнорируются, дубликаты игнорируются. |

---

## 5. Фракции

### 5.1 `missions\faction_prop.ini` - `[FactionProps]` ([FactionProps.cpp:727](../../../src/Content/FactionProps.cpp#L727))

Одна секция на фракцию. Секция сохраняется под группой самого последнего ключа `affiliation` (переменная между секциями не сбрасывается, поэтому секция без `affiliation` использует группу предыдущей секции, изначально 0); заполняются две таблицы (свойств и имён), а вторая секция для той же группы отбрасывается (побеждает первое определение). Значения по умолчанию берутся из конструктора в [FactionProps.cpp:217](../../../src/Content/FactionProps.cpp#L217).

| Ключ | Тип | По умолчанию | Значение / допустимые значения |
|---|---|---|---|
| `affiliation` | string | группа 0 | Nickname группы репутации фракции. Также выбирает краткое имя группы для таблиц имён. Не проверяется относительно групп. |
| `scan_announce` | bool | true | |
| `scan_chance` | float | 1.0 | |
| `scan_for_cargo` | string nickname товара, int level | - | Повторяемый. Значение 0 — товар (хешируется в id архетипа), значение 1 — `0`, `1` или `2` (всё остальное сохраняется как 0). Одинаковая пара не добавляется дважды. |
| `formation` | id, id | - | Повторяемая запись отображения `CreateID(значение 0) -> CreateID(значение 1)`; повторный ключ сохраняет своё первое значение. |
| `npc_ship` | id | - | Повторяемый; nickname `[NPCShipArch]` (раздел 6), на котором может летать фракция. |
| `msg_id_prefix` | string | пусто | Префикс сообщений связи (общий ридер `CommReferrable`). |
| `mc_costume` | string | 0 | Nickname костюма для персонажей фракции в баре (`GetCostumeID`). |
| `space_costume` | string head, string body, accessories... | - | Повторяемый. Значение 0 — часть тела «голова», значение 1 — часть тела «тело», значения 2.. — nickname аксессуаров. |
| `voice` | string | - | Повторяемый nickname голоса (`GetVoiceID`). |
| `jump_preference` | string | любой (0) | Точное `jumpgate` (1) или `jumphole` (2); любой другой текст -> любой. |
| `assassinate_target` | id | - | Повторяемый. |
| `Legality` | string | не задан | `lawful` или `unlawful`; задаёт id имён законности фракции. |
| `nickname_plurality` | string | `singular` | `singular` или `plural`; другой текст оставляет значение по умолчанию. |
| `firstname_male`, `firstname_female`, `lastname`, `formation_desig`, `large_ship_names` | int first, [int last] | пусто | Повторяемые диапазоны последовательных id ресурсов; отсутствующее второе значение повторяет первое. Каждый ключ ведёт свой список; количества складываются. |
| `firstname` | - | - | Распознаётся и игнорируется. |
| `large_ship_desig` | int | 0 | |
| `rank_desig` | 5 int | 0 | Значения 0-2 — id обозначений для трёх полос рангов, значения 3 и 4 — два порога ранга (ранг ниже порога 3 использует обозначение 0, ниже порога 4 — обозначение 1, иначе — обозначение 2). |

### 5.2 `equipment\commodities_per_faction.ini` - `[FactionGood]` ([FactionProps.cpp:887](../../../src/Content/FactionProps.cpp#L887))

| Ключ | Тип | Значение |
|---|---|---|
| `faction` | string | Nickname группы репутации; выбирает фракцию, чей список расширяется. Фракция без записи `[FactionProps]` приводит к сбою на последующих строках `MarketGood` (проверки на null нет, как в retail). |
| `MarketGood` | string | Повторяемый; nickname товара (`GetGoodID`), добавляемый в список фракции. `faction` должен предшествовать ему в той же секции. |

### 5.3 `missions\empathy.ini` - `[RepChangeEffects]` ([FactionProps.cpp:922](../../../src/Content/FactionProps.cpp#L922))

Файл открывается по строке пути, переданной из `LoadContentGameTables`; отсутствующий файл оставляет таблицу пустой. Секция сохраняется под своим `group` (вторая секция для той же группы отбрасывается); коэффициенты эмпатии сортируются по возрастанию коэффициента в конце секции.

| Ключ | Тип | Значение |
|---|---|---|
| `group` | string | Nickname группы репутации, к которой применяются эффекты. Секция без него сохраняется под группой -1. |
| `empathy_rate` | string group, float rate | Повторяемый. Когда репутация игрока с `group` изменяется на D, репутация с названной группой изменяется на `D * rate`. |
| `event` | string, float | Коэффициент для события. Значение 0 (точные слова, без учёта регистра): `object_destruction`, `random_mission_success`, `random_mission_failure`, `random_mission_abortion`; остальные слова игнорируются. Значение 1 — коэффициент. |

---

## 6. NPC-корабли и персонажи

### 6.1 `missions\npcships.ini`, `missions\npcships_test.ini` и файлы отдельных миссий - `[NPCShipArch]` ([NpcShips.cpp:42](../../../src/Content/NpcShips.cpp#L42))

`npcships_test.ini` читается сразу после `npcships.ini` в ту же таблицу. Файл миссии может назвать собственный файл через `npc_ship_file` ([missions.md](missions.md)); он разбирается той же функцией в собственную таблицу миссии. Записи сохраняются под `CreateID(nickname)`; побеждает первое определение nickname.

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `nickname` | id | нет | Id архетипа (на него ссылаются `ship_by_npc_arch`, `npc_ship`, `npc_ship_arch`). |
| `ship_archetype` | string | 0 | Nickname корабля (`GetShipID`, см. [archetypes-and-equipment.md](archetypes-and-equipment.md)). |
| `pilot` | id | пусто | Nickname личности пилота (раздел 1). |
| `loadout` | string | 0 | Nickname лодаута (`GetLoadoutID`). |
| `state_graph` | string | -1 | Имя графа состояний лидера в `AI\state_graph.db` (сравнивается без учёта регистра); неизвестные имена дают -1. |
| `npc_class` | список id | пусто | Все значения добавляются (дубликаты игнорируются). Строка `ship_by_class` (раздел 3.1) подходит этому архетипу, когда её класс или один из членов названного ею `[ShipClass]` есть в этом списке. |
| `level` | string | 0 | Ведущие нецифровые символы пропускаются, затем `atoi`; поэтому `d5` читается как 5. |

### 6.2 `missions\specific_npc.ini` - `[NPC]` ([NpcShips.cpp:160](../../../src/Content/NpcShips.cpp#L160))

Читается контроллером запуска `Story` при конструировании ([StoryLauncher.cpp:36](../../../src/Content/StoryLauncher.cpp#L36)). Сохраняется под `CreateID(nickname)`, побеждает первое определение; результат разбора не проверяется, поэтому запись, у которой нет части тела, голоса или индивидуального имени, всё равно вставляется (она просто «неполная»). Те же ключи (`ParseValue`) используются для записей `[GF_NPC]` баз ([systems-and-bases.md](systems-and-bases.md#5-missionsmbasesini)).

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `nickname` | id | нет | Id записи. |
| `base_appr` | string | нет | Nickname костюма (`GetCostumeID`); если он разрешается, вся внешность копируется из него. Более поздние строки `head`/`body`/... перезаписывают отдельные части. |
| `head` | string | 0 | Часть тела «голова». |
| `body` | string | 0 | Часть тела «тело». |
| `lefthand`, `righthand` | string | 0 | Части тела «руки». |
| `accessory` | string | - | Повторяемый nickname аксессуара; счётчик растёт с каждой строкой. |
| `voice` | string | 0 | Nickname голоса (`GetVoiceID`). |
| `individual_name` | int | 0 | Id ресурса имени персонажа. |
| `affiliation` | string | -1 | Nickname группы репутации. |

### 6.3 Блоки `[NPC]` файлов миссий ([NpcShips.cpp:356](../../../src/Content/NpcShips.cpp#L356))

Файлы миссий ([missions.md](missions.md)) объявляют актёров через `[NPC]`; `LoadMsnNpc` читает эти ключи. Результат функции «есть nickname» вызывающая сторона не проверяет: запись сохраняется под id своего nickname (побеждает первое определение), запись без nickname сохраняется под id 0.

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `nickname` | id | нет | Обязателен. |
| `space_costume` | string head, string body, accessories... | 0 | Как в `[FactionProps]`. |
| `voice` | string | 0 | Nickname голоса (обрезается до 79 символов). |
| `npc_ship_arch` | id | пусто | Nickname `[NPCShipArch]`. |
| `affiliation` | string | -1 | Nickname группы репутации. |
| `individual_name` | int | 0 | Id ресурса. |

### 6.4 NPC-солары

Секции `[Object]` файлов систем могут превратить solar-объект в NPC с помощью ключей `base`, `reputation`, `pilot`, `behavior` и `msg_id_prefix`; их читает `ParseSolarNpcKey` ([NpcCreate.cpp:23](../../../src/Content/NpcCreate.cpp#L23)), а описаны они в [systems-and-bases.md](systems-and-bases.md#33-секции-object-ключи-solar-npc).

---

## 7. Лут: `missions\lootprops.ini`

Файл читается дважды, между чтениями выполняется `reset()`: секции `[mLootProps]` — функцией `LoadLootProps` ([LootProps.cpp:52](../../../src/Content/LootProps.cpp#L52)), секции `[PhantomLoot]` — функцией `LoadPhantomLoot` ([:89](../../../src/Content/LootProps.cpp#L89)). Записи добавляются в порядке файла (проверки дубликатов нет); при поиске берётся первая запись с данным id.

### 7.1 `[mLootProps]`

Описывает, сколько единиц товара выпадает из уничтоженного NPC. Для товара без записи используются записи, чьи nickname в точности равны `default_commodity` или `default_equipment` (проверки на null нет: эти две записи должны существовать).

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `nickname` | id | нет | Nickname товара (сопоставляется с товаром выпадающего груза). |
| `drop_properties` | float, int, int, int, int, int | 0 для всех | Шесть позиционных значений. Значение 0 — шанс выпадения в процентах (хранится, делённое на 100). Значения 1-5 — целые числа, которые использует генератор лута ([SpaceSupport.cpp:413](../../../src/Content/SpaceSupport.cpp#L413)): значение 1 — минимальная ценность (сумма ценностей игроков), ниже которой ничего не выпадает, значение 2 — делитель, применяемый к избытку ценности (0 сохраняет число предметов), значение 3 — базовое количество, добавляемое к масштабированному количеству, значение 4 — потолок этой суммы (которая также ограничена числом несомых предметов), значение 5 — потолок итогового количества после применения шанса. Эти значения выведены из арифметики потребителя; в порте для них нет имён. |

### 7.2 `[PhantomLoot]`

Дополнительный лут, добавляемый кораблям, чей ранг репутации попадает в диапазон (см. ту же функцию в `SpaceSupport.cpp`).

| Ключ | Тип | По умолчанию | Значение |
|---|---|---|---|
| `nickname` | id | нет | Id выпадающего предмета (используется как id архетипа). |
| `toughness_range` | int, int | 0, 0 | Включающий диапазон ранга репутации корабля, который подходит; максимум `-1` означает отсутствие верхней границы. |
| `percent_chance` | float | 0 | Шанс в процентах (хранится, делённый на 100). |
| `num_to_drop` | int, int | 0, 0 | Минимальное и максимальное количество (`min + rand() * (max - min + 1) / 32768`). |
