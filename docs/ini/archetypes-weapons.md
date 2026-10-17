# Weapon equipment sections and hardpoint-type names

This page documents the equipment sections that describe weapons and projectiles (`Gun`, `MineDropper`, `CounterMeasureDropper`, `Munition`, `Mine`, `CounterMeasure`) and the table of hardpoint-type names that guns, shield generators, munitions and ships accept. The keys shared by all records are in [archetypes-and-equipment.md](archetypes-and-equipment.md); ships are in [archetypes-ships-solars-asteroids.md](archetypes-ships-solars-asteroids.md).

**File location / how it is found.** These sections live in the equipment files listed by the repeated `equipment` key of `freelancer.ini` `[Data]` (in the dev ini: `equipment\weapon_equip.ini` and `equipment\prop_equip.ini` among others); the loader is `Archetype::LoadEquipment` and accepts any equipment section in any of those files ([freelancer-ini.md](freelancer-ini.md)). Sections and keys are case-insensitive. Value notation (`int`, `float`, `bool`, `id`, `cstr`) is explained in [archetypes-and-equipment.md](archetypes-and-equipment.md#value-notation-used-on-these-pages).

Weapon dependency order matters: a `Gun`'s `projectile_archetype` and a `Munition`'s `motor` are plain ids that are resolved when the weapon range is computed or the weapon is used, not while the file is read, but `inherit` and `loot_appearance` are resolved while reading (the target must already be loaded).

## Launcher keys (`Gun`, `MineDropper`, `CounterMeasureDropper`)

All three sections are `Launcher` records: `Root` + `Equipment` + `AttachedEquipment` keys (see the equipment page) plus the keys below ([common_archetype_weapons.cpp:60](../../src/Common/common_archetype_weapons.cpp#L60)).

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `damage_per_fire` | float | 0.0 | Damage value of one shot (informational for AI/UI). | [L65](../../src/Common/common_archetype_weapons.cpp#L65) |
| `power_usage` | float | 0.0 | Energy per shot. | [L71](../../src/Common/common_archetype_weapons.cpp#L71) |
| `refire_delay` | float | 0.0 | Seconds between shots. | [L77](../../src/Common/common_archetype_weapons.cpp#L77) |
| `muzzle_velocity` | float | 0.0 | Projectile launch speed. | [L83](../../src/Common/common_archetype_weapons.cpp#L83) |
| `projectile_archetype` | id (whole value) | 0 | Nickname of the `Munition` / `Mine` / `CounterMeasure` fired. | [L89](../../src/Common/common_archetype_weapons.cpp#L89) |
| `use_animation` | cstr (whole value) | empty | Animation script played when the launcher fires. | [L95](../../src/Common/common_archetype_weapons.cpp#L95) |

`MineDropper` has no other keys.

## `[Gun]`  ([common_archetype_weapons.cpp:266](../../src/Common/common_archetype_weapons.cpp#L266))

Launcher keys plus:

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `dispersion_angle` | float, degrees | 0.0 | Converted to radians (multiplied by 0.017453292) on read. | [L271](../../src/Common/common_archetype_weapons.cpp#L271) |
| `auto_turret` | bool | false | Gun is a turret (its hardpoint type becomes `hp_turret`). | [L279](../../src/Common/common_archetype_weapons.cpp#L279) |
| `turn_rate` | float, degrees/s | 15 deg (0.2617994 rad) | Converted to radians on read. | [L285](../../src/Common/common_archetype_weapons.cpp#L285) |
| `gun_azimuth` | float `[0]` min, float `[1]` max, degrees | 0, 0 (limits not valid) | Stored as given (no radian conversion). The limits become "valid" only if `max - min` is between 1 and 360 inclusive; otherwise the values are kept but the validity flag stays off. | [L292](../../src/Common/common_archetype_weapons.cpp#L292) |
| `gun_elevation` | float `[0]` min, float `[1]` max, degrees | 0, 0 (limits not valid) | Same rule as `gun_azimuth`. | [L308](../../src/Common/common_archetype_weapons.cpp#L308) |
| `hp_gun_type` | list of hardpoint-type names (`str[0..n]`) | empty list | Hardpoint types the gun fits, one per parameter up to the first empty one. A name that is not in the table is skipped silently. Repeated keys **append**. An empty list means the built-in list of 23 types (`hp_gun`, `hp_gun_special_1..10`, `hp_turret_special_1..10`, `hp_torpedo_special_1..2`). | [L320](../../src/Common/common_archetype_weapons.cpp#L320) |

The *effective* hardpoint type reported for a gun is `hp_turret` when `auto_turret` is true, otherwise "none" when `projectile_archetype` is empty, else the `HpType` of its `Munition` (looked up through `projectile_archetype`, with no check for a missing record).

## `[CounterMeasureDropper]`  ([L420](../../src/Common/common_archetype_weapons.cpp#L420))

Launcher keys plus:

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `ai_range` | float | 0.0 | AI trigger range; **stored squared** (`ai_range * ai_range`). | [L425](../../src/Common/common_archetype_weapons.cpp#L425) |

## Projectile keys (`Munition`, `Mine`, `CounterMeasure`)

`Projectile` is a `Root` + `Equipment` record (not attached equipment) ([L468](../../src/Common/common_archetype_weapons.cpp#L468)).

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `lifetime` | float | 0.0 | Seconds the projectile lives. | [L473](../../src/Common/common_archetype_weapons.cpp#L473) |
| `owner_safe_time` | float | -1.0 | Seconds during which the projectile cannot hit its owner. | [L479](../../src/Common/common_archetype_weapons.cpp#L479) |
| `requires_ammo` | bool | false (`Mine` and `CounterMeasure`: true) | Firing consumes ammunition. | [L485](../../src/Common/common_archetype_weapons.cpp#L485) |
| `force_gun_ori` | bool | false | Projectile takes the gun's orientation. | [L491](../../src/Common/common_archetype_weapons.cpp#L491) |
| `loot_appearance` | equipment nickname | none (the projectile itself) | Equipment whose model is shown as loot (resolved at read time). | [helper L145](../../src/Common/common_archetype.cpp#L145), [call L497](../../src/Common/common_archetype_weapons.cpp#L497) |

## `[Munition]`  ([L556](../../src/Common/common_archetype_weapons.cpp#L556))

Projectile keys plus the following. Object type is preset to `BULLET`.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `damage` or `hull_damage` | float | 0.0 | Hull damage. Both spellings set the same field. | [L561](../../src/Common/common_archetype_weapons.cpp#L561) |
| `energy_damage` | float | 0.0 | Damage to shields/energy. | [L567](../../src/Common/common_archetype_weapons.cpp#L567) |
| `weapon_type` | id (whole value) | 0 | Weapon-type record (damage multiplier table). | [L573](../../src/Common/common_archetype_weapons.cpp#L573) |
| `motor` | id (whole value) | 0 | `[Motor]` nickname; empty means no motor (range = `lifetime * muzzle_velocity`). | [L579](../../src/Common/common_archetype_weapons.cpp#L579) |
| `seeker` | `dumb` \| `lock` | none (0) | `dumb` = 1, `lock` = 2 (case-insensitive), any other value = 0 (not seeking). A non-zero seeker changes the object type to `GUIDED`, otherwise `BULLET`. | [L585](../../src/Common/common_archetype_weapons.cpp#L585) |
| `time_to_lock` | float | 0.0 | Seconds needed to lock. | [L603](../../src/Common/common_archetype_weapons.cpp#L603) |
| `seeker_range` | float | 0.0 | Lock range. | [L609](../../src/Common/common_archetype_weapons.cpp#L609) |
| `seeker_fov_deg` | float, degrees | cos 0 = 1.0 | **Stored as the cosine** of the angle in radians. | [L615](../../src/Common/common_archetype_weapons.cpp#L615) |
| `max_angular_velocity` | float | 100.0 | Turn rate limit of a guided projectile. | [L625](../../src/Common/common_archetype_weapons.cpp#L625) |
| `detonation_dist` | float | 0.0 | Proximity fuse distance; **stored squared**. | [L631](../../src/Common/common_archetype_weapons.cpp#L631) |
| `hp_type` | hardpoint-type name (`str[0]`) | `hp_gun` (4) | Hardpoint type the munition belongs to. A name that is not in the table also gives 4. | [L639](../../src/Common/common_archetype_weapons.cpp#L639) |
| `cruise_disruptor` | bool | false | Munition disrupts cruise. | [L645](../../src/Common/common_archetype_weapons.cpp#L645) |

Example (synthetic):

```ini
[Munition]
nickname = demo_missile
hull_damage = 300
seeker = lock
seeker_range = 1500
seeker_fov_deg = 30
time_to_lock = 2
motor = demo_motor
lifetime = 6
hp_type = hp_torpedo
```

## `[Mine]`  ([L783](../../src/Common/common_archetype_weapons.cpp#L783))

Projectile keys plus (object type preset to `MINE`):

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `linear_drag` | float | 0.0 | Drag. | [L788](../../src/Common/common_archetype_weapons.cpp#L788) |
| `detonation_dist` | float | 0.0 | Detonation distance (stored as read, not squared). | [L794](../../src/Common/common_archetype_weapons.cpp#L794) |
| `seek_dist` | float | 0.0 | Distance at which the mine starts to home in. | [L800](../../src/Common/common_archetype_weapons.cpp#L800) |
| `acceleration` | float | -1.0 | Acceleration. | [L806](../../src/Common/common_archetype_weapons.cpp#L806) |
| `top_speed` | float | 40.0 | Maximum speed. | [L812](../../src/Common/common_archetype_weapons.cpp#L812) |

## `[CounterMeasure]`  ([L870](../../src/Common/common_archetype_weapons.cpp#L870))

Projectile keys plus:

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `linear_drag` | float | 0.0 | Drag. | [L875](../../src/Common/common_archetype_weapons.cpp#L875) |
| `range` | float | 0.0 | Effect radius. | [L881](../../src/Common/common_archetype_weapons.cpp#L881) |
| `diversion_pctg` | int (float truncated) | 0 | Chance (percent) that a missile is diverted. | [L887](../../src/Common/common_archetype_weapons.cpp#L887) |

## Hardpoint-type names

Table `HpTypeTable` ([common_archetype.cpp:40](../../src/Common/common_archetype.cpp#L40)); 63 entries, matched case-insensitively with the first match winning. Used by `Gun hp_gun_type`, `Munition hp_type`, `ShieldGenerator hp_type` / `hp_shield_type` and `Ship hp_type` (the ship's first parameter). The numeric value is stored in the record.

| Name | Value | Name | Value |
|---|---|---|---|
| `hp_gun` | 4 | `hp_shield_generator` | 5 |
| `hp_fighter_shield_generator` | 6 | `hp_elite_shield_generator` | 7 |
| `hp_freighter_shield_generator` | 8 | `hp_thruster` | 9 |
| `hp_torpedo` | 0xa | `hp_mine_dropper` | 0xb |
| `hp_countermeasure_dropper` | 0xc | `hp_turret` | 0xd |
| `hp_cargo_pod` | 0xe | `hp_gun_special_1` ... `hp_gun_special_10` | 0xf ... 0x18 |
| `hp_turret_special_1` ... `hp_turret_special_10` | 0x19 ... 0x22 | `hp_torpedo_special_2` | 0x23 |
| `hp_torpedo_special_1` | 0x24 | `hp_fighter_shield_special_1` ... `_10` | 0x25 ... 0x2e |
| `hp_elite_shield_special_1` ... `_10` | 0x2f ... 0x38 | `hp_freighter_shield_special_1` ... `_10` | 0x39 ... 0x42 |

Note the swapped order of the two torpedo specials (`_1` = 0x24, `_2` = 0x23); the value 0 is not in the table (a `Ship hp_type` whose name is missing is skipped).
