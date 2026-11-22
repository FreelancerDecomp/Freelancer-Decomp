# Plan: an x64 client, then an SDL3 renderer

This is the plan for a 64-bit Freelancer client that stays compatible with retail, and for the
steps after it towards a cross-platform renderer on SDL3 and SDL_GPU. It builds on the x64
server work in [x64.md](x64.md). Each numbered step is meant to be one pull request.

Status: proposed 2026-11-20; phases 1 and 2 mostly done, 5.1 and 5.2 merged (2026-11-21).

## Ground rules

Retail compatibility comes first, as for the x64 server:

- **Win32 stays the retail reference.** Its FLRegress and RpcCodecTest output does not change,
  RP8 stays its renderer, and every layout check stays active there. A change is either the same
  code on Win32 (for example `std::uintptr_t`, which is `unsigned int` there) or sits behind
  `_WIN64` / `_M_IX86`.
- **For the ports, compatibility means network and data.** An x64 or SDL3 client plays with
  retail clients and servers, and reads the retail `DATA\`, saves and data-only mods (including
  their 32-bit resource DLLs) without conversion. DLL plugins and exe patches stay Win32-only.
- **No new behaviour.** The port renderers draw what RP8 draws. RP8 screenshots are the
  reference. Visual extras such as RP9's volumetric nebulae stay off on x64; they are evaluated
  on their own in the optional stage after phase 4.
- Known last-bit differences (x87 trigonometry and logarithms, see step 1.2) are listed in
  [x64.md](x64.md), not hidden.

## Where we are

Updated 2026-11-21, from `master` after #96.

| Done or in progress | |
|---|---|
| x64 server modules (#17) | Built in CI, packet codecs byte-identical to Win32 |
| Thorn for x64 (#31) | Merged; the x64 server enters bases (room scripts) |
| Engine handles below 2 GB (#37), render plugin handles (#38) | Merged; the x64 FLDedicated shuts down cleanly |
| DPNet (#29) and its adapter (#30) | Merged; CI requires Microsoft's DirectPlay 8 client to join the x64 server |
| Phase 1 | Done: ReadFile handles and views (#33), x87 helpers (#40), resource DLLs as data files (#41), client layout checks (#43) |
| Phase 2 | Mostly done: leaf modules (#39, #45, #46, #47), the EULA skip (#44), RemoteServer (#49), freelancer.exe (#50), `IObjRW` / `Watchable` offsets (#51), `Slot23` arguments (#52). The whole solution builds for `Release\|x64` except the Win32-only projects below |
| Phase 5 | 5.1 and 5.2 merged (#58, #60), with their follow-ups (#78, #81). 5.3 under way: TexConvert, the D3DX-free texture loader (#59), the shader toolchain (#94) and the 2D draws (#96) are merged; the 3D draws are next |
| CMake (#11) | Draft. Not needed for Windows; needed before any non-Windows build |

What is left, from a fresh survey of `master` (counts are from grep, then spot-checked):

| Area | Size |
|---|---|
| Projects without an x64 build | movie (step 2.2); RP8 and RP9 (Win32-only renderers); FLServer (MFC), DPNetCheck, MemoryDiagnostics and the 8 resource-only DLLs, which are Win32-only by design |
| Inline assembly | None left outside `#if defined(_M_IX86)`; every block has a portable version |
| Layout `static_assert`s not yet `FL_LAYOUT_ASSERT` | None left in the client modules. The plain ones that remain hold on both platforms |
| Hard-coded retail byte offsets | About 160 in the client modules (freelancer 92, of which `cli_nebula.cpp` 37; movie 48; alchemy 9; flmaterials 5; RemoteServer 2; Deformable2 1), and about 45 more in Common and Server |
| Raw `this + 0x..` offsets in Common's AI behaviours | About 44 sites in 14 files (`common_behavior.cpp` and the behaviours). They already run in the x64 server and hit the wrong fields there; being fixed first |
| Pointers stored in 32-bit integers | movie's big-number code (about 300 casts, left out on x64, step 2.2); the rest was fixed in phase 2 |
| Renderer | RP8 cannot be built for x64 (D3DX8 has no 64-bit version). The x64 client gets RPGPU (phase 5), which so far draws the 2D draws (menus, HUD) |

Rough total for phases 1–3: 5 to 7 weeks of work, plus the runtime surprises a first run always
brings.

## Phase 1: shared foundations

These also make the existing x64 server safer.

**1.1 Pointer-sized file handles and views.** Done in #33. On x64 ReadFile hands out 32-bit
ids for its UTF handles and mapping tokens (`src/include/handle_value.h`; Win32 keeps the
addresses, as retail), `IFileSystem::MapFileView` returns `std::intptr_t`, and its callers keep
the whole address. With `FL_HIGH_MEMORY=1` the x64 programs reserve all free address space below
4 GB at startup, so most remaining truncations fail at once; the x64 CI runs set it, and the client
should call the same function when it gets an x64 build. It catches most, not all: memory taken
before the call (the process heap's first segment is usually below 4 GB) and the LowArena window
for EngBase's handles stay low, so an address that lands there by chance still fits in 32 bits. A
passing run with the switch is evidence, not proof, that no truncation is left.

**1.2 x87 helpers for the client.** Done in #40. Extend `x87_portable.h`:

- a compare helper that reproduces FCOM/FCOMP plus the FNSTSW flag tests, unordered cases
  included (the six compare sites in `menu_sound_arch.cpp` and `lparser.c`);
- `log10` as FLDLG2 + FYL2X, then ×10 rounded to float (`menu_sound_arch.cpp`, `SoundManager.cpp`).
  This can differ from Win32 in the last bit, like `x87_trig.h`.

Also document the client's precision rule (done in #40). The client runs at 53-bit precision
except between `BeginScene` and `EndScene`, where it uses the precision the Direct3D device left:
24-bit with RP8's default (`FPU_PRESERVE` 0). Portable client arithmetic is therefore the server's
`double` rule outside the scene and `float` arithmetic in the x87 order inside it. The
`_control87` precision switches (`client_frame_tick.cpp`, `engine_init.cpp`) are Win32 only,
since x64 has no x87 precision control.

**1.3 Layout checks.** Done in #43. Convert the client modules' layout `static_assert`s to
`FL_LAYOUT_ASSERT`, mostly with a script. Nothing changes on Win32. Checks that hold on every
platform stay `static_assert`.

**1.4 Retail resource DLLs.** Done in #41. `freelancer.ini` `[Resources]` DLLs and `resources.dll`
are loaded with `LoadLibraryA`. A 64-bit process cannot load a 32-bit DLL that way, so the retail
strings and infocards (and every mod's resource DLL) would disappear. Add
`Platform::ResourceModuleLoad`: `LoadLibraryExA(..., LOAD_LIBRARY_AS_DATAFILE |
LOAD_LIBRARY_AS_IMAGE_RESOURCE)` on x64, unchanged `LoadLibraryA` on Win32. Use it only at
`dataconfig_progress.cpp:217` and `main.cpp:750`. Then check that
`DialogBoxParamA(g_client.resources, ...)` (`engine_init.cpp:792`) works with that handle.

## Phase 2: the client builds for x64

Compile and link only; nothing runs yet. Modules go in dependency order, one or a few per PR,
each adding `Release|x64` / `Debug|x64` to the `.vcxproj` (and to `CMakeLists.txt` if #11 has
merged by then).

**2.1 Leaf modules** (done: #39, #45, #46, #47)**:** ximage, imeui, SoundStreamer, SoundManager,
flmaterials, alchemy, Deformable2, luaprofile. Includes:

- the asm sites, ported with the phase 1.2 helpers;
- alchemy's pointer casts (`alchemy_alchemy.cpp`, `alchemy_effectobject.cpp`,
  `alchemy_effectlibrary.cpp`). alchemy's x87 sin/cos path is unreachable on SSE CPUs; x64 uses
  the SSE path;
- pointer-sized storage in `.mat` properties (`Shading/Material.cpp` string and component
  payloads, `flmaterials` `FlmLoadDeclaredParameter` and its raw record offsets);
- Deformable2's skin table (`FSkinVertex` allocated as 8 bytes per entry), its bone `Parent`
  handle in an `int`, its facet table, and `Count*4` pointer arrays;
- the same pointer-array strides in `ximage.cpp`, `RendComp/TriMesh.cpp` and
  `RendComp/Plugins.cpp` (sphere materials).

**2.2 movie.dll:**

- `SetWindowLongPtrA` / `GWLP_*` for the subclassed window procedure and user data;
- the exe resolves movie's exports by their decorated 32-bit names (`engine_init.cpp:292`).
  `PlayMovie`'s name is different on x64; take its x64 name there, so Win32 is unchanged;
- the big-number code (`MovieField*.cpp`, `MovieGraph.cpp`, `MovieExp.cpp`, `MovieWide.cpp`,
  about 320 pointer-to-integer casts) answers the WM ASF Reader's `IWMAuthorizer` challenge: the
  old Windows Media Format SDK "unlock", with four built-in certificates (`MoviePlay.cpp`). On x64
  the movie module attaches no service provider and leaves that code out, on the assumption
  that today's reader plays unprotected WMV without it. If the reader refuses, x64 skips the
  intro movies, as it skips the EULA, and a GitHub issue tracks a way to bring them back.
  Not on the critical path: done after the exe runs.

**2.3 RemoteServer** (done: #49)**:** seven asm blocks, all float copies or double-to-float
narrowing (covered by `X87CopyFloat`). The packet codecs it shares with RemoteClient are already
ported and checked by RpcCodecTest.

**2.4 freelancer.exe** (builds: #44, #50, #51, #52; the hard-coded offsets and Common's behaviour
offsets in "Where we are" are what is left)**:**

- the seven asm blocks;
- pointers kept in integers: `engine_light.cpp`, the `DrawStateA` sites, `cli_asteroid.cpp`,
  `space_scene_obj_draw.cpp`, `long` engine-instance handles, and the UI slot messages that
  pass `this` as `unsigned` (`Slot23`);
- `FL_FACET_OFFSET` for `G_ThornFacetTable` and the `Watchable` ±8 adjustments;
- the 133 hard-coded byte offsets (for example `cli_nebula.cpp` `This + 0x294`), each replaced
  by a named member;
- the crash-report block for `dw15.exe` (`main.cpp:150`), which has 32-bit offsets; leave it out
  on x64.
- the agreement dialog: `EBUEula.dll` is a 32-bit DLL whose `EBUEula` export the client calls,
  so x64 cannot load it. The x64 client skips the agreement, as FLDedicated does (#44,
  `RunEulaGate` in `main.cpp`); Win32 still shows it.

**2.5 (dropped) RP9 for x64.** RP9 was to be a stopgap x64 renderer. The x64 client goes
straight to RPGPU instead (phase 5, now in parallel with phases 2 to 4). RP9 stays a Win32-only
option.

**Gate:** `Release|x64` of every project that is not Win32-only is required in CI, with vcpkg
required (a missing vcpkg is a build error, not a skipped RPGPU, since #60). The
Win32-only projects are RP8, RP9, FLServer, DPNetCheck, MemoryDiagnostics and the resource-only
DLLs. Win32 FLRegress and RpcCodecTest output is unchanged.

## Phase 3: the x64 client runs and plays with retail

**3.1 (folded into 5.2) RPNull.** The RPGPU skeleton answers RP8's IIDs and draws nothing but a
cleared frame, which is what RPNull was for. A no-device mode of the skeleton (the Null backend in
5.2's follow-ups) covers CI machines without a GPU.

**3.2 Client smoke test.** A scripted run of the real client: start, new game, first base,
launch, fly, dock, save, load, quit. It runs on Win32 (RP8) and x64 (RPGPU without a device) in CI,
and runs on x64 with RPGPU by hand. It also runs with the "everything above 4 GB" switch from
step 1.1.

**3.3 Interop.** Report-only CI first, required once stable:

| Client | Server |
|---|---|
| x64 client | Win32 FLServer / FLDedicated (the retail reference) |
| Win32 client | x64 FLDedicated (over DPNet, #30) |
| x64 client | x64 FLDedicated |

The x64 client starts on Microsoft's DirectPlay. As far as we know, Windows' optional DirectPlay
feature includes a 64-bit `dpnet.dll`; check this first. A client-side DPNet adapter
(`IDirectPlay8Client` on DPNet, the counterpart of #30) then removes the dependency.

**3.4 Data.** Load retail saves and at least one data-only mod that ships its own 32-bit
resource DLL, on x64.

## Phase 4: rendering without D3DX

Still on Windows. This prepares RPGPU, which needs it before it can draw textures.

**4.1 Texture decoder.** Our own loader for what D3DX 8.1 reads in Freelancer's data (DDS,
including DXT; TGA; BMP; cube maps), with D3DX 8.1's choice of format and mip chain.
`IRenderPipeline` exposes `_D3DXIMAGE_INFO` (`GetImageInfo`), so that struct is kept as our own
definition. Test: on Win32, decode every texture in `DATA\` with both D3DX8 and the new loader,
and compare the surfaces byte for byte.

**4.2 Screenshot comparison.** A set of fixed scenes (fixed camera, frozen time, fixed
resolution), captured through the existing screenshot path and compared with a tolerance. RP8 on
Win32 is the reference.

## Optional stage: RP9's extras on x64

Not on the path to SDL3. The extras were added by the port and have no retail reference, so they
get their own evaluation once the screenshot comparison from step 4.2 exists:

- Capture the step 4.2 scenes with the extras on and off, next to the RP8 reference, and look
  at what they change.
- Decide from that whether they stay, change, or go, on Win32 too.
- Only if they stay: port `render_ex.*` and `nebula_volume.*` to x64 and give RPGPU an Ex side.
  The check is that RPGPU draws what the Win32 RP9 build draws with the same `volumetric.ini`.

RP9 is Win32-only, so on x64 the extras would need RPGPU's own Ex side. That is a separate
decision.

## Phase 5: RPGPU, the SDL3 + SDL_GPU renderer

A port-only DACOM module, selected in `DACOM.INI`. It is developed on Windows first, with its
output compared against RP8. It is the x64 client's first renderer, so it starts now, in
parallel with the rest of phase 2.

**5.1 SDL3 dependency.** Done in #58 and #60. From vcpkg in manifest mode: one `src/vcpkg.json`
with a feature per consumer (`rpgpu` today), `builtin-baseline` pinned to a vcpkg release, built
from source for Win32 and x64, so a later SDL platform layer or audio decoder shares RPGPU's SDL3.
`src/Vcpkg.props` wires it into port-only projects only, so no retail module sees SDL. RPGPU is an
explicit setting (`FreelancerRpGpu`, like `FreelancerRp9`; on by default on x64, off on Win32). A
developer machine without vcpkg skips it with a warning; CI passes `FreelancerRequireVcpkg=true`,
so there a missing vcpkg is an error. CI caches vcpkg's binary packages per triplet, runner image
and manifest. The same manifest works under CMake later. The window stays the client's own. SDL
wraps the existing `HWND` (`SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER`), so nothing else in the
client changes yet.

**5.2 Skeleton.** Done in #58. Every RP8 interface, with state kept and resources held in CPU
memory. Bring up the device and swap chain, display modes, gamma, clear, and present.

Follow-ups to the skeleton, before 5.3 builds on it:

- RPGPU reads the `[RenderPipeline]` keys as RP8 does, through one shared header
  (`render/rp8_semantics.h`): the number syntax, the property tables and `FvfStride`. The
  skeleton's own parser had drifted (`0x1C` read as 28 where RP8 reads 1).
- A backend seam with an SDL backend and a Null backend (an ini key), so the 3.2 smoke test runs
  on CI machines without a GPU, and `PrintScreen` reads back for the 4.2 comparison.
- Only handles in LowArena. The font atlas gets a pointer-sized lock record on x64, so texture
  images no longer need to live below 2 GB.
- `RpGpu.cpp` split into state, resources, backend and DDS parsing; a port-owned
  `render/rp_types.h` instead of `d3d9.h` in `IRenderPipeline.h`.

**5.3 Fixed-function emulation.** What reaches the device is small: 33 render states, 11
texture-stage states, a few vertex formats (FVF), and no programmable shaders in RP8. Write the
fixed-function pipeline in HLSL, convert it offline with SDL_shadercross to DXIL, SPIR-V and
MSL, and keep the bytecode in the repository. In parts, each its own PR:

1. Done in #59: TexConvert, the D3DX-free replacement for `D3DXLoadSurfaceFromMemory`, checked
   against D3DX 8.1 in CI.
2. Done in #94: the shader toolchain (offline compile, committed bytecode, a CI check).
3. Done in #96: the 2D draws (pre-transformed vertices: menus, HUD), texture stage 0 and the
   alpha test.
4. 3D geometry: world, view and projection as RP8 keeps them, vertex- and index-buffer draws,
   culling, fill mode, z-bias, mip maps, DXT textures uploaded as blocks. Ships and stations
   draw unlit. In #98.
5. Lighting (RP8's light manager, Direct3D 8's per-vertex model) and fog (vertex, table, W).
   In #99.
6. Four texture stages (every op but bump mapping and `PREMODULATE`, which pass the current
   colour through), texture transforms and texture-coordinate generation.

**5.4 Buffers and textures.** Vertex and index buffers that stay on the GPU (static, dynamic,
and the lock/discard pattern), render targets, and screenshots. Done or under way: buffers in CPU
memory with RP8's lock semantics (#58), textures through TexConvert (#59), mip chains and DXT
uploads (5.3, part 4), screenshots (#101), cube maps (#104) and static vertex buffers kept on
the GPU. Still copied each frame: index buffers, the ring pools and user-pointer draws. Render
targets need nothing: RP8 answers `SetRenderTarget` with "unsupported", and the game does not
call it.

**5.5 Parity.** RPGPU on Win32 passes the phase 4.2 scene set within tolerance against RP8, the
smoke test passes on it, and then it becomes the default on x64. Win32's default stays RP8.

## Phase 6: other platforms (Linux, macOS, Windows ARM64)

Not started until the hold on the non-Windows port is lifted. It needs CMake (#11), which is
not on the critical path before then: MSBuild builds Win32 and x64, and #11 would have to prove
byte-identical Win32 output again.

- An SDL3 implementation of `platform.h` (window, timers, threads, files, modules).
- Input: DirectInput replaced by SDL events.
- Audio: DirectSound and ACM replaced by SDL audio and an MP3 decoder. Retail audio is MP3 inside
  WAV files.
- Movies: the intro movies are WMV played through DirectShow. They need a decoder, or they are
  skipped on these platforms (to be decided).
- IME: `imeui` replaced by SDL text input.
- Network: DPNet, with no Microsoft DirectPlay at all.
- Files: retail `DATA\` mixes upper and lower case in names that the ini files spell differently,
  so case-insensitive path lookup is a requirement for reading retail data unchanged.
- Compilers: clang and GCC, and the MSVC CRT behaviours documented in
  [re/crt_compat.md](re/crt_compat.md).
- Windows ARM64 is the cheapest of these and a good first check that the code is free of x64-only
  assumptions.

## Decisions needed

1. Phase 6: lifting the hold on the non-Windows port, and what to do with the movies there.

Decided: whether RP9's extras come to x64 is decided in the optional stage after phase 4, from
the screenshot comparison.
Step 2.2: the movie module's authorizer and big-number code stay out of x64; if the reader then
refuses the intros, x64 skips them.
Step 2.5: no RP9 on x64; RPGPU is the x64 client's first renderer. Step 3.1: RPNull is the RPGPU
skeleton. Step 5.1: SDL3 from vcpkg in manifest mode, scoped to RPGPU. CMake (#11) waits until
phase 6.
