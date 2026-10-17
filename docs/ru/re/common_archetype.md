# Архетипы: `Archetype::Load*`, `ArchDB`, `ICliObj`

Архетип — запись игровых данных (корабль, солар, оборудование, астероид, взрыв), которую
`Common.dll` читает из INI при `DataConfig::startup` и держит в базах по `CreateID(nickname)`.
Документ описывает форматы файлов, правила вставки в базы и контракт с клиентскими «компаньонами»
(`ICliObj`), который пересекает границу `Common.dll` ↔ `freelancer.exe`. Код:
`src/include/common_archetype.h`, `src/Common/common_archetype*.cpp`, `common_archetypeloader.cpp`;
клиентская сторона — [freelancer_cliobj.md](freelancer_cliobj.md).

## Классы

```
Root                      базовая запись (nickname, DA_archetype, type, hit_pts, mass, ...)
 ├─ EqObj                 объект с оборудованием (CollisionGroup, shield_link, docking_sphere, fuse)
 │   ├─ Ship              + CommReferrable (множественное наследование), hp_type
 │   └─ Solar
 ├─ Equipment             volume, use_count, units_per_container, lootable, ...
 │   ├─ LootCrate, Commodity, Power, Scanner, Light, Tractor, Armor, RepairDroid,
 │   │  RepairKit, ShieldBattery, TradeLaneEquip, AttachedFX/InternalFX
 │   ├─ AttachedEquipment → Launcher → Gun; MineDropper, CounterMeasureDropper,
 │   │                      ShieldGenerator, Thruster, CloakingDevice, CargoPod, Shield, Engine
 │   └─ Projectile → Munition, Mine, CounterMeasure
 ├─ Asteroid, DynamicAsteroid   (в базе под Root, не под Equipment)
 └─ Simple                (взрывные/эффектные «простые» объекты)
MotorData                 не Root: nickname, accel, lifetime, delay (отдельная база)
Explosion                 не Root: отдельная база
```

Каждый класс сообщает `get_class_type()` (перечисление `AClass*` в заголовке; для `Root` — 0, `EqObj` — 1,
`Equipment` — 6, `LootCrate` — 8, далее по таблице). Виртуальные слоты `Root`: деструктор, `read(INI_Reader&)`,
`redefine(Root const&)`, `load()` (меш), `get_class_type()`, `free_resources()`. `create_instance` и
`is_loaded` не виртуальные; `Root::EngineIndex == -1` означает «меш не загружен».

## Базы `ArchDB`

Одна база на вид (`GShipDb`, `GSolarDb`, `GAsteroidDb`, `GDynamicAsteroidDb`, `GSimpleDb`,
`GEquipmentDb`, `GMotorDb`, `GExplosionDb`); ключ — `unsigned` = `CreateID(nickname)` = `Root::Nickname`.
Двух одноимённых записей разных видов конфликта нет: корабль и пушка с одним ником лежат в разных базах.

| Ситуация при вставке | Результат |
|---|---|
| Нет `nickname` (id 0) | ошибка `Loaded '%s' has no nickname .. not adding to database.`, запись удаляется |
| Новый id | запись остаётся в базе, компаньону вызывается `Bind` |
| Дубликат, `bReload == false` (так всегда зовёт DataConfig) | предупреждение `... value is not unique. Aborted second version.`, новая запись удаляется |
| Дубликат, `bReload == true` | существующая запись получает `redefine(новая)`, новая удаляется |

`Get*` (`GetShip`, `GetEquipment`, …) на промахе пишут `ArchDB::Get(%u|%s) failed` и возвращают
`nullptr`; `GetExplosion` и внутренние `Find` молчат. Для сетевого сжатия ID ведётся вектор
«большой ID ↔ 16-битный малый ID» (`LargeIDToSmallID`/`SmallIDToLargeID`): в него попадают
Simple, Ship, Solar, Asteroid, DynamicAsteroid и Motor, но не Equipment и не Explosion; вектор
не очищается `Archetype::Free`.

## Загрузчики

Все — `__cdecl` в `namespace Archetype`; возвращают **прирост числа записей** в соответствующих
базах (0 — файл не открылся или пуст; DataConfig в этом случае лишь предупреждает).

| Загрузчик | Типичные файлы | Заголовки секций |
|---|---|---|
| `LoadEquipment` | `equipment\*_equip.ini` | `[Motor]`, `[Explosion]`, `[LootCrate]`, `[Commodity]`, `[Power]`, `[Engine]`, `[Armor]`, `[Shield]`, `[ShieldGenerator]`, `[Thruster]`, `[Gun]`, `[MineDropper]`, `[CounterMeasureDropper]`, `[Scanner]`, `[Light]`, `[Tractor]`, `[CargoPod]`, `[AttachedFX]`, `[InternalFX]`, `[TradeLane]`, `[RepairDroid]`, `[RepairKit]`, `[ShieldBattery]`, `[CloakingDevice]`, `[Munition]`, `[Mine]`, `[CounterMeasure]`, `[LOD]` |
| `LoadSolar` | `solar\solararch.ini` | `[Solar]`, `[CollisionGroup]`, `[Explosion]`, `[LOD]` |
| `LoadShips` | `ships\shiparch.ini`, `rtc_shiparch.ini` | `[Ship]`, `[CollisionGroup]`, `[LOD]`, `[Simple]`, `[Explosion]` |
| `LoadAsteroids` | `solar\asteroidarch.ini` | `[Asteroid]`, `[AsteroidMine]` (тот же класс), `[DynamicAsteroid]` |
| `LoadSimples` | `fx\explosions.ini` | `[Simple]`, `[Explosion]` |

Блок записи (Ship/Solar/Asteroid/Equipment): (1) если есть фабрика — `ICliObjFactory::Create(TypeId)`
даёт компаньона; (2) создаётся объект, ему передаётся компаньон; (3) значения читаются циклом:
ключ `inherit = <nickname>` копирует донора (поиск в той же базе; промах — предупреждение
`'inherit' failed. couldn't find %s`) и чтение продолжается; прочие ключи идут в `read()`, а ключи,
которых запись не знает, — в `ICliObj::ReadUnknown`; (4) вставка в базу. `[CollisionGroup]` относится
к последнему Ship/Solar, `[LOD]` — компаньону последнего листа, `[Explosion]` — `LoadExplosion`.
`read()` вернул `false` — вызывается `SinglePlayer()` (побочный эффект лога), секция всё равно вставляется.

Общие ключи `Root::read`: `nickname`, `DA_archetype`/`attachment_archetype` (путь меша),
`type` (битовая маска `OBJECT_TYPE`, 29 имён), `ids_name`, `ids_info`, `explosion_arch`,
`explosion_resistance`, `hit_pts`, `mass`, `rotation_inertia`, `phantom_physics`, `mission_property`.
`EqObj`: `docking_camera`, `docking_sphere`, `fuse`, `shield_link` (nick, hardpoint, hardpoint генератора…),
`nomad`. `[CollisionGroup]`: `obj`, `type` (19 имён: Fin, …, Starboard_Arm), `hit_pts`, `separable`,
`destroy_parent`, `root_health_proxy`, `parent_impulse`/`child_impulse`, `mass`, `rotation_inertia`,
`separation_explosion`, `debris_type`, `explosion_resistance`, `dmg_hp`/`dmg_obj`, `fuse`,
`linked_equip`. Группы получают ID с 4 (счётчик стартует с 3, `++` до выдачи).
`[Motor]`: `nickname`, `accel`, `lifetime`, `delay` (по умолчанию 15 / 1.5 / 0).
`[Explosion]` (`Explosion`): `nickname`, `radius`, `impulse`, `hull_damage`, `energy_damage`,
`process` (`shatter`/`disappear`/`none`), `lifetime`, `effect` (id [, сила]), `num_child_pieces`,
`debris_impulse`, `innards_debris_*`, `debris_type`.

## Контракт `ICliObjFactory` / `ICliObj`

Клиентская фабрика — единственный объект `ICliObjFactory` (в exe, `src/freelancer/Client/client_cliobj.*`)
с одним слотом `Create(int TypeId, ICliObj** Out)`; Server передаёт `nullptr`, тогда `Root::Companion == nullptr`
и все ветки компаньона пропускаются. Компаньон — пользовательская часть записи (материалы, LOD,
эффекты попаданий, настройки камеры); слоты `ICliObj`: `AddRef`, `Release`, `ReadUnknown`, `Lod`
(`[LOD]`), `Redefine`, `Bind` (после вставки в базу), `FreeResources`, `Inherit`, `Load`, далее
безымянные `Slot09…13` (деструктор, очистка кэшей). Идентификаторы типа:

| TypeId | Секция | TypeId | Секция |
|---:|---|---:|---|
| 0 | `[Simple]` | 0x15 | `[Light]` |
| 2 | `[Solar]` | 0x16 | `[Tractor]` |
| 3 | `[Ship]` | 0x17 | `[AttachedFX]` |
| 4 | `[Asteroid]`/`[AsteroidMine]` | 0x18 | `[InternalFX]` |
| 5 | `[DynamicAsteroid]` | 0x19 | `[RepairDroid]` |
| 8 | `[LootCrate]` | 0x1A | `[RepairKit]` |
| 9 | `[CargoPod]` | 0x1B | `[ShieldBattery]` |
| 0xA | `[Commodity]` | 0x1C | `[CloakingDevice]` |
| 0xB | `[Power]` | 0x1D | `[TradeLane]` |
| 0xC | `[Engine]` | 0x1F | `[Munition]` |
| 0xD | `[Shield]` | 0x20 | `[Mine]` |
| 0xE | `[ShieldGenerator]` | 0x21 | `[CounterMeasure]` |
| 0xF | `[Thruster]` | 0x22 | `[Armor]` |
| 0x11 | `[Gun]` | 0x12/0x13 | `[MineDropper]`/`[CounterMeasureDropper]` |
| 0x14 | `[Scanner]` | | |

`[Motor]` и `[Explosion]` фабрику не зовут. Компаньон копируется/наследуется вместе с записью
(`Root::operator=` делает `AddRef` нового и `Release` старого — на этом ломался `inherit =` для
оборудования, если присваивать указатель без `AddRef`).

## Меш и физические части

`Root::load` → `Archetype::Load(path)` кладёт меш в индекс `DALib::Engine` (ключ — `DACOM_CRC::GetCRC32`
пути) и записывает `EngineIndex`; затем (кроме `.sph`) `PhyArch::LoadSurfaces` читает соседний `.sur`
(формат — в [common.md](common.md), раздел «Архетипы»). `Munition::load` с пустым `DA_archetype`
использует общий архетип луча `BEAM_ARCH` (`LoadBeamArchIdx` в конце `LoadEquipment`).
`Archetype::Free` обходит базы, освобождает записи и снимает `BEAM_ARCH`; `FreeExplosions` чистит взрывы.

## Известные расхождения

- Донорское копирование при `inherit =` делают `operator=` листовых классов (`EqObj`, `Ship`,
  `Solar`, `Equipment`-семейство: `AttachedFXEquip`, `Launcher`, `Munition`, …), а не общий
  `Root::operator=`.
- Хранилища `fuse`/`docking_sphere`/`shield_link`/`CollisionGroup`/карта физических частей —
  современные контейнеры (часто по указателю в слоте старого VC6-списка), а не VC6-структуры.
- Статус по функциям и конкретные дыры — в decomp-tracker (`Common.dll`).
