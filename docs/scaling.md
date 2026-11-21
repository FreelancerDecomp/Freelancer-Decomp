# Scaling: many players, many NPCs

Status: planning document. The fork is still retail-compatible. This file tracks:

- **Part A**: algorithmic fixes that are allowed today, because they change no observable behaviour;
- **Part B**: what would have to change **if** we drop retail compatibility to raise the player cap
  (target 1000 players, possibly more later) and to run many more NPCs.

Paths are relative to `src/`. References name functions rather than line numbers, because the code
moves. "N" is the number of objects (ships, missiles, loot) in a system, "P" the number of connected
players.

## Part A — safe now (behaviour-identical)

Each fix is a side index next to an existing container. The container, its order, struct layouts and
the packets stay as they are; only the search stops being a walk. **Done** items are on master; the
rest are open.

| # | Where | Today | Fix | Equivalence conditions |
|---|-------|-------|-----|------------------------|
| A1 | **Done (#9).** `ClientObjDb_FindObject` / `FindObjectInKind` (`freelancer/Client/client_objdb.cpp`) | Walks the kind's intrusive list (kind 0: kinds 1, 4, 5, 6, 7, 2). Called for every incoming position update (`IClientImpl::SendCommonUpdateObject`), every destroy, launcher lookups, and per HUD entry per frame (`hud_targetable_objects.cpp`). O(N) per call, about O(N²) per update interval. | Per-kind map id → object, kept in `LinkGameobj` / `UnlinkGameobj`; kind 0 does the lookups in the same kind order. | First hit in list order wins; the id must be final when the object is linked; the map lives outside `FGameobjList`. |
| A2 | **Done (#9).** `IObjDB::FindById` (`client_objdb.cpp`) | Same walk. Common uses it for fire control, pathfinding and timers. | Route through A1. | As A1. |
| A3 | `ClientObjDb_FindByCObject` (`client_objdb.cpp`) | Walks the bucket for the matching `cobject()`, once per collision event. | Map `CObject*` → object, kept with A1. | `cobject()` stable while linked. |
| A4 | `CObject::Find` (both overloads) and `RemoveFromClassList` (`Common/common_cobject.cpp`) | Walk `G_ClassLists`; removal is O(n). Used by client and server. | Per-class maps to list iterators. The `std::list` and the shared `FindFirst`/`FindNext` cursor stay. | `CSimple::Id` and `EngineInstanceId` are plain fields: every write after the object is listed must update the map. First match in list order, with its `AddRef`. |
| A5 | `FSoundManager::FindInstanceByKey` / `FindAttached` (`SoundManager/SoundManager.cpp`) | Linear walk per lookup; `MixerTick` does it per source, so O(sources × instances) per frame. | Map `PlayKey` → node. | First match in list order; voice assignment unchanged because the lists stay. |
| A6 | Contact list watch-list merge in `RebuildContacts` (`freelancer/Client/hud_contact_list.cpp`) | O(watched × contacts) per rebuild (every 3 s). | Temporary map during the merge. | Low priority; the watch list is small. |
| A7 | **Mostly done (#10)**: `ObjectList::find_by_id` answers from a side table, so a miss now costs about 7 hash lookups per loaded system plus the in-transit walk. `Server_FindObject`, `StarSystem::find_object` (`Server/server_classes.cpp`) | Before #10: one-entry cache, then the cached system's 7 lists, then the in-transit list, then **every system**: up to O(all objects in the universe) per call. Used by the observer, the near-ships pass and collision resolution. | Remaining: (1) skip systems whose lists are all empty (every universe system is in `SysDB::SysMap`, loaded or not); (2) update #10's per-list index on each edit instead of marking it stale (every death moves an object to `Deferred`, so busy lists are often stale). A global id map is not worth it: it saves hash lookups per system, not per object, and is hard to keep exact (lists appended before their id is readable, systems re-created at the same address). | Keep the one-entry cache, its hit/miss counters and every write to it. When an id exists in more than one place, return what the walk would. |
| A8 | `StarSystem::sp_obj_update` (`Server/server_classes.cpp`) | Walks `Ships` for every incoming client position update. | Per-system map id → ship. | First match in list order. |
| A9 | **Done (#34).** `SystemPlayerView_SendObjectUpdate` and other id lookups in a view's tracked list (`Server/server_classes.cpp`, `Server/server_observer.cpp`) | For each incoming player update, **each other player's view** walks its tracked list (every object in the system): O(P × N) per update, O(P² × N) per tick. The worst hot path at 1000 players. | Per-view map id → tracked node. The fan-out over players stays. | Sends stay in the same order. Multiplayer only (single player returns early), so FLRegress does not cover it. |
| A10 | `CDPServer` proxy registry (`DALib/CDPServer.cpp`); `OnConnect` slot search (`RemoteClient/RemoteClient_transport.cpp`) | Linear walk of a `std::list`; first free slot of 128. | Map / free-slot list. | Low priority until B1; the chosen slot stays the lowest free one. |
| A11 | `G_PlayerListNames` (`freelancer/Client/client_iclient.cpp`) | Never erased: "remove" inserts. Grows over a session. | Probably leave: nothing reads it, so the cost is memory only, and it reproduces retail on purpose. Ask before changing. | — |

Algorithmic, but **not** safe today:

- **Ships in a spatial index for scanner queries** (`ScanList::add_in_range`, `Common/common_cescanner.cpp`).
  The scanner keeps the first 256 hits in list order, so a different candidate order changes *which*
  ships it keeps.
- **Spatial grid for the server observer's visibility test** (`SystemPlayerView_ShouldBeVisible`). It
  changes the order in which objects are found, and so the order of creates and resyncs on the wire.
- **Frustum culling of ships before enqueue** (`EnqueueObject`,
  `freelancer/Client/space_scene_obj_draw.cpp`). It changes what is queued and the sort input; the picture
  should be the same but is not provably identical.
- **Incremental chat table** (`FillPrivate`, `freelancer/Client/chat_control.cpp`). Retail rebuilds and
  `qsort`s; `qsort` is not stable, so an incremental version can reorder equal entries.

## Part B — when we break compatibility

### B1. Player cap (128 → 1000+)

| Item | Where | Today | Change |
|------|-------|-------|--------|
| Proxy table | `ClientProxies[128]` (`RemoteClient/ClientImpl.h`), loops in `RemoteClient_transport.cpp` | Fixed 128, loops hard-code 128. A 129th connection gets client id 0 but is still accepted. | Size from `MaxPlayers`; refuse a full server cleanly (`DPNERR_SESSIONFULL`). |
| Server settings | `FLServer/ServerConfigDlg.cpp` | Spinner 1..0x80, larger values rejected, text box 3 characters, message string `0xf006`. `FLServer.cfg` stores an int32, so the file format is fine. | Widen range, text limit and message. |
| DirectPlay | `SetMaxPlayers` in `RemoteClient_transport.cpp` and `DALib/CDPServer.cpp` | Not clamped; DALib default 0x10. | Check whether Microsoft DirectPlay 8 copes with 1000 connections; make it configurable in DPNet (port-only). |
| PlayerDB | `PlayerDB` (`Server/server_classes.cpp`) | `new PlayerData[MaxPlayers]` (0x418 bytes each); broadcasts loop 1..MaxPlayers. | Works; O(P) per event (see costs). |
| Chat ids | `ChatIdForPlayer` (`freelancer/Client/chat_init.h`), `GetPlayerId` (`include/common_server_types.h`), `MakeChatId` (`Server/server_classes.cpp`), `Server/server_chat.cpp` | Player id truncated to 16 bits; bits 16..23 are a channel tag. | Fine up to 65535 players; above that ids alias. |
| Vibe bitmap | `g_VibeBitmap` (`Server/server_pub.cpp`) | 4096 bits; allocates per-player reputation ids. | Fine for 1000; grow before about 4000 players. |
| Formation id list | `AddId` in the MISCOBJUPDATE codec (`remote_server_channel_codecs.cpp`) | Count is one byte. | Only matters for formations above 255. |

The client has no array sized for 128 players, so a server with a higher cap may well work with retail
clients on the wire. The costs below are what stop it:

- Login: the newcomer gets 4 packets per existing player and everyone gets 4 about the newcomer
  (`PlayerDB`, `Server/server_classes.cpp`). About a million packets to fill a 1000-player server.
- Every join, leave, group or system change makes the client rebuild and sort its whole chat table on
  the next frame (`ChatControlRefreshAll` / `FillPrivate`, `freelancer/Client/chat_control.cpp`). Retail
  builds that array with `alloca`.
- A group or system change of any player is broadcast to all (`PlayerDB::SendGroupID` / `SendSystemID`).

### B2. Many ships near the client (players and NPCs alike)

Hard caps; things silently disappear when they fill up:

| Cap | Where | Effect | Change |
|-----|-------|--------|--------|
| Scanner list 256 | `ScanList` (`include/common_cequip.h`), `ScanList::add_in_range` | Radar and targeting miss ships; the 256 kept are first in list order, not nearest. Applies to NPC scanners too. | Layout shared across DLLs: grow it or use a vector, and prefer the nearest. |
| Particle pools, 8 × 1000 | `alchemy/alchemy_common.cpp` | New particles not emitted (engine trails, weapon fire). | Pool size is INI-configurable; raise it in data, or let pools grow. |
| Sound channels | `FSoundManager::MixerTick` (`SoundManager/SoundManager.cpp`) | Sources beyond the cap muted in attach order. | Choose voices by distance and priority. |
| IVP spawned mindists 1000 | `ivp_mindist_settings.cpp` | Coarser collision. | Already INI-configurable (`MAX_SPAWNED_MINDIST_COUNT`). |
| Spatial query result 2048 | `FSpatialQueryResult` (`include/common_spatial.h`) | Solars only on the client. | Grow if more objects go into the index. |

Per-frame costs with no level of detail:

- `ClientObjDb_UpdateAll` updates every ship, missile and loot item each frame; only solars are limited
  by distance.
- `EnqueueByManagerKind` / `EnqueueObject` queue every ship within 20000 units, then sort.
- `ScanList::add_in_range` walks every ship: ships are not in the client's spatial index (solars only).
- `RebuildContacts` walks and sorts all ships every 3 seconds.

### B3. Server and NPCs

The server runs locally in single player too, so for NPC counts it is usually the first limit.

Interest management (which updates each client gets):

- One view per player per system, and each view tracks **every** object in its system
  (`AddTrackedObject`). Every tick it distance-tests all of them with fixed radii: 2.5 km for unowned
  objects (NPCs), 5 km for strangers, 10 km for group mates (`SystemPlayerView_ShouldBeVisible`). Cost
  O(players in system × objects in system) per tick.
- NPC resyncs are kept in a list sorted by insertion sort (`Server/server_observer.cpp`): O(k²) in the
  number of objects due for a resync.
- No byte budget: a governor stops all sends at `GetLinkSaturation() >= 0.9`, oldest updates first.
- Player ship updates fan out to every other player in the system (`StarSystem::sp_obj_update`), even
  players 100 km away.

Change: a spatial grid per system with per-cell subscriptions, so a view only looks at nearby cells, and
a per-client byte budget with priority by distance instead of the 0.9 gate.

Hard caps:

| Cap | Where | Effect | Change |
|-----|-------|--------|--------|
| Near-ships query 0x800 | `StarSystem_UpdateNearShips` (`Server/IServerImpl.cpp`) | Results beyond 2048 dropped. | Vector. |
| `scan_objects` bucket 0x800; `pub::` callers 0x400 | `Server/server_classes.cpp`, `Server/server_pub.cpp` | Same. | Same; the `pub::` signatures are a plugin API. |
| Object id counter | `CFLIDMaker::AllocateObjectID` (`Common/common_id.cpp`) | `NextDynamicId++ & 0x3FFFFFFF`: never reused, no check against live objects, can return 0 (= "none"). Projectiles and loot use the same counter, so a busy long-running server wraps after about 1.07 billion allocations and can collide with live objects. | Free list or generation scheme; skip 0. A 64-bit id is a wire change. |

Per-tick costs:

- `StarSystem_Update` ticks 7 object lists linearly; steps are capped at 0.2 s, so a slow frame runs extra
  steps (a risk of a spiral as N grows).
- Spawning is data-driven: `density` / `repop_time` (`Content/Zones.cpp`), `max_battle_size`
  (`Content/Encounters.cpp`), density checks in `Encounters.cpp` and `Populators.cpp`. Raising these in
  data is the first step to more NPCs, and on its own keeps compatibility.
- Collision resolution looks up both objects through `Server_FindObject` (A7).

## Not investigated yet

- Pilot and AI cost per NPC per tick on the server.
- Draw internals in Thorn and EngBase; per-ship cost inside `FClientShipGameobj::update`.
- Whether Microsoft DirectPlay 8 handles 1000 connections.
- Measurements: nothing here is profiled. An FLRegress stage that spawns N ships and reports frame
  time would turn these estimates into numbers.
