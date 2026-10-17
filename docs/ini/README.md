# INI reference

**English** · [Русский](../ru/ini/README.md)

Freelancer is configured almost entirely through INI-style files: the engine (`DACOM.INI`), the
master game file (`freelancer.ini`) and several thousand data tables that `freelancer.ini` and
other data files point at. This folder documents, for every place where the code reads such a file,
**which sections and keys are read, with what type, default and allowed values, and which source
file reads them.**

Everything here was written from the source in `src/` (the code is the ground truth). Where the
code does not validate a value, the page says *not validated*. Meanings that could only be inferred
from names or from the surrounding code are marked as such. The retail data shipped with the game is
not reproduced; examples are short and synthetic.

Start with [parsers.md](parsers.md) — it explains the two parsers, the text and binary (BINI)
formats, comment/`@include` syntax, positional values and how file paths are resolved. All other
pages assume it.

## Contents

| Area | Page | What it covers |
|------|------|----------------|
| Parsing | [parsers.md](parsers.md) | `INI_Reader` and the DACOM `IProfileParser`; value parsing rules; path resolution |
| Engine | [engine-dacom.md](engine-dacom.md) | `DACOM.INI` / `dacomsrv.ini`: `[Libraries]`, `DllPath`, `DACOM` env var, and the sections of every engine module |
| Master file | [freelancer-ini.md](freelancer-ini.md) | `freelancer.ini`: every section/key and the `[Data]` file lists |
| State | [saves-and-initial-world.md](saves-and-initial-world.md) | `InitialWorld.ini`, `.fl` save games, new-character templates |
| Discovery | [data-driven-files.md](data-driven-files.md) | INI files that are only found because another INI names them |
| World | [world-data.md](world-data.md) | `universe.ini`, systems (Common side), bases, rooms, markets, costumes, new character, fuses, reputation |
| Archetypes | [archetypes-and-equipment.md](archetypes-and-equipment.md) | archetype loader, `inherit`, equipment sections |
| | [archetypes-weapons.md](archetypes-weapons.md) | guns, munitions, mines, countermeasures; hardpoint-type names |
| | [archetypes-ships-solars-asteroids.md](archetypes-ships-solars-asteroids.md) | ships, solars, asteroids, collision groups, asteroid fields |
| | [goods-loadouts-constants.md](goods-loadouts-constants.md) | `constants.ini`, goods, loadouts |
| Content (single-player logic) | [systems-and-bases.md](systems-and-bases.md) | zones, jump data, shortest-path tables, `mBases.ini`, news |
| | [ai-and-encounters.md](ai-and-encounters.md) | pilot personalities, formations, encounters, factions, NPC ships, loot |
| | [missions.md](missions.md) | mission script files; [conditions](missions-conditions.md) · [actions](missions-actions.md) · [object lists](missions-objlist.md) · [saves](missions-saves.md) · [support tables](missions-support.md) |
| | [random-missions.md](random-missions.md) | random-mission tables and vignette generator |
| Server | [server.md](server.md) | `FLServer.exe` / `Server.dll` start-up configuration |
| Client | [client-ui-config.md](client-ui-config.md) | key map, preferences, performance options, start-up/login, fonts, cursors, cameras |
| | [client-ui-config-windows.md](client-ui-config-windows.md) | window definitions, HUD, rollover/infocard/knowledge tables |
| | [client-world-fx.md](client-world-fx.md) | client side of system files, star sphere, asteroids, nebulae |
| | [client-world-fx-effects.md](client-world-fx-effects.md) | effects, light animations, gate/jump effects, client fuses |
| | [client-world-fx-archetypes.md](client-world-fx-archetypes.md) | client-only keys of archetype records, `[LOD]` |
| | [client-audio-misc.md](client-audio-misc.md) | sound and voice archetypes, effect types, shape databases, debris, graphs |

## Page conventions

- Each page starts with its purpose and a *File location* paragraph (file name, how the path is built).
- Key tables use the columns **Key · Type · Default · Meaning / allowed values · Source**; *Source*
  links to the reading code.
- Positional values (comma-separated lists read by index) are listed in index order.
- Keys that may repeat (one line per entry) are called out in the page text.

## Keeping it current

When you add or change an INI read in `src/`, update the page of its area in the same change.
The pages were written by reading the code, not by running the game against every data file.
