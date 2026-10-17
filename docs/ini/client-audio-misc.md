# Client configuration: sound, voices, effect types, shape databases and other tables

This page documents the INI files the client reads for sound and voice archetypes, global audio settings, visual-effect type tables, texture shape databases, debris, dynamic asteroids, graphs and mission-created solar objects. Keyboard, options, display, fonts, cursors, cameras and start-up are in [client-ui-config.md](client-ui-config.md); window definitions and the HUD in [client-ui-config-windows.md](client-ui-config-windows.md); the rest of the effect and world files in [client-world-fx.md](client-world-fx.md).

## File location / how it is found

All files are read through `INI_Reader` ([parsers.md](parsers.md)); section and key names are case-insensitive; repeated keys are delivered in file order (what happens with them is stated per key). The files are named by `[Data]` keys in `Freelancer.ini` (see [client-ui-config.md](client-ui-config.md#43-data-file-loads) and [freelancer-ini.md](freelancer-ini.md)) and resolved against the Data path (`[Freelancer] data path`, default `..\Data`), except where a row says otherwise. Shipped files are binary `BINI`; text files with the same sections and keys behave identically.

Conventions: *Type* names the getter — *string* (`get_value_string`), *int* (`get_value_int`), *number* (`get_value_float`, accepts decimals and `0x..`), *bool* (`get_value_bool`: `true`/`1`/`false`/`0`, anything else logs an error and reads as false); *nickname* means the string is hashed with `CreateID` (case-insensitive hash of the text). "Default" is the value the destination holds before the file is read; the reader has no default mechanism of its own. "Not validated" means the code does not range-check the value.

| File | Located by | Read by | Section |
|---|---|---|---|
| sound archetypes | `[Data] sounds` | `LoadClientSoundArchIni` | 1 |
| voice profiles | `[Data] voices` | `LoadClientVoicesIni` | 2 |
| `audio\SoundCFG.ini` | fixed name | `LoadClientSoundCfgIni` | 3 |
| effect type and LOD tables | inside the `[Data] effects` file | `LoadEffectTypeSection`, `LoadEffectLodSection` | 4 |
| shape databases | various | `FShapeDB::Load` | 5 |
| debris | `[Data] debris` | `LoadDebrisArchFile` | 6 |
| dynamic asteroids | `[DynamicAsteroids]` sections of asteroid-field files | `CliDynamicAsteroidParseIni` | 7 |
| graphs | `[Data] igraph` | `GraphLibraryLoad` | 8 |
| `Universe\MissionCreatedSolars.ini` | fixed name | `LoadMissionCreatedSolars` | 9 |
| planet-ring file | solar `ring` key | `FTexturePanels::Load` | 10 |

---

## 1. Sound archetypes (`[Sound]`)

`[Data] sounds = <file>`; loaded by `LoadClientSoundArchIni` ([menu_sound_arch.cpp:2774](../../src/freelancer/Client/menu_sound_arch.cpp#L2774)), keys parsed by `ParseSoundValue` ([menu_sound_arch.cpp:2305](../../src/freelancer/Client/menu_sound_arch.cpp#L2305)). Only `[Sound]` sections are read; each defines one sound archetype. The `[Data] sounds` key may be repeated to load several files.

A section without `nickname` (or whose nickname is empty) is skipped with `Sound has no nickname`. If the same nickname is defined again, the earlier archetype is **redefined** from the new section (warning `Redefining sound`); two different spellings with the same hash are a collision error and the second is dropped.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | nickname | none | Name of the sound archetype; the hash is the lookup key (also kept as text). | [menu_sound_arch.cpp:2796](../../src/freelancer/Client/menu_sound_arch.cpp#L2796) |
| `file` | string | none | Audio file passed to the sound manager. A later `file` in the same section replaces the earlier one. Not validated here (a missing file is only reported when the sound is loaded). | [menu_sound_arch.cpp:2307](../../src/freelancer/Client/menu_sound_arch.cpp#L2307) |
| `attenuation` | number → int | `0` | Attenuation level, stored as a signed byte. The loader checks it afterwards: values above `0` are clamped to `0`, values below `-96` to `-96`, with a warning `attenuation out of valid range (0, -96)`. `0` is full volume. | [menu_sound_arch.cpp:2315](../../src/freelancer/Client/menu_sound_arch.cpp#L2315), [menu_sound_arch.cpp:2829](../../src/freelancer/Client/menu_sound_arch.cpp#L2829) |
| `range` | number [, number] | `-1, -1` (unset) | Audible distance: `range = <min>[, <max>]`. If `max` is omitted it is `min * 30`. A negative `min` resets both to `-1` (unset). A warning `invalid range` is logged when `min >= max`. | [menu_sound_arch.cpp:2322](../../src/freelancer/Client/menu_sound_arch.cpp#L2322), [menu_sound_arch.cpp:2853](../../src/freelancer/Client/menu_sound_arch.cpp#L2853) |
| `persistent` | string | not persistent | `space` or `gameflow` (case-insensitive): keep the loaded sound across the space / game-flow phase. Any other value logs `unknown 'persistent' value` and is ignored. | [menu_sound_arch.cpp:2332](../../src/freelancer/Client/menu_sound_arch.cpp#L2332) |
| `type` | string | `normal` | Volume category: `normal`, `interface`, `voice`, `music`, `ambience` (case-insensitive; the spelling is `ambience`). The category selects which user volume (`audio_sfx`, `audio_interface`, `audio_voice`, `audio_music`, `audio_ambient` in `PerfOptions.ini`) applies. Other values log `unknown 'type' value`. | [menu_sound_arch.cpp:2346](../../src/freelancer/Client/menu_sound_arch.cpp#L2346), [menu_sound_arch.cpp:2291](../../src/freelancer/Client/menu_sound_arch.cpp#L2291) |
| `streamer` | bool | `false` | Stream the file instead of loading it. | [menu_sound_arch.cpp:2357](../../src/freelancer/Client/menu_sound_arch.cpp#L2357) |
| `is_2d` | bool | `false` | Play as a non-positional (2D) sound. | [menu_sound_arch.cpp:2363](../../src/freelancer/Client/menu_sound_arch.cpp#L2363) |
| `pitch_bendable` | bool | `false` | Allow Doppler/engine pitch bending of the sound. | [menu_sound_arch.cpp:2369](../../src/freelancer/Client/menu_sound_arch.cpp#L2369) |
| `crv_pitch` | number → int | off | Enables random pitch variation: each play picks an integer offset in `-N ... +N` (`N` = the value, stored as a signed byte, so only `1 ... 127` have an effect; `0` or negative = no variation). | [menu_sound_arch.cpp:2375](../../src/freelancer/Client/menu_sound_arch.cpp#L2375), [menu_sound_arch.cpp:497](../../src/freelancer/Client/menu_sound_arch.cpp#L497) |
| `lod1` | nickname | none | Nickname of another sound archetype used as the lower-detail variant; sets **both** LOD slots. Not validated (an unknown name is resolved at play time). | [menu_sound_arch.cpp:2388](../../src/freelancer/Client/menu_sound_arch.cpp#L2388) |
| `lod2` | nickname | none | Second LOD slot only. | [menu_sound_arch.cpp:2390](../../src/freelancer/Client/menu_sound_arch.cpp#L2390) |

Any other key logs `unknown [Sound] parameter` and is ignored.

---

## 2. Voice profiles (`[Voice]`, `[Sound]`)

`[Data] voices = <file>`; `LoadClientVoicesIni` ([voice_profiles.cpp:175](../../src/freelancer/Client/voice_profiles.cpp#L175)). A `[Voice]` section opens a profile; the `[Sound]` (alias `[VoiceSound]`) sections that follow it describe that profile's messages.

### 2.1 `[Voice]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | nickname | none | Profile name (hash is the key; the text is kept). | [voice_profiles.cpp:198](../../src/freelancer/Client/voice_profiles.cpp#L198) |
| `extend` | string (≤127 chars) | none | Name of an existing profile. When present, **this `[Voice]` section does not create a profile**: the following `[Sound]` sections are added to the named profile instead (an unknown name means the following sections are rejected with `[Sound] listed outside [Voice] block`). | [voice_profiles.cpp:204](../../src/freelancer/Client/voice_profiles.cpp#L204) |
| `script` | string, **repeated** | none | A script name appended to the profile's script list. | [voice_profiles.cpp:217](../../src/freelancer/Client/voice_profiles.cpp#L217) |

Other keys log `Voice (...) has invalid parameter`. A profile whose nickname is already registered is dropped (the first definition wins). For a new profile the loader looks for `<Data>\audio\<nickname>.utf`; when that file exists, **it supplies all audio files** by message id and the per-message `file` keys below are ignored.

### 2.2 `[Sound]` / `[VoiceSound]` (inside a profile)

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `msg` | nickname | none (required) | Message id of the line (the nickname that scripts and missions use to request this voice line). A section without `msg` is dropped with `missing [Sound] msg parameter`. A message id already defined in the profile keeps its **first** definition. | [voice_profiles.cpp:294](../../src/freelancer/Client/voice_profiles.cpp#L294) |
| `file` | string | none | Audio file for the message. **Order matters:** the file is attached to the message id known at the time the `file` line is read, so `msg` must come **before** `file`; a `file` before `msg` is stored under message id 0. | [voice_profiles.cpp:276](../../src/freelancer/Client/voice_profiles.cpp#L276) |
| `priority` | int | `0` | Playback priority, stored as a signed byte. Not validated. | [voice_profiles.cpp:280](../../src/freelancer/Client/voice_profiles.cpp#L280) |
| `attenuation` | int | `0` | Volume attenuation, stored as a signed byte. Not validated. | [voice_profiles.cpp:285](../../src/freelancer/Client/voice_profiles.cpp#L285) |
| `duration` | – | – | Accepted and ignored. | [voice_profiles.cpp:290](../../src/freelancer/Client/voice_profiles.cpp#L290) |

Other keys log `invalid [Sound] parameter`.

---

## 3. `SoundCFG.ini`

Fixed name `audio\SoundCFG.ini`, loaded at the end of `LoadClientIniData` ([dataconfig_progress.cpp:227](../../src/freelancer/Client/dataconfig_progress.cpp#L227)) by `LoadClientSoundCfgIni` ([sound_config.cpp:47](../../src/freelancer/Client/sound_config.cpp#L47)). Only the `[CFG]` section is read; other sections of the file (for example reverb definitions) are ignored here. The "ducking" settings lower the music/ambience while a comm, a real-time cutscene (RTC) or spaceflight dialogue is playing. Levels are in decibels (`0` = unchanged, negative = quieter), times in seconds. Every value is a number; none is validated.

| Key | Default | Meaning | Source |
|---|---|---|---|
| `COCKPIT_ATTENUATION` | `0` | Extra attenuation applied to sounds in the cockpit mix. | [sound_config.cpp:67](../../src/freelancer/Client/sound_config.cpp#L67) |
| `ducking_rtc_down_by` | `-10` | Ducking level during real-time cutscenes. | [sound_config.cpp:69](../../src/freelancer/Client/sound_config.cpp#L69) |
| `ducking_rtc_down_time` | `0.25` | Time to reach the ducked level in a cutscene. | [sound_config.cpp:71](../../src/freelancer/Client/sound_config.cpp#L71) |
| `ducking_rtc_up_time` | `1` | Time to return to normal after a cutscene. | [sound_config.cpp:73](../../src/freelancer/Client/sound_config.cpp#L73) |
| `ducking_spaceflight_down_by` | `-6` | Ducking level for spaceflight dialogue. | [sound_config.cpp:75](../../src/freelancer/Client/sound_config.cpp#L75) |
| `ducking_spaceflight_down_time` | `0.2` | Time to duck. | [sound_config.cpp:77](../../src/freelancer/Client/sound_config.cpp#L77) |
| `ducking_spaceflight_up_time` | `0.75` | Time to recover. | [sound_config.cpp:79](../../src/freelancer/Client/sound_config.cpp#L79) |
| `ducking_comm_down_by` | `-6` | Ducking level while a comm message plays. | [sound_config.cpp:81](../../src/freelancer/Client/sound_config.cpp#L81) |
| `ducking_comm_down_time` | `0.25` | Time to duck. | [sound_config.cpp:83](../../src/freelancer/Client/sound_config.cpp#L83) |
| `ducking_comm_up_time` | `0.25` | Time to recover. | [sound_config.cpp:85](../../src/freelancer/Client/sound_config.cpp#L85) |
| `music_fade_time` | `3` | Default music fade time in seconds. | [sound_config.cpp:87](../../src/freelancer/Client/sound_config.cpp#L87) |
| `cross_fade_silence` | `-96` | Level treated as silence when music is cross-faded. | [sound_config.cpp:89](../../src/freelancer/Client/sound_config.cpp#L89) |
| `spaceflight_dialogue_pan_range` | `90` | Angular range used to pan spaceflight comm dialogue towards the speaker's direction. | [sound_config.cpp:91](../../src/freelancer/Client/sound_config.cpp#L91) |
| `spaceflight_dialogue_pan_factor` | `20` | Strength of that panning. | [sound_config.cpp:93](../../src/freelancer/Client/sound_config.cpp#L93) |
| `ear_doppler_factor` | none | Doppler factor passed to the sound manager (`SetDopplerFactor`) when the key is present and the sound manager exists. | [sound_config.cpp:95](../../src/freelancer/Client/sound_config.cpp#L95) |
| `ear_rolloff_factor` | none | Distance rolloff factor (`SetRolloffFactor`), same conditions. | [sound_config.cpp:102](../../src/freelancer/Client/sound_config.cpp#L102) |

---

## 4. Effect types and LOD (`[EffectType]`, `[EffectLOD]`)

These two sections live inside the visual-effects file named by `[Data] effects`; the other sections of that file (`[Effect]`, `[VisEffect]`, `[BeamSpear]`, `[BeamBolt]`) are described in [client-world-fx.md](client-world-fx.md). The dispatcher is `EffectArchLoadIni` ([vis_effect_arch.cpp:775](../../src/freelancer/Client/vis_effect_arch.cpp#L775)); the parsers are in [effect_type_info.cpp](../../src/freelancer/Client/effect_type_info.cpp).

### 4.1 `[EffectType]`

Overrides the built-in parameters of one effect type ([effect_type_info.cpp:125](../../src/freelancer/Client/effect_type_info.cpp#L125)). The built-in values (table 4.3) apply to every type not mentioned.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | string | none | One of the 47 `EFT_*` names of table 4.3 (case-sensitive). All following keys of the section change that type. An unknown name logs `EffectTypes: unknown type` and the section's keys then modify the **previously selected** type (or nothing if there was none). Keys before the first valid `nickname` are ignored. | [effect_type_info.cpp:136](../../src/freelancer/Client/effect_type_info.cpp#L136) |
| `priority` | number | per type | Compared by `EffectTypeWithinPerf` with the effect-detail level (`1 - generic_effect_perf`, see [client-ui-config.md](client-ui-config.md#32-performance-option-keys)): the effect is allowed when the level is not above its priority (currently used for splash-hit effects). | [effect_type_info.cpp:154](../../src/freelancer/Client/effect_type_info.cpp#L154), [effect_type_info.cpp:115](../../src/freelancer/Client/effect_type_info.cpp#L115) |
| `generic_priority` | number | per type | Priority used for generic (non-specific) effects of the type. | [effect_type_info.cpp:156](../../src/freelancer/Client/effect_type_info.cpp#L156) |
| `lod_type` | string | per type | `EFT_LOD_TRAIL`, `EFT_LOD_WEAPON`, `EFT_LOD_SMALL`, `EFT_LOD_MEDIUM`, `EFT_LOD_LARGE`, `EFT_LOD_SMALL_DISTANT` (case-insensitive). Any other value (including `EFT_LOD_NONE`) leaves the type unchanged. Selects the row of `[EffectLOD]` used for distance fading. | [effect_type_info.cpp:158](../../src/freelancer/Client/effect_type_info.cpp#L158) |
| `radius` | number | per type | Bounding radius for visibility/LOD. A negative value logs an error and is replaced by `1.0`. | [effect_type_info.cpp:171](../../src/freelancer/Client/effect_type_info.cpp#L171) |
| `visibility` | string | per type | `EXIST_OFFSCREEN` keeps the effect alive off screen; any other word sets the flag off. | [effect_type_info.cpp:184](../../src/freelancer/Client/effect_type_info.cpp#L184) |
| `update` | string | per type | `UPDATE_OFFSCREEN` keeps updating off screen; any other word sets the flag off. | [effect_type_info.cpp:190](../../src/freelancer/Client/effect_type_info.cpp#L190) |
| `run_time` | number | per type | Run time in seconds; table entries use `-1` for continuous effects (a negative value sets a flag on the effect archetype, see [effect_type_info.h](../../src/freelancer/Client/effect_type_info.h)). | [effect_type_info.cpp:196](../../src/freelancer/Client/effect_type_info.cpp#L196) |
| `fps` | number | per type (`5`) | Update rate of the effect. | [effect_type_info.cpp:198](../../src/freelancer/Client/effect_type_info.cpp#L198) |
| `pbubble` | number, number | per type | Near and far distance of the effect's "pbubble" range. A warning `pbubble ranges backwards` is logged when far < near (the values are still stored). | [effect_type_info.cpp:200](../../src/freelancer/Client/effect_type_info.cpp#L200) |

Any other key logs `EffectType (...) has unknown parameter`.

### 4.2 `[EffectLOD]`

[effect_type_info.cpp:225](../../src/freelancer/Client/effect_type_info.cpp#L225). The **first key must be `type`**; if it is not, the section is ignored.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `type` | string | none | Row to define: `lod_type_trail`, `lod_type_weapon`, `lod_type_small`, `lod_type_medium`, `lod_type_large`, `lod_type_small_distant` (case-insensitive). `none` is not accepted. An unknown name logs `invalid LOD type` and ends the section. | [effect_type_info.cpp:229](../../src/freelancer/Client/effect_type_info.cpp#L229) |
| `max_lod_screen_size` | number | `-1` | Screen size (radius / distance, scaled by the camera) at and above which the effect is at full intensity. | [effect_type_info.cpp:255](../../src/freelancer/Client/effect_type_info.cpp#L255) |
| `min_lod_screen_size` | number | `-1` | Screen size at which the fade-in starts (blend = `(size - min_lod) / (max_lod - min_lod)`, clamped to `0 ... 1`). | [effect_type_info.cpp:257](../../src/freelancer/Client/effect_type_info.cpp#L257) |
| `min_screen_size` | number | `-1` | Effects smaller than this on screen are not drawn at all. | [effect_type_info.cpp:259](../../src/freelancer/Client/effect_type_info.cpp#L259) |

### 4.3 Built-in effect types

The initial table (`G_EffectTypeTable`, [effect_type_info.cpp:38](../../src/freelancer/Client/effect_type_info.cpp#L38)); values in `[EffectType]` sections overwrite these. Columns: priority, generic priority, LOD type, radius, visibility flag, update flag, run time, fps, pbubble near and far.

| Type | priority | generic | LOD type | radius | visibility | update | run_time | fps | pbubble |
|---|---|---|---|---|---|---|---|---|---|
| `EFT_NONE` | 0 | 0 | EFT_LOD_NONE | 10 | - | - | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_SMALL` | 1 | 0.5 | EFT_LOD_SMALL | 10 | - | - | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_MEDIUM` | 1 | 0.5 | EFT_LOD_MEDIUM | 50 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_LARGE` | 1 | 0.5 | EFT_LOD_LARGE | 100 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_DEBRIS` | 0 | 0 | EFT_LOD_SMALL | 10 | - | - | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_SHIP` | 1 | 0.5 | EFT_LOD_MEDIUM | 50 | - | - | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_MISSILE` | 0 | 0 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_MINE` | 0 | 0 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_MUZZLE_FLASH` | 0 | 0 | EFT_LOD_SMALL_DISTANT | 5 | - | - | 15 | 5 | 100, 500 |
| `EFT_MUZZLE_LARGE_FLASH` | 1 | 0.5 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_MISSILE_DRIVE` | 0.5 | 0.5 | EFT_LOD_WEAPON | 100 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_WEAPON_PROJ` | 1 | 0.5 | EFT_LOD_WEAPON | 15 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_WEAPON_LARGE_PROJ` | 1 | 0.5 | EFT_LOD_LARGE | 50 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_WEAPON_IMPACT` | 0.5 | 0.5 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_WEAPON_LARGE_IMPACT` | 1 | 0.5 | EFT_LOD_MEDIUM | 20 | - | - | 15 | 5 | 100, 500 |
| `EFT_WEAPON_COUNTER_MEASURE` | 1 | 0.5 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_WEAPON_MINE` | 1 | 0.5 | EFT_LOD_MEDIUM | 20 | - | - | 15 | 5 | 100, 500 |
| `EFT_DAMAGE_HULL` | 0.5 | 0.5 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_DAMAGE_LARGE_SHIELD` | 1 | 0.5 | EFT_LOD_LARGE | 100 | - | - | 15 | 5 | 100, 500 |
| `EFT_DAMAGE_SHIELD` | 0.5 | 0.5 | EFT_LOD_SMALL | 5 | - | - | 15 | 5 | 100, 500 |
| `EFT_DAMAGE_CRUISE` | 0.5 | 0.5 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_DAMAGE_SMOKE` | 0.5 | 0.5 | EFT_LOD_MEDIUM | 20 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_DAMAGE_LARGE_SMOKE` | 0.5 | 0.5 | EFT_LOD_LARGE | 20 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_DEBRIS_TRAIL` | 0.5 | 0.5 | EFT_LOD_SMALL | 5 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_ENGINE_CONTRAIL` | 0 | 0 | EFT_LOD_SMALL | 5 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 100, 500 |
| `EFT_ENGINE_TRAIL` | 1 | 0.5 | EFT_LOD_TRAIL | 100 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 100, 500 |
| `EFT_ENGINE_LARGE_TRAIL` | 1 | 0.5 | EFT_LOD_TRAIL | 100 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 500, 2000 |
| `EFT_ENGINE_FIRE` | 0.5 | 0.5 | EFT_LOD_SMALL_DISTANT | 7.5 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_ENGINE_LARGE_FIRE` | 0.5 | 0.5 | EFT_LOD_SMALL_DISTANT | 25 | EXIST_OFFSCREEN | - | -1 | 5 | 250, 1000 |
| `EFT_ENGINE_THRUSTER` | 0 | 0 | EFT_LOD_SMALL_DISTANT | 7.5 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_EQUIP_CLOAK` | 1 | 0.5 | EFT_LOD_SMALL | 5 | - | - | -1 | 5 | 100, 500 |
| `EFT_EQUIP_LARGE_CLOAK` | 1 | 0.5 | EFT_LOD_MEDIUM | 100 | - | - | -1 | 5 | 500, 1500 |
| `EFT_EQUIP_ATTACHED` | 1 | 0.5 | EFT_LOD_LARGE | 100 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_EQUIP_ATTACHED_LARGE` | 1 | 0.5 | EFT_LOD_LARGE | 500 | EXIST_OFFSCREEN | - | -1 | 5 | 1000, 5000 |
| `EFT_EQUIP_ATTACHED_INSANELY_LARGE` | 1 | 0.5 | EFT_LOD_LARGE | 500 | EXIST_OFFSCREEN | - | -1 | 5 | 1000, 5000 |
| `EFT_MISC_DUST` | 0.5 | 0.5 | EFT_LOD_NONE | 500 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 100, 500 |
| `EFT_MOTION_DUST` | 1 | 1 | EFT_LOD_NONE | 500 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 100, 500 |
| `EFT_JUMP_EFFECTS` | 1 | 0.5 | EFT_LOD_NONE | 0 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 100, 500 |
| `EFT_JUMP_GATE_EFFECTS` | 1 | 0.5 | EFT_LOD_NONE | 0 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 1000, 2500 |
| `EFT_LANE_ACTIVE` | 0.5 | 0.5 | EFT_LOD_NONE | 50 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_LANE_TUBE` | 1 | 0.5 | EFT_LOD_NONE | 50 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_LANE_SHIP` | 0.5 | 0.5 | EFT_LOD_MEDIUM | 50 | - | - | 15 | 5 | 100, 500 |
| `EFT_LANE_SHIP_TRAVEL` | 0.5 | 0.5 | EFT_LOD_MEDIUM | 50 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_CUSTOM_EFFECTS` | 0 | 0 | EFT_LOD_LARGE | -1 | - | - | 15 | 5 | 100, 500 |
| `EFT_CODE_PROJ_EFFECT` | 1 | 0.5 | EFT_LOD_WEAPON | 15 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_TRACTORBEAM` | 1 | 1 | EFT_LOD_NONE | -1 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 100, 500 |
| `EFT_FLASHLIGHT` | 1 | 1 | EFT_LOD_NONE | -1 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |

---

## 5. Shape databases (`[Texture]` and `[Shape]`)

A *shape database* maps a shape name to a rectangle of a texture; the UI, HUD, cursors, star glows and many effects draw from them. One parser, `FShapeDB::Load(path, baseDir)` ([texture_shapes.cpp:299](../../src/freelancer/Client/texture_shapes.cpp#L299)), reads all of them. When the base directory is given, the path is joined to it; otherwise it is used as is. A file that cannot be opened logs `unable to load ShapeDB file` and loads nothing. Shape names are compared case-insensitively and are limited to 23 characters; a name defined twice logs `shape name already found` and the **first** definition is kept. A shape lookup that finds nothing logs a `missing shape` warning at the point of use.

Files that are loaded as shape databases:

| File | Named by | Source |
|---|---|---|
| `mouse.ini` (also holds `[Cursor]`, see [client-ui-config.md](client-ui-config.md#6-mouse-cursors-mouseini)) | fixed name | [input_manager.cpp:1104](../../src/freelancer/Client/input_manager.cpp#L1104) |
| `Interface\ButtonTextures.ini` | fixed name | [nn_montage_draw.cpp:40](../../src/freelancer/Client/nn_montage_draw.cpp#L40) |
| `Interface\NeuroNet\NavMap\NewNavMap\NavMapTextures.ini` | fixed name | [nn_navmap.cpp:1102](../../src/freelancer/Client/nn_navmap.cpp#L1102) |
| `Interface\UI\UITextures.ini` | fixed name | [ui_scroll.cpp:163](../../src/freelancer/Client/ui_scroll.cpp#L163) |
| `solar\StarArch.ini` | fixed name (the same file also carries star/lens-glow sections, see [client-world-fx.md](client-world-fx.md)) | [star_sphere.cpp:2745](../../src/freelancer/Client/star_sphere.cpp#L2745) |
| the `shapes` file of `[HUD]` in `hud.ini` | [client-ui-config-windows.md](client-ui-config-windows.md#41-hud) | [hud_element_list.cpp:463](../../src/freelancer/Client/hud_element_list.cpp#L463) |
| every `[Data] effect_shapes` file | `Freelancer.ini` | [dataconfig_progress.cpp:135](../../src/freelancer/Client/dataconfig_progress.cpp#L135), [texture_shapes.cpp:728](../../src/freelancer/Client/texture_shapes.cpp#L728) |
| every `file =` of a `[TexturePanels]` section in system, nebula and asteroid-field files | [systems-and-bases.md](systems-and-bases.md), [client-world-fx.md](client-world-fx.md) | [star_sys.cpp:401](../../src/freelancer/Client/star_sys.cpp#L401), [texture_panels.cpp:994](../../src/freelancer/Client/texture_panels.cpp#L994) |

### 5.1 `[Texture]` section (repeated)

The keys form a small state machine and are processed in order. Typical use:

```ini
[Texture]
file         = textures\example_atlas.txm       ; texture archive to load
texture_name = example_atlas_tex                ; texture inside the archive
shape_name   = button_normal                    ; start a shape on that texture ...
dim          = 0, 0, 64, 64                     ; ... and give it a rectangle
shape_name   = button_pressed
dim          = 64, 0, 64, 64
tex_shape    = example_full_tex                 ; one more texture, whole-texture shape of the same name
```

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `file` | string | none | Texture archive (`.txm`) to load; the path goes to the data file system. An empty value, or the same name as the previous `file` (case-insensitive), is ignored. If the archive cannot be opened the game logs `file not found`. All archives loaded by one call are released again at the end of the load, so the textures stay only as long as shapes reference them. Resets the current texture. | [texture_shapes.cpp:349](../../src/freelancer/Client/texture_shapes.cpp#L349) |
| `name` or `texture_name` (synonyms) | string | none | Selects the texture by name inside the loaded archives (CRC-32 of the name). A texture that is not found logs `missing texture`; the current texture is then empty. Logs `shape definition never followed texture` if the previous texture selection was not followed by a shape. | [texture_shapes.cpp:399](../../src/freelancer/Client/texture_shapes.cpp#L399) |
| `shape_name` | string (≤23 chars) | none | Starts a new shape on the current texture (it covers the whole texture until a `dim` follows). Error `'shape_name' field must be preceeded by valid 'texture_name' field` without a current texture. | [texture_shapes.cpp:436](../../src/freelancer/Client/texture_shapes.cpp#L436) |
| `tex_shape` | string | none | Shortcut: selects the texture of that name **and** creates a shape of the same name covering the whole texture. A missing texture is skipped. | [texture_shapes.cpp:473](../../src/freelancer/Client/texture_shapes.cpp#L473) |
| `dim` | 4 to 8 numbers | none | `dim = x, y, w, h [, ul_u, ul_v, lr_u, lr_v]` — sets the rectangle of the shape just started by `shape_name` (see 5.3). The first four values are required; the last four read as `0` when omitted. A `dim` without a preceding `shape_name` logs an error and is ignored; it ends the shape (a second `dim` needs a new `shape_name`). | [texture_shapes.cpp:519](../../src/freelancer/Client/texture_shapes.cpp#L519) |

Any other key logs `unknown [Shape] parameter` (the message says `[Shape]` even inside `[Texture]`) and is ignored.

### 5.2 `[Shape]` section (repeated)

Defines one shape on the texture selected by the most recent `[Texture]` section (the selection survives from section to section). An error `bad texture id` is logged when there is none.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `name` or `shape_name` | string (≤23 chars) | none (required) | Shape name. A section without a name logs `no shape name was specified`. | [texture_shapes.cpp:581](../../src/freelancer/Client/texture_shapes.cpp#L581) |
| `x`, `y` | number | `0` | Top-left corner of the rectangle. | [texture_shapes.cpp:594](../../src/freelancer/Client/texture_shapes.cpp#L594) |
| `w`, `h` | number | `0` | Size of the rectangle. | [texture_shapes.cpp:602](../../src/freelancer/Client/texture_shapes.cpp#L602) |
| `upper_left` | number, number | `-1, -1` | Corner of the inner (content) area, same units as `x`/`y`; a negative component means "use the rectangle's edge". | [texture_shapes.cpp:610](../../src/freelancer/Client/texture_shapes.cpp#L610) |
| `lower_right` | number, number | `-1, -1` | Opposite corner of the inner area; `1` is added to each component (the values are inclusive pixel coordinates). | [texture_shapes.cpp:615](../../src/freelancer/Client/texture_shapes.cpp#L615) |
| `dim` | 8 numbers | – | Sets `x, y, w, h, upper_left (2), lower_right (2)` at once; **no** `+1` is applied to the `lower_right` pair, and omitted values read as `0`. | [texture_shapes.cpp:622](../../src/freelancer/Client/texture_shapes.cpp#L622) |

Any other key logs `unknown shape parameter`.

### 5.3 Rectangle rules

Applied by `TexShapeSetRect` ([texture_shapes.cpp:137](../../src/freelancer/Client/texture_shapes.cpp#L137)) to every shape:

* `x`, `y`: if both are `<= 1` they are taken as fractions of the texture size and multiplied by the texture width/height; otherwise they are texel positions.
* `w`, `h`: if either is `0`, **both** become the full texture size. If both are `<= 1` (and non-zero) they are fractions of the texture size; otherwise texels.
* The texture coordinates are inset by `0.01` texel on every edge so that neighbouring shapes in an atlas do not bleed.
* `upper_left` / `lower_right`: a negative component is replaced by the corresponding edge of the rectangle; they define the inner area from which the shape's scale and centre offset are computed.

---

## 6. Debris (`[Debris]`)

`[Data] debris = <file>`; loaded by `LoadDebrisArchFile` ([client_debris.cpp:152](../../src/freelancer/Client/client_debris.cpp#L152)); only `[Debris]` sections are read, each parsed by `ReadDebrisArch` ([client_debris.cpp:70](../../src/freelancer/Client/client_debris.cpp#L70)). A file that cannot be opened logs `Failed to open debris file`. A section without `nickname` is dropped. When a nickname already exists, the **first** definition is kept and the later one is discarded (the log text `First overwritten with second` is misleading).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | nickname | none (required) | Name by which other files (for example explosion definitions) refer to this debris type. | [client_debris.cpp:79](../../src/freelancer/Client/client_debris.cpp#L79) |
| `trail` | nickname | none | Effect archetype attached as a trail to each piece. | [client_debris.cpp:83](../../src/freelancer/Client/client_debris.cpp#L83) |
| `lifetime` | number [, number] | min `0`, max `3.4e38` | `lifetime = <min>[, <max>]` in seconds. One value sets min and max to it; if the second value is below the first, max becomes min. | [client_debris.cpp:87](../../src/freelancer/Client/client_debris.cpp#L87) |
| `explosion` | nickname | none | Explosion archetype created when a piece dies by exploding. | [client_debris.cpp:100](../../src/freelancer/Client/client_debris.cpp#L100) |
| `death_method` | string | `vanishing` | `exploding` or `vanishing` (case-insensitive). Any other value leaves the previous setting (the default, `vanishing`). A vanishing piece may be removed as soon as it has lived longer than the minimum lifetime. | [client_debris.cpp:104](../../src/freelancer/Client/client_debris.cpp#L104) |
| `linear_drag` | number | `0` | Linear drag. Not validated. | [client_debris.cpp:119](../../src/freelancer/Client/client_debris.cpp#L119) |
| `angular_drag` | number ×3 | `0, 0, 0` | Angular drag per axis. Not validated. | [client_debris.cpp:123](../../src/freelancer/Client/client_debris.cpp#L123) |

---

## 7. Dynamic asteroids (`[DynamicAsteroids]`)

A `[DynamicAsteroids]` section sits inside an asteroid-field file (the rest of the field file, `[Field]`, `[Band]`, billboards and so on, is in [client-world-fx.md](client-world-fx.md)); it is dispatched from `ParseFieldSection` ([cli_asteroid.cpp:991](../../src/freelancer/Client/cli_asteroid.cpp#L991)) and parsed by `CliDynamicAsteroidParseIni` ([cli_asteroid_dynamic.cpp:420](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L420)). It configures a pool of asteroids that are spawned around the camera and recycled. A field can contain several such sections; each creates its own pool.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `asteroid` | string | none | Nickname of a *dynamic asteroid archetype* (hashed). Not found: warning `dynamic asteroid archetype ... not found` and the pool stays empty. | [cli_asteroid_dynamic.cpp:427](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L427) |
| `count` | number → int | `10` | Pool size before detail scaling; the real pool is `int(count * detail_scale + 0.5)`, where the detail scale is `(1 - d) * 0.5 + d` for the global asteroid detail `d` (the `Asteroids` performance option, `1` by default, i.e. a scale between 0.5 and 1). Not validated (a negative value gives an invalid allocation). | [cli_asteroid_dynamic.cpp:449](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L449), [cli_asteroid.cpp:1005](../../src/freelancer/Client/cli_asteroid.cpp#L1005) |
| `placement_radius` | number | `150` | Radius around the camera in which asteroids are placed; the inner radius is set to `0.7 *` this value (default inner `75`). | [cli_asteroid_dynamic.cpp:454](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L454) |
| `max_velocity` | number | `50` | Upper bound of the random initial speed. | [cli_asteroid_dynamic.cpp:461](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L461) |
| `max_angular_velocity` | number | `5` | Upper bound of the random spin. | [cli_asteroid_dynamic.cpp:465](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L465) |
| `color_shift` | number ×3 | `1, 1, 1` | Per-channel colour multiplier applied to the asteroids. | [cli_asteroid_dynamic.cpp:469](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L469) |
| `zone` | string | the field's zone | Zone nickname (in the active system) in which the pool spawns. Read in a second pass by the parent ([cli_asteroid.cpp:1025](../../src/freelancer/Client/cli_asteroid.cpp#L1025)); an unknown zone logs `linked to missing Zone`; with no valid `zone` the field's own zone is used. | [cli_asteroid.cpp:1025](../../src/freelancer/Client/cli_asteroid.cpp#L1025) |

Dynamic asteroids are only simulated while the game option `DYN_ASTEROIDS` is on.

---

## 8. Graph library (`[IGraph]`)

`[Data] igraph = <file>`; `GraphLibraryLoad` ([graph_library.cpp:340](../../src/freelancer/Client/graph_library.cpp#L340)). Defines named piecewise curves (`IGraph`) that other systems sample (colour-over-time and similar). **Errors in this file raise a Windows message box** (`unable to open IGraph file`, `invalid IGraph::ElemType`, `unknown IGraph parameter`, `unable to create IGraph object (duplicate?)`), so a typo is immediately visible.

Each `[IGraph]` section has the shape `nickname`, `type`, then a list of `point` lines. The `type` line starts the point list: **every key after `type` must be `point`**, so `nickname` has to come first.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | string (≤127 chars) | none | Curve name (case-insensitive; a duplicate name is an error and the curve is not created). | [graph_library.cpp:372](../../src/freelancer/Client/graph_library.cpp#L372) |
| `type` | string | none | Element type: `FLOAT`, `COLOR` or `INTEGER` (case-insensitive). Anything else shows an error box and the section is skipped. | [graph_library.cpp:387](../../src/freelancer/Client/graph_library.cpp#L387) |
| `point` | **repeated**; `x, value` | none | One key of the curve. `x` is a number (the position along the curve, normally increasing; not validated). `value` is a number for `FLOAT`; for `INTEGER` and `COLOR` it is truncated to a 32-bit integer. At most 1024 points per curve are held; more overflow the loader's buffer (not checked). | [graph_library.cpp:418](../../src/freelancer/Client/graph_library.cpp#L418) |

---

## 9. Mission-created solars: `Universe\MissionCreatedSolars.ini`

Fixed name, relative to the Data path; loaded once, on first use ([mission_created_solars.cpp:12](../../src/freelancer/Client/mission_created_solars.cpp#L12)). It gives position and info-card text for bases that missions create at run time and which therefore have no `[Object]` in a system file; the navigation map uses it ([nn_navmap_update.cpp:233](../../src/freelancer/Client/nn_navmap_update.cpp#L233)). A missing file means no entries.

`[MissionCreatedSolar]` (repeated section):

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `base` | nickname | none | Base nickname (hashed). | [mission_created_solars.cpp:41](../../src/freelancer/Client/mission_created_solars.cpp#L41) |
| `ids_info` | number → int | none | String-resource id of the info card. | [mission_created_solars.cpp:46](../../src/freelancer/Client/mission_created_solars.cpp#L46) |
| `pos` | number ×3 | none | System position `x, y, z`. | [mission_created_solars.cpp:51](../../src/freelancer/Client/mission_created_solars.cpp#L51) |

A record is added **as soon as all three keys have been seen in the section** (in any order), and the "seen" flags are then reset, so one section can produce several records if the keys repeat. The last values of each key carry over between sections (they are not cleared), so a section that repeats only `base` reuses the previous `ids_info` and `pos` only if all three were seen again in it. Lookups return the first record with a given base.

---

## 10. Planet-ring file (`FTexturePanels::Load`)

A solar object in a system file can name a ring with `ring = <zone nickname>, <file>` ([client_gameobj.cpp:2159](../../src/freelancer/Client/client_gameobj.cpp#L2159), see [systems-and-bases.md](systems-and-bases.md)). The `<file>` is read by `FTexturePanels::Load` ([texture_panels.cpp:961](../../src/freelancer/Client/texture_panels.cpp#L961)); the zone must exist in the active system (without it the log reads `planet ring must have zone` and no ring is built). The ring is a flat band between the zone's two sizes, drawn as a mesh.

### 10.1 `[TexturePanels]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `file` | string, **repeated** | none | Shape database ([section 5](#5-shape-databases-texture-and-shape)) holding the ring textures, resolved against the Data path. | [texture_panels.cpp:998](../../src/freelancer/Client/texture_panels.cpp#L998) |

### 10.2 `[Exterior]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `color` | number ×3 | previous value | R, G, B of the ring, each `0 ... 255` (divided by 255). | [texture_panels.cpp:1010](../../src/freelancer/Client/texture_panels.cpp#L1010) |
| `shape` | string | none | Shape (from the `[TexturePanels]` databases) used for the ring. An unknown name logs `shape ... not found for rings`. | [texture_panels.cpp:1016](../../src/freelancer/Client/texture_panels.cpp#L1016) |
| `detail_shape` | string | none | Optional second shape drawn as detail over the first. | [texture_panels.cpp:1025](../../src/freelancer/Client/texture_panels.cpp#L1025) |
| `detail_tile` | number, number | previous value | Tiling of the detail shape; the second value is truncated to an integer. | [texture_panels.cpp:1035](../../src/freelancer/Client/texture_panels.cpp#L1035) |
| `opacity` | number | `1.0` | Ring opacity. | [texture_panels.cpp:1040](../../src/freelancer/Client/texture_panels.cpp#L1040) |
| `thickness_edge` | number | `0.1` | Edge fade thickness parameter. | [texture_panels.cpp:1042](../../src/freelancer/Client/texture_panels.cpp#L1042) |
| `inner_edge` | number | `0.1` | Inner edge fade parameter. | [texture_panels.cpp:1044](../../src/freelancer/Client/texture_panels.cpp#L1044) |
| `outer_edge` | number | `0.1` | Outer edge fade parameter. | [texture_panels.cpp:1046](../../src/freelancer/Client/texture_panels.cpp#L1046) |
| `num_segments` | number → int | `32` | Number of segments around the ring (the mesh has `2 * num_segments` vertices). Not validated (`0` divides by zero when the angle step is computed). | [texture_panels.cpp:1048](../../src/freelancer/Client/texture_panels.cpp#L1048) |
| `transition_dist` | number | `150` | Distance of the transition between the ring and its far representation. | [texture_panels.cpp:1050](../../src/freelancer/Client/texture_panels.cpp#L1050) |

### 10.3 `[Clouds]` (repeated)

Adds a layer of cloud panels to the ring. The keys are those of a nebula `[Clouds]` section, parsed by the nebula code ([cli_nebula.cpp:1824](../../src/freelancer/Client/cli_nebula.cpp#L1824)); see [client-world-fx.md](client-world-fx.md). Each section creates one cloud layer.
