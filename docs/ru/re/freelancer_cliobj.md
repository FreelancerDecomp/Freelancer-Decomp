# Клиентские компаньоны архетипов (`Archetype::ICliObj`)

Каждая запись архетипа, которую `Common.dll` грузит на клиенте, получает «компаньона» —
клиентский объект с данными, нужными только `freelancer.exe`: материалы и LOD, эффекты попаданий,
настройки камеры корабля, звуки и огни оборудования. Фабрика и все компаньоны — код exe
(`src/freelancer/Client/client_cliobj.*`, retail `Client\cmn\CliArch.cpp`, `ArchLod.cpp`); границу
exe ↔ Common пересекают только интерфейсы `ICliObj` (`src/include/common_archetype.h`) и
`ICliObjFactory` (`src/include/common_cliobj.h`). Контракт Common-стороны (какой `TypeId` для какой
секции, когда зовётся какой слот) — [common_archetype.md](common_archetype.md).

Не путать с другим семейством `Cli*` в exe — рантайм-объектами в космосе (`ClientEquip`, `CliGun`,
`CliEngine`, `SpaceDustPerf`, …, retail `Client\Space\*.cpp`): они живые, а не компаньоны `Root+0x04`
(см. [equip_fx_factory.md](equip_fx_factory.md)).

## Иерархия

```
ICliObj
├── CliRoot                      база, 0x54 байта
├── CliShip, CliSolar            0x88: камера корабля / DEFAULT_TEXTURE
├── CliProj ── CliMunition       снаряды (0x60 / 0x84)
├── CliAsteroid, CliDynamicAsteroid
└── CliEQEngine, CliEQShieldGenerator, CliEQThruster, CliEQLauncher, CliEQGun, CliEQLight,
    CliEQTractor, CliEQAttachedFX, CliEQInternalFX, CliEQTradeLane, CliEQRepairDroid, CliEQShield
ICliObjFactory ── CliFactory     синглтон (DAT_006164a0), слот 0 = Create(TypeId, ICliObj**)
```

Имена классов подтверждены RTTI и `__RTDynamicCast` в `Inherit`. Фабрика по `TypeId` (список —
в common_archetype.md) создаёт объект своего размера; `CliShip`/`CliSolar` — 0x88, `CliEQEngine` —
0xAC, `CliEQGun` — 0x88, `CliEQLauncher` (дропперы мин/ловушек) — 0x84, `CliEQLight` — 0x98,
`CliEQTradeLane` — 0xC8, `CliMunition` — 0x84; остальные — от 0x54 до 0x70. В порте каждый
retail-класс — свой класс (`FCliShip`, `FCliMunition`, `FCliEngine`, `FCliGun`, `FCliEQLight`,
`FCliTradeLane`, …), без общих `FCliSized<N>`-заглушек.

## Слоты базы (14, vtable `005c7734`)

| Слот | Роль |
|---:|---|
| 0, 1 | `AddRef`, `Release` (при нуле: `Slot11`, затем деструктор) |
| 2 | `ReadUnknown` — ключ, неизвестный `Root::read` |
| 3 | `Lod` — секция `[LOD]` |
| 4 | `Redefine` — копирование компаньона-донора |
| 5 | `Bind` — после вставки записи в `ArchDB` |
| 6 | `FreeResources` (→ `Slot11`) |
| 7 | `Inherit` — `inherit =`; база почти пуста, подклассы переопределяют |
| 8 | `Load` — загрузка ресурсов (при `Root::load`) |
| 9–13 | безымянные: удаляющий деструктор, снятие кэша разрешённых ID и эффектов, обход записей дистанций |

Имена 2–8 — каталог порта (в PE строк нет), роли подтверждены местами вызова из `Archetype::Load*`.

## База (`CliRoot`, 0x54 байта)

| Поле | Содержимое |
|---|---|
| `+0x04` | голова списка `[LOD]` (узлы 0x34: хэш `obj`, до 8 диапазонов `LODranges`, `MinSpecLOD`) |
| `+0x08`, `+0x0C` | `DistanceRender` и кэш `LodPick` (по умолчанию −1) |
| `+0x10` | `CacheString envmap_material` |
| `+0x14`, `+0x18` | `Root*` (после `Bind`) и счётчик ссылок |
| `+0x20…+0x3C` | пути материалов (`{path, libId}`, шаг 8) и вектор разрешённых записей материалов |
| `+0x40…+0x4C` | записи дистанций `surface_hit_effects` (шаг 0x24: порог, ID эффектов, живые `EffectArch*`) |
| `+0x50` | флаги состояния (кэш построен, `Slot10` сделан, сторона движка, `Slot12` сделан, `LodPick` посчитан) |

Ключи базы (`ReadUnknown`): `material_library` (путь [, номер]), `LODranges`, `MinSpecLOD`,
`distance_render`, `envmap_material`, `surface_hit_effects` (порог + список ID эффектов); неизвестный
ключ — `false`, и подкласс может продолжить. Особенность retail, воспроизведённая в порте:
пересчёт `LodPick` при не загруженном `Root` выходит рано и **не** трогает старое значение.

## Подклассы

- **`CliShip`** (0x88): `camera_offset` (2 float), `camera_angular_acceleration`,
  `camera_angular_slerp_multiplier`, углы поворота (радианы после зажима),
  `camera_turn_look_ahead_slerp_amount` (по умолчанию 0.5), cockpit-ini как `CacheString`; `Redefine`
  копирует LOD и поля камеры, `Inherit` вызывает базу.
- **`CliSolar`** (0x88): `DEFAULT_TEXTURE` (inline, до 31 символа).
- **`CliProj` / `CliMunition`**: `const_effect_delay`, `const_effect`, `one_shot_sound`,
  `HP_trail_parent`, список `munition_hit_effect` и вектор живых `EffectArch*`; `Load` разрешает
  эффекты по ID (`GEffectArchById`), `Slot11` их снимает, выбор живого эффекта — `PickLiveEffect`.
- **`CliEQ*`**: каждый класс читает свои ключи секции оборудования (двигатель — эффекты и звуки
  тяги, `Light` — параметры огня и свечения, `TradeLane` — прокрутка колец, `ShieldGenerator` —
  `shield_hit_effects`, `Launcher`/`Gun` загружают компаньона снаряда).

## Жизненный цикл

`DataConfig::startup` → `Archetype::Load*` → `Factory->Create` → `new Root`-потомок с компаньоном →
INI (`ReadUnknown`/`Lod`) → вставка в `ArchDB` → `Bind`; `inherit =` → `Inherit(donor)`;
`Root::load` → `Load(mode)`; `Root::free_resources` → `FreeResources`/`Release`. Копирование записи
`Root::operator=` обязано делать `AddRef` нового и `Release` старого компаньона (иначе первый же
наследник уничтожает компаньона донора).

## Известные пробелы

- Первая загрузка VisEffect `.ale` (`LoadAlePath`) принадлежит VisEffectArch/alchemy, EffectArch
  только соблюдает контракт счётчиков ссылок.
- `Inherit`/`Load` отдельных `CliEQ*` и имена листьев `Mine`/`CounterMeasure` (`TypeId 0x20/0x21`)
  уточняются по tracker.
