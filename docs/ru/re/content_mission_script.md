# Сюжетная машина миссий (`[Trigger]`, `Cnd_*`, `Act_*`)

Сюжетные миссии кампании (`Mission_01a` … `Mission_13`, `GenericMission`) — INI-скрипты
`DATA\MISSIONS\M01A\m01a.ini` … `M13\m13.ini`, которые исполняет один и тот же движок триггеров.
Документ описывает формат скрипта и устройство движка (TU21/TU38 в [content.md](content.md)); аргументы
каждого опкода — в [content_mission_opcodes.md](content_mission_opcodes.md). Код:
`src/Content/MissionScript.*`, `MissionCnd.*`, `MissionAct.*`, `MissionObject.*`, `MissionSave.cpp`,
`MissionDialogs.*`, `RtcScripts.*`.

## Контроллер миссии

Класс миссии — наследник `IDLL` с 31-слотовой таблицей: слот 0 — приём сообщения контроллера (если
флаги завершения выставлены, зовёт `pub::Controller::Destroy`), слот 1 — деструктор, 2–6 — поиск и
создание объектов миссии по метке/id, 18 — привязка игрока, 21 — **диспетчер сообщений миссии**
(heartbeat, сохранение `4`, загрузка `5`, `0x1000` — активация начальных триггеров, события игрока),
25 — разбор INI, 26 — разбор токена `State`, 23/24 — сообщения об успехе/провале
(`pub::Player::DisplayMissionMessage`), 28 — рассылка `0x100F` по триггерам, 30 — слежение за
состоянием миссии и колбэк в `Story`. Потомки (`Mission_*`) отличаются деструктором и, у `01a`/`01b`,
обработчиком сообщений слота 21. Каждый конструктор открывает свой INI (`Missions\M0N\m0N.ini`,
`GenericMission` — путь из `CreateParms`) через `pub::GetDataPath` + `INI_Reader` и зовёт разбор.

`DestroyMission` (случайная миссия) делит с сюжетными миссиями только базовый контроллер
(слот 0, карты объектов); триггеров у неё нет — у неё `StateMachineGroup` и генератор.

## Разбор INI

Порядок секций в `m0N.ini` обрабатывается так:

1. секции объектов мира (в базе `MissionObject`): `[MsnLoot]`, `[MsnSolar]`, `[MsnShip]` /
   `[MsnFormation]`, `[MsnRandEnc]`, `[Mission]` (`mission_title`, `mission_offer`, `reward`,
   `npc_ship_file`), `[ObjList]`, `[MetaBehavior]`, `[NNObjective]`, `[Dialog]`;
2. `[Trigger]` — триггеры;
3. `[NPC]` — именованные NPC миссии;
4. `[Dialog]` — реплики диалога (`line = …`, говорящий `Player` — игрок).

Загрузка сейва (сообщение 5) дополнительно читает `[MissionState]` (`rtc`, `ambi_scene`),
`[TriggerSave]`, `[NNObjective]`, `[BStoryMissionDone]`.

### Секция `[Trigger]`

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

- Ключа `cond` нет: условие — ключ `Cnd_*` (одно на триггер), действия — ключи `Act_*` (любое число,
  в порядке записи). Остальные ключи: `nickname` (id = `CreateID`), `system`, `repeatable`,
  `InitState`. Имена ключей сравниваются без учёта регистра (`Act_Ethercomm` ≡ `Act_EtherComm`);
  неизвестные ключи молча пропускаются.
- Триггеры лежат в карте по id; активные — в списке. Состояние триггера: `ACTIVE`, `INACTIVE`
  (по умолчанию), `COMPLETE`. Сообщение `0x1000` активирует триггеры с `InitState = ACTIVE`;
  `Act_ActTrig`/`Act_DeactTrig` включают и выключают триггер по nickname; `Cnd_WatchTrigger`
  ждёт чужой триггер.
- Условие подписывается на набор id сообщений диспетчера (например `Cnd_Destroyed` — на уничтожение
  объекта). Когда `Matches` истинен, исполняется список `Act_*`; повторяемость задаёт ключ
  `repeatable`. Отложенные действия (`Act_ActTrig` с задержкой, `Cnd_Timer`) тикают по сообщению `0x20`.
- Значение `State` у `Act_ChangeState` — токен `FAIL`, `SUCCEED` или `NO_STATE`
  (числа 0, 1, 2; прочее — `-1`). Он меняет `MissionStateNum`; слот 30 по значению показывает
  сообщение об успехе/провале и сообщает `Story` код завершения.
- Опкодов 44 `Cnd_*` и 85 `Act_*`; каждый — отдельный класс (`MissionCnd.*`, `MissionAct.*`) с
  разбором строки (`Parse`) и исполнением.

## Номера глав

`MissionNum` (поле `Story`, 0…42): конструктор миссии записывает два числа — текущий номер и «код
завершения», который `Story` получает при успехе:

| Миссия | Номер | Код | Миссия | Номер | Код |
|---|---:|---:|---|---:|---:|
| `Mission_01a` | 1 | 2 | `Mission_07` | 22 | 23 |
| `Mission_01b` | 4 | 4 | `Mission_08` | 25 | 26 |
| `Mission_02` | 7 | 8 | `Mission_09` | 28 | 29 |
| `Mission_03` | 10 | 11 | `Mission_10` | 31 | 32 |
| `Mission_04` | 13 | 14 | `Mission_11` | 34 | 35 |
| `Mission_05` | 16 | 17 | `Mission_12` | 37 | 38 |
| `Mission_06` | 19 | 20 | `Mission_13` | 40 | 41 |

Обработчик сообщений `Story` по коду завершения выбирает продолжение: код 2 (`01a`) → главы 3 и 4
и немедленный запуск `Mission_01b`; код 4 (`01b`) → «ждать деньги» (глава 5); коды 8, 11, 14, 17, 26 →
следующий номер (9, 12, 15, 18, 27) в ожидании денег; коды 20, 29, 32, 35, 38 → следующая глава
и сразу запуск `Mission_07`, `10`, `11`, `12`, `13` соответственно; код 23 → глава 24 (ждать);
код 41 → глава 42 (кампания окончена). «Деньги» — порог `delta_worth` из таблицы по индексу номера
(индексы 5, 9, 12, 15, 18, 24, 27 — 3500, 5200, 7000, 10000, 16000, 36000, 30000; индексы 21, 30,
33, 36, 39, 42 — 0); heartbeat `Story` запускает `Mission_02` … `Mission_13` из «денежных» номеров
5, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39 (см. [content.md](content.md)).
Внутри одной миссии «стадии» скрипта — это nickname триггеров и `MissionStateNum`, а не значения
`MissionNum`.
