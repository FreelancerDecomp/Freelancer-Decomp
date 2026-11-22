# Freelancer Decomp

A reverse-engineered reimplementation of **Freelancer** (Digital Anvil / Microsoft, 2003): the
game client, the server and every engine DLL, rebuilt as C++ source and checked against the
retail disassembly.

The project is heading for:

- **A cross-platform client.** RPGPU, a new renderer on [SDL3](https://www.libsdl.org/) and
  SDL_GPU (Direct3D 12, Vulkan, Metal), replaces the Direct3D 8 renderer outside the 32-bit
  Windows build. Together with the platform layer it takes the client to x64 Windows, Linux and
  macOS.
- **A 64-bit server.** The server modules and `FLDedicated`, a console server with no UI, build
  and run as x64. The server hosts over DPNet, the project's own DirectPlay 8.
- **Retail compatibility.** The ports play with retail clients and servers, and they read the
  retail `DATA`, saves and data-only mods without conversion. The 32-bit Windows build stays the
  reference: its behaviour, packets and structure layouts match retail, and CI checks this on
  every change.

You need your own copy of Freelancer. The game data (`DATA`) is not in this repository.

This is a fork of [FreelancerDecomp/Freelancer-Decomp](https://github.com/FreelancerDecomp/Freelancer-Decomp),
originally written by Giperion.

## Status

| Area | State |
|---|---|
| Win32 client and server | Complete and playable. This build is the retail reference |
| x64 server | Runs: it loads the game data, logs players in and enters bases. CI requires a retail DirectPlay 8 client to join it, and the x64 packet codecs must match Win32 byte for byte |
| x64 client | Builds. It runs on RPGPU, which is being brought up |
| RPGPU (SDL3 + SDL_GPU) | Menus and HUD render. 3D geometry, lighting, fog and the multi-stage texture pipeline are in progress |
| Linux and macOS | The portable modules (DPNet, TexConvert, qhull) build and are tested in CI. A macOS client is the next milestone |

The plans: [docs/x64-client-plan.md](docs/x64-client-plan.md) (x64 client, RPGPU, other
platforms), [docs/x64.md](docs/x64.md) (x64 build) and [docs/scaling.md](docs/scaling.md)
(large servers).

## What the port adds

Beyond the decompiled retail modules, the port adds these modules and tools. None of them
changes the behaviour of a retail module:

| Module | What it is |
|---|---|
| [DPNet](docs/dpnet.md) (`src/DPNet`) | Our own implementation of the DirectPlay 8 network protocol, written from Microsoft's published specifications. It is portable C++20 with no OS headers. An adapter in DALib puts it behind `IDirectPlay8Server`, so the x64 server hosts without Microsoft's deprecated `dpnet.dll`. Its tests reproduce the specification's byte dumps exactly and run a simulated lossy network and 30,000 malformed datagrams, under sanitizers on Linux and Windows |
| DPNetCheck (`src/DPNetCheck`) | Joins a running server through Microsoft's DirectPlay 8 client, as a retail client does. CI runs it against the server on Microsoft's DirectPlay (Win32) and on DPNet (x64), and both must give the same result |
| [RPGPU](docs/x64-client-plan.md#phase-5-rpgpu-the-sdl3--sdl_gpu-renderer) (`src/RPGPU`) | The SDL3 + SDL_GPU renderer. It implements RP8's interfaces and emulates Direct3D 8's fixed-function pipeline in HLSL, compiled offline to DXIL, SPIR-V and MSL |
| TexConvert (`src/TexConvert`) | A texture loader that replaces D3DX 8.1 (DDS with DXT, TGA, BMP, cube maps). CI checks its output against D3DX byte for byte |
| [FLDedicated](docs/dedicated_server.md) (`src/FLDedicated`) | A console dedicated server with no MFC, windows or dialogs, built for Win32 and x64. It shares its host code with `FLServer.exe`, so retail clients see the same server |
| [Platform layer](docs/platform.md) (`src/include/platform.h`) | One interface for files, threads, synchronisation, time and modules. On Windows each call is the same Win32 call retail made. The POSIX backend for macOS and Linux is checked by `src/PortTests` |
| [x64 support](docs/x64.md) | Layout checks that apply only to the retail Win32 layout, 32-bit handle ids for addresses, portable versions of the x87 assembly, and a mode that puts every allocation above 4 GB so truncated pointers fail immediately |
| [FLRegress](docs/flregress.md) and [RpcCodecTest](docs/testing.md) | A headless harness that dumps the server's game state, and a test of the packet codecs' wire bytes. CI requires both outputs to stay identical across runs and to the PR's base commit, and the x64 codec bytes to match Win32 |
| [CMake build](docs/building.md) | Replaces the Visual Studio solution with a single build for Win32, x64 and the portable modules on Linux and macOS |

## Building and running

1. **Clone** this repository.
2. **Add the game data:** copy the `DATA` folder of an installed Freelancer (or of a mod) into
   the repository root, next to `src`, so that `DATA\` and `DATA_EXTRA\` sit side by side.
3. **Add the DirectX 8.1 SDK:** download the
   [DirectX 8.1 SDK (Final)](https://archive.org/details/dx81sdk_full), install or extract it, and
   copy its `include` and `lib` folders into a `dx8` folder at the repository root (so that
   `dx8\include\d3d8.h` and `dx8\lib\d3d8.lib` exist). Only the headers and libraries are used.
   You can give another location with `-DDx8SdkDir=<path>`.
4. **Build.** Open the repository folder in Visual Studio (*File > Open > Folder*) and pick a
   preset, or build from a Developer Command Prompt at the repository root:

   ```
   rem Win32, the retail reference (vcvarsall.bat x86)
   cmake --preset release
   cmake --build --preset release

   rem x64 with RPGPU (vcvarsall.bat x64)
   cmake --preset release-x64
   cmake --build --preset release-x64
   ```

Win32 builds to `bin\` and x64 to `bin_x64\`. `bin\` already holds the runtime configuration
(`DACOM.INI`, `freelancer.ini`, `FLConfigDatabase.txt`, `NewPlayer.fl`, `MPNewCharacter.fl`).
Run `freelancer.exe` **from its output directory**, because the library and data paths are
relative to it. To run a dedicated server, see [docs/dedicated_server.md](docs/dedicated_server.md).

On Linux and macOS, `cmake --preset portable` builds the modules that do not use the Windows API.

All presets and further details: [docs/building.md](docs/building.md).

### Requirements

- Visual Studio 2022 (platform toolset v143) with the *Desktop development with C++* workload,
  *C++ MFC for latest v143 build tools (x86 & x64)* (used by `FLServer`), *C++ CMake tools for
  Windows* (CMake 3.27 or later, and Ninja) and a Windows 10/11 SDK.
- vcpkg for RPGPU and SDL3. Visual Studio includes it; elsewhere, set `VCPKG_ROOT`.
- Python 3 on `PATH` for every output directory except `bin\`. A post-build step copies the
  runtime configuration from `bin\`.

### Optional features

Port-only features are build switches, set with `-D<Name>=0|1` at configure time (for example
`cmake --preset release -DFreelancerRp9=1`):

| Switch | Default | What it does |
|---|---|---|
| `FreelancerRpGpu` | 1 on x64, 0 on Win32 | RPGPU, the SDL3 + SDL_GPU renderer |
| `FreelancerWidescreenSupport` | 1 | Widescreen (Hor+) display modes and HUD layout |
| `FreelancerWalkMode` | 1 | Walking through base rooms as Trent ([docs/walk_mode.md](docs/walk_mode.md)) |
| `FreelancerRp9` | 0 | `RP9.dll`, a Direct3D 9 renderer with volumetric nebulae ([docs/re/rp9.md](docs/re/rp9.md)). It needs the [DirectX SDK (June 2010)](https://www.microsoft.com/en-us/download/details.aspx?id=6812) and `DirectX9` in `bin\DACOM.INI` |
| `FreelancerBetaLabel` | 0 | The "Beta" label in the corner of the screen |
| `FreelancerEnableTickProfile` | 0 | On-screen frame profiler |

## Documentation

Start at [docs/README.md](docs/README.md). [docs/MISSION.md](docs/MISSION.md) covers the
project's principles, and [docs/re/deviations.md](docs/re/deviations.md) lists every known way
the port differs from retail. [docs/ini/README.md](docs/ini/README.md) is a reference for every
INI file the code reads.

## Repository layout

```
src\          all source code, with a CMakeLists.txt per module (CMakeLists.txt and cmake\ at the root)
bin\          Win32 Release output and the runtime configuration files
DATA_EXTRA\   the port's own data (shaders, volumetric.ini), next to the game's DATA
EBUEula.dll   retail EULA dialog, loaded as ..\EBUEula.dll with ..\EULA.rtf (as the retail game does)
EXE\          gundll.dll, the retail GUN client (multiplayer server list), loaded as ..\exe\gundll.dll
docs\         documentation
tools\        the RPGPU shader build script
```

`EBUEula.dll` and `gundll.dll` are the only retail binaries the port uses unchanged. They are
third-party components of the original game and have not been reimplemented. Both are 32-bit,
so the x64 client skips the EULA dialog.

## License

The source code of Freelancer Decomp is released into the public domain under
[The Unlicense](LICENSE). This does not cover third-party code and files in the repository:
the vendored libraries under `src\` (`src\zlib\vendor`, `src\qhull\vendor`) keep their own
licenses, and the retail files (`EBUEula.dll`, `EULA.RTF`, `EXE\gundll.dll`) and the game data
remain the property of their respective owners.
