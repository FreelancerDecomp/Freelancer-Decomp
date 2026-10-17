# Deviations from retail

This file lists where the port deliberately or temporarily differs from the retail client's behaviour, and the "retail quirks" the port reproduces on purpose (comments in `src/` refer to them). Read together with [substitutions.md](substitutions.md), which describes what exactly the exe does instead of calling the ported modules.

Entry format: what retail does (listing address) — what the port does (file in `src/`) — why — removal condition — owner.

Closed entries are deleted from the file; their history lives in git. An entry is closed only with proof: the file and line in `src/` plus the listing address the code now matches.

Differences that follow from the "No retail ABI" rule (modern `std::` containers instead of VC6 nodes, struct sizes) are not counted as debt and are not recorded here.

## Client: Beta label and on-screen profiler (CLIENT-BETA-LABEL, 2026-10-12)

Status: OPEN — intentional deviation at the owner's direct request. Owner: Giperion; freelancer.exe / Client and Common.dll / IVP. Removal condition: only an explicit instruction from Giperion; do not close on a retail audit, a version change or completion of the port.

In `FUN_00425080` (`0x00425080..0x00425138`) retail calls `EndScene` right after the flush (`0x0042508f..0x0042509a`): there is neither a caption nor any timing there.

The version label (`Beta 2`, `src/freelancer/Client/client_tick_profile.cpp`) is guarded by the `FREELANCER_BETA_LABEL` define and is **off by default** (the `FreelancerBetaLabel` property in `src/Freelancer.Diagnostics.props`, command line `/p:FreelancerBetaLabel=1`). When enabled, the port draws it in the top-left corner of every finished frame, with a 12-pixel margin. `DrawClientTickProfile` is called from `EndSceneHalf` after `MaterialBatcher->FlushAll(1)` and before `Pipeline->EndScene`. Below the label it optionally prints the list of timings of the previous finished `TickClientFrame` (QPC, milliseconds; a parent's time includes its children): UI/Server/Game tick, Objects, Physics / hits (with sub-items IVP simulate, PSI prepare, Sim units, Commit / hulls, Contacts / events, Move beams, Find beam hits, Dispatch hits), World / FX, Sound, Render, Present.

The profiler is enabled by the `FREELANCER_ENABLE_TICK_PROFILE` define (0 by default; the `src/Freelancer.Diagnostics.props` sheet, the command line `/p:FreelancerEnableTickProfile=1` takes priority). When both defines are 0, `DrawClientTickProfile` draws nothing. For physics, a port-only export `PhySys::SetTickProfile` and the contract `src/include/common_tick_profile.h` (`FPhysicsTickProfile`) were added: the client installs a sink for the duration of its `Physics / hits`. These QPC measurements and the API exist only in the port; they do not change the order or arguments of game calls.

## Client: widescreen resolutions, Hor+ cameras (WIDESCREEN, 2026-10-16)

Status: OPEN — intentional port-only feature at the owner's direct request. Owner: Giperion; freelancer.exe / Client. Removal condition: only an explicit instruction from Giperion. All code is under `FREELANCER_WIDESCREEN_SUPPORT`; a build with `/p:FreelancerWidescreenSupport=0` (the "Freelancer — diagnostics" sheet → "Widescreen resolutions" = Off) gives retail 4:3 behaviour. Do not close on a retail audit.

Retail only knows 4:3: the settings page holds five fixed resolutions (ctor `0x004a982f..0x004a98bc`) and accepts a device mode only when `Width * 3 == Height * 4` (`FUN_004af120`); cameras store a horizontal half-angle and derive the vertical one from the viewport aspect (`FUN_0040fd60`, `FUN_0040f8c0`, `FUN_00556ce0`, `FUN_0041a140`), so on a wide screen the picture loses its top and bottom, and the Win3D interface (camera `DAT_0067dbf8`) is cropped.

The port with the define:

- Hor+ cameras. The stored horizontal half-angle means the angle at 4:3 of the same height; a camera whose viewport covers the whole surface width widens the tangent by `surface aspect / (4/3)` (`src/freelancer/Client/widescreen.h`); the inverse conversions divide by the same factor. HUD window viewports (target, comm, nav map, preview) are not wider than the surface and stay on retail maths. Letterbox keeps the full width and cuts only the height; at an aspect wider than 1.85 the bars do not go negative.
- The interface stays 4:3 in the centre (scaled by height): consumers that tied their scale to the surface width take `WidescreenEquivalentSurfaceWidth` (font selection `FUN_00416110`/`FUN_00416f50` and their cache key, HUD bands `FUN_004d5410`, contact captions, the steering circle, lead markers, the ticker, the nav map, the cursor). The loading picture is drawn 4:3 in the centre.
- The settings page mode list is built from the device modes: not smaller than 800×600, not narrower than 4:3, up to 24 resolutions (16 and 32 bit); the selection is keyed by width only, and one height remains per width — the one with the aspect closest to the desktop. Without a saved `[Display] size`, a fullscreen start uses the desktop resolution.
- Corner HUD windows at the screen edges (like HudShift): `HUD_Status` to the right; `HUD_Target`, `HUD_ContactList`, the comm window `HUD_Receiver` to the left; NeuroNet buttons `NN_MainButton` to the right; main menu: buttons to the left, `Version` to the right, logo to the left edge. The windows' world positions do not change: while a window is drawn, updated or receives the mouse (Win3D bypasses in `win3d.cpp`), the Win3D camera `DAT_0067dbf8` is shifted (`src/freelancer/Client/hud_widescreen.cpp`), so meshes, Project/Unproject and mouse rays agree at any depth. Centred children (`SpecialModeText*`, `ScaledBackground`) are not shifted.

Known leftovers: full-screen 2D elements other than the loading picture are stretched; drawing inside corner windows that counts pixels from the surface width without a camera is not shifted; the LOD threshold in `RendComp` (viewport width / XScale) picks a slightly coarser LOD on a wide screen than 4:3 of the same height.

## SoundManager / SoundStreamer: Fraunhofer ACM (decode only) (SOUND-ACM-DECODE-ONLY, 2026-10-10)

Status: OPEN — intentional deviation at the owner's direct request. Owner: Giperion; SoundManager.dll / SoundStreamer.dll. Removal condition: only an explicit instruction from Giperion; do not close on a routine retail-conformance audit.

When enumerating ACM drivers, retail accepts only the exact name `Fraunhofer IIS MPEG Layer-3 Codec (advanced)` (`SoundManager.dll` `LAB_06ee8c60`, `SoundStreamer.dll` `LAB_06ef3f10`). The port additionally accepts the exact name `... (decode only)` in both modules (`src/SoundManager/SoundManager.cpp:443`, `src/SoundStreamer/SoundStreamer.cpp:92`). Reason: the system Fraunhofer ACM in modern Windows returns the name `(decode only)`, the retail lookup rejects such a driver although it decodes the game's MP3/WAV. The first matching driver is selected, opening and decoding still go through ACM, other MP3 codecs are not accepted.

## DALib: `CDPClientProxy::SendMsgs` — simplified packing loop

Status: OPEN, `simplified`. Owner: DALib.dll.

Retail `CDPClientProxy::SendMsgs` (`DALib.dll` `0x065c3730`) is a branching state machine with its own path for large messages. The port (`src/DALib/CDPClientProxy.cpp:44`) collects the queue into a batch of up to `0x400` bytes, compresses it if the compressed size is no larger than the original, and hands it to `CDPServer::SendTo`. The send order and the "compress or not" decision are preserved; handling of a message larger than one batch is not ported (a message above `0x400` bytes is not skipped by the loop). Closed by porting `0x065c3730..0x065c3aa2` line by line.

## SoundManager: archetype map — linear array

Status: OPEN, container representation. Owner: SoundManager.dll.

Retail keeps the sound archetype map in a red-black tree (`+0xF8`); the port uses an array sorted by id (insertion adds ascending ids, removal shifts), so the traversal order and the result of any lookup match the tree (`FSoundManager::DestroyAllArchetypes`, `src/SoundManager/SoundManager.cpp:2093`, = `FUN_06ee4800`, destruction order by ascending id). Only the representation differs; there is no removal condition as long as the order stays the same.

## Screens / movie / boot splash: `movie.dll` via `GetProcAddress`

Status: OPEN. Owner: freelancer.exe / menu screens; out of scope by the owner's decision.

Retail imports `MOVIE.DLL` statically (three `[Startup]` movies, `FUN_005b1300` `0x005b18c0..0x005b19bc`). The port (`src/freelancer/Client/engine_init.cpp:265`, `PlayStartupMovies`) does `LoadLibraryA("movie.dll")` and takes six `MediaPlayer` exports via `GetProcAddress`, because `src/movie` does not publish a header in `src/include`; if the DLL or an export is missing, the movies are skipped instead of failing startup. Removal condition: a `movie` header in `src/include` and a static import, as in retail.

## `Acquire3DMathEngine`: one shared cache instead of per-translation-unit statics

Status: OPEN, `simplified` (tracker `0x06391cf0`). Owner: Common.dll.

The retail body (`0x06391cf0..0x06391d28`) does not cache: every call builds an AGGDESC, calls `DACOM_Acquire` and `CreateInstance`. In retail the cache lives at the call sites — 25 file-level statics in separate translation units. In the port none of the 114 call sites cached, so the cache is collapsed into one `static` variable inside `Acquire3DMathEngine` (`src/Common/common_csys.cpp:729`). Observable difference: retail may hold up to 25 references to the engine, the port holds one. Removal condition: restore the cache at the call sites and bring the body to retail if `CreateInstance` stops returning a shared instance.

## `ArchetypeLoadFromPath`: port-only helper with no callers

Status: OPEN, scaffold. Owner: freelancer.exe / `client_mesh_load`.

This is not a retail function: it replaces code in `FUN_0059ec80` / `FUN_00420dd0` and the room spawners (`Archetype::Load`, then an optional `IEngine::CreateInstance`, fallback `.sph` → `.cmp`). Defined in `src/freelancer/Client/client_mesh_load.cpp:96`, declared in `client_mesh_load.h`; there are no callers in `src/` (it is only pulled in by `#include` in `menu_universe_base.cpp`). Removal condition: delete the function and the header; the retail paths already call `Archetype::Load` directly.

## Universe: uninitialised retail stack values reproduced as zeros

Status: retail quirk, reproduced on purpose (the owner's decision on retail UB, 2026-10-08).

The loader `FUN_06337250` (`SYSTEM`) keeps stack slots between INI sections that retail does not zero: the `[System]` position (`+0x74`) and the `[Object]` nickname; their first values in retail are stack garbage, in the port 0 (`src/Common/common_universe.cpp:78`). `CopyRelPath` (`:329`, = `0633753a` and `063367e8`) copies the system file name byte by byte without a bound into a `0x108` stack buffer; the port stops at the bound — for names shorter than the buffer the result is identical.

## FUN_00402500 aliasing: base Inherit of ICliObj (2026-09-25, W3-A)

Status: retail quirk, partly reproduced (the owner's decision on retail UB, 2026-10-08). Owner: freelancer.exe / SPACE_OBJ (`src/freelancer/Client/client_cliobj.cpp`).

The base `Inherit` `LAB_00406970` (`FCliObj::Inherit`, `client_cliobj.cpp:2091`) first copies fields member by member (the donor's `+0x04` lands in the recipient), then calls `FUN_00402500(&this->LodHead, Src->LodHead)` (`0x00402500..0x00402575`). This gives `*Dest == Source`: retail frees the donor's shared LOD chain (`0x00402510..0x00402520`) and reads the freed nodes (`0x00402550`, `0x00402565`) — a use-after-free, the outcome depends on the MSVCRT heap; the recipient is left with an empty chain either way (`*Dest` = null, the copies are lost), the donor with a dangling pointer.

The port (`CopyLodChain`, `client_cliobj.cpp:1464`) does not free the chain in the only differing case `*Dest == Source != null`: the recipient gets the same result (empty), the donor keeps a live chain. The other paths match the listing. On regular data this is not reached: `LAB_00406970` sits in slot 7 only for CliRoot and CliEQShield, and `inherit =` in `DATA\` occurs only for `[Light]` (CliEQLight, its own `FUN_0040b0a0`, does not call `FUN_00402500`). Removal condition: it becomes known that retail never reaches `0x00406b31` with a non-empty donor `+0x04`.

## PerfOptions (`FUN_005b38d0` / `FUN_005b3ec0` / `FUN_005b40f0`, BOOT, 2026-09-01)

Status: structural differences with no observable effect; option behaviour (order, values, apply bodies) matches retail. The entry is needed by comments in `perf_options.h`.
Owner: freelancer.exe / Client. Removal condition: none (replacing the container with a static array is not observable).

- **Registry.** Retail is a `std::list` filled by static initializers (`FUN_005b3520`, 36 registrars in the `_initterm` XC slots `0x0060f000..0x006101bc`). The port is a static array `G_Options` (`src/freelancer/Client/perf_options.cpp:298`) in the same order, so the apply walk (`FUN_005b40f0`) and the save walk (`FUN_005b3ec0`) match. `GetPerfOptionSlot` indexes `G_Slots` without a bounds check, like `FUN_005b3510` @ `0x005b3514`.
- **Option record without a vtable.** The retail object is 0x1c bytes: vtable, name, IDS name, IDS description, value, "applied", callback. Slot 0 ("value read") of all 36 options = `0053e4a0` = `RET 4`, so the port has none; slot 1 (the option's apply, or the adapter `004c7160` to the callback at `+0x18`) is the `Apply` field of a `G_Options` row (`perf_options.h`, `FPerfOption`).
- **Registry teardown** `FUN_005b4050` @ `0x005b22c3` is not ported: it only frees the heap of options and nodes, and the tail of WinMain does not read the registry (comment in `main.cpp:555`).
- **Retail quirk reproduced on purpose.** `FUN_004afd80` always does `default * 0.01`, even when `optlist.ini` holds an index: `cockpit = 1` → 0.01 (the `> 0.5` threshold in `LAB_005b3820` gives "off"), `skip_transition_scripts = 1` → 0.01 (the `< 0.5` threshold in `LAB_004548f0` gives "on"), `DIFFICULTY_SCALE = 2` → 0.02. The port repeats this as is.

## Common.dll ErrorHandler::FdumpHook: extra debugger output (2026-09-27)

Status: OPEN (the owner's port-only debug line; remove only at his decision).
Owner: Common.dll / ErrorHandler.

Retail `LAB_0630abd0` (`ErrorHandler::activate` installs it at `0x0630ad5f`) writes the message only to the configured logs via `fwrite`/`fflush` (`0x0630ad01`, `0x0630ad11`). In `FdumpHook()` (`src/Common/common_errorhandler.cpp:319-320`) the port first formats the message into a local 1024-byte buffer and calls `OutputDebugStringA` twice, so messages reach the debugger even when the log does not accept them. The rest of the body (severity, the counters `DAT_063fcf30/34`, the `Flags >> 4` versus `LogId` route, the severity limit, `fwrite`/`fflush` for a FILE*) repeats `LAB_0630abd0`; retail's unbounded `vsprintf` (UB) is bounded by the buffer in the port.
Removal condition: remove these two lines and the formatting that feeds them when the owner finishes debugging FDUMP.

## Minor Common exports (2026-09-01)

Status: no deviations; the entry is kept for comments in `src/Common/common_misc.cpp` (`ParseGUID`) and `src/Common/common_csys.cpp:387` (`RotateMatrix`).

- **`ParseGUID` (`0631b9c3`..`0631bb64`).** At `0631b9e7` retail does a `strcpy` of the remainder of the string into a stack buffer (`[S+0x1c]`) and never reads it (Data1 `[S+0x18]`, Data2 `[S+0x14]`, Data4 `[S+0xc..0x13]`, the pair buffer `[S+8]`). The port omits the copy: its only observable effect is a UB frame overflow on a long tail.
- **`ParseGUID` returns NULL** at `0631b9c3` and `0631bb4d` (`CMPSD.REPE` with 16 zero bytes `DAT_06401750`, `SETZ`/`DEC`/`AND`) — this is retail behaviour, the port repeats it (`common_misc.cpp`, `kNullGuidText`).
- `ValueNum` (`0631bee0`) uses `MsvcrtAtof` (`src/Common/msvcrt_atof.cpp`) — a decimal emulation of MSVCRT `atof`, `"0x10"` gives 0 as in retail. `RotateMatrix` (`src/Common/common_csys.cpp:377` = `06303fe0`) goes through the math engine.

## `Gun::get_hp_type_by_index`: retail reads one element further (2026-09-01)

Status: retail UB, deterministic in the port (the owner's decision of 2026-10-08 on retail UB); for all defined indices the behaviour matches.

`Gun::get_hp_type_by_index` (`062f68c0..062f690a`) compares the index with the size of the `HpGunTypes` vector via `CMP EDX,EAX / JG` (`062f68f8`), i.e. it accepts `Index == Count` and reads a dword past the end of the array. The port (`src/Common/common_archetype_weapons.cpp:361`) returns `HpTypeNone` for this index, as for any other out-of-range one. With an empty vector the default table `DAT_063ecc38` is used (24 dwords, `0 <= Index <= 0x17`) — the bounds match. The same applies to `ShieldGenerator::get_hp_type_by_index` (`src/Common/common_archetype_equipment.cpp:1466`, the non-empty-vector branch stops at `Count`). The `HpAttachmentType` enumerator names are not retail (an enum leaves no symbols in the binary), only the values are retail; the names are derived from the `hp_gun_type` strings of the archetype loader.

## Cameras: mixed calling convention in the camera table

Status: retail quirk, reproduced in the port.

The retail camera vtable mixes two conventions. Slots `0x00..0x44` take `this` as an explicit stack argument (they start with `MOV EAX,[ESP+4]`; confirmed by the call `push edi; call [ecx+0x18]` at `0x0051b431`). Slots `0x48` (`LAB_0040f5d0`, `RET 4`, `this` is not read — the only stack argument is the output vector) and `0x4c` (`FUN_0040f5f0`, `RET 8`; the call `mov ecx,this; push value; push key` at `0x0051b3fe`) are `__thiscall`. The port declares the former as `DACOM_API` (stdcall), and `GetVelocity(float* Out3)` and `SetProperty(Key, Value)` as ordinary virtual (thiscall) methods in `src/freelancer/Client/space_camera.h` (lines 11-13 and 44-62), the implementations `FSpaceCamera::GetVelocity` (`space_camera.cpp:1058`) and the 0x48/0x4c overrides in GameCamera. Binary compatibility with retail is not a goal here; the goal is matching slot semantics.

## `ScriptCamera`: the destructor does not release the script, a failed load leaves the engine hanging — retail, reproduced on purpose

**Retail:** the destructor `0x0051dfc0` releases two sinks and detaches both watchers, but not the `IScriptEngine` at `+0xf8` — it was supposed to be released by the deactivation slot `0x68`. The failed-load path (`0x0051e575` `FDUMP` without a pointer check, `0x0051e57d` `XOR AL,AL`) returns `false` without releasing the engine and without zeroing `Script`; only the deactivation slot removes it.
**Port:** `src/freelancer/Client/space_camera_script.cpp` (`ScriptCameraDestruct`, `ScriptCameraActivate`) and `.h` repeat the asymmetry; comments refer to this entry.
**Removal condition:** none — this is retail behaviour, a "fix" changes the timing of `IsFinished` and the release order.

## Docking camera: `SetPlayerStrafeBurst` takes `int`, not `StrafeDir`

**Retail:** slot `0x58` (`FUN_0051eaa0`) starts with `FUN_004c7950` (reset of the behaviour direction) and `FUN_004c7a30(StrafeNone)`.
**Port:** `DockingCameraAttach` (`src/freelancer/Client/space_camera_docking.cpp:65-66`) calls `ResetPlayerBehaviorDirection()` and `SetPlayerStrafeBurst(0)` (`player_behavior.cpp:122/130`). The difference is only in the signature: `Dir` is an `int` (0 none, 1 left, 2 right, 3 up, 4 down) so that cameras do not pull in the `common_cequip.h` headers. It does not change behaviour; the entry exists as a reference from `player_behavior.h`.

## rpclocal: the ch1 buffer family — IClient senders keep the caller's block, IServerImpl senders copy

**Retail, IClient direction:** `0x11 SET_WEAPON_GROUP` (`0x06d8b4f0`) puts a raw `(buffer, length)` pair into the packet without an ownership flag (`payload+0x08` is not written) — the packet borrows the caller's pointer. `0x13/0x17/0x19/0x1a/0x1c` (`0x06d8b540`, `0x06d8b7a0`, `0x06d8b7f0`, `0x06d8b890`, `0x06d8b840`) set the literal 1 in `payload+0x08` (`06d8b573`, `06d8b7d3`, `06d8b823`, `06d8b873`, `06d8b8c3`): this is the ownership flag, the class destructor (`06d9b1a0`, `06d9b4c0`, `06d9b580`, `06d9b640`, `06d9b700`) frees the caller's block, so Server.dll hands them `operator_new` blocks. `0x16`/`0x18` copy (`0x06d8b5b1`, `operator new(0)` yields a non-null block).
**Retail, IServerImpl direction:** seven types (`0x06d8e000` 0x13, `06d8e080` 0x16, `06d8e100` 0x18, `06d8e1c0` 0x17, `06d8e240` 0x19, `06d8e2c0` 0x1c, `06d8e340` 0x1a) copy via `operator_new` + `REP MOVS` and set the flag; only `SET_WEAPON_GROUP` (`06d8dfb0`) borrows the pointer. The packet layouts differ too: `IServerImpl::PopUpDialog` (`06d8e180`) writes one dword at `packet+0x30`; `SPObjUpdate` (`06d8dc30`) reads a 0x30-byte `SSPObjUpdateInfo` with a `double` at `+0x20` (narrowed to float); `SubmitChat` (`06d8ecd0`) writes the target id at `+0x00`, the length at `+0x08`, a copy of the body from `+0x0c`.
**Port:** the flag is `bOwnsBuffer` (`src/rpclocal/rpc_payloads.h`, `FPayloadCommonBuffer`), `DestroyBuffer` and `RpcCopyIntoPacketBlock` (`rpc_payload_ch1.cpp`); `0x11` is `MarshalBufferCommon(..., false, 0)` (the dangling-pointer window is reproduced); the server side is `MarshalServerBufferCopy` (`rpclocal.cpp`). There is no synchronous delivery: all types go through the queue, as in retail. Removal condition: none (retail behaviour).

## rpclocal: CREATELOOT — the retail swap of `record+0x2c` / `record+0x30` reproduced

**Retail:** the sender `0x06d8aae0` and the receiver `caseD_28` (`0x06d811c8`) convert a 0x48-byte record to the 0x46-byte wire form and back; 16 of the 18 transfers are mutually inverse, two are not: sender `06d8abe3` (`record+0x30` → `wire+0x38`) and `06d8abea` (`record+0x2c` → `wire+0x3c`), receiver `06d812c7` (`wire+0x38` → `record+0x2c`) and `06d812cd` (`wire+0x3c` → `record+0x30`) — the fields are swapped over the round trip. The list counter is truncated to a byte (`06d811e9 MOVZX ESI,AL`), the walk uses a byte index over the whole list (`06d81267..06d812ea`).
**Port:** `src/rpclocal/rpc_payloads.h` (the "RETAIL TRANSPOSITION" comment) and `rpc_payload_ch2.cpp` repeat both the swap and the walk. Both ends of the wire are ours, a "fix" would diverge from retail. Removal condition: none.

## rpclocal: delivery from file-level statics, session id by reference, and arguments that retail drops

All of this is retail behaviour reproduced on purpose (do not "fix"):
- ch2 `0x02 LOGINRESPONSE`, `0x06 DESTROYOBJECT`, `0x0a ACTIVATEOBJECT`, `0x21 SYSTEM_SWITCH_OUT`, `0x22 SYSTEM_SWITCH_IN` are delivered from `.data` (`0x06dbb904`, `0x06dbb824/28`, `0x06dbb8f4..fc`, `0x06dbb82c/30`, `0x06dbb91c`) — not reentrant in retail either; port: file-level statics in `rpc_payload_ch2.cpp` (`GLoginResponseStatic`, `GDestroyObjectStatic`, `GActivateObjectStatic`, `GSystemSwitchOutStatic`, `GSystemSwitchInStatic_06dbb91c`). ch3 `LOGIN` (`.data:0x06dbba38`) and `CREATENEWCHAR` (`.data:0x06dbbb10`) are statics too (`rpc_payload_ch3.cpp`).
- The unit replacing the session id is passed by reference for ch2 `0x30, 0x33, 0x3e, 0x3f, 0x40, 0x41, 0x43, 0x44, 0x46, 0x47, 0x4b, 0x4c` and by value for the rest; `IClient` slot 9 has the retail shape `(x, Packet)` (`src/include/rpc_iclient.h`, `rpc_client_proxy.cpp`: `SendPacketClientRef` dereferences, like `06d8b238`).
- `IClient+0x01c STOPTRADELANE` (`0x06d8bd10`, `RET 0x10`) writes one of its four arguments into the packet; the receiver (`06d839fe`) sees zeros in the other two. The reverse direction (`IServerImpl::StopTradelane`, `06d8e770`) writes three dwords — `MarshalServerStopTradelane`.
- ch3 `0x05 REQUESTCHARINFO` does not transmit the `bool` (the receiver pushes the literal 0); `0x31 REQUEST_EVENT` / `0x32 REQUEST_CANCEL` narrow `int` and `unsigned long` to a byte; `PLAYER_TRADE` sub-type `0x80` does not transmit the session id (`StopTradeRequest` receives `payload+0x08`); `PLAYER_TRADE` sub-type `0x10` leaves `Health = 1.0f` (no `set_status`, `06d83842` / `06d8baa0`); ch3 `0x0a GFREQUESTSHIPINFO` is registered, but retail has no sender.
- ch2 `0x4f SENDCOMM`: `payload+0x54` is written (`06d890db`) and not read (`06d83083` pushes 0); ch2 `0x08 CHARSELECTVERIFIED`: `payload+0x04` is neither written nor read by anyone; ch2 `0x29 SETREPUTATION`: `arg+0x0c` is not serialized, the wide name is allocated as `(len+1)*4` while `(len+1)*2` is copied (`FUN_06d92710`).

## rpclocal: wire-format codecs — retail asymmetries reproduced on purpose

The addresses are in the comments of `src/rpclocal/rpc_wire_*.cpp`, `rpc_containers.cpp`, `rpc_transport.cpp`; this is a list so that nobody "fixes" them.
- **Routing header without bounds checking.** `FUN_06da1380` (`06da1380..06da13b2`) contains no `CMP`/`TEST`/conditional jumps and always returns 0; the cursor is *set* to 2. Port: `FRpcMarshalStream::PutHeaderUnchecked` (`rpc_transport.h/.cpp`) in all 48 encoders; with `Capacity < 2` two bytes are written past the buffer and success is reported, as in retail. The `if (result) return 0x16` branches are dead in retail too. The sticky error is cleared at the call site (74 sites, `06dae650..06dae65a` etc.).
- **ch2 `0x2e SETADDITEM` decoder** (`rpc_wire_ch2.cpp`): `operator new` (`06d97398`) *before* the bounds check (`06d973a3..06d973b1`) — with a truncated packet the block stays allocated (the packet destructor frees it); the old hardpoint is not freed (unlike `0x4f`, `06dad539/44`) — a twice-decoded packet leaks.
- **ch1 `0x02 FIREWEAPON`:** bit `0x20` (tier 64.0 for Z) is never set (`06da514b FCOMP` without continuation), although the writer (`06da53c5`) and the reader (`06da5859`) know it; two "exactly one hardpoint" tests of different widths (`06da516d CMP EAX,1` → bit `0x40`, `06da5466 CMP AL,1` → whether to write the counter) diverge on 257 ids. The hardpoint vector grows one element at a time by the retail policy `Size + max(Size,1)` (`RpcRetailVectorAppend`, `rpc_containers.h`; `06da5682`, `06da5945..06da5a1f`).
- **ch2 `0x04/0x42 CREATESHIP/CREATESOLAR`:** the literal-hardpoint length and the wide-name length go out as the low byte of a dword (`06da3d79`, `06d97f7f`) while the body goes with the full length (`06da3da1`, `06d97fbf`) — names of 128/256 bytes and more desynchronise the two sides; bit `0x04` of the flags is built from `payload+0x9c != 0` (`06d97d96`) regardless of the sender's choice (`06d895ef`); `payload+0x6c..0x78` is serialized by nobody. After the slot call, the CREATESHIP/CREATESOLAR statics are cleared by `CEqObjCreateInfo::clear` (`src/include/server_createinfo.h`) ↔ the inline code `06d82f60..06d8300d` (dummy list nodes and the vector block are kept).
- **ch2 `0x29 SETREPUTATION`:** the relation counter comparison is signed (`06d91f6a CMP EAX,0xff / JLE`); the sender uses `IsMPServer` (`06d921d0`), the decoder `SinglePlayer` (`06d92504`); the name length is written as the low byte and read signed (`06d92037` / `06d9246b MOVSX`), a negative length goes to `operator new` as is (`06d92471`); `CREATELOOT` sends two extra vectors only in single player (`06d92b7f`, `06d92ff1`).
- **ch2 `0x54 MISCOBJUPDATE`:** the string decoder takes `wcslen` of a copy with a terminator (`06d992d2`) — an embedded NUL truncates; the encoder sends all elements of `Ids` (`06d99023`), the decoder reads as many as the byte says (`06d9960d`); the string buffer is `malloc`/`free` without a test (`06d99271`).
- **ch1 `0x12 PLAYER_TRADE`:** the `IsMPServer` test is inverted between the encoder (`06da70ad JNZ`) and the decoder (`06da74ac JZ`) — the bounds check and the reported length differ (`FRpcMarshalStream::Reserve`, `PutDwordUncounted`, `GetDwordUncounted`); the decoder's sub-type `0x40` branch returns code `0x16` instead of `0x17` (`06da7541/52`); a sub-type with several bits sends only the header and the sub-type byte (jump table `06da73b0` / `06da7818`). The packet factory writes `0xffffffff` at `packet+0x34` (`06d9bafa`), ch3 `0x22` writes `0x3f800000` at `packet+0x4c` (`06da0e46`): `src/rpclocal/rpc_packet_registry.cpp`, per-type classes.
- **ch2 `0x4f SENDCOMM`:** `payload+0x50` is quantized in two steps — `FMUL float 0.1f` and `FMUL double 255.0` (`06dad26d`, `06dad278`), `PutQuantizedTwoStage` (`rpc_wire_ch2.cpp:853`), the decoder multiplies by `1/25.5` (`06dad614`) and zero-extends the byte. Do not fold into a single constant. The old text explained this by "x87 53-bit precision"; `src/Directory.Build.props` claims the opposite (24-bit precision after `CreateDevice`) — the discrepancy is not verified.
- **ch2 `0x2c CREATEMINE` / `0x2d CREATECOUNTER` / `CREATEGUIDED`:** `CREATEGUIDED payload+0x00` and `CREATEMINE payload+0x44` do not travel (`06da8850`, `06da9aab`); the CREATEMINE tail `+0x10..0x1f, +0x2c..0x43` is inside `if (SinglePlayer())` on both sides (`06da9745` / `06da99fe`); these are different codecs (CREATEMINE translates the archetype through `LargeIDToSmallID`, `06da965a`).
- **Blob decoders** of three different shapes are not unified (`LAB_06d978f0`/`LAB_06d97730` allocate unconditionally, `LAB_06da62d0`/`LAB_06da5bc0` reuse the buffer, `LAB_06db10e0` skips a zero counter); `LAB_06da5bc0` writes a NUL at `Buffer[Size]` into the reused buffer too (`06da5ca1`, a 1-byte overflow). ch3 `0x27 TRADERESPONSE` does not round-trip: the common encoder `LAB_06da9110` sends the address of `Buffer`, the decoder allocates `Size` bytes and overwrites the pointer with the second dword (`06d9a6d3..06d9a702`).
- **Equipment decoder** (`FUN_06da2480`) leaks on a short buffer (`LAB_06da26dd`), calls `operator new(HardpointSize)` with zero without a check (`06da265c`) and silently loses an element when `operator new(0x18)` fails (`06da2510`).
- **Packet statistics** (`.data:06dbbf4c`, `FUN_06da16b0`): the decoder accounts `Length + 2`, but compares the *uncorrected* length with `Max + 2` (`06da4eff..06da4f0b`, `06d9313b..06d93147`) — a new maximum must exceed the old one by 4; the average is an unsigned `DIV` (`06da4c80`, `06da4f13`). Port: `GRpcPacketStats` (`rpc_transport.cpp`).

## rpclocal / Server / Content: zeroed indeterminate stack bytes (retail leaves the bytes uninitialised, the port writes 0)

**Retail:** local structures on the stack/heap do not write some of their bytes (padding, upper halves of words, field tails), and the "value" goes to the wire/receiver as garbage. Verified in the listing: a frame without initialisation (`HandlePacket`: `SUB ESP,0x214` `06d81185`), the packet constructors do not touch the payload.
**Port:** writes 0 — retail's observable value is undefined, so the difference cannot be reproduced. Known places: ch2 `CREATEMINE +0x0e` (`Info.Pad0e`, `rpc_payload_ch2.cpp`; retail writes the word `+0x0c`, `06d81ae7`), `REPAIROBJECT +0x06` (`Info.Pad_0x06`, `06d82b73/63/5f`), `CHARSELECTVERIFIED +0x04`, `SETDIRECTIVE` padding `+0x09..0x0b` / `+0x91..0x93` (heap packet, `06d9d030`), `REQUESTSPACESCRIPT` the tail of the 0x104 field after the name copy and the byte `+0x14` for a foreign enum (`06d8aa84..06d8aaa1`), ch3 `LOGIN` the 0x404 window (`wcscpy` into a stack window, `rpc_payload_ch3.cpp`), `DAMAGEOBJECT` the upper half of the record id (`06d9081d`, `DamageEntry::SubObjId` is a `ushort`), `CollisionGroupDesc::Id` upper half on decoding (`rpc_wire_varlen.cpp`, nodes are built from eight uninitialised stack bytes: `FUN_06d85500` from the slot `ESP+0x14`, only the word `06d8f6af` is written), upper halves of the `CollisionGroupDesc` / damage list nodes (`06da47d5`, `06da490b`). Server.dll: `EQInfo` in `pub::SpaceObj::Dock` and `SetDockEquipment` (`src/Server/server_pub.cpp`, `src/Content/DockQueues.cpp`: retail writes the dword `+0` and the byte `+4`), `PhySys::CreateParms` in `JumpIn` / `Relocate` / `Launch` (`Name[1..7]`, `Pad27`; `SpaceObj_InitTransferParms`), `FInvincibility` in `SetInvincible` and the `SPRequestInvincibility` record (padding `+6/+7`, `IServerImpl.cpp`), `payload+4` in `IServerImpl::CommComplete` when both ids fail to resolve, `PhantomParms +0x07..0x0f` in `Phantom::Create`.
**Removal condition:** none — the value does not exist in retail; the entry exists so that new places following the same rule can refer to it.

## rpclocal: former bridges (BRIDGE) to Common-type mirrors removed

The comments in `rpc_containers.h` / `rpc_payload_ch3.cpp` mention "BRIDGE": all such places are closed in code. rpclocal links `Common.lib` (`rpclocal.vcxproj:55`; the retail `rpclocal.dll` imports COMMON.DLL by name, `.rdata:06db3004`, `06db3070..06db308c`, `06db30d8`, `06db30ac`), so ch2 `SETEQUIPMENT/SETCARGO/REMOTEOBJECTCARGOUPDATE` work with the real `EquipDescVector` (`RpcEquipHandleFromVector` / `RpcEquipVectorFromHandle`, `rpc_containers.cpp`), ch3 `REQEQUIPMENT/REQCARGO` with `EquipDescList`, `REQCOLLISIONGROUPS` with `std::list<CollisionGroupDesc>`, `DAMAGEOBJECT` holds the Common `DamageList` (`rpc_payloads.h`), `MISCOBJUPDATE` holds `std::wstring` / `std::vector<unsigned int>` (`rpc_iclient.h`: `Slot115` / `Slot120`). The remaining module "mirrors" are wire layouts (`FRpcCollisionGroupDescMirror`, nodes 0x0c/0x10/0x14, a 4-byte handle), not a substitution of Common types. The structure `FRpcDamageListMirror` (`rpc_payloads.h`) is not used anywhere — it stays as a documented image of the retail layout.

## rpclocal.dll -- the last 22 wire codecs and the per-type packet dump

> **Status:** not debt — retail wire-format asymmetries are reproduced on purpose (`src/rpclocal/`). The entry exists for comments in `rpc_containers.h`, `rpc_wire_closeout.cpp`, `rpc_wire_ch3.cpp`.
> **Source of truth:** `EXE\Disasm\rpclocal.dll.txt`.

Retail behaviours that the port repeats rather than "fixes":

* **ch1 0x1b POP_UP_DIALOG** (`LAB_06da5de0`): the length of the second block is written as `SizeB` (`06da5ede`), but `SizeA` bytes are copied from `BufferB` (`06da5ee8`, `06da5f14`/`06da5f19`, `06da5f29`); the decoder `LAB_06da5fe0` reads `SizeB` back. The stream desynchronises when the sizes differ. Port: `src/rpclocal/rpc_wire_closeout.cpp` (`RpcEncodeCommonPopUpDialog`), the same bytes as retail.
* **ch2 0x2b SETGROUPFEELINGS** (`LAB_06d91890` / `LAB_06d91a40`): the encoder does not write the relation counter (`06d91960` only compares it), the decoder reads a counter word (`06d91b3b`). Reproduced in both directions (`rpc_wire_closeout.cpp`, `RpcEncodeServerSetGroupFeelings`); safe — an element is 8 bytes, the counter is bounded by the decoder's checks.
* **ch2 0x32 REQUESTSPACESCRIPT** decode (`LAB_06d98850`): copies `Name` into a packet pointer (`06d98911`, `06d9891f`) that nobody allocated (the ctor `FUN_06d9db10` zeroes it). The port repeats the copy without its own allocation (`rpc_wire_closeout.cpp`, the decoder, next to `RpcEncodeServerRequestSpaceScript`).
* **ch3 0x39 CREATENEWCHAR**: `FUN_06da1580` with a zero length does not store the advanced buffer pointer (`06da1612` is skipped by the jump `06da1614`) — the port leaves `MarshalPtr` untouched (`rpc_transport.cpp`, `GetWideString`; `rpc_wire_ch3.cpp:575`); `wcscpy` into the name without a clamp under the retail null test (`06daf621`, `rpc_wire_ch3.cpp:616-622`).
* **ch3 0x01 LOGIN**: a real MSVCRT `_wcslwr` over the packet (`06dafc1d`, `rpc_wire_ch3.cpp:120`).
* **ch2 0x4e SETDIRECTIVE**: retail does not write six padding bytes (wire+0x09..0x0b, +0x91..0x93); the port does not write them either (null-source `PutBytes`), the size 0x94 is the same.
* **Packet slot `+0x0c`** (`Dump(std::ostream&)`): 12 of the 162 types have a body, the other 150 share an empty `RET 0x4`. Retail prints three wide strings (LOGIN, SELECTCHARACTER, CREATENEWCHAR) through `operator<<(void const*)` (`.rdata:06db318c`, ordinal 439) — i.e. an address, not text. Reproduced (`rpc_packet_dump.cpp`).

## rpclocal: the "Common.dll is unreachable" premise was never real

> **Status:** not debt — a reference for comments in `rpc_containers.h`, `rpc_payload_ch3.cpp`.

The retail `rpclocal.dll` imports 417 symbols by name from COMMON.DLL, including `EquipDesc::is_internal`/`make_internal`/`set_hardpoint`/`set_*` and `StringAlloc`; the port links `Common.lib` (`src/rpclocal/rpclocal.vcxproj:55`). Therefore equipment delivery and marshalling go through the real `EquipDesc` and `StringAlloc` interning. The module "mirrors" (`FRpc*Mirror`) remain only as wire layouts (0x18 record, nodes 0x0c/0x10/0x14), not as a workaround for an unreachable Common.dll.

## Server.dll wave 17 — the character save file (FUN_06d4ccd0), retail facts

> **Status:** all debt items are closed in code (the last one on 2026-10-07). The entry is kept because `src/Server/server_pub.cpp` refers to "wave 17 item 7".

* The character save in retail is `key = value` text (`fprintf` through `.rdata:s_%s_=_%s_06d6b578`), not a binary format; `FUN_06d4ccd0` (`.text:06d4ccd0..06d4ea4f`) refuses to write if the cheat check fired (`RET 0xc`: `FileName`, `Description`, `DescripStrId`). The invented 'FLSV' format (`0x56534C46`) was removed; the real magic is `"FLS1"` (`.rdata:06d6b30c`) before the XOR-encoded text.
* The source of position/orientation (`.text:06d4d635..06d4d6aa`): in single player `IObjInspect` slot 0; otherwise `cobject()` (only for `(ObjectClass & 0x503) == 0x503`), then `__RTDynamicCast` to `CRemotePhysicsController` (`.text:06d4d672`). Port: `src/Server/server_pub.cpp:1593` `dynamic_cast<CRemotePhysicsController*>`; on a miss the locals are not written (retail zeroes them at `.text:06d4d427..06d4d43d`).
* The account directory is created not by `FUN_06d4ccd0` (when `fopen` fails — FDUMP `PlayerDB.cpp` 0xad4 and `AL = 0`, `.text:06d4cf96..06d4cfb1`), but by four calls of `COMMON.DLL::BuildDirectory` in `PlayerDB` (`.text:06d511b6`, `06d51944`, `06d524da`, `06d53ee0`); the port has no `_mkdir`.

## `GetDirectionMatrix`: retail compares an integer with 0.98 (0xa3c10, 2026-09-01)

> **Status:** a retail bug reproduced on purpose (`src/Common/common_csys.cpp:452-488`, comment in `src/include/common_csys.h:115`).

Reference axis selection at `06303cf9..06303d16`: `dot = column1(Base) . Dir` → `ftol` (truncation) → integer abs → `FILD`/`FCOMP double 0.98`. The author wrote `abs()` from `<stdlib.h>` over a float, so `trunc(dot)` ∈ {-1, 0, 1}, and the condition `abs(...) <= 0.98` is true only at zero: the "column 1" branch is always taken, except when `dot == ±1.0`. The port repeats `abs(static_cast<int>(Dot))` (`common_csys.cpp:488`) — the matrix for nearly parallel inputs depends on the threshold. There is nothing to fix without changing the semantics. There are no direct references to the function in `freelancer.exe`, only the IAT slot `005c65a0`; the export is mandatory.

## `atoi`/`atol`/`_wtoi`: own copy of MSVCRT instead of the port's CRT

> **Status:** a deliberate mechanism difference with a 1:1 result. **Do not "fix".** Owner: `src/include/msvcrt_compat.h` (`MsvcrtAtoi`, `MsvcrtWtoi`).
> **Removal condition:** none; replacing it with the CRT `atoi` breaks reading of 32-bit hash IDs.

Retail calls `atoi`/`atol`/`_wtoi` from the system `MSVCRT.DLL`: a loop without overflow checking, the sum wraps modulo 2^32. The port is built with UCRT, where these functions saturate the result to `INT_MAX`/`INT_MIN`, while the game data stores hash IDs as decimal numbers with the high bit set (for example `ship_archetype = 2806013121` turned into `0x7fffffff`). Therefore every retail call is ported as `MsvcrtAtoi`/`MsvcrtWtoi` (the semantics were measured on the 32-bit `msvcrt.dll` of Windows 10 19045: 2 million random strings, 0 mismatches).

Residual differences: retail asks whichever `msvcrt.dll` is installed in the OS (for old OSes the set of whitespace characters and Unicode digits of `_wtoi` was not measured); the port takes `isspace`/`isdigit` from a fixed "C" locale table (retail does not change `LC_CTYPE`, in practice there is no difference).
Rule for new code: a port of a call to `MSVCRT.DLL::atoi`/`atol`/`_wtoi`/`_wtol` is written as `MsvcrtAtoi`/`MsvcrtWtoi`; if retail calls something else at that place (`strtoul`, its own parser), exactly that must be carried over. As of 2026-10-17 there are no bare `atoi`/`atol`/`_wtoi` in `src/Content` and `src/imeui`; `src/RP8` has not been rechecked for conformance.

## Content.dll: end-of-list check for objectives instead of stepping through the ring node (CONTENT-OBJECTIVE-END-ITERATOR)

> **Status:** a preserved container adaptation (modern STL). The step result matches retail.
> **Owner:** `src/Content/RandomMissionMachine.cpp` (`FRmRuntimeMachine::UpdatePlayerObjectives`, `First != List.end()` around line 1305).
> **Removal condition:** replace the local check with an equivalent advance operation that preserves the end position on an empty list; as long as a plain `std::list` is used, the check before `++First` must not be removed.

**Retail** (`DLLS/BIN/Content.dll.txt`, `LAB_06f8b330`): `06f8b35e` reads the first node; when there is no ship, `06f8b376 MOV EAX,[EAX]` moves to the next node without an emptiness check; the comparison with the end is only at `06f8b37c`. For an empty ring list the sentinel node points to itself, the position stays at the end, and the objectives update continues.
**Port:** `First != List.end()` is added before `++First`; on a non-empty list the first node is skipped as in retail, on an empty one `end()` is preserved and the rest of the function runs (modern STL forbids `++end()`, a direct translation gave a Debug assert when delivering a captured person after docking). There is no early exit from the mission update.

## Server.dll: chat commands on std::wregex instead of GRETA

> **Status:** a library substitution with proven equivalence for the single use; the owner's decision of 2026-09-24, retail UB accepted by the decision of 2026-10-08. Code: `src/Server/server_chat.cpp` (`ChatCommand_Make` :354 = `FUN_06d18ac0`, `ChatCommand_Match` :371 = `FUN_06d18be0`, `Server_SubmitChat` :556..).
> **Removal condition:** the owner requires a GRETA port — then port `FUN_06d1be80` (the pattern compiler) and `FUN_06d1f300` (the search) and return `ChatCommand_Make`/`Match` to them.

**Retail.** `IServerImpl::SubmitChat` (`0x06d191b0`) builds a table of eight commands (`{rpattern*, callback}`, `new rpattern(wstring, 0x8000, 0)`) on a statically linked GRETA (`FUN_06d1be80`, `FUN_06d1f300`, matchers `LAB_06d2b200`…`LAB_06d2b610`, character classes via `iswctype`).
**Port.** `std::wregex` (ECMAScript) + `std::regex_search(..., match_continuous)` over `std::wcmatch`; GRETA is not ported (its functions and globals are `skipped` in the tracker). The patterns are the same (`.rdata` `06d6898c`…`06d6888c`: `^echo\s+(.*)`, `^(invite|i)\s+(.*)`, `^(join|j)\s+(.*)`, `^(leave|lv)`, `^(universe|u)\s+(.*)`, `^(system|s)\s+(.*)`, `^(local|l)\s+(.*)`, `^(group|g)\s+(.*)`) with adjustments that preserve the GRETA semantics: `.` → `[^\n]` (GRETA `0x8000` fails only on NUL and `'\n'`), `\s` → a class of 0x09–0x0D, 0x20 and codes ≥0x100 with C1_SPACE (`ChatCommand_SpaceClass()`), `match_continuous` instead of MSVC's multiline `^` (GRETA's `^` is the start of the text only), case is significant, the patterns are not freed on unload (as in `FUN_06d18a60`).
**Retail UB.** The command pass gate `CMP [ESP+0x24],ESI` (`.text:06d193f8`) reads the uninitialised third output argument of `BinaryRDLReader::extract_text_from_buffer` (Common `0632a8a0` does not write it). The port seeds `DecodedSize = 1` (`server_chat.cpp:614`), and the gate reduces to `GChatText[0] == '/'` (`.text:06d19402`). The tracker item `0x06d191b0` is `ready_unverified`, the UB is named.

## FLServer: IRichEditOle reference across OnDestroy

> **Status:** OPEN (environment difference, MFC 4.2 vs MFC 14; no removal by porting more code)
> **Owner:** FLServer (`src/FLServer/FLServerView.cpp`, `CFLServerView::OnDestroy`, lines ~113-124)
> **Removal condition:** the port links an MFC whose `CRichEditView::OnDestroy` clears or keeps `m_lpRichEditOle`, or a rich edit control that keeps its `IRichEditOle` alive after MFC's `Release` (as riched20 of 2003 did). Then the AddRef/Release pair goes.

**Retail.** `CFLServerView::OnDestroy` (.text:0040e170) calls `CRichEditView::OnDestroy` first, then `m_pDocument->GetInPlaceActiveItem(this)` through vtbl+0xb8. MFC's base OnDestroy calls `m_lpRichEditOle->Release()` and leaves the pointer set; with MFC42 and riched20 of 2003 the control still held its own reference, so the following query hit a live object and returned NULL.
**Port.** Same calls, same order, plus one extra `AddRef` on `m_lpRichEditOle` taken around them and released after the query. Under MFC 14 with msftedit the base `Release` drops the last reference and `GetInPlaceActiveItem` → `IRichEditOle::GetObject` jumps through a freed vtable (owner's crash dump `FLServer.dmp`, EIP 0x78 under `CFLServerView::OnDestroy+0x68`, on closing the server window).
**Why.** The only way to keep retail's call order with the modern control; dropping or reordering the post-destroy query would change retail.

## DALib: IPv6 host enumeration answers dropped

> **Status:** OPEN (environment difference, DirectPlay 8.1 of 2003 vs the current dpnet.dll)
> **Owner:** DALib (`src/DALib/CDPClient.cpp`, `DPN_MSGID_ENUM_HOSTS_RESPONSE` case of the message handler, ~line 384)
> **Removal condition:** the client stack handles IPv6 hosts end to end (server-list address storage instead of `inet_addr`, link-local scope ids, Connect URLs) — the owner's planned IPv6 support. Then the filter goes.

**Retail.** `EnumerateHosts` (.text:065c2870) builds a TCP/IP host address with no hostname and calls `IDirectPlay8Client::EnumHosts`; the handler queues every `ENUM_HOSTS_RESPONSE`. DirectPlay 8.1 was IPv4-only, so every sender was IPv4. The client stores the sender's `hostname` and parses it with `inet_addr` (`CLoginMgr`, 005acea9..005acf78); `FRemoteServerImpl::Connect` rebuilds `x-directplay:/hostname=<host>;port=<n>`.
**Port.** The handler reads the sender's `hostname` and does not queue an answer whose host contains `:` (`wcschr(SenderHost, L':')`). The current dpnet.dll enumerates over IPv4 and IPv6; a link-local `fe80::…` sender (no scope id) breaks `inet_addr` and Connect (owner's beta report). The IPv4 answer of the same host arrives on the next retry.
**Why.** An explicit IPv4 broadcast hostname on the enumeration address (commit 32a6b89d) did not stop the IPv6 answers; dropping them in our own handler is deterministic.

## Client: single-instance guard behind FL_SINGLE_INSTANCE

> **Status:** OPEN (owner's development need, not an environment difference)
> **Owner:** freelancer.exe (`src/freelancer/Client/main.cpp`, `WinMain`, retail FUN_005b2f50, `#if defined(FL_SINGLE_INSTANCE)` near line 685)
> **Removal condition:** multiplayer testing no longer needs several clients on one machine — then define `FL_SINGLE_INSTANCE` in the client build (or drop the `#if`), which restores the retail code unchanged.

**Retail.** `WinMain` starts with `CreateMutexA(NULL, FALSE, "FreelancerClient")` (.text:005b2f87..005b2fac); a null handle or `GetLastError() == ERROR_ALREADY_EXISTS` (0xb7) jumps to the exit (LAB_005b3385) and returns 1, so only one client runs per machine.
**Port.** The same code, compiled only when `FL_SINGLE_INSTANCE` is defined. Nothing defines it, so by default the client creates no mutex and any number of instances start.
**Why.** The owner tests multiplayer by running several local clients against one FLServer.
