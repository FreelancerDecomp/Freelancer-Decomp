# Content.dll — игровая логика кампании и мира

`Content.dll` содержит игровую логику поверх `Server.dll`: сюжетные миссии и скрипты, случайные
миссии, население космоса и баз, радиопереговоры NPC (GCS), поведение пилотов, данные баз
(`mbases.ini`), состояние игрока. Это не DACOM-компонент, а набор «контроллеров» Server'а; документ
нужен тому, кто портирует или правит `src/Content`. Построчные факты и статусы — в decomp-tracker
(модуль `Content.dll`, образ `0x06ea0000`; листинг лежит в `DLLS/BIN/Content.dll.{txt,c}`, а не в
`EXE/Disasm/`).

Форматы миссионного скрипта: [content_mission_script.md](content_mission_script.md) (машина
состояний, `[Trigger]`) и [content_mission_opcodes.md](content_mission_opcodes.md) (аргументы
`Cnd_*`/`Act_*`).

## Как модуль загружается

- Нет `RegisterComponent`, нет IID-строк `"1.11_…"`, нет `FDUMP`. Из DACOM импортируются только
  `stricmp` и `DACOM_Acquire` (создание `"3DMathEngine"`). Экспорты (совпадают с retail):
  `CreateInstance`, `DestroyInstance`, класс `IDLL` (конструкторы, деструктор, `operator=`, vftable).
- Загружает **Server.dll**: `IServerImpl::Startup` читает `freelancer.ini` `[Initial SP DLLs]` /
  `[Initial MP DLLs]` (строки `DLL = <файл>, <имя>, <приоритет>`), для каждой зовёт
  `pub::Controller::Create(файл, имя, nullptr, приоритет)` → `LoadLibrary` →
  `GetProcAddress("CreateInstance")` → `CreateInstance(controllerId, имя, parms)`. Content сам зовёт
  `Create("Content.dll", …)` для `Story` и миссий (путь модуля — `GetModuleFileNameA`).
- `CreateInstance` — цепочка `stricmp` по **23 именам**; неизвестное имя даёт `0`, и Server делает
  `DestroyInstance(0)` + `FreeLibrary`. Так ведут себя `AISandbox` и `TestAutomation` из retail
  `freelancer.ini`: этих имён нет ни в одном retail-модуле (мёртвые строки), реализовывать нечего.
- Объект контроллера наследует `IDLL`: слот 0 — `HandleMessage(msg, payload)` (чистая виртуальная в
  базе), слот 1 — деструктор (`DestroyInstance` зовёт его с `1`). `controllerId` — 30-битный счётчик.
- Приоритет определяет порядок в очереди контроллеров: `HIGHEST=0 … LOWEST=4`. Частоту тика задаёт
  `pub::Controller::SetHeartbeatInterval` (секунды): сообщение тика — `0x20`; интервал `< 0`
  (по умолчанию `-1`) тик отключает, интервал `> 0` накапливает `dt` и шлёт `0x20` с
  `float* dt` в payload.

## Контроллеры

| Имя | SP/MP | Приоритет (ini) | Heartbeat | Роль |
|---|---|---|---|---|
| `GameSupport` | оба | HIGHEST | 1.0 с | Загружает все таблицы данных (см. ниже), состояние игроков, статистика, сохранение/загрузка `[mPlayer]`, смена систем, лучшие пути |
| `SpaceSupport` | оба | NORMAL | 1.0 с | Ворота, доки, очереди стыковки, создание/снаряжение кораблей, tradelane, репутация за бой |
| `BaseSupport` | оба | NORMAL | нет | Базы: GCS-скрипты барменов/торговцев (`scripts\gcs\genericScripts.ini`), новости, выбор объектов GF |
| `SpacePop` | оба | LOWEST | 3.0 с | Популятор космоса (энкаунтеры) |
| `BasePop` | оба | LOWEST | нет | Популятор баз (NPC комнат) |
| `StoryLauncher` | только SP | BELOW_NORMAL | нет | Читает `missions\specific_npc.ini`; на сообщение `7` создаёт `Story` |
| `Story` | SP | (создаётся кодом, NORMAL) | 5.0 с | Кампания: `[StoryInfo]`, номер главы, запуск `Mission_*` |
| `Mission_01a`, `Mission_01b`, `Mission_02` … `Mission_13` | SP | — | задаёт конструктор (у `01a` 0.033 с) | Сюжетные миссии: каждая — свой класс, конструктор и размер объекта (0x440 для 01a, 0x330 для 01b, 0x32C для остальных) |
| `GenericMission` | SP | — | — | Сюжетная миссия из произвольного INI (путь — `CreateParms+0xC`) |
| `DestroyMission` | оба | — | 1.0 с | Случайная миссия «уничтожить» (оффер на базе; 0x29C байт) |

В `[Initial SP DLLs]` / `[Initial MP DLLs]` retail кроме перечисленных стоят ещё мёртвые
`AISandbox` и `TestAutomation` (см. выше); `Story`, `Mission_*`, `GenericMission` и `DestroyMission`
в списках не значатся и создаются только по требованию. Литерал `"Mission_01a"` встречается в
Content один раз — в `CreateInstance`: первое `Mission = Mission_01a` берётся из шаблона
`NewPlayer.fl` (см. ниже).

Помимо слота 0 Content отдаёт Server/Common наследников их классов — виртуальные вызовы идут
**из** Common/Server в Content: `StateMachine`/`MessageHandler`/`SMEventData` (случайные миссии,
GCS), `CommReferrable`, `pub::AI::BaseOp` и `BaseHint` (директивы и гиды), `pub::AI::Personality`,
`pub::Reputation::Enumerator`, `pub::System::ZoneEnumerator`. Поэтому «нет вызывающих внутри
Content» ничего не доказывает.

## Сообщения Server → Content

Широковещательная рассылка: `controllerId & 0x3FFFFFFF == 0` — всем загруженным контроллерам.
Номера из Server (`IServerImpl::*`), там, где источник известен:

| msg | Событие | Обрабатывают |
|---:|---|---|
| 0 | экипировка игрока | GameSupport |
| 1 / 2 | `BaseEnter` / `BaseExit` | GameSupport, BaseSupport |
| 4 / 5 | сохранение `[mPlayer]` в `FILE*` / загрузка из `INI_Reader*` (payload `{playerId, FILE*|INI_Reader*}`) | GameSupport, Story, миссии |
| 6 | `PlayerLaunch` | миссии |
| 7 | логин игрока (`Login`, смена персонажа, MP-логаут) | StoryLauncher |
| 8 | MP-логаут | GameSupport |
| 0xB | `CommComplete` | SpaceSupport |
| 0x16 / 0x17 | вход в tradelane | GameSupport, SpacePop (0x16) |
| 0x1B | `GFObjSelect` | BaseSupport |
| 0x20 | heartbeat (`float dt`) | все с интервалом `> 0` |
| 0x25 | назначение ворот/прыжка | GameSupport, SpaceSupport |
| 0x29 / 0x2A | `LocationEnter` / `LocationExit` (payload `{locationId, playerId}`) | GameSupport, BaseSupport, BasePop |
| 0x31 | `RequestBestPath` | GameSupport |
| 0x32 / 0x36 | терминал/док; хвост `PlayerLaunch` | SpaceSupport, SpacePop (0x36) |
| 0x33 | `RequestPlayerStats` | GameSupport |
| 0x37 | обнуление `Players[id]` | GameSupport |
| 0x39 | создание объекта (корабль/экипировка) | GameSupport, SpaceSupport |
| 0x3A / 0x3B | смена системы | GameSupport; 0x3B — SpacePop |
| 0x40 / 0x41 | загрузка/сохранение сейва после `Reputation` | GameSupport |

Сообщения `0x1000` и выше — внутренние сообщения Content между контроллерами (`_SendMessage` на
конкретный id): `0x1000` — активация начальных триггеров миссии, `0x1001` — боевое состояние
(`CSNType`), `0x100E` — «корабль в прицеле», `0x1010`, `0x100D` и др. Часть сообщений ни один из
контроллеров не обрабатывает.

## Сюжет и кампания

- `StoryLauncher` читает `[NPC]` из `missions\specific_npc.ini` (именованные сюжетные NPC) и на
  сообщение `7` вызывает `pub::Controller::Create(<путь Content.dll>, "Story", {playerId}, NORMAL)`.
  В MP `StoryLauncher` нет.
- `Story` хранит имя миссии (по умолчанию `"No_Mission"`), номер главы `MissionNum` (0…42),
  `delta_worth` (богатство, нужное для следующей главы), `ship_bought`. Формат сейва:

  ```
  [StoryInfo]
  Mission = <Mission_NN | GenericMission [путь]>
  MissionNum = <n>
  delta_worth = <число>
  ship_bought = <0|1>
  ```

  Загрузка (сообщение 5): создаёт контроллер из поля `Mission` и шлёт ему сообщения `5`, `0x1000`.
- Первая миссия нового SP-сейва: шаблон `EXE\NewPlayer.fl` (шифрованный `FLS1`) содержит
  `[StoryInfo] Mission = Mission_01a, MissionNum = 1, delta_worth = -1`; `Server.dll`
  (`PlayerDB::create_restart_file`) копирует его в новый сейв. `InitialWorld.ini` к сюжету отношения
  не имеет.
- Автоматический запуск следующей главы (heartbeat `Story`): при `delta_worth ≤ богатство` и без
  живой миссии номера 5, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39 запускают соответственно
  `Mission_02` … `Mission_13`. Остальные переходы делает обработчик сообщений `Story` по коду
  завершения миссии (`01a`→`01b` сразу; `M06→M07` и `M09→M13` частично сразу, остальные ждут
  деньги); `MissionNum = 42` — кампания окончена.

Подробности машины состояний миссии — [content_mission_script.md](content_mission_script.md).

## Случайные миссии и популяторы

- `DestroyMission` создаётся контроллером базы при принятии оффера (имя контроллера берётся из
  объекта оффера) или из секции `[RandomMission]` сейва; имя типа берётся из `mbases.ini`
  (`mission_type = DestroyMission`).
  Она не наследует сюжетную машину триггеров: общий предок — только базовый контроллер миссии
  (слот 0: уничтожить контроллер по флагу), дальше — `StateMachineGroup`, генератор
  (дерево `DecisionNode`/`DataNode`, «виньетки»), runtime-автоматы и параметры встреч
  (`SPParams`/`TLParams`).
- **SpacePop** обрабатывает только `0x16` (tradelane), `0x20` (heartbeat), `0x36` (запуск игрока),
  `0x3B` (смена системы). Тик перебирает живые системы/зоны; для каждой зоны с загруженным
  `EncounterParameters` и ещё не «насыщенной» строит формацию: отбор зон по трём кольцам (10 м,
  2000 м, 4000 м) и три прохода спавна. Флаг «популяция включена» по умолчанию `true`.
- Данные встреч: в `universe\systems\<sys>\<sys>.ini` секция `[EncounterParameters]`
  (`nickname`, `filename = missions\encounters\*.ini`); файл содержит `[EncounterFormation]`
  (`ship_by_class`, `pilot_job`, `formation_by_class`, `behavior`, `arrival`, `permutation`) и
  `[Creation]`. Имена классов кораблей/пилотов/формаций разрешаются в таблицах, которые грузит
  GameSupport.
- **BasePop** на сообщение `0x29` выбирает NPC комнаты: из имён GF-размещений вида `zg\npc\m\a\…`
  и пула фракции в `mbases.ini` собирает персонажей (роли `trader`, `shipdealer`, `equipment`,
  `bartender`), на `0x2A` разрушает их.

## Данные GameSupport

GameSupport при создании читает (пути относительно `pub::GetDataPath()`): `audio\voices_space_*.ini`,
`audio\voices_recognizable.ini`, `missions\empathy.ini`, `voice_properties.ini`, `formations.ini`
(`[Formation]`), `pilots_population.ini`, `pilots_story.ini`, `npcships.ini`, `npcships_test.ini`
(всегда), `faction_prop.ini`, `equipment\commodities_per_faction.ini`, `shipclasses.ini`,
`ships\shiparch.ini`, `lootprops.ini`, `scripts\GCS\GCSExcl.ini`, `PTough.ini`,
`RankDiff.ini`, `news.ini`; `mbases.ini` (`[MBase]`, `[BaseFaction]`, NPC баров, офферы) —
отдельный проход. Сохранение игрока (`[mPlayer]`): `locked_gate`, `vnpc`, `rumor`, `can_dock`,
`dock_exception`, `can_tl`, `tlr_exception` и статистика; `[RandomMission]` пересоздаёт контроллер;
`[locked_gates]` (`locked_gate`, `npc_locked_gate`) пишет SpaceSupport. `DynBaseInfo`/`DynSysInfo`
читаются при загрузке (`0x40`).

## Контракт AI: `pub::AI::ContentCallback`

Content не импортирует `IBehaviorManager::set_content_callback`: его вызывает Server из
`pub::AI::SubmitState` (тип 0: граф состояний, `DirectiveCallback`, `ContentCallback`, `Personality`).
Два AI-класса Content наследуют `ContentCallback` (по десяти слотам: `follow_event`,
`tether_object_event`, `lead_object_event`, `ship_in_sights_event`, `request_flee_destination`,
`information_request`, `scan_state_change`, `combat_state_notification`, `combat_drift_event`,
`get_creation_type`): объект системного `[Object]` (переопределяет только
`combat_state_notification` → сообщение `0x1001`) и корабль энкаунтера (переопределяет девять слотов;
`get_creation_type` читает поле объекта). Остальные слоты — no-op Common (`get_creation_type` = 3).
Директивы строит фабрика по типу (`Idle`, `Delay`, `Goto`, `Trail`, `Follow`, `Guide`, `Face`, `Dock`,
`InstantTradelane`, `Launch`, `WaitForPlayerManeuver`, `TractorBeam`, `Cancel`), личности пилотов
читаются из `pilots_*.ini` (`Personality` и вложенные `*UseStruct`/`*ReactionStruct`/`JobStruct`).

## Карта единиц трансляции

В retail Content собран из 65 `.obj`; их границы (по общему заголовку со статическими ID
`CreateID("any")`, `"house_fighters"`, …) стали номерами «TU» в комментариях исходников. Хвост
`06fa9000`–`06fb2221` — CRT, `__ehhandler`'ы и EH-funclet'ы (статус следует за родителем).

| TU | Адреса | Подсистема | `src/Content/` |
|---|---|---|---|
| 0–14 | `06ea1000`–`06ea5c10` | `GenericMission`, `Mission_01a` … `Mission_13` | `StoryMissions.cpp`, `ContentStaticIds.h` |
| 15, 16 | `06ea5c10`–`06ea7750` | `Story`, `StoryLauncher` | `Story.cpp`, `StoryLauncher.cpp` |
| 17, 22 | `06ea7750`–`06eb2670`, `06ed3d90`–`06ed4220` | база `mbases.ini`, GF_NPC-записи, ранги | `BaseDB.*`, `BaseNpcRecords.cpp` |
| 18, 52 | `06eb2670`–`06eb3ec0`, `06f5a2e0`–`06f5c760` | `BasePop`, `SpacePop` | `Populators.*` |
| 19 | `06eb3ec0`–`06eb6c00` | `BaseSupport`, новости | `BaseSupport.*` |
| 20 | `06eb6c00`–`06eb7360` | база космических объектов | `NpcBase.*` |
| 21 | `06eb7360`–`06ed3d90` | движок триггеров миссий (`[Trigger]`, `Cnd_*`/`Act_*`) | `MissionScript.*`, `MissionCnd.*`, `MissionAct.*` |
| 23 | `06ed4220`–`06ed5390` | `[CharacterEncounter]`, RTC-сцены | `RtcScripts.*` |
| 24 | `06ed5390`–`06ed5a70` | солнечные NPC (`[Object]`) | `NpcCreate.*` |
| 25 | `06ed5a70`–`06eeea30` | GCS (радиопереговоры) | `Gcs.*` |
| 26 | `06eeea30`–`06eef190` | `CreateInstance`/`DestroyInstance` | `Content.cpp` |
| 27 | `06eef190`–`06ef2070` | `DestroyMission` | `DestroyMission.cpp` |
| 28 | `06ef2070`–`06ef3630` | диалоги миссий | `MissionDialogs.*` |
| 29, 30, 32 | `06ef3630`–`06efb350`, `06efe100`–`06efe8e0` | параметры встреч, `formations.ini` | `EncounterParams.*`, `Encounters.*` |
| 31, 59, 60, 62 | `06efb350`–`06efe100`, `06f802b0`–`06f84060`, `06f94d10`–`06f97360` | `SPParams`/`TLParams`, `SP_Attack`/`TL_Attack`, `KillableSolar` | `RandomEncounters.*` |
| 33 | `06efe8e0`–`06f00670` | `GameSupport` | `GameSupport.cpp` |
| 34 | `06f00670`–`06f03fa0` | GenericScripts, `GCS_Exclusions` | `GenericScripts.*` |
| 35 | `06f03fa0`–`06f056d0` | свойства лута | `LootProps.*` |
| 36 | `06f056d0`–`06f05d00` | маршрутизатор `StateMachine`-сообщений | `ContentMessageRouter.*` |
| 37 | `06f05d00`–`06f0e000` | гиды (`MB_DOCKGUIDE`, `goto`, `patrol_path`, …) | `Guides.*`, `GotoGuides.cpp`, `PatrolGuides.cpp` |
| 38 | `06f0e000`–`06f24080` | объект миссии: `HandleMessage`, `ParseMissionIni`, сохранение | `MissionObject.*`, `MissionSave.cpp` |
| 39 | `06f24080`–`06f24670` | путь данных (`pub::GetDataPath`) | `ContentDataPath.cpp` |
| 40 | `06f24670`–`06f28f60` | директивы `[ObjList]` | `MissionObjList.*`, `ObjListDirective.h` |
| 41 | `06f28f60`–`06f31190` | граф систем, пути прыжков | `SystemConnections.*` |
| 42, 43 | `06f31190`–`06f34ef0` | `npcships.ini`, имена NPC, NPC-корабль | `NpcShips.*`, `NpcNames.*`, `NpcShip.*` |
| 44 | `06f34ef0`–`06f36f10` | груз, товары фракций | `Cargo.*` |
| 45 | `06f36f10`–`06f46940` | личности пилотов, пути патруля | `PilotPersonalities.*`, `PatrolPaths.*` |
| 46 | `06f46940`–`06f4a780` | состояние игрока | `PlayerState.cpp`, `ContentPlayerState.h` |
| 47, 49, 51, 63 | `06f4a780`–`06f4ed80`, `06f55e20`–`06f56f50`, `06f57cb0`–`06f5a2e0`, `06f97360`–`06fa75c0` | RankDiff, бонусный лут, классы кораблей, генератор случайных миссий | `RandomMission*.*` |
| 48 | `06f4ed80`–`06f55e20` | свойства фракций | `FactionProps.*` |
| 50 | `06f56f50`–`06f57cb0` | база NPC-корабля (директивы, урон) | `BehaviorCmds.*` |
| 54 | `06f6b840`–`06f6fa90` | контроль доступа/стыковки (`dock_disallowed`) | `DockQueues.*` |
| 53 | `06f5c760`–`06f6b840` | `SpaceSupport`, очереди стыковки, спавн, голоса | `SpaceSupport.*`, `DockQueues.*`, `SpawnPlacement.cpp`, `SpacePaths.*`, `Voices.*` |
| 55 | `06f6fa90`–`06f6fd90` | запросы tradelane | `TradelaneRequests.*` |
| 56 | `06f6fd90`–`06f70dd0` | репутация за бой | `CombatReputation.*` |
| 57, 61 | `06f70dd0`–`06f71fc0`, `06f84060`–`06f94d10` | автоматы и runtime случайных миссий | `RandomMissionRuntime/Machine/Triggers.*` |
| 58, 64 | `06f71fc0`–`06f802b0`, `06fa75c0`–`06fa9000` | `SystemInfo`, зоны, общие STL-хелперы | `SystemInfo.*`, `Zones.cpp` |

Общие STL-инстанцирования (TU64, узлы карт TU17) в порте — обычные контейнеры в месте
использования, а не отдельные функции.

## Правила работы с портом

- Один retail-класс — один класс порта; общих баз, которых нет в retail, не заводим; VC6-раскладки
  STL не воспроизводим. Заголовки базовых классов `StateMachine`/`BaseOp`/`Personality` — только из
  `src/include/common*.h` и `server.h`; публичный ABI модуля — `src/include/content.h`.
- Разбор данных — через `INI_Reader` и `pub::GetDataPath`, как в retail (собственных `fopen`-парсеров
  и порт-логов в модуле быть не должно).
- Статус и дыры по функциям — в decomp-tracker; нарушения этих правил, которые ещё живы, записаны в
  [deviations.md](deviations.md).
