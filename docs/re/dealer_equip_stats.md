# Dealer equipment info card

When the player selects an item in the buy/sell window (equipment, ship), the window updates the
description panel: name, IDS text and a block of statistics. The selection is handled by
`NnInventory::ShowInfoCard(ArchId, Ids)` (`src/freelancer/Client/nn_inventory.cpp`, retail
`FUN_0047ba90`; the trade window is `NN_Inventory` / `NN_Dealer`); the statistics text is built by
`BuildDealerEquipmentStatText` (`dealer_equip_stats.{h,cpp}`, retail `FUN_00484090`). Function status
and gaps are in decomp-tracker.

## Panel layout

`RichTextWin` panels: `+0x398` / `+0x39c` / `+0x3a0` are `InfoCard_Facts` / `InfoCard_Stats` /
`InfoCard_Values`, `+0x4bc` is `InfoCard_Info`. The statistics generator is called for the same
`ArchId` in three modes, distinguished by the flags `(bEmitLabels, bEmitValues)`:

| Mode | Flags | Purpose |
|---|---|---|
| label column | `(1, 0)` | left column of the two-column layout |
| value column | `(0, 1)` | right column |
| full block | `(1, 1)` | single panel |

The two-column path is chosen for the classes `ShieldGenerator`, `Thruster`, `Gun`, `MineDropper`,
`CounterMeasureDropper`, `RepairKit`, `ShieldBattery`, `Munition`, `Mine`, `CounterMeasure` — exactly
those the generator can format — and only if the column panels exist. For ships
(`+0x4e0 != 0`) the description is taken from the ship's `IdsInfo1/2/3`, and no statistics block is
built. The "prices at all bases" comparison block for commodities is `AppendCommodityTradeReport`
(`commodity_by_base.cpp`, retail `FUN_004704d0`).

## Statistics generator

The class is `Equipment::get_class_type()`; values outside the listed classes yield a single empty line.
All labels are IDS strings with the module looked up by the high word (a `FUN_004347e0` analogue); numbers
are printed truncated toward zero (`_ftol`) in the formats `%d`, `%0.2f`, `%d%%`; energy damage is always
recomputed through `ShieldEquipConsts::HULL_DAMAGE_FACTOR`.

| Class | Lines |
|---|---|
| ShieldGenerator | class and level by hardpoint type, capacity, regeneration, rebuild power |
| Thruster | energy usage, speed estimate `(MaxThrust + 48000) / 600` |
| Gun | class and level (by the maximum of the hardpoint type), hull damage, energy damage, two ranges, refire rate `1/RefireDelay`, energy usage or homing turn rate |
| MineDropper | hull and energy damage, maximum mine speed; the lines end there |
| CounterMeasureDropper, CounterMeasure | range, diversion percentage |
| RepairKit, ShieldBattery | `Root::HitPts` |
| Munition, Mine (as a commodity) | hull and energy damage, explosion radius |

The class and level of a weapon/shield are determined by the hardpoint type: the ranges GunSpecial1–10, TurretSpecial1–10,
FighterShieldSpecial1–10, EliteShieldSpecial1–10, FreighterShieldSpecial1–10 give the class (IDS `0x395`…
`0x399`) and the level `type − range start + 1`; other types skip both lines.

A retail quirk reproduced in the port: for a guided munition (`Munition.Motor` lifetime greater than zero) the first
range shows `SeekerRange` — the leftover on the FPU stack after `FSTP`
of the full formula, not the computed value.

For the layout of the IDS labels (`0x6cf`…`0x6e0`, `0x3b1`, `0x3c6`, `0x3d1`) see
`dealer_equip_stats.cpp` — the table there is the only source; the field offsets of `Launcher`, `Munition`,
`Mine`, `CounterMeasure`, `ShieldGenerator`, `Thruster`, `Explosion`, `MotorData` are in
`src/include/common_archetype.h`.
