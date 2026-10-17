# Freelancer Decomp — documentation

**English** · [Русский](ru/README.md)

Freelancer Decomp is a from-source reimplementation of the **Freelancer** game client
(Digital Anvil / Microsoft, 2003). Every module of the original client is rebuilt as C++ source
that is checked against the retail disassembly. The goal is a working client whose behaviour
matches the original, with documented internals that can be modified. See [MISSION.md](MISSION.md).

The project contains **no game assets and no retail binaries**. To build and run it you need
your own installation of Freelancer (see [Local layout](#local-layout)).

## Where to start

| You want to… | Read |
|--------------|------|
| Know what is configurable, and where each INI file is read | [ini/README.md](ini/README.md) — reference of every INI file, section and key the code reads |
| Understand how the modules fit together | [re/dll_inventory.md](re/dll_inventory.md) — one table of every DLL, its role and its source directory |
| Call into another module / find out what an interface slot is | [re/cross_dll_protocol.md](re/cross_dll_protocol.md) — the method |
| See what the port does differently from retail | [re/deviations.md](re/deviations.md) — the ledger of known divergences; [re/substitutions.md](re/substitutions.md) explains the concept of "substitution" |
| Work on one subsystem | the module documents below |
| Check the status of a single function | the decomp-tracker (MCP server), not markdown — markdown holds only what does not fit a per-function record |

## Module documents (`re/`)

| Area | Documents |
|------|-----------|
| Engine core | [dacom](re/dacom.md) (object manager, built-in INI parser) · [dalib](re/dalib.md) · [readfile](re/readfile.md) (file system) · [x86math](re/x86math.md) · [system](re/system.md) · [engbase](re/engbase.md) · [zlib](re/zlib.md) · [winmain](re/winmain.md) · [resources](re/resources.md) · [movie](re/movie.md) · [imeui](re/imeui.md) · [luaprofile](re/luaprofile.md) |
| Rendering | [render](re/render.md) (pipeline, RendComp, ximage) · [shading](re/shading.md) (textures, materials, FLAppearance) · [alchemy](re/alchemy.md) (particles / FX) · [deformable2](re/deformable2.md) · [rp9](re/rp9.md) (port-only Direct3D 9 renderer, volumetric nebula; `FreelancerRp9`) |
| Audio | [audio](re/audio.md) |
| Scripting | [thorn](re/thorn.md) (scene scripts, Lua 3.2) · [thorn_thn_format](re/thorn_thn_format.md) (`.thn` file format) |
| Game data and logic | [common](re/common.md) · [common_archetype](re/common_archetype.md) · [common_physys](re/common_physys.md) · [content](re/content.md) · [mission script format](re/content_mission_script.md) · [mission opcodes](re/content_mission_opcodes.md) |
| Client executable | [freelancer](re/freelancer.md) · [freelancer_cliobj](re/freelancer_cliobj.md) · [equip_fx_factory](re/equip_fx_factory.md) · [dealer_equip_stats](re/dealer_equip_stats.md) · [options_screen](re/options_screen.md) · [hd_edition](hd_edition.md) (widescreen) · [walk_mode](walk_mode.md) (port-only walking in bases; `FreelancerWalkMode`) |
| Server / network | [server](re/server.md) (Server.dll, FLServer, SP connection, remote DLLs) · [rpclocal](re/rpclocal.md) (local RPC packet protocol) |
| Build and CRT | [crt_compat](re/crt_compat.md) (modern CRT/STL vs. the retail VC6 runtime) · [refactoring](re/refactoring.md) (code rules) |

## Local layout

```
Freelancer\
  src\        Visual Studio solution (Freelancer.sln) — all project source code
  bin\        Release output + the dev *.ini files (source of truth for the INI files)
  bin_debug\  Debug output       binasan\  DebugASan output
  EXE\        retail binaries and Ghidra listings (not in git; reference only, never loaded)
  DATA\       retail game data (not in git)
  DATA_EXTRA\ the port's own data next to DATA (RP9 shaders, volumetric.ini; in git)
  docs\       this documentation
```

`EXE\` and `DATA\` come from your own Freelancer 1.1 installation. No retail DLL is loaded at
runtime: `bin\DACOM.INI` points every `[Libraries]` entry into `bin\`.

## Building

Requires Visual Studio 2022 (MSVC v143, Win32 target) and the DirectX 8 SDK headers (`dx8\`).

```
MSBuild.exe src\Freelancer.sln -t:Build -p:Configuration=Debug -v:minimal
```

| Configuration | Output directory |
|---------------|------------------|
| Debug | `bin_debug\` |
| Release | `bin\` |
| DebugASan | `binasan\` (AddressSanitizer; copies the `clang_rt.asan_*` runtime next to the exe) |

Run `freelancer.exe` from the directory of the configuration you built. Debug and ASan builds
copy the dev `*.ini` from `bin\` with paths rewritten (`src/copy_dev_ini.py`) — edit the files in
`bin\`, not the copies. The retail DLLs in `EXE\` are not ASan-instrumented.

Hunting memory leaks: [memory_diagnostics.md](memory_diagnostics.md).
