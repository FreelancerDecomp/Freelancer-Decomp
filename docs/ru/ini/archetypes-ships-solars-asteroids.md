# Архетипы кораблей, solar-объектов и астероидов, группы столкновений, поля астероидов

Эта страница описывает секции `[Ship]`, `[Solar]`, `[Asteroid]`, `[AsteroidMine]`, `[DynamicAsteroid]` и `[CollisionGroup]` файлов архетипов, секцию `[Object]` системных файлов, читаемую через архетипы solar (`CSolar::ReadObj`), проход репутации объектов по тем же секциям и файлы полей астероидов, читаемые Common.dll и сервером. Ключи каждой записи (`nickname`, `type`, `hit_pts`, ...) и нотация значений описаны в [archetypes-and-equipment.md](archetypes-and-equipment.md); имена типов хардпойнтов — в [archetypes-weapons.md](archetypes-weapons.md#названия-типов-hardpoint).

**Расположение файла / как он находится.** Ключи `[Data]` файла `freelancer.ini` ([freelancer-ini.md](freelancer-ini.md)): `ships` (dev ini: `ships\shiparch.ini` и `ships\rtc_shiparch.ini`) -> `Archetype::LoadShips`; `solar` (`solar\solararch.ini`) -> `LoadSolar`; `asteroids` (`solar\asteroidarch.ini`) -> `LoadAsteroids`. Файлы полей астероидов называются ключом `[Asteroids] file =` каждого системного файла ([systems-and-bases.md](systems-and-bases.md)). Системные секции `[Object]` входят в файлы отдельных систем, перечисленные в `universe.ini`.

Поскольку `inherit` и `CollisionGroup` действуют на только что прочитанные записи, порядок секций внутри файла важен: донор должен быть определён раньше записи, которая его наследует, а `[CollisionGroup]` относится к ближайшему предыдущему `[Ship]` / `[Solar]`.

## Ключи, общие для `Ship` и `Solar` (`EqObj::read`)

Применяются после ключей `Root` ([common_archetype_ships.cpp:720](../../../src/Common/common_archetype_ships.cpp#L720)).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `docking_camera` | bool | true | Поведение камеры при стыковке. | [L725](../../../src/Common/common_archetype_ships.cpp#L725) |
| `docking_sphere` | `[0]` имя типа, `[1]` хардпойнт, `[2]` float радиус, необязательный `[3]` анимация | нет | Одна сфера стыковки на строку; повторяющиеся ключи **добавляются**. `[0]`: `berth` (1), `moor_small` (3), `moor_medium` (4), `moor_large` (5), `ring` (6), `pad` (6), `jump` (7), `airlock` (8), без учёта регистра; любое другое имя даёт тип 0 (без сообщения). `[1]` при чтении приводится к нижнему регистру. `[3]` — имя скрипта анимации стыковки (сохраняется, только если присутствует). | [L731](../../../src/Common/common_archetype_ships.cpp#L731) |
| `fuse` | `[0]` nickname fuse (id), `[1]` float, `[2]` float | нет | Fuse повреждений для всего объекта; повторяющиеся ключи **добавляются**. В `Server.dll` fuse зажигается (с `[1]` как аргументом времени старта), когда очки прочности объекта не выше `[2]` и когда повреждение заставляет очки прочности пересечь `[2]` ([server_gameobject.cpp:1193](../../../src/Server/server_gameobject.cpp#L1193), [:1455](../../../src/Server/server_gameobject.cpp#L1455)); то есть `[2]` — порог очков прочности, а `[1]` — время старта. Клиентский reader называет эти два float наоборот ([client_equip_fx.cpp:3410](../../../src/freelancer/Client/client_equip_fx.cpp#L3410)); два модуля порта расходятся, и здесь это не было решено по retail-бинарнику. Файлы fuse: см. [server.md](server.md#файлы-fuse). | [L757](../../../src/Common/common_archetype_ships.cpp#L757) |
| `shield_link` | `[0]` архетип щита (id), `[1]` хардпойнт щита, `[2..n]` хардпойнты генераторов | нет | Связывает щит с хардпойнтами его генераторов; повторяющиеся ключи **добавляются**. Каждая строка получает следующий id связи, считая от 0xfff0. Параметры с `[2]` собираются до первого пустого. | [L772](../../../src/Common/common_archetype_ships.cpp#L772) |
| `nomad` | bool | false | Корабль — Nomad (используется кодом AI/визуализации). | [L797](../../../src/Common/common_archetype_ships.cpp#L797) |

Пример (синтетический):

```ini
[Ship]
nickname = demo_fighter
ids_name = 1100
ship_class = 0
mass = 40
hold_size = 50
steering_torque = 7000, 7000, 8000
angular_drag = 2, 2, 2
hp_type = hp_gun, HpWeapon01, HpWeapon02
hp_type = hp_fighter_shield_generator, HpShield01
shield_link = demo_shield_01, HpShield01, HpShield01
docking_sphere = berth, HpDockMountA, 2, Sc_open dock A
```

## `[Ship]`  ([common_archetype_ships.cpp:1003](../../../src/Common/common_archetype_ships.cpp#L1003))

Ключи `Root` + `EqObj` плюс следующие. Корабль также несёт часть *CommReferrable*.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `inherit` | nickname | - | Обрабатывается загрузчиком до `read()`; копирует корабль-донор, см. ниже. | [loader L298](../../../src/Common/common_archetypeloader.cpp#L298) |
| `bay_door_anim` | cstr (всё значение) | пусто | Скрипт анимации двери отсека. | [L1011](../../../src/Common/common_archetype_ships.cpp#L1011) |
| `HP_bay_surface` | cstr (всё значение) | пусто | Хардпойнт на поверхности отсека. | [L1017](../../../src/Common/common_archetype_ships.cpp#L1017) |
| `HP_bay_external` | cstr (всё значение) | пусто | Внешний хардпойнт отсека. | [L1023](../../../src/Common/common_archetype_ships.cpp#L1023) |
| `HP_tractor_source` | cstr (всё значение) | `HpHeadlight` | Хардпойнт начала луча трактора. | [L1029](../../../src/Common/common_archetype_ships.cpp#L1029) |
| `num_exhaust_nozzles` | int | 9 | Число сопел выхлопа двигателя; максимум (`Ship::MAX_EXHAUST_NOZZLES` = 9) здесь не навязывается. | [L1035](../../../src/Common/common_archetype_ships.cpp#L1035) |
| `hold_size` | float | 0.0 | Вместимость трюма. | [L1041](../../../src/Common/common_archetype_ships.cpp#L1041) |
| `linear_drag` | float | 100.0 | Линейное сопротивление. | [L1047](../../../src/Common/common_archetype_ships.cpp#L1047) |
| `angular_drag` | float x3 (`[0]`,`[1]`,`[2]`) | 0.75, 0.75, 0.75 | Угловое сопротивление по X, Y, Z. | [L1053](../../../src/Common/common_archetype_ships.cpp#L1053) |
| `steering_torque` | float x3 | 2000, 2000, 2000 | Момент управления по X, Y, Z. | [L1061](../../../src/Common/common_archetype_ships.cpp#L1061) |
| `nudge_force` | float | 0.0 | Сила бокового толчка («nudge»). | [L1069](../../../src/Common/common_archetype_ships.cpp#L1069) |
| `strafe_force` | float | 0.0 | Сила страйфа. | [L1075](../../../src/Common/common_archetype_ships.cpp#L1075) |
| `strafe_power_usage` | float | 0.0 | Расход энергии на страйф. | [L1081](../../../src/Common/common_archetype_ships.cpp#L1081) |
| `ids_info1`, `ids_info2`, `ids_info3` | int | 0 | Дополнительные id строк infocard. | [L1087](../../../src/Common/common_archetype_ships.cpp#L1087), [L1093](../../../src/Common/common_archetype_ships.cpp#L1093), [L1099](../../../src/Common/common_archetype_ships.cpp#L1099) |
| `ship_class` | int | 0 | Номер класса корабля; не проверяется. | [L1105](../../../src/Common/common_archetype_ships.cpp#L1105) |
| `nanobot_limit` | int | -1 | Максимальное число наноботов; -1 = не задано. | [L1111](../../../src/Common/common_archetype_ships.cpp#L1111) |
| `shield_battery_limit` | int | -1 | Максимальное число щитовых батарей; -1 = не задано. | [L1117](../../../src/Common/common_archetype_ships.cpp#L1117) |
| `max_bank_angle` | float, градусы | 0.0 | Значения выше 180 ограничиваются до 180, затем переводятся в радианы. Отрицательные значения не ограничиваются. | [L1123](../../../src/Common/common_archetype_ships.cpp#L1123) |
| `msg_id_prefix` | str (всё значение, хранится в 64-символьной строке) | пусто | Префикс id comm-сообщений этого корабля (`CommReferrable`). | [common_watchable.cpp:75](../../../src/Common/common_watchable.cpp#L75) |
| `hp_type` | `[0]` имя типа хардпойнта, `[1..n]` имена хардпойнтов | нет | Объявляет, какие хардпойнты модели принимают оборудование этого типа («допустимые хардпойнты»). Одна запись на строку; повторяющиеся ключи добавляют новые записи. `[0]` должно быть в таблице типов хардпойнтов; неизвестное имя (или с значением 0) молча поглощает строку. Имена с `[1]` читаются до первого пустого параметра. | [L1151](../../../src/Common/common_archetype_ships.cpp#L1151) |

### `inherit` корабля

`inherit = <nickname корабля>` ([common_archetypeloader.cpp:294](../../../src/Common/common_archetypeloader.cpp#L294)): донор ищется (по хешу значения) среди уже загруженных кораблей; промах пишет `ArchDB::Get(...) failed` и предупреждение "'inherit' failed. couldn't find %s", и строка игнорируется. При попадании читаемая запись перезаписывается: всеми полями `Root`, сферами стыковки, клоном групп столкновений донора, `msg_id_prefix`, `bay_door_anim`, `HP_bay_surface`, `HP_bay_external`, `HP_tractor_source`, `ids_info1..3`, `ship_class`, `num_exhaust_nozzles`, `hold_size`, `linear_drag`, `angular_drag`, `steering_torque`, `nudge_force`, `strafe_force`, `strafe_power_usage`, `max_bank_angle`, списком `hp_type`, `nanobot_limit`, `shield_battery_limit`. Собственный nickname записи сохраняется. **Не копируются:** `fuse`, `shield_link`, `nomad`, `docking_camera`. Всё, прочитанное до строки `inherit`, теряется; ключи после неё переопределяют.

## `[Solar]`  ([common_archetype_ships.cpp:892](../../../src/Common/common_archetype_ships.cpp#L892))

Ключи `Root` + `EqObj` плюс:

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `inherit` | nickname | - | На уровне загрузчика, как у кораблей, но среди solar; отсутствующий донор молча игнорируется. Копирует поля `Root`, сферы стыковки, группы столкновений, `destructible`, `solar_radius`, `toughness`, `open_anim`, `jump_out_hp`, `loadout`. | [loader L365](../../../src/Common/common_archetypeloader.cpp#L365) |
| `toughness` | int | 0 | Ранг прочности (используется также как ранг репутации solar-объектов). | [L900](../../../src/Common/common_archetype_ships.cpp#L900) |
| `destructible` | bool | false | Solar можно уничтожить. | [L906](../../../src/Common/common_archetype_ships.cpp#L906) |
| `open_anim` | cstr (всё значение) | пусто | Скрипт анимации, открывающий solar. | [L912](../../../src/Common/common_archetype_ships.cpp#L912) |
| `loadout` | id (всё значение) | 0 | nickname `[Loadout]` по умолчанию для экземпляров этого solar (см. [goods-loadouts-constants.md](goods-loadouts-constants.md)). | [L918](../../../src/Common/common_archetype_ships.cpp#L918) |
| `jump_out_hp` | cstr (всё значение) | пусто | Хардпойнт, из которого выпрыгивают корабли. | [L924](../../../src/Common/common_archetype_ships.cpp#L924) |
| `solar_radius` | float | 500.0 | Радиус для проверок размещения/атмосферы. | [L930](../../../src/Common/common_archetype_ships.cpp#L930) |

## `[Asteroid]`, `[AsteroidMine]`, `[DynamicAsteroid]`  ([common_archetype_misc.cpp:39](../../../src/Common/common_archetype_misc.cpp#L39))

`[Asteroid]` и `[AsteroidMine]` — один и тот же тип записи (тип объекта предустановлен в `ASTEROID`); `[DynamicAsteroid]` принимает только ключи `Root`. Все три загружаются `LoadAsteroids` из `solar\asteroidarch.ini`.

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `inherit` | nickname | - | На уровне загрузчика; копирует поля `Root` плюс `detect_radius`, `explosion_offset`, `recharge_time`; отсутствующий донор пишет предупреждение 'inherit'. В `[DynamicAsteroid]` недоступен. | [loader L408](../../../src/Common/common_archetypeloader.cpp#L408) |
| `detect_radius` | float | 25.0 | Радиус, на котором мина обнаруживает корабли. | [L45](../../../src/Common/common_archetype_misc.cpp#L45) |
| `explosion_offset` | float | 0.0 | Смещение центра взрыва. | [L51](../../../src/Common/common_archetype_misc.cpp#L51) |
| `recharge_time` | float | 1000.0 | Время до повторного срабатывания мины. | [L57](../../../src/Common/common_archetype_misc.cpp#L57) |

## `[CollisionGroup]` (после `[Ship]` или `[Solar]`)

Описывает одну разрушаемую/отделяемую часть ближайшей предыдущей записи ([common_archetype_ships.cpp:495](../../../src/Common/common_archetype_ships.cpp#L495)). Группы получают последовательные 16-битные id для каждой записи. `[CollisionGroup]` без предшествующей записи молча пропускается в `ships`; в `solar` пишет "Attempt to load a solar group into a solar that wasn't loaded." В `ships` исчерпание 16-битного счётчика id пишет "Exceeds maximum collision group". Неизвестные ключи внутри секции игнорируются.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `obj` | cstr (всё значение) | пусто | Имя части модели (имя объекта CMP). | [L502](../../../src/Common/common_archetype_ships.cpp#L502) |
| `type` | имя типа группы (всё значение) | 1 | Без учёта регистра; имена и значения: `Fin` 0x2, `Top_Fin` 0x4, `Port_Fin` 0x8, `Starboard_Fin` 0x10, `Port_Wing` 0x20, `Starboard_Wing` 0x40, `Middle_Wing` 0x80, `Engine` 0x100, `Port_Engine` 0x200, `Middle_Engine` 0x400, `Starboard_Engine` 0x800, `Top_Engine` 0x1000, `Bottom_Engine` 0x2000, `Spoiler` 0x4000, `Tail` 0x8000, `Port_Side_Panel` 0x10000, `Starboard_Side_Panel` 0x20000, `Port_Arm` 0x40000, `Starboard_Arm` 0x80000. Любое другое значение даёт 1 без сообщения. | [L506](../../../src/Common/common_archetype_ships.cpp#L506) |
| `hit_pts` | int | 0 | Очки прочности части. | [L512](../../../src/Common/common_archetype_ships.cpp#L512) |
| `separable` | нет | false | Одно присутствие делает часть отделяемой; любое значение игнорируется. | [L516](../../../src/Common/common_archetype_ships.cpp#L516) |
| `destroy_parent` | bool, необязательный | false | `destroy_parent` без значения означает true; со значением разбирается как bool. | [L520](../../../src/Common/common_archetype_ships.cpp#L520) |
| `root_health_proxy` | bool | false | Урон части применяется к корню. | [L527](../../../src/Common/common_archetype_ships.cpp#L527) |
| `parent_impulse` | float | 0.0 | Импульс родителю при отрыве части. | [L531](../../../src/Common/common_archetype_ships.cpp#L531) |
| `child_impulse` | float | 0.0 | Импульс отделившейся части. | [L535](../../../src/Common/common_archetype_ships.cpp#L535) |
| `mass` | float | 1.0 | Масса части. | [L539](../../../src/Common/common_archetype_ships.cpp#L539) |
| `rotation_inertia` | float x3 | нет | Инерция по X, Y, Z. | [L543](../../../src/Common/common_archetype_ships.cpp#L543) |
| `separation_explosion` | id (всё значение) | 0 | Взрыв при отделении части. | [L550](../../../src/Common/common_archetype_ships.cpp#L550) |
| `debris_type` | id (всё значение) | 0 | Архетип обломков части. | [L554](../../../src/Common/common_archetype_ships.cpp#L554) |
| `dmg_hp` | cstr (всё значение) | - | Хардпойнт, повреждаемый вместе с этой частью. **Работает в паре с `dmg_obj`** (см. ниже). | [L196](../../../src/Common/common_archetype_ships.cpp#L196) |
| `dmg_obj` | id (всё значение) | - | Объект (архетип оборудования), повреждаемый вместе с этой частью. | [L202](../../../src/Common/common_archetype_ships.cpp#L202) |
| `group_dmg_hp` | cstr (всё значение) | - | Как `dmg_hp`, применяется ко всей группе. | [L208](../../../src/Common/common_archetype_ships.cpp#L208) |
| `group_dmg_obj` | id (всё значение) | - | Как `dmg_obj`, для списка группы. | [L214](../../../src/Common/common_archetype_ships.cpp#L214) |
| `explosion_resistance` | float | 1.0 | Масштаб урона от взрыва. | [L561](../../../src/Common/common_archetype_ships.cpp#L561) |
| `fuse` | `[0]` id fuse, `[1]` float, `[2]` float | нет | Fuse повреждений группы, повторяющиеся ключи добавляются; семантика параметров та же, что у `fuse` уровня объекта выше. | [L565](../../../src/Common/common_archetype_ships.cpp#L565) |
| `linked_equip` | `[0]` имя вида оборудования, `[1]` float | вид 0, здоровье 0.0 | Привязывает здоровье части к классу оборудования. `[0]` (без учёта регистра): `scanner` 0x80000, `engine` 0x20000, `weapon` 0x80, `shield` 0x100, `thruster` 0x400, `power` 0x40000, `light` 0x1, `tractor` 0x100000, `attached_fx` 0x2, `internal_fx` 0x400000, `tradelane` 0x800000, `repairdroid` 0x200000, `cloaking_device` 0x1000, `armor` 0x1000000; любое другое значение даёт 0. | [L574](../../../src/Common/common_archetype_ships.cpp#L574) |

**Парность `dmg_hp` / `dmg_obj`.** Две пары ключей заполняют список записей (хардпойнт, объект) по порядку: `dmg_hp` заполняет хардпойнт последней записи, если у неё его ещё нет, иначе начинает новую запись; `dmg_obj` заполняет объект последней записи, если у неё его нет, иначе начинает новую. В конце секции каждая запись, у которой нет хардпойнта или объекта, считается «непарной»: пишется предупреждение "Unmatched dmg_obj or dmg_hp values for Group[%s]. No damage objects will be attached." (соответственно `group_dmg_*`), и **весь список отбрасывается**. То же независимо применяется к списку `group_dmg_*`.

## Секции `[Object]` системных файлов через архетипы solar (`CSolar::ReadObj`)

Каждая секция `[Object]` системного файла (например, `universe\systems\<sys>\<sys>.ini`) читается `CSolar::ReadObj` ([common_csolar.cpp:692](../../../src/Common/common_csolar.cpp#L692)) при создании solar — на сервере в мультиплеере ([server_classes.cpp:6932](../../../src/Server/server_classes.cpp#L6947)) и на клиенте. Системный файл содержит много других секций и ключей, принадлежащих другим reader ([systems-and-bases.md](systems-and-bases.md), [world-data.md](world-data.md)); здесь потребляются только перечисленные ниже ключи, остальные ключи секции пропускаются.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `archetype` | nickname solar | нет (объект не создаётся) | Архетип `[Solar]`. Неизвестное имя пишет "Archetype \"%s\" not found while reading Solar object" и **останавливает чтение секции**; solar не создаётся. | [L710](../../../src/Common/common_csolar.cpp#L710) |
| `nickname` | id (всё значение) | нет | Nickname объекта и id объекта. Отсутствие nickname: ошибка "Solar with Archetype(%s) missing nickname", объект не создаётся. | [L734](../../../src/Common/common_csolar.cpp#L734) |
| `pos` | float x3 | 0, 0, 0 | Мировая позиция. | [L740](../../../src/Common/common_csolar.cpp#L740) |
| `rotate` | float x3, градусы | единичная | Углы Эйлера (`EulerMatrix`, градусы). | [L746](../../../src/Common/common_csolar.cpp#L746) |
| `spin` | float x3 | 0, 0, 0 | Угловая скорость. | [L754](../../../src/Common/common_csolar.cpp#L754) |
| `visit` | int (приводится к 8 битам) | 0 | Флаги посещения объекта. | [L760](../../../src/Common/common_csolar.cpp#L760) |
| `parent` | id (всё значение) | 0 | Nickname родительского объекта. | [L764](../../../src/Common/common_csolar.cpp#L764) |
| `goto` | `[0]` nickname системы, `[1]` nickname врат | нет | Назначение прыжковых врат/дыры: id системы и nickname целевых врат. Система, которая не разрешается, пишет "Jumpgate has invalid destination". Дальнейшие параметры (эффект туннеля) здесь не читаются. | [L768](../../../src/Common/common_csolar.cpp#L768) |
| `base` | nickname базы (всё значение) | нет | База, к которой ведёт этот объект; неизвестная база пишет "Could not find base %s for solar object." | [L780](../../../src/Common/common_csolar.cpp#L780) |
| `dock_with` | nickname базы (всё значение) | нет | База, с которой объект стыкуется; неизвестная база пишет ошибку. | [L791](../../../src/Common/common_csolar.cpp#L791) |
| `loadout` | id (всё значение) | `loadout` архетипа | `[Loadout]` экземпляра. Непустой loadout, которого не существует, пишет "Unable to find [Loadout] %s" (не фатально). | [L802](../../../src/Common/common_csolar.cpp#L802) |
| `atmosphere_range` | float | 0.0 | Радиус атмосферы; если меньше `solar_radius` архетипа, пишется ошибка. | [L806](../../../src/Common/common_csolar.cpp#L806) |
| `anim` | cstr (всё значение) | нет | Непрерывно проигрываемая анимация; повторяющиеся ключи добавляются. | [L812](../../../src/Common/common_csolar.cpp#L812) |
| `prev_ring`, `next_ring` | id (всё значение) | 0 | Соседние кольца trade lane. | [L820](../../../src/Common/common_csolar.cpp#L820), [L824](../../../src/Common/common_csolar.cpp#L824) |
| `tradelane_space_name` | int | 0 | Id строки названия trade lane. | [L828](../../../src/Common/common_csolar.cpp#L828) |
| `destructible` | bool | из архетипа | Переопределяет флаг архетипа. | [L832](../../../src/Common/common_csolar.cpp#L832) |
| `space_costume` | `[0]` голова, `[1]` тело, `[2..n]` аксессуары | нет | Имена частей тела из `bodyparts.ini`/`costumes.ini`. Неизвестная голова/тело пишет "space costume head/body %s does not exist"; отсутствующее тело пишет "space costume malformed". Неизвестное имя аксессуара не проверяется (retail разыменовывает там null). | [L837](../../../src/Common/common_csolar.cpp#L837) |
| `voice` | id (всё значение) | 0 | Id голоса. | [L893](../../../src/Common/common_csolar.cpp#L893) |
| `comm_appr` | nickname костюма (всё значение) | нет | Целый костюм, ищется по имени. | [L898](../../../src/Common/common_csolar.cpp#L898) |

После цикла объект отклоняется (пишется ошибка), если архетип отсутствует, nickname пуст или записи репутации для nickname не существует (см. следующий раздел).

### Проход репутации объектов (`LoadObjectVibesFromSystemIni`)

Перед созданием объектов Common один раз обходит секции `[Object]` системного файла, чтобы создать для каждого запись репутации ([common_cobjdb.cpp:54](../../../src/Common/common_cobjdb.cpp#L54), вызывается из [common_universe.cpp:822](../../../src/Common/common_universe.cpp#L822)). Чтение секции прекращается, как только известны имя, nickname и группа.

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `nickname` | id (всё значение) | нет (секция игнорируется) | Nickname объекта = handle репутации. | [L76](../../../src/Common/common_cobjdb.cpp#L76) |
| `ids_name` | int | 0 | Id строки отображаемого имени. | [L80](../../../src/Common/common_cobjdb.cpp#L80) |
| `infocard_ids` или `ids_info` | int | 0 | Id строки infocard (принимаются оба написания). | [L84](../../../src/Common/common_cobjdb.cpp#L84) |
| `group` | str (всё значение, усекается до 15 символов) | пусто | Группа репутации (фракция) объекта, например nickname группы, определённой в `initialworld.ini` ([saves-and-initial-world.md](saves-and-initial-world.md)). | [L88](../../../src/Common/common_cobjdb.cpp#L88) |
| `archetype` | nickname solar | нет | Ищет архетип solar, из которого берётся ранг (`toughness`). | [L107](../../../src/Common/common_cobjdb.cpp#L107) |

## Файлы полей астероидов (`CAsteroidField::load`)

Файл поля называется ключом `file` секции `[Asteroids]` системного файла вместе с ключом `zone`, называющим зону, которую он заполняет. Загрузчик Common `CAsteroidField::load` ([common_casteroidfield.cpp:74](../../../src/Common/common_casteroidfield.cpp#L74)) запускается сервером на всём файле ([server.md](server.md#астероидные-поля-на-сервере)) и клиентом; остальные секции файла (`[Band]`, `[AsteroidBillboards]`, `[DynamicAsteroids]`, `[TexturePanels]`, `[properties]`) относятся к клиенту/визуализации и описаны в [world-data.md](world-data.md). Нераспознанные секции пропускаются.

### `[Cube]`

Сплошные астероиды, заполняющие каждый кубик сетки поля. Предустановки конструктора для углов привязки поворота — `0, 90, 180, 270` градусов по каждой оси.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `xaxis_rotation`, `yaxis_rotation`, `zaxis_rotation` | четыре float, градусы (`[0]..[3]`) | 0, 90, 180, 270 | Четыре угла поворота, к которым кубик может привязываться вокруг этой оси. Читаются все четыре (отсутствующие читаются как 0). | [L99](../../../src/Common/common_casteroidfield.cpp#L99), [L104](../../../src/Common/common_casteroidfield.cpp#L104), [L109](../../../src/Common/common_casteroidfield.cpp#L109) |
| `asteroid` | `[0]` nickname астероида, `[1]` x, `[2]` y, `[3]` z, `[4]` угол X, `[5]` угол Y, `[6]` угол Z, необязательный `[7]` флаг | нет | Один камень, размещаемый в каждом кубике; повторяющиеся ключи добавляют камни. `[0]` должен быть загруженным архетипом `[Asteroid]` (промах пишется базой данных и затем разыменовывается, как в retail). `[1]`-`[3]` — доли половины размера кубика, ограничиваются до [-1, 1]. `[4]`-`[6]` — углы поворота в градусах; если любого из трёх нет, пишется ошибка "invalid rotation specified for an asteroid" и все три равны 0. `[7]` сравнивается точно (с учётом регистра): `mine` делает камень миной, `instant_kill` заставляет его убивать при контакте; любой другой текст означает ни то, ни другое. | [L114](../../../src/Common/common_casteroidfield.cpp#L114) |

### `[Exclusion Zones]`

Ключи применяются к **последней** строке `exclusion` секции; `exclude_billboards`, `billboard_count`, `exclude_dynamic_asteroids` и `empty_cube_frequency` до любой строки `exclusion` не имеют зоны, к которой применяться (retail этого не проверяет).

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `exclusion` | nickname зоны (`str[0]`) | нет | Зона той же системы, вырезающая дыру в поле. Отсутствующая зона пишет "AsteroidField linked to missing Exclusion Zone (%s)". | [L255](../../../src/Common/common_casteroidfield.cpp#L255) |
| `exclude_billboards` | int (усечённый float) | не задано | Значение больше 0 исключает billboard в зоне. | [L278](../../../src/Common/common_casteroidfield.cpp#L278) |
| `billboard_count` | int (усечённый float) | не задано | Если больше 0 — число billboard внутри зоны. | [L285](../../../src/Common/common_casteroidfield.cpp#L285) |
| `exclude_dynamic_asteroids` | int (усечённый float) | не задано | Если больше 0 **и** зона — сфера или эллипсоид, динамические астероиды в зоне исключаются. | [L293](../../../src/Common/common_casteroidfield.cpp#L293) |
| `empty_cube_frequency` | float | не задано | Доля пустых кубиков внутри зоны; отрицательные значения игнорируются. | [L307](../../../src/Common/common_casteroidfield.cpp#L307) |

### `[LootableZone]`

Описание динамической добычи, привязанное к зоне. Значения по умолчанию новой записи: количество добычи 1..1, сложность астероида 100, динамическая сложность 1, id 0. Запись используется, только если задан `dynamic_loot_container` или `dynamic_loot_commodity`; она привязывается к зоне, названной ключом `zone` (и запоминается в списке зон добычи поля), иначе к собственной зоне поля, иначе пишется ошибка "loot zone information attached to asteroid field (%s) that has no zone". Ключи `asteroid_loot_container`, `asteroid_loot_commodity`, `asteroid_loot_count` и `asteroid_loot_difficulty` проверяются retail-кодом, но не имеют эффекта.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `dynamic_loot_container` | id (`str[0]`) | 0 | Архетип ящика с добычей. | [L352](../../../src/Common/common_casteroidfield.cpp#L352) |
| `dynamic_loot_commodity` | id (`str[0]`) | 0 | Архетип товара. | [L358](../../../src/Common/common_casteroidfield.cpp#L358) |
| `zone` | nickname зоны (`str[0]`) | собственная зона поля | Зона, которой принадлежит запись; более поздняя строка `zone` заменяет более раннюю (неизвестное имя даёт отсутствие зоны). | [L364](../../../src/Common/common_casteroidfield.cpp#L364) |
| `dynamic_loot_count` | int `[0]` min, int `[1]` max | 1, 1 | Оба поднимаются минимум до 1; если max < min, то min устанавливается равным max. | [L370](../../../src/Common/common_casteroidfield.cpp#L370) |
| `dynamic_loot_difficulty` | int | 1 | Ограничивается диапазоном 1..100. | [L385](../../../src/Common/common_casteroidfield.cpp#L385) |
