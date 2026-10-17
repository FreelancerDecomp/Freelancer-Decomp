# Freelancer: HD Edition — compatibility plan

Status: research and plan (2026-10-15); only what is listed in the section
"What the port has" is implemented. Goal: the port runs Freelancer: HD Edition data and behaves like
an HD installation **without loading plugin DLLs and without byte patches** — every HD change is
rewritten as plain C++ in the module that owns the retail function, behind a compile-time switch, so
the vanilla build stays retail-exact. That is why `jflp.dll` and `HudShift.dll` are not added to
`bin/DACOM.INI`: they patch hardcoded addresses and crash the client.

The HD v0.7.1 installation was studied together with the plugin sources (FLHDE/flplusplus, FLSharp,
adoxa, jflp v1.27). The installed plugins are **older** than the public sources: FLSharp 0.8.0
(the sources are 0.9.2), flplusplus without touchpad/confine_cursor/vsync/max_fps/direct_ips. The
behaviour of the installed versions is what gets ported. The patch address tables and the plugin
ini files are in the git history of this file (commits before the cut-down); when working on an item
they must be re-checked against the listing.

## What HD changes

| Layer | What | Impact on the port |
|---|---|---|
| DATA | hundreds of INI files converted from BINI to text; new models, ALE, THN, fonts | data only; the reader accepts text. New plugin ini files: `HudShift.ini`, `ranks.ini`, `Territory.ini`, `storyfactions.ini`, `MultiIntro.ini` |
| Resource DLLs | changed texts; new `Controls.dll` (`.rsrc` only) in `[Resources]`, IDS `0x7xxxx` | data |
| Static byte patches | ready-made patched exe/DLLs (Freelancer.exe, Common, Server, rendcomp, rp8, DALib, ebueula) | rewrite as C++ in the corresponding functions |
| Plugin DLLs | `jflp.dll` and 17 plugins in `dacom.ini [Libraries]`, `callsign.dll` in `[Initial SP DLLs]` | rewrite as C++ |
| External wrappers | `d3d8.dll`, `dinput8.dll`, ReShade | no code needed |
| Server-side | `FLHook.dll` and plugins | multiplayer, after the beta |

## Change groups

**jflp (widescreen, statistics, small things).** Widescreen recalculation of FOV/proportions/UI width
on every mode change, a mode list without the 4:3 filter and a default mode equal to the desktop, THN
cameras; Power/Engine/Scanner/Tractor/Armor statistics in the dealer infocards; "C4" sectors instead
of "4C"; Alt+Enter; roll in mouse-flight; 8-way strafe; the first weapon group active on start; a
number of FDUMP suppressions.

**Byte patches.** Gamma `slider + 0.75`, texture group limit, chat pool/length, IDS replacements on
`Controls.dll`, HUD thresholds (ContactList "Far", cruise speed), group update period on NavMap, FPS
limit from RTC, removed EULA/ESRB/mutex, VRAM check; in the DLLs — IVP damping, docking cancel
distance of 50 km, explosions without distance falloff, saves without encryption, AA/AF as
configuration values (not constants).

**flplusplus.** `lod_scale`, `pbubble_scale`, `character_detail_scale`, `asteroid_dist_scale`,
save and screenshot folders (PNG via WIC), private fonts from `fonts.ini [FontFiles]` (**required by
the HD data**), `Restart.fl` regeneration.

**FLSharp 0.8.0.** Improved object synchronisation (`CheckForSync` intervals/thresholds), waypoint
fixes, sound and weapon animation fixes, flashes on every barrel, copy/paste in `RichTextEditWin`,
a resolution list of up to 127 modes, test sounds in the settings, display of the required
reputation, crash fixes.

**Other plugins.** DelayDisplay (delay/ammo in the weapon list), HudShift (shift of ≈385 HUD
coordinates for widescreen; keyed by retail addresses), numeric, ranks, storyfactions, Territory,
TurretZoom, MultiIntro, WheelScroll, Zone, rus_chat/cyrillic_chat, HUDless, Hail, callsign, MPRep,
gls_workaround.

## Implementation principles (plan)

- One compile-time switch per function, `#if` rather than `#ifdef`; every place is marked with a
  `// HD:` comment; by default the vanilla build contains no HD code.
- Numeric parameters (lod_scale, TurretZoom arguments, HudShift positions) are read at start-up from
  the same HD files the plugins use, so the existing HD configuration works as is. The presence of
  a plugin in `dacom.ini` served as the on/off switch; in the port such an entry must be recognised
  together with the arguments of its line, while an unknown name must still fail loudly.
- Constants that retail reads from the shared `.rdata` are separate literals per reader in the port;
  the HD value has to be applied to each reader explicitly. HudShift needs a layer of named
  constants instead of literals.
- HD branches are intentional functions, not retail debt: they are not listed in `deviations.md` and
  do not change the vanilla function's status in the tracker. An item whose hook function is not yet
  `ready_verified` is first brought to vanilla accuracy.

## Owner decisions (2026-10-15)

1. HD bugs are carried over by meaning, not byte for byte: the enemy-targeting hint (J1) stays
   working (only the FDUMP is removed); the 0.85 factor applies only to `HUD_Core::DrawBars`, not
   to the Sun corona or the engine sound; the frame limiter stays `Sleep(1)` (if needed, a wait on a
   high-resolution timer, not a busy-wait); Cyrillic input only for real cp1251, Latin-1 is not
   broken; `%Z` in `FmtStrSprintf` is changed globally only if the Zone texts require it.
2. Patches that only produce log strings are not carried over.
3. FLSharp 0.8.0 behaviour as installed; 0.9.2 goes to the backlog.
4. Multiplayer items (synchronisation, chat, server list, MPRep, FLHook) — after the beta.

## What the port has

- The "Hor+" widescreen mode — `FREELANCER_WIDESCREEN_SUPPORT` (the `FreelancerWidescreenSupport`
  property in `src/Directory.Build.props`, enabled by default): cameras widen the frustum
  horizontally, the Win3D interface stays 4:3, the corner HUD windows are moved towards the screen
  edges by a camera shift (`widescreen.h`, `hud_widescreen.*`). This is the port's own mechanism that
  replaces the jflp recalculation and HudShift; it does not repeat them byte for byte. It is described
  in [re/freelancer.md](re/freelancer.md), the deviations in [re/deviations.md](re/deviations.md).
- Everything else (`hd_edition.h`, `FL_HD_*`, the `FreelancerEdition` property, the `binhd\`
  directory, `FHdConfig`, plugin recognition in dacom) is **not implemented**.

## Backlog outside the installed version

FLSharp 0.9.2 (fixes for the dealer crash and the server filter, waypoint names, group relations,
cursor, camera, light limit, etc.) and the new flplusplus keys (`touchpad_support`,
`confine_cursor`, `vsync`, `max_fps`, `direct_ips`). Installer options not applied in the
installation (LevelRequirements, AdvWideHUD, SkippableCutscenes, TopDownTargetView,
Windowed/Borderless, DoNotPauseOnAltTab) are candidates for runtime settings.
