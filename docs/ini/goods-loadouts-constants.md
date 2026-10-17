# constants.ini, goods files and loadout files

This page documents three data tables read by Common.dll: the global tuning constants (`constants.ini`), the trade goods (`[Good]` sections of the `goods` files) and the equipment loadouts (`[Loadout]` sections). Archetype definitions that these tables refer to are described in [archetypes-and-equipment.md](archetypes-and-equipment.md); value notation (`int`, `float`, `bool`, `id`, `cstr`) is explained there.

**File location / how it is found.** All three are listed in `freelancer.ini` `[Data]` ([freelancer-ini.md](freelancer-ini.md)) and resolved against the data path:

| `[Data]` key | Dev-ini value(s) | Reader |
|---|---|---|
| `constants` | `constants.ini` | `ReadConstants` ([common_constants.cpp:270](../../src/Common/common_constants.cpp#L270)) |
| `goods` (repeated) | `equipment\goods.ini`, `engine_good.ini`, `misc_good.ini`, `st_good.ini`, `weapon_good.ini` | `GoodInfoList::load` ([common_goodinfo.cpp:538](../../src/Common/common_goodinfo.cpp#L538)) |
| `loadouts` (repeated) | `ships\loadouts.ini`, `loadouts_special.ini`, `loadouts_utility.ini`, `solar\loadouts.ini` | `Loadout::Load` ([common_loadout.cpp:176](../../src/Common/common_loadout.cpp#L176)) |

Section and key names are case-insensitive (see [parsers.md](parsers.md)). Per-base market price tables (`market_*.ini`) are a different file type, see [systems-and-bases.md](systems-and-bases.md).

---

## constants.ini

A missing/unopenable file makes `ReadConstants` return false without a message; otherwise every recognised section is applied. Within every section **except** `[CollisionConsts]`, a key that is not in the section's table logs "Unknown Constant definition: <line>". Each constant has a built-in default; a key that is absent leaves it. Only the first value (`[0]`) of a scalar key is used. Sections can be repeated; later keys overwrite earlier ones.

Types: `float` (`get_value_float`), `int` (float parsed, then truncated toward zero), `double`, `id` (`CreateID` of the whole value).

### `[Constants]`  ([common_constants.cpp:386](../../src/Common/common_constants.cpp#L386))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `TestValue` | float | 3.1415 | Read, never used. | [L279](../../src/Common/common_constants.cpp#L279) |
| `TestString` | str (whole value, at most 127 chars) | `sample` | Read, never used. | [L390](../../src/Common/common_constants.cpp#L390) |
| `COLLISION_DAMAGE_FACTOR` | float | 0.25 | Global scale of collision damage. | [L281](../../src/Common/common_constants.cpp#L281) |
| `LOOT_OWNER_SAFE_TIME` | float | 1.0 | Seconds the dropper cannot pick up its own loot. | [L282](../../src/Common/common_constants.cpp#L282) |
| `LOOT_UNSEEN_LIFE_TIME` | float | 60.0 | Lifetime of loot nobody can see. | [L283](../../src/Common/common_constants.cpp#L283) |
| `LOOT_UNSEEN_RADIUS` | float | 2500.0 | Radius of the "seen" test for loot. | [L284](../../src/Common/common_constants.cpp#L284) |
| `MUSIC_CROSS_FADE_DELAY` | float | 30.0 | Music cross-fade delay. | [L285](../../src/Common/common_constants.cpp#L285) |
| `JETTISONED_CARGO_VELOCITY` | float | 10.0 | Speed of jettisoned cargo. | [L286](../../src/Common/common_constants.cpp#L286) |
| `MUZZLE_CONE_ANGLE` | float | 90.0 | Muzzle cone angle. | [L287](../../src/Common/common_constants.cpp#L287) |
| `FIRE_FAILED_DELAY` | float | 1.0 | Seconds between "fire failed" sounds. | [L288](../../src/Common/common_constants.cpp#L288) |
| `PLAYER_COLLISION_GROUP_HIT_PTS_SCALE` | float | 1.0 | Scale of collision-group hit points for the player. | [L289](../../src/Common/common_constants.cpp#L289) |
| `PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE` | float | 1.0 | Scale of attached-equipment hit points for the player. | [L290](../../src/Common/common_constants.cpp#L290) |
| `SND_CARGO_JETTISONED` | id | `cargo_jettison` | Sound nickname. | [L293](../../src/Common/common_constants.cpp#L293) |
| `FIRE_FAILED_SOUND` | id | `fire_dry` | Sound nickname. | [L294](../../src/Common/common_constants.cpp#L294) |
| `MAX_PLAYER_AMMO` | int | 50 | Ammunition cap of the player. | [L297](../../src/Common/common_constants.cpp#L297) |

### `[EngineEquipConsts]`  ([L415](../../src/Common/common_constants.cpp#L415))

All `float` ([table L300](../../src/Common/common_constants.cpp#L300)). Defaults: `CRUISING_SPEED` 300, `CRUISE_ACCEL_TIME` 5, `CRUISE_DRAG` 3, `CRUISE_DISRUPT_TIME` 0, `MAX_DELTA_FX_THROTTLE` 0.1, `MAX_ENGINE_FX_THROTTLE` 0.9, and 0 for `THROTTLE_STEADY_TIME`, `THROTTLE_ATTEN_MOD_RANGE`, `DELTA_THROTTLE_ATTEN_MOD_CHANGING`, `DELTA_THROTTLE_ATTEN_MOD_STEADY`, `CRUISE_STEADY_TIME`, `DELTA_CRUISE_ATTEN_MOD_STEADY`, `CRUISE_ATTEN_MOD_RANGE`. They tune the cruise engine speed/charge and the engine-sound throttle attenuation.

### `[ShieldEquipConsts]`  ([L424](../../src/Common/common_constants.cpp#L424))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `HULL_DAMAGE_FACTOR` | float | 1.0 | Factor applied to damage that passes to the hull. | [L317](../../src/Common/common_constants.cpp#L317) |

### `[ThrusterEquipConsts]`  ([L433](../../src/Common/common_constants.cpp#L433))

| Key | Type | Default | Meaning | Source |
|---|---|---|---|---|
| `INTERIOR_SOUND_NAME` | id | `interior_thruster_sound` | Sound nickname. | [L329](../../src/Common/common_constants.cpp#L329) |
| `EXTERIOR_SOUND_NAME` | id | `exterior_thruster_sound` | Sound nickname. | [L330](../../src/Common/common_constants.cpp#L330) |
| `MAX_VOLUME_FORCE` | float | 20000 | Thrust at which the sound is loudest. | [L321](../../src/Common/common_constants.cpp#L321) |
| `MIN_VOLUME_FORCE` | float | 5000 | Thrust at which the sound starts. | [L322](../../src/Common/common_constants.cpp#L322) |
| `INSIDE_CONE_ANGLE` | float | 60 | Inner sound cone angle. | [L323](../../src/Common/common_constants.cpp#L323) |
| `OUTSIDE_CONE_ANGLE` | float | 220 | Outer sound cone angle. | [L324](../../src/Common/common_constants.cpp#L324) |
| `OUTSIDE_CONE_ATTENUATION` | float | -10 | Attenuation outside the cone. | [L325](../../src/Common/common_constants.cpp#L325) |

### `[PhySysConsts]`  ([L444](../../src/Common/common_constants.cpp#L444))

Physics tuning. `float` unless noted ([tables L333](../../src/Common/common_constants.cpp#L333)).

| Key | Type | Default | Source |
|---|---|---|---|
| `PHYSICAL_SIM_RATE` | float | 16 | [L334](../../src/Common/common_constants.cpp#L334) |
| `MAX_SPAWNED_MINDIST_COUNT` | int | 100 | [L365](../../src/Common/common_constants.cpp#L365) |
| `ANOM_LIMITS_MAX_VELOCITY` | float | 1000 | [L335](../../src/Common/common_constants.cpp#L335) |
| `ANOM_LIMITS_MAX_ANGULAR_VELOCITY_PER_PSI` | float | 0.94247788 | [L336](../../src/Common/common_constants.cpp#L336) |
| `MATERIAL_FRICTION` | double | 0.1 | [L361](../../src/Common/common_constants.cpp#L361) |
| `MATERIAL_ELASTICITY` | double | 0.9 | [L362](../../src/Common/common_constants.cpp#L362) |
| `MIN_TIME_BETWEEN_COLLISIONS` | double | 0.5 | [L363](../../src/Common/common_constants.cpp#L363) |
| `RMGR_LOOK_AHEAD_TIME_WORLD` / `_MAX_RADIUS_WORLD` / `_MIN_DISTANCE_WORLD` / `_MAX_DISTANCE_WORLD` | float | 1.0 / 5.0 / 100 / 1000 | [L337](../../src/Common/common_constants.cpp#L337) |
| `RMGR_LOOK_AHEAD_MIN_SECONDS_WORLD` | float | 2.1 / 16 (computed once at start-up; not recomputed if `PHYSICAL_SIM_RATE` is overridden) | [L341](../../src/Common/common_constants.cpp#L341) |
| `RMGR_LOOK_AHEAD_TIME_INTRA` / `_MAX_RADIUS_INTRA` / `_MIN_DISTANCE_INTRA` / `_MAX_DISTANCE_INTRA` | float | 0.5 / 0.9 / 5 / 50 | [L342](../../src/Common/common_constants.cpp#L342) |
| `RMGR_LOOK_AHEAD_MIN_SECONDS_INTRA` | float | 2.1 / 16 (as above) | [L346](../../src/Common/common_constants.cpp#L346) |
| `DEFAULT_LINEAR_DAMPING` | float | 1.0 | [L347](../../src/Common/common_constants.cpp#L347) |
| `DEFAULT_ANGULAR_DAMPING` | float x3 (`[0]`,`[1]`,`[2]`) | 2, 2, 2 | [L448](../../src/Common/common_constants.cpp#L448) |
| `GOLEM_CHILD_MASS` | float | 100000 | [L348](../../src/Common/common_constants.cpp#L348) |
| `GOLEM_CHILD_LINEAR_DAMP` | float | 1.0 | [L349](../../src/Common/common_constants.cpp#L349) |
| `GOLEM_CHILD_ANGULAR_DAMP` | float x3 | 1, 1, 1 | [L455](../../src/Common/common_constants.cpp#L455) |
| `GOLEM_MAX_DELTA_POSITION` | float | 10 | [L350](../../src/Common/common_constants.cpp#L350) |
| `GOLEM_DELTA_ORIENTATION` | float | 1.5707964 | [L351](../../src/Common/common_constants.cpp#L351) |
| `GOLEM_MAX_TRANSLATION_FORCE` | float x3 | 1e12, 1e12, 1e12 | [L462](../../src/Common/common_constants.cpp#L462) |
| `GOLEM_MAX_TORQUE` | float | 1e12 | [L352](../../src/Common/common_constants.cpp#L352) |
| `GOLEM_FORCE_FACTOR` / `GOLEM_TORQUE_FACTOR` | float | 0.8 / 0.8 | [L353](../../src/Common/common_constants.cpp#L353) |
| `GOLEM_DAMP_FACTOR` / `GOLEM_ANGULAR_DAMP_FACTOR` | float | 1.0 / 1.0 | [L354](../../src/Common/common_constants.cpp#L354) |
| `RC_MAX_DELTA_POSITION` / `RC_MAX_DELTA_ORIENTATION` | float | 50 / 5 | [L357](../../src/Common/common_constants.cpp#L357) |

("Golem" is the physics controller of a rigid body; "RMGR" the region manager; "RC" the remote-controlled-body correction.)

### `[CommConsts]`  ([L478](../../src/Common/common_constants.cpp#L478))

Voice/chatter audibility ([tables L369](../../src/Common/common_constants.cpp#L369)).

| Key | Type | Default |
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

### `[CollisionConsts]`  ([common_physys.cpp:317](../../src/Common/common_physys.cpp#L317))

Handled by `Collision::Load` (called from `ReadConstants` for this header). Unknown keys are ignored without a message.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `damage` | `[0]` object-type name, `[1]` float | none | Collision damage inflicted by an object of that type. `[0]` is one of the object-type names of [archetypes-and-equipment.md](archetypes-and-equipment.md#object-type-names-type-); an unknown name logs a warning and, like `NONE`, is skipped. Repeated keys for one type: the **first** wins. | [L324](../../src/Common/common_physys.cpp#L324) |
| `max_impact_speed` | float | 250.0 | Reference impact speed of the collision-damage formula (`Collision::CalcCollisionDamage` divides by this value). | [L334](../../src/Common/common_physys.cpp#L334) |

Example (synthetic):

```ini
[PhySysConsts]
PHYSICAL_SIM_RATE = 16
DEFAULT_ANGULAR_DAMPING = 2, 2, 2

[CollisionConsts]
damage = FIGHTER, 10
max_impact_speed = 250
```

---

## Goods files: `[Good]`

A goods file is a sequence of `[Good]` sections ([common_goodinfo.cpp:303](../../src/Common/common_goodinfo.cpp#L303)). An unopenable file logs "Cannot open goods file". A `[GoodType]` section is obsolete and logs a warning; other sections are ignored. All goods files are merged into one global list. Prices are stored as `float`.

**Order dependence.** `good_buy_price`, `bad_buy_price`, `good_sell_price`, `bad_sell_price` are **multipliers applied to the `price` read so far**, so they must appear after `price`. `price` fills any of the four derived prices that are still 0 with `price * 0.95` (good buy, bad sell) and `price * 1.05` (bad buy, good sell).

A `[Good]` whose `nickname` already exists in the global list is abandoned at that key: the rest of its keys are not read, and an empty record is still added to the list.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | id (whole value) | hash of null (0) | Good key. | [L322](../../src/Common/common_goodinfo.cpp#L322) |
| `equipment` | id (whole value) | 0 | Nickname of the `Commodity` / equipment archetype the good stands for. Required for categories `Commodity` and `Equipment`; the archetype is looked up with no null check. After the section the good's `ids_name` / `ids_info` default to the archetype's, and for attached equipment `attachment_archetype` and `HP_child` default to the archetype's. | [L333](../../src/Common/common_goodinfo.cpp#L333) |
| `category` | `Commodity` \| `Equipment` \| `ShipHull` \| `Ship` | unknown (0xFFFFFFFF) | Case-insensitive. Any other value logs "unknown value: category = %s" and leaves the category unknown. Only `Commodity` and `Equipment` take their archetype id from `equipment`; the other categories get archetype id 0. | [L350](../../src/Common/common_goodinfo.cpp#L350) |
| `ship` | id (whole value) | 0 | Ship archetype (for `Ship` goods). | [L372](../../src/Common/common_goodinfo.cpp#L372) |
| `hull` | nickname of another good | 0 | For a `Ship` good: its `ShipHull` good, which must already be loaded; a miss crashes (retail behaviour). | [L439](../../src/Common/common_goodinfo.cpp#L439) |
| `price` | int | 0 | Base price. See the order note above. | [L376](../../src/Common/common_goodinfo.cpp#L376) |
| `good_buy_price`, `bad_buy_price`, `good_sell_price`, `bad_sell_price` | float multiplier | derived from `price` | Multiplier of `price`. | [L396](../../src/Common/common_goodinfo.cpp#L396), [L403](../../src/Common/common_goodinfo.cpp#L403), [L407](../../src/Common/common_goodinfo.cpp#L407), [L411](../../src/Common/common_goodinfo.cpp#L411) |
| `jump_dist` | int | 50 | Jump distance used by jump-related checks. | [L415](../../src/Common/common_goodinfo.cpp#L415) |
| `ids_name` or `name` | int | archetype's (0 if none) | Display-name string id; both spellings are accepted. | [L342](../../src/Common/common_goodinfo.cpp#L342) |
| `ids_info` or `info_desc` | int | archetype's | Infocard string id; both spellings are accepted. | [L346](../../src/Common/common_goodinfo.cpp#L346) |
| `shop_archetype` | cstr | empty (falls back to `attachment_archetype`) | Model shown in the shop. | [L419](../../src/Common/common_goodinfo.cpp#L419) |
| `material_library` | cstr | empty | Material library of the shop model. | [L423](../../src/Common/common_goodinfo.cpp#L423) |
| `item_icon` | cstr | empty | Icon model. | [L427](../../src/Common/common_goodinfo.cpp#L427) |
| `HP_child` | cstr | empty | Child hardpoint. | [L431](../../src/Common/common_goodinfo.cpp#L431) |
| `attachment_archetype` | cstr | empty | Attachment model. | [L435](../../src/Common/common_goodinfo.cpp#L435) |
| `addon` | `[0]` equipment nickname, `[1]` hardpoint or `internal`, `[2]` int count, optional `[3]` slot | none | An extra item that is added to a bought ship. Repeated keys append. `[1]` `internal` (case-insensitive) mounts in the cargo bay, any other text is a hardpoint name. `[2]` of 0 or less (or missing) becomes 1. `[3]`: `mid` or `high` (case-insensitive) selects the mid/high add-on list; any other value or none selects the default list. | [L451](../../src/Common/common_goodinfo.cpp#L451) |
| `combinable` | bool | false | Several units may be merged into one cargo stack. | [L496](../../src/Common/common_goodinfo.cpp#L496) |
| `free_ammo` | `[0]` munition nickname (id), `[1]` int | 0, 0 | Ammunition given for free with the item. | [L337](../../src/Common/common_goodinfo.cpp#L337) |
| `msg_id_prefix` | str | empty | Comm message prefix (`CommReferrable`), tried for every other key. | [L504](../../src/Common/common_goodinfo.cpp#L504) |

If a good's `nickname` differs from its `equipment` (both non-zero) the warning "Good (%s) nickname different from equipment" is logged.

Example (synthetic):

```ini
[Good]
nickname = demo_commodity
equipment = demo_commodity
category = commodity
price = 200
good_sell_price = 1.2
```

---

## Loadout files: `[Loadout]`

A loadout is a named list of equipment and cargo, referenced by id from solar archetypes, system objects, NPC definitions and good `addon`s. The file path is opened as given by the loader (the dispatcher already resolved it against the data path); an unopenable file logs "unable to open Loadout file". Sections other than `[Loadout]` are skipped.

Rules ([common_loadout.cpp:176](../../src/Common/common_loadout.cpp#L176)):

* Only the **first** `nickname` of a section counts; later `nickname` lines are ignored. A second `[Loadout]` with an already-used nickname **replaces** the earlier content (the item list is cleared and refilled).
* `equip` and `cargo` before `nickname` dereference a missing loadout (a crash, as in retail).
* Items receive consecutive ids in file order.
* The key `archetype` is tested and ignored. All other keys are ignored.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | id (whole value) | none | Loadout key. | [L192](../../src/Common/common_loadout.cpp#L192) |
| `equip` | `[0]` equipment, optional `[1]` hardpoint, optional `[2]` float health | - | Mounted item. `[0]` is the archetype nickname (hashed) **or** a decimal id: a value that parses as a non-zero integer is used as the id (note that `atoi` accepts a leading number, so a nickname such as `2fast` would be read as 2), else it is hashed. Without `[1]` the item sits in the cargo bay (`BAY`, "internal"); with `[1]` it is mounted on that hardpoint. `[2]` is the health (default 1.0). Marked as equipped. | [L210](../../src/Common/common_loadout.cpp#L210), [reader L111](../../src/Common/common_loadout.cpp#L111) |
| `cargo` | `[0]` item, optional `[1]` int count, optional `[2]` hardpoint, optional `[3]` float health, optional `[4]` bool mission | count 1, health 1.0, mission false | Not-mounted cargo. `[0]` as for `equip`. `[2]` empty means the cargo bay. **If `[2]` parses as a non-zero number, the rest of the line (hardpoint, health, mission) is ignored.** If the item's good is `combinable` and sits in the cargo bay, an earlier entry of the same archetype is merged (counts added, health folded); if that earlier entry differs in mission flag or hardpoint, the merge loop of the original code never terminates, so avoid mixing. | [L218](../../src/Common/common_loadout.cpp#L218), [reader L137](../../src/Common/common_loadout.cpp#L137) |
| `archetype` | - | - | Tested and ignored. | [L231](../../src/Common/common_loadout.cpp#L231) |

Example (synthetic):

```ini
[Loadout]
nickname = demo_loadout
equip = demo_engine_01
equip = demo_gun_01, HpWeapon01
cargo = demo_commodity, 12
```
