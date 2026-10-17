# Секции оружейного оборудования и имена типов hardpoint

Эта страница описывает секции оборудования, задающие оружие и снаряды (`Gun`, `MineDropper`, `CounterMeasureDropper`, `Munition`, `Mine`, `CounterMeasure`), и таблицу имён типов hardpoint, которые принимают орудия, генераторы щитов, боеприпасы и корабли. Ключи, общие для всех записей, описаны в [archetypes-and-equipment.md](archetypes-and-equipment.md); корабли — в [archetypes-ships-solars-asteroids.md](archetypes-ships-solars-asteroids.md).

**Расположение файлов / как они находятся.** Эти секции лежат в файлах оборудования, перечисленных повторяющимся ключом `equipment` в `freelancer.ini` `[Data]` (в dev ini, среди прочих, это `equipment\weapon_equip.ini` и `equipment\prop_equip.ini`); загрузчик — `Archetype::LoadEquipment`, он принимает любую секцию оборудования в любом из этих файлов ([freelancer-ini.md](freelancer-ini.md)). Имена секций и ключей не зависят от регистра. Обозначения значений (`int`, `float`, `bool`, `id`, `cstr`) объяснены в [archetypes-and-equipment.md](archetypes-and-equipment.md#обозначения-значений-на-этих-страницах).

Порядок зависимостей оружия важен: `projectile_archetype` у `Gun` и `motor` у `Munition` — простые id, которые разрешаются при вычислении дальности оружия или при его использовании, а не при чтении файла, тогда как `inherit` и `loot_appearance` разрешаются при чтении (цель должна быть уже загружена).

## Ключи пусковых устройств (`Gun`, `MineDropper`, `CounterMeasureDropper`)

Все три секции — записи `Launcher`: ключи `Root` + `Equipment` + `AttachedEquipment` (см. страницу оборудования) плюс ключи ниже ([common_archetype_weapons.cpp:60](../../../src/Common/common_archetype_weapons.cpp#L60)).

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `damage_per_fire` | float | 0.0 | Урон одного выстрела (информационно для AI/UI). | [L65](../../../src/Common/common_archetype_weapons.cpp#L65) |
| `power_usage` | float | 0.0 | Энергия на выстрел. | [L71](../../../src/Common/common_archetype_weapons.cpp#L71) |
| `refire_delay` | float | 0.0 | Секунды между выстрелами. | [L77](../../../src/Common/common_archetype_weapons.cpp#L77) |
| `muzzle_velocity` | float | 0.0 | Начальная скорость снаряда. | [L83](../../../src/Common/common_archetype_weapons.cpp#L83) |
| `projectile_archetype` | id (всё значение) | 0 | Nickname выпускаемого `Munition` / `Mine` / `CounterMeasure`. | [L89](../../../src/Common/common_archetype_weapons.cpp#L89) |
| `use_animation` | cstr (всё значение) | пусто | Скрипт анимации, проигрываемый при выстреле пускового устройства. | [L95](../../../src/Common/common_archetype_weapons.cpp#L95) |

У `MineDropper` других ключей нет.

## `[Gun]`  ([common_archetype_weapons.cpp:266](../../../src/Common/common_archetype_weapons.cpp#L266))

Ключи пускового устройства плюс:

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `dispersion_angle` | float, градусы | 0.0 | При чтении переводится в радианы (умножается на 0.017453292). | [L271](../../../src/Common/common_archetype_weapons.cpp#L271) |
| `auto_turret` | bool | false | Орудие является турелью (тип его hardpoint становится `hp_turret`). | [L279](../../../src/Common/common_archetype_weapons.cpp#L279) |
| `turn_rate` | float, градусы/с | 15 град. (0.2617994 рад) | При чтении переводится в радианы. | [L285](../../../src/Common/common_archetype_weapons.cpp#L285) |
| `gun_azimuth` | float `[0]` минимум, float `[1]` максимум, градусы | 0, 0 (пределы не действительны) | Хранится как задано (без перевода в радианы). Пределы становятся «действительными» только если `max - min` лежит между 1 и 360 включительно; иначе значения сохраняются, но флаг действительности остаётся выключенным. | [L292](../../../src/Common/common_archetype_weapons.cpp#L292) |
| `gun_elevation` | float `[0]` минимум, float `[1]` максимум, градусы | 0, 0 (пределы не действительны) | То же правило, что у `gun_azimuth`. | [L308](../../../src/Common/common_archetype_weapons.cpp#L308) |
| `hp_gun_type` | список имён типов hardpoint (`str[0..n]`) | пустой список | Типы hardpoint, на которые подходит орудие, по одному на параметр до первого пустого. Имя, которого нет в таблице, молча пропускается. Повторяющиеся ключи **добавляются**. Пустой список означает встроенный список из 23 типов (`hp_gun`, `hp_gun_special_1..10`, `hp_turret_special_1..10`, `hp_torpedo_special_1..2`). | [L320](../../../src/Common/common_archetype_weapons.cpp#L320) |

*Эффективный* тип hardpoint, сообщаемый для орудия: `hp_turret`, если `auto_turret` истинно, иначе «нет», когда `projectile_archetype` пуст, иначе `HpType` его `Munition` (ищется через `projectile_archetype`, без проверки на отсутствующую запись).

## `[CounterMeasureDropper]`  ([L420](../../../src/Common/common_archetype_weapons.cpp#L420))

Ключи пускового устройства плюс:

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `ai_range` | float | 0.0 | Дальность срабатывания для AI; **хранится в квадрате** (`ai_range * ai_range`). | [L425](../../../src/Common/common_archetype_weapons.cpp#L425) |

## Ключи снарядов (`Munition`, `Mine`, `CounterMeasure`)

`Projectile` — запись `Root` + `Equipment` (не присоединяемое оборудование) ([L468](../../../src/Common/common_archetype_weapons.cpp#L468)).

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `lifetime` | float | 0.0 | Секунды жизни снаряда. | [L473](../../../src/Common/common_archetype_weapons.cpp#L473) |
| `owner_safe_time` | float | -1.0 | Секунды, в течение которых снаряд не может поразить владельца. | [L479](../../../src/Common/common_archetype_weapons.cpp#L479) |
| `requires_ammo` | bool | false (`Mine` и `CounterMeasure`: true) | Выстрел расходует боеприпасы. | [L485](../../../src/Common/common_archetype_weapons.cpp#L485) |
| `force_gun_ori` | bool | false | Снаряд принимает ориентацию орудия. | [L491](../../../src/Common/common_archetype_weapons.cpp#L491) |
| `loot_appearance` | nickname оборудования | нет (сам снаряд) | Оборудование, чья модель показывается как добыча (разрешается при чтении). | [хелпер L145](../../../src/Common/common_archetype.cpp#L145), [вызов L497](../../../src/Common/common_archetype_weapons.cpp#L497) |

## `[Munition]`  ([L556](../../../src/Common/common_archetype_weapons.cpp#L556))

Ключи снаряда плюс следующие. Тип объекта заранее задан как `BULLET`.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `damage` или `hull_damage` | float | 0.0 | Урон корпусу. Оба написания задают одно и то же поле. | [L561](../../../src/Common/common_archetype_weapons.cpp#L561) |
| `energy_damage` | float | 0.0 | Урон щитам/энергии. | [L567](../../../src/Common/common_archetype_weapons.cpp#L567) |
| `weapon_type` | id (всё значение) | 0 | Запись типа оружия (таблица множителей урона). | [L573](../../../src/Common/common_archetype_weapons.cpp#L573) |
| `motor` | id (всё значение) | 0 | Nickname `[Motor]`; пусто означает отсутствие двигателя (дальность = `lifetime * muzzle_velocity`). | [L579](../../../src/Common/common_archetype_weapons.cpp#L579) |
| `seeker` | `dumb` \| `lock` | нет (0) | `dumb` = 1, `lock` = 2 (без учёта регистра), любое другое значение = 0 (не самонаводится). Ненулевой seeker меняет тип объекта на `GUIDED`, иначе `BULLET`. | [L585](../../../src/Common/common_archetype_weapons.cpp#L585) |
| `time_to_lock` | float | 0.0 | Секунды, нужные для захвата цели. | [L603](../../../src/Common/common_archetype_weapons.cpp#L603) |
| `seeker_range` | float | 0.0 | Дальность захвата. | [L609](../../../src/Common/common_archetype_weapons.cpp#L609) |
| `seeker_fov_deg` | float, градусы | cos 0 = 1.0 | **Хранится как косинус** угла в радианах. | [L615](../../../src/Common/common_archetype_weapons.cpp#L615) |
| `max_angular_velocity` | float | 100.0 | Предел скорости поворота управляемого снаряда. | [L625](../../../src/Common/common_archetype_weapons.cpp#L625) |
| `detonation_dist` | float | 0.0 | Расстояние неконтактного взрывателя; **хранится в квадрате**. | [L631](../../../src/Common/common_archetype_weapons.cpp#L631) |
| `hp_type` | имя типа hardpoint (`str[0]`) | `hp_gun` (4) | Тип hardpoint, которому принадлежит боеприпас. Имя, которого нет в таблице, тоже даёт 4. | [L639](../../../src/Common/common_archetype_weapons.cpp#L639) |
| `cruise_disruptor` | bool | false | Боеприпас срывает круиз. | [L645](../../../src/Common/common_archetype_weapons.cpp#L645) |

Пример (синтетический):

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

## `[Mine]`  ([L783](../../../src/Common/common_archetype_weapons.cpp#L783))

Ключи снаряда плюс (тип объекта заранее задан как `MINE`):

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `linear_drag` | float | 0.0 | Сопротивление. | [L788](../../../src/Common/common_archetype_weapons.cpp#L788) |
| `detonation_dist` | float | 0.0 | Расстояние детонации (хранится как прочитано, не в квадрате). | [L794](../../../src/Common/common_archetype_weapons.cpp#L794) |
| `seek_dist` | float | 0.0 | Расстояние, на котором мина начинает самонаведение. | [L800](../../../src/Common/common_archetype_weapons.cpp#L800) |
| `acceleration` | float | -1.0 | Ускорение. | [L806](../../../src/Common/common_archetype_weapons.cpp#L806) |
| `top_speed` | float | 40.0 | Максимальная скорость. | [L812](../../../src/Common/common_archetype_weapons.cpp#L812) |

## `[CounterMeasure]`  ([L870](../../../src/Common/common_archetype_weapons.cpp#L870))

Ключи снаряда плюс:

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `linear_drag` | float | 0.0 | Сопротивление. | [L875](../../../src/Common/common_archetype_weapons.cpp#L875) |
| `range` | float | 0.0 | Радиус действия. | [L881](../../../src/Common/common_archetype_weapons.cpp#L881) |
| `diversion_pctg` | int (усечённый float) | 0 | Вероятность (в процентах) отклонения ракеты. | [L887](../../../src/Common/common_archetype_weapons.cpp#L887) |

## Названия типов hardpoint

Таблица `HpTypeTable` ([common_archetype.cpp:40](../../../src/Common/common_archetype.cpp#L40)); 63 записи, сравнение без учёта регистра, побеждает первое совпадение. Используется в `Gun hp_gun_type`, `Munition hp_type`, `ShieldGenerator hp_type` / `hp_shield_type` и `Ship hp_type` (первый параметр корабля). Числовое значение сохраняется в записи.

| Имя | Значение | Имя | Значение |
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

Обратите внимание на переставленный порядок двух специальных торпед (`_1` = 0x24, `_2` = 0x23); значения 0 в таблице нет (`Ship hp_type` с отсутствующим именем пропускается).
