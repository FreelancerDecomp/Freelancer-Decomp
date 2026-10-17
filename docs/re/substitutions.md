# Substitutions: where the exe does its own thing instead of calling the ported modules

This document explains one of the project's main risks and how to look for it. The client is `freelancer.exe` plus ~30 DLLs; there are two dishonest ways to port "up to the picture", and both look like progress. It is for whoever decides what counts as a defect: substitutions are accounted for as debt, not as working code. The ledger of open debt is [deviations.md](deviations.md), the status of individual functions is in decomp-tracker; this page covers only the concepts and the method. (The earlier per-function audits of 2026-09 are obsolete — most of the mechanisms they describe have since been removed — and were deleted; they are preserved in git history.)

## Two diagnoses

**Active substitution** — the exe (or a module) runs our own mechanism that retail does not have, instead of retail's: our own UI layer instead of the `WinControl`/NeuroNet tree, our own list of proxy objects instead of `IObjDB`, our own font rasterizer instead of `TRANode`, a private "valve" in the frame (a "draw only if…" condition), a vtable stub that returns "success". Such code is not improved — it is **dismantled** and replaced with a call into the real module. A substitution not recorded in `deviations.md` with the retail mechanism, our mechanism, the reason and the removal condition is a defect.

**Dead capital** — the module is ported correctly, but the client does not call it: there are no calls from the exe side. There is nothing to fix in the module; **the calls must be built on the caller's side**, strictly from the listing. Consequence: edits to such a module do not change what is visible on screen, and "the function is `ready_verified`" does not yet mean "the function runs in the game".

The third state is a **hole**: the retail path is not ported and there is no substitution, it simply does not execute. It is right to leave it visible (`simplified` + a ledger entry) rather than close it with similar-looking code.

## Categories (where substitutions were searched for and where they occur)

- **Frame and pipeline** — render pass order, RP8 states, the batcher, Win3D; typical substitutions are private gates on present and scene.
- **HUD and menus** — NeuroNet/WinControl, buttons, cursor, fonts and IDS, the `PAGE_*` screens; the typical substitution is a "flat" widget layer that draws directly.
- **World, objects, physics, server** — `IObjDB`, `CObject`/`PhySys`, spawn and player loadout, `Server.dll`/`rpclocal.dll`; the typical substitution is proxy objects "for the picture" with no obligations to the object database.
- **Engine and effects** — Alchemy, Thorn, EngBase, Shading, sound, DALib; workarounds occur at the boundaries (particle draw dispatch, attach-object identity).
- **Containers and CRT** — `std::map`/`std::vector` instead of VC6 trees and vector, UCRT instead of MSVCRT: allowed as a conscious architecture (insert/lookup/erase behaviour must match the listing); CRT differences are covered separately, see `crt_compat_*.md`.

## How to search and verify

- "I see no call in the port" proves nothing: the port is incomplete and not-yet-ported modules may do the calling. Code is dead only if it has no calls **in retail** (listing XREFs, raw grep).
- A substitution shows up as a double chain: an exe function that has a retail address (`FUN_...`) but whose body does not follow the listing, or a call that bypasses the interface of a module that is supposed to make it. Look for empty bodies next to a `FUN_`/`LAB_` comment and for `FULLPORT-DEFERRED` markers.
- Dead capital is measured by callers: the tracker's `list_references` (it misses calls — cross-check against `;XREF` in the `.txt`), grep over `src/freelancer` (raw `Vtable[N]` sites included).
- A candidate is closed only when `src/` contains code that implements the retail behaviour, with the file and the listing address given; "probably fixed already" is not grounds.
