# Freelancer Decomp

A reverse-engineered reimplementation of the **Freelancer** (2003) game client and its engine
DLLs, built from source with a modern Visual Studio. Every module the game loads is built from
`src/`; no retail binary of the game is loaded except the two third-party DLLs listed below.

You need your own copy of Freelancer: the game data (`DATA`) is not part of this repository.

Russian: [README.ru.md](README.ru.md). Documentation: [docs/README.md](docs/README.md).

## Building and running

1. **Clone** this repository.
2. **Add the game data:** copy the `DATA` folder of an installed Freelancer (or of a mod) into
   the repository root, next to `src`, so that `DATA\` and `DATA_EXTRA\` sit side by side.
3. **Add the DirectX 8.1 SDK:** download the
   [DirectX 8.1 SDK (Final)](https://archive.org/details/dx81sdk_full), install or extract it, and
   copy its `include` and `lib` folders into a `dx8` folder at the repository root (so that
   `dx8\include\d3d8.h` and `dx8\lib\d3d8.lib` exist). Only the headers and libraries are used;
   another location can be given with `/p:Dx8SdkDir=<path>`.
4. **Build the Release configuration** of `src\Freelancer.sln` (`Release|Win32`) in Visual Studio,
   or from a Developer Command Prompt:

   ```
   msbuild src\Freelancer.sln -t:Build -p:Configuration=Release -p:Platform=Win32
   ```

The output goes to `bin\`, which already holds the runtime configuration (`DACOM.INI`,
`freelancer.ini`, `FLConfigDatabase.txt`, `NewPlayer.fl`, `MPNewCharacter.fl`). Run
`bin\freelancer.exe` **from the `bin` directory** (the library and data paths are relative to it).

### Requirements

- Visual Studio 2022 (platform toolset v143) with the *Desktop development with C++* workload,
  the *C++ MFC for latest v143 build tools (x86 & x64)* component (used by `FLServer`) and a
  Windows 10/11 SDK. Everything builds as 32-bit (`Win32`).
- Debug builds (`bin_debug\`) also need Python 3 on `PATH`: a post-build step copies the runtime
  configuration from `bin\`.

### Optional features

Port-only features are build switches in `src/Freelancer.Diagnostics.props` (also on the VS
property page *Freelancer diagnostics*), or `/p:<Name>=0|1` on the MSBuild command line:

| Property | Default | What it does |
|---|---|---|
| `FreelancerWidescreenSupport` | 1 | Widescreen (Hor+) display modes and HUD layout |
| `FreelancerWalkMode` | 1 | Walking through base rooms as Trent ([docs/walk_mode.md](docs/walk_mode.md)) |
| `FreelancerRp9` | 0 | `RP9.dll`, a Direct3D 9 renderer with volumetric nebulae ([docs/re/rp9.md](docs/re/rp9.md)); needs the [DirectX SDK (June 2010)](https://www.microsoft.com/en-us/download/details.aspx?id=6812) and `DirectX9` in `bin\DACOM.INI` |
| `FreelancerBetaLabel` | 0 | The "Beta" label in the corner of the screen |
| `FreelancerEnableTickProfile` | 0 | On-screen frame profiler |

## Repository layout

```
src\          Visual Studio solution (Freelancer.sln) — all source code
bin\          Release output + the runtime configuration files
DATA_EXTRA\   the port's own data (RP9 shaders, volumetric.ini), next to the game's DATA
EBUEula.dll   retail EULA dialog, loaded as ..\EBUEula.dll with ..\EULA.rtf (as the retail game)
EXE\          gundll.dll — the retail GUN client (multiplayer server list), loaded as ..\exe\gundll.dll
docs\         documentation (English; Russian translation in docs\ru)
```

`EBUEula.dll` and `gundll.dll` are the only retail binaries the port uses unchanged: they are
third-party components of the original game and have not been reimplemented.
