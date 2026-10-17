# `.thn` scene schema

`.thn` is an executable Lua 3.2 chunk that produces the globals `duration`, `entities` and `events`. The input can be source text or Lua bytecode; saving a scene produces Lua text. VM integration, the shared load context, native objects and factories are described in [thorn.md](thorn.md).

## Binary header

| Offset | Contents |
|---|---|
| 0 | `0x1B` — ESC |
| 1–3 | ASCII `Lua` |
| 4 | version `0x32` |
| 5 | number representation: `0` — text, `8` — native `double` |
| 6–13, only when the size is 8 | check `double`, then the chunk body |

For the native variant the check number is `3.14159265358979323846e8`; the check compares its integer part with `314159265` rather than requiring a byte-for-byte `double` match. The PC variant uses little endian. When the size is zero there is no check `double`. The Lua 4/5 format is not accepted by this loader.

## Root and time

```lua
duration = 5.0
entities = {
    { entity_name = "camera", type = CAMERA,
      spatialprops = { pos = {0, 0, 0},
        orient = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}} },
      cameraprops = { fovh = 60, hvaspect = 1.333333,
        nearplane = 0.1, farplane = 100000 } }
}
events = {
    { 0.0, SET_CAMERA, {"camera"} }
}
```

| Field | Unit in the input Lua |
|---|---|
| root `duration` | seconds; the native duration is whole milliseconds |
| first element of an event | seconds; the native start time is whole milliseconds |
| `duration` in event properties | seconds; the native duration is whole milliseconds |
| `start_time` of a SCENE/SUB_SCENE entity | seconds; the native value is milliseconds |
| `pcurve_period`, IDs, types and flags | numbers, not multiplied by 1000 |

`start_time` inside an event is a type-specific field. It is not the first element of the event and has no time conversion common to all types. `events` may be absent; `entities` must be a table. Traversal uses `foreach`, so table keys are not native entity handles or event IDs.

## Entities

| Type ID | Name |
|---|---|
| 0 | `UNKNOWN_ENTITY` |
| 1 | `COMPOUND` |
| 2 | `DEFORMABLE` |
| 3 | `CAMERA` |
| 4 | `MONITOR` |
| 5 | `LIGHT` |
| 6 | `SOUND` |
| 7 | `MARKER` |
| 8 | `HARDPOINT` |
| 9 | `SCENE` |
| 10 | `SUB_SCENE` |
| 11 | `MOTION_PATH` |
| 12 | `DELETED` |
| 13 | `PSYS` |

In the shared Lua namespace the name `HARDPOINT` ends up with the value `1` for `target_type` at the end of registration. A hardpoint entity needs the numeric `type = 8`; the constant of the same name does not equal this ID. A `DELETED` entity is not created on load.

| Common field | Form |
|---|---|
| `entity_name` | name that events refer to |
| `type` | numeric ID from the table above |
| `template_name` | string; the literal `"NULL"` is converted to an empty string |
| `flags` | numeric bit mask |
| `lt_grp`, `srt_grp`, `usr_flg` | numeric values stored in byte fields |
| `spatialprops` | position and orientation table |
| `userprops` | table of user string properties; a numeric value may be converted to a string |

`pos = {x, y, z}`. `orient = {{c0x, c0y, c0z}, {c1x, c1y, c1z}, {c2x, c2y, c2z}}` gives the **three columns** of the matrix. Loading an entity's spatial properties reads `pos` and `orient`; `q_orient` and `axisrot` belong to the spatial event.

| Type | Own properties |
|---|---|
| COMPOUND, DEFORMABLE | `compoundprops = { floor_height = number }` |
| CAMERA, MONITOR | `cameraprops = { fovh, hvaspect, nearplane, farplane }` |
| LIGHT | `lightprops` |
| SOUND | `audioprops`; `archetype` inside it may supply `template_name` |
| SCENE, SUB_SCENE | `start_time`, `ambient = {r,g,b}`, `up`, `front` and flat fog fields |
| MOTION_PATH | `pathprops = { path_type, path_data, control_pts }` |
| PSYS | `psysprops = { sparam = number }` |

Defaults for CAMERA/MONITOR: `fovh = 60`, `hvaspect = 4/3`, `nearplane = 0.1`, `farplane = 100000`. The SCENE/SUB_SCENE fog fields lie directly in the entity table, not in nested `fogprops` or `reverbprops`.

## Property tables

| `lightprops` | Value |
|---|---|
| `on`, `infinite` | `Y` / `N` |
| `color` | `{r,g,b}`, integer components |
| `diffuse`, `specular`, `ambient`, `direction`, `atten` | three numeric components |
| `range` | number |
| `cutoff`, `theta` | degrees; the native light stores radians |
| `type` | `L_POINT = 1`, `L_SPOT = 2`, `L_DIRECT = 3` |

`audioprops` contains `attenuation`, `pan`, `dmin`, `dmax`, `ain`, `aout`, `atout`, `rmix`. `ain` and `aout` are integers; the other parameters are numeric. The additional `archetype` is used when loading SOUND.

| Fog field | Value |
|---|---|
| `fogon`, `fogtable` | `Y` / `N` |
| `fogcolor` | `{r,g,b}` |
| `fogmode` | `F_NONE = 0`, `F_EXP = 1`, `F_EXP2 = 2`, `F_LINEAR = 3` |
| `fogstart`, `fogend`, `fogdensity` | numbers |

`reverbprops` contains an integer `env` and numeric `vol`, `decay`, `damping`. `psysprops` contains a numeric `sparam`. In a property-animation event the presence of a field sets the corresponding change mask; the table need not contain all parameters.

## Events

An event has the form `{ start, type, {"primary", "secondary"}, properties }`. The start and type are at indices 1 and 2; the name table is at index 3, the optional properties table at index 4. Names are resolved after the entities are created.

Common properties: `duration`, `flags`, `event_flags`, `param_curve`, `pcurve_period`. A missing `duration` inside the properties table sets `USE_SCRIPT_DURATION`; an explicitly given `flags` then replaces this mask.

| ID | Type name | Own property keys |
|---|---|---|
| 0 | `UNDEFINED_EVENT` | the event is not created |
| 1 | `USER_EVENT` | `user_event_string` |
| 2 | `SET_CAMERA` | no own keys |
| 3 | `START_SOUND` | `start_time` |
| 4 | `START_LIGHT_PROP_ANIM` | `lightprops` |
| 5 | `START_CAMERA_PROP_ANIM` | `cameraprops = { fovh, aspect, near, far }` |
| 6 | `START_PATH_ANIMATION` | `start_percent`/`percent1`, `stop_percent`/`percent2`, `offset`, `up`, `front` |
| 7 | `START_SPATIAL_PROP_ANIM` | `spatialprops`, `target_part`, `target_type` |
| 8 | `ATTACH_ENTITY` | `offset`, `up`, `front`, `target_part`, `target_type` |
| 9 | `CONNECT_HARDPOINTS` | `hardpoint`, `parent_hardpoint` |
| 10 | `START_MOTION` | `animation`, `locked_bone`, `trans_time`, `time_scale`, `weight`, `loop`, `heading`, `trans_scale`, `start_time` |
| 11 | `START_IK` | `end_effector`, `count_to_root`, `ik_id`, `target_part`, `target_type`, `damping`, `point_at`, `move_to`, `transition_duration`, `up`, `front` |
| 12 | `START_SUB_SCENE` | `start_time` |
| 13 | `START_PSYS` | no own keys |
| 14 | `START_PSYS_PROP_ANIM` | `psysprops` |
| 15 | `START_AUDIO_PROP_ANIM` | `audioprops` |
| 16 | `START_FOG_PROP_ANIM` | `fogprops` |
| 17 | `START_REVERB_PROP_ANIM` | `reverbprops` |
| 18 | `START_FLR_HEIGHT_ANIM` | `floor_height`, `target_part`, `target_type` directly in the event properties |
| 19 | `SUBTITLE` | `strid` |

Before creation, an event of type 6 whose primary entity has type 1–8 is turned into `ATTACH_ENTITY`. The generic `START = 23` becomes `START_SOUND` for SOUND and `START_MOTION` for the other types. `PROPERTY_ANIM = 0` does not denote this generic START.

In the event's `spatialprops`, `pos`, `axisrot = {angle, axis}`, `q_orient = {x,y,z,w}` and a three-column `orient` are allowed. When `q_orient` is present the matrix `orient` is not read. The targets use `ROOT = 0`, `HARDPOINT = 1`, `PART = 2`.

## Axes and flags

Axes: `X_AXIS = 0`, `Y_AXIS = 1`, `Z_AXIS = 2`, `NEG_X_AXIS = 3`, `NEG_Y_AXIS = 4`, `NEG_Z_AXIS = 5`. The values `Y = 1`, `N = 0` are numeric constants.

| Constant | Value | Context |
|---|---|---|
| `REFERENCE`, `HIDDEN` | 1, 16 | entities |
| `LIT_AMBIENT`, `LIT_DYNAMIC` | 2, 4 | COMPOUND, DEFORMABLE, PSYS |
| `FOG_PROPS_REMOVED` | 32 | SCENE |
| `SPATIAL`, `STREAM`, `LOOP` | 2, 4, 8 | SOUND and START_SOUND |
| `USE_SCRIPT_DURATION` | 1 | events |
| `POSITION`, `PATH_POSITION`, `ORIENTATION`, `LOOK_AT` | 2, 2, 4, 8 | spatial/path/attach events |
| `ENTITY_RELATIVE`, `PARENT_CHILD`, `ORIENTATION_RELATIVE` | 32, 64, 128 | path/attach |

Other registered values: `ADD_PATH = 6`, `LOOKAT_ENTITY = 8`, `STOP = 21`, `STOP_IK = 22`. The meaning of a mask value depends on the context; two names sharing a number does not make them interchangeable.

## Curves and paths

```lua
param_curve = {
    CLSID = "CatmullRomPCurve",
    points = { {a, b, c, d}, {a2, b2, c2, d2} }
}
```

Each point contains four numbers. The format does not name their components, so the last two must not be renamed to presumed tangents. `CLSID` selects the DACOM factory by name, not the curve's display name.

Curve factory names: `CatmullRomPCurve`, `ThornParamCurve`, `LinearPCurve`, `ThornLPCurve`, `SmoothPCurve`, `StepPCurve`, `RampUpPCurve`, `RampDownPCurve`, `BumpOutPCurve`, `BumpInPCurve`, `FreeFormPCurve`.

For MOTION_PATH an explicitly given `path_type` selects the factory; `path_data` is loaded through its `IPersistString`. Without `path_type`, `CV_CRSplinePath` is selected and `control_pts = {{x,y,z}, ...}` are passed to the spline interface. These two load branches are not merged. For an orientation path the factory `CV_CROrientationSplinePath` exists; the `path_data` format is determined by the selected object.

## Load and save asymmetries

- `SET_CAMERA` is saved without the fourth properties table.
- Loading reads `cameraprops` for CAMERA and MONITOR, saving writes them only for CAMERA. The spatial block of MONITOR and HARDPOINT is skipped on save.
- `compoundprops` is read for COMPOUND and DEFORMABLE, but saved only for DEFORMABLE.
- An entity camera uses `hvaspect`/`nearplane`/`farplane`; a camera event uses `aspect`/`near`/`far`.
- `percent1`/`percent2` are aliases on load; saving writes `start_percent`/`stop_percent`.
- `ik_id` is read when loading IK but is not written to the scene text.
- `userprops` is saved from the same native string collection that the property read and write operations use.
- Diagnostic long event names, for example `START_LIGHT_PROPERTY_ANIM`, are not registered as Lua globals in place of the short names from the table above.

The schema reader is in [ThornLua.cpp](../../src/Thorn/ThornLua.cpp), the text writer in [ThornScene.cpp](../../src/Thorn/ThornScene.cpp). The tables here describe the data format; they are not a report on the completeness of its handlers.
