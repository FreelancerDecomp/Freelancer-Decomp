# Freelancer DLL inventory

A map of the client's modules: what each retail DLL does, where its port lives and whether it is ported at all. It exists to quickly tell which `src/` directory to go to and which `EXE\` files are only a reference. Module details are in `docs/re/<module>.md` (if present); the per-element status of functions, globals and vtables is in decomp-tracker, and progress figures are deliberately absent here. Calls through a foreign vtable: [cross_dll_protocol.md](cross_dll_protocol.md).

Status: **port** — `src/<module>/` exists and builds into `bin\`; **none** — there is no port. How complete a port is, is in the tracker and in `deviations.md`, not here.

Retail DLLs are never loaded: `bin/DACOM.INI` sets `DllPath = .\` and every `[Libraries]` entry points into `bin\`. The retail `[Libraries]` list is `EXE/DACOM.INI`; `FL_DevOnlyLibs.ini` from there is absent from the repository.

## Engine (the DACOM chain, order as in `[Libraries]`)

| DLL | Role | Sources | Status |
|-----|------|-----------|--------|
| `dacom.dll` | core: `COManager`, the built-in INI parser (`IProfileParser`), CRC registry, `FDUMP`, `LogStream` | `src/dacom` | port |
| `DALib.dll` | `DALib::Startup/Shutdown`, brings up the rest of the engine; in retail pulls in zlib by ordinal | `src/DALib` | port |
| `ReadFile.dll` | `FileSystem` (DOS/UTF/MEM), `SearchPath` | `src/ReadFile` | port |
| `x86math.dll` | `3DMathEngine` | `src/x86math` | port |
| `EngBase.dll` | `Animation`, `Hardpoint`, `Engine`, `ChannelArchetype` | `src/EngBase` | port |
| `system.dll` | `SystemContainer`, loading of `[System]` components | `src/system` | port |
| `RP8.dll` | D3D8 pipeline (`DirectX8`) | `src/RP8` | port |
| `SoundManager.dll` / `SoundStreamer.dll` | SFX (DirectSound/EAX) and streaming audio (ACM); create DirectSound via `CoCreateInstance` | `src/SoundManager`, `src/SoundStreamer` | port |
| `Deformable2.dll` | deformable meshes, skinning, IK | `src/Deformable2` | port |
| `Thorn.dll` | THORN: `.thn` scene scripts, embedded Lua 3.2, curves | `src/Thorn` | port |
| `Shading.dll` | textures, materials, batcher | `src/Shading` | port |
| `RendComp.dll` | `VMesh`, `TriMesh`, `SphereMesh`, `RenderManager` | `src/RendComp` | port |
| `alchemy.dll` | particles / FX | `src/alchemy` | port |
| `ximage.dll` | `BigImage` (large intro/RichDisplay rasters) | `src/ximage` | port |
| `flmaterials.dll` | FL game materials, `FLAppearance` | `src/flmaterials` | port |
| `zlib.dll` | C API (not DACOM, not in `[Libraries]`); retail `DALib` imports it by ordinal | `src/zlib` | port |
| `Common.dll` | shared runtime: `Csys`, INI, networking, hardpoint, physical objects; ~3.9k exports, C++ classes, not factories. `freelancer.exe` links against our `Common.lib` | `src/Common` (+ `src/IVP`, `src/qhull`) | port |
| `luaprofile.dll` | Lua 3.2 VM, the DACOM component `"ILua"`; not loaded in retail, not in `[Libraries]`. `IProfileParser` is answered by the built-in dacom parser | `src/luaprofile` | port |
| `movie.dll` | intro video (`MediaPlayer::*`, WM ASF Reader graph) | `src/movie` | port |
| `imeui.dll` | IME UI (CJK) | `src/imeui` | port |

`IVP` (physics) and `qhull` are part of the `Common` build; the shared cross-module ABI is published in `src/include/`.

## Client, game world, server

| DLL | Role | Sources | Status |
|-----|------|-----------|--------|
| `freelancer.exe` | entry point, menu, space, UI | `src/freelancer` | port |
| `Content.dll` | missions, AI, populators; loaded from `[Initial SP DLLs]` after engine initialisation; lives in `DLLS/BIN/` | `src/Content` | port |
| `rpclocal.dll` | local SP server, a thin wrapper over `Server.dll` (`IServer`) | `src/rpclocal` | port |
| `Server.dll` | server side of the world (SP and dedicated) | `src/Server` | port |
| `FLServer.exe` | dedicated server | `src/FLServer` | port |
| `remoteserver.dll` / `RemoteClient.dll` | MP transport (client and server sides of one `Network\` code base) | `src/RemoteServer`, `src/RemoteClient`, `src/RemoteCommon` | port |

## Resource DLLs

`resources`, `InfoCards`, `MiscText`, `MiscTextInfo2`, `NameResources`, `EquipResources`, `OfferBribeResources`, `serverresources` are purely resource DLLs (`/NOENTRY`: STRINGTABLE, RT_HTML, menus), rebuilt by `rc.exe` from `src/Resources/` into `bin\`; status **port**. More in [resources.md](resources.md).

## Without a port

| DLL | Role | Comment |
|-----|------|-------------|
| `jflp.dll`, `HudShift.dll` | mods: patch code at hardcoded VAs | must **not** be added to `bin/DACOM.INI` — crash; status: none |
| `gundll.dll` | GUN list server (COM) | none |
| `ebueula.dll` | EULA dialog | none; a third-party retail binary, called from `freelancer.exe` as is |
| `unicows.dll` | Layer for Unicode (Win9x) | none, not needed on Win10+ |
| `debuglib.dll` | dev debug overlay | none |

## Client startup order

`freelancer.exe` `WinMain` → `DALib::Startup` (reads `DACOM.INI`, loads `[Libraries]`) → opening `FileSystem` and checking that the components are present (`IHardpoint`, `IRenderer`, `ITextureLibrary`, `IMaterialLibrary`, `IAlchemy`, `IMaterialBatcher`) → RP8/D3D8 initialisation → resource DLLs from `freelancer.ini` → Content modules → connection through `rpclocal`. The places where `freelancer.exe` does the work itself instead of calling the module's port are in [substitutions.md](substitutions.md).
