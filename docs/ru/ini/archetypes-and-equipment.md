# Файлы архетипов и секции оборудования

Загрузчик архетипов (`Archetype::LoadEquipment`, `LoadSolar`, `LoadShips`, `LoadAsteroids`, `LoadSimples` в Common.dll) читает «шаблонные» записи, из которых создаётся каждый игровой объект. Эта страница описывает, как обходятся эти файлы, ключи, общие для всех записей (`Root`), а также секции, описывающие небоевое оборудование, плюс `[Motor]` и `[Explosion]`. Оружие описано в [archetypes-weapons.md](archetypes-weapons.md), корабли/solar/астероиды — в [archetypes-ships-solars-asteroids.md](archetypes-ships-solars-asteroids.md), товары/loadout/константы — в [goods-loadouts-constants.md](goods-loadouts-constants.md).

**Расположение файлов / как они находятся.** Все файлы перечислены в `freelancer.ini` `[Data]` и разрешаются относительно пути к данным (`GetFileSysPath(.., nullptr)`), см. [freelancer-ini.md](freelancer-ini.md). Ключ `[Data]` выбирает загрузчик ([common_dataconfig.cpp:42](../../../src/Common/common_dataconfig.cpp#L42)); повторяющиеся ключи загружаются в порядке следования в файле, и порядок важен (см. «Ссылки между записями» ниже).

| Ключ `[Data]` | Загрузчик | Понимаемые секции |
|---|---|---|
| `equipment` (повторяется) | `LoadEquipment` | `Motor`, `Explosion`, 25 видов оборудования ниже, `LOD` |
| `solar` | `LoadSolar` | `Solar`, `CollisionGroup`, `Explosion`, `LOD` |
| `ships` (повторяется) | `LoadShips` | `Ship`, `CollisionGroup`, `LOD`, `Simple`, `Explosion` |
| `asteroids` | `LoadAsteroids` | `Asteroid`, `AsteroidMine`, `DynamicAsteroid` |
| `explosions` (и `debris`, тот же файл) | `LoadSimples` | `Simple`, `Explosion` |

Любое другое имя секции в этих файлах пропускается без сообщения ([common_archetypeloader.cpp:453](../../../src/Common/common_archetypeloader.cpp#L453)). Файлы читаются как BINI или текст через `INI_Reader` ([parsers.md](parsers.md)); имена секций и ключей не зависят от регистра.

## Обозначения значений на этих страницах

| Тип | Геттер | Примечания |
|---|---|---|
| `int` | `get_value_int(i)` | MSVCRT `atoi` параметра: только десятичное, переполнение по модулю 2^32, без hex. |
| `float` | `get_value_float(i)` / `value_num(i)` | `ParseNumber` (в текстовых файлах принимает `0x` hex и т. п.); хранится как `float`, если не указано иное. |
| `bool` | `get_value_bool(i)` | Текстовые файлы принимают ровно `true`/`1`/`false`/`0` (без учёта регистра); всё остальное пишет в лог «Invalid bool value» и даёт `false`. |
| `str` (всё значение) | `get_value_string()` | Вся правая часть, включая запятые, одной строкой. |
| `str[i]` | `get_value_string(i)` | Один параметр, разделённый запятыми, с обрезанными пробелами. |
| `id` | `CreateID(string)` | 32-битный хеш текста (сначала приводится к нижнему регистру, текст обрезается до 255 символов, null даёт 0). Записи хранятся и ищутся по этому хешу, поэтому nickname не зависят от регистра, а два имени с одинаковым хешем конфликтуют. Используется для каждого «nickname» и каждой ссылки на другую запись. |
| `cstr` | `StringAlloc(text, flag)` | Текст, помещённый в пул строк процесса; без интерпретации. |

«По умолчанию» — значение, присваиваемое конструктором записи; общего механизма значений по умолчанию нет. Отсутствующий ключ оставляет значение по умолчанию, повторяющийся скалярный ключ сохраняет последнее вхождение. Проверяется ли значение на диапазон — указано для каждого ключа; если ничего не сказано, значение не проверяется.

## Поведение загрузчика (все файлы архетипов)

* Запись создаётся на заголовке `[Section]` и заполняется вызовом `read()` для каждого ключа по порядку. Ключи, которых класс записи не знает, передаются **клиентскому компаньону** (`ICliObj::ReadUnknown`, только клиент; ключи, которые он потребляет, перечислены в [client-world-fx-archetypes.md](client-world-fx-archetypes.md)). Сообщать ли о неизвестном ключе — решает он; на сервере (компаньона нет) такой ключ молча игнорируется для `Ship`/`Solar` и оборудования (`SinglePlayer()` вызывается, а его результат отбрасывается), а для `Asteroid`/`DynamicAsteroid`/`Motor`/`Explosion` результат вообще не проверяется.
* Секции `[LOD]` после записи передаются компаньону **последней успешно вставленной** записи; Common их не читает.
* Записи нужен `nickname`. Без него вставка не удаётся («Loaded '%s' has no nickname .. not adding to database»). Повторяющиеся nickname: `Equipment`, `Asteroid`, `DynamicAsteroid`: побеждает первая запись, более поздняя секция молча отбрасывается. `Ship`: побеждает первая, в лог пишется предупреждение «nickname = %s value is not unique». `Solar`: существующая запись обновляется из дубликата через `Root::redefine` плюс `toughness` (ids_name, ids_info, hit_pts, explosion_arch, explosion_resistance, mass, rotation_inertia, toughness), остальное в дубликате игнорируется. `Simple`: побеждает первая плюс предупреждение. (Ветвь «перезагрузки», переопределяющая дубликаты, `DataConfig` никогда не выбирается.)
* Пустой результат («contains no new definitions») пишет предупреждение с именем файла для `equipment`, `solar`, `ships`, `asteroids`, `explosions`, `loadouts`.
* `Archetype::LoadEquipment` также создаёт общий архетип двигателя `BEAM_ARCH` после каждого файла, независимо от того, открылся ли файл.

### `inherit` (записи оборудования, кораблей, solar, астероидов)

`inherit = <nickname>` копирует ранее загруженную запись того же вида в читаемую запись и должен стоять **до** ключей, которые должны её переопределять. Nickname читаемой записи сохраняется. Подробности, различающиеся по видам:

* **Оборудование** ([common_archetype_equipment.cpp:66](../../../src/Common/common_archetype_equipment.cpp#L66)): донор ищется в базе оборудования; если класс донора отличается от класса записи, ничего не копируется (без сообщения). Отсутствующий донор пишет в лог «'inherit' failed. couldn't find %s». Доноры `Armor` не копируются вовсе (этот класс попадает в пустую ветвь switch копирования). `Gun`, `ShieldGenerator`, `Munition`, `Mine`, `CounterMeasure`, дропперы и все листовые классы копируют собственные поля.
* **Ship**, **Solar**, **Asteroid**: обрабатываются загрузчиком до вызова `read()` ([common_archetypeloader.cpp:294](../../../src/Common/common_archetypeloader.cpp#L294), [:361](../../../src/Common/common_archetypeloader.cpp#L361), [:404](../../../src/Common/common_archetypeloader.cpp#L404)); какие именно поля копируются, см. на странице кораблей. Отсутствующий донор пишет предупреждение (Ship, Asteroid) или молча игнорируется (Solar).

### Ссылки между записями

Ссылки разрешаются в момент чтения ключа, а не после: `inherit`, `loot_appearance`, `pod_appearance` (оборудование) и `inherit` (корабли/solar/астероиды) ищут цель среди уже загруженного. Цель, определённая позже в том же файле или в более поздней записи `[Data]`, не находится (поиск пишет в лог `ArchDB::Get(...) failed`, ссылка остаётся пустой). Ссылки, которые только хранятся как `id` (`explosion_arch`, `projectile_archetype`, `motor`, `loadout`, ...), разрешаются позже, в момент использования.

## Ключи каждой записи (`Root::read`)

Их принимает каждая секция каждого файла архетипов ([common_archetype.cpp:468](../../../src/Common/common_archetype.cpp#L468)).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | id (`str[0]`) | нет (запись отклоняется) | Ключ записи. | [L470](../../../src/Common/common_archetype.cpp#L470) |
| `DA_archetype` / `attachment_archetype` | cstr | пусто | Путь к файлу визуальной модели (в поставляемых данных это файлы `.3db` и `.cmp`); разрешается движком, загрузчиком не проверяется. Оба написания задают одно и то же поле. Если пусто у Munition, используется общий `BEAM_ARCH`. | [L476](../../../src/Common/common_archetype.cpp#L476) |
| `type` | имя типа объекта (`str[0]`) | 0 | Одно из имён типов объектов в таблице ниже (без учёта регистра). Неизвестное имя пишет в лог «unknown OBJECT_TYPE» и даёт 0. Конструкторы заранее задают тип для некоторых классов (см. ниже). | [L482](../../../src/Common/common_archetype.cpp#L482) |
| `ids_name` | int | 0 | Id строкового ресурса отображаемого имени. | [L488](../../../src/Common/common_archetype.cpp#L488) |
| `ids_info` | int | 0 | Id строкового ресурса инфокарты. | [L494](../../../src/Common/common_archetype.cpp#L494) |
| `explosion_arch` | id | 0 | Nickname `[Explosion]`, проигрываемого при уничтожении. | [L500](../../../src/Common/common_archetype.cpp#L500) |
| `explosion_resistance` | float | 1.0 | Масштаб получаемого урона от взрывов. | [L506](../../../src/Common/common_archetype.cpp#L506) |
| `hit_pts` | float | 100.0 (Equipment: 1.0, Shield: 0.0) | Очки прочности. | [L512](../../../src/Common/common_archetype.cpp#L512) |
| `mass` | float | 1.0 | Масса. | [L518](../../../src/Common/common_archetype.cpp#L518) |
| `rotation_inertia` | float ×3 (`[0]`,`[1]`,`[2]`) | нет (флаг выключен) | Момент инерции X, Y, Z; наличие ключа выставляет флаг «есть момент инерции». | [L524](../../../src/Common/common_archetype.cpp#L524) |
| `phantom_physics` | bool | false | Флаг фантомной физики (потребляется кодом физики). | [L533](../../../src/Common/common_archetype.cpp#L533) |
| `mission_property` | нет | - | Распознаётся и игнорируется. | [L539](../../../src/Common/common_archetype.cpp#L539) |

Типы объектов, заданные конструкторами (более поздний ключ `type` их переопределяет; у `Munition` более поздний ключ `seeker` снова сбрасывает тип): `LootCrate`, `Commodity` = `LOOT`; `Munition` = `BULLET` (становится `GUIDED`, как только `seeker` равен `dumb` или `lock`, и снова `BULLET` при любом другом значении `seeker`); `Mine` = `MINE`; `Asteroid` = `ASTEROID`; `DynamicAsteroid` = внутреннее значение `0x40000000` (имени в таблице нет).

### Имена типов объектов (`type =`)

Таблица `GObjectTypes` ([common_archetype.cpp:300](../../../src/Common/common_archetype.cpp#L300)); значение — битовая маска типа объекта. Сравнение без учёта регистра; побеждает первое совпадение.

| Имя | Биты | Имя | Биты | Имя | Биты |
|---|---|---|---|---|---|
| `NONE` | 0 | `MOON` | 0x1 | `PLANET` | 0x2 |
| `SUN` | 0x4 | `BLACKHOLE` | 0x8 | `SATELLITE` | 0x10 |
| `DOCKING_RING` | 0x20 | `JUMP_GATE` | 0x40 | `TRADELANE_RING` | 0x80 |
| `STATION` | 0x100 | `WAYPOINT` | 0x200 | `AIRLOCK_GATE` | 0x400 |
| `JUMP_HOLE` | 0x800 | `WEAPONS_PLATFORM` | 0x1000 | `DESTROYABLE_DEPOT` | 0x2000 |
| `NON_TARGETABLE` | 0x4000 | `MISSION_SATELLITE` | 0x8000 | `FIGHTER` | 0x10000 |
| `FREIGHTER` | 0x20000 | `GUNBOAT` | 0x40000 | `CRUISER` | 0x80000 |
| `TRANSPORT` | 0x100000 | `CAPITAL` | 0x200000 | `MINING` | 0x400000 |
| `GUIDED` | 0x1000000 | `BULLET` | 0x2000000 | `MINE` | 0x4000000 |
| `LOOT` | 0x10000000 | `ASTEROID` | 0x20000000 | | |

Та же таблица используется для `[CollisionConsts] damage` в `constants.ini` ([goods-loadouts-constants.md](goods-loadouts-constants.md)).

## Ключи каждой записи оборудования (`Equipment::read`)

Добавляются базовым классом всех 25 видов оборудования ([common_archetype_equipment.cpp:61](../../../src/Common/common_archetype_equipment.cpp#L61)). Ключи `Root` пробуются первыми.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `inherit` | nickname (`str`) | - | См. «inherit» выше. | [L66](../../../src/Common/common_archetype_equipment.cpp#L66) |
| `use_count` | bool | true | Предмет учитывается (стекуется) в грузе. | [L311](../../../src/Common/common_archetype_equipment.cpp#L311) |
| `units_per_container` | int | 1 | Единиц на грузовой контейнер. | [L317](../../../src/Common/common_archetype_equipment.cpp#L317) |
| `volume` | float | 0.0 | Объём груза. | [L331](../../../src/Common/common_archetype_equipment.cpp#L331) |
| `tractored_explosion` | id | 0 | Взрыв, проигрываемый при захвате лучом. | [L338](../../../src/Common/common_archetype_equipment.cpp#L338) |
| `lootable` | bool | true | Может выпасть как добыча. | [L344](../../../src/Common/common_archetype_equipment.cpp#L344) |

(`hit_pts` проверяется в `Equipment::read` повторно, но эта ветвь недостижима, потому что `Root::read` потребляет его первым.)

## Ключи присоединяемого оборудования (`AttachedEquipment::read`)

Общие для `Shield`, `ShieldGenerator`, `Thruster`, `CargoPod`, `CloakingDevice` и каждого оружейного пускового устройства ([common_archetype_equipment.cpp:454](../../../src/Common/common_archetype_equipment.cpp#L454)). Применяются после ключей `Equipment`.

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `separation_explosion` | id (`str[0]`) | 0 | Взрыв при отрыве детали. | [L459](../../../src/Common/common_archetype_equipment.cpp#L459) |
| `debris_type` | id (`str[0]`) | 0 | Архетип обломков детали. | [L465](../../../src/Common/common_archetype_equipment.cpp#L465) |
| `child_impulse` | float | 0.0 | Импульс, сообщаемый оторванной детали. | [L471](../../../src/Common/common_archetype_equipment.cpp#L471) |
| `parent_impulse` | float | 0.0 | Импульс, сообщаемый родителю. | [L477](../../../src/Common/common_archetype_equipment.cpp#L477) |
| `HP_child` | cstr (`str[0]`) | пусто | Имя дочернего hardpoint. | [L483](../../../src/Common/common_archetype_equipment.cpp#L483) |
| `toughness` | int | 0 | Ранг прочности. | [L489](../../../src/Common/common_archetype_equipment.cpp#L489) |

## Секции оборудования

Имя секции выбирает класс. Таблица ниже — таблица диспетчеризации загрузчика ([common_archetypeloader.cpp:27](../../../src/Common/common_archetypeloader.cpp#L27)); все имена сравниваются без учёта регистра, и каждая секция может встречаться любое число раз.

| Секция | Класс | Читает | Примечания |
|---|---|---|---|
| `LootCrate` | LootCrate | Root + Equipment | Тип объекта заранее задан как `LOOT`. Собственных ключей нет. |
| `Commodity` | Commodity | + `decay_per_second`, `loot_appearance`, `pod_appearance` | Тип объекта заранее задан как `LOOT`. |
| `Power` | Power | + 4 ключа | |
| `Engine` | Engine | + 7 ключей | |
| `Armor` | Armor | + `hit_pts_scale` | |
| `Shield` | Shield | Root + Equipment + AttachedEquipment | `hit_pts` по умолчанию 0. Собственных ключей нет. |
| `ShieldGenerator` | ShieldGenerator | + 9 ключей | |
| `Thruster` | Thruster | + 2 ключа | |
| `Gun`, `MineDropper`, `CounterMeasureDropper` | пусковые устройства | см. [archetypes-weapons.md](archetypes-weapons.md) | |
| `Scanner` | Scanner | + 3 ключа | |
| `Light` | Light | + 2 ключа | |
| `Tractor` | Tractor | + 2 ключа | |
| `CargoPod` | CargoPod | ключи AttachedEquipment | Собственных ключей нет. |
| `AttachedFX` | AttachedFXEquip | Root + Equipment | Собственных ключей нет (визуальные ключи читает клиентский компаньон). |
| `InternalFX` | InternalFXEquip | + `use_animation` | |
| `TradeLane` | TradeLaneEquip | Root + Equipment | Собственных ключей нет. |
| `RepairDroid` | RepairDroid | + `repair_rate` | |
| `RepairKit` | RepairKit | + `loot_appearance` | |
| `ShieldBattery` | ShieldBattery | + `loot_appearance` | |
| `CloakingDevice` | CloakingDevice | + 5 ключей | |
| `Munition`, `Mine`, `CounterMeasure` | снаряды | см. [archetypes-weapons.md](archetypes-weapons.md) | |

Пример (синтетический):

```ini
[Engine]
nickname = demo_engine_01
ids_name = 1000
DA_archetype = equipment\models\demo_engine.cmp
hit_pts = 2000
volume = 5
max_force = 30000
power_usage = 20
linear_drag = 0.4
```

### `[Commodity]`  ([L1346](../../../src/Common/common_archetype_equipment.cpp#L1346))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `decay_per_second` | float | 0.0 | Очки прочности, теряемые в секунду в космосе; рабочее значение ограничено 25 % от `hit_pts`. | [L1351](../../../src/Common/common_archetype_equipment.cpp#L1351) |
| `loot_appearance` | nickname оборудования | нет (сам товар) | Оборудование, чья модель показывается как добыча. Разрешается при чтении (см. ссылки между записями). | [хелпер L145](../../../src/Common/common_archetype.cpp#L145), [вызов L1357](../../../src/Common/common_archetype_equipment.cpp#L1357) |
| `pod_appearance` | nickname `CargoPod` | нет | Запись грузового контейнера. Сохраняется, только если найденная запись — `CargoPod`; любой другой класс молча игнорируется. | [L1360](../../../src/Common/common_archetype_equipment.cpp#L1360) |

### `[Power]`  ([L548](../../../src/Common/common_archetype_equipment.cpp#L548))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `capacity` | float | 0.0 | Ёмкость энергии. | [L553](../../../src/Common/common_archetype_equipment.cpp#L553) |
| `charge_rate` | float | 0.0 | Скорость подзарядки. | [L559](../../../src/Common/common_archetype_equipment.cpp#L559) |
| `thrust_capacity` | float | 0.0 | Ёмкость энергии для thruster. | [L565](../../../src/Common/common_archetype_equipment.cpp#L565) |
| `thrust_charge_rate` | float | 0.0 | Скорость подзарядки thruster. | [L571](../../../src/Common/common_archetype_equipment.cpp#L571) |

### `[Engine]`  ([L622](../../../src/Common/common_archetype_equipment.cpp#L622))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `power_usage` | float | 0.0 | Коэффициент потребления энергии. | [L627](../../../src/Common/common_archetype_equipment.cpp#L627) |
| `cruise_power_usage` | float | 0.0 | Потребление энергии в круизе. | [L633](../../../src/Common/common_archetype_equipment.cpp#L633) |
| `cruise_charge_time` | float | 0.0 | Секунды зарядки круиза. | [L639](../../../src/Common/common_archetype_equipment.cpp#L639) |
| `max_force` | float | 0.0 | Сила тяги. | [L645](../../../src/Common/common_archetype_equipment.cpp#L645) |
| `linear_drag` | float | 0.0 | Линейное сопротивление, добавляемое двигателем. | [L651](../../../src/Common/common_archetype_equipment.cpp#L651) |
| `indestructible` | bool | false | Двигатель нельзя уничтожить. | [L657](../../../src/Common/common_archetype_equipment.cpp#L657) |
| `reverse_fraction` | float | 0.0 | Доля тяги, доступная при заднем ходе. | [L663](../../../src/Common/common_archetype_equipment.cpp#L663) |

### `[Armor]`  ([L706](../../../src/Common/common_archetype_equipment.cpp#L706))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `hit_pts_scale` | float | 1.0 | Множитель, применяемый к очкам прочности корабля. | [L711](../../../src/Common/common_archetype_equipment.cpp#L711) |

### `[ShieldGenerator]`  ([L865](../../../src/Common/common_archetype_equipment.cpp#L865))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `hp_type` | имя типа hardpoint (`str[0]`) | `hp_shield_generator` (5) | Одна запись таблицы типов hardpoint из [archetypes-weapons.md](archetypes-weapons.md#названия-типов-hardpoint). **Неизвестное имя даёт значение 7 (`hp_elite_shield_generator`), а не значение по умолчанию.** | [L870](../../../src/Common/common_archetype_equipment.cpp#L870) |
| `shield_type` | id | 0 | Запись типа щита (используется таблицей связей щитов корабля). | [L882](../../../src/Common/common_archetype_equipment.cpp#L882) |
| `regeneration_rate` | float | 0.0 | Регенерация щита в секунду. | [L888](../../../src/Common/common_archetype_equipment.cpp#L888) |
| `max_capacity` | float | 0.0 | Ёмкость щита. | [L894](../../../src/Common/common_archetype_equipment.cpp#L894) |
| `constant_power_draw` | float | 0.0 | Потребление энергии во включённом состоянии. | [L900](../../../src/Common/common_archetype_equipment.cpp#L900) |
| `rebuild_power_draw` | float | 0.0 | Потребление энергии при перестройке. | [L906](../../../src/Common/common_archetype_equipment.cpp#L906) |
| `offline_rebuild_time` | float | 0.0 | Секунды перестройки после отключения. | [L912](../../../src/Common/common_archetype_equipment.cpp#L912) |
| `offline_threshold` | float | 0.0 | Доля ёмкости, при которой щит отключается. | [L918](../../../src/Common/common_archetype_equipment.cpp#L918) |
| `hp_shield_type` | список имён типов hardpoint (`str[0..n]`) | пустой список | Типы hardpoint, на которые можно установить генератор, по одному на параметр до первого пустого. Имя, которого нет в таблице типов hardpoint, молча пропускается. Повторяющиеся ключи **добавляются**. Пустой список означает «встроенный список из 34 записей» (все типы `hp_*shield*`). | [L924](../../../src/Common/common_archetype_equipment.cpp#L924) |

### `[Thruster]`  ([L987](../../../src/Common/common_archetype_equipment.cpp#L987))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `power_usage` | float | 0.0 | Потребление энергии. | [L993](../../../src/Common/common_archetype_equipment.cpp#L993) |
| `max_force` | float | 0.0 | Сила тяги. | [L999](../../../src/Common/common_archetype_equipment.cpp#L999) |

### `[Scanner]`  ([L1029](../../../src/Common/common_archetype_equipment.cpp#L1029))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `power_usage` | float | 0.0 | Хранится как порог мощности. | [L1034](../../../src/Common/common_archetype_equipment.cpp#L1034) |
| `range` | float | 1000.0 | Дальность радара. | [L1040](../../../src/Common/common_archetype_equipment.cpp#L1040) |
| `cargo_scan_range` | float | 0.0 | Дальность сканирования груза. | [L1046](../../../src/Common/common_archetype_equipment.cpp#L1046) |

### `[Light]`  ([L1086](../../../src/Common/common_archetype_equipment.cpp#L1086))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `always_on` | bool | true | Свет горит постоянно. | [L1091](../../../src/Common/common_archetype_equipment.cpp#L1091) |
| `docking_light` | bool | false | Свет является посадочным. | [L1097](../../../src/Common/common_archetype_equipment.cpp#L1097) |

### `[Tractor]`  ([L1117](../../../src/Common/common_archetype_equipment.cpp#L1117))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `max_length` | float | 0.0 | Максимальная длина луча. | [L1121](../../../src/Common/common_archetype_equipment.cpp#L1121) |
| `reach_speed` | float | 100.0 | Скорость, с которой луч вытягивается. | [L1126](../../../src/Common/common_archetype_equipment.cpp#L1126) |

### `[InternalFX]`  ([L1197](../../../src/Common/common_archetype_equipment.cpp#L1197))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `use_animation` | cstr (всё значение) | пусто | Скрипт анимации, проигрываемый при использовании оборудования. | [L1202](../../../src/Common/common_archetype_equipment.cpp#L1202) |

### `[RepairDroid]`  ([L1230](../../../src/Common/common_archetype_equipment.cpp#L1230))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `repair_rate` | float | 0.0 | Скорость ремонта. | [L1235](../../../src/Common/common_archetype_equipment.cpp#L1235) |

### `[RepairKit]` и `[ShieldBattery]`  ([L1404](../../../src/Common/common_archetype_equipment.cpp#L1404), [L1428](../../../src/Common/common_archetype_equipment.cpp#L1428))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `loot_appearance` | nickname оборудования | нет (сам предмет) | Оборудование, чья модель показывается как добыча (разрешается при чтении). | [хелпер L145](../../../src/Common/common_archetype.cpp#L145), вызовы [L1409](../../../src/Common/common_archetype_equipment.cpp#L1409), [L1433](../../../src/Common/common_archetype_equipment.cpp#L1433) |

### `[CloakingDevice]`  ([L1272](../../../src/Common/common_archetype_equipment.cpp#L1272))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `power_usage` | float | 0.0 | Потребление энергии. | [L1277](../../../src/Common/common_archetype_equipment.cpp#L1277) |
| `cloakin_time` | float | 0.0 | Секунды до маскировки. | [L1283](../../../src/Common/common_archetype_equipment.cpp#L1283) |
| `cloakout_time` | float | 0.0 | Секунды до снятия маскировки. | [L1289](../../../src/Common/common_archetype_equipment.cpp#L1289) |
| `cloakin_fx` | id | 0 | Эффект, проигрываемый при включении маскировки. | [L1295](../../../src/Common/common_archetype_equipment.cpp#L1295) |
| `cloakout_fx` | id | 0 | Эффект, проигрываемый при снятии маскировки. | [L1301](../../../src/Common/common_archetype_equipment.cpp#L1301) |

## `[Motor]` (в файлах оборудования)

Определяет двигатель ракеты/торпеды, на который ссылается `motor =` у `Munition`. Разбирается только в `LoadEquipment` ([common_archetypeloader.cpp:144](../../../src/Common/common_archetypeloader.cpp#L144), [common_archetype_misc.cpp:113](../../../src/Common/common_archetype_misc.cpp#L113)). Повторяющийся nickname обновляет у существующего двигателя `accel`, `lifetime`, `delay`.

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `nickname` | id (всё значение) | нет | Ключ двигателя. | [L117](../../../src/Common/common_archetype_misc.cpp#L117) |
| `accel` | float | 15.0 | Ускорение. | [L123](../../../src/Common/common_archetype_misc.cpp#L123) |
| `lifetime` | float | 1.5 | Время работы в секундах. | [L129](../../../src/Common/common_archetype_misc.cpp#L129) |
| `delay` | float | 0.0 | Задержка перед зажиганием. | [L135](../../../src/Common/common_archetype_misc.cpp#L135) |

## `[Explosion]` (файлы оборудования, solar, кораблей и взрывов)

Записи взрывов хранятся по nickname в map ([common_archetypeloader.cpp:723](../../../src/Common/common_archetypeloader.cpp#L723), [common_archetype_misc.cpp:173](../../../src/Common/common_archetype_misc.cpp#L173)). Учитывается только **первый** `nickname` в секции (последующие игнорируются); ключи, стоящие до него, игнорируются. Если nickname уже существует, переиспользуется существующая запись, а новые ключи перезаписывают её поля (сброс происходит только при перезагрузке). Неизвестные ключи игнорируются.

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `nickname` | id (всё значение) | нет | Ключ взрыва. | [загрузчик L733](../../../src/Common/common_archetypeloader.cpp#L733) |
| `radius` | float | 0.0 | Радиус поражения. | [L175](../../../src/Common/common_archetype_misc.cpp#L175) |
| `impulse` | float | 0.0 | Импульс, сообщаемый объектам в радиусе. | [L180](../../../src/Common/common_archetype_misc.cpp#L180) |
| `hull_damage` | float | 0.0 | Урон корпусу. | [L185](../../../src/Common/common_archetype_misc.cpp#L185) |
| `energy_damage` | float | 0.0 | Урон энергии. | [L190](../../../src/Common/common_archetype_misc.cpp#L190) |
| `lifetime` | float `[0]`, необязательный float `[1]` | 0.0 / 10000.0 | `[0]` = минимальное время жизни; `[1]` = максимальное, по умолчанию равно `[0]`, если задано `lifetime = x` (пустой второй параметр). | [L195](../../../src/Common/common_archetype_misc.cpp#L195) |
| `effect` | id `[0]`, необязательный float `[1]` | - / -1.0 | Nickname эффекта и необязательная сила (-1, если не указана). Повторяющиеся ключи добавляются. | [L204](../../../src/Common/common_archetype_misc.cpp#L204) |
| `process` | `none` \| `shatter` \| `disappear` | `disappear` | Что происходит со взрывающимся объектом (хранится как 2 / 0 / 1). Без учёта регистра; любое другое значение оставляет предыдущую настройку. | [L215](../../../src/Common/common_archetype_misc.cpp#L215) |
| `debris_type` | id `[0]`, необязательный int `[1]` | - / количество 0 | Архетип обломков и их количество. Повторяющиеся ключи добавляются. | [L228](../../../src/Common/common_archetype_misc.cpp#L228) |
| `num_child_pieces` | int (усечённый float) | 0 | Число дочерних частей. | [L238](../../../src/Common/common_archetype_misc.cpp#L238) |
| `debris_impulse` | float | 5.0 | Импульс, сообщаемый обломкам. | [L244](../../../src/Common/common_archetype_misc.cpp#L244) |
| `innards_debris_object` | id (всё значение) | - | Объект внутренних обломков. Повторяющиеся ключи добавляются. | [L249](../../../src/Common/common_archetype_misc.cpp#L249) |
| `innards_debris_radius` | float | 0.0 | Радиус появления внутренних обломков. | [L254](../../../src/Common/common_archetype_misc.cpp#L254) |
| `innards_debris_start_time` | float | -1.0 | Время начала внутренних обломков. | [L259](../../../src/Common/common_archetype_misc.cpp#L259) |
| `innards_debris_num` | float | 0.0 | Число внутренних обломков. | [L264](../../../src/Common/common_archetype_misc.cpp#L264) |

## Записи `[Simple]`

`[Simple]` (в `ships\shiparch.ini` и `fx\explosions.ini`) — простая запись `Root`: она принимает только перечисленные выше ключи `Root` (плюс всё, что читает клиентский компаньон). Дубликаты: побеждает первая, в лог пишется предупреждение ([common_archetypeloader.cpp:163](../../../src/Common/common_archetypeloader.cpp#L163)).
