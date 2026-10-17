# Случайные миссии: таблицы и генератор виньеток (Content.dll)

Случайные («генерируемые компьютером») миссии строятся из набора INI-таблиц: кривые сложности и денег, бонусный лут, уничтожаемые solar-объекты, формации solar-объектов, взвешенное дерево решений «виньеток» миссий с их текстами и коммуникационными последовательностями, а также таблицы классов кораблей и рангов. Этот документ описывает каждый ключ, который принимают эти файлы, и единственную секцию сохранения, которую пишет контроллер случайных миссий.

## Расположение файлов / как они находятся

* Все пути заданы относительно каталога Data игры (`[Freelancer] data path` из `freelancer.ini`, см. [freelancer-ini.md](freelancer-ini.md)) и открываются через `OpenContentDataIni` ([ContentDataPath.cpp:17](../../../src/Content/ContentDataPath.cpp#L17)).
* Семь файлов в `RandomMissions\` читаются конструктором глобального генератора случайных миссий `FRandomMissionGenerator` ([RandomMissionGen.cpp:311](../../../src/Content/RandomMissionGen.cpp#L311)), который является статическим объектом `Content.dll` (создаётся при загрузке DLL). Каждый файл открывается, только если он существует; отсутствующий файл просто оставляет таблицу пустой.
* `missions\shipclasses.ini`, второй проход по `ships\shiparch.ini` и `missions\RankDiff.ini` читаются в `LoadContentGameTables` ([Encounters.cpp:495](../../../src/Content/Encounters.cpp#L495)). Результат открытия там тоже не проверяется.
* Разбор выполняет `INI_Reader` (см. [parsers.md](parsers.md)): ключи и заголовки нечувствительны к регистру, повторяющиеся ключи выдаются в порядке файла, значения через запятую позиционные. Типы — как в [missions.md](missions.md#соглашения-принятые-в-таблицах). Значения-фракции — это nickname фракций (групп репутации), разрешаемые через `pub::Reputation::GetReputationGroup`; архетипы — через `pub::GetEquipmentID` / `pub::GetNicknameId`. Неизвестное имя молча разрешается в 0/-1 (не проверяется), если строка ниже не говорит иного.

| Файл | Заголовок(и) | Читатель |
|---|---|---|
| `RandomMissions\NpcRankToDiff.ini` | `[RankAndFormationSizeToDifficulty]` | [`LoadNpcRankToDifficulty`](../../../src/Content/RandomMissionGen.cpp#L169) |
| `RandomMissions\Diff2Money.ini` | `[Diff2Money]` | [конструктор генератора](../../../src/Content/RandomMissionGen.cpp#L326) |
| `RandomMissions\RMLootInfo.ini` | `[Global]`, `[RMBonusLoot]` | [`LoadRmLootInfo`](../../../src/Content/RandomMissionDB.cpp#L545) |
| `RandomMissions\SolarFormations.ini` | `[SolarFormation]` | [`LoadSolarFormations`](../../../src/Content/RandomMissionDB.cpp#L827) |
| `RandomMissions\KillableSolars.ini` | `[KillableSolar]` | [`LoadKillableSolars`](../../../src/Content/RandomEncounters.cpp#L58) |
| `RandomMissions\VignetteCriticalLoot.ini` | `[critical_loot]` | [`LoadVignetteCriticalLoot`](../../../src/Content/RandomEncounters.cpp#L256) |
| `RandomMissions\VignetteParams.ini` | `[DecisionNode]`, `[DocumentationNode]`, `[DataNode]` | [`FRandomMissionGenerator::Load`](../../../src/Content/RandomMissionGen.cpp#L408) |
| `missions\shipclasses.ini` | `[ShipClass]` | [`LoadShipClasses`](../../../src/Content/RandomMissionDB.cpp#L679) |
| `ships\shiparch.ini` (второй проход) | `[Ship]` | [`LoadShipMissionProperties`](../../../src/Content/RandomMissionDB.cpp#L762) |
| `missions\RankDiff.ini` | `[RankDiffDB]` | [`LoadRankDifficulties`](../../../src/Content/RandomMissionDB.cpp#L118) |

## `NpcRankToDiff.ini`

Сопоставляет рангу NPC список значений сложности (по одному на размер формации).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `NPCRank` (в `[RankAndFormationSizeToDifficulty]`) | 0: ранг NPC<br>1...: сложность для каждого размера формации | int<br>float | - | Добавляет значения в список этого ранга (если у ранга уже есть список, он сохраняется, а новые значения **дописываются** после старых). Значения читаются до первого пустого. | [RandomMissionGen.cpp:177](../../../src/Content/RandomMissionGen.cpp#L177) |

## `Diff2Money.ini`

Сопоставляет сложности миссии базовую награду.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `Diff2Money` (в `[Diff2Money]`) | 0: сложность<br>1: деньги | float<br>int | - | Одна точка кривой. Повторяющаяся сложность сохраняет первую точку. Награда для заданной сложности — линейная интерполяция между соседними точками, усечённая, затем округлённая до «круглой» суммы: суммы свыше 100 сохраняют две старшие цифры и, когда старшая цифра меньше 5, третью цифру как 5, если она была 5 или больше, и как 0 в противном случае (всё после неё становится 0). Сложность выше последней точки или ниже первой кодом не определена (выше возвращается 0, ниже интерполируется от (0, 0)). | [RandomMissionGen.cpp:334](../../../src/Content/RandomMissionGen.cpp#L334) |

## `RMLootInfo.ini`

Бонусный лут, выпадающий из NPC случайных миссий.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `[Global]` `BonusLootDropChance` | 0 | float | 0 (также когда файл отсутствует) | Шанс, что NPC выбросит бонусный лут; ограничивается диапазоном 0..1 (значение <= 0 или NaN даёт 0, >= 1 даёт 1). | [RandomMissionDB.cpp:553](../../../src/Content/RandomMissionDB.cpp#L553) |
| `[RMBonusLoot]` `archetype` | 0: nickname оборудования | str | 0 | Выпадающий предмет. | [L491](../../../src/Content/RandomMissionDB.cpp#L491) |
| `num_to_drop` | 0: минимум<br>1: максимум (необязательно) | int | 0, 0 | Диапазон количества; максимум по умолчанию равен минимуму. | [L495](../../../src/Content/RandomMissionDB.cpp#L495) |
| `faction` | 0...: nickname фракций или `all` | str | нет | `all` (без учёта регистра, проверяется только в значении 0) регистрирует запись для каждой фракции; иначе каждая перечисленная фракция получает собственную копию. **Секция без `faction` отбрасывается**, потому что записи хранятся по фракциям. | [L503](../../../src/Content/RandomMissionDB.cpp#L503) |
| `difficulty_range` | 0: минимум<br>1: максимум | float | 0, 0 | Запись является кандидатом для сложностей миссии внутри замкнутого диапазона. | [L521](../../../src/Content/RandomMissionDB.cpp#L521) |
| `weight` | 0 | int (читается как `get_value_int`, хранится как float) | 0 | Относительный вес выпадения. Выбор делается среди кандидатов (записи фракции `all` плюс собственные записи фракции) пропорционально весу; если сумма весов равна 0, ничего не выбирается. | [L526](../../../src/Content/RandomMissionDB.cpp#L526) |

## `SolarFormations.ini`

Именованные группы solar-объектов (станции, оборонительные турели, ...), размещаемые вокруг сражения.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` | 0 | id | 0 | Id формации; дубликат сохраняет первую формацию. | [RandomMissionDB.cpp:853](../../../src/Content/RandomMissionDB.cpp#L853) |
| `type` | 0 | str | -1 | Одно из `big_solar_formation` (0), `battle_background_formation` (1), `defensive_outpost_formation` (2), `loot_group_formation` (3); другие слова оставляют -1. | [L857](../../../src/Content/RandomMissionDB.cpp#L857) |
| `pos` | 0: `mandatory` или `optional`<br>1: тип solar<br>2,3,4: x,y,z<br>5..8: кватернион W,X,Y,Z (необязательно) | str<br>str<br>float | - | Один слот solar (повторяемый, по порядку). Тип solar — одно из `big_solar` (0), `defensive_solar` (1), `killable_neutral` (2), `background_solar` (3). **Неизвестное слово в значении 0 или 1 молча повторно использует значение предыдущей строки `pos`** (первая строка в файле начинается с 0 / 0). Кватернион нормализуется; без него слот имеет единичную ориентацию. | [L863](../../../src/Content/RandomMissionDB.cpp#L863) |

## `KillableSolars.ini`

Уничтожаемые solar-объекты, которые может разместить генератор; выбираются по фракции, типу solar, сложности и зоне, в которой находятся.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` (в `[KillableSolar]`) | 0 | id | пусто | Id записи. **Обязателен**: запись без nickname отбрасывается. | [RandomEncounters.cpp:87](../../../src/Content/RandomEncounters.cpp#L87) |
| `solar_type` | 0 | str | -1 | `big_solar`, `defensive_solar`, `killable_neutral`, `background_solar` (0-3). **Обязателен** (незаданный тип отбрасывает запись). | [L91](../../../src/Content/RandomEncounters.cpp#L91) |
| `string_id` | 0 | int | 0 | Id строки отображаемого имени. | [L106](../../../src/Content/RandomEncounters.cpp#L106) |
| `faction` | 0...: nickname фракций или `all` | str | нет | Фракции, которые могут использовать этот solar; `all` (без учёта регистра, значение 0) = любая. **Обязателен** (пустой список без `all` отбрасывает запись). | [L110](../../../src/Content/RandomEncounters.cpp#L110) |
| `archetype` | 0 | nick | 0 | Архетип solar. **Обязателен** (0 отбрасывает запись). | [L128](../../../src/Content/RandomEncounters.cpp#L128) |
| `loadout` | 0 | str | 0 | Loadout. **Обязателен**. | [L132](../../../src/Content/RandomEncounters.cpp#L132) |
| `hitpoints` | 0: минимум<br>1: максимум (необязательно) | int | 0 | Диапазон очков прочности; максимум по умолчанию равен минимуму. Должно выполняться `0 < min <= max`, иначе запись отбрасывается; значения выше максимума архетипа заменяются этим максимумом. | [L136](../../../src/Content/RandomEncounters.cpp#L136) |
| `difficulty` | 0: минимум<br>1: максимум | float | 0, 0 | Диапазон сложности миссии; должно выполняться `0 <= min <= max <= 100`, иначе запись отбрасывается. | [L145](../../../src/Content/RandomEncounters.cpp#L145) |
| `only_allowed_in_zone_type` | 0...: `rock`, `debris`, `ice`, `lava`, `crystal` | str | 0 (любая зона) | Ограничивает запись зонами этих видов (флаги 0x40, 0x80, 0x100, 0x200, 0x800). Завершающее `all` сравнивается, но не имеет эффекта; неизвестные слова игнорируются. | [L150](../../../src/Content/RandomEncounters.cpp#L150) |
| `pilot_choices` | 0 | int | 1 | Число вариантов пилота. | [L169](../../../src/Content/RandomEncounters.cpp#L169) |

Дублирующийся `nickname` сохраняет первую запись, прошедшую проверку.

## `VignetteCriticalLoot.ini`

Предметы лута, которые игрока просят уничтожить, захватить трактором или вернуть.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `faction` (в `[critical_loot]`) | 0...: nickname фракций или `all` | str | нет | Предлагающие фракции. | [RandomEncounters.cpp:280](../../../src/Content/RandomEncounters.cpp#L280) |
| `archetype` | 0 | str | 0 | Предмет лута (`pub::GetEquipmentID`). | [L297](../../../src/Content/RandomEncounters.cpp#L297) |
| `name` | 0: id строки | int | 0 | Отображаемое имя. | [L301](../../../src/Content/RandomEncounters.cpp#L301) |
| `why_desired` | 0, 1: id строк | int | 0, 0 | Два текста, объясняющих, зачем нужен лут. | [L305](../../../src/Content/RandomEncounters.cpp#L305) |
| `type` | 0...: `destroy`, `tractor`, `return` | str | нет | Виды миссий, для которых можно использовать предмет (1, 2, 3); другие слова игнорируются. | [L310](../../../src/Content/RandomEncounters.cpp#L310) |

Проверка не выполняется; неполные записи сохраняются как есть.

## `VignetteParams.ini` (дерево решений миссии)

Генератор строит бинарное дерево решений из узлов-«виньеток»; каждый путь от листа к корню дерева определяет тип миссии, тексты, коммуникационные последовательности и окно сложности. Загружается `FRandomMissionGenerator::Load` ([RandomMissionGen.cpp:408](../../../src/Content/RandomMissionGen.cpp#L408)). Каждый заголовок создаёт один узел: `[DecisionNode]`, `[DocumentationNode]` и `[DataNode]` читают ключи ниже; **любой другой заголовок всё равно вставляет пустой узел по умолчанию** с `node_id` -1. Узлы хранятся по `node_id`, повторяющийся id сохраняет первый узел. После загрузки дерево связывается: каждый `child_node` получает id родителя, дописанный в его список родителей (id потомка, у которого нет собственного узла, создаёт пустой узел по умолчанию); корень — наименьший id узла, к которому приходят вверх по первому родителю; потомки каждого узла с `implemented = false` отключаются вместе со своими поддеревьями; узлы, которые реализованы и не имеют потомков, являются листьями.

### Ключи узла

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `node_id` | 0 | int | -1 | Id узла (ключ таблицы). | [RandomMissionGen.cpp:1064](../../../src/Content/RandomMissionGen.cpp#L1064) |
| `child_node` | 0 | int | нет | Id дочернего узла. Повторяемый; узел решения имеет двух потомков (первый выбирается для решения `T` на пути по дереву, второй — для `F`). Вероятность первого потомка равна `1 / число потомков` («шанс первого потомка» из INI прочитать нельзя). | [L1069](../../../src/Content/RandomMissionGen.cpp#L1069) |
| `implemented` | 0 | bool | true | `false` отключает узел и его поддерево. | [L1075](../../../src/Content/RandomMissionGen.cpp#L1075) |
| `nickname`, `documentation` | 0 | id | пусто | Имя узла (оба ключа задают одно и то же поле). Генератор распознаёт имена `random_mission`, `destroy_solars_mission`, `assassinate_mission`, `destroy_ships_mission` и `loot_item_mission` (сравниваются через `CreateID`) как маркеры типа миссии при обходе пути; прочие имена — лишь документация. | [L1080](../../../src/Content/RandomMissionGen.cpp#L1080) |
| `weight` | 0 | float | 1.0 | Относительный вес узла при выборе пути через него; точный 0 (в том числе значение по умолчанию) становится 1.0. | [L969](../../../src/Content/RandomMissionGen.cpp#L969) |
| `difficulty` | 0: минимум<br>1: максимум | float | 0, 0 | Окно сложности миссии, к которому применяется узел. | [L996](../../../src/Content/RandomMissionGen.cpp#L996) |
| `offer_group` | 0...: nickname фракций или `all` | str | нет | Фракции, которые могут предлагать миссии через этот узел. `all` (значение 0, без учёта регистра) = каждая группа репутации. Дубликаты удаляются. | [L1001](../../../src/Content/RandomMissionGen.cpp#L1001) |
| `hostile_group` | 0...: nickname фракций или `all` | str | нет | Фракции, к которым могут принадлежать NPC миссии. | [L1006](../../../src/Content/RandomMissionGen.cpp#L1006) |
| `allowable_zone_types` | 0...: `open`, `field`, `exclusion` | str | все три (7) | Виды зон, пригодные для сражения (флаги 1, 2, 4). Если ключ присутствует, набор перестраивается только из перечисленных слов (неизвестные слова игнорируются; ключ без допустимых слов оставляет 0). | [L1018](../../../src/Content/RandomMissionGen.cpp#L1018) |
| `offer_text` | см. ниже | | | Фрагменты описания предложения (повторяемый). | [L974](../../../src/Content/RandomMissionGen.cpp#L974) |
| `objective_text` | см. ниже | | | Тексты по целям (повторяемый). | [L981](../../../src/Content/RandomMissionGen.cpp#L981) |
| `failure_text` | 0: id строки<br>1...: имена параметров | int<br>str | 0 | Текст, показываемый при провале миссии. | [L986](../../../src/Content/RandomMissionGen.cpp#L986) |
| `reward_text` | 0: id строки<br>1...: имена параметров | int<br>str | 0 | Текст награды. | [L991](../../../src/Content/RandomMissionGen.cpp#L991) |
| `comm_sequence` | см. ниже | | | Скриптовые радиопереговоры (повторяемый). | [L1011](../../../src/Content/RandomMissionGen.cpp#L1011) |

Любой другой ключ в секции узла игнорируется.

### Параметры текста

Слова, допустимые после id строки в `offer_text`, `objective_text`, `failure_text` и `reward_text` (`GRmTextParamNames`, без учёта регистра; [RandomMissionRuntime.cpp:76](../../../src/Content/RandomMissionRuntime.cpp#L76)). Каждое подставляется в строку (слоты `%s`/`%d`, по порядку) при построении текста: `FSTRING_PARAM_NONE` (0), `OFFER_GROUP` (1), `HOSTILE_GROUP` (2), `BIG_SOLAR` (3), `OTHER_SOLAR` (4), `CRITICAL_LOOT` (5), `TARGET_FULL_NAME` (6), `REWARD_MONEY` (7), `FACTION_REWARD` (8), `FACTION_HIT` (9), `TIME_TO_RETURN_TO_BATTLE` (10), `MISSION_DIFFICULTY` (11), `OFFER_BASE` (12), `TARGET_ZONE` (13), `TARGET_SYSTEM` (14). В `failure_text`/`reward_text`/`objective_text` слово, которого нет в таблице, молча повторяет предыдущий параметр (0 для первого); в `offer_text` первое неизвестное слово завершает запись. Какие параметры поддерживает тип миссии, зафиксировано в коде (например, путь уничтожения solar-объектов поддерживает 1-3 и 9-13).

### `offer_text`

`offer_text = <режим>, [singular|plural,] <id строки>, PARAM, PARAM, ..., [singular|plural,] <id строки>, ...` ([RandomMissionGen.cpp:671](../../../src/Content/RandomMissionGen.cpp#L671)).

* Значение 0: `append` или `replace` (без учёта регистра); всё прочее оставляет `replace`. Флаг хранится вместе с текстом; его применение (дописать к тексту, унаследованному от родительского узла, или заменить его) находится в построителе текста генератора.
* Остальное — список записей вида `[singular|plural,] <id строки>, PARAM, PARAM, ...`. Токен, который разбирается как **ненулевое число**, — это id строки записи; слова после него — имена параметров (таблица ниже), а первый токен, не являющийся именем параметра, завершает запись и читается заново как начало следующей записи (так что следующий id строки или слово `singular`/`plural` идёт сразу за ним). `singular` / `plural` задают форму записи; любое другое нечисловое слово перед id строки пропускается.
* При построении текста запись выводится, если: форма по умолчанию (ни одно из слов) — всегда; `singular` — её параметры содержат `OFFER_GROUP` и у предлагающей фракции есть singular-nickname (`faction_prop.ini`), либо же содержат `HOSTILE_GROUP` и враждебная фракция singular; `plural` — те же проверки с «не singular». Запись `singular`/`plural` без группового параметра никогда не выводится.

### `objective_text`

`objective_text = <цель>, <id строки>, PARAM, ...` ([RandomMissionGen.cpp:786](../../../src/Content/RandomMissionGen.cpp#L786)). Имена целей (без учёта регистра) и числовая цель, которую они выбирают: `guide_to_pre_battle` 1, `guide_to_main_battle` 2, `guide_to_main_from_pre` 3, `kill_big_solar` 4, `kill_other_solars` 5, `return_to_battle` 9, `destroy_loot` 10, `tractor_loot` 11, `return_loot` 12, `jettison_loot` 13, `pb_kill_ships` 19, `pb_kill_big_ships` 20, `pb_dont_kill_ships` 21, `mb_kill_ships` 22, `mb_kill_big_ships` 23, `mb_dont_kill_ships` 24. Неизвестное имя цели сохраняется под непригодным ключом (никогда не запрашивается). Повторяющаяся цель повторно разбирает уже существующий текст, т. е. побеждает более поздняя строка.

### `comm_sequence`

`comm_sequence = <триггер>, <получатель>, <float>, <мин. задержка>, <макс. задержка>, [<говорящий>, <текст>]...` ([RandomMissionGen.cpp:855](../../../src/Content/RandomMissionGen.cpp#L855)).

| Индекс | Тип | По умолчанию | Значение |
|---|---|---|---|
| 0 | str | `NONE` (0) | Триггер (без учёта регистра): `NONE` 0, `IN_SPACE_IN_SYSTEM` 1, `IN_SPACE_NOT_IN_SYSTEM` 2, `PLAYER_IN_PRE_BATTLE_SPAWN_RANGE` 3, `SUCCESS_KILLED_ALL_GOING_TO_MAIN` 4, `SUCCESS_NOT_KILLED_ALL_GOING_TO_MAIN` 5, `PLAYER_IN_MAIN_BATTLE_SOLAR_SPAWN_RANGE` 6, `PLAYER_IN_MAIN_BATTLE_SHIP_SPAWN_RANGE` 7, `FIRST_FRIENDLY_SPAWN` 8, `FIRST_DEFENSIVE_SHIP_SPAWN` 9, `FIRST_BIG_SHIP_SPAWN` 10, `LEFT_COMBAT_AREA` 11, `REENTERED_COMBAT_AREA` 12, `GLOBAL_SUCCESS` 13, `GLOBAL_FAIL_COMBAT_RANGE` 14, `GLOBAL_FAIL_DESTROYED_CRITICAL_LOOT` 15, `GLOBAL_FAIL_OTHER` 16. Неизвестное слово оставляет значение по умолчанию. |
| 1 | str | `BASE` (1) | Получатель: `NONE` 0, `BASE` 1, `BIG_SOLAR` 2, `BIG_SHIP` 3, `DEFENSIVE_SHIP` 4, `FRIENDLY_SHIP` 5, `PLAYER` 6, `ALL_PLAYERS` 7, `PLAYERS_IN_RANGE` 8, `PLAYERS_OUT_OF_RANGE` 9, `PLAYERS_IN_SYSTEM` 10, `PLAYERS_OUT_OF_SYSTEM` 11. |
| 2 | float | 0 | Сохраняется; его роль в исходнике не названа. |
| 3, 4 | float | 0, 0 | Минимальная и максимальная задержка в секундах. Для четырёх триггеров `GLOBAL_*` максимум выше 5.0 обрезается до 5.0, после чего минимум тоже устанавливается в 5.0, если он не меньше 5.0. |
| 5... | str, str | - | Пары `говорящий, текст`: говорящий — слово-получатель из таблицы выше (неизвестное = `BASE`); текст — строка, сохраняемая до 63 символов (буквальное имя строки переговоров). Пары читаются до первого пустого значения. |

## `shipclasses.ini`

Именованные группы архетипов кораблей, используемые генератором энкаунтеров. Читается `LoadShipClasses` ([RandomMissionDB.cpp:679](../../../src/Content/RandomMissionDB.cpp#L679)).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` (в `[ShipClass]`) | 0 | id | 0 | Id класса; дубликат класса сохраняет своё первое определение. | [L692](../../../src/Content/RandomMissionDB.cpp#L692) |
| `member` | 0: nickname корабля или nickname более раннего класса | id | нет | Добавляет члена. Если имя — nickname **более раннего** класса, вместо него добавляются члены этого класса (разворачивание). Каждый член хранится один раз. Повторяемый. | [L696](../../../src/Content/RandomMissionDB.cpp#L696) |

## `ships\shiparch.ini` (второй проход: свойства миссий)

Файл архетипов читается второй раз только ради свойств миссий в его секциях `[Ship]` (остальная часть файла описана в [archetypes-and-equipment.md](archetypes-and-equipment.md)). Читается один раз ([RandomMissionDB.cpp:762](../../../src/Content/RandomMissionDB.cpp#L762)).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` | 0 | str | 0 | Архетип корабля (`pub::GetShipID`). | [L777](../../../src/Content/RandomMissionDB.cpp#L777) |
| `mission_property` | 0 | str | нет | `can_use_berths`, `can_use_med_moors` (сохраняется как свойство средних швартовок) или `can_use_large_moors` (без учёта регистра); другие слова игнорируются. Повторяемый; каждое свойство хранится один раз. Сообщает генератору, у каких доков может появиться корабль. | [L781](../../../src/Content/RandomMissionDB.cpp#L781) |

## `RankDiff.ini`

Сопоставляет прогрессу сюжета базовую сложность случайных миссий (одиночная игра).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `rank_diff` (в `[RankDiffDB]`) | 0: имя стадии сюжета<br>1: сложность | str<br>float | стадия предыдущей строки | Имена стадий — те, что в [missions-support.md](missions-support.md#названия-этапов-сюжета) (без учёта регистра). **Имя, которого нет в таблице, повторно использует стадию предыдущей строки** (первая строка файла получает неопределённое значение). Сложность ограничивается диапазоном 0..100. После загрузки записи сортируются по стадии. В одиночной игре стадия игрока выбирает запись (или ближайшую меньшую), а сложность миссии затем выбирается из окна 1.3689^3 вокруг неё. | [RandomMissionDB.cpp:132](../../../src/Content/RandomMissionDB.cpp#L132) |

## Секция сохранения `[RandomMission]`

Контроллер случайных миссий (`DestroyMission`) сохраняет себя секцией `[RandomMission]`, `type = DestroyMission`, за которой следуют ключи ниже ([DestroyMission.cpp:407](../../../src/Content/DestroyMission.cpp#L407), [RandomMissionDB.cpp:305](../../../src/Content/RandomMissionDB.cpp#L305)); загрузка выполняется в `LoadContentPlayerState` ([Support.cpp:346](../../../src/Content/Support.cpp#L346)) и `FRandomMissionLoader::Load` ([DestroyMission.cpp:659](../../../src/Content/DestroyMission.cpp#L659)). При загрузке миссия перестраивается из этих значений и `random_seed` (сам путь по дереву не сохраняется: генератор повторно инициализируется `random_seed`, чтобы получились тот же путь по дереву и те же тексты).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `type` | 0: имя контроллера | str (до 60 символов) | - | Записывается `DestroyMission`. Секция обрабатывается только через строку `type`; значение используется как имя контроллера для `pub::Controller::Create` (сопоставляется через `stricmp` с таблицей контроллеров Content; `DestroyMission` — единственный контроллер случайных миссий), тогда как данные всегда загружаются как данные destroy-mission. | [Support.cpp:352](../../../src/Content/Support.cpp#L352) |
| `offer_group` | 0 | int | -1 | Предлагающая фракция (id группы репутации). | [RandomMissionDB.cpp:320](../../../src/Content/RandomMissionDB.cpp#L320) |
| `offer_base` | 0 | uint | 0 | База, предложившая миссию. | [L325](../../../src/Content/RandomMissionDB.cpp#L325) |
| `offer_ids` | 0 | int | 0 | Id строки предложения. | [L330](../../../src/Content/RandomMissionDB.cpp#L330) |
| `reward` | 0 | int | 0 | Денежная награда. | [L335](../../../src/Content/RandomMissionDB.cpp#L335) |
| `rep_reward` | 0 | float | 0 | Полученная репутация. | [L340](../../../src/Content/RandomMissionDB.cpp#L340) |
| `rep_hit` | 0 | float | 0 | Репутация, потерянная у враждебной группы. | [L345](../../../src/Content/RandomMissionDB.cpp#L345) |
| `difficulty` | 0 | float | 0 | Сложность миссии. | [L350](../../../src/Content/RandomMissionDB.cpp#L350) |
| `jump_pref` | 0 | int | 0 | Предпочтение маршрута, передаваемое в поиск маршрута прыжков (предпочтение врат/дыр). | [L355](../../../src/Content/RandomMissionDB.cpp#L355) |
| `enemy_group` | 0 | uint | `0xFFFFFFFF` | Враждебная фракция. | [DestroyMission.cpp:583](../../../src/Content/DestroyMission.cpp#L583) |
| `dest_sys` | 0 | uint | 0 | Id системы назначения. | [L588](../../../src/Content/DestroyMission.cpp#L588) |
| `objective_pos` | 0,1,2 | **int** | 0,0,0 | Позиция цели; записывается как `%g,%g,%g`, но читается обратно через `get_value_int`, поэтому дробные части усекаются. | [L593](../../../src/Content/DestroyMission.cpp#L593) |
| `objective_zone` | 0 | uint | 0 | Id зоны; добавляется в глобальный список использованных зон целей. | [L600](../../../src/Content/DestroyMission.cpp#L600) |
| `random_seed` | 0 | uint | 0 | Seed, используемый для воссоздания той же виньетки. | [L608](../../../src/Content/DestroyMission.cpp#L608) |

Побеждает первый подходящий ключ в строке; неизвестные ключи игнорируются. У генератора есть также читатель и писатель `tree_path = T/F string` (`FRmTreePath::Load`/`Save`, [RandomMissionGen.cpp:1115](../../../src/Content/RandomMissionGen.cpp#L1115)), но в порте их никто не вызывает, поэтому `tree_path` в сохранениях не встречается. Секция сохраняется только в одиночной игре и только если первая конечная машина состояний миссии согласна, что сохранение возможно.
