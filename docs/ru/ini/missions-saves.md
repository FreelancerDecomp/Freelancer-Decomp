# Разделы сохранений, которые пишет и читает Content.dll (сюжет, миссии, состояние игрока)

Когда игрок сохраняется в одиночной игре, контроллер `Story`, активная сюжетная миссия, объект миссии, контроллер случайных миссий и состояние игрока Content дописывают в файл сохранения `.fl` каждый свои разделы; при загрузке те же классы читают их обратно. Этот документ перечисляет разделы, ключи и их точные форматы так, как их пишет и читает код. Окружающий контейнер файла сохранения и разделы, которые пишет слой server/common (`[Player]`, `[Group]`, `[Time]`, ...), описаны в [saves-and-initial-world.md](saves-and-initial-world.md).

## Расположение файла / как он находится

Сохранение `.fl` — это INI-текст в каталоге сохранений пользователя (см. [saves-and-initial-world.md](saves-and-initial-world.md)); при сохранении он передаётся контроллерам как `FILE*` (сообщение `PlayerSave`, только когда `pub::SinglePlayer()` истинно), а при загрузке — как открытый перематываемый `INI_Reader*` (сообщение `PlayerLoad`). Каждый загрузчик сначала запоминает состояние читателя (`get_state`), перематывает его (`reset`), сканирует весь файл и затем восстанавливает состояние, поэтому порядок разделов в файле для них не важен. Разбор выполняет `INI_Reader` (см. [parsers.md](parsers.md)).


## `[StoryInfo]`

Пишется и читается контроллером `Story` ([Story.cpp:331](../../../src/Content/Story.cpp#L331), [Story.cpp:345](../../../src/Content/Story.cpp#L345)).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `ship_bought` | 0 | bool (`true`/`false`) | false | Игрок купил новый корабль (флаг reputation hack). | [L399](../../../src/Content/Story.cpp#L399) |
| `Mission` | 0: имя контроллера<br>1: путь (только `GenericMission`) | str | `No_Mission` | Имя контроллера сюжетной миссии, создаваемого при загрузке: `Mission_01a`, `Mission_01b`, `Mission_02` .. `Mission_13`, `GenericMission` или `No_Mission` (миссии нет). Для `GenericMission` значение 1 — путь к скрипту миссии (см. [missions.md](missions.md#расположение-файла--как-он-находится)). **Запись выводит только значение 0**, поэтому сохранённый `GenericMission` теряет свой путь. Имя не проверяется; неизвестное имя не создаёт контроллер. | [L362](../../../src/Content/Story.cpp#L362) |
| `MissionNum` | 0: номер сюжетного этапа | int | 0 | Текущий сюжетный этап (0..42; см. [названия этапов](missions-support.md#названия-этапов-сюжета)). Запускает отправку сообщений `PlayerLoad` и `MissionStart` сюжетной миссии. Пишется, только если миссия активна или этап не равен 0. | [L376](../../../src/Content/Story.cpp#L376) |
| `delta_worth` | 0 | float | -1.0 | Стоимость денег и активов, необходимая до старта следующей сюжетной миссии (`-1` = не требуется). | [L394](../../../src/Content/Story.cpp#L394) |
| `debug` | 0 | bool | false | Отладочный флаг сюжета. | [L403](../../../src/Content/Story.cpp#L403) |

Формат записи: `[StoryInfo]`, `ship_bought = true|false`, необязательный `Mission = <name>`, `MissionNum = <n>`, `delta_worth = <float>`, затем `debug = 0|1`.

## `[Mission01aSave]`, `[Mission01bSave]`

Только эти две сюжетные миссии пишут собственный раздел ([StoryMissions.cpp:136](../../../src/Content/StoryMissions.cpp#L136), [StoryMissions.cpp:252](../../../src/Content/StoryMissions.cpp#L252)); остальные миссии опираются на `[MissionState]`.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `MissionStateNum` | 0 | int | - | Состояние миссии (коды `FAIL`/`SUCCEED`/`NO_STATE`). В `Mission_01a` пишется только после того, как игрок вошёл на первую базу, а при загрузке задаёт текущее и предыдущее состояние и повторно отправляет задания, если оно не равно 3. В `Mission_01b` читается после того, как скрипт заново создаёт состояние после принятия миссии. | [Mission01a L157](../../../src/Content/StoryMissions.cpp#L157), [Mission01b L296](../../../src/Content/StoryMissions.cpp#L296) |

## `[mPlayer]`

Состояние игрока Content ([PlayerState.cpp:259](../../../src/Content/PlayerState.cpp#L259) пишет, [Support.cpp:284](../../../src/Content/Support.cpp#L284) читает).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `locked_gate` | 0: id врат | uint | нет | Регистрирует блокировку врат для этого игрока (повторяется). | [Support.cpp:299](../../../src/Content/Support.cpp#L299) |
| `vnpc` | 0..3 | uint | нет | Запись виртуального NPC `base id, npc id, ?, ?` (читается как четыре беззнаковых числа; запись дописывает после чисел комментарий `; <base nickname>, <npc name>`). | [Support.cpp:304](../../../src/Content/Support.cpp#L304) |
| `rumor` | 0, 1 | uint | нет | Слух, который услышал игрок: два беззнаковых числа (роли в исходнике не названы). | [Support.cpp:333](../../../src/Content/Support.cpp#L333) |
| `can_dock` | 0 | bool | по умолчанию для состояния | Стыковка игрока разрешена (`Act_PlayerCanDock`). | [Support.cpp:313](../../../src/Content/Support.cpp#L313) |
| `dock_exception` | 0: id объекта | uint | нет | Исключение из `can_dock` (повторяется). | [Support.cpp:317](../../../src/Content/Support.cpp#L317) |
| `can_tl` | 0 | bool | по умолчанию для состояния | Торговые линии разрешены (`Act_PlayerCanTradelane`). | [Support.cpp:322](../../../src/Content/Support.cpp#L322) |
| `tlr_exception` | 0, 1: id колец | uint | нет | Пара-исключение (повторяется). | [Support.cpp:326](../../../src/Content/Support.cpp#L326) |
| `ship_type_killed` | 0: id типа корабля, 1: количество | uint, int | нет | Статистика убийств по архетипам кораблей (повторяется). | [Support.cpp:75](../../../src/Content/Support.cpp#L75) |
| `rm_completed` / `rm_aborted` / `rm_failed` | 0: тип случайной миссии (0..255), 1: количество | uchar, int | нет | Статистика случайных миссий по типам миссий (повторяется). | [Support.cpp:82](../../../src/Content/Support.cpp#L82) |
| `total_cash_earned` | 0 | float | 0 | Статистика. | [Support.cpp:103](../../../src/Content/Support.cpp#L103) |
| `total_time_played` | 0 | float | 0 | Статистика (секунды). | [Support.cpp:108](../../../src/Content/Support.cpp#L108) |
| `sys_visited` / `base_visited` / `holes_visited` | 0: id | uint | нет | Id посещённых систем / баз / прыжковых дыр; дубликаты игнорируются (повторяется). | [Support.cpp:113](../../../src/Content/Support.cpp#L113) |

Неизвестные ключи `[mPlayer]` передаются читателю статистики и игнорируются. После загрузки ранг игрока и «деньги до следующего ранга» пересчитываются по таблице toughness ([missions-support.md](missions-support.md#ptoughini)).

## `[locked_gates]`

Пишется `SaveContentGateLocks` и читается `LoadContentGateLocks` ([SpaceSupport.cpp:1312](../../../src/Content/SpaceSupport.cpp#L1312), [SpaceSupport.cpp:1335](../../../src/Content/SpaceSupport.cpp#L1335)).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `locked_gate` | 0: id врат | uint | нет | Врата, закрытые для всех (регистрируются для игрока 0). | [L1320](../../../src/Content/SpaceSupport.cpp#L1320) |
| `npc_locked_gate` | 0: id врат | uint | нет | Врата, закрытые для NPC. | [L1325](../../../src/Content/SpaceSupport.cpp#L1325) |

Раздел пишется, только если существует хотя бы одна блокировка.

## `[MissionState]` и состояние скрипта

Пишется `FMissionScript::SaveMissionState` ([MissionScript.cpp:1696](../../../src/Content/MissionScript.cpp#L1696)) между `[MissionState]` и `[BStoryMissionDone]` ([MissionScript.cpp:1058](../../../src/Content/MissionScript.cpp#L1058)); читается `LoadMissionState` ([MissionScript.cpp:1751](../../../src/Content/MissionScript.cpp#L1751)). Загрузчик останавливается на заголовке `[BStoryMissionDone]`. Все ключи находятся в одном разделе `[MissionState]`.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `mission_accepted` | 0 | bool/int (пишется `1`/`0`) | - | Является ли эта миссия текущей миссией игрока; `true` при загрузке заново присоединяет к миссии группу и уведомляет сюжет. | [L1808](../../../src/Content/MissionScript.cpp#L1808) |
| `att_clamp` | 0 | bool (`true`/`false`) | - | Ограничитель атак случайной популяции. | [L1825](../../../src/Content/MissionScript.cpp#L1825) |
| `tradelane_attacks` | 0 | bool | - | Атаки на торговых линиях включены. | [L1831](../../../src/Content/MissionScript.cpp#L1831) |
| `scan_clamp` | 0 | bool | - | Ограничитель сканирования. | [L1836](../../../src/Content/MissionScript.cpp#L1836) |
| `gcs_clamp` | 0 | bool | - | Ограничитель GCS. | [L1841](../../../src/Content/MissionScript.cpp#L1841) |
| `random_pop` | 0 | bool | - | Случайная популяция включена. | [L1846](../../../src/Content/MissionScript.cpp#L1846) |
| `hostile_clamp` | 0 | bool | - | Ограничитель враждебности (пишется с завершающим пробелом: `hostile_clamp = false `). | [L1851](../../../src/Content/MissionScript.cpp#L1851) |
| `msn_offer` | 0: id строки | int | - | Id текста предложения миссии; применяется, только если не равен нулю. | [L1859](../../../src/Content/MissionScript.cpp#L1859) |
| `msn_title` | 0: id строки | int | - | Id текста заголовка миссии; применяется, только если не равен нулю. | [L1864](../../../src/Content/MissionScript.cpp#L1864) |
| `nnpath` | 0: id строки перемещения, 1: id строки цели, 2,3,4: x,y,z, 5: метка, 6: id системы | int, int, float, str, uint | - | Активный путь NN (`Act_NNPath`); пишется, только пока он активен. При чтении скрывает все задания. Значение 5 применяется, если оно не пусто. | [L1869](../../../src/Content/MissionScript.cpp#L1869) |
| `rtc` | 0: путь к скрипту, 1: повторяемый | str (127), bool | - | Выполнявшийся RTC-скрипт; при загрузке первая строка `rtc` освобождает скрипты, запущенные конструктором миссии, затем каждая строка ставится в очередь как ещё не запущенный скрипт с его флагом повторяемости. Для каждого незавершённого скрипта пишется одна строка. | [L1766](../../../src/Content/MissionScript.cpp#L1766) |
| `ambi_scene` | 0: путь, 1: id комнаты, 2: id базы | str, uint, uint | - | Фоновая сцена, запущенная через `Act_AddAmbient`. | [L1783](../../../src/Content/MissionScript.cpp#L1783) |
| `friendly_fire_excused` | 0: метка | str (47) | - | Метка из `Act_DisableFriendlyFire` (повторяется). | [L1800](../../../src/Content/MissionScript.cpp#L1800) |
| `story_cue` | - | int | - | **Пишется, но никогда не читается**: у загрузчика нет для него обработчика. | [L1737](../../../src/Content/MissionScript.cpp#L1737) |

Загрузчик `[MissionState]` также собирает следующие за ним разделы `[NNObjective]` (только после того, как встретился заголовок `[MissionState]`), предварительно очищая список заданий; их грамматика та же, что в скриптах миссий ([missions.md](missions.md#nnobjective)), с числовыми `nickname`/`state`.

### `[TriggerSave]`

Один раздел на каждый активный триггер или один раздел, если ожидается `Act_Save`. Пишется в [MissionScript.cpp:1718](../../../src/Content/MissionScript.cpp#L1718) и [L1742](../../../src/Content/MissionScript.cpp#L1742); читается в [L1889](../../../src/Content/MissionScript.cpp#L1889).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `trigger` | 0: id триггера (числовой `CreateID`) | uint | - | Триггер, который был активен. Первая строка `trigger` в файле отбрасывает исходный набор активных триггеров скрипта; затем после загрузки каждый из них активируется. Следующие строки (в том же разделе) передаются функции `Load` условия этого триггера. | [L1893](../../../src/Content/MissionScript.cpp#L1893) |
| *состояние условия* | см. [missions-conditions.md](missions-conditions.md) | | | Одна строка, пишется активным условием: `Cnd_Timer = <elapsed>`, `Cnd_Destroyed = <count>`, `Cnd_DistShip = <timer>, <distance>`, `Cnd_DistVec = ...`, `Cnd_DistVecLbl = <timer>`, `Cnd_LootAcquired = <count>`, `Cnd_ProjHit = <count>`, `Cnd_ProjHitShipToLbl = <count>`, `Cnd_TetherBroke = <a>, <b>`, `Cnd_NPCSystemEnter = <label>,<id>, ...`, `Cnd_NPCSystemExit = ...`. Условия, не перечисленные здесь, ничего не пишут. | |

Значение `trigger` читается трижды через `get_value_int(0)` (каждое чтение разбирает одну и ту же строку); поэтому числовой id больше `INT_MAX` читается через `atoi`, совместимый с MSVCRT.

## Разделы объектов миссии (корабли, solar, vibe)

`FMissionObject::SaveMission` ([MissionSave.cpp:700](../../../src/Content/MissionSave.cpp#L700)) пишет снимок живой миссии; `LoadMissionSave` ([MissionSave.cpp:889](../../../src/Content/MissionSave.cpp#L889)) воссоздаёт из него таблицы возрождения. Для каждого корабля запись сначала выводит необязательный `[MetaBehavior]` (исходная строка guide, записывается дословно как строка формата) или `[MObjective]` (исходные строки директив, см. [missions-objlist.md](missions-objlist.md)), затем `[MsnShipSave]`; для каждой группы — `[MsnWingSave]`; для каждой группы случайных встреч — `[MsnRandEncSave]`.

| Раздел | Ключ / запись | Значения | Тип | Значение | Источник |
|---|---|---|---|---|---|
| `[MsnShipSave]` | `nickname` | 0 | str (47) | Имя корабля. | [L922](../../../src/Content/MissionSave.cpp#L922) |
| | `npc` | 0: id `[NPC]` | uint | Id записи персонажа (числовой хеш). | [L924](../../../src/Content/MissionSave.cpp#L924) |
| | `random_name`, `jumper`, `original_jump_flag` | 0 | bool (`0`/`1`) | Флаги. | [L926](../../../src/Content/MissionSave.cpp#L926) |
| | `label` | 0: id метки | int | Один хеш метки на строку (повторяется). | [L932](../../../src/Content/MissionSave.cpp#L932) |
| | `ol_priority` | 0 | int | Приоритет директивы. | [L934](../../../src/Content/MissionSave.cpp#L934) |
| | `hit_pts` | 0 | int | Текущие очки прочности. | [L936](../../../src/Content/MissionSave.cpp#L936) |
| | `radius` | 0 | float | Радиус корабля. | [L938](../../../src/Content/MissionSave.cpp#L938) |
| `[MsnWingSave]` | `nickname` | 0 | str | Имя группы; забирает предшествующие ей разделы `[MsnShipSave]`. | [L1029](../../../src/Content/MissionSave.cpp#L1029) |
| | `position` | 0,1,2 | float | Позиция (пишется, когда нет объекта прибытия). | [L1033](../../../src/Content/MissionSave.cpp#L1033) |
| | `orientation` | 0..8 | float | Матрица вращения 3x3, по строкам. | [L1039](../../../src/Content/MissionSave.cpp#L1039) |
| | `arrival_obj` | 0 | uint | Id объекта прибытия (пишется вместо позиции для jumper'ов, ещё находящихся снаружи). | [L1043](../../../src/Content/MissionSave.cpp#L1043) |
| | `formation` | 0 | uint | Хеш id строя. | [L1047](../../../src/Content/MissionSave.cpp#L1047) |
| | `ship` | 0 | str | Пишется (по одному на корабль), но загрузчиком **не читается**. | [L749](../../../src/Content/MissionSave.cpp#L749) |
| `[MsnRandEncSave]` | `rand_enc_owner` | 0 | uint | Id владеющего `[MsnRandEnc]`. | [L961](../../../src/Content/MissionSave.cpp#L961) |
| | `position`, `orientation`, `ol_priority`, `hit_pts` | как выше | | Размещение лидера. | [L965](../../../src/Content/MissionSave.cpp#L965) |
| | `num_ships` | 0 | int | Количество кораблей в группе. | [L983](../../../src/Content/MissionSave.cpp#L983) |
| | `ship_type` | 0...: id классов, затем `END` | uint | До 32 id классов кораблей на строку; чтение останавливается на первом значении, равном ровно `END`. Одна строка на вариант. | [L987](../../../src/Content/MissionSave.cpp#L987) |
| | `names` | 0...: имена, затем `END` | str (47) | До 32 имён кораблей, завершается `END`. | [L1005](../../../src/Content/MissionSave.cpp#L1005) |
| `[MsnSolarSave]` | `solar_name` | 0 | str (47) | Созданный `[MsnSolar]`, подлежащий возрождению (повторяется). | [L1058](../../../src/Content/MissionSave.cpp#L1058) |
| `[MarkObj]` | `name` | 0 | str (47) | Объект, помеченный `Act_MarkObj` (повторяется). | [L1070](../../../src/Content/MissionSave.cpp#L1070) |
| `[Invulnerables]` | `invulnerable` | 0: имя, 1: длительность, 2: флаг | str, float, bool | Пишется как `name, %f, 0|1`. | [L1082](../../../src/Content/MissionSave.cpp#L1082) |
| `[Decloaked]` | `decloaked` | 0 | str (47) | Имя объекта со снятой маскировкой (пишется как `decloaked= name`). | [L1098](../../../src/Content/MissionSave.cpp#L1098) |
| `[MsnVibeInfo]` | `vibe` | 0: имя A или `empty`, 1: имя B или `empty`, 2: rep A, 3: rep B, 4: отношение | str, str, int, int, float | Одно отношение между двумя сторонами; `empty` означает, что сторона — группа репутации (rep A / rep B), а не объект. | [L1110](../../../src/Content/MissionSave.cpp#L1110) |
| `[RandomEncounter]` | `nickname`, `enabled`, `encounter_type` и ключи параметров встречи | | | Пишется `SaveRandEncs` как `nickname = <name>`, `enabled = <0|1>, <count>`, затем дамп параметров встречи; читается `LoadRandEncsSave`: `enabled` обновляет каждый `[MsnRandEnc]` с этим именем (кроме имени `RandomMission`); `encounter_type` (`TL_Attack` / `SP_Attack`, здесь с учётом регистра) создаёт новую встречу только для nickname `RandomMission`. Ключи параметров: см. [ai-and-encounters.md](ai-and-encounters.md). | [L1122](../../../src/Content/MissionSave.cpp#L1122) |
| `[NNObjective]` | как в скриптах миссий | | | Задания, пишутся `SaveNNObjectives` в конце `SaveMission` ([MissionObject.cpp:915](../../../src/Content/MissionObject.cpp#L915)): `nickname = <numeric id>`, `type = <n>, <values>`, `state = <n>`, `hidden = 0|1`. | [MissionObject.cpp:839](../../../src/Content/MissionObject.cpp#L839) |

## `[RandomMission]`

Пишется контроллером случайных миссий (`DestroyMission`) и читается состоянием игрока Content; см. [random-missions.md](random-missions.md#секция-сохранения-randommission).
