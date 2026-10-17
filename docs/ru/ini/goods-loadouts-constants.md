# constants.ini, файлы goods и файлы loadout

Эта страница описывает три таблицы данных, читаемые Common.dll: глобальные константы настройки (`constants.ini`), торговые товары (секции `[Good]` файлов `goods`) и комплектации оборудования (секции `[Loadout]`). Определения архетипов, на которые ссылаются эти таблицы, описаны в [archetypes-and-equipment.md](archetypes-and-equipment.md); там же объяснена нотация значений (`int`, `float`, `bool`, `id`, `cstr`).

**Расположение файлов / как они находятся.** Все три перечислены в `[Data]` файла `freelancer.ini` ([freelancer-ini.md](freelancer-ini.md)) и разрешаются относительно пути данных:

| Ключ `[Data]` | Значение(я) в dev-ini | Читатель |
|---|---|---|
| `constants` | `constants.ini` | `ReadConstants` ([common_constants.cpp:270](../../../src/Common/common_constants.cpp#L270)) |
| `goods` (повторяемый) | `equipment\goods.ini`, `engine_good.ini`, `misc_good.ini`, `st_good.ini`, `weapon_good.ini` | `GoodInfoList::load` ([common_goodinfo.cpp:538](../../../src/Common/common_goodinfo.cpp#L538)) |
| `loadouts` (повторяемый) | `ships\loadouts.ini`, `loadouts_special.ini`, `loadouts_utility.ini`, `solar\loadouts.ini` | `Loadout::Load` ([common_loadout.cpp:176](../../../src/Common/common_loadout.cpp#L176)) |

Имена секций и ключей нечувствительны к регистру (см. [parsers.md](parsers.md)). Таблицы цен отдельных баз (`market_*.ini`) — файлы другого типа, см. [systems-and-bases.md](systems-and-bases.md).

---

## constants.ini

Отсутствующий или неоткрываемый файл заставляет `ReadConstants` вернуть false без сообщения; в остальных случаях применяется каждая распознанная секция. В каждой секции, **кроме** `[CollisionConsts]`, ключ, которого нет в таблице секции, записывает в лог «Unknown Constant definition: <строка>». У каждой константы есть встроенное значение по умолчанию; отсутствующий ключ оставляет его. Для скалярного ключа используется только первое значение (`[0]`). Секции можно повторять; более поздние ключи перезаписывают более ранние.

Типы: `float` (`get_value_float`), `int` (разбирается как float, затем усекается к нулю), `double`, `id` (`CreateID` от всего значения).

### `[Constants]`  ([common_constants.cpp:386](../../../src/Common/common_constants.cpp#L386))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `TestValue` | float | 3.1415 | Читается, никогда не используется. | [L279](../../../src/Common/common_constants.cpp#L279) |
| `TestString` | str (всё значение, не более 127 символов) | `sample` | Читается, никогда не используется. | [L390](../../../src/Common/common_constants.cpp#L390) |
| `COLLISION_DAMAGE_FACTOR` | float | 0.25 | Глобальный масштаб урона от столкновений. | [L281](../../../src/Common/common_constants.cpp#L281) |
| `LOOT_OWNER_SAFE_TIME` | float | 1.0 | Секунды, в течение которых выбросивший не может подобрать собственный лут. | [L282](../../../src/Common/common_constants.cpp#L282) |
| `LOOT_UNSEEN_LIFE_TIME` | float | 60.0 | Время жизни лута, который никто не видит. | [L283](../../../src/Common/common_constants.cpp#L283) |
| `LOOT_UNSEEN_RADIUS` | float | 2500.0 | Радиус проверки «виден» для лута. | [L284](../../../src/Common/common_constants.cpp#L284) |
| `MUSIC_CROSS_FADE_DELAY` | float | 30.0 | Задержка перекрёстного затухания музыки. | [L285](../../../src/Common/common_constants.cpp#L285) |
| `JETTISONED_CARGO_VELOCITY` | float | 10.0 | Скорость сброшенного груза. | [L286](../../../src/Common/common_constants.cpp#L286) |
| `MUZZLE_CONE_ANGLE` | float | 90.0 | Угол конуса дула. | [L287](../../../src/Common/common_constants.cpp#L287) |
| `FIRE_FAILED_DELAY` | float | 1.0 | Секунды между звуками «выстрел не удался». | [L288](../../../src/Common/common_constants.cpp#L288) |
| `PLAYER_COLLISION_GROUP_HIT_PTS_SCALE` | float | 1.0 | Масштаб очков прочности групп столкновений для игрока. | [L289](../../../src/Common/common_constants.cpp#L289) |
| `PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE` | float | 1.0 | Масштаб очков прочности прикреплённого оборудования для игрока. | [L290](../../../src/Common/common_constants.cpp#L290) |
| `SND_CARGO_JETTISONED` | id | `cargo_jettison` | Nickname звука. | [L293](../../../src/Common/common_constants.cpp#L293) |
| `FIRE_FAILED_SOUND` | id | `fire_dry` | Nickname звука. | [L294](../../../src/Common/common_constants.cpp#L294) |
| `MAX_PLAYER_AMMO` | int | 50 | Предел боезапаса игрока. | [L297](../../../src/Common/common_constants.cpp#L297) |

### `[EngineEquipConsts]`  ([L415](../../../src/Common/common_constants.cpp#L415))

Все `float` ([таблица L300](../../../src/Common/common_constants.cpp#L300)). Значения по умолчанию: `CRUISING_SPEED` 300, `CRUISE_ACCEL_TIME` 5, `CRUISE_DRAG` 3, `CRUISE_DISRUPT_TIME` 0, `MAX_DELTA_FX_THROTTLE` 0.1, `MAX_ENGINE_FX_THROTTLE` 0.9, и 0 для `THROTTLE_STEADY_TIME`, `THROTTLE_ATTEN_MOD_RANGE`, `DELTA_THROTTLE_ATTEN_MOD_CHANGING`, `DELTA_THROTTLE_ATTEN_MOD_STEADY`, `CRUISE_STEADY_TIME`, `DELTA_CRUISE_ATTEN_MOD_STEADY`, `CRUISE_ATTEN_MOD_RANGE`. Они настраивают скорость/заряд крейсерского двигателя и затухание звука двигателя в зависимости от тяги.

### `[ShieldEquipConsts]`  ([L424](../../../src/Common/common_constants.cpp#L424))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `HULL_DAMAGE_FACTOR` | float | 1.0 | Коэффициент, применяемый к урону, проходящему на корпус. | [L317](../../../src/Common/common_constants.cpp#L317) |

### `[ThrusterEquipConsts]`  ([L433](../../../src/Common/common_constants.cpp#L433))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `INTERIOR_SOUND_NAME` | id | `interior_thruster_sound` | Nickname звука. | [L329](../../../src/Common/common_constants.cpp#L329) |
| `EXTERIOR_SOUND_NAME` | id | `exterior_thruster_sound` | Nickname звука. | [L330](../../../src/Common/common_constants.cpp#L330) |
| `MAX_VOLUME_FORCE` | float | 20000 | Тяга, при которой звук самый громкий. | [L321](../../../src/Common/common_constants.cpp#L321) |
| `MIN_VOLUME_FORCE` | float | 5000 | Тяга, при которой звук начинается. | [L322](../../../src/Common/common_constants.cpp#L322) |
| `INSIDE_CONE_ANGLE` | float | 60 | Угол внутреннего конуса звука. | [L323](../../../src/Common/common_constants.cpp#L323) |
| `OUTSIDE_CONE_ANGLE` | float | 220 | Угол внешнего конуса звука. | [L324](../../../src/Common/common_constants.cpp#L324) |
| `OUTSIDE_CONE_ATTENUATION` | float | -10 | Затухание за пределами конуса. | [L325](../../../src/Common/common_constants.cpp#L325) |

### `[PhySysConsts]`  ([L444](../../../src/Common/common_constants.cpp#L444))

Настройка физики. `float`, если не указано иное ([таблицы L333](../../../src/Common/common_constants.cpp#L333)).

| Ключ | Тип | По умолчанию | Источник |
|---|---|---|---|
| `PHYSICAL_SIM_RATE` | float | 16 | [L334](../../../src/Common/common_constants.cpp#L334) |
| `MAX_SPAWNED_MINDIST_COUNT` | int | 100 | [L365](../../../src/Common/common_constants.cpp#L365) |
| `ANOM_LIMITS_MAX_VELOCITY` | float | 1000 | [L335](../../../src/Common/common_constants.cpp#L335) |
| `ANOM_LIMITS_MAX_ANGULAR_VELOCITY_PER_PSI` | float | 0.94247788 | [L336](../../../src/Common/common_constants.cpp#L336) |
| `MATERIAL_FRICTION` | double | 0.1 | [L361](../../../src/Common/common_constants.cpp#L361) |
| `MATERIAL_ELASTICITY` | double | 0.9 | [L362](../../../src/Common/common_constants.cpp#L362) |
| `MIN_TIME_BETWEEN_COLLISIONS` | double | 0.5 | [L363](../../../src/Common/common_constants.cpp#L363) |
| `RMGR_LOOK_AHEAD_TIME_WORLD` / `_MAX_RADIUS_WORLD` / `_MIN_DISTANCE_WORLD` / `_MAX_DISTANCE_WORLD` | float | 1.0 / 5.0 / 100 / 1000 | [L337](../../../src/Common/common_constants.cpp#L337) |
| `RMGR_LOOK_AHEAD_MIN_SECONDS_WORLD` | float | 2.1 / 16 (вычисляется один раз при запуске; не пересчитывается, если `PHYSICAL_SIM_RATE` переопределён) | [L341](../../../src/Common/common_constants.cpp#L341) |
| `RMGR_LOOK_AHEAD_TIME_INTRA` / `_MAX_RADIUS_INTRA` / `_MIN_DISTANCE_INTRA` / `_MAX_DISTANCE_INTRA` | float | 0.5 / 0.9 / 5 / 50 | [L342](../../../src/Common/common_constants.cpp#L342) |
| `RMGR_LOOK_AHEAD_MIN_SECONDS_INTRA` | float | 2.1 / 16 (как выше) | [L346](../../../src/Common/common_constants.cpp#L346) |
| `DEFAULT_LINEAR_DAMPING` | float | 1.0 | [L347](../../../src/Common/common_constants.cpp#L347) |
| `DEFAULT_ANGULAR_DAMPING` | float x3 (`[0]`,`[1]`,`[2]`) | 2, 2, 2 | [L448](../../../src/Common/common_constants.cpp#L448) |
| `GOLEM_CHILD_MASS` | float | 100000 | [L348](../../../src/Common/common_constants.cpp#L348) |
| `GOLEM_CHILD_LINEAR_DAMP` | float | 1.0 | [L349](../../../src/Common/common_constants.cpp#L349) |
| `GOLEM_CHILD_ANGULAR_DAMP` | float x3 | 1, 1, 1 | [L455](../../../src/Common/common_constants.cpp#L455) |
| `GOLEM_MAX_DELTA_POSITION` | float | 10 | [L350](../../../src/Common/common_constants.cpp#L350) |
| `GOLEM_DELTA_ORIENTATION` | float | 1.5707964 | [L351](../../../src/Common/common_constants.cpp#L351) |
| `GOLEM_MAX_TRANSLATION_FORCE` | float x3 | 1e12, 1e12, 1e12 | [L462](../../../src/Common/common_constants.cpp#L462) |
| `GOLEM_MAX_TORQUE` | float | 1e12 | [L352](../../../src/Common/common_constants.cpp#L352) |
| `GOLEM_FORCE_FACTOR` / `GOLEM_TORQUE_FACTOR` | float | 0.8 / 0.8 | [L353](../../../src/Common/common_constants.cpp#L353) |
| `GOLEM_DAMP_FACTOR` / `GOLEM_ANGULAR_DAMP_FACTOR` | float | 1.0 / 1.0 | [L354](../../../src/Common/common_constants.cpp#L354) |
| `RC_MAX_DELTA_POSITION` / `RC_MAX_DELTA_ORIENTATION` | float | 50 / 5 | [L357](../../../src/Common/common_constants.cpp#L357) |

(«Golem» — физический контроллер твёрдого тела; «RMGR» — менеджер регионов; «RC» — коррекция удалённо управляемого тела.)

### `[CommConsts]`  ([L478](../../../src/Common/common_constants.cpp#L478))

Слышимость голоса/переговоров ([таблицы L369](../../../src/Common/common_constants.cpp#L369)).

| Ключ | Тип | По умолчанию |
|---|---|---|
| `COMM_PLAYER_FAR_DIST` | float | 2000 |
| `COMM_PLAYER_FAR_DIST_ATTEN` | float | -15 |
| `CHATTER_MAX_DIST` | float | 2000 |
| `CHATTER_MAX_DIST_ATTEN` | float | -20 |
| `CHATTER_START_ATTEN` | float | -5 |
| `COMM_CONFLICT_PRIORITY_CUTOFF` | int | -2 |
| `WALLA_MAX_DIST` | float | 2500 |
| `WALLA_MAX_DIST_ATTEN` | float | -26 |
| `WALLA_START_ATTEN` | float | -8 |
| `WALLA_PRIORITY_CUTOFF` | int | -4 |

### `[CollisionConsts]`  ([common_physys.cpp:317](../../../src/Common/common_physys.cpp#L317))

Обрабатывается `Collision::Load` (вызывается из `ReadConstants` для этого заголовка). Неизвестные ключи игнорируются без сообщения.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `damage` | `[0]` имя типа объекта, `[1]` float | нет | Урон от столкновения, наносимый объектом этого типа. `[0]` — одно из имён типов объектов из [archetypes-and-equipment.md](archetypes-and-equipment.md#имена-типов-объектов-type-); неизвестное имя записывает предупреждение и, как и `NONE`, пропускается. Повторяющиеся ключи для одного типа: побеждает **первый**. | [L324](../../../src/Common/common_physys.cpp#L324) |
| `max_impact_speed` | float | 250.0 | Опорная скорость удара в формуле урона от столкновения (`Collision::CalcCollisionDamage` делит на это значение). | [L334](../../../src/Common/common_physys.cpp#L334) |

Пример (синтетический):

```ini
[PhySysConsts]
PHYSICAL_SIM_RATE = 16
DEFAULT_ANGULAR_DAMPING = 2, 2, 2

[CollisionConsts]
damage = FIGHTER, 10
max_impact_speed = 250
```

---

## Файлы goods: `[Good]`

Файл goods — последовательность секций `[Good]` ([common_goodinfo.cpp:303](../../../src/Common/common_goodinfo.cpp#L303)). Неоткрываемый файл записывает в лог «Cannot open goods file». Секция `[GoodType]` устарела и записывает предупреждение; прочие секции игнорируются. Все файлы goods сливаются в один глобальный список. Цены хранятся как `float`.

**Зависимость от порядка.** `good_buy_price`, `bad_buy_price`, `good_sell_price`, `bad_sell_price` — это **множители, применяемые к уже прочитанной на этот момент `price`**, поэтому они должны идти после `price`. `price` заполняет любую из четырёх производных цен, которая всё ещё равна 0, значением `price * 0.95` (good buy, bad sell) и `price * 1.05` (bad buy, good sell).

`[Good]`, чей `nickname` уже есть в глобальном списке, прекращается на этом ключе: остальные его ключи не читаются, а пустая запись всё равно добавляется в список.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | id (всё значение) | хеш null (0) | Ключ товара. | [L322](../../../src/Common/common_goodinfo.cpp#L322) |
| `equipment` | id (всё значение) | 0 | Nickname архетипа `Commodity` / оборудования, которому соответствует товар. Обязателен для категорий `Commodity` и `Equipment`; архетип ищется без проверки на null. После секции `ids_name` / `ids_info` товара по умолчанию берутся из архетипа, а для прикрепляемого оборудования `attachment_archetype` и `HP_child` по умолчанию берутся из архетипа. | [L333](../../../src/Common/common_goodinfo.cpp#L333) |
| `category` | `Commodity` \| `Equipment` \| `ShipHull` \| `Ship` | неизвестна (0xFFFFFFFF) | Без учёта регистра. Любое другое значение записывает в лог «unknown value: category = %s» и оставляет категорию неизвестной. Только `Commodity` и `Equipment` берут id архетипа из `equipment`; остальные категории получают id архетипа 0. | [L350](../../../src/Common/common_goodinfo.cpp#L350) |
| `ship` | id (всё значение) | 0 | Архетип корабля (для товаров `Ship`). | [L372](../../../src/Common/common_goodinfo.cpp#L372) |
| `hull` | nickname другого товара | 0 | Для товара `Ship`: его товар `ShipHull`, который должен быть уже загружен; промах приводит к крашу (поведение retail). | [L439](../../../src/Common/common_goodinfo.cpp#L439) |
| `price` | int | 0 | Базовая цена. См. замечание о порядке выше. | [L376](../../../src/Common/common_goodinfo.cpp#L376) |
| `good_buy_price`, `bad_buy_price`, `good_sell_price`, `bad_sell_price` | float, множитель | выводится из `price` | Множитель `price`. | [L396](../../../src/Common/common_goodinfo.cpp#L396), [L403](../../../src/Common/common_goodinfo.cpp#L403), [L407](../../../src/Common/common_goodinfo.cpp#L407), [L411](../../../src/Common/common_goodinfo.cpp#L411) |
| `jump_dist` | int | 50 | Дистанция прыжка, используемая проверками, связанными с прыжками. | [L415](../../../src/Common/common_goodinfo.cpp#L415) |
| `ids_name` или `name` | int | из архетипа (0, если его нет) | Id строки отображаемого имени; принимаются оба написания. | [L342](../../../src/Common/common_goodinfo.cpp#L342) |
| `ids_info` или `info_desc` | int | из архетипа | Id строки инфокарты; принимаются оба написания. | [L346](../../../src/Common/common_goodinfo.cpp#L346) |
| `shop_archetype` | cstr | пусто (откат на `attachment_archetype`) | Модель, показываемая в магазине. | [L419](../../../src/Common/common_goodinfo.cpp#L419) |
| `material_library` | cstr | пусто | Библиотека материалов модели магазина. | [L423](../../../src/Common/common_goodinfo.cpp#L423) |
| `item_icon` | cstr | пусто | Модель значка. | [L427](../../../src/Common/common_goodinfo.cpp#L427) |
| `HP_child` | cstr | пусто | Дочерний hardpoint. | [L431](../../../src/Common/common_goodinfo.cpp#L431) |
| `attachment_archetype` | cstr | пусто | Модель прикрепления. | [L435](../../../src/Common/common_goodinfo.cpp#L435) |
| `addon` | `[0]` nickname оборудования, `[1]` hardpoint или `internal`, `[2]` int количество, необязательно `[3]` слот | нет | Дополнительный предмет, добавляемый к купленному кораблю. Повторяющиеся ключи дописываются. `[1]` `internal` (без учёта регистра) монтирует в грузовой отсек, любой другой текст — имя hardpoint. `[2]`, равное 0 или меньше (или отсутствующее), становится 1. `[3]`: `mid` или `high` (без учёта регистра) выбирает список дополнений mid/high; любое другое значение или его отсутствие выбирает список по умолчанию. | [L451](../../../src/Common/common_goodinfo.cpp#L451) |
| `combinable` | bool | false | Несколько единиц можно объединять в одну стопку груза. | [L496](../../../src/Common/common_goodinfo.cpp#L496) |
| `free_ammo` | `[0]` nickname боеприпаса (id), `[1]` int | 0, 0 | Боеприпасы, выдаваемые бесплатно вместе с предметом. | [L337](../../../src/Common/common_goodinfo.cpp#L337) |
| `msg_id_prefix` | str | пусто | Префикс сообщений переговоров (`CommReferrable`), проверяется для любого другого ключа. | [L504](../../../src/Common/common_goodinfo.cpp#L504) |

Если `nickname` товара отличается от его `equipment` (оба ненулевые), записывается предупреждение «Good (%s) nickname different from equipment».

Пример (синтетический):

```ini
[Good]
nickname = demo_commodity
equipment = demo_commodity
category = commodity
price = 200
good_sell_price = 1.2
```

---

## Файлы loadout: `[Loadout]`

Loadout — именованный список оборудования и груза, на который по id ссылаются архетипы solar, объекты систем, определения NPC и `addon` товаров. Путь к файлу открывается так, как его дал загрузчик (диспетчер уже разрешил его относительно пути данных); неоткрываемый файл записывает в лог «unable to open Loadout file». Секции, кроме `[Loadout]`, пропускаются.

Правила ([common_loadout.cpp:176](../../../src/Common/common_loadout.cpp#L176)):

* Учитывается только **первый** `nickname` секции; более поздние строки `nickname` игнорируются. Вторая секция `[Loadout]` с уже использованным nickname **заменяет** прежнее содержимое (список предметов очищается и заполняется заново).
* `equip` и `cargo` до `nickname` разыменовывают отсутствующий loadout (краш, как в retail).
* Предметы получают последовательные id в порядке файла.
* Ключ `archetype` проверяется и игнорируется. Все остальные ключи игнорируются.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | id (всё значение) | нет | Ключ loadout. | [L192](../../../src/Common/common_loadout.cpp#L192) |
| `equip` | `[0]` оборудование, необязательно `[1]` hardpoint, необязательно `[2]` float прочность | - | Смонтированный предмет. `[0]` — nickname архетипа (хешируется) **или** десятичный id: значение, разбираемое как ненулевое целое, используется как id (учтите, что `atoi` принимает ведущее число, поэтому nickname вида `2fast` будет прочитан как 2), иначе оно хешируется. Без `[1]` предмет лежит в грузовом отсеке (`BAY`, «internal»); с `[1]` он смонтирован на этом hardpoint. `[2]` — прочность (по умолчанию 1.0). Помечается как экипированный. | [L210](../../../src/Common/common_loadout.cpp#L210), [reader L111](../../../src/Common/common_loadout.cpp#L111) |
| `cargo` | `[0]` предмет, необязательно `[1]` int количество, необязательно `[2]` hardpoint, необязательно `[3]` float прочность, необязательно `[4]` bool миссионный | количество 1, прочность 1.0, миссионный false | Несмонтированный груз. `[0]` — как у `equip`. Пустое `[2]` означает грузовой отсек. **Если `[2]` разбирается как ненулевое число, остаток строки (hardpoint, прочность, миссионный) игнорируется.** Если товар предмета `combinable` и лежит в грузовом отсеке, более ранняя запись того же архетипа объединяется (количества складываются, прочность сворачивается); если эта более ранняя запись отличается флагом миссионного груза или hardpoint, цикл слияния исходного кода никогда не завершается, поэтому смешивать не следует. | [L218](../../../src/Common/common_loadout.cpp#L218), [reader L137](../../../src/Common/common_loadout.cpp#L137) |
| `archetype` | - | - | Проверяется и игнорируется. | [L231](../../../src/Common/common_loadout.cpp#L231) |

Пример (синтетический):

```ini
[Loadout]
nickname = demo_loadout
equip = demo_engine_01
equip = demo_gun_01, HpWeapon01
cargo = demo_commodity, 12
```
