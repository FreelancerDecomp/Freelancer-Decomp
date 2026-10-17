# Client visual effects, light animations, gate/jump effects and fuses

This page documents the client-side effect configuration files: the effect archive files named by `[Data] effects` (`[Effect]`, `[VisEffect]`, `[BeamSpear]`, `[BeamBolt]`, `[EffectType]`, `[EffectLOD]`), `fx\lightanim.ini`, the gate tunnel file (`[Data] gate_tunnels`), the jump effect file (`[Data] jump_effect`) and the fuse files (`[Data] fuses`) as the client reads them. The system-file, star, asteroid and nebula readers are in [client-world-fx.md](client-world-fx.md); the client-side keys of archetype records (which reference effects by nickname) are in [client-world-fx-archetypes.md](client-world-fx-archetypes.md).

**File location / how it is found.** All files are resolved against the data path (`GetFileSysPath(.., nullptr)`, see [freelancer-ini.md](freelancer-ini.md)). Which file is read comes from `freelancer.ini [Data]`:

| `[Data]` key | Read by | When |
|---|---|---|
| `effects` (repeatable) | `EffectArchLoadIni(path, false)` ([vis_effect_arch.cpp:775](../../src/freelancer/Client/vis_effect_arch.cpp#L775)) via `LoadClientDataValue` ([dataconfig_progress.cpp:129](../../src/freelancer/Client/dataconfig_progress.cpp#L129)) | Client start-up, in the order the keys appear in `[Data]`. |
| `effect_shapes` | queued, loaded into the effect shape library (format in [client-ui-config.md](client-ui-config.md)) | End of engine initialisation; the shape names of gate tunnel layers are looked up there. |
| `gate_tunnels` | `GateTunnelIniLoad` ([gate_tunnel.cpp:1048](../../src/freelancer/Client/gate_tunnel.cpp#L1048)); the value is captured by `LoadStartupConfig` ([startup_config.cpp:128](../../src/freelancer/Client/startup_config.cpp#L128)) | At every system load of the player (space entry), before the system file is read ([space_state.cpp:927](../../src/freelancer/Client/space_state.cpp#L927)). |
| `jump_effect` | `JumpEffectIniLoad` ([jump_effect.cpp:220](../../src/freelancer/Client/jump_effect.cpp#L220)); value captured as above | Each time a system is entered, before the system file is read ([space_scene_load.cpp:24](../../src/freelancer/Client/space_scene_load.cpp#L24)). |
| `fuses` (repeatable) | queued by Common (`FuseDB::AddFuseINIFile`), loaded by `ClientFuseDB::LoadAll` ([client_fuse.cpp:941](../../src/freelancer/Client/client_fuse.cpp#L941)); the server reads the same files ([server.md](server.md)) | At every system load of the player (space entry), before the system file is read ([space_state.cpp:925](../../src/freelancer/Client/space_state.cpp#L925)). |
| (literal) `fx\lightanim.ini` | `LightAnimationLoad` ([engine_light.cpp:1387](../../src/freelancer/Client/engine_light.cpp#L1387)) | At every system load of the player (space entry), before the system file is read ([space_state.cpp:922](../../src/freelancer/Client/space_state.cpp#L922)). |

A file that does not open is skipped silently by all of these loaders (no message). Header and key names are case-insensitive; the notation (`str`, `id`, `float`, ...) is the one explained in [client-world-fx.md](client-world-fx.md#notation).

## 1. Effect archive files (`[Data] effects`)

`EffectArchLoadIni` reads one file as a flat list of sections in file order and dispatches on the header; unknown headers are skipped. All references between sections are resolved **while the key is read**, so the target must already be loaded: an earlier section of the same file or an earlier `effects` file (and, for `snd_effect`, an earlier `sounds` file in `[Data]`). A `[VisEffect]` or beam therefore has to precede the `[Effect]` that names it.

| Header | Reader | Purpose |
|---|---|---|
| `[Effect]` | `LoadEffectSection` ([L658](../../src/freelancer/Client/vis_effect_arch.cpp#L658)) | An effect archetype: ties a visual effect, a sound and a light together under one nickname. This is the nickname that weapons, engines, explosions, fuses etc. refer to. |
| `[VisEffect]` | `VisEffectLoadSection` ([L727](../../src/freelancer/Client/vis_effect_arch.cpp#L727)) | A particle/alchemy visual effect and its texture libraries. |
| `[BeamSpear]`, `[BeamBolt]` | `LoadBeamSection` ([L359](../../src/freelancer/Client/vis_effect_arch.cpp#L359)) | Beam appearance (projectile streaks). |
| `[EffectType]` | `LoadEffectTypeSection` ([effect_type_info.cpp:125](../../src/freelancer/Client/effect_type_info.cpp#L125)) | Overrides built-in per-type parameters (priority, LOD, radius, ...). |
| `[EffectLOD]` | `LoadEffectLodSection` ([effect_type_info.cpp:225](../../src/freelancer/Client/effect_type_info.cpp#L225)) | Screen-size thresholds for the LOD classes. |

### 1.1 `[Effect]`

[EffectArchParseValue, vis_effect_arch.cpp:401](../../src/freelancer/Client/vis_effect_arch.cpp#L401). The registry is keyed by `nickname`. A nickname that already exists logs "[Effect] nickname = %s value is not unique. Aborted second version." and the second section is discarded (the reload/merge path is never selected by the data loader). Unknown keys are ignored without a message. `nickname` has no special position, but an effect without one is registered under id 0.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | id (`str`) | 0 | Registry key. | [L404](../../src/freelancer/Client/vis_effect_arch.cpp#L404) |
| `effect_type` | str (whole value), exact match | `EFT_NONE` (index 0) | One of the built-in type names in the table below, compared with a **case-sensitive** `strcmp`. An unknown name logs the error "unknown effect type" (plus an `INI_Reader` link message) and the effect is stored with the invalid index 47. When the type's `run_time` is negative, bit 0 of the effect's flags is set (cleared otherwise). | [L410](../../src/freelancer/Client/vis_effect_arch.cpp#L410) |
| `vis_effect` | str (whole value) | none | Nickname of an already loaded `[VisEffect]`. A miss logs "[Effect] %s couldn't find [VisEffect] %s." | [L442](../../src/freelancer/Client/vis_effect_arch.cpp#L442) |
| `vis_generic` | str (whole value) | none | Second `[VisEffect]`, the "generic" variant: it replaces `vis_effect` when the generic-effects performance level is above the type's `priority` but not above its `generic_priority` ([particle_effect.cpp:283](../../src/freelancer/Client/particle_effect.cpp#L283)). Same lookup and message as `vis_effect`. | [L457](../../src/freelancer/Client/vis_effect_arch.cpp#L457) |
| `vis_beam` | str (whole value) | none | Nickname of an already loaded `[BeamSpear]`/`[BeamBolt]`. A miss logs "[Effect] %s couldn't find [VisBeam] %s." | [L469](../../src/freelancer/Client/vis_effect_arch.cpp#L469) |
| `snd_effect` | id (`str`) | none | Sound archetype nickname. A miss logs "[Effect] %s couldn't find [Sound] %s." | [L481](../../src/freelancer/Client/vis_effect_arch.cpp#L481) |
| `lgt_effect` | str (whole value), max 31 chars | empty | Name of a light animation (`[LightAnim]` `name`, section 2) played with the effect. The name is looked up when the effect runs, not at load time ([particle_effect.cpp:1093](../../src/freelancer/Client/particle_effect.cpp#L1093)). | [L497](../../src/freelancer/Client/vis_effect_arch.cpp#L497) |
| `lgt_range_scale` | float | 1.0 | Scale of the light range. | [L515](../../src/freelancer/Client/vis_effect_arch.cpp#L515) |
| `lgt_time_scale` | float | 1.0 | Scale of the light animation time. | [L520](../../src/freelancer/Client/vis_effect_arch.cpp#L520) |
| `lgt_radius` | float | 0 | Light radius. | [L525](../../src/freelancer/Client/vis_effect_arch.cpp#L525) |
| `lgt_color` | byte x3 (`float[0..2]`) | `255, 255, 255` | Light RGB, 0..255 (low 8 bits of the truncated value). | [L531](../../src/freelancer/Client/vis_effect_arch.cpp#L531) |

Built-in effect type names (47; index = position in this list): `EFT_NONE`, `EFT_EXPLOSION_SMALL`, `EFT_EXPLOSION_MEDIUM`, `EFT_EXPLOSION_LARGE`, `EFT_EXPLOSION_DEBRIS`, `EFT_EXPLOSION_SHIP`, `EFT_EXPLOSION_MISSILE`, `EFT_EXPLOSION_MINE`, `EFT_MUZZLE_FLASH`, `EFT_MUZZLE_LARGE_FLASH`, `EFT_MISSILE_DRIVE`, `EFT_WEAPON_PROJ`, `EFT_WEAPON_LARGE_PROJ`, `EFT_WEAPON_IMPACT`, `EFT_WEAPON_LARGE_IMPACT`, `EFT_WEAPON_COUNTER_MEASURE`, `EFT_WEAPON_MINE`, `EFT_DAMAGE_HULL`, `EFT_DAMAGE_LARGE_SHIELD`, `EFT_DAMAGE_SHIELD`, `EFT_DAMAGE_CRUISE`, `EFT_DAMAGE_SMOKE`, `EFT_DAMAGE_LARGE_SMOKE`, `EFT_DEBRIS_TRAIL`, `EFT_ENGINE_CONTRAIL`, `EFT_ENGINE_TRAIL`, `EFT_ENGINE_LARGE_TRAIL`, `EFT_ENGINE_FIRE`, `EFT_ENGINE_LARGE_FIRE`, `EFT_ENGINE_THRUSTER`, `EFT_EQUIP_CLOAK`, `EFT_EQUIP_LARGE_CLOAK`, `EFT_EQUIP_ATTACHED`, `EFT_EQUIP_ATTACHED_LARGE`, `EFT_EQUIP_ATTACHED_INSANELY_LARGE`, `EFT_MISC_DUST`, `EFT_MOTION_DUST`, `EFT_JUMP_EFFECTS`, `EFT_JUMP_GATE_EFFECTS`, `EFT_LANE_ACTIVE`, `EFT_LANE_TUBE`, `EFT_LANE_SHIP`, `EFT_LANE_SHIP_TRAVEL`, `EFT_CUSTOM_EFFECTS`, `EFT_CODE_PROJ_EFFECT`, `EFT_TRACTORBEAM`, `EFT_FLASHLIGHT` ([effect_type_info.cpp:38](../../src/freelancer/Client/effect_type_info.cpp#L38)). The numeric defaults of each type live in that table, not in the data files.

Example (synthetic):

```ini
[Effect]
nickname = demo_muzzle
effect_type = EFT_MUZZLE_FLASH
vis_effect = demo_muzzle_vis
snd_effect = demo_muzzle_snd
lgt_effect = demo_flash_light
lgt_color = 255, 220, 160
lgt_radius = 12
```

### 1.2 `[VisEffect]`

[VisEffectLoadSection, L727](../../src/freelancer/Client/vis_effect_arch.cpp#L727). The section is keyed by its **first `nickname`**; keys before it are dropped. Entries are found case-insensitively and an existing entry is **modified, not replaced**: a second section with the same nickname overwrites scalar keys and appends further `textures`. Unknown keys are ignored.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | str (whole value) | - | Entry key (case-insensitive). | [L734](../../src/freelancer/Client/vis_effect_arch.cpp#L734) |
| `effect_crc` | int (`atoi`) | 0 | Stored unchanged (the name suggests the CRC of the alchemy effect it plays; the loader does not check it). | [L747](../../src/freelancer/Client/vis_effect_arch.cpp#L747) |
| `has_alpha` | bool | false | Effect uses alpha blending. | [L752](../../src/freelancer/Client/vis_effect_arch.cpp#L752) |
| `alchemy` | str (whole value), max 259 chars | empty | Path of the alchemy effect library (`.ale`) the effect comes from. | [L757](../../src/freelancer/Client/vis_effect_arch.cpp#L757) |
| `textures` | str (whole value) | - | One texture-library name per key; repeatable, the names are kept in order. Each is resolved when the effect is first loaded; one that cannot be loaded logs "[VisEffect] ...: Failed to load texture library: %s. Removing from txm list." and is dropped. | [L762](../../src/freelancer/Client/vis_effect_arch.cpp#L762), [L129](../../src/freelancer/Client/vis_effect_arch.cpp#L129) |

### 1.3 `[BeamSpear]` and `[BeamBolt]`

Both headers use [LoadBeamSection, L359](../../src/freelancer/Client/vis_effect_arch.cpp#L359); the header decides the kind (`BeamSpear` = 0, `BeamBolt` = 1). Keys before the first `nickname` are dropped; a nickname that already exists (case-insensitive) updates the existing entry. All numeric fields start at 1.0, the two texture modes at 0. When the section ends, the values are corrected: `tip_length` is raised to at least `core_width`; `tail_length` to at least `core_width` for a spear and at least `sec_core_width` for a bolt; `head_width` to at least `core_width`; `head_brightness` and `trail_brightness` are clamped to `0.01..1.0` ([L184](../../src/freelancer/Client/vis_effect_arch.cpp#L184)). Unknown keys are ignored.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | str (whole value) | - | Entry key; referenced by `vis_beam` of `[Effect]`. | [L365](../../src/freelancer/Client/vis_effect_arch.cpp#L365) |
| `tip_color` | float x3 (0..255, /255) | `1, 1, 1` | Colour. | [L235](../../src/freelancer/Client/vis_effect_arch.cpp#L235) |
| `tail_color` | float x3 (/255) | `1, 1, 1` | Colour. | [L241](../../src/freelancer/Client/vis_effect_arch.cpp#L241) |
| `core_color` | float x3 (/255) | `1, 1, 1` | Colour. | [L247](../../src/freelancer/Client/vis_effect_arch.cpp#L247) |
| `sec_core_color` | float x3 (/255) | `1, 1, 1` | Colour. | [L253](../../src/freelancer/Client/vis_effect_arch.cpp#L253) |
| `outter_color` | float x3 (/255) | `1, 1, 1` | Colour (the key is spelled with two `t`). | [L259](../../src/freelancer/Client/vis_effect_arch.cpp#L259) |
| `sec_outter_color` | float x3 (/255) | `1, 1, 1` | Colour (two `t`). | [L265](../../src/freelancer/Client/vis_effect_arch.cpp#L265) |
| `tip_length` | float | 1.0 | Length; corrected as above. | [L271](../../src/freelancer/Client/vis_effect_arch.cpp#L271) |
| `tail_length` | float | 1.0 | Length; corrected as above. | [L276](../../src/freelancer/Client/vis_effect_arch.cpp#L276) |
| `core_length` | float | 1.0 | Length. | [L281](../../src/freelancer/Client/vis_effect_arch.cpp#L281) |
| `core_width` | float | 1.0 | Width; lower bound for `tip_length`, `tail_length` (spear) and `head_width`. | [L286](../../src/freelancer/Client/vis_effect_arch.cpp#L286) |
| `sec_core_width` | float | 1.0 | Width; lower bound for `tail_length` of a bolt. | [L291](../../src/freelancer/Client/vis_effect_arch.cpp#L291) |
| `head_brightness` | float | 1.0 | Clamped to `0.01..1.0`. | [L296](../../src/freelancer/Client/vis_effect_arch.cpp#L296) |
| `trail_brightness` | float | 1.0 | Clamped to `0.01..1.0`. | [L301](../../src/freelancer/Client/vis_effect_arch.cpp#L301) |
| `flash_size` | float | 1.0 | Size of the muzzle flash. | [L306](../../src/freelancer/Client/vis_effect_arch.cpp#L306) |
| `head_width` | float | 1.0 | Width of the head; corrected as above. | [L311](../../src/freelancer/Client/vis_effect_arch.cpp#L311) |
| `head_texture` | enum | mode 0 | Exactly `ball` selects mode 1; `star` or any other text selects mode 0 (case-sensitive). | [L316](../../src/freelancer/Client/vis_effect_arch.cpp#L316) |
| `trail_texture` | enum | mode 0 | Exactly `thin` selects mode 1; `wide` or any other text selects mode 0 (case-sensitive). | [L326](../../src/freelancer/Client/vis_effect_arch.cpp#L326) |

### 1.4 `[EffectType]`

[LoadEffectTypeSection, effect_type_info.cpp:125](../../src/freelancer/Client/effect_type_info.cpp#L125). Patches one row of the built-in type table. `nickname` selects the row (exact, case-sensitive match against the 47 names in section 1.1); an unknown name logs the warning "EffectTypes: unknown type" and the following keys **continue to modify the previous row** (or are ignored when there was none). Keys before the first valid `nickname` are ignored. This is the only reader on this page that reports an unknown key: it logs the error "EffectType (%s) has unknown parameter (%s)".

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | str (whole value) | - | Type name (see 1.1). | [L136](../../src/freelancer/Client/effect_type_info.cpp#L136) |
| `priority` | float | per type | The effect is created in full detail while the generic-effects performance level (`generic_effect_perf` option) is at or below this value. | [L154](../../src/freelancer/Client/effect_type_info.cpp#L154) |
| `generic_priority` | float | per type | Upper level up to which the `vis_generic` variant is used instead (see `vis_generic`). | [L156](../../src/freelancer/Client/effect_type_info.cpp#L156) |
| `lod_type` | enum | per type | `EFT_LOD_TRAIL`, `EFT_LOD_WEAPON`, `EFT_LOD_SMALL`, `EFT_LOD_MEDIUM`, `EFT_LOD_LARGE` or `EFT_LOD_SMALL_DISTANT` (case-insensitive). `EFT_LOD_NONE` is not accepted; any other text leaves the value unchanged without a message. | [L158](../../src/freelancer/Client/effect_type_info.cpp#L158) |
| `radius` | float | per type | Effect radius, also used for the screen-size LOD computation. A negative value logs "effect type (%s) has negative radius" and is replaced by 1.0. | [L171](../../src/freelancer/Client/effect_type_info.cpp#L171) |
| `visibility` | enum | per type | `EXIST_OFFSCREEN` (case-insensitive) = the effect exists off screen; any other text = it does not. | [L184](../../src/freelancer/Client/effect_type_info.cpp#L184) |
| `update` | enum | per type | `UPDATE_OFFSCREEN` (case-insensitive) = updated off screen; any other text = not. | [L190](../../src/freelancer/Client/effect_type_info.cpp#L190) |
| `run_time` | float | per type | Run time in seconds; a negative value marks a continuous effect. | [L196](../../src/freelancer/Client/effect_type_info.cpp#L196) |
| `fps` | float | per type | Update rate. | [L198](../../src/freelancer/Client/effect_type_info.cpp#L198) |
| `pbubble` | float x2 | per type | Visibility distance bubble: the working distance is interpolated from the first value (full effects detail) to the second (lowest detail) by the effects performance setting. Far below near logs the warning "pbubble ranges backwards" but is kept. | [L200](../../src/freelancer/Client/effect_type_info.cpp#L200) |

### 1.5 `[EffectLOD]`

[LoadEffectLodSection, effect_type_info.cpp:225](../../src/freelancer/Client/effect_type_info.cpp#L225). The **first key must be `type`**; if it is missing the section is ignored without a message.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `type` | enum | - | `lod_type_trail`, `lod_type_weapon`, `lod_type_small`, `lod_type_medium`, `lod_type_large` or `lod_type_small_distant` (case-insensitive). Anything else logs "invalid LOD type (%s)" and drops the section. | [L229](../../src/freelancer/Client/effect_type_info.cpp#L229) |
| `max_lod_screen_size` | float | -1.0 | On-screen size (radius / distance, scaled by the camera) at and above which the effect is at full LOD blend 1.0. Between `min_lod_screen_size` and this value the blend rises linearly. | [L255](../../src/freelancer/Client/effect_type_info.cpp#L255) |
| `min_lod_screen_size` | float | -1.0 | On-screen size at which the LOD blend is 0. | [L257](../../src/freelancer/Client/effect_type_info.cpp#L257) |
| `min_screen_size` | float | -1.0 | On-screen size at or below which the effect is not drawn at all. | [L259](../../src/freelancer/Client/effect_type_info.cpp#L259) |

The `[EffectType]`/`[EffectLOD]` keys are also described by the chunk that owns `effect_type_info.cpp`; this table is verified against the same code.

## 2. Light animations: `fx\lightanim.ini`

[LightAnimationLoad, engine_light.cpp:1387](../../src/freelancer/Client/engine_light.cpp#L1387). Each `[LightAnim]` section is one animation; other headers are skipped. Animations are looked up by name (case-insensitive) from the launcher key `light_anim` ([client-world-fx-archetypes.md](client-world-fx-archetypes.md)) and by `lgt_effect` of `[Effect]`. A section without frames is still registered.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `name` | str (whole value), max 31 chars | empty | Animation name. | [L1403](../../src/freelancer/Client/engine_light.cpp#L1403) |
| `frame` | float, byte x3, float | - | `frame = <time>, <r>, <g>, <b>, <intensity>`. Repeatable; frames are kept in file order and the loader does not sort or check the times. `r`, `g`, `b` are 0..255 (low 8 bits of the truncated value). An `intensity` of 0 or less is replaced by 0.01. The last frame of the section is marked as the end. | [L1416](../../src/freelancer/Client/engine_light.cpp#L1416) |

Example (synthetic):

```ini
[LightAnim]
name = demo_flash_light
frame = 0.0, 255, 240, 200, 1.0
frame = 0.15, 255, 120, 40, 0.6
frame = 0.4, 0, 0, 0, 0
```

## 3. Gate tunnels (`[Data] gate_tunnels`)

[GateTunnelIniLoad, gate_tunnel.cpp:1048](../../src/freelancer/Client/gate_tunnel.cpp#L1048). Each `[gate_tunnel]` section creates a tunnel; the `[layer]` sections that follow (at most two) belong to the most recent tunnel. A tunnel is looked up by its `nickname` from `jump_tunnel` of a jump gate effect (section 4). Unknown keys are ignored. A `[layer]` before any `[gate_tunnel]` is not guarded (retail behaviour).

### 3.1 `[gate_tunnel]`

If `num_spline_control_points` is above 0 the random paths (radius, rotation, colour offsets, brightness) are generated once when the section ends.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | id (`str`) | id of `default` | Tunnel key. | [L1065](../../src/freelancer/Client/gate_tunnel.cpp#L1065) |
| `write_depth_buffer` | float, tested for 0 | on | Exactly 0 turns depth writing off; anything else keeps it on. | [L1067](../../src/freelancer/Client/gate_tunnel.cpp#L1067) |
| `num_spline_control_points` | float, truncated to int | 11 | Number of random control points of the tunnel path. 0 or less skips path generation. | [L1072](../../src/freelancer/Client/gate_tunnel.cpp#L1072) |
| `x_range` | float | 0 | Control points are drawn from `-x_range/2 .. +x_range/2` sideways. | [L1074](../../src/freelancer/Client/gate_tunnel.cpp#L1074) |
| `y_range` | float | 0 | The same vertically. | [L1076](../../src/freelancer/Client/gate_tunnel.cpp#L1076) |
| `z_range` | float | 0 | Length: control point `i` lies at `-(i * z_range / count) - 5` along the axis. | [L1078](../../src/freelancer/Client/gate_tunnel.cpp#L1078) |
| `min_radius` | float | 50 | Lower bound of the random ring radius per control point. | [L1080](../../src/freelancer/Client/gate_tunnel.cpp#L1080) |
| `max_radius` | float | 50 | Upper bound. | [L1082](../../src/freelancer/Client/gate_tunnel.cpp#L1082) |
| `far_radius_factor` | float | 1.0 | The radius is blended towards `radius * factor` with distance. | [L1084](../../src/freelancer/Client/gate_tunnel.cpp#L1084) |
| `min_speed` | float | 3e-5 | Travel speed at time 0. | [L1086](../../src/freelancer/Client/gate_tunnel.cpp#L1086) |
| `max_speed` | float | 0.06 | Speed reached after `time_to_max_speed`. | [L1088](../../src/freelancer/Client/gate_tunnel.cpp#L1088) |
| `time_to_max_speed` | float | 8 | Seconds to accelerate from `min_speed` to `max_speed`. | [L1090](../../src/freelancer/Client/gate_tunnel.cpp#L1090) |
| `num_t_steps` | float | 200 | Subdivisions along the tunnel length. | [L1092](../../src/freelancer/Client/gate_tunnel.cpp#L1092) |
| `num_s_steps` | float | 12 | Segments around the circumference. | [L1094](../../src/freelancer/Client/gate_tunnel.cpp#L1094) |
| `min_rotation` | float | -pi | Lower bound of the random twist per control point (radians). | [L1096](../../src/freelancer/Client/gate_tunnel.cpp#L1096) |
| `max_rotation` | float | +pi | Upper bound. | [L1098](../../src/freelancer/Client/gate_tunnel.cpp#L1098) |
| `min_rgb` | int x3 (0..255, /255) | `0, 0, 0` | Lower bound of the random colour offset. Read with `get_value_int`. | [L1100](../../src/freelancer/Client/gate_tunnel.cpp#L1100) |
| `max_rgb` | int x3 (0..255, /255) | `0, 0, 0` | Upper bound. | [L1113](../../src/freelancer/Client/gate_tunnel.cpp#L1113) |

### 3.2 `[layer]`

A third layer for the same tunnel logs "more than two layers specified in gate tunnel %s" and is skipped.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `color` | int x3 (0..255, /255) | `0.5, 0.5, 0.5` | Layer colour. | [L1172](../../src/freelancer/Client/gate_tunnel.cpp#L1172) |
| `near_alpha` | float | 1.0 | Alpha at the near end. | [L1184](../../src/freelancer/Client/gate_tunnel.cpp#L1184) |
| `far_alpha` | float | 0 | Alpha at the far end. | [L1186](../../src/freelancer/Client/gate_tunnel.cpp#L1186) |
| `radius_factor` | float | 1.0 | Multiplier of the tunnel radius for this layer. | [L1188](../../src/freelancer/Client/gate_tunnel.cpp#L1188) |
| `u_offset` | float | 0 | Initial texture U offset. | [L1190](../../src/freelancer/Client/gate_tunnel.cpp#L1190) |
| `v_offset` | float | 0 | Initial texture V offset. | [L1192](../../src/freelancer/Client/gate_tunnel.cpp#L1192) |
| `du` | float | 0.09 | Texture scroll per second in U. | [L1194](../../src/freelancer/Client/gate_tunnel.cpp#L1194) |
| `dv` | float | 0.07 | Texture scroll per second in V. | [L1196](../../src/freelancer/Client/gate_tunnel.cpp#L1196) |
| `v_scale` | float | 10 | Texture repeats along the tunnel length. | [L1198](../../src/freelancer/Client/gate_tunnel.cpp#L1198) |
| `texture` | str (whole value) | none | Shape name in the effect shape library (`[Data] effect_shapes`). A miss logs the library's warning and "Couldn't find Effect shape %s for the Gate Tunnel"; the layer then has no texture. | [L1200](../../src/freelancer/Client/gate_tunnel.cpp#L1200) |

Example (synthetic):

```ini
[gate_tunnel]
nickname = demo_tunnel
num_spline_control_points = 12
z_range = 800
min_radius = 40
max_radius = 60
min_rgb = 20, 20, 60
max_rgb = 80, 60, 160

[layer]
color = 200, 200, 255
texture = demo_tunnel_shape
```

## 4. Jump effects (`[Data] jump_effect`)

[JumpEffectIniLoad, jump_effect.cpp:220](../../src/freelancer/Client/jump_effect.cpp#L220). Two section kinds. The descriptor is chosen at run time: a jump gate solar uses the `jump_effect` key of its `[Object]` ([client-world-fx.md](client-world-fx.md#18-object-client-half)) as the `nickname` of a `[JumpGateEffect]`; when the key is absent it uses the nickname `jump_effect_default`; a missing descriptor logs "Missing jump effect: %s" ([jump_effect.cpp:94](../../src/freelancer/Client/jump_effect.cpp#L110)).

### 4.1 `[JumpGateEffect]`

Up to five "glow rings" are created on the gate at timed moments. **State carries over between sections of the same file:** the record is not cleared between `[JumpGateEffect]` sections except for the nickname, the ring effect/hardpoint ids, `jump_tunnel_effect` and `jump_tunnel`; the three per-ring counters are never reset. A later section that omits a key therefore inherits the earlier section's value for it.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | id (`str`) | empty | Descriptor key. | [L265](../../src/freelancer/Client/jump_effect.cpp#L265) |
| `glow_ring_effect` | up to five ids (`str[0..4]`) | empty | `[Effect]` nicknames of the rings. Every non-empty parameter `i` raises the effect count to `i + 1`. | [L269](../../src/freelancer/Client/jump_effect.cpp#L269) |
| `glow_ring_hp` | up to five names (`str[0..4]`) | empty | Hardpoint of the gate that each ring is attached to. | [L283](../../src/freelancer/Client/jump_effect.cpp#L283) |
| `glow_create_time` | up to five floats | 0 | Seconds after the start at which each ring is created. | [L296](../../src/freelancer/Client/jump_effect.cpp#L296) |
| `jump_out_time` | float | 0 | Duration (seconds) of the jump-out phase. | [L308](../../src/freelancer/Client/jump_effect.cpp#L308) |
| `jump_out_tunnel_time` | float | 0 | Duration (seconds) of the jump-out tunnel phase (the screen fades during the remaining time). | [L312](../../src/freelancer/Client/jump_effect.cpp#L312) |
| `jump_in_tunnel_time` | float | 0 | Duration (seconds) of the jump-in tunnel phase. | [L316](../../src/freelancer/Client/jump_effect.cpp#L316) |
| `jump_in_time` | float | 0 | Duration (seconds) of the arrival phase. | [L320](../../src/freelancer/Client/jump_effect.cpp#L320) |
| `kill_time_before_done` | float | 0 | Seconds before the end of the arrival phase (`jump_in_time`) at which the arrival effects are stopped. | [L324](../../src/freelancer/Client/jump_effect.cpp#L324) |
| `jump_tunnel_effect` | id (`str`) | empty | `[Effect]` nickname of the tunnel particle effect. | [L328](../../src/freelancer/Client/jump_effect.cpp#L328) |
| `jump_tunnel` | id (`str`) | empty | Nickname of a `[gate_tunnel]` (section 3) shown to the player during the jump. A miss logs "gate tunnel '%s' not found." | [L332](../../src/freelancer/Client/jump_effect.cpp#L332) |
| `jump_ambient` | byte x3 | `0, 0, 0` | Ambient RGB during the jump (low 8 bits of the truncated value). | [L336](../../src/freelancer/Client/jump_effect.cpp#L336) |
| `jump_background_color` | byte x3 | `0, 0, 0` | Background RGB during the jump. | [L345](../../src/freelancer/Client/jump_effect.cpp#L345) |

The ring count of a descriptor is the smallest of the three counters (effects, hardpoints, create times). Any other key logs the warning "UNKNOWN jumpeffect parameter". The per-key defaults above are zero in the port; in the first section retail leaves them uninitialised.

### 4.2 `[JumpShipEffect]`

Each section replaces the previous one (the descriptor is rebuilt), so the **last** `[JumpShipEffect]` section wins. If the file has none, a default with empty ids is created at first use and the warning "no JumpShipEffectDesc was loaded, creating a default" is logged. Any other key logs "UNKNOWN jump ship effect parameter".

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `jump_out_effect` | id (`str`) | empty | `[Effect]` nickname played on a ship that jumps out. | [L388](../../src/freelancer/Client/jump_effect.cpp#L388) |
| `jump_in_effect` | id (`str`) | empty | Played on a ship that arrives. | [L392](../../src/freelancer/Client/jump_effect.cpp#L392) |
| `jump_done_effect_player` | id (`str`) | empty | Played when the player's jump finishes. | [L396](../../src/freelancer/Client/jump_effect.cpp#L396) |
| `jump_done_effect_nonplayer` | id (`str`) | empty | Played when a non-player ship's jump finishes. | [L400](../../src/freelancer/Client/jump_effect.cpp#L400) |

Example (synthetic):

```ini
[JumpGateEffect]
nickname = jump_effect_default
glow_ring_effect = demo_ring_a, demo_ring_b
glow_ring_hp = HpRing01, HpRing02
glow_create_time = 0.0, 1.5
jump_tunnel = demo_tunnel
jump_ambient = 40, 60, 120

[JumpShipEffect]
jump_out_effect = demo_jump_out
jump_in_effect = demo_jump_in
```

## 5. Client fuses (`[Data] fuses`)

A fuse is a timed script that attaches effects, explosions and impulses to an object when it burns (for example when a ship is dying). The same files are read by the server for its own actions ([server.md](server.md)); the client recognises only the action sections below and silently skips every other header. The database is keyed by the fuse `name` id; a fuse whose name already exists is ignored (the first one wins).

File layout: a `[fuse]` section starts a fuse; every following section up to the next `[fuse]` is an action of that fuse ([ClientFuse::Read, client_fuse.cpp:90](../../src/freelancer/Client/client_fuse.cpp#L90), [ClientFuseDB::LoadFile, L949](../../src/freelancer/Client/client_fuse.cpp#L949)). Actions are kept sorted by their trigger time `at_t`.

### 5.1 `[fuse]` and keys common to all actions

These are implemented in Common ([common_fuses.cpp:538](../../src/Common/common_fuses.cpp#L538), [:448](../../src/Common/common_fuses.cpp#L448)) and shared with the server; see [world-data.md](world-data.md).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `name` (`[fuse]`) | id (`str`) | empty | Fuse nickname (key of the database). | [common_fuses.cpp:550](../../src/Common/common_fuses.cpp#L550) |
| `lifetime` (`[fuse]`) | float | -1 | Duration in seconds; the burn progress advances by `dt / lifetime`. Not validated. | [common_fuses.cpp:552](../../src/Common/common_fuses.cpp#L552) |
| `death_fuse` (`[fuse]`) | bool | false | The fuse is a death fuse. | [common_fuses.cpp:554](../../src/Common/common_fuses.cpp#L554) |
| `at_t` (every action) | float, optional second float | 0 | Trigger time. With a second value the time is a random choice in the range `[first, second]`. Handled before the action's own keys. | [common_fuses.cpp:453](../../src/Common/common_fuses.cpp#L453) |

### 5.2 `[start_explosion]`

[StartExplosionAction::Slot08, L188](../../src/freelancer/Client/client_fuse.cpp#L188). The action starts only an explosion archetype whose `process` is `none`.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `explosion` | id (`str`) | empty | Nickname of an `[Explosion]` archetype ([archetypes-and-equipment.md](archetypes-and-equipment.md)); looked up when the fuse is loaded and again when it triggers. | [L195](../../src/freelancer/Client/client_fuse.cpp#L195) |
| `hardpoint` | cstr | empty | Hardpoint of the host where the explosion is placed. If it is not found nothing starts. | [L199](../../src/freelancer/Client/client_fuse.cpp#L199) |
| `pos_offset` | float x3 | `0, 0, 0` | Offset, rotated by the hardpoint orientation. | [L203](../../src/freelancer/Client/client_fuse.cpp#L203) |

### 5.3 `[start_cam_particles]`

[L312](../../src/freelancer/Client/client_fuse.cpp#L312): starts a particle effect fixed to the camera.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `effect` | id (`str`) | empty | `[Effect]` nickname (section 1.1). An empty value starts nothing. | [L319](../../src/freelancer/Client/client_fuse.cpp#L319) |

### 5.4 `[start_effect]`

[L418](../../src/freelancer/Client/client_fuse.cpp#L418). `pos_offset` and `ori_offset` apply to the **most recent** `hardpoint` key; before any `hardpoint` they are ignored and log "StartEffectAction::Read() pos_offset precedes any hardpoint.  Ignored." (and the same text for `ori_offset`). At trigger time one hardpoint of the list is tried starting from a random index and the first one that exists is used; the name `ROOT` (case-insensitive) means the host's own origin. Without any `hardpoint` key the effect starts at the host's origin.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `effect` | id (`str`) | empty | `[Effect]` nickname. | [L425](../../src/freelancer/Client/client_fuse.cpp#L425) |
| `attached` | bool | false | `true`: the effect follows the host; `false`: it is placed in the world once and moves with the host's velocity. | [L429](../../src/freelancer/Client/client_fuse.cpp#L429) |
| `hardpoint` | cstr | - | Adds a hardpoint candidate (repeatable). | [L433](../../src/freelancer/Client/client_fuse.cpp#L433) |
| `pos_offset` | float x3 | `0, 0, 0` | Offset of the last hardpoint, rotated by the hardpoint orientation. | [L446](../../src/freelancer/Client/client_fuse.cpp#L446) |
| `ori_offset` | float x3 (degrees) | identity | Orientation of the last hardpoint entry. Value 2 is applied first as a rotation about the Y axis, then value 0 rotates matrix rows 1 and 2, then value 1 rows 0 and 1. Not validated. | [L463](../../src/freelancer/Client/client_fuse.cpp#L463) |

### 5.5 `[impulse]`

[L701](../../src/freelancer/Client/client_fuse.cpp#L701): pushes nearby objects away from a point of the host. Nothing happens unless both `radius` and `force` are above 0.0001.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `hardpoint` | cstr | empty | Hardpoint of the host that is the centre of the push. | [L708](../../src/freelancer/Client/client_fuse.cpp#L708) |
| `force` | float | 0 | Strength at the centre; falls off linearly to 0 at `radius`. | [L712](../../src/freelancer/Client/client_fuse.cpp#L712) |
| `radius` | float | 0 | Reach of the push. | [L716](../../src/freelancer/Client/client_fuse.cpp#L716) |
| `pos_offset` | float x3 | `0, 0, 0` | Offset from the hardpoint (applied with the transposed hardpoint orientation). | [L720](../../src/freelancer/Client/client_fuse.cpp#L720) |

### 5.6 `[ignite_fuse]`

[L860](../../src/freelancer/Client/client_fuse.cpp#L860): starts another fuse.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `fuse` | id (`str`) | empty | Name of another fuse of the client database; it is loaded together with this one. | [L867](../../src/freelancer/Client/client_fuse.cpp#L867) |

Example (synthetic):

```ini
[fuse]
name = demo_death_fuse
lifetime = 4
death_fuse = true

[start_explosion]
at_t = 0.0
explosion = demo_ship_blast
hardpoint = HpEngine01

[start_effect]
at_t = 0.5, 1.5
effect = demo_smoke
attached = true
hardpoint = HpEngine01
pos_offset = 0, 0, -1
```
