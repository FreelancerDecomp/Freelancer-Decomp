# Thorn: `.thn` scripts, curves and paths

`Thorn.dll` is a DACOM component that loads scene scripts (`.thn`: menu intros, rooms, cutscenes),
stores the entities and events they describe, and plays them back frame by frame. The script is read by the embedded
Lua 3.2, the result is held by a native C++ scene. Alongside, Thorn provides parametric curves and
spline paths. This page is for whoever works on rooms/intros, `IScriptEngine` or the `.thn` format;
the format itself is described separately in [thorn_thn_format.md](thorn_thn_format.md).

Sources are in `src/Thorn/`; the public contracts are `src/include/thorn_iface.h` and `thorn_controls.h`.
Function statuses and differences from retail are in decomp-tracker and in the Thorn section of
[deviations.md](deviations.md). Rules for foreign interfaces: [cross_dll_protocol.md](cross_dll_protocol.md).

| Part | Files |
|------|-------|
| DACOM factories and engine facets | `Thorn.cpp`, `Thorn.h` |
| Lua/native boundary and descriptors | `ThornLua.cpp`, `lua32/` |
| Entities, events, playback | `ThornScene.h/.cpp` |
| Curves and paths | `ParamCurve.cpp`, `SplinePath.cpp`, `SplineMath.h` |

## Components

Factories are registered when the DLL loads; the **component name** selects the factory, `QueryInterface` selects the interface of an
already created object. A curve's display name does not replace the component name in a script's `CLSID`.

| Registration name | Object |
|-------------------|--------|
| `IScriptEngine` | Scene engine; facets `IGeoTransformControl` (scene coordinate system) and `IScriptControl` (playback) |
| `CatmullRomPCurve`, `ThornParamCurve`, `LinearPCurve`, `ThornLPCurve`, `SmoothPCurve`, `StepPCurve`, `RampUpPCurve`, `RampDownPCurve`, `BumpOutPCurve`, `BumpInPCurve`, `FreeFormPCurve` | Parametric curve, `IParamCurve` |
| `CV_CRSplinePath`, `CV_CROrientationSplinePath` | A single path object: `ICRSpline`, `IMotionPath`, `IPersistString` |

`LinearPCurve` is registered twice (the plain linear evaluator and a Hermite-based variant); the selection order
belongs to the DACOM registry, and the second factory is not dead. The engine and spline factories support a controlling
outer and a separate inner unknown; the curve factories do not use the descriptor's aggregation fields. The internal
descriptor `FThornAggDesc14` (`Thorn.h`) is not the same as the descriptor with an IID: the two must not be
swapped just because their sizes coincide.

Who uses it: `Common` creates `IScriptEngine` through DACOM and hands it the file system; `RoomData`
(`src/Common/common_roomdata.cpp`) pulls out descriptors and properties to build a room and releases the
engine. The client script object (`src/freelancer/Client/gf_script_object.cpp`, `gf_script_spawn.cpp`)
holds the engine, creates external objects from descriptors, binds them to entities and passes the time.

## Lua 3.2 and loading

`src/Thorn/lua32/` contains Lua 3.2.2 (origin and local patches in `ORIGIN.txt`, licence in
`COPYRIGHT`); `_VERSION` is `"Lua 3.2"`, the bytecode format version is `0x32`. Replacing it with Lua 4/5 would require
changing both the API and the format. The base library, math and I/O are exposed; the string library as a whole is not exposed
(it is part of the build only for the I/O helper functions). The text parser is the same file as in
`luaprofile` (`lparser_stub.c` includes `luaprofile/lua32/src/lparser.c`), but the VM and state in Thorn
are its own: Thorn does not use `ILua` from luaprofile. `IProfileParser` (INI) does not take part in `.thn` either.

The Lua / C++ boundary:

- `.thn` is read through `IFileSystem` and executed as a Lua chunk (first byte ESC means bytecode, otherwise text).
  The chunk must produce an `entities` table; `events` may be absent; the duration is taken from
  `duration`. After execution the loader walks `entities`, then `events` through `foreach`, creates
  native entities and finds their handles by the names in the events. Lua stores only the description of the input; the scene,
  the event descriptors and control of external objects belong to C++.
- Playback is performed by native events and controllers; an ordinary step does not re-execute the Lua chunk.
  Saving a scene writes Lua text from the native state (bytecode is not compiled).
- **One shared VM per module**, not one per file or per `IScriptEngine`. The Lua 3.2 API works through a
  globally selected `lua_state`; the loader object selects the VM at creation and restores the previous
  value on destruction. The current engine, COM context, scene, the handle of the entity being processed and the
  name→handles table are module state, borrowed without `AddRef`/`Release`. Loading is therefore not a
  transaction: a nested load from a tag method overwrites these values, and simultaneous work by several
  threads on one VM is not supported.
- `lua_Object` is a C API handle, not a pointer; temporary handles live between `lua_beginblock` /
  `lua_endblock`. A string from Lua belongs to the VM; a long-lived copy must be made. Reading a table may
  invoke a tag method and the GC. VM errors go through `setjmp`/`longjmp`; C++ destructors are not
  called on such a jump.
- Time: in `.thn` it is seconds, inside the scene and the schedule it is integer milliseconds. Names from Lua are borrowed for the duration
  of the factory call; the factory copies them.

## Scene, events, ownership

`FScriptEngine` owns `FThornScene`: the entity registry and the master collection of events. Pending and active
events hold borrowed references, and separate time nodes own the stop times; the order of active events accounts for
dependencies between entities. A callback may modify the scene during traversal, so the selected node, its
payload and the saved receiver have different lifetimes — looking up by key again after a callback does not
replace reading the selected node. Stop and restart preserve the loaded scene, a full clear
destroys events and entities; pause, reset and loop are separate state transitions.

The concrete descriptor owner type (`DescriptorOwnerType`) remains whatever was set at creation, even
if the public field `Snap.Type` changes when a descriptor is applied. Descriptor packets are initialised before
the Lua fields are parsed (the contents of uninitialised retail heap are not part of the contract). The USER field is passed to the
context without interpretation. PATH_MOTION publishes a live `FThornPathEventDesc`; the loader, playback
and serializer work with a single owner of these fields.

### Entity binding

Binding connects an entity created by the script to a DACOM object supplied by the client.
`FThornScene::Attach` finds the entity and requests the needed controls (`thorn_controls.h`) through
`QueryInterface`; events read the stored QI references. The raw `EngineObj` does not replace them and does not get `AddRef`
merely because of `Attach`. The set of controls is chosen by `DescriptorOwnerType`. A repeated bind releases the
previous reference immediately before the QI; the references belong to the entity; a bind may be partial (one
control present, the others absent).

- `ATTACH_ENTITY` changes the link and relative transform of already existing entities;
  `CONNECT_HARDPOINTS` passes the names of two hardpoints through the compound control. These are different contracts
  (`parent_hardpoint` belongs to CONNECT).
- `Detach` performs the specific unbind; `Attach(null)` does not replace it. Unbind and entity deletion are different
  cleanup sequences (unbinding CAMERA/MONITOR removes the mutual monitor links, deletion separately
  owns the local nodes); the destructor is not a repeated `Detach`.
- The scene coordinate system is a live owner to which entities and events hold borrowed references;
  it differs from the geo control of an external object. The geo and script facets of `FScriptEngine` share
  one identity but are not interchangeable.

## Curves and paths

`FParamCurve` (`ParamCurve.cpp`) unifies the curve families and selects the evaluator and rebuild policy
through `FCurveSpec`: Catmull-Rom and linear interpolation have their own evaluators; Smooth, Step, Ramp, Bump, FreeForm
use Hermite data; `ThornParamCurve` and `ThornLPCurve` change the rebuild policy, not the polynomial. Curve data
lives in three different places: a map by signed ID (`FParamKeyStorage`), a key list for traversal and
sorting (`EvaluationKeys_`), and derived Hermite knots (`Knots_`); they must not be collapsed into one array
without checking the order of publication and deletion. The wrapping period is set by the calling event, not by the curve. The public
result is a `float`; inside the port the value between evaluation and consumer is passed as an extended one
(`FThornExtendedProgress`) — this is an internal contract of the port, not the layout of a retail object.

`FSplinePath` (`SplinePath.cpp`) stores control points separately from ghost points, lookup tables and
orientation data; changing the points or the OPEN/CLOSED mode requires a consistent rebuild. `IMotionPath`
reads the path, `IPersistString` loads and saves its string form — both are facets of the same owner.
The MOTION_PATH entity separately stores the saved persist descriptor and the runtime `IMotionPath` reference: they
must not be merged, they have different QI paths and different lifetimes.

## Known limitations

- The state of each function (verified / partial / simplified) and the exact arithmetic terms are in the
  decomp-tracker cards (`thorn.dll`).
- The shipped game `.thn` files (1423 files in `DATA/SCRIPTS`) are bytecode (`ESC Lua 0x32`); the text path of the
  parser is checked by saving a scene and loading it again.
