# Building

The build is CMake. `CMakeLists.txt` at the repo root lists the modules; each one has a
`CMakeLists.txt` in its folder under `src\`, and `cmake/Freelancer.cmake` holds the compiler,
resource-compiler and linker options they share.

- **Windows** (MSVC, Ninja): every module, for Win32 (the retail reference) or x64
  ([x64.md](x64.md)).
- **Linux and macOS** (GCC or Clang): the *portable* modules only, the ones that use no Windows
  API (today qhull, TexConvert and DPNet with its tests). Every other module is Windows-only
  until the non-Windows port gives it a platform backend; see [Linux and macOS](#linux-and-macos).

## Requirements (Windows)

- Visual Studio 2022 with the *Desktop development with C++* workload, *C++ MFC for latest v143
  build tools (x86 & x64)* (FLServer) and *C++ CMake tools for Windows* (CMake 3.27 or later and
  Ninja).
- The DirectX 8.1 SDK in `dx8\` (README.md), or `-DDx8SdkDir=<path>`.
- Python 3 on `PATH` for every output directory except `bin\` (a post-build step copies the dev
  `*.ini` files, see below).
- RPGPU (on by default on x64): vcpkg. Visual Studio ships it; elsewhere set `VCPKG_ROOT`.
- RP9 only: the DirectX SDK (June 2010), found through `%DXSDK_DIR%` or `-DDx9SdkDir=<path>`.

## Building on Windows

From a Developer Command Prompt of the platform you build, **x86** (`vcvarsall.bat x86`) for
Win32 or **x64** (`vcvarsall.bat x64`) for x64, at the repo root:

```
cmake --preset release
cmake --build --preset release
```

| Preset | Platform | Output | |
|---|---|---|---|
| `release` | Win32 | `bin\` | the retail reference build |
| `debug` | Win32 | `bin_debug\` | |
| `debugasan` | Win32 | `binasan\` | AddressSanitizer; also copies the `clang_rt.asan_*` runtime next to the exe |
| `release-x64` | x64 | `bin_x64\` | |
| `debug-x64` | x64 | `bin_x64_debug\` | |

Each preset has its own build tree under `build\`. One module: `cmake --build --preset release
--target Common`. In Visual Studio, *File > Open > Folder* on the repo root offers the same
presets; VS Code's CMake Tools does too.

All output directories are siblings at the repo root, so every `..\DATA` / `..\EXE` path the game
resolves from its exe directory works from each of them. Every build into a directory other than
`bin\` copies the dev `*.ini` and character files from `bin\` with the absolute paths rewritten
(`src/copy_dev_ini.py`): edit the files in `bin\`, not the copies.

Only the Ninja generator is supported: the Visual Studio generators would turn the options into
MSBuild properties, and MSBuild adds its own defaults on top. The configure step stops with an
error for that, and for a toolchain that is neither x86 nor x64 MSVC.

## Switches

Port-only features, `-D<Name>=0|1` at configure time (for example
`cmake --preset release -DFreelancerRp9=1`):

| Switch | Default | |
|---|---|---|
| `FreelancerWidescreenSupport` | 1 | Widescreen (Hor+) display modes and HUD; 0 builds retail's 4:3 client |
| `FreelancerWalkMode` | 1 | Walking through base rooms ([walk_mode.md](walk_mode.md)) |
| `FreelancerRp9` | 0 | `RP9.dll` and the client's Direct3D 9 hooks ([re/rp9.md](re/rp9.md)), Win32 only |
| `FreelancerRpGpu` | 1 on x64, 0 on Win32 | `RPGPU.dll`, the SDL3 + SDL_GPU renderer ([re/rpgpu.md](re/rpgpu.md)) |
| `FreelancerBetaLabel` | 0 | The "Beta 2" label in the corner of the screen |
| `FreelancerEnableTickProfile` | 0 | On-screen frame profiler |

## vcpkg

Port-only modules that need third-party packages (today RPGPU, with SDL3) get them from vcpkg:
one manifest, `src/vcpkg.json`, with a feature per module. The root `CMakeLists.txt` turns on the
features of the modules that are on and installs them while it configures (vcpkg's toolchain
file, found through `VCPKG_ROOT`, `VCPKG_INSTALLATION_ROOT` or Visual Studio's copy, unless
`CMAKE_TOOLCHAIN_FILE` is given). With no such module on, vcpkg is not used at all. Without
vcpkg a module that is on is skipped with a warning, or fails the configure step with
`-DFreelancerRequireVcpkg=ON`, as in CI.

Every module says what it is in its `fl_add_module()`: `RETAIL` (a rebuild of a retail module or
resource DLL), `PORT` (port-only: RP9, RPGPU, FLDedicated, TexConvert) or `TOOL` (FLRegress,
RpcCodecTest, TexConvertTest, DPNetCheck, MemoryDiagnostics). A retail module that asks for a
vcpkg package stops the configure step.

## The options, and why they are what they are

`cmake/Freelancer.cmake` spells every option out instead of taking CMake's MSVC defaults. The set
is the one of the Visual Studio solution this build replaced, which was checked equal command
line for command line and binary for binary (Release, Win32 and x64) before the solution was
removed. The ones that matter for fidelity:

- **x87 floating point on Win32** (`/arch:IA32 /fp:precise`), as the VC6-built retail modules:
  Direct3D 8 drops the main thread's x87 precision to 24-bit and never restores it, and x87 code
  inherits that where SSE2 code would not. A few files differ on purpose (`fl_item()` in alchemy,
  Thorn and the client's `alchemy_float_forward.cpp`). x64 always uses SSE2.
- **`_ITERATOR_DEBUG_LEVEL=1` in Release** keeps the std containers retail-sized: the layout
  `static_assert`s in `src/include` hold only with it (or with Debug's level 2).
- **No `NDEBUG`**: asserts stay on in Release.
- **No `/sdl`**: it zero-fills new objects before their constructors run (hiding the
  uninitialised-member behaviour of retail) and gives every exported class an exported
  `__autoclassinit2` that retail does not export.
- **C++20 with `/permissive`**: `/std:c++20` alone implies `/permissive-`; the decompiled sources
  compile unchanged without it.
- **Link order**: objects are linked in the order of the `SOURCES` list, which is also the order of
  static initialisation (see the note on `vis_effect_arch.cpp` in `src/freelancer/CMakeLists.txt`);
  libraries in the order `fl_link()` names them. This is why the source lists are written out
  rather than globbed.
- **DX8 headers after the Windows SDK**: `dx8\include` is appended to cl's `INCLUDE`, not passed
  as `/I`, so the SDK's `basetsd.h` wins.
- Per-file settings (`fl_item()`) replace the module-wide ones instead of adding to them: `/RTC1`
  with `/O2` is an error, and `/GL` cannot be taken back.
- **Precompiled header** (`PCH` in `fl_add_module()`: Common, freelancer and Content):
  `src/pch/fl_pch.h` holds only Windows SDK and standard library headers and is forced in front
  of `fl_layout.h`. It is build-time only, but a header there can still change code, so read the
  note in the file before changing its list. Files that must set up macros before `<windows.h>`
  opt out with `fl_item(<files> PrecompiledHeader NotUsing)`.

## Adding a module or a source file

- A source file goes into its module's `SOURCES` list; where it goes there is its place in the link
  order.
- A module is a folder with a `CMakeLists.txt` (`fl_add_module()`, `fl_link()`, see
  `cmake/Freelancer.cmake`), added to `FL_PROJECTS` in the root `CMakeLists.txt` after every
  module it links. A module for one platform only returns early (`if(NOT FL_WIN32) return()`).
- A resource-only DLL from a retail dump: `src/Resources/import_decompiled_rc.py` writes its
  `CMakeLists.txt` ([re/resources.md](re/resources.md)).

## Linux and macOS

```
cmake --preset portable && cmake --build --preset portable && ctest --preset portable
```

`portable-asan` builds the same in Debug with AddressSanitizer and UndefinedBehaviorSanitizer.
Needs CMake 3.27 or later, Ninja and a C++20 compiler (GCC 11+, Clang 14+, Apple Clang 15+). CI
builds both presets on Linux (GCC, Clang) and macOS.

A module is portable when it uses no Windows API: `PORTABLE` in its `fl_add_module()`, and its
folder in the non-Windows `FL_PROJECTS` of the root `CMakeLists.txt`. On these compilers
`fl_add_module()` makes a plain CMake target: the MSVC-only settings (`.rc` and `.def` files,
`fl_item()`, `fl_cl_options()`, linker flags) are ignored, and `fl_layout.h` is forced in as on
Windows. Making more modules portable is the non-Windows port (on hold; see
[x64-client-plan.md](x64-client-plan.md)), which needs a non-Windows backend for
`src/include/platform.h` ([platform.md](platform.md)) first.
