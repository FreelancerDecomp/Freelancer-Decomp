# Карточка оборудования у дилера

Когда игрок выбирает товар в окне покупки/продажи (оборудование, корабль), окно обновляет панель
описания: название, текст из IDS и блок характеристик. Выбор обрабатывает
`NnInventory::ShowInfoCard(ArchId, Ids)` (`src/freelancer/Client/nn_inventory.cpp`, retail
`FUN_0047ba90`; окно торговли — `NN_Inventory` / `NN_Dealer`), текст характеристик строит
`BuildDealerEquipmentStatText` (`dealer_equip_stats.{h,cpp}`, retail `FUN_00484090`). Статус функций
и недоделки — в decomp-tracker.

## Раскладка панелей

Панели `RichTextWin`: `+0x398` / `+0x39c` / `+0x3a0` — `InfoCard_Facts` / `InfoCard_Stats` /
`InfoCard_Values`, `+0x4bc` — `InfoCard_Info`. Генератор характеристик зовётся для одного и того же
`ArchId` в трёх режимах, различающихся флагами `(bEmitLabels, bEmitValues)`:

| Режим | Флаги | Назначение |
|---|---|---|
| колонка подписей | `(1, 0)` | левая колонка двухколоночной раскладки |
| колонка значений | `(0, 1)` | правая колонка |
| полный блок | `(1, 1)` | одиночная панель |

Двухколоночный путь выбирается для классов `ShieldGenerator`, `Thruster`, `Gun`, `MineDropper`,
`CounterMeasureDropper`, `RepairKit`, `ShieldBattery`, `Munition`, `Mine`, `CounterMeasure` — ровно
тех, которые умеет форматировать генератор — и только если есть панели-колонки. Для кораблей
(`+0x4e0 != 0`) описание берётся из `IdsInfo1/2/3` корабля, а блок характеристик не строится. Блок
сравнения «цены во всех базах» для товаров — `AppendCommodityTradeReport`
(`commodity_by_base.cpp`, retail `FUN_004704d0`).

## Генератор характеристик

Класс — `Equipment::get_class_type()`; значения вне перечисленных классов дают одну пустую строку.
Все надписи — строки IDS с поиском модуля по старшему слову (`FUN_004347e0`-аналог); числа
печатаются усечением к нулю (`_ftol`) в форматах `%d`, `%0.2f`, `%d%%`; урон энергией всегда
пересчитывается через `ShieldEquipConsts::HULL_DAMAGE_FACTOR`.

| Класс | Строки |
|---|---|
| ShieldGenerator | класс и уровень по типу гнезда, ёмкость, регенерация, мощность перестройки |
| Thruster | расход энергии, оценка скорости `(MaxThrust + 48000) / 600` |
| Gun | класс и уровень (по максимуму типа гнезда), урон корпусу, урон энергии, две дальности, скорострельность `1/RefireDelay`, расход энергии либо угловая скорость самонаведения |
| MineDropper | урон корпусу и энергии, максимальная скорость мины; на этом строки кончаются |
| CounterMeasureDropper, CounterMeasure | дальность, процент отвлечения |
| RepairKit, ShieldBattery | `Root::HitPts` |
| Munition, Mine (как товар) | урон корпусу и энергии, радиус взрыва |

Класс и уровень оружия/щита определяет тип гнезда: полосы GunSpecial1–10, TurretSpecial1–10,
FighterShieldSpecial1–10, EliteShieldSpecial1–10, FreighterShieldSpecial1–10 дают класс (IDS `0x395`…
`0x399`) и уровень `тип − начало полосы + 1`; остальные типы пропускают обе строки.

Особенность retail, воспроизведённая в порте: для управляемого боеприпаса (у `Munition.Motor` время
жизни больше нуля) первая дальность показывает `SeekerRange` — остаток на стеке FPU после `FSTP`
полной формулы, а не посчитанное значение.

Раскладку подписей IDS (`0x6cf`…`0x6e0`, `0x3b1`, `0x3c6`, `0x3d1`) читать в
`dealer_equip_stats.cpp` — таблица там единственный источник; смещения полей `Launcher`, `Munition`,
`Mine`, `CounterMeasure`, `ShieldGenerator`, `Thruster`, `Explosion`, `MotorData` — в
`src/include/common_archetype.h`.
