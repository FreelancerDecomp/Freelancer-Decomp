# Клиентские сопутствующие ключи записей архетипов (`ICliObj::ReadUnknown`, `[LOD]`)

Файлы архетипов (корабли, solar-объекты, астероиды, оборудование, взрывы) читаются загрузчиком архетипов Common ([archetypes-and-equipment.md](archetypes-and-equipment.md)). В клиенте каждая запись дополнительно несёт **сопутствующий** объект (`Archetype::ICliObj`, создаваемый клиентской фабрикой), который хранит данные, нужные только клиенту: ссылки на эффекты и звуки, параметры кокпита и камеры, диапазоны LOD, параметры света. Эта страница перечисляет ключи, которые понимают сопутствующие объекты, и секцию `[LOD]`. Читатели системных файлов описаны в [client-world-fx.md](client-world-fx.md), эффекты, на которые ссылаются эти ключи, — в [client-world-fx-effects.md](client-world-fx-effects.md).

**Расположение файла / как он находится.** Те же файлы и ключи `[Data]`, что и у записей архетипов (`equipment`, `solar`, `ships`, `asteroids`, `explosions`; см. [freelancer-ini.md](freelancer-ini.md)), читаются через `INI_Reader` ([parsers.md](parsers.md)). Клиент не читает ничего из отдельного файла. Обозначения (`str`, `id`, `float`, ...) объяснены в [client-world-fx.md](client-world-fx.md#обозначения); значения `id` здесь — псевдонимы эффектов (`[Effect]`), звуков или мешей, хешируемые через `CreateID`, и **их существование не проверяется при чтении ключа**.

## Как достигается сопутствующий объект

* Пока читается секция записи, Common сначала предлагает каждый ключ классу записи (`Root::read` и листовые классы, [common_archetype.cpp:468](../../../src/Common/common_archetype.cpp#L468)). Только ключ, который класс записи **не** потребил, попадает в `Companion->ReadUnknown(reader)` ([common_archetype.cpp:542](../../../src/Common/common_archetype.cpp#L542)). Ключ, который Common уже обрабатывает (например, `ids_info`), поэтому никогда не доходит до сопутствующего объекта, даже если код сопутствующего объекта его тоже проверяет.
* Каждый класс сопутствующего объекта сначала вызывает свой базовый класс, поэтому общие ключи из раздела 2 принимаются записью любого вида. Ключ, который никто не потребил, клиент игнорирует без сообщения (что загрузчик делает с результатом `false`, описано в [archetypes-and-equipment.md](archetypes-and-equipment.md)).
* Секция `[LOD]` не читается Common: загрузчик передаёт её сопутствующему объекту последней успешно вставленной записи ([common_archetypeloader.cpp:443](../../../src/Common/common_archetypeloader.cpp#L443)), поэтому она должна следовать за записью, к которой относится.
* Записи с пометкой «inherit» копируют и поля сопутствующего объекта донора, поэтому ключ, заданный в доноре, наследуется ([archetypes-and-equipment.md](archetypes-and-equipment.md)).

Класс сопутствующего объекта выбирается по виду секции ([FCliObjFactory::Create, client_cliobj.cpp:4328](../../../src/freelancer/Client/client_cliobj.cpp#L4328)):

| Секция | Сопутствующий объект | Дополнительные ключи |
|---|---|---|
| `[Ship]` | `FCliShip` | раздел 3 |
| `[Solar]` | `FCliSolar` | раздел 4 |
| `[Asteroid]`, `[AsteroidMine]` | `FCliAsteroid` | нет (только общие ключи) |
| `[DynamicAsteroid]` | `FCliDynamicAsteroid` | раздел 5 |
| `[Engine]` | `FCliEngine` | раздел 6 |
| `[Shield]` | `FCliEQShield` | нет |
| `[ShieldGenerator]` | `FCliShieldGenerator` | раздел 7 |
| `[Thruster]` | `FCliEQThruster` | раздел 8 |
| `[Gun]` | `FCliGun` (пусковая установка) | раздел 9 |
| `[MineDropper]`, `[CounterMeasureDropper]` | `FCliLauncher` | раздел 9 |
| `[Light]` | `FCliEQLight` | раздел 10 |
| `[Tractor]` | `FCliTractor` | раздел 11 |
| `[AttachedFX]` | `FCliEQAttachedFX` | раздел 12 |
| `[InternalFX]` | `FCliInternalFx` | раздел 13 |
| `[RepairDroid]` | `FCliRepairDroid` | раздел 14 |
| `[TradeLane]` | `FCliTradeLane` | раздел 15 |
| `[Munition]` | `FCliMunition` | раздел 16 |
| `[Mine]`, `[CounterMeasure]` | `FCliProj` | раздел 16 (первые три ключа) |
| `[Simple]`, `[LootCrate]`, `[CargoPod]`, `[Commodity]`, `[Power]`, `[Scanner]`, `[RepairKit]`, `[ShieldBattery]`, `[CloakingDevice]`, `[Armor]` | `FCliObj` | нет (только общие ключи) |

## 1. Секция `[LOD]`

[ApplyLodSection, client_cliobj.cpp:147](../../../src/freelancer/Client/client_cliobj.cpp#L147). Каждый блок `[LOD]` описывает диапазоны LOD одного подобъекта модели.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `obj` | str (всё значение) | - | Начинает новый узел LOD для подобъекта с этим именем (сохраняется его CRC-32). **Сохраняется только последний `obj` блока**; более ранние узлы того же блока отбрасываются. `LODranges` перед первым `obj` относятся к анонимному узлу (CRC 0). | [L155](../../../src/freelancer/Client/client_cliobj.cpp#L155) |
| `LODranges` | до 8 float | - | Расстояния до камеры, на которых используется каждый следующий уровень LOD модели; список внутренне замыкается значением -1. В конце блока `[LOD]` узел, у которого первый диапазон отрицателен, отбрасывается. Если значений меньше трёх, `MinSpecLOD` устанавливается в 0. | [L116](../../../src/freelancer/Client/client_cliobj.cpp#L116) |
| `MinSpecLOD` | int | 1 | Уровень LOD, ограничиваемый на машинах минимальной конфигурации; относится к текущему узлу, поэтому должен следовать за `obj`/`LODranges`. Игнорируется, если узла ещё нет. | [L136](../../../src/freelancer/Client/client_cliobj.cpp#L136) |

`LODranges` и `MinSpecLOD` принимаются также непосредственно внутри записи (раздел 2) и тогда относятся к узлу записи по умолчанию.

Пример (синтетический):

```ini
[Ship]
nickname = demo_fighter
LODranges = 0, 40, 120, 400

[LOD]
obj = demo_turret
LODranges = 0, 60, 200
MinSpecLOD = 1
```

## 2. Ключи, принимаемые каждой записью (`FCliObj::ReadUnknown`)

[client_cliobj.cpp:1975](../../../src/freelancer/Client/client_cliobj.cpp#L1975).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `material_library` | str (всё значение), необязательный int `[2]` | нет | Добавляет библиотеку материалов (путь) для загрузки вместе с моделью; повторяемый. Если присутствует третий параметр, он читается как id библиотеки (иначе -1), но в качестве пути всё равно используется **весь текст значения, включая запятые**. | [L1978](../../../src/freelancer/Client/client_cliobj.cpp#L1978) |
| `LODranges`, `MinSpecLOD` | см. раздел 1 | - | То же, что в блоке `[LOD]`. | [L1988](../../../src/freelancer/Client/client_cliobj.cpp#L1988) |
| `distance_render` | float | -1 | Сохраняется (и копируется через `inherit`), но никакой код в `src/` не читает его обратно. Не проверяется. | [L1991](../../../src/freelancer/Client/client_cliobj.cpp#L1991) |
| `envmap_material` | str (всё значение) | пусто | Имя материала карты окружения; непустое значение включает отображение окружения для модели ([gf_object_ship.cpp:433](../../../src/freelancer/Client/gf_object_ship.cpp#L433)). | [L1997](../../../src/freelancer/Client/client_cliobj.cpp#L1997) |
| `surface_hit_effects` | float, затем id (`str[1..]`) | - | `surface_hit_effects = <порог>, <эффект>, <эффект>, ...`: одна запись на ключ (повторяемый). Эффекты — псевдонимы `[Effect]`. Запись выбирается сравнением значения урона (урон столкновения или `hull_damage` боеприпаса, попавшего в объект) с порогами: записи просматриваются в порядке файла, и используется последняя, у которой порог не выше значения, поэтому перечисляйте их по возрастанию; значение ниже первого порога ничего не воспроизводит; из выбранной записи случайно выбирается один эффект ([client_cliobj.cpp:2320](../../../src/freelancer/Client/client_cliobj.cpp#L2320), [client_gameobj.cpp:531](../../../src/freelancer/Client/client_gameobj.cpp#L531)). | [L2003](../../../src/freelancer/Client/client_cliobj.cpp#L2003) |

## 3. `[Ship]`

[FCliShip::ReadUnknown, L2440](../../../src/freelancer/Client/client_cliobj.cpp#L2440). Три угла поворота задаются в градусах, ограничиваются диапазоном `0..90` и хранятся в радианах ([ship_camera_params.cpp:20](../../../src/freelancer/Client/ship_camera_params.cpp#L20)); NaN сохраняется.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `bay_doors_open_snd` | id (`str`) | пусто | Звук при открытии створок ангара. | [L2446](../../../src/freelancer/Client/client_cliobj.cpp#L2446) |
| `bay_doors_close_snd` | id (`str`) | пусто | Звук при их закрытии. | [L2452](../../../src/freelancer/Client/client_cliobj.cpp#L2452) |
| `cockpit` | cstr (всё значение) | пусто | INI кокпита, используемый в виде от первого лица (относительно пути данных, например в `cockpits\<nation>\`); читается загрузчиками камеры, перечисленными в [client-ui-config.md](client-ui-config.md). | [L2459](../../../src/freelancer/Client/client_cliobj.cpp#L2459) |
| `pilot_mesh` | id (`str`) | пусто | Меш модели пилота. | [L2465](../../../src/freelancer/Client/client_cliobj.cpp#L2465) |
| `camera_offset` | float x2 | `0, 0` | Смещение камеры (два значения). | [L2471](../../../src/freelancer/Client/client_cliobj.cpp#L2471) |
| `camera_angular_slerp_multiplier` | float | 0.04 | Сглаживание вращения камеры. | [L2478](../../../src/freelancer/Client/client_cliobj.cpp#L2478) |
| `camera_angular_acceleration` | float | 0.07 | Угловое ускорение камеры. | [L2484](../../../src/freelancer/Client/client_cliobj.cpp#L2484) |
| `camera_horizontal_turn_angle` | float (градусы) | 0.2 рад | Угол горизонтального упреждения; ограничивается `0..90`, хранится в радианах. | [L2490](../../../src/freelancer/Client/client_cliobj.cpp#L2490) |
| `camera_vertical_turn_up_angle` | float (градусы) | 0.2 рад | Угол вверх; обработка та же. | [L2496](../../../src/freelancer/Client/client_cliobj.cpp#L2496) |
| `camera_vertical_turn_down_angle` | float (градусы) | 0.2 рад | Угол вниз; обработка та же. | [L2502](../../../src/freelancer/Client/client_cliobj.cpp#L2502) |
| `camera_turn_look_ahead_slerp_amount` | float | 0.5 | Сглаживание упреждения; хранится без изменений. | [L2508](../../../src/freelancer/Client/client_cliobj.cpp#L2508) |

## 4. `[Solar]`

[FCliSolar::ReadUnknown, L2605](../../../src/freelancer/Client/client_cliobj.cpp#L2605).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `shape_name` | str (всё значение), не более 31 символа | `DEFAULT_TEXTURE` | Имя формы навигационной карты, используемой для этого solar-объекта. | [L2610](../../../src/freelancer/Client/client_cliobj.cpp#L2610) |
| `infocard_ids` | int | 0 | Id строки инфокарты (то же поле пишет и `ids_info`, но `ids_info` сначала потребляется Common и до этого кода не доходит). | [L2629](../../../src/freelancer/Client/client_cliobj.cpp#L2629) |
| `open_sound` | id (`str`) | пусто | Звук при открытии объекта (двери/врат). | [L2635](../../../src/freelancer/Client/client_cliobj.cpp#L2635) |
| `close_sound` | id (`str`) | пусто | Звук при его закрытии. | [L2641](../../../src/freelancer/Client/client_cliobj.cpp#L2641) |
| `animated_textures` | bool | false | Материал solar-объекта использует анимированные текстуры. | [L2647](../../../src/freelancer/Client/client_cliobj.cpp#L2647) |

## 5. `[DynamicAsteroid]`

[FCliDynamicAsteroid::ReadUnknown, L2868](../../../src/freelancer/Client/client_cliobj.cpp#L2868).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `particle_effect` | id (`str`) | нет | Псевдоним `[Effect]` эффекта частиц, прикреплённого к астероиду. Указание ключа также устанавливает флаг «есть эффект». | [L2874](../../../src/freelancer/Client/client_cliobj.cpp#L2874) |

## 6. `[Engine]`

[FCliEngine::ReadUnknown, L3914](../../../src/freelancer/Client/client_cliobj.cpp#L3914). Все значения `*_sound` — псевдонимы звуков, а все значения `*_effect` — псевдонимы `[Effect]`. Три ключа `*_range` принимают два целых `low, high`; если `low` больше `high`, читатель пишет в лог «<ключ> out of order!» и устанавливает `high` равным `low`.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `flame_effect` | id (`str[0]`) | пусто | Эффект пламени двигателя. | [L3919](../../../src/freelancer/Client/client_cliobj.cpp#L3919) |
| `trail_effect` | id (`str[0]`) | пусто | Шлейф двигателя для кораблей, не принадлежащих игроку. | [L3925](../../../src/freelancer/Client/client_cliobj.cpp#L3925) |
| `trail_effect_player` | id (`str[0]`) | пусто | Шлейф для корабля игрока. | [L3931](../../../src/freelancer/Client/client_cliobj.cpp#L3931) |
| `cruise_disrupt_effect` | id (`str[0]`) | пусто | Эффект при срыве круиза. | [L3937](../../../src/freelancer/Client/client_cliobj.cpp#L3937) |
| `inside_sound_cone` | int | 0 | Внутренний угол конуса звука двигателя (градусы). | [L3943](../../../src/freelancer/Client/client_cliobj.cpp#L3943) |
| `outside_sound_cone` | int | 360 | Внешний угол конуса. | [L3949](../../../src/freelancer/Client/client_cliobj.cpp#L3949) |
| `outside_cone_attenuation` | float | 0 | Затухание за пределами внешнего конуса. | [L3955](../../../src/freelancer/Client/client_cliobj.cpp#L3955) |
| `rumble_sound` | id (`str`) | пусто | Зацикленный гул. | [L3961](../../../src/freelancer/Client/client_cliobj.cpp#L3961) |
| `rumble_pitch_range` | int x2 | `0, 0` | Диапазон высоты тона гула. | [L3967](../../../src/freelancer/Client/client_cliobj.cpp#L3967) |
| `rumble_atten_range` | int x2 | `0, 0` | Диапазон затухания гула. | [L3973](../../../src/freelancer/Client/client_cliobj.cpp#L3973) |
| `character_start_sound` | id (`str`) | пусто | Звук запуска двигателя. | [L3979](../../../src/freelancer/Client/client_cliobj.cpp#L3979) |
| `character_loop_sound` | id (`str`) | пусто | Зацикленный звук работающего двигателя. | [L3985](../../../src/freelancer/Client/client_cliobj.cpp#L3985) |
| `character_pitch_range` | int x2 | `0, 0` | Диапазон высоты тона цикла. | [L3991](../../../src/freelancer/Client/client_cliobj.cpp#L3991) |
| `cruise_start_sound` | id (`str`) | пусто | Запуск круизного двигателя. | [L3998](../../../src/freelancer/Client/client_cliobj.cpp#L3998) |
| `cruise_loop_sound` | id (`str`) | пусто | Круизный цикл. | [L4004](../../../src/freelancer/Client/client_cliobj.cpp#L4004) |
| `cruise_stop_sound` | id (`str`) | пусто | Остановка круиза. | [L4010](../../../src/freelancer/Client/client_cliobj.cpp#L4010) |
| `cruise_disrupt_sound` | id (`str`) | пусто | Срыв круиза. | [L4016](../../../src/freelancer/Client/client_cliobj.cpp#L4016) |
| `cruise_backfire_sound` | id (`str`) | пусто | Обратная вспышка двигателя. | [L4022](../../../src/freelancer/Client/client_cliobj.cpp#L4022) |
| `engine_kill_sound` | id (`str`) | пусто | Отключение двигателя. | [L4028](../../../src/freelancer/Client/client_cliobj.cpp#L4028) |

## 7. `[ShieldGenerator]`

[FCliShieldGenerator::ReadUnknown, L3620](../../../src/freelancer/Client/client_cliobj.cpp#L3620).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `shield_collapse_particle` | id (`str`) | пусто | Псевдоним `[Effect]`, воспроизводимый при коллапсе щита. | [L3625](../../../src/freelancer/Client/client_cliobj.cpp#L3625) |
| `shield_collapse_sound` | id (`str`) | пусто | Звук при коллапсе. | [L3631](../../../src/freelancer/Client/client_cliobj.cpp#L3631) |
| `shield_rebuilt_sound` | id (`str`) | пусто | Звук при восстановлении щита. | [L3637](../../../src/freelancer/Client/client_cliobj.cpp#L3637) |
| `shield_hit_effects` | float, затем id (`str[1..]`) | - | Та же форма и то же правило выбора, что у `surface_hit_effects` (раздел 2): `<порог>, <эффект>, ...`, одна запись на ключ, повторяемый; воспроизводится при попадании в щит (сравниваемое значение поставляет код попадания в щит). | [L3646](../../../src/freelancer/Client/client_cliobj.cpp#L3646) |

## 8. `[Thruster]`

[FCliEQThruster::ReadUnknown, L2978](../../../src/freelancer/Client/client_cliobj.cpp#L2978).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `particles` | id (`str`) | пусто | Псевдоним `[Effect]` пламени маневрового двигателя. | [L2983](../../../src/freelancer/Client/client_cliobj.cpp#L2983) |
| `hp_particles` | cstr | пусто | Имя hardpoint, на котором размещаются частицы. | [L2989](../../../src/freelancer/Client/client_cliobj.cpp#L2989) |

## 9. `[Gun]`, `[MineDropper]`, `[CounterMeasureDropper]` (пусковые установки)

[FCliLauncher::ReadUnknown, L3745](../../../src/freelancer/Client/client_cliobj.cpp#L3745); `[Gun]` добавляет `turret_sound` ([FCliGun::ReadUnknown, L3841](../../../src/freelancer/Client/client_cliobj.cpp#L3841)). Загрузка пусковой установки также загружает сопутствующий объект оборудования, названного её `projectile_archetype`.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `flash_radius` | float | 4.0 | Радиус света дульной вспышки. | [L3750](../../../src/freelancer/Client/client_cliobj.cpp#L3750) |
| `light_anim` | str (всё значение), не более 31 символа | пусто | Имя анимации света из `fx\lightanim.ini` ([client-world-fx-effects.md](client-world-fx-effects.md#2-анимации-света-fxlightanimini)). | [L3756](../../../src/freelancer/Client/client_cliobj.cpp#L3756) |
| `flash_particle_name` | id (`str`) | пусто | Псевдоним `[Effect]` дульной вспышки. | [L3762](../../../src/freelancer/Client/client_cliobj.cpp#L3762) |
| `dry_fire_sound` | id (`str`) | пусто | Звук при выстреле без энергии/боеприпасов. | [L3768](../../../src/freelancer/Client/client_cliobj.cpp#L3768) |
| `turret_sound` (только `[Gun]`) | id (`str`) | пусто | Звук вращения турели. | [L3846](../../../src/freelancer/Client/client_cliobj.cpp#L3846) |

## 10. `[Light]`

[FCliEQLight::ReadUnknown, L3074](../../../src/freelancer/Client/client_cliobj.cpp#L3074). Цвета — float 0..255, делённые на 255.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `color` | float x3 | `0, 0, 0` | Цвет лампы. Пока `glow_color` всё ещё целиком нулевой, `color` устанавливает и его. | [L3082](../../../src/freelancer/Client/client_cliobj.cpp#L3082) |
| `glow_color` | float x3 | `0, 0, 0` | Цвет свечения. | [L3103](../../../src/freelancer/Client/client_cliobj.cpp#L3103) |
| `min_color` | float x3 | `0, 0, 0` | Цвет в самой тусклой точке мигания. | [L3114](../../../src/freelancer/Client/client_cliobj.cpp#L3114) |
| `avg_delay` | float | 0 | Средняя задержка между миганиями. | [L3125](../../../src/freelancer/Client/client_cliobj.cpp#L3125) |
| `intensity` | float | 0 | Интенсивность света. | [L3131](../../../src/freelancer/Client/client_cliobj.cpp#L3131) |
| `bulb_size` | float | 1.0 | Размер спрайта лампы. | [L3137](../../../src/freelancer/Client/client_cliobj.cpp#L3137) |
| `glow_size` | float | 1.0 | Размер спрайта свечения. | [L3143](../../../src/freelancer/Client/client_cliobj.cpp#L3143) |
| `blink_duration` | float | 0.3 | Длительность мигания. | [L3149](../../../src/freelancer/Client/client_cliobj.cpp#L3149) |
| `flare_cone` | float x2 (градусы) | косинусы `-1, -1` | Два угла конуса; сохраняются косинус значения 1 и косинус значения 0. | [L3155](../../../src/freelancer/Client/client_cliobj.cpp#L3155) |
| `lightsource_cone` | float | 180 | Угол конуса источника света (градусы, хранится без изменений). | [L3169](../../../src/freelancer/Client/client_cliobj.cpp#L3169) |

## 11. `[Tractor]`

[FCliTractor::ReadUnknown, L2909](../../../src/freelancer/Client/client_cliobj.cpp#L2909).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `color` | byte x3 | `255, 255, 255` | Цвет луча (младшие 8 бит каждого усечённого значения). | [L2915](../../../src/freelancer/Client/client_cliobj.cpp#L2915) |
| `operating_effect` | id (`str`) | пусто | Псевдоним `[Effect]` эффекта луча. | [L2923](../../../src/freelancer/Client/client_cliobj.cpp#L2923) |
| `tractor_complete_snd` | id (`str`) | пусто | Звук при завершении притягивания. | [L2929](../../../src/freelancer/Client/client_cliobj.cpp#L2929) |

## 12. `[AttachedFX]`

[FCliEQAttachedFX::ReadUnknown, L2800](../../../src/freelancer/Client/client_cliobj.cpp#L2800).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `particles` | id (`str`) | пусто | Псевдоним `[Effect]` прикреплённого эффекта. | [L2805](../../../src/freelancer/Client/client_cliobj.cpp#L2805) |
| `use_throttle` | bool | false | Эффект следует за тягой. | [L2811](../../../src/freelancer/Client/client_cliobj.cpp#L2811) |

## 13. `[InternalFX]`

[FCliInternalFx::ReadUnknown, L3267](../../../src/freelancer/Client/client_cliobj.cpp#L3267). В отличие от `[Engine]`, значения конусов здесь — float.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `use_sound` | id (`str`) | пусто | Звук, воспроизводимый при использовании оборудования. | [L3272](../../../src/freelancer/Client/client_cliobj.cpp#L3272) |
| `inside_sound_cone` | float | 360 | Внутренний угол конуса. | [L3278](../../../src/freelancer/Client/client_cliobj.cpp#L3278) |
| `outside_sound_cone` | float | 360 | Внешний угол конуса. | [L3284](../../../src/freelancer/Client/client_cliobj.cpp#L3284) |
| `outside_cone_attenuation` | float | 0 | Затухание за пределами внешнего конуса. | [L3290](../../../src/freelancer/Client/client_cliobj.cpp#L3290) |

## 14. `[RepairDroid]`

[FCliRepairDroid::ReadUnknown, L3554](../../../src/freelancer/Client/client_cliobj.cpp#L3554).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `active_effect` | id (`str`) | пусто | Псевдоним `[Effect]`, воспроизводимый во время ремонта. | [L3559](../../../src/freelancer/Client/client_cliobj.cpp#L3559) |

## 15. `[TradeLane]`

[FCliTradeLane::ReadUnknown, L4137](../../../src/freelancer/Client/client_cliobj.cpp#L4137): шестнадцать ключей id (таблица на L4148) и семь ключей float (таблица на L4183). Все значения `tl_*` хранятся как хешированные псевдонимы (эффектов или звуков) и разыскиваются при использовании линии.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `tl_player_enter`, `tl_player_travel`, `tl_player_splash`, `tl_player_exit`, `tl_player_disrupt` | id (`str`) | пусто | Эффекты (или звуки) для игрока в линии: вход, движение, всплеск, выход, срыв. | [L4149](../../../src/freelancer/Client/client_cliobj.cpp#L4149) |
| `tl_ship_enter`, `tl_ship_travel`, `tl_ship_splash`, `tl_ship_exit`, `tl_ship_disrupt` | id (`str`) | пусто | То же для остальных кораблей. | [L4154](../../../src/freelancer/Client/client_cliobj.cpp#L4154) |
| `tl_ring_active`, `tl_ring_shield`, `tl_ring_tube` | id (`str`) | пусто | Эффекты кольца линии (активное, щит, туннель). | [L4159](../../../src/freelancer/Client/client_cliobj.cpp#L4159) |
| `tl_ring_active_start`, `tl_ring_active_loop`, `tl_ring_active_end` | id (`str`) | пусто | Начало, цикл и конец активации кольца (псевдонимы эффектов или звуков; загрузчик их не различает). | [L4162](../../../src/freelancer/Client/client_cliobj.cpp#L4162) |
| `secs_before_enter` | float | 1.0 | Задержка перед эффектом входа. | [L4184](../../../src/freelancer/Client/client_cliobj.cpp#L4184) |
| `secs_before_exit` | float | 1.0 | Задержка перед эффектом выхода. | [L4185](../../../src/freelancer/Client/client_cliobj.cpp#L4185) |
| `secs_before_splash` | float | 1.0 | Задержка перед эффектом всплеска. | [L4186](../../../src/freelancer/Client/client_cliobj.cpp#L4186) |
| `activation_start` | float | 600 | Параметр активации (начало). | [L4187](../../../src/freelancer/Client/client_cliobj.cpp#L4187) |
| `activation_end` | float | 300 | Параметр активации (конец). | [L4188](../../../src/freelancer/Client/client_cliobj.cpp#L4188) |
| `spin_max` | float | 50 | Максимальное вращение кольца. | [L4189](../../../src/freelancer/Client/client_cliobj.cpp#L4189) |
| `spin_accel` | float | 25 | Ускорение вращения кольца. | [L4190](../../../src/freelancer/Client/client_cliobj.cpp#L4190) |

## 16. `[Munition]`, `[Mine]`, `[CounterMeasure]`

`[Mine]` и `[CounterMeasure]` используют `FCliProj` ([L2733](../../../src/freelancer/Client/client_cliobj.cpp#L2733)) и принимают первые три ключа; `[Munition]` (`FCliMunition`, [L3333](../../../src/freelancer/Client/client_cliobj.cpp#L3333)) принимает все пять.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `const_effect_delay` | float | 0 | Задержка перед началом постоянного эффекта (шлейфа). | [L2739](../../../src/freelancer/Client/client_cliobj.cpp#L2739) |
| `const_effect` | id (`str`) | пусто | Псевдоним `[Effect]` эффекта, сопровождающего снаряд. | [L2745](../../../src/freelancer/Client/client_cliobj.cpp#L2745) |
| `one_shot_sound` | id (`str`) | пусто | Звук, воспроизводимый один раз при запуске снаряда. | [L2751](../../../src/freelancer/Client/client_cliobj.cpp#L2751) |
| `munition_hit_effect` (только боеприпас) | id (`str`) | - | Псевдоним `[Effect]`, воспроизводимый при попадании; повторяемый, сохраняются все, один выбирается случайно. | [L3356](../../../src/freelancer/Client/client_cliobj.cpp#L3356) |
| `HP_trail_parent` (только боеприпас) | cstr | пусто | Hardpoint пусковой установки, за которым следует шлейф. | [L3362](../../../src/freelancer/Client/client_cliobj.cpp#L3362) |

Пример (синтетический):

```ini
[Gun]
nickname = demo_gun
flash_particle_name = demo_muzzle
light_anim = demo_flash_light
dry_fire_sound = demo_dryfire

[Munition]
nickname = demo_shot
const_effect = demo_shot_trail
munition_hit_effect = demo_hit_a
munition_hit_effect = demo_hit_b
```
