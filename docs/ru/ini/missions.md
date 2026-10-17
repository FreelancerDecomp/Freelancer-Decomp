# Файлы скриптов миссий (Content.dll)

Документ описывает INI-подобные скрипты миссий, которые `Content.dll` загружает при старте сюжетной миссии, «GenericMission» или скриптовой сцены: заголовок `[Mission]`, локальные для миссии таблицы NPC/кораблей/solar/лута, цели, диалоги, мета-поведения и движок `[Trigger]`. Условия триггеров (`Cnd_*`), действия триггеров (`Act_*`) и директивы `[ObjList]` описаны в отдельных файлах (см. [Связанные файлы](#связанные-файлы)).

Всё ниже сверено с портированным кодом в `src/Content/`. Там, где код значение не проверяет, это указано; списки допустимых значений приведены только там, где код сравнивает с ними.

## Расположение файла / как он находится

* Пути скриптов миссий **относительны каталога Data игры** (каталог `[Freelancer] data path` из `freelancer.ini`, см. [freelancer-ini.md](freelancer-ini.md)). Они открываются через `OpenContentDataIni(RelativePath, Reader)` ([ContentDataPath.cpp:17](../../../src/Content/ContentDataPath.cpp#L17)): `pub::GetDataPath()->GetFileSysPath(DataDir)`, затем `_makepath(FullPath, nullptr, DataDir, RelativePath, nullptr)` и `INI_Reader::open(FullPath, false)`. Отсутствие файла на этом уровне не ошибка; ряд вызывающих игнорирует возвращаемое значение и просто ничего не загружает.
* У сюжетных миссий пути зашиты в код ([StoryMissions.cpp](../../../src/Content/StoryMissions.cpp)):

  | Контроллер миссии | Путь (относительно Data) |
  |---|---|
  | `Mission_01a` | `Missions\M01a\m01a.ini` ([L100](../../../src/Content/StoryMissions.cpp#L100)) |
  | `Mission_01b` | `Missions\M01b\m01b.ini` ([L201](../../../src/Content/StoryMissions.cpp#L201)) |
  | `Mission_02` ... `Mission_13` | `Missions\M02\m02.ini` ... `Missions\M13\m13.ini` ([L312-L435](../../../src/Content/StoryMissions.cpp#L312)) |
  | `GenericMission` | берётся из сохранения: второе значение `Mission = GenericMission, <path>` в `[StoryInfo]` ([Story.cpp:368](../../../src/Content/Story.cpp#L368)); путь передаётся без изменений и не проверяется |

* RTC-скрипты (цели `Act_AddRTC`, `missions\m01a\M001a_s006x_Li01_02_nrml.ini`, `missions\m01a\rtc_success.ini`) — файлы другого типа, см. [missions-support.md](missions-support.md#rtc-скрипты-файлы-characterencounter).
* Разбор выполняет `INI_Reader` (Common.dll, текст или BINI): ключи и заголовки нечувствительны к регистру, повторяющиеся ключи доставляются все в порядке файла, значения разделены запятыми и индексируются с 0. См. [parsers.md](parsers.md). Скрипты миссий читаются **позиционно** (`get_value_*(index)`), а не по имени, для каждой записи триггера.

## Соглашения, принятые в таблицах

* **Индекс** `0`, `1`, ... — позиция значения, разделённого запятыми, в строке. `empty(i)` означает `is_value_empty(i)` (значение отсутствует или пустое).
* **Типы**: `int` = `get_value_int` (разбор в стиле `atoi`; `12.7` становится `12`), `float` = `get_value_float` (`ParseNumber` движка; см. [parsers.md](parsers.md)), `bool` = `get_value_bool` (ровно `true`/`1`/`false`/`0`, всё остальное пишет в лог "Invalid bool value" и читается как false), `str` = `get_value_string`, `id` = строка, хешируемая `CreateID` (хеш без учёта регистра), `nick` = строка, разрешаемая `pub::GetNicknameId` (тот же хеш, используется для nickname solar/объектов, определённых в другом месте).
* **Label** (метка) = локальное для миссии имя. Метка разрешается во время выполнения (`GetLabelObjectId` / `GetLabelShipId`) по порядку использования: литерал `Player`, nickname кораблей `[MsnShip]`/`[MsnFormation]`, nickname `[MsnSolar]`, nickname `[MsnLoot]` и все дополнительные значения `label =`, прикреплённые к этим записям. Несколько записей могут делить одну метку; действия и условия с групповой меткой тогда применяются к каждому члену.
* **TString<N>** = буферы фиксированного размера: имя длиннее `N-1` символов молча усекается (`TString<0x30>` = 47 символов, `TString<0x20>` = 31, `TString<0x40>` = 63, `TString<0x80>` = 127).
* «По умолчанию» — значение, которое имеет объект при отсутствии ключа (конструктор/статический инициализатор); у INI-парсера собственного механизма значений по умолчанию нет.

## Порядок загрузки внутри файла миссии

`FMissionScript::LoadMission` ([MissionScript.cpp:954](../../../src/Content/MissionScript.cpp#L954)) сначала вызывает базовый `FMissionObject::LoadMission` ([MissionObject.cpp:4139](../../../src/Content/MissionObject.cpp#L4139)), который проходит по всем заголовкам в порядке файла и обрабатывает заголовки из таблицы ниже; затем сбрасывает reader и проходит файл второй раз для `[Trigger]`, `[NPC]` и `[Dialog]` (предварительная регистрация костюмов). Неизвестные заголовки молча пропускаются обоими проходами.

| Заголовок | Проход | Значение | Раздел |
|---|---|---|---|
| `[Mission]` | 1 | id текстов названия/предложения, награда, файл архетипов кораблей NPC | [ниже](#mission) |
| `[MsnLoot]` | 1 | контейнер с лутом, который миссия может создать | [ниже](#msnloot) |
| `[MsnSolar]` | 1 | solar-объект миссии | [ниже](#msnsolar) |
| `[MsnShip]`, `[MsnFormation]` | 1 | корабль или формация миссии | [ниже](#msnship--msnformation) |
| `[MsnRandEnc]` | 1 | случайная атакующая встреча, принадлежащая миссии | [ниже](#msnrandenc) |
| `[ObjList]` | 1 | именованный список директив ИИ | [missions-objlist.md](missions-objlist.md) |
| `[MetaBehavior]` | 1 | именованный список guide-поведений | [ниже](#metabehavior) |
| `[Dialog]` | 1, 2 | скриптовый диалог по связи | [ниже](#dialog) |
| `[NNObjective]` | 1 | запись текста цели навигационной сети (NN) | [ниже](#nnobjective) |
| `[NPC]` | 2 | запись персонажа-NPC миссии | [ниже](#npc) |
| `[Trigger]` | 2 | один триггер | [ниже](#trigger) |

Секции **сохранения**, которые эти классы тоже читают/пишут (`[MissionState]`, `[TriggerSave]`, `[MsnShipSave]`, ...), описаны в [missions-saves.md](missions-saves.md).

## `[Mission]`

Читается `LoadMissionHeaderValues` ([MissionObject.cpp:3729](../../../src/Content/MissionObject.cpp#L3729)). Может встречаться несколько раз; каждое вхождение обновляет один и тот же объект миссии.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `mission_title` | 0: id в таблице строк | int | нет (текст не задан) | Строит `FmtStr` названия миссии (`begin_mad_lib(id)`); не проверяется. | [MissionObject.cpp:3733](../../../src/Content/MissionObject.cpp#L3733) |
| `mission_offer` | 0: id в таблице строк | int | нет | Строит текст предложения миссии. | [MissionObject.cpp:3738](../../../src/Content/MissionObject.cpp#L3738) |
| `reward` | 0: деньги | int | 0 | Денежная награда, хранящаяся в миссии; при успехе миссии делится между членами группы. | [MissionObject.cpp:3743](../../../src/Content/MissionObject.cpp#L3743) |
| `npc_ship_file` | 0: путь относительно Data | str | нет | Открывает этот файл через `OpenContentDataIni` и загружает записи `[NPCShipArch]` в локальную для миссии таблицу кораблей NPC (формат тот же, что у `missions\npcships.ini`, см. [ai-and-encounters.md](ai-and-encounters.md)). Результат открытия не проверяется. | [MissionObject.cpp:3747](../../../src/Content/MissionObject.cpp#L3747) |

## `[NPC]`

Запись персонажа миссии (имя, внешность, голос, архетип корабля). Читается `LoadMsnNpc` ([NpcShips.cpp:356](../../../src/Content/NpcShips.cpp#L356)) во втором проходе; запись хранится под id своего `nickname` (уникальная вставка: повторный nickname сохраняет первую запись). Те же ключи без `space_costume`/`npc_ship_arch` используются в `missions\specific_npc.ini` (см. [missions-support.md](missions-support.md#specific_npcini)).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` | 0: имя | id | пусто (запись тогда игнорируется по возвращаемому значению загрузчика) | Ключ записи; также имя, на которое ссылается `[MsnShip] npc =`. | [NpcShips.cpp:367](../../../src/Content/NpcShips.cpp#L367) |
| `space_costume` | 0: часть головы, 1: часть тела, 2...: аксессуары | str | нули | Костюм, показываемый в космической связи. Имена разрешаются `pub::GF::GetBodyPart` (голова с типом 1, тело с типом 0) и `pub::GF::GetAccessory`; аксессуары читаются до первого пустого значения (без проверки границ). | [NpcShips.cpp:373](../../../src/Content/NpcShips.cpp#L373) |
| `voice` | 0: nickname голоса | str | 0 | `pub::GetVoiceID`; имена длиннее 79 символов усекаются. | [NpcShips.cpp:385](../../../src/Content/NpcShips.cpp#L385) |
| `npc_ship_arch` | 0: nickname из `[NPCShipArch]` | id | сброшено | Шаблон корабля/пилота из `npcships.ini` (или из `npc_ship_file` миссии). | [NpcShips.cpp:399](../../../src/Content/NpcShips.cpp#L399) |
| `affiliation` | 0: nickname фракции | str | `0xFFFFFFFF` | `pub::Reputation::GetReputationGroup`. | [NpcShips.cpp:403](../../../src/Content/NpcShips.cpp#L403) |
| `individual_name` | 0: id в таблице строк | int | 0 | id отображаемого имени персонажа. | [NpcShips.cpp:407](../../../src/Content/NpcShips.cpp#L407) |

## `[MsnLoot]`

Определение одного контейнера с лутом. Каждый заголовок добавляет запись; ключи применяются к новейшей записи в `LoadMissionLootValues` ([MissionObject.cpp:3541](../../../src/Content/MissionObject.cpp#L3541)). Создаётся `Act_SpawnLoot`.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` | 0: имя | str (47 символов) | пусто | Имя записи; его `CreateID` также добавляется в список меток записи. | [L3545](../../../src/Content/MissionObject.cpp#L3545) |
| `archetype` | 0: nickname оборудования | str | 0 | `pub::GetEquipmentID`. | [L3552](../../../src/Content/MissionObject.cpp#L3552) |
| `string_id` | 0: id | int | 0 | id строки имени контейнера. | [L3560](../../../src/Content/MissionObject.cpp#L3560) |
| `position` | 0,1,2: x,y,z | float | 0,0,0 | Мировая позиция. | [L3564](../../../src/Content/MissionObject.cpp#L3564) |
| `rel_pos_obj` | 0: метка | str (47) | пусто | Позиция задана относительно этого объекта; установка ключа также взводит флаг относительной позиции (даже для пустого значения). | [L3570](../../../src/Content/MissionObject.cpp#L3570) |
| `rel_pos_offset` | 0,1,2: x,y,z | float | 0,0,0 | Смещение от `rel_pos_obj`. | [L3576](../../../src/Content/MissionObject.cpp#L3576) |
| `orientation` | 0..3: кватернион W,X,Y,Z | float | единичный | Нормализуется перед использованием (нулевой кватернион не защищён). | [L3582](../../../src/Content/MissionObject.cpp#L3582) |
| `velocity` | 0,1,2 | float | 0 | Начальная линейная скорость. | [L3589](../../../src/Content/MissionObject.cpp#L3589) |
| `angular_velocity` | 0,1,2 | float | 0 | Начальная угловая скорость. | [L3595](../../../src/Content/MissionObject.cpp#L3595) |
| `equip_amount` | 0: количество | int | 0 | Единиц в контейнере. | [L3601](../../../src/Content/MissionObject.cpp#L3601) |
| `health` | 0: масштаб | float | 0 | Масштаб очков прочности. | [L3605](../../../src/Content/MissionObject.cpp#L3605) |
| `label` | 0: метка | id | нет | Добавляет метку (может повторяться). | [L3609](../../../src/Content/MissionObject.cpp#L3609) |
| `can_jettison` | 0 | bool | флаг записи «нельзя выбросить» = 0 (т. е. можно выбросить) | `false` помечает контейнер как не выбрасываемый. | [L3613](../../../src/Content/MissionObject.cpp#L3613) |

Конструктор записи лута также задаёт «ИИ может тянуть лучом» = 1 и «лут миссии» = 0 ([MissionObject.cpp:3937](../../../src/Content/MissionObject.cpp#L3937)); ни то, ни другое нельзя прочитать из INI.

## `[MsnSolar]`

Solar-объект, который миссия размещает в космосе. Разбирается `LoadMissionSolarValues` ([MissionObject.cpp:3646](../../../src/Content/MissionObject.cpp#L3646)); создаётся `Act_SpawnSolar` или при старте миссии.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` | 0: имя | str (47) | пусто | Имя; его хеш также добавляется как метка. | [L3650](../../../src/Content/MissionObject.cpp#L3650) |
| `position` | 0,1,2 | float | 0,0,0 | Позиция. | [L3657](../../../src/Content/MissionObject.cpp#L3657) |
| `orientation` | 0..3: W,X,Y,Z | float | единичный | Кватернион, нормализуется. | [L3667](../../../src/Content/MissionObject.cpp#L3667) |
| `faction` | 0: nickname фракции | str | `0xFFFFFFFF` | `pub::Reputation::GetReputationGroup`. | [L3671](../../../src/Content/MissionObject.cpp#L3671) |
| `string_id` | 0: id | int | нет | id строки отображаемого имени. | [L3675](../../../src/Content/MissionObject.cpp#L3675) |
| `pilot` | 0: nickname личности пилота | id | не задано | Ищется в таблице личностей пилотов (`pilots_*.ini`); флаг «есть пилот» взводится только при успешном поиске. | [L3680](../../../src/Content/MissionObject.cpp#L3680) |
| `archetype` | 0: nickname архетипа solar | nick | 0 | `pub::GetNicknameId`. | [L3687](../../../src/Content/MissionObject.cpp#L3687) |
| `loadout` | 0: nickname loadout | str | 0 | `pub::GetLoadoutID`. | [L3691](../../../src/Content/MissionObject.cpp#L3691) |
| `system` | 0: nickname системы | str | 0 | `pub::GetSystemID`. | [L3695](../../../src/Content/MissionObject.cpp#L3695) |
| `base` | 0: nickname базы | str | 0 | `pub::GetBaseID`. | [L3699](../../../src/Content/MissionObject.cpp#L3699) |
| `voice` | 0: nickname голоса | str | 0 | `pub::GetVoiceID`. | [L3703](../../../src/Content/MissionObject.cpp#L3703) |
| `radius` | 0 | float | 150.0 | Радиус, используемый при проверках размещения/видимости. | [L3707](../../../src/Content/MissionObject.cpp#L3707) |
| `label` | 0: метка | id | нет | Дополнительная метка (может повторяться). | [L3711](../../../src/Content/MissionObject.cpp#L3711) |
| `costume` | 0: голова, 1: тело, 2...: аксессуары | str | нули | Та же грамматика, что у `[NPC] space_costume`. | [L3715](../../../src/Content/MissionObject.cpp#L3715) |
| `visit` | 0 | str | 0 | Эффект имеет только точное слово `unknowable` (без учёта регистра): оно задаёт флаг посещения solar равным `0x80`. Всё остальное игнорируется. | [L3719](../../../src/Content/MissionObject.cpp#L3719) |

## `[MsnShip]` / `[MsnFormation]`

Оба заголовка используют один парсер (`LoadMissionShipValues`, [MissionObject.cpp:3764](../../../src/Content/MissionObject.cpp#L3764)): каждый заголовок создаёт запись **группы**; ключи уровня корабля пишут в слот шаблонного корабля, являющийся последним кораблём группы. Ключ `formation =` или `ship =` *удаляет* этот слот шаблона и превращает группу в формацию / группу-ссылку, после чего последующие ключи уровня корабля теряют смысл (код этого не защищает и пишет через нулевой слот, как retail; не ставьте ключи уровня корабля после `ship =` / `formation =`).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` | 0: имя | str (47) | пусто | Имя группы и шаблонного корабля; его хеш добавляется в метки корабля. | [L3771](../../../src/Content/MissionObject.cpp#L3771) |
| `npc` | 0: nickname из `[NPC]` | id | сброшено | Запись персонажа пилота. | [L3781](../../../src/Content/MissionObject.cpp#L3781) |
| `random_name` | 0 | bool | false | Случайное имя пилота. | [L3785](../../../src/Content/MissionObject.cpp#L3785) |
| `label` | 0: метка | id | нет | Добавляет метку (может повторяться). | [L3789](../../../src/Content/MissionObject.cpp#L3789) |
| `init_objectives` | 0: nickname из `[ObjList]` | id | нет | Начальный список директив. Игнорируется, если у этого корабля уже задан `init_metabehavior`. | [L3793](../../../src/Content/MissionObject.cpp#L3793) |
| `init_metabehavior` | 0: nickname из `[MetaBehavior]` | id | нет | Начальный guide. Игнорируется, если уже задан `init_objectives`. | [L3799](../../../src/Content/MissionObject.cpp#L3799) |
| `jumper` | 0 | bool | false | Корабль прибывает через jump gate (также копируется в «исходный флаг прыжка»). | [L3805](../../../src/Content/MissionObject.cpp#L3805) |
| `cargo` | 0: nickname товара, 1: количество | str, float | нет | `pub::GetGoodID`; неизвестный товар пропускает всю строку. Количество — `ftol(float)`. Может повторяться (добавляет). | [L3811](../../../src/Content/MissionObject.cpp#L3811) |
| `hit_points` | 0 | int | -1 (брать из архетипа) | Задаёт и начальные очки прочности, и очки прочности при возрождении. | [L3824](../../../src/Content/MissionObject.cpp#L3824) |
| `radius` | 0 | float | 7.0 | Радиус корабля, используемый для интервалов при создании. | [L3830](../../../src/Content/MissionObject.cpp#L3830) |
| `system` | 0: nickname системы | str | не задано | Сохраняется, только если система есть в таблице систем Content. | [L3834](../../../src/Content/MissionObject.cpp#L3834) |
| `position` | 0,1,2 | float | 0,0,0 | Позиция создания группы. | [L3846](../../../src/Content/MissionObject.cpp#L3846) |
| `rel_pos` | 0: курс (градусы), 1: метка, 2: дальность | float, str (47), float | курс 0, дальность 2500.0 | Создание относительно другого объекта; взводит флаг относительной позиции. Курс умножается на pi/180. Значение 2 переопределяет дальность, только если не пусто. | [L3856](../../../src/Content/MissionObject.cpp#L3856) |
| `orientation` | 0..3: W,X,Y,Z | float | единичный | Взводит флаг «есть ориентация». | [L3867](../../../src/Content/MissionObject.cpp#L3867) |
| `arrival_obj` | 0: метка, 1: id | str (47), int | пусто / -1 | Прибытие через этот объект; также обнуляет радиус каждого уже имеющегося слота корабля. Значение 1 — только если не пусто. | [L3877](../../../src/Content/MissionObject.cpp#L3877) |
| `formation` | 0: nickname формации | id | `CreateID("transport_liberty")` | Формация из `formations.ini` (см. [ai-and-encounters.md](ai-and-encounters.md)). Удаляет слот шаблонного корабля. | [L3888](../../../src/Content/MissionObject.cpp#L3888) |
| `ship` | 0: nickname более ранней группы `[MsnShip]` | str (47) | нет | Втягивает первый корабль указанной более ранней группы в эту группу и **удаляет ту раннюю группу**. Имя, не совпавшее ни с одной группой, игнорируется. Удаляет слот шаблона. | [L3899](../../../src/Content/MissionObject.cpp#L3899) |

## `[MsnRandEnc]`

Случайная встреча-атака в тредлейне/космосе, принадлежащая миссии. Ключи читает `FMissionObject::LoadMission` ([MissionObject.cpp:4168](../../../src/Content/MissionObject.cpp#L4168)).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` | 0: имя | str (31), id | пусто | Имя, используемое `Act_EnableEnc` / `Act_DisableEnc` и сохранением. | [L4176](../../../src/Content/MissionObject.cpp#L4176) |
| `encounter_type` | 0: тип | str | нет | `TL_Attack` (без учёта регистра; создаёт объекты `TLParams`/`TLPzr`) или `SP_Attack` (`SPParams`/`SPPzr`). Любое другое значение оставляет прежние объекты (на свежей записи они нулевые, так что секция тогда не имеет пригодной встречи). **Остальные ключи секции читает сам объект-populator**, поэтому `encounter_type` должен идти после `nickname`, а все следующие ключи относятся к грамматике параметров встреч, описанной в [ai-and-encounters.md](ai-and-encounters.md). | [L4182](../../../src/Content/MissionObject.cpp#L4182) |

## `[NNObjective]`

Одна запись списка целей «навигационной сети», показываемого в журнале миссии. Читается `LoadNNObjective` ([MissionObject.cpp:839](../../../src/Content/MissionObject.cpp#L839)); та же функция загружает эти секции из сохранения. Секция **обязана** содержать корректную строку `type`; без неё секция отбрасывается.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` | 0: имя или число | id | сброшено | Если значение 0 разбирается (MSVCRT `atoi`) в ненулевое число, оно используется напрямую как id цели (так перезагружаются сохранения); иначе `CreateID(name)`. На него ссылаются `Act_SetNNObj`, `Act_SetNNState`, `Act_SetNNHidden`. | [L851](../../../src/Content/MissionObject.cpp#L851) |
| `type` | 0: тип; 1...: зависят от типа | см. ниже | нет | Создаёт цель и разбирает остальные значения. Значение 0 — либо число (первый символ цифра или `-`; `0` пропускает строку), либо слово: `navmarker` = 2, `base` = 1, `ids` = 3, `rep_inst` = 4 (без учёта регистра). Всё остальное пропускает строку. Типы 5-8 существуют только как числа (их пишут сохранения; 7 и 8 ничего не разбирают). Число вне диапазона 1-8 **не проверяется**: цель не создаётся, и последующий вызов `Parse` идёт через нулевой объект. | [L859](../../../src/Content/MissionObject.cpp#L859) |
| `state` | 0 | int или слово | 0 (см. примечание) | Число — код состояния/типа, отправляемый клиенту; слова `ACTIVE` или `HIDDEN` задают код состояния 10 и флаг hidden в false / true. Другие слова дают 10 без изменения флага hidden. Если `state` отсутствует, код остаётся 0 (retail оставляет его неинициализированным). | [L883](../../../src/Content/MissionObject.cpp#L883) |
| `hidden` | 0 | bool | false | Флаг hidden цели. | [L900](../../../src/Content/MissionObject.cpp#L900) |

Значения, зависящие от типа (значение 0 — сам тип):

| `type` | Имя | Значения | Источник |
|---|---|---|---|
| 1 | `base` | 1: nickname базы или число (число, если начинается с цифры или `-`; иначе `pub::GetBaseID`), 2: id в таблице строк | [L495](../../../src/Content/MissionObject.cpp#L495) |
| 2 | `navmarker` | 1: nickname системы или число, 2: id строки, 3: int (сохраняется; роль в исходниках не выяснена), 4,5,6: x,y,z (float) | [L542](../../../src/Content/MissionObject.cpp#L542) |
| 3 | `ids` | 1: id строки | [L588](../../../src/Content/MissionObject.cpp#L588) |
| 4 | `rep_inst` | 1: система, 2: id строки, 3: int, 4,5,6: x,y,z, 7: метка (47 символов) | [L632](../../../src/Content/MissionObject.cpp#L632) |
| 5, 6 | (без имени) | 1: id строки, 2: id системы (число), 3: rep instance (int), 4,5: int (данные маркера), 6,7,8: float. Три float хранятся в обратном порядке (сначала значение 8), поэтому сохранение, записанное игрой, перезагружается с переставленными x и z; это повторяет retail. | [L393](../../../src/Content/MissionObject.cpp#L393) |
| 7, 8 | (без имени) | значения не разбираются | [L730](../../../src/Content/MissionObject.cpp#L730) |

Позиции в типах 2 и 4: `x = значение 4`, `y = значение 5`, `z = значение 6`.

## `[Dialog]`

Скриптовый разговор по связи, запускается `Act_StartDialog`. Читается `FMissionDialog::Load` ([MissionDialogs.cpp:19](../../../src/Content/MissionDialogs.cpp#L19)). **Первая** строка значений секции читается безусловно и должна быть `nickname`; иначе вся секция пропускается ([MissionObject.cpp:4252](../../../src/Content/MissionObject.cpp#L4252)). Повторный nickname диалога сохраняет первый диалог.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` | 0: имя | id | н/д | id диалога (`CreateID`), на него ссылается `Act_StartDialog`. Должен быть первой строкой. | [MissionObject.cpp:4259](../../../src/Content/MissionObject.cpp#L4259) |
| `system` | 0: nickname системы | str | нет | Система, в которой диалог может выполняться. Если игрок в другой системе, `Act_StartDialog` завершается неудачей; без `system` диалог ничего не делает. | [MissionDialogs.cpp:64](../../../src/Content/MissionDialogs.cpp#L64) |
| `line` | 0: метка говорящего, 1: метка адресата, 2: nickname текста связи | str, str, nick | н/д | Одна реплика разговора. Реплики группируются по говорящему (значения 0 и 1 могут быть `Player` или именем/меткой миссии; на момент старта каждый говорящий и адресат должен разрешиться в корабль или solar в системе диалога, иначе `Act_StartDialog` завершается неудачей). Каждая реплика запоминает id связи предыдущей `line` в файле (0 для первой реплики секции); реплика, у которой id предшественника равен 0, — «первая реплика», и последняя посещённая первая реплика (говорящие обходятся во внутреннем порядке говорящих диалога) произносится сразу (через 0.5 с, или 1.0 с, если адресат — корабль игрока), а остальные реплики ставятся в очередь у своих говорящих. | [MissionDialogs.cpp:24](../../../src/Content/MissionDialogs.cpp#L24) |

Проход 2 ([MissionScript.cpp:980](../../../src/Content/MissionScript.cpp#L980)) дополнительно просматривает каждую `line`, у которой значение 1 равно `Player` (без учёта регистра), и регистрирует в миссии костюм говорящего (значение 0), чтобы его можно было показать в окне связи игрока.

## `[MetaBehavior]`

Именованный список «guide»-поведений, который можно выдать NPC (`init_metabehavior =`, `Act_GiveMB`). Читается `LoadMetaBehavior` ([Guides.cpp:166](../../../src/Content/Guides.cpp#L166)).

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` | 0: имя | id | `CreateID("")` | id списка; повторный id сохраняет первый список. | [Guides.cpp:171](../../../src/Content/Guides.cpp#L171) |
| `system` | - | - | - | Распознаётся и игнорируется. | [Guides.cpp:176](../../../src/Content/Guides.cpp#L176) |
| `MB_GotoGuide` | см. ниже | | | Создаёт goto-guide (тип 5). Любое другое имя ключа игнорируется. Имя ключа проверяется через `stricmp`. | [Guides.cpp:181](../../../src/Content/Guides.cpp#L181) |
| `MB_DockGuide` | см. ниже | | | Создаёт dock-guide (тип 6). | [Guides.cpp:186](../../../src/Content/Guides.cpp#L186) |

`MB_GotoGuide` ([Guides.cpp:123](../../../src/Content/Guides.cpp#L123)):

| Индекс | Тип | По умолчанию | Значение |
|---|---|---|---|
| 0 | str (47) | пусто | Имя целевого объекта. |
| 1,2,3 | float | 0 | Первая позиция x,y,z. |
| 4,5,6 | float | 0 | Пункт назначения x,y,z. |
| 7 | int | 0 | Дальность (хранится как float; целочисленный разбор). |
| 8 | bool | true (внутренний флаг = 1) | Читается, только если не пусто. Роль флага в исходниках не выяснена. |
| 9 | str | приоритет 0 | Необязателен. `NORMAL` -> приоритет 1, `ALWAYS_EXECUTE` -> приоритет 0, всё остальное оставляет приоритет 0. |

`MB_DockGuide` ([Guides.cpp:148](../../../src/Content/Guides.cpp#L148)):

| Индекс | Тип | Значение |
|---|---|---|
| 0 | str (47) | Имя целевого объекта. |
| 1,2,3 | float | Первая позиция x,y,z (пункт назначения не записывается). |
| 4 | nick | nickname цели стыковки (`pub::GetNicknameId`). |
| 5 | float | Дальность. |

Guide сохраняет всю исходную строку (`get_line_ptr`), чтобы сохранение могло снова вывести её под `[MetaBehavior]`.

## `[Trigger]`

Движок правил миссии. **Одна секция `[Trigger]` определяет один триггер.** Разбирается `LoadMissionTriggerSection` ([MissionScript.cpp:298](../../../src/Content/MissionScript.cpp#L298)) и `LoadTrigger` ([MissionScript.cpp:238](../../../src/Content/MissionScript.cpp#L238)). Первый `nickname` в секции начинает триггер, а остаток секции потребляет `LoadTrigger`; второй `nickname` в той же секции поэтому игнорируется, как и ключи до `nickname` (кроме `State`, который читается и отбрасывается). Если триггер с таким nickname уже существует, вся секция пропускается.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` | 0: имя | id | н/д | id триггера, используемый `Act_ActTrig`, `Act_DeactTrig`, `Cnd_WatchTrigger`, `Act_Save`. | [MissionScript.cpp:304](../../../src/Content/MissionScript.cpp#L304) |
| `InitState` | 0 | str | `INACTIVE` | `ACTIVE`, `INACTIVE` или `COMPLETE` (без учёта регистра); любое другое слово оставляет состояние `INACTIVE`. Триггеры, которые при загрузке `ACTIVE`, активируются сообщением о старте миссии. | [MissionScript.cpp:275](../../../src/Content/MissionScript.cpp#L275) |
| `repeatable` | 0 | bool | false | `true` снова взводит триггер после срабатывания; `false` эффекта не имеет. | [MissionScript.cpp:286](../../../src/Content/MissionScript.cpp#L286) |
| `system` | - | - | - | Распознаётся и игнорируется. | [MissionScript.cpp:293](../../../src/Content/MissionScript.cpp#L293) |
| `State` (в секции, до `nickname`) | 0 | str | - | Вычисляется `MissionStateFromName`, результат отбрасывается. | [MissionScript.cpp:329](../../../src/Content/MissionScript.cpp#L329) |
| `Cnd_*` | зависит от условия | | | **Условие триггера.** Используется ровно одно: более поздняя строка `Cnd_*` заменяет более раннюю (прежний объект утекает, не удаляется). Триггер без `Cnd_*` не имеет условия: события его никогда не будят, а активация разыменует отсутствующее условие (не защищено, как в retail), поэтому у каждого триггера оно должно быть. Условие решает, какие игровые события будят триггер (heartbeat, уничтожение объекта, ...). | [conditions](missions-conditions.md) |
| `Act_*` | зависит от действия | | | **Действия, выполняемые, когда условие выполнено**, в порядке файла. Любое количество; каждая строка добавляет одно действие. | [actions](missions-actions.md) |

Имена действий и условий сопоставляются через `stricmp` в фиксированном порядке, побеждает первое совпадение (списки в [MissionScript.cpp:86](../../../src/Content/MissionScript.cpp#L86) и [MissionScript.cpp:143](../../../src/Content/MissionScript.cpp#L143)). Ключ, не являющийся ни одним из них и ни одной из записей выше, молча игнорируется. Параметры строки `Cnd_*` или `Act_*` читаются позиционными геттерами `Parse` соответствующей записи; лишние значения игнорируются, отсутствующие читаются как пустые/0.

Жизненный цикл триггера (не настраивается): когда триггер активируется (`InitState = ACTIVE` при старте миссии, `Act_ActTrig`), выполняется `OnActivate` его условия, и условие проверяется один раз без события; истинное условие сразу завершает триггер. После этого условие проверяется на каждое сообщение из своей таблицы событий (heartbeat для опрашиваемых условий, конкретное сообщение для событийных). При совпадении все действия выполняются по порядку, и триггер становится `COMPLETE` (или снова `ACTIVE`, если `repeatable`).

## Связанные файлы

* [missions-conditions.md](missions-conditions.md) - каждая запись `Cnd_*`.
* [missions-actions.md](missions-actions.md) - каждая запись `Act_*`.
* [missions-objlist.md](missions-objlist.md) - директивы `[ObjList]` (`Goto*`, `Follow`, `Dock`, ...).
* [missions-saves.md](missions-saves.md) - секции, которые объекты миссии и сюжета пишут в сохранения / читают из них.
* [missions-support.md](missions-support.md) - RTC-скрипты, общие скрипты бара, голоса, `specific_npc.ini`, таблица стойкости игрока, записи базовых NPC (взятка/знание/слух), названия стадий сюжета.
* [random-missions.md](random-missions.md) - таблицы случайных миссий и генератор виньеток.
* [ai-and-encounters.md](ai-and-encounters.md) - формации, архетипы кораблей NPC, личности пилотов, параметры встреч.
