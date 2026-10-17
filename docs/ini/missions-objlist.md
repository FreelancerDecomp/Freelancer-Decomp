# `[ObjList]` directives (NPC objectives)

`[ObjList]` sections define named lists of AI directives (go here, follow that, dock, ...) that mission ships execute in order. A list is attached to a ship with `init_objectives =` in `[MsnShip]`, `Act_GiveObjList`, or `Act_SpawnShip`/`Act_SpawnShipRel`, and is also what the game writes back into save games as `[MObjective]` when a ship is mid-script.

## File location / how it is found

`[ObjList]` sections live in mission script files (see [missions.md](missions.md#file-location--how-it-is-found)); the same parser also reads `[MObjective]` sections of save games ([missions-saves.md](missions-saves.md)). The section loader is `LoadObjList` ([MissionObjList.cpp:200](../../src/Content/MissionObjList.cpp#L200)); each directive is a key of the section and is parsed positionally by that directive class's `Parse`. Key names are matched case-insensitively in the table order below ([MissionObjList.cpp:166](../../src/Content/MissionObjList.cpp#L166)); an unknown key is skipped silently.

Every directive keeps the **entire source line** (up to 255 characters) so that it can be saved again verbatim.

## Section keys

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `nickname` | 0: list name | id | `CreateID("")` | The id by which `init_objectives`, `Act_GiveObjList` etc. refer to the list. A duplicate id keeps the first list. | [L206](../../src/Content/MissionObjList.cpp#L206) |
| `system` | - | - | - | Recognised and ignored. | [L211](../../src/Content/MissionObjList.cpp#L211) |
| *directive keys* | see below | | | Appended to the list in file order. | [L214](../../src/Content/MissionObjList.cpp#L214) |

## Conventions

* Targets named in a directive (ship names, solar names, labels, `Player`) are resolved **at execution time** in the ship's current system. A name that does not resolve ends the directive immediately (the ship goes on to the next one); there is no error.
* "Kind" values: `goto` (no flags), `goto_no_cruise` (forbid cruise), `goto_cruise` (force cruise). Any other word is treated like `goto`.
* Several numbers that look like they could be floats are read with `get_value_int` (an `atoi`-style parse), so a decimal fraction is **truncated**: `GotoVec`, `Guide`, `GuideShip`, `Follow`, `Trail`, `Dock`, `StayInRange`/`StayOutOfRange` (sphere form). Where a value is read as float it is marked `float`.
* Fields that the op classes leave unnamed are shown as `Field_xx`; their meaning is not documented in the source. Constructor defaults of the underlying op records ([include/common_pub.h](../../src/include/common_pub.h)) are quoted where relevant. The directive Goto op validates `Field_50 >= 0` and `Field_64 >= Field_60`.
* A directive that sets a `priority` (see `SetPriority`) uses it for all following directives; the default priority of a ship is used when none is set.

## Directives

| Key / entry | Values (positional) | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|---|
| `Avoidance` | 0: flag | str | `false` | Switches obstacle avoidance. The compare is `stricmp(value, "true")`: only the word `true` enables it, every other string (including `1`) disables it. Also applied to the ship's formation mates. | [L253](../../src/Content/MissionObjList.cpp#L253) |
| `BreakFormation` | none | - | - | Removes the ship from its formation. | [L1215](../../src/Content/MissionObjList.cpp#L1215) |
| `CancelObjective` | none | - | - | Cancels the ship's current directives (or leaves its formation). | [L1230](../../src/Content/MissionObjList.cpp#L1230) |
| `Delay` | 0: seconds | float | 0.0 | Wait for that many seconds. | [L284](../../src/Content/MissionObjList.cpp#L284) |
| `Idle` | none | - | - | Submits an idle directive (the ship does nothing until it is cancelled). | [L302](../../src/Content/MissionObjList.cpp#L302) |
| `Dock` | 0: dock object nickname<br>1: second nickname or `NONE` (optional)<br>2: object name (optional)<br>3,4: ints (optional with 2) | nick<br>nick<br>str (47)<br>int, int | 0<br>0<br>empty | Dock with the object. Value 1 and later are read only while the previous value is present; `NONE` gives 0. The two numbers go to `Field_20`/`Field_24` of the dock op. If value 2 names an object it must resolve at execution time or the directive is skipped. | [L1277](../../src/Content/MissionObjList.cpp#L1277) |
| `InstantTradelane` | 0: lane ring nickname<br>1: second nickname (optional) | nick<br>nick | 0<br>0 | Puts the ship into a trade lane instantly. | [L1319](../../src/Content/MissionObjList.cpp#L1319) |
| `Launch` | 0: launch object nickname<br>1: int<br>2: berth kind | nick<br>int<br>str | 0<br>0<br>0 | Launch from a dock. Kind (stricmp): `NONE` 0, `BERTH` 1, `PAD` 2, `MOOR_SMALL` 3, `MOOR_MEDIUM` 4, `MOOR_LARGE` 5, `RING` 6, `JUMP_GATE` 7; other words keep 0. If the object is unset (0) or value 1 is -1 the engine picks a free docking spot for the ship. | [L1368](../../src/Content/MissionObjList.cpp#L1368) |
| `Follow` | 0: target name<br>1: distance<br>2,3,4: offset x,y,z (optional)<br>5: int (optional) | str (47)<br>int<br>int<br>int | empty<br>150.0<br>0,0,0<br>400.0 | Follow the target at a distance. A distance of exactly -1 becomes 2500.0. Values 3 and 4 are read whenever value 2 is present. | [L602](../../src/Content/MissionObjList.cpp#L602) |
| `Trail` | 0: target name<br>1: distance | str (47)<br>int | empty | Trail behind the target at the distance. | [L556](../../src/Content/MissionObjList.cpp#L556) |
| `GotoShip` | 0: kind (str)<br>1: target ship name (str, 47)<br>2: range -> `Field_50` (float)<br>3: flag -> `Field_58` (str, `true` only)<br>4: int -> `Field_54` (optional)<br>5: object name (optional, str 47)<br>6,7: ints -> `Field_60`, `Field_64` (with 5)<br>8: float -> `Field_68` (optional, with 5)<br>9: int (optional, with 5) | mixed | op defaults: `Field_50` 500.0, `Field_54` -1.0, `Field_60` 200.0, `Field_64` 500.0 | Fly to a ship. Value 3 is compared with `stricmp(.., "true")`, so only the word `true` sets the flag. Value 5 (when present) names an object that must resolve at execution time. Values 6.. are read only when value 5 is non-empty. | [L379](../../src/Content/MissionObjList.cpp#L379) |
| `GotoSpline` | 0: kind (str)<br>1..12: four 3D control points, x,y,z each (float)<br>13: range -> `Field_50` (float)<br>14: flag -> `Field_58` (bool)<br>15: int -> `Field_54` (optional)<br>16: object name (optional, str 47)<br>17,18: ints -> `Field_60`, `Field_64` (with 16)<br>19: float (optional, with 16)<br>20: int (optional, with 16) | mixed | op defaults as `GotoShip` | Fly along a spline through the control points. Storage order inside the op is values 4-6, 7-9, 1-3, 10-12 (`Field_20..Field_4C`), mirroring retail. | [L430](../../src/Content/MissionObjList.cpp#L430) |
| `GotoVec` | 0: kind (str)<br>1,2,3: x,y,z (**int**)<br>4: range -> `Field_50` (int)<br>5: flag -> `Field_58` (bool)<br>6: int -> `Field_54` (optional)<br>7: object name (optional, str 47)<br>8,9: ints -> `Field_60`, `Field_64` (with 7)<br>10: float (optional, with 7)<br>11: int (optional, with 7) | mixed | op defaults as `GotoShip` | Fly to a point (integer coordinates). If value 7 names an object it is resolved at execution (failure skips the directive). | [L482](../../src/Content/MissionObjList.cpp#L482) |
| `Guide` | 0: target name (str 47)<br>1,2,3: follow offset x,y,z (int)<br>4: kind (str)<br>5,6,7: destination x,y,z (int)<br>8: range (int)<br>9: flag (bool)<br>10: int<br>11,12: ints<br>13: bool (opt.) -> `Field_A0`<br>14: int (opt.) -> `Field_AC`<br>15..18: floats (opt.) -> `Field_A4`, `Field_B0`, `Field_B4`, `Field_A8`<br>19: int (opt.) | mixed | `Field_A0` 1, `Field_AC` 3, `Field_A4` 10.0, `Field_B0` 10.0, `Field_B4` -10.0, `Field_A8` 0.3927 (pi/8) | Follow the named target at the offset while flying to a destination point (a combined follow + goto op). Values 13..18 are each applied only when non-empty. The target must resolve at execution time. | [L658](../../src/Content/MissionObjList.cpp#L658) |
| `GuideShip` | 0: target name (str 47)<br>1,2,3: follow offset (int)<br>4: kind (str)<br>5: destination object name (str 47)<br>6: range (int)<br>7: flag (bool)<br>8: int<br>9,10: ints<br>11: bool (opt.)<br>12: int (opt.)<br>13..16: floats (opt.)<br>17: int (opt.) | mixed | as `Guide` | As `Guide`, but the destination is another object. Both the target and the destination must resolve at execution time (an empty name resolves to nothing) or the directive is skipped. | [L760](../../src/Content/MissionObjList.cpp#L760) |
| `Face` | 0: `OBJECT` or `POINT`<br>`OBJECT`: 1: target name (str 47), 2: bool (opt.)<br>`POINT`: 1,2,3: x,y,z (float), 4: bool (opt.) | str | mode unset | Turn to face an object or a point. A value 0 other than `OBJECT`/`POINT` leaves the mode unset and the rest of the line is ignored. The optional bool sets `Field_44` of the face op. | [L830](../../src/Content/MissionObjList.cpp#L830) |
| `MakeNewFormation` | 0: formation nickname<br>1...: member names or `ADD_FOLLOWERS` | id<br>str (47) | none | Forms this ship and the named ships into the formation from `formations.ini`. `ADD_FOLLOWERS` (case-insensitive) sets a flag; every other word, including `Player`, is a member name. If the formation id is unknown the line is ignored. | [L1105](../../src/Content/MissionObjList.cpp#L1105) |
| `FollowPlayer` | 0: formation nickname<br>1...: ship names | id<br>str (47) | none | Orders each named ship to follow this ship at the offset between its formation slot and the leader's slot (follow distance 1000.0). Up to *slot count + 1* values are read (empty ones skipped); an unknown formation or one without slots makes the line a no-op. | [L1191](../../src/Content/MissionObjList.cpp#L1191) |
| `SetPriority` | 0: priority | str | unchanged | `NORMAL` -> 1, `ALWAYS_EXECUTE` -> 0; other words keep the previous value. | [L916](../../src/Content/MissionObjList.cpp#L916) |
| `SetLifeTime` | 0: seconds | int | 0 | Sets the ship's (and its formation mates') life time. | [L896](../../src/Content/MissionObjList.cpp#L896) |
| `StayInRange` | `x,y,z,radius [,bool]` **or** `name,radius [,bool]` **or** box form | float/int | see text | Keep the ship inside a sphere/box (see below). | [L1021](../../src/Content/MissionObjList.cpp#L1021) |
| `StayOutOfRange` | same as `StayInRange` | | | Keep the ship outside the sphere/box. | [L1055](../../src/Content/MissionObjList.cpp#L1055) |
| `WaitForPlayerManeuver` | 0: behavior name | str (max 31 chars) | empty | The name is stored in the directive's op (unchecked `strcpy` into a 32-byte buffer: longer names overflow; not validated against any list). The step itself finishes when the player's ship is running the formation behavior and is in the same formation as this ship. | [L1443](../../src/Content/MissionObjList.cpp#L1443) |

### Zone directives (`StayInRange` / `StayOutOfRange`)

Shared parser `ParseZone` ([MissionObjList.cpp:974](../../src/Content/MissionObjList.cpp#L974)). Let `i` be the running index, starting at 0.

1. If value 5 is present: **box form**. Values 0..2 are the box minimum (float), values 3..5 the maximum (float); `i` becomes 6.
2. Then, if value 3 is present (which is always true after a box): **sphere form** - values `i`, `i+1`, `i+2` are the centre (ints), `i+3` the radius (int); `i += 4`. Otherwise **object form** - value `i` is an object name (47 chars), value `i+1` the radius (int); `i += 2`.
3. If value `i` is present and not true (`get_value_bool`), the directive's invert mode is set to 2 (otherwise it stays 0 for `StayInRange`, 1 for `StayOutOfRange`).

In practice the useful lines are `StayInRange = x, y, z, radius` and `StayInRange = object_name, radius`. The object form needs the name to resolve at execution time.

### Not readable from the INI

`FObjListTractorBeam` (directive type 0x19) and the type numbers 0x17/0x18 have no INI key; the tractor-beam directive is only created by the engine.
