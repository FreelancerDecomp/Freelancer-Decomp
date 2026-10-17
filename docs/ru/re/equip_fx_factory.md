# EquippedObj: клиентские FX оборудования

Клиентский игровой объект корабля или солара (`FClientGameobj`, `src/freelancer/Client/client_gameobj.*`,
retail `Client\Space\EquippedObj.cpp`) для каждой единицы оборудования создаёт небольшую
обёртку-уведомитель (`ClientEquip`) и кладёт её в список объекта (`+0xb8`, число — `+0xbc`). Эти
обёртки реагируют на события оборудования (выстрел, попадание, включение), каждый кадр обновляются
и играют звуки, свет и частицы. Документ описывает устройство; статус функций — в decomp-tracker.
Файлы порта: `client_equip_fx.{h,cpp}` (фабрика, очистка, пересборка), `client_gameobj.*`
(инициализация списка), `cli_projectile_fx.*` (выстрел, активация, взрыв), `cli_tractor_beam.cpp`.

## Устройство

- Объект хранит `CEqObj*` (`+0x10`) и допускает оборудование, только если
  `(ObjectClass & 0x103) == 0x103`. Узел списка — `{Next, Prev, Wrapper*}`.
- Обёртка — наследник `ClientEquip`: слот 0 — уведомление (`INotify`), слот 1 — удаляющий деструктор,
  слот 2 — `bool Update(float)`. Обновление объекта зовёт слот 2 каждой обёртки; при ответе
  `false` вызывает деструктор и снимает узел. В порте база — `FClientEquip : INotify`, узел —
  `FEquipFxListNode`, per-type таблицы — обычные виртуальные переопределения.
- **Пересборка** (`EquippedObjRebuildEquipFx`): очистка списка → обход всего оборудования корабля →
  `EquippedObjCreateEquipFx(id)` на каждую единицу → сканирование fuse-воспламенений
  (`Fuse::Load` прототипов через `ClientFuseDB`) → завершающий вызов слота 170 объекта. Зовётся
  из загрузчиков `IObjRW` корабля/солара и из `SpawnPlayerShip` (слот 92 игрового объекта) до
  установки наблюдателя игрока.

## Тип оборудования → обёртка

Фабрика выбирает класс по маске типа `CEquip` (`EEquipType`):

| Тип | Значение | Класс порта | Размер |
|---|---:|---|---:|
| Light | `0x1` | `FCliLightEquipFx` | `0x20` |
| AttachedFX | `0x2` | `FCliAttachedFxEquipFx` | `0x20` |
| MineDropper | `0x20` | `FCliMineDropperFx` | `0x40` |
| CmDropper | `0x40` | `FCliCmDropperFx` | `0x40` |
| Gun | `0x80` | `FCliGunFx` | `0x44` |
| Shield | `0x100` | `FCliShieldEquipFx` | `0x18` |
| ShieldGenerator | `0x200` | `FCliShieldGenEquipFx` | `0x10` |
| Thruster | `0x400` | `FCliThrusterEquipFx` | `0x20` |
| CloakingDevice | `0x1000` | `FCliCloakEquipFx` | `0x10` |
| Engine | `0x20000` | `FCliEngineEquipFx` | `0x60` |
| Tractor | `0x100000` | `FCliTractorEquipFx` | `0x38` |
| RepairDroid | `0x200000` | `FCliRepairDroidEquipFx` | `0x20` |
| InternalFX | `0x400000` | `FCliInternalFxEquipFx` | `0x14` |
| TradeLane | `0x800000` | `FCliTradeLaneEquipFx` | `0x34` |

Семейство оружия (`FCliLauncherFx` — 0x40, общая база мин/ловушек/пушек) имеет собственные
слоты 3–5: `TryFire`, `FireBarrelAim`, `FireDirect`; их единственный внешний вызывающий —
диспетчер выстрела `CliShipFireWeapons`, который затем один раз проигрывает общие
«одиночные» звуки. Пушка — `0x44` (на `+0x40` голос турели, который ни один деструктор не
освобождает). Обёртка щита (`CliShield`, `0x18`) — единственная, кроме оружия, со своими слотами:
перезарядка и снятие коллапса (`collapse`/`rebuilt` звуки генератора), столкновение, попадание
боеприпаса; слоты попадания объекта шлют удар по подобъекту щита (`SubObjectID::IsShieldEquipID`,
тип `0x100`) именно ей. Обёртка tradelane держит два `AnimDB::VChannel`, которые каждый кадр
регистрирует и которые крутят суставы `TLR_Left`/`TLR_Right` по углу вращения.

## Свет

Огни оборудования входят в общую иерархию эффектов (`FEffectBase`), идут через общий пакет
экранных четырёхугольников и рисуются своей таблицей `CliEquipLight` (бакет эффектов `0x2e`);
отдельного драйвера огней в порте нет. `Light`-обёртка реагирует на события `0xa/0xb/0xc` и
активируется отдельным вызовом.

## Цепочка выстрела

`CliShipFireWeapons` (диспетчер: обходит тот же список `+0xb8`) → `TryFire`/`FireDirect` гнезда →
`CliLauncherFireProjectiles` → клиентский игровой объект снаряда (`FClientMunitionGameobj`,
`FClientBeamGameobj`, `FClientProjectileGameobj`) → активация эффектов
(`CliBeamActivateEffects`) → `BeamEffectStart`. Клиентский вход выстрела:
`EquippedObjHandleEquipMsg` (операции `0x18` — огонь, `0x19` — активация оборудования) ←
`CliShipPreprocessEquipUpdate` ← `CliShipHandleEquipUpdate`. Серверная сторона —
`IServerImpl::FireWeapon`.

## Известные пробелы

- Часть веток требует кода, которого в порте нет: ammo-UI и сетевой хвост выстрела
  (`DAT_0067ecd0`, `FUN_005532d0`), `FUN_004c4320` (`ObjectShip`-хвост снятия оборудования
  игрока), флаги/`BaseWatcher` в слоте 170.
- Остальной статус — в tracker (поиск по `EquippedObj`, `epic:fuse`, `epic:sound`).
