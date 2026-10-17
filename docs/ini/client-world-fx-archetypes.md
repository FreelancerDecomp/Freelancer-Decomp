# Client companion keys of archetype records (`ICliObj::ReadUnknown`, `[LOD]`)

Archetype files (ships, solars, asteroids, equipment, explosions) are read by Common's archetype loader ([archetypes-and-equipment.md](archetypes-and-equipment.md)). In the client every record also carries a **companion** object (`Archetype::ICliObj`, created by the client's factory) that holds the data only the client needs: effect and sound references, cockpit and camera parameters, LOD ranges, light parameters. This page lists the keys the companions understand and the `[LOD]` section. The system-file readers are in [client-world-fx.md](client-world-fx.md), the effects these keys refer to are in [client-world-fx-effects.md](client-world-fx-effects.md).

**File location / how it is found.** The same files and `[Data]` keys as the archetype records (`equipment`, `solar`, `ships`, `asteroids`, `explosions`; see [freelancer-ini.md](freelancer-ini.md)), read through `INI_Reader` ([parsers.md](parsers.md)). Nothing is read by the client from a separate file. Notation (`str`, `id`, `float`, ...) is explained in [client-world-fx.md](client-world-fx.md#notation); `id` values here are nicknames of effects (`[Effect]`), sounds or meshes, hashed with `CreateID`, and **their existence is not checked when the key is read**.

## How the companion is reached

* While a record's section is being read, Common offers every key to the record class first (`Root::read` and the leaf classes, [common_archetype.cpp:468](../../src/Common/common_archetype.cpp#L468)). Only a key that the record class does **not** consume falls through to `Companion->ReadUnknown(reader)` ([common_archetype.cpp:542](../../src/Common/common_archetype.cpp#L542)). A key that Common already handles (for example `ids_info`) therefore never reaches the companion even if the companion code also tests it.
* Each companion class first calls its base class, so the generic keys of section 2 are accepted by every record kind. A key nobody consumes is ignored without a message by the client (what the loader does with the `false` result is described in [archetypes-and-equipment.md](archetypes-and-equipment.md)).
* A `[LOD]` section is not read by Common: the loader hands it to the companion of the last record that was inserted successfully ([common_archetypeloader.cpp:443](../../src/Common/common_archetypeloader.cpp#L443)), so it must follow the record it belongs to.
* Records marked "inherit" copy the donor's companion fields too, so a key given in the donor is inherited ([archetypes-and-equipment.md](archetypes-and-equipment.md)).

The companion class is chosen by the section kind ([FCliObjFactory::Create, client_cliobj.cpp:4328](../../src/freelancer/Client/client_cliobj.cpp#L4328)):

| Section | Companion | Extra keys |
|---|---|---|
| `[Ship]` | `FCliShip` | section 3 |
| `[Solar]` | `FCliSolar` | section 4 |
| `[Asteroid]`, `[AsteroidMine]` | `FCliAsteroid` | none (generic keys only) |
| `[DynamicAsteroid]` | `FCliDynamicAsteroid` | section 5 |
| `[Engine]` | `FCliEngine` | section 6 |
| `[Shield]` | `FCliEQShield` | none |
| `[ShieldGenerator]` | `FCliShieldGenerator` | section 7 |
| `[Thruster]` | `FCliEQThruster` | section 8 |
| `[Gun]` | `FCliGun` (a launcher) | section 9 |
| `[MineDropper]`, `[CounterMeasureDropper]` | `FCliLauncher` | section 9 |
| `[Light]` | `FCliEQLight` | section 10 |
| `[Tractor]` | `FCliTractor` | section 11 |
| `[AttachedFX]` | `FCliEQAttachedFX` | section 12 |
| `[InternalFX]` | `FCliInternalFx` | section 13 |
| `[RepairDroid]` | `FCliRepairDroid` | section 14 |
| `[TradeLane]` | `FCliTradeLane` | section 15 |
| `[Munition]` | `FCliMunition` | section 16 |
| `[Mine]`, `[CounterMeasure]` | `FCliProj` | section 16 (first three keys) |
| `[Simple]`, `[LootCrate]`, `[CargoPod]`, `[Commodity]`, `[Power]`, `[Scanner]`, `[RepairKit]`, `[ShieldBattery]`, `[CloakingDevice]`, `[Armor]` | `FCliObj` | none (generic keys only) |

## 1. `[LOD]` section

[ApplyLodSection, client_cliobj.cpp:147](../../src/freelancer/Client/client_cliobj.cpp#L147). Each `[LOD]` block describes the LOD ranges of one sub-object of the model.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `obj` | str (whole value) | - | Starts a new LOD node for the sub-object with this name (its CRC-32 is stored). **Only the last `obj` of a block is kept**; earlier nodes of the same block are dropped. `LODranges` before the first `obj` apply to an anonymous node (CRC 0). | [L155](../../src/freelancer/Client/client_cliobj.cpp#L155) |
| `LODranges` | up to 8 floats | - | Camera distances at which each successive LOD level of the model is used; the list is closed with -1 internally. At the end of a `[LOD]` block a node whose first range is negative is discarded. Fewer than three values set `MinSpecLOD` to 0. | [L116](../../src/freelancer/Client/client_cliobj.cpp#L116) |
| `MinSpecLOD` | int | 1 | LOD level capped on minimum-spec machines; applies to the current node, so it must follow `obj`/`LODranges`. Ignored when there is no node yet. | [L136](../../src/freelancer/Client/client_cliobj.cpp#L136) |

`LODranges` and `MinSpecLOD` are also accepted directly inside a record (section 2) and then apply to the record's default node.

Example (synthetic):

```ini
[Ship]
nickname = demo_fighter
LODranges = 0, 40, 120, 400

[LOD]
obj = demo_turret
LODranges = 0, 60, 200
MinSpecLOD = 1
```

## 2. Keys accepted by every record (`FCliObj::ReadUnknown`)

[client_cliobj.cpp:1975](../../src/freelancer/Client/client_cliobj.cpp#L1975).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `material_library` | str (whole value), optional int `[2]` | none | Adds a material library (path) to load for the model; repeatable. If a third parameter is present it is read as the library id (otherwise -1), but the **whole value text, commas included,** is still used as the path. | [L1978](../../src/freelancer/Client/client_cliobj.cpp#L1978) |
| `LODranges`, `MinSpecLOD` | see section 1 | - | Same as in a `[LOD]` block. | [L1988](../../src/freelancer/Client/client_cliobj.cpp#L1988) |
| `distance_render` | float | -1 | Stored (and copied by `inherit`), but no code in `src/` reads it back. Not validated. | [L1991](../../src/freelancer/Client/client_cliobj.cpp#L1991) |
| `envmap_material` | str (whole value) | empty | Name of the environment-map material; a non-empty value enables environment mapping for the model ([gf_object_ship.cpp:433](../../src/freelancer/Client/gf_object_ship.cpp#L433)). | [L1997](../../src/freelancer/Client/client_cliobj.cpp#L1997) |
| `surface_hit_effects` | float, then ids (`str[1..]`) | - | `surface_hit_effects = <threshold>, <effect>, <effect>, ...`: one record per key (repeatable). The effects are `[Effect]` nicknames. The record is chosen by comparing a damage value (the damage of a collision, or the `hull_damage` of a munition that hits the object) with the thresholds: the records are scanned in file order and the last one whose threshold is not above the value is used, so list them in ascending order; a value below the first threshold plays nothing; one effect of the chosen record is picked at random ([client_cliobj.cpp:2320](../../src/freelancer/Client/client_cliobj.cpp#L2320), [client_gameobj.cpp:531](../../src/freelancer/Client/client_gameobj.cpp#L531)). | [L2003](../../src/freelancer/Client/client_cliobj.cpp#L2003) |

## 3. `[Ship]`

[FCliShip::ReadUnknown, L2440](../../src/freelancer/Client/client_cliobj.cpp#L2440). The three turn angles are given in degrees, clamped to `0..90` and stored in radians ([ship_camera_params.cpp:20](../../src/freelancer/Client/ship_camera_params.cpp#L20)); a NaN is kept.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `bay_doors_open_snd` | id (`str`) | empty | Sound played when the bay doors open. | [L2446](../../src/freelancer/Client/client_cliobj.cpp#L2446) |
| `bay_doors_close_snd` | id (`str`) | empty | Sound when they close. | [L2452](../../src/freelancer/Client/client_cliobj.cpp#L2452) |
| `cockpit` | cstr (whole value) | empty | Cockpit INI used in first-person view (data-path relative, for example under `cockpits\<nation>\`); read by the camera loaders listed in [client-ui-config.md](client-ui-config.md). | [L2459](../../src/freelancer/Client/client_cliobj.cpp#L2459) |
| `pilot_mesh` | id (`str`) | empty | Mesh of the pilot model. | [L2465](../../src/freelancer/Client/client_cliobj.cpp#L2465) |
| `camera_offset` | float x2 | `0, 0` | Camera offset (two values). | [L2471](../../src/freelancer/Client/client_cliobj.cpp#L2471) |
| `camera_angular_slerp_multiplier` | float | 0.04 | Camera rotation smoothing. | [L2478](../../src/freelancer/Client/client_cliobj.cpp#L2478) |
| `camera_angular_acceleration` | float | 0.07 | Camera angular acceleration. | [L2484](../../src/freelancer/Client/client_cliobj.cpp#L2484) |
| `camera_horizontal_turn_angle` | float (degrees) | 0.2 rad | Horizontal look-ahead angle; clamped `0..90`, stored in radians. | [L2490](../../src/freelancer/Client/client_cliobj.cpp#L2490) |
| `camera_vertical_turn_up_angle` | float (degrees) | 0.2 rad | Upward angle; same treatment. | [L2496](../../src/freelancer/Client/client_cliobj.cpp#L2496) |
| `camera_vertical_turn_down_angle` | float (degrees) | 0.2 rad | Downward angle; same treatment. | [L2502](../../src/freelancer/Client/client_cliobj.cpp#L2502) |
| `camera_turn_look_ahead_slerp_amount` | float | 0.5 | Smoothing of the look-ahead; stored unchanged. | [L2508](../../src/freelancer/Client/client_cliobj.cpp#L2508) |

## 4. `[Solar]`

[FCliSolar::ReadUnknown, L2605](../../src/freelancer/Client/client_cliobj.cpp#L2605).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `shape_name` | str (whole value), max 31 chars | `DEFAULT_TEXTURE` | Name of the nav-map shape used for this solar. | [L2610](../../src/freelancer/Client/client_cliobj.cpp#L2610) |
| `infocard_ids` | int | 0 | Infocard string id (the same field is also written by `ids_info`, but `ids_info` is consumed by Common first and does not reach this code). | [L2629](../../src/freelancer/Client/client_cliobj.cpp#L2629) |
| `open_sound` | id (`str`) | empty | Sound when the object (a door/gate) opens. | [L2635](../../src/freelancer/Client/client_cliobj.cpp#L2635) |
| `close_sound` | id (`str`) | empty | Sound when it closes. | [L2641](../../src/freelancer/Client/client_cliobj.cpp#L2641) |
| `animated_textures` | bool | false | The solar's material uses animated textures. | [L2647](../../src/freelancer/Client/client_cliobj.cpp#L2647) |

## 5. `[DynamicAsteroid]`

[FCliDynamicAsteroid::ReadUnknown, L2868](../../src/freelancer/Client/client_cliobj.cpp#L2868).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `particle_effect` | id (`str`) | none | `[Effect]` nickname of the particle effect attached to the asteroid. Giving the key also sets the "has effect" flag. | [L2874](../../src/freelancer/Client/client_cliobj.cpp#L2874) |

## 6. `[Engine]`

[FCliEngine::ReadUnknown, L3914](../../src/freelancer/Client/client_cliobj.cpp#L3914). All `*_sound` values are sound nicknames and all `*_effect` values are `[Effect]` nicknames. The three `*_range` keys take two integers `low, high`; if `low` is greater than `high` the reader logs "<key> out of order!" and sets `high` to `low`.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `flame_effect` | id (`str[0]`) | empty | Engine flame effect. | [L3919](../../src/freelancer/Client/client_cliobj.cpp#L3919) |
| `trail_effect` | id (`str[0]`) | empty | Engine trail for non-player ships. | [L3925](../../src/freelancer/Client/client_cliobj.cpp#L3925) |
| `trail_effect_player` | id (`str[0]`) | empty | Trail for the player's ship. | [L3931](../../src/freelancer/Client/client_cliobj.cpp#L3931) |
| `cruise_disrupt_effect` | id (`str[0]`) | empty | Effect when cruise is disrupted. | [L3937](../../src/freelancer/Client/client_cliobj.cpp#L3937) |
| `inside_sound_cone` | int | 0 | Inner cone angle of the engine sound (degrees). | [L3943](../../src/freelancer/Client/client_cliobj.cpp#L3943) |
| `outside_sound_cone` | int | 360 | Outer cone angle. | [L3949](../../src/freelancer/Client/client_cliobj.cpp#L3949) |
| `outside_cone_attenuation` | float | 0 | Attenuation outside the outer cone. | [L3955](../../src/freelancer/Client/client_cliobj.cpp#L3955) |
| `rumble_sound` | id (`str`) | empty | Rumble loop. | [L3961](../../src/freelancer/Client/client_cliobj.cpp#L3961) |
| `rumble_pitch_range` | int x2 | `0, 0` | Pitch range of the rumble. | [L3967](../../src/freelancer/Client/client_cliobj.cpp#L3967) |
| `rumble_atten_range` | int x2 | `0, 0` | Attenuation range of the rumble. | [L3973](../../src/freelancer/Client/client_cliobj.cpp#L3973) |
| `character_start_sound` | id (`str`) | empty | Engine start sound. | [L3979](../../src/freelancer/Client/client_cliobj.cpp#L3979) |
| `character_loop_sound` | id (`str`) | empty | Engine running loop. | [L3985](../../src/freelancer/Client/client_cliobj.cpp#L3985) |
| `character_pitch_range` | int x2 | `0, 0` | Pitch range of the loop. | [L3991](../../src/freelancer/Client/client_cliobj.cpp#L3991) |
| `cruise_start_sound` | id (`str`) | empty | Cruise engine start. | [L3998](../../src/freelancer/Client/client_cliobj.cpp#L3998) |
| `cruise_loop_sound` | id (`str`) | empty | Cruise loop. | [L4004](../../src/freelancer/Client/client_cliobj.cpp#L4004) |
| `cruise_stop_sound` | id (`str`) | empty | Cruise stop. | [L4010](../../src/freelancer/Client/client_cliobj.cpp#L4010) |
| `cruise_disrupt_sound` | id (`str`) | empty | Cruise disruption. | [L4016](../../src/freelancer/Client/client_cliobj.cpp#L4016) |
| `cruise_backfire_sound` | id (`str`) | empty | Engine backfire. | [L4022](../../src/freelancer/Client/client_cliobj.cpp#L4022) |
| `engine_kill_sound` | id (`str`) | empty | Engine shut-off. | [L4028](../../src/freelancer/Client/client_cliobj.cpp#L4028) |

## 7. `[ShieldGenerator]`

[FCliShieldGenerator::ReadUnknown, L3620](../../src/freelancer/Client/client_cliobj.cpp#L3620).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `shield_collapse_particle` | id (`str`) | empty | `[Effect]` nickname played when the shield collapses. | [L3625](../../src/freelancer/Client/client_cliobj.cpp#L3625) |
| `shield_collapse_sound` | id (`str`) | empty | Sound on collapse. | [L3631](../../src/freelancer/Client/client_cliobj.cpp#L3631) |
| `shield_rebuilt_sound` | id (`str`) | empty | Sound when the shield is rebuilt. | [L3637](../../src/freelancer/Client/client_cliobj.cpp#L3637) |
| `shield_hit_effects` | float, then ids (`str[1..]`) | - | Same shape and selection rule as `surface_hit_effects` (section 2): `<threshold>, <effect>, ...`, one record per key, repeatable; played when the shield is hit (the compared value is supplied by the shield-hit code). | [L3646](../../src/freelancer/Client/client_cliobj.cpp#L3646) |

## 8. `[Thruster]`

[FCliEQThruster::ReadUnknown, L2978](../../src/freelancer/Client/client_cliobj.cpp#L2978).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `particles` | id (`str`) | empty | `[Effect]` nickname of the thruster flame. | [L2983](../../src/freelancer/Client/client_cliobj.cpp#L2983) |
| `hp_particles` | cstr | empty | Hardpoint name on which the particles are placed. | [L2989](../../src/freelancer/Client/client_cliobj.cpp#L2989) |

## 9. `[Gun]`, `[MineDropper]`, `[CounterMeasureDropper]` (launchers)

[FCliLauncher::ReadUnknown, L3745](../../src/freelancer/Client/client_cliobj.cpp#L3745); `[Gun]` adds `turret_sound` ([FCliGun::ReadUnknown, L3841](../../src/freelancer/Client/client_cliobj.cpp#L3841)). Loading a launcher also loads the companion of the equipment named by its `projectile_archetype`.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `flash_radius` | float | 4.0 | Radius of the muzzle-flash light. | [L3750](../../src/freelancer/Client/client_cliobj.cpp#L3750) |
| `light_anim` | str (whole value), max 31 chars | empty | Name of a light animation from `fx\lightanim.ini` ([client-world-fx-effects.md](client-world-fx-effects.md#2-light-animations-fxlightanimini)). | [L3756](../../src/freelancer/Client/client_cliobj.cpp#L3756) |
| `flash_particle_name` | id (`str`) | empty | `[Effect]` nickname of the muzzle flash. | [L3762](../../src/freelancer/Client/client_cliobj.cpp#L3762) |
| `dry_fire_sound` | id (`str`) | empty | Sound when firing with no energy/ammunition. | [L3768](../../src/freelancer/Client/client_cliobj.cpp#L3768) |
| `turret_sound` (`[Gun]` only) | id (`str`) | empty | Turret rotation sound. | [L3846](../../src/freelancer/Client/client_cliobj.cpp#L3846) |

## 10. `[Light]`

[FCliEQLight::ReadUnknown, L3074](../../src/freelancer/Client/client_cliobj.cpp#L3074). Colours are 0..255 floats divided by 255.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `color` | float x3 | `0, 0, 0` | Bulb colour. While `glow_color` is still all zero, `color` also sets it. | [L3082](../../src/freelancer/Client/client_cliobj.cpp#L3082) |
| `glow_color` | float x3 | `0, 0, 0` | Glow colour. | [L3103](../../src/freelancer/Client/client_cliobj.cpp#L3103) |
| `min_color` | float x3 | `0, 0, 0` | Colour at the dimmest point of a blink. | [L3114](../../src/freelancer/Client/client_cliobj.cpp#L3114) |
| `avg_delay` | float | 0 | Average delay between blinks. | [L3125](../../src/freelancer/Client/client_cliobj.cpp#L3125) |
| `intensity` | float | 0 | Light intensity. | [L3131](../../src/freelancer/Client/client_cliobj.cpp#L3131) |
| `bulb_size` | float | 1.0 | Size of the bulb sprite. | [L3137](../../src/freelancer/Client/client_cliobj.cpp#L3137) |
| `glow_size` | float | 1.0 | Size of the glow sprite. | [L3143](../../src/freelancer/Client/client_cliobj.cpp#L3143) |
| `blink_duration` | float | 0.3 | Duration of a blink. | [L3149](../../src/freelancer/Client/client_cliobj.cpp#L3149) |
| `flare_cone` | float x2 (degrees) | cosines `-1, -1` | Two cone angles; the cosine of value 1 and of value 0 are stored. | [L3155](../../src/freelancer/Client/client_cliobj.cpp#L3155) |
| `lightsource_cone` | float | 180 | Cone angle of the light source (degrees, stored unchanged). | [L3169](../../src/freelancer/Client/client_cliobj.cpp#L3169) |

## 11. `[Tractor]`

[FCliTractor::ReadUnknown, L2909](../../src/freelancer/Client/client_cliobj.cpp#L2909).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `color` | byte x3 | `255, 255, 255` | Beam colour (low 8 bits of each truncated value). | [L2915](../../src/freelancer/Client/client_cliobj.cpp#L2915) |
| `operating_effect` | id (`str`) | empty | `[Effect]` nickname of the beam effect. | [L2923](../../src/freelancer/Client/client_cliobj.cpp#L2923) |
| `tractor_complete_snd` | id (`str`) | empty | Sound when a pull completes. | [L2929](../../src/freelancer/Client/client_cliobj.cpp#L2929) |

## 12. `[AttachedFX]`

[FCliEQAttachedFX::ReadUnknown, L2800](../../src/freelancer/Client/client_cliobj.cpp#L2800).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `particles` | id (`str`) | empty | `[Effect]` nickname of the attached effect. | [L2805](../../src/freelancer/Client/client_cliobj.cpp#L2805) |
| `use_throttle` | bool | false | The effect follows the throttle. | [L2811](../../src/freelancer/Client/client_cliobj.cpp#L2811) |

## 13. `[InternalFX]`

[FCliInternalFx::ReadUnknown, L3267](../../src/freelancer/Client/client_cliobj.cpp#L3267). Unlike `[Engine]`, the cone values are floats.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `use_sound` | id (`str`) | empty | Sound played when the equipment is used. | [L3272](../../src/freelancer/Client/client_cliobj.cpp#L3272) |
| `inside_sound_cone` | float | 360 | Inner cone angle. | [L3278](../../src/freelancer/Client/client_cliobj.cpp#L3278) |
| `outside_sound_cone` | float | 360 | Outer cone angle. | [L3284](../../src/freelancer/Client/client_cliobj.cpp#L3284) |
| `outside_cone_attenuation` | float | 0 | Attenuation outside the outer cone. | [L3290](../../src/freelancer/Client/client_cliobj.cpp#L3290) |

## 14. `[RepairDroid]`

[FCliRepairDroid::ReadUnknown, L3554](../../src/freelancer/Client/client_cliobj.cpp#L3554).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `active_effect` | id (`str`) | empty | `[Effect]` nickname played while repairing. | [L3559](../../src/freelancer/Client/client_cliobj.cpp#L3559) |

## 15. `[TradeLane]`

[FCliTradeLane::ReadUnknown, L4137](../../src/freelancer/Client/client_cliobj.cpp#L4137): sixteen id keys (table at L4148) and seven float keys (table at L4183). All `tl_*` values are stored as hashed nicknames (effects or sounds) and looked up when a lane is used.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `tl_player_enter`, `tl_player_travel`, `tl_player_splash`, `tl_player_exit`, `tl_player_disrupt` | id (`str`) | empty | Effects (or sounds) for the player in a lane: enter, travel, splash, exit, disrupt. | [L4149](../../src/freelancer/Client/client_cliobj.cpp#L4149) |
| `tl_ship_enter`, `tl_ship_travel`, `tl_ship_splash`, `tl_ship_exit`, `tl_ship_disrupt` | id (`str`) | empty | The same for other ships. | [L4154](../../src/freelancer/Client/client_cliobj.cpp#L4154) |
| `tl_ring_active`, `tl_ring_shield`, `tl_ring_tube` | id (`str`) | empty | Effects of the lane ring (active, shield, tube). | [L4159](../../src/freelancer/Client/client_cliobj.cpp#L4159) |
| `tl_ring_active_start`, `tl_ring_active_loop`, `tl_ring_active_end` | id (`str`) | empty | Ring activation start, loop and end (effect or sound nicknames; the loader does not distinguish them). | [L4162](../../src/freelancer/Client/client_cliobj.cpp#L4162) |
| `secs_before_enter` | float | 1.0 | Delay before the enter effect. | [L4184](../../src/freelancer/Client/client_cliobj.cpp#L4184) |
| `secs_before_exit` | float | 1.0 | Delay before the exit effect. | [L4185](../../src/freelancer/Client/client_cliobj.cpp#L4185) |
| `secs_before_splash` | float | 1.0 | Delay before the splash effect. | [L4186](../../src/freelancer/Client/client_cliobj.cpp#L4186) |
| `activation_start` | float | 600 | Activation parameter (start). | [L4187](../../src/freelancer/Client/client_cliobj.cpp#L4187) |
| `activation_end` | float | 300 | Activation parameter (end). | [L4188](../../src/freelancer/Client/client_cliobj.cpp#L4188) |
| `spin_max` | float | 50 | Maximum spin of the ring. | [L4189](../../src/freelancer/Client/client_cliobj.cpp#L4189) |
| `spin_accel` | float | 25 | Spin acceleration of the ring. | [L4190](../../src/freelancer/Client/client_cliobj.cpp#L4190) |

## 16. `[Munition]`, `[Mine]`, `[CounterMeasure]`

`[Mine]` and `[CounterMeasure]` use `FCliProj` ([L2733](../../src/freelancer/Client/client_cliobj.cpp#L2733)) and accept the first three keys; `[Munition]` (`FCliMunition`, [L3333](../../src/freelancer/Client/client_cliobj.cpp#L3333)) accepts all five.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `const_effect_delay` | float | 0 | Delay before the constant (trail) effect starts. | [L2739](../../src/freelancer/Client/client_cliobj.cpp#L2739) |
| `const_effect` | id (`str`) | empty | `[Effect]` nickname of the effect that accompanies the projectile. | [L2745](../../src/freelancer/Client/client_cliobj.cpp#L2745) |
| `one_shot_sound` | id (`str`) | empty | Sound played once when the projectile is launched. | [L2751](../../src/freelancer/Client/client_cliobj.cpp#L2751) |
| `munition_hit_effect` (munition only) | id (`str`) | - | `[Effect]` nickname played on impact; repeatable, all are kept and one is chosen at random. | [L3356](../../src/freelancer/Client/client_cliobj.cpp#L3356) |
| `HP_trail_parent` (munition only) | cstr | empty | Hardpoint of the launcher that the trail follows. | [L3362](../../src/freelancer/Client/client_cliobj.cpp#L3362) |

Example (synthetic):

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
