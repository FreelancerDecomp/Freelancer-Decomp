# Client world, space and FX configuration (system file, star, asteroid, nebula)

This page documents how the game client (`freelancer.exe`) reads the parts of the per-system INI file that describe what the player sees in space, and the files that file points at: the star archetype file (`solar\StarArch.ini`), asteroid field files and nebula files. Visual-effect archetypes, jump/gate effects, light animations and client fuses are in [client-world-fx-effects.md](client-world-fx-effects.md); the client-side keys of archetype records (`ICliObj::ReadUnknown`, `[LOD]`) are in [client-world-fx-archetypes.md](client-world-fx-archetypes.md).

**File location / how it is found.** The system file is the file named by `file =` in the matching `[system]` block of `universe.ini` (resolved by `Universe::get_filename`, relative to the directory of `universe.ini`; see [world-data.md](world-data.md) and [systems-and-bases.md](systems-and-bases.md)). The client opens it when the player enters a system (`StarSysEnter`, [star_sys.cpp:811](../../src/freelancer/Client/star_sys.cpp#L811)). Files that the system file points at (`[Asteroids] file`, `[Nebula] file`, `[Object] ring`) are resolved against the data path (`GetFileSysPath(.., DALib::Data)`, `[Freelancer] data path` in [freelancer-ini.md](freelancer-ini.md)); absolute paths are accepted there. `[TexturePanels] file` entries are resolved with `GetFullFilename` (data path joined unconditionally, no absolute-path shortcut). `solar\StarArch.ini` is a literal name, also under the data path. Every reader uses `INI_Reader` ([parsers.md](parsers.md)), so every file may be text or BINI, header and key names are case-insensitive, and repeated keys/sections are all delivered in file order.

## Notation

| Type | Meaning |
|---|---|
| `float` | `get_value_float(i)`; `ParseNumber` of one comma-separated parameter, stored as `float`. `float[0..2]` means three parameters. |
| `int` | `get_value_int(i)` (MSVCRT `atoi`: decimal, wraps modulo 2^32). |
| `bool` | `get_value_bool(i)`: text files accept exactly `true`/`1`/`false`/`0`; anything else logs "Invalid bool value" and gives `false`. |
| `str` (whole value) | `get_value_string()`: the whole right-hand side, commas included. |
| `str[i]` | `get_value_string(i)`: one trimmed comma-separated parameter. |
| `id` | `CreateID(string)`: 32-bit hash of the text; nicknames are matched through this hash. |
| `byte` | A float read from the file and converted with a truncating `ftol`; only the low 8 bits are kept, so 300 becomes 44 and -1 becomes 255. Not validated. |

"Default" is the value present before the file is parsed (constructor or loader initialisation). There is no generic default mechanism; a key that is absent leaves that value, a repeated scalar key keeps the last occurrence unless stated. Nothing is range-checked unless the table says so ("not validated" is stated where the omission is likely to matter).

## 1. System file: client reader (`StarSysLoadIni`)

[star_sys.cpp:241](../../src/freelancer/Client/star_sys.cpp#L241). Two passes run on the same file:

1. `PreloadSystemObjectArchetypes` ([L196](../../src/freelancer/Client/star_sys.cpp#L196)) reads only `[Object]` / `archetype =` and pins every named solar archetype until the load finishes.
2. `StarSysLoadIni` walks every section once, in file order, and tests the header against the list below. Any other header (`[Zone]`, `[EncounterParameters]`, `[Music]`-like unknown names, ...) is skipped here; those are read by Common, Content and the server (see [world-data.md](world-data.md), [ai-and-encounters.md](ai-and-encounters.md), [systems-and-bases.md](systems-and-bases.md)).

If the file cannot be opened the client logs the warning "unknown system (%s)" and goes on without reading anything. Every occurrence of a repeatable section (`[Object]`, `[LightSource]`, `[Asteroids]`, `[Nebula]`, `[Archetype]`, `[TexturePanels]`) is processed. Unknown keys inside a recognised section are ignored without a message except where noted.

### 1.1 `[SystemInfo]` (alias `[System]`)

Both header spellings run the same code ([L259](../../src/freelancer/Client/star_sys.cpp#L259)).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `space_color` | byte x3 (`float[0..2]`) | `0, 0, 64` | RGB clear colour of space. | [L263](../../src/freelancer/Client/star_sys.cpp#L263) |
| `space_farclip` | float | 21000 (draw range 20000 + 1000) | Far clip distance. Values below 1000 are raised to 1000; the draw range becomes the value minus 1000. | [L269](../../src/freelancer/Client/star_sys.cpp#L269), [space_scene_obj_draw.cpp:2799](../../src/freelancer/Client/space_scene_obj_draw.cpp#L2799) |

The defaults for the far clip are re-applied (`SpaceResetFarClip`) at the start of every system load. The same section is also parsed by Common and Content (see [world-data.md](world-data.md)).

### 1.2 `[Music]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `space` | id (`str`) | 0 | Nickname of the music track for ordinary flight. | [L279](../../src/freelancer/Client/star_sys.cpp#L279) |
| `danger` | id (`str`) | 0 | Track when hostile ships are near. | [L281](../../src/freelancer/Client/star_sys.cpp#L281) |
| `battle` | id (`str`) | 0 | Track during combat. | [L283](../../src/freelancer/Client/star_sys.cpp#L283) |
| `explore` | (value unused) | - | Obsolete. Logs the warning "explore music is obsolete in system file %s, valid types are space, danger and battle". | [L285](../../src/freelancer/Client/star_sys.cpp#L285) |

The three ids are zeroed each time a system is entered ([L824](../../src/freelancer/Client/star_sys.cpp#L824)) and are given to the music state machine ([L981](../../src/freelancer/Client/star_sys.cpp#L981)); a scripted override takes precedence, and a zone with a `music` key (zone property mask 0x400000) at the player's position replaces the `space` track. The nicknames are sound-archetype nicknames (see [client-audio-misc.md](client-audio-misc.md)); their existence is not checked at load time.

### 1.3 `[Archetype]` (preload list)

Pins archetypes and sounds so they are resident before the player arrives. The section is repeatable and every key may repeat; each occurrence adds one entry. Entries are released again when the system is left (`ReleaseSystemPreloads`, [L465](../../src/freelancer/Client/star_sys.cpp#L465)).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `snd` | id (`str`) | - | Sound nickname to preload (client sound archetype). | [L297](../../src/freelancer/Client/star_sys.cpp#L297) |
| `voice` | id (`str[0]`), id (`str[1]`) | - | `voice = <voice nickname>, <message nickname>`. A voice that does not exist logs "Voice %u is invalid"; a message that does not exist in that voice logs "Voice message %u is invalid for voice %u". | [L305](../../src/freelancer/Client/star_sys.cpp#L305), [L171](../../src/freelancer/Client/star_sys.cpp#L171) |
| `ship` | id (`str`) | - | Ship archetype nickname. | [L315](../../src/freelancer/Client/star_sys.cpp#L315) |
| `equipment` | id (`str`) | - | Equipment archetype nickname. | [L317](../../src/freelancer/Client/star_sys.cpp#L317) |
| `simple` | id (`str`) | - | Simple (explosion/debris) archetype nickname. | [L319](../../src/freelancer/Client/star_sys.cpp#L319) |
| `solar` | id (`str`) | - | Solar archetype nickname. | [L321](../../src/freelancer/Client/star_sys.cpp#L321) |
| `asteroid` | id (`str`) | - | Asteroid archetype nickname. | [L323](../../src/freelancer/Client/star_sys.cpp#L323) |

Any other key logs the warning "unknown (or old-style) preload archetype (<line>)". A name that is not a known archetype of that kind logs "Failed to preload archetype". Names are looked up in what the archetype files have already loaded ([archetypes-and-equipment.md](archetypes-and-equipment.md)).

### 1.4 `[Ambient]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `color` | byte x3 (`float[0..2]`) | `0, 0, 0` | System ambient light RGB. The renderer adds the user's ambient game option (clamped at 255). | [L350](../../src/freelancer/Client/star_sys.cpp#L350), [L798](../../src/freelancer/Client/star_sys.cpp#L798) |

### 1.5 `[Background]` (star sphere meshes)

At the start of each `[Background]` section all three layer names are reset to the empty string, so a section replaces the previous one completely ([L358](../../src/freelancer/Client/star_sys.cpp#L358)). Each value is the name of a mesh (model archetype) loaded as a star-sphere layer; a name that cannot be loaded logs the error "couldnt create starsphere %s" when the layer is first needed.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nebulae` | str (whole value) | empty | Nebula layer (layer 0). | [L370](../../src/freelancer/Client/star_sys.cpp#L370) |
| `basic_stars` | str (whole value) | empty | Basic star layer (layer 1). | [L366](../../src/freelancer/Client/star_sys.cpp#L366) |
| `complex_stars` | str (whole value) | empty | Complex star layer (layer 2). | [L368](../../src/freelancer/Client/star_sys.cpp#L368) |
| `file` | str (whole value) | empty | Alias: writes layer 2, exactly like `complex_stars`; the last of the two keys in the file wins. | [L372](../../src/freelancer/Client/star_sys.cpp#L372) |

Which layers are drawn follows the star-sphere detail setting ([star_sys.cpp:698](../../src/freelancer/Client/star_sys.cpp#L698)): detail above 0.75 draws nebulae and complex stars, above 0.5 nebulae and basic stars, otherwise nebulae only; 0.001 or less draws nothing. The layers are drawn in the order basic, complex, nebulae. The whole sphere can also be switched off by game options 0x19 / 0x1a.

### 1.6 `[Dust]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `spacedust` | str (whole value) | empty | Name of the system-wide space dust effect, handed to `SpaceDustSetSystemDust`. (The zone-level `spacedust` key is read by Common, see [world-data.md](world-data.md).) | [L382](../../src/freelancer/Client/star_sys.cpp#L382) |

### 1.7 `[LightSource]`

Each `[LightSource]` section creates one game light, feeds every key to the light parser below and then enables the light ([star_sys.cpp:388](../../src/freelancer/Client/star_sys.cpp#L388), `GameLightLoad` [engine_light.cpp:1803](../../src/freelancer/Client/engine_light.cpp#L1803)). Key names here are compared with a case-insensitive string compare; an unrecognised key is ignored without a message. Positional numbers are read with `atof` on the comma-separated value text (`ValueNum`, [common_ini.cpp:326](../../src/Common/common_ini.cpp#L326)), so hexadecimal and base-N forms that `get_value_float` accepts are **not** understood here.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `type` | enum, one word | none (-1) | `AMBIENT`, `POINT`, `CONE` or `DIRECTIONAL` (case-insensitive; internal ids -2, 1, 2, 3). Anything else logs the error "invalid [LightSource] type = '%s'" and sets type -1. Set it before `pos`/`direction`: those keys behave differently per type. | [L749](../../src/freelancer/Client/engine_light.cpp#L749), [L591](../../src/freelancer/Client/engine_light.cpp#L591) |
| `pos` | number x3 | `0, 0, 0` | Light position. The Z axis is negated for the Direct3D copy; for `DIRECTIONAL` lights only the stored position changes. | [L701](../../src/freelancer/Client/engine_light.cpp#L701) |
| `range` | number | 0 | Reach of the light. | [L720](../../src/freelancer/Client/engine_light.cpp#L720) |
| `color` | number x3 | `1, 1, 1` | RGB in 0..255; clamped to 0..255 per channel and stored in whole 1/255 steps. | [L726](../../src/freelancer/Client/engine_light.cpp#L726) |
| `attenuation` | number x3 | `0, 1, 0` | Constant, linear, quadratic attenuation. | [L757](../../src/freelancer/Client/engine_light.cpp#L757) |
| `direction` | number x3 | none | Direction vector, normalised on read (a zero vector divides by zero, not guarded). Ignored for the Direct3D copy of `POINT` lights. | [L767](../../src/freelancer/Client/engine_light.cpp#L767) |
| `ambient_curve` | str (whole value) | none | Name of a graph (from `igraph.ini`, `[Data] igraph`) stored as the light's ambient curve. An unknown name shows a message box "Error: unknown graph name". | [L795](../../src/freelancer/Client/engine_light.cpp#L795) |
| `atten_curve` | graph name (`str[0]`), optional type (`str[1]`) | none | `atten_curve = <graph>[, <type>]`. The optional second parameter is a light type (same words as `type`) applied first. The graph becomes the distance falloff curve (evaluated over `0..range`); on an `AMBIENT` light it is stored as the ambient curve instead. Unknown graph: message box. | [L813](../../src/freelancer/Client/engine_light.cpp#L813) |
| `color_curve` | graph name (`str[0]`), number (`[1]`) | none | `color_curve = <graph>, <period seconds>`: the graph's colour multiplies the light colour over time; the time wraps at the period. Unknown graph: message box. | [L823](../../src/freelancer/Client/engine_light.cpp#L823) |

Graph names are defined by `[IGraph]` sections of `igraph.ini` (client UI/config chunk: [client-ui-config.md](client-ui-config.md)).

### 1.8 `[Object]` (client half)

`[Object]` sections are consumed three times: by Common (`CSolar::ReadObj`, which creates the object from its own keys such as `nickname`, `pos`, `archetype`; see [world-data.md](world-data.md)), by the client pre-pass (`archetype`, section 1), and by the client solar game object `FClientSolarGameobj::ReadIni` ([client_gameobj.cpp:2148](../../src/freelancer/Client/client_gameobj.cpp#L2148)), which re-reads the same section after Common finished ([space_solar_world.cpp:26](../../src/freelancer/Client/space_solar_world.cpp#L26)). The client half tests the keys below in this order and the first matching key wins; every other key is ignored.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `archetype` | solar nickname (`str`) | - | Pre-pass only: pins the solar archetype while the system loads. An unknown nickname logs the error "unknown Solar archetype (%s)" and is not guarded afterwards. | [star_sys.cpp:210](../../src/freelancer/Client/star_sys.cpp#L210) |
| `ring` | `str[0]`, `str[1]` | - | `ring = <zone nickname>, <ini file>`: creates a texture-panel ring around the named zone of this system, read from the INI file (data-path relative). Repeatable; each key creates one ring. A zone that does not exist is dereferenced without a check. The ring file format (`[TexturePanels]`, `[Exterior]`, `[Clouds]`) is read by `FTexturePanels::Load` ([texture_panels.cpp:961](../../src/freelancer/Client/texture_panels.cpp#L961)); see [client-ui-config.md](client-ui-config.md). | [L2159](../../src/freelancer/Client/client_gameobj.cpp#L2159) |
| `burn_color` | float x3 | unset | Burn colour of the object, 0..255 per channel (divided by 255 on read). | [L2171](../../src/freelancer/Client/client_gameobj.cpp#L2171) |
| `dock_script` | str (whole value) | empty | Stored as the object's dock script name; truncated to 259 characters. | [L2183](../../src/freelancer/Client/client_gameobj.cpp#L2183) |
| `voice` | id (`str`) | 0 | Voice nickname stored on the solar object. | [L2189](../../src/freelancer/Client/client_gameobj.cpp#L2189) |
| `jump_effect` | id (`str`) | 0 (uses `jump_effect_default`) | Nickname of a `[JumpGateEffect]` block of the jump-effect file ([client-world-fx-effects.md](client-world-fx-effects.md)). | [L2197](../../src/freelancer/Client/client_gameobj.cpp#L2197) |
| `star` | str (whole value) | - | Nickname of a `[star]` block of `solar\StarArch.ini` (section 2). Creates a star sphere for this object. | [L2202](../../src/freelancer/Client/client_gameobj.cpp#L2202) |

After the keys the object looks up the nebula that contains it (no key involved).

### 1.9 `[TexturePanels]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `file` | str (whole value) | - | Shape-library INI (`[Texture]`/`[Shape]` records) added to the system's shape database. Repeatable; resolved with `GetFullFilename`. Format: [client-ui-config.md](client-ui-config.md). | [L405](../../src/freelancer/Client/star_sys.cpp#L405) |

### 1.10 `[Asteroids]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `file` | str (whole value), max 255 chars | empty | Asteroid field INI (section 3), data-path relative. | [L423](../../src/freelancer/Client/star_sys.cpp#L423) |
| `zone` | str (whole value), max 63 chars | empty | Nickname of the zone of this system that the field fills. The last `file` and the last `zone` of the section are used, so one section creates exactly one field. | [L425](../../src/freelancer/Client/star_sys.cpp#L425) |

The first `[Asteroids]` section of a load also loads Common's asteroid data for the system (`CmnAsteroid::Load`). A zone that does not exist logs the error "AsteroidField (%s) linked to missing Zone (%s)" and the field is not loaded. The server and Common read the same section ([server.md](server.md), [world-data.md](world-data.md)).

### 1.11 `[Nebula]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `file` | str (whole value), max 255 chars | - | Nebula INI (section 4), data-path relative. Every `file` key creates a new nebula object; if a section contains several, only the last one is loaded (the earlier ones are dropped without being freed). No `file` key means nothing is loaded. | [L439](../../src/freelancer/Client/star_sys.cpp#L439) |
| `zone` | str (whole value), max 63 chars | empty | Nickname of the zone that bounds the nebula (last occurrence wins). A missing zone logs the error "Nebula linked to missing Zone (%s)"; the loader then continues without a guard. | [L446](../../src/freelancer/Client/star_sys.cpp#L446) |

Example (synthetic):

```ini
[Asteroids]
file = solar\asteroids\demo_field.ini
zone = Zone_Demo_field

[Nebula]
file = solar\nebula\demo_nebula.ini
zone = Zone_Demo_nebula
```

## 2. Star archetypes: `solar\StarArch.ini`

An object gets a visible star when its `[Object]` has `star = <nickname>` (section 1.8). The client loads `solar\StarArch.ini` as a shape library at the start of every space entry (`LoadSolarStarArchIni`, [star_sphere.cpp:2740](../../src/freelancer/Client/star_sphere.cpp#L2740)); the same file also holds the `[star]`, `[star_glow]`, `[lens_glow]`, `[lens_flare]` and `[spines]` records described here, read by `FStarSphere::Load` ([star_sphere.cpp:1548](../../src/freelancer/Client/star_sphere.cpp#L1548)). Shape names used below are `[Shape]` names of that same file (format in [client-ui-config.md](client-ui-config.md)).

**Record lookup rule (all five record types).** The file is scanned header by header. For a header of the right name the reader looks for the **first `nickname` key** of the block and compares its value with the requested nickname (case-insensitive). If it differs the block is abandoned; if it matches, only the keys **after** `nickname` are applied. A key placed before `nickname` is therefore never used, and `nickname` should be the first key. Only the first matching block is read. Nothing is logged for a missing record or file.

### 2.1 `[star]`

Selected by the `star =` nickname of the object.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | str (whole value) | - | Record key. | [L1577](../../src/freelancer/Client/star_sphere.cpp#L1577) |
| `radius` | float | 1000 | Radius of the star in metres. | [L1590](../../src/freelancer/Client/star_sphere.cpp#L1590) |
| `star_glow` | str (whole value) | built-in glow | Nickname of a `[star_glow]` block (outer glow billboard). Applied once: after one succeeds, later `star_glow` keys are ignored; a failed lookup can be retried by a later key. | [L1594](../../src/freelancer/Client/star_sphere.cpp#L1594) |
| `star_center` | str (whole value) | none | Nickname of a `[star_glow]` block used as the bright centre. Every occurrence replaces the previous one; a failed lookup leaves no centre. | [L1600](../../src/freelancer/Client/star_sphere.cpp#L1600) |
| `lens_glow` | str (whole value) | built-in body | Nickname of a `[lens_glow]` block (corona/body). Applied once, as for `star_glow`. | [L1623](../../src/freelancer/Client/star_sphere.cpp#L1623) |
| `lens_flare` | str (whole value) | empty flare | Nickname of a `[lens_flare]` block. Applied once. | [L1617](../../src/freelancer/Client/star_sphere.cpp#L1617) |
| `spines` | str (whole value) | empty | Nickname of a `[spines]` block. Applied once. | [L1629](../../src/freelancer/Client/star_sphere.cpp#L1629) |
| `intensity_fade_in` | float | 1.0 | Seconds for the star to fade in; exactly 0 is replaced by 1e-7. | [L1635](../../src/freelancer/Client/star_sphere.cpp#L1635) |
| `intensity_fade_out` | float | 1.0 | Seconds to fade out; exactly 0 becomes 1e-7. | [L1643](../../src/freelancer/Client/star_sphere.cpp#L1643) |
| `zone_occlusion_fade_in` | float | 2.0 | As implemented (matching the retail listing) the value is stored into the **intensity** fade-in field; only the zone-occlusion field is tested for 0 and set to 1e-7. The zone-occlusion field itself keeps 2.0. | [L1650](../../src/freelancer/Client/star_sphere.cpp#L1650) |
| `zone_occlusion_fade_out` | float | 2.0 | Same quirk for the fade-out fields. | [L1659](../../src/freelancer/Client/star_sphere.cpp#L1659) |

### 2.2 `[star_glow]`

Used for both `star_glow` and `star_center` ([star_sphere.cpp:2211](../../src/freelancer/Client/star_sphere.cpp#L2211)). Colours are plain floats (0..1), not 0..255.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | str (whole value) | - | Record key (see lookup rule). | [L2228](../../src/freelancer/Client/star_sphere.cpp#L2228) |
| `shape` | str (whole value), max 31 chars | empty | Shape name in the StarArch shape library. | [L2240](../../src/freelancer/Client/star_sphere.cpp#L2240) |
| `scale` | float | 1.0 | Size multiplier (times the star radius). | [L2253](../../src/freelancer/Client/star_sphere.cpp#L2253) |
| `inner_color` | float x3 | `1, 1, 1` | Colour at the centre of the billboard. | [L2257](../../src/freelancer/Client/star_sphere.cpp#L2257) |
| `outer_color` | float x3 | `1, 1, 1` | Colour at the rim. | [L2266](../../src/freelancer/Client/star_sphere.cpp#L2266) |
| `alpha` | float | 1.0 | Opacity. | [L2275](../../src/freelancer/Client/star_sphere.cpp#L2275) |

### 2.3 `[lens_glow]`

[star_sphere.cpp:2288](../../src/freelancer/Client/star_sphere.cpp#L2288). After the block is read, the blended colour is recomputed from `inner_color`, `outer_color` and `interior_tween` and the corona geometry is rebuilt.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | str (whole value) | - | Record key. | [L2306](../../src/freelancer/Client/star_sphere.cpp#L2306) |
| `shape` | str (whole value), max 31 chars | empty | Shape name. | [L2318](../../src/freelancer/Client/star_sphere.cpp#L2318) |
| `radius_scale` | float | 2.0 | Corona size relative to the star's on-screen size (multiplies the star's pixel scale). | [L2334](../../src/freelancer/Client/star_sphere.cpp#L2334) |
| `inner_color` | float x3 | `1, 1, 1` | Inner colour (0..1). | [L2338](../../src/freelancer/Client/star_sphere.cpp#L2338) |
| `outer_color` | float x3 | `1, 1, 1` | Outer colour (0..1). | [L2347](../../src/freelancer/Client/star_sphere.cpp#L2347) |
| `glow_fade_in_seconds` | float | 0.25 | Fade-in time of the glow. | [L2356](../../src/freelancer/Client/star_sphere.cpp#L2356) |
| `glow_fade_out_seconds` | float | 0.25 | Fade-out time. | [L2360](../../src/freelancer/Client/star_sphere.cpp#L2360) |
| `interior_tween` | float | 0.5 | Blend factor between the inner and outer colour for the interior slots; also the inner/outer radius fraction of the interior ring. Not validated. | [L2364](../../src/freelancer/Client/star_sphere.cpp#L2364) |
| `interior_ray_tween` | float | 1.0 | Scale applied to the on-screen offsets of the outer corona points (used from detail level 3). Not validated. | [L2368](../../src/freelancer/Client/star_sphere.cpp#L2368) |
| `max_LOD` | float, converted to unsigned | 5 | Highest corona detail level; the active level is also lowered by the `SunEffect` performance setting. Not validated (negative values are undefined). | [L2372](../../src/freelancer/Client/star_sphere.cpp#L2372) |

### 2.4 `[lens_flare]`

[star_sphere.cpp:2511](../../src/freelancer/Client/star_sphere.cpp#L2511). The previous bead list is freed when the block is looked up, even if the lookup then fails.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | str (whole value) | - | Record key. | [L2531](../../src/freelancer/Client/star_sphere.cpp#L2531) |
| `bead` | float x6 | - | One flare bead per key (repeatable). The six numbers are stored unchanged; the code does not name them. | [L2543](../../src/freelancer/Client/star_sphere.cpp#L2543) |
| `min_radius` | float | 5.0 | Lower bound of the flare radius. | [L2550](../../src/freelancer/Client/star_sphere.cpp#L2550) |
| `max_radius` | float | 800 | Upper bound of the flare radius. | [L2554](../../src/freelancer/Client/star_sphere.cpp#L2554) |
| `shape` | str (whole value), max 31 chars | empty | Shape name of the bead texture. | [L2558](../../src/freelancer/Client/star_sphere.cpp#L2558) |

### 2.5 `[spines]`

[star_sphere.cpp:2600](../../src/freelancer/Client/star_sphere.cpp#L2600). The spine list is freed when the block is looked up.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `nickname` | str (whole value) | - | Record key. | [L2621](../../src/freelancer/Client/star_sphere.cpp#L2621) |
| `radius_scale` | float | 1.0 | Multiplier applied to the first two numbers of every spine after the block is read. | [L2633](../../src/freelancer/Client/star_sphere.cpp#L2633) |
| `shape` | str (whole value), max 31 chars | empty | Shape name. | [L2637](../../src/freelancer/Client/star_sphere.cpp#L2637) |
| `min_radius` | float | 1.0 | Lower bound of the spine radius. | [L2653](../../src/freelancer/Client/star_sphere.cpp#L2653) |
| `max_radius` | float | 1000 | Upper bound. | [L2657](../../src/freelancer/Client/star_sphere.cpp#L2657) |
| `spine` | float x9 | - | One spine per key (repeatable), nine numbers stored in order (not named in the code). The spines are spread evenly around a circle in file order (angle `2*pi*index/count`). | [L2661](../../src/freelancer/Client/star_sphere.cpp#L2661) |

Example (synthetic):

```ini
[star]
nickname = demo_star
radius = 2500
star_glow = demo_glow
lens_glow = demo_corona
intensity_fade_in = 1.5

[star_glow]
nickname = demo_glow
shape = demo_glow_shape
scale = 4
inner_color = 1, 0.9, 0.6
outer_color = 1, 0.5, 0.2
```

## 3. Asteroid field files (`[Asteroids] file =`)

Loaded by `CliAsteroidLoadFieldIni` ([cli_asteroid.cpp:858](../../src/freelancer/Client/cli_asteroid.cpp#L858)). The same file is read in full by several other readers: the server reads `[Field]` (`cube_size`, `fill_dist`, `empty_cube_frequency`) itself ([server.md](server.md)), and Common's `CAsteroidField::load` ([common_casteroidfield.cpp:74](../../src/Common/common_casteroidfield.cpp#L74)) reads `[Cube]`, `[Exclusion Zones]` and `[LootableZone]` from the same open file after the client sections (see [archetypes-and-equipment.md](archetypes-and-equipment.md) and [server.md](server.md)). The `[DynamicAsteroids]` section is described in 3.5.

Behaviour:

* A file that does not open logs "asteroid field ini file (%s) could not be opened"; the field object stays registered. A field with no zone logs "Asteroid field (%s) must be hooked up to a zone".
* Sections are processed in file order. `[TexturePanels]` must come **before** any `[Band]`, `[ExclusionBand]` and `[AsteroidBillboards]` that name shapes: shapes are looked up while the section is parsed, in the field's own shape library built from the `[TexturePanels] file =` entries read so far.
* The tint values of `[Field]` are applied to a `[Band]` and to `[AsteroidBillboards]` when that section is read, so `[Field]` should come first; `[ExclusionBand]` sections receive the final tint after the whole file has been read.
* Unknown sections and keys are ignored. Numeric values converted to integers are truncated toward zero.

### 3.1 `[TexturePanels]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `file` | str (whole value) | - | Shape-library INI for this field (`GetFullFilename`). Repeatable. | [L907](../../src/freelancer/Client/cli_asteroid.cpp#L907) |

### 3.2 `[Field]`

[ParseFieldSection, cli_asteroid.cpp:779](../../src/freelancer/Client/cli_asteroid.cpp#L779).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `cube_size` | float, truncated to int | 250 | Edge length of one cube of the field; also handed to Common's field loader. | [L788](../../src/freelancer/Client/cli_asteroid.cpp#L788) |
| `fill_dist` | float, truncated to int | 1200 | Distance around the camera in which cubes are populated; multiplied by the detail-slider scale before it is given to the common field. | [L792](../../src/freelancer/Client/cli_asteroid.cpp#L792) |
| `LOD` | float, truncated to int | 0 | Level of detail forced on the asteroid meshes; clamped to `0..(lod count - 1)` of each mesh when applied. | [L796](../../src/freelancer/Client/cli_asteroid.cpp#L796) |
| `tint_field` | float x3 (0..255) | `1, 1, 1` (after /255) | Sets **both** diffuse and ambient tint (each /255). | [L802](../../src/freelancer/Client/cli_asteroid.cpp#L802) |
| `diffuse_color` | float x3 (0..255) | `1, 1, 1` | Diffuse tint only; later of `tint_field`/`diffuse_color` wins. | [L814](../../src/freelancer/Client/cli_asteroid.cpp#L814) |
| `ambient_color` | float x3 (0..255) | `1, 1, 1` | Ambient tint only. | [L820](../../src/freelancer/Client/cli_asteroid.cpp#L820) |
| `ambient_increase` | float x3, truncated to bytes | `0, 0, 0` | Extra ambient added to bands (each /255 when applied). | [L826](../../src/freelancer/Client/cli_asteroid.cpp#L826) |
| `empty_cube_frequency` | float | 0 | Fraction of cubes left empty; handed to the common field after loading (`set_empty_freq`). Not validated. | [L832](../../src/freelancer/Client/cli_asteroid.cpp#L832) |
| `max_alpha` | float | 1.0 | Maximum alpha of the field's asteroids (the "full" alpha before fading). Not validated. | [L836](../../src/freelancer/Client/cli_asteroid.cpp#L836) |
| `test_asteroid_placement` | presence only | off | The value is ignored. When present, the near-asteroid draw pass returns at once ([L2616](../../src/freelancer/Client/cli_asteroid.cpp#L2616)): the cube asteroids are not drawn (a placement-debug switch). | [L840](../../src/freelancer/Client/cli_asteroid.cpp#L840) |
| `contains_fog_zone` | presence only | off | The value is ignored. When present and the camera is inside a nebula, the field's ambient pass takes its fog factor from that nebula ([L2131](../../src/freelancer/Client/cli_asteroid.cpp#L2131)). | [L844](../../src/freelancer/Client/cli_asteroid.cpp#L844) |

### 3.3 `[Band]` and `[ExclusionBand]`

A band is a ring of "spoke" meshes around the zone. `[Band]` (at most one is meaningful; a later one re-parses into the same band) belongs to the field's own zone ([L924](../../src/freelancer/Client/cli_asteroid.cpp#L924)); each `[ExclusionBand]` creates an additional band and takes the zone named by its `zone` key ([L938](../../src/freelancer/Client/cli_asteroid.cpp#L938)). Both use the same value parser ([BandParseIni, L169](../../src/freelancer/Client/cli_asteroid.cpp#L169)).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `shape` | str (whole value) | none | Shape name in the field's shape library. A name that is not found logs the shape-library warning and leaves no shape. | [L181](../../src/freelancer/Client/cli_asteroid.cpp#L181) |
| `texture_aspect` | float | 1.0 (the stored value is the key times 1.15) | Aspect ratio of one spoke; the ring circumference is divided by `aspect * height` to get the number of ring points. | [L187](../../src/freelancer/Client/cli_asteroid.cpp#L187), [L3133](../../src/freelancer/Client/cli_asteroid.cpp#L3133) |
| `height` | float | 1000 | Height of a spoke. The four `fade` distances are multiplied by it when the band is built, and it is then halved for use. | [L193](../../src/freelancer/Client/cli_asteroid.cpp#L193), [L3159](../../src/freelancer/Client/cli_asteroid.cpp#L3159) |
| `offset_dist` | float | 0 | Distance mode: the ring radius is the zone size (per axis, X and Z) **minus** this value. Clears the percent mode. | [L197](../../src/freelancer/Client/cli_asteroid.cpp#L197), [L3189](../../src/freelancer/Client/cli_asteroid.cpp#L3189) |
| `offset_percent` | float | - | Percent mode: the ring radius is the zone size **times** this value (1.0 = the zone edge). The last of `offset_dist`/`offset_percent` in the section selects the mode. | [L202](../../src/freelancer/Client/cli_asteroid.cpp#L202), [L3196](../../src/freelancer/Client/cli_asteroid.cpp#L3196) |
| `color_shift` | float x3 | `1, 1, 1` | Per-channel multiplier of the band tint. | [L207](../../src/freelancer/Client/cli_asteroid.cpp#L207) |
| `fade` | float x4 | `1500, 2500, 100000, 130000` | Four distances a, b, c, d, **in units of the band `height`** (they are multiplied by it). A spoke is invisible nearer than `a`, fades in between `a` and `b`, is fully visible from `b` to `c`, fades out between `c` and `d` and is invisible beyond `d`. Not validated (ascending order is assumed). | [L213](../../src/freelancer/Client/cli_asteroid.cpp#L213), [L1530](../../src/freelancer/Client/cli_asteroid.cpp#L1530) |
| `vert_increase` | float, truncated to int | 1 | Multiplier of the number of ring points; values below 1 are raised to 1. | [L220](../../src/freelancer/Client/cli_asteroid.cpp#L220) |
| `render_parts` | float, truncated to int | 1 | Number of parts the ring is split into for drawing; values below 1 are raised to 1 and the value is limited to half the number of ring points. | [L227](../../src/freelancer/Client/cli_asteroid.cpp#L227), [L3288](../../src/freelancer/Client/cli_asteroid.cpp#L3288) |
| `cull_mode` | float, truncated to int | 0 | Culling mode of the band mesh (passed through; the accepted numbers are not checked). | [L234](../../src/freelancer/Client/cli_asteroid.cpp#L234) |
| `ambient_intensity` | float | 1.0 | Ambient lighting multiplier. | [L238](../../src/freelancer/Client/cli_asteroid.cpp#L238) |
| `zone` (`[ExclusionBand]` only) | str (whole value) | field zone | Zone nickname of the exclusion zone this band surrounds; repeatable, the last valid one wins. An unknown zone logs "Exband (exclusion zone band) linked to missing Zone (%s)". | [L961](../../src/freelancer/Client/cli_asteroid.cpp#L961) |

### 3.4 `[AsteroidBillboards]`

Distant billboard rocks ([BillboardParseIni, L427](../../src/freelancer/Client/cli_asteroid.cpp#L427)). Read once; a later section re-parses into the same block.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `shape` | str (whole value) | none | Shape name in the field's shape library. | [L437](../../src/freelancer/Client/cli_asteroid.cpp#L437) |
| `count` | float, truncated to int | 100 | Number of billboard rocks. The field may raise it to the exclusion manager's maximum. | [L443](../../src/freelancer/Client/cli_asteroid.cpp#L443) |
| `size` | float x2 | 500 / 100 | Minimum and maximum rock size. The two parameters are swapped when the second is smaller, so order does not matter. | [L447](../../src/freelancer/Client/cli_asteroid.cpp#L447) |
| `start_dist` | float | 1500 | Distance at which billboards start. | [L460](../../src/freelancer/Client/cli_asteroid.cpp#L460) |
| `fade_dist_percent` | float | 0.3 | Fraction of the distance used for fading; clamped to `0..1`. | [L464](../../src/freelancer/Client/cli_asteroid.cpp#L464) |
| `ambient_intensity` | float | 1.0 | Ambient lighting multiplier. | [L473](../../src/freelancer/Client/cli_asteroid.cpp#L473) |
| `color_shift` | float x3 | `1, 1, 1` | Per-channel multiplier of the tint. | [L477](../../src/freelancer/Client/cli_asteroid.cpp#L477) |

### 3.5 `[DynamicAsteroids]`

Moving asteroids that are spawned around the camera ([CliDynamicAsteroidParseIni, cli_asteroid_dynamic.cpp:420](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L420)). Each section creates one manager. The tint of the enclosing field is applied to it after parsing.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `asteroid` | str[0] | none | Nickname of a `[DynamicAsteroid]` archetype ([archetypes-and-equipment.md](archetypes-and-equipment.md)). A miss logs the warning "dynamic asteroid archetype (%s) not found" and nothing is spawned. | [L427](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L427) |
| `count` | float, truncated to int | 10 | Number of simultaneously live asteroids; scaled by the asteroid detail setting (`count * scale + 0.5`, scale between 0.5 and 1) before use. | [L449](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L449) |
| `placement_radius` | float | 150 | Radius around the camera in which asteroids are placed; an inner radius of 70% of it is derived. | [L454](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L454) |
| `max_velocity` | float | 50 | Upper bound of the random linear speed. | [L461](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L461) |
| `max_angular_velocity` | float | 5 | Upper bound of the random spin. | [L465](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L465) |
| `color_shift` | float x3 | `1, 1, 1` | Per-channel multiplier of the tint. | [L469](../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L469) |
| `zone` | str (whole value) | field zone | Zone nickname of this system in which the asteroids live; read from the same section by the field loader, the last valid one wins and a miss logs "DynamicAsteroids, linked to missing Zone (%s)". Without a valid `zone` the field's own zone is used. | [cli_asteroid.cpp:1025](../../src/freelancer/Client/cli_asteroid.cpp#L1025) |

Example (synthetic):

```ini
[TexturePanels]
file = solar\asteroids\demo_shapes.ini

[Field]
cube_size = 400
fill_dist = 2000
tint_field = 200, 190, 180

[AsteroidBillboards]
shape = demo_rock
count = 150
size = 300, 800
```

## 4. Nebula files (`[Nebula] file =`)

Loaded by `FNebulaEffect::Load(file, zone)` ([cli_nebula.cpp:1829](../../src/freelancer/Client/cli_nebula.cpp#L1829)); the file is resolved against the data path and an unreadable file logs "unable to open nebula file %s" (the nebula is still created with defaults). Sections are processed in file order, every occurrence of a section is applied, and the shape names used by `[Exterior]`/`[Clouds]` are looked up **while the section is read** in the nebula's own shape library, so `[TexturePanels]` must come first. Colour values are 0..255 floats divided by 255 unless the table says otherwise. Unknown sections are skipped; `[ObjectFog]` is recognised but its keys are never read. Integer-like switches are read as floats and truncated; any non-zero value turns them on.

| Section | Purpose |
|---|---|
| `[TexturePanels]` | Shape libraries for the nebula. |
| `[Exterior]` | The puffy "bits" that form the outside of the cloud. |
| `[Clouds]` | One cloud layer of puff sprites inside the nebula (repeatable; each creates a layer). |
| `[Fog]` | Fog parameters inside the nebula. |
| `[Lightning]`, `[BackgroundLightning]`, `[DynamicLightning]` | Three independent lightning effects. |
| `[NebulaLight]` | Ambient light and sun burn-through. |
| `[Exclusion Zones]` | Zones in which the nebula is thinned out. |

### 4.1 `[TexturePanels]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `file` | str (whole value) | - | Shape-library INI (`GetFullFilename`). Repeatable. | [L1913](../../src/freelancer/Client/cli_nebula.cpp#L1913) |

### 4.2 `[Exterior]`

[ParseExteriorSection, L1220](../../src/freelancer/Client/cli_nebula.cpp#L1220). Defaults are those set when the nebula is created (`FNebulaEffect::FNebulaEffect`, `Load`).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `bit_radius` | float | unset | Radius of one exterior bit. | [L1225](../../src/freelancer/Client/cli_nebula.cpp#L1225) |
| `color` | float x3 (/255) | unset | Tint of the bits. | [L1229](../../src/freelancer/Client/cli_nebula.cpp#L1229) |
| `shape` | str (whole value) | - | Adds one shape (shape-library name). At most eight are kept; further `shape` keys are ignored. A missing shape yields a null entry and the library's warning. | [L1234](../../src/freelancer/Client/cli_nebula.cpp#L1234) |
| `shape_weights` | float, one parameter per shape read so far | all 1.0 | Relative selection weight of each shape; must come after the `shape` keys. | [L1247](../../src/freelancer/Client/cli_nebula.cpp#L1247) |
| `fill_shape` | str (whole value) | none | Shape used to fill the interior. | [L1253](../../src/freelancer/Client/cli_nebula.cpp#L1253) |
| `plane_slices` | float, truncated to int | 1 | Number of slices. | [L1259](../../src/freelancer/Client/cli_nebula.cpp#L1259) |
| `min_bits` | float, truncated to int | 4 | Minimum bits. | [L1263](../../src/freelancer/Client/cli_nebula.cpp#L1263) |
| `max_bits` | float, truncated to int | 8 | Maximum bits. | [L1267](../../src/freelancer/Client/cli_nebula.cpp#L1267) |
| `bit_radius_random_variation` | float | 0 | Relative random size variation; clamped to `0..0.9999` (stored as `1-v` and `1+v`). | [L1271](../../src/freelancer/Client/cli_nebula.cpp#L1271) |
| `equator_bias` | float | stored 0 (equivalent to `equator_bias = 1`) | Stored as `1 - value`. Not validated. | [L1285](../../src/freelancer/Client/cli_nebula.cpp#L1285) |
| `move_bit_percent` | float | 0.75 | Fraction of bits that move. Not validated. | [L1291](../../src/freelancer/Client/cli_nebula.cpp#L1291) |
| `opacity` | float | 0.75 | Opacity of the exterior (both stored opacity values). | [L1295](../../src/freelancer/Client/cli_nebula.cpp#L1295) |

### 4.3 `[Clouds]`

[LocalNebulaCloudsParseIni, L916](../../src/freelancer/Client/cli_nebula.cpp#L916). All defaults are re-applied at the start of every `[Clouds]` section. Switch keys (`no_cloud_blend` ... `disable_batcher`) are floats truncated to int, 0 = off.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `puff_count` | float, truncated to int | 0 | Number of puff sprites. | [L972](../../src/freelancer/Client/cli_nebula.cpp#L972) |
| `puff_radius` | float | 50 | Radius of one puff. | [L976](../../src/freelancer/Client/cli_nebula.cpp#L976) |
| `puff_colora`, `puff_colorb` | float x3 (/255) | unset | The two colours puffs are blended between. **Both spellings fill the next free slot in file order**: the first key read becomes colour 1 (and also seeds colour 2), the second becomes colour 2, further ones are ignored. | [L980](../../src/freelancer/Client/cli_nebula.cpp#L980) |
| `puff_shape` | str (whole value) | - | Adds one puff shape, at most eight; later keys ignored. A missing shape logs "Texture Shape (%s) not found for [Clouds] in file (%s)" and a null shape is still stored. | [L1003](../../src/freelancer/Client/cli_nebula.cpp#L1003) |
| `puff_weights` | float, one parameter per shape read so far | all 1.0 | Selection weights; normalised to sum 1 after the section. Must come after the `puff_shape` keys. | [L1026](../../src/freelancer/Client/cli_nebula.cpp#L1026) |
| `puff_opacity` | (value ignored) | - | Recognised but ignored. | [L1031](../../src/freelancer/Client/cli_nebula.cpp#L1031) |
| `puff_max_alpha` | float | 0.75 | Maximum alpha of a puff. | [L1035](../../src/freelancer/Client/cli_nebula.cpp#L1035) |
| `puff_drift` | float | 0 | Drift speed range. | [L1039](../../src/freelancer/Client/cli_nebula.cpp#L1039) |
| `lightning_shape` | str[0..3] (four shape names) | null shapes | Four shapes for the lightning sprite, each up to 31 characters; a miss stores null. | [L1043](../../src/freelancer/Client/cli_nebula.cpp#L1043) |
| `lightning_color` | float x3 (/255) | unset | Colour of the cloud-embedded lightning. | [L1064](../../src/freelancer/Client/cli_nebula.cpp#L1064) |
| `lightning_gap` | float | unset | Seconds between flashes. | [L1068](../../src/freelancer/Client/cli_nebula.cpp#L1068) |
| `lightning_duration` | float | 0 | Duration of a flash. | [L1072](../../src/freelancer/Client/cli_nebula.cpp#L1072) |
| `lightning_size` | float | 1.0 | Size of the lightning. | [L1076](../../src/freelancer/Client/cli_nebula.cpp#L1076) |
| `llight_range` | float | 0 | Range of the light produced by a flash. | [L1080](../../src/freelancer/Client/cli_nebula.cpp#L1080) |
| `llight_color` | float x3 (/255) | unset | Colour of that light. | [L1084](../../src/freelancer/Client/cli_nebula.cpp#L1084) |
| `lightning_intensity` | float | 1.0 | Intensity of the flash. | [L1088](../../src/freelancer/Client/cli_nebula.cpp#L1088) |
| `max_distance` | float | 1500 | Extent of the cloud volume; puffs are placed within a radius of `max_distance/2 + puff_radius` around the layer centre. | [L1092](../../src/freelancer/Client/cli_nebula.cpp#L1092) |
| `near_fade_distance` | float x2 | `50, 100` | Near-fade start and end. If the second is smaller they are swapped and the warning "near_fade_distances are flipped" is logged. | [L1096](../../src/freelancer/Client/cli_nebula.cpp#L1096) |
| `no_cloud_blend` | switch | off | Disable blending of the cloud sprites. | [L1112](../../src/freelancer/Client/cli_nebula.cpp#L1112) |
| `zwrite_clouds` | switch | off | Write the cloud sprites to the depth buffer. | [L1116](../../src/freelancer/Client/cli_nebula.cpp#L1116) |
| `disable_clouds` | switch | off | Do not draw the puffs. | [L1120](../../src/freelancer/Client/cli_nebula.cpp#L1120) |
| `disable_wisps` | switch | not initialised by the loader | Do not draw the wisps. Unlike the other switches no default is assigned, so set it explicitly. | [L1124](../../src/freelancer/Client/cli_nebula.cpp#L1124) |
| `disable_lightning_flash` | switch | off | Do not draw the flash. | [L1128](../../src/freelancer/Client/cli_nebula.cpp#L1128) |
| `disable_lightning_bolts` | switch | off | Do not draw the bolts. | [L1132](../../src/freelancer/Client/cli_nebula.cpp#L1132) |
| `disable_batcher` | switch | off | Draw without the material batcher. | [L1136](../../src/freelancer/Client/cli_nebula.cpp#L1136) |

### 4.4 `[Fog]`

[ParseFogSection, L1306](../../src/freelancer/Client/cli_nebula.cpp#L1306).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `distance` | float | 10000 | Far fog distance. | [L1311](../../src/freelancer/Client/cli_nebula.cpp#L1311) |
| `near` | float | 0 | Near fog distance. | [L1315](../../src/freelancer/Client/cli_nebula.cpp#L1315) |
| `color` | float x3 (/255) | unset | Fog colour; each channel is capped at 1.0. | [L1319](../../src/freelancer/Client/cli_nebula.cpp#L1319) |

### 4.5 `[Lightning]`

Whole-nebula lightning ([LightningLoadIni, L1575](../../src/freelancer/Client/cli_nebula.cpp#L1575)). The loader assigns no defaults: keys that are absent keep whatever the object held. The first flash is scheduled at a random negative countdown scaled by `gap`.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `gap` | float | unset | Seconds between flashes. | [L1583](../../src/freelancer/Client/cli_nebula.cpp#L1583) |
| `color` | float x3 (/255) | unset | Flash colour. | [L1585](../../src/freelancer/Client/cli_nebula.cpp#L1585) |
| `size` | float | unset | Size. | [L1591](../../src/freelancer/Client/cli_nebula.cpp#L1591) |
| `duration` | float | unset | Duration of a flash. | [L1593](../../src/freelancer/Client/cli_nebula.cpp#L1593) |

### 4.6 `[BackgroundLightning]`

[L1332](../../src/freelancer/Client/cli_nebula.cpp#L1332): the flashing of the sky behind the nebula.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `gap` | float | 10 | Seconds between flashes (the negated value is stored alongside). | [L1337](../../src/freelancer/Client/cli_nebula.cpp#L1337) |
| `duration` | float | 0.5 | Flash duration. | [L1344](../../src/freelancer/Client/cli_nebula.cpp#L1344) |
| `color` | float x3 (/255) | `0.25, 0.25, 0.25` | Flash colour. | [L1348](../../src/freelancer/Client/cli_nebula.cpp#L1348) |
| `intensity` | float | 1.0 | Flash intensity. | [L1352](../../src/freelancer/Client/cli_nebula.cpp#L1352) |

### 4.7 `[DynamicLightning]`

[L1360](../../src/freelancer/Client/cli_nebula.cpp#L1360): flashes of the dynamic scene light. When `duration` is above 0 (its default 0.5 is), a white directional game light is created for the nebula after loading.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `gap` | float | 3.0 | Seconds between flashes. | [L1365](../../src/freelancer/Client/cli_nebula.cpp#L1365) |
| `duration` | float | 0.5 | Flash duration. | [L1367](../../src/freelancer/Client/cli_nebula.cpp#L1367) |
| `color` | float x3 (/255) | `1, 1, 1` | Light colour. | [L1369](../../src/freelancer/Client/cli_nebula.cpp#L1369) |
| `ambient_intensity` | float | 0.25 | Ambient contribution. | [L1371](../../src/freelancer/Client/cli_nebula.cpp#L1371) |
| `intensity_increase` | float | 0.75 | Additional intensity at the peak of a flash. | [L1373](../../src/freelancer/Client/cli_nebula.cpp#L1373) |

### 4.8 `[NebulaLight]`

[L1379](../../src/freelancer/Client/cli_nebula.cpp#L1379).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `ambient` | byte x3 | not set (flag off) | Ambient colour inside the nebula (0..255 each, kept as bytes). Giving the key also turns the override on. | [L1384](../../src/freelancer/Client/cli_nebula.cpp#L1384) |
| `sun_burnthrough_intensity` | float | 0.5 | How strongly the sun shows through the nebula. | [L1393](../../src/freelancer/Client/cli_nebula.cpp#L1393) |
| `sun_burnthrough_scaler` | float | 1.75 | Scale of the burn-through. | [L1397](../../src/freelancer/Client/cli_nebula.cpp#L1397) |

### 4.9 `[Exclusion Zones]`

[ParseExclusionZonesSection, L1408](../../src/freelancer/Client/cli_nebula.cpp#L1408). This section header has exactly this spelling (with the space). The keys form a small state machine: `exclusion` adds a zone, and the keys that follow it (`fog_far`, `zone_shell`, then the three shell keys) describe that zone. Those keys are **ignored** if no `exclusion` has been accepted in the current section, and the three shell keys are ignored unless a `zone_shell` has produced a shell.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `exclusion` | str (whole value) | - | Zone nickname of this system. An unknown zone logs "AsteroidField linked to missing Exclusion Zone (%s)" (the text says AsteroidField; it is the nebula reader) and is skipped. | [L1418](../../src/freelancer/Client/cli_nebula.cpp#L1418) |
| `fog_far` | float | unset | Far fog distance inside this exclusion zone. | [L1433](../../src/freelancer/Client/cli_nebula.cpp#L1433) |
| `zone_shell` | str (whole value) | none | Mesh name (model archetype) of the shell drawn around the zone. A shell mesh already loaded for this nebula is reused; a mesh that cannot be loaded leaves no shell. | [L1441](../../src/freelancer/Client/cli_nebula.cpp#L1441) |
| `shell_scalar` | float | 1.0 | Scale of the shell; values below 1.0 are raised to 1.0. | [L1491](../../src/freelancer/Client/cli_nebula.cpp#L1491) |
| `max_alpha` | float | 1.0 | Maximum alpha of the shell; values above 1.0 are lowered to 1.0. | [L1501](../../src/freelancer/Client/cli_nebula.cpp#L1501) |
| `exclusion_tint` | float x3 (/255) | `1, 1, 1` | Tint of the shell; each channel is capped at 1.0. | [L1511](../../src/freelancer/Client/cli_nebula.cpp#L1511) |

Example (synthetic):

```ini
[TexturePanels]
file = solar\nebula\demo_shapes.ini

[Exterior]
shape = puff_a
shape = puff_b
shape_weights = 2, 1
color = 120, 90, 160
opacity = 0.6

[Clouds]
puff_count = 40
puff_shape = puff_a
puff_colora = 140, 100, 180
puff_colorb = 100, 70, 150

[Fog]
distance = 6000
color = 40, 30, 60
```
