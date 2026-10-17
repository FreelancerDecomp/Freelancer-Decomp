# Physics: `PhySys`, `CObject`, IVP

In retail, physics is performed by Ipion Virtual Physics (IVP), statically linked into `Common.dll`; `PhySys`
is a thin layer between game objects (`CObject`/`CShip`/`CBeam`/...) and IVP. In the port `PhySys` lives in
`src/Common/common_physys*.cpp`, the world in `common_physicalsystem.cpp`, and IVP itself is a separate tree
`src/IVP/` (linked into `Common.dll`). The document describes the structure of the layer and its contracts; the status of
individual functions is in decomp-tracker.

## The world (`PhysicalSystem`)

- `PhySys::Startup(systemId)` creates (or finds) a `PhysicalSystem` in `std::map<unsigned, PhysicalSystem>`
  and increments the reference counter; `Shutdown` decrements it and at zero deletes the IVP environment (the material -
  when no world is left). The class (RTTI confirmed): `PhysicalSystem : IVP_Listener_Object,
  IVP_Listener_Collision, IVP_Collision_Filter_Coll_Group_Ident`; fields - system id, `IVP_Environment*`,
  a vector of `PhySys::CollisionEvent` (filled by `event_pre_collision`, cleared every tick, read by
  `GetCollisions`), a `set<CObject*>` of moving objects (`MovingObjectCount`), a reference counter.
- The first `AddRef` builds the environment: creation parameters with the world's collision filter and a
  `PhyPerfCounter` counter, `create_environment("digitalanvil", 0x11c2de34)`, PSI step = `1/PHYSICAL_SIM_RATE`,
  zero gravity, both listeners, anomaly limits and the range manager's look-ahead time
  (`RMGR_LOOK_AHEAD_*`; values from `[PhySysConsts]`, before the INI is loaded they equal
  `2.1 / PHYSICAL_SIM_RATE`).
- `PhySys::Update(systemId, dt)`: in single player - one `simulate_dtime` step with `dt`, at most
  `0.0999`; in multiplayer - a `double` accumulator with steps of `0.0999`. After the step the pose of each
  moving object is copied from the IVP body into the `CObject`.
- The physics constants are `[PhySysConsts]` in `constants.ini` (`PHYSICAL_SIM_RATE`, `ANOM_LIMITS_*`,
  `RMGR_*`, `DEFAULT_*_DAMPING`, `GOLEM_*`, `MATERIAL_FRICTION/ELASTICITY`, `MAX_SPAWNED_MINDIST_COUNT`, `RC_*`);
  the value of `MAX_SPAWNED_MINDIST_COUNT` is copied into `IVP_Mindist_Settings` (global `.data:0658aa10`).

## Objects and bodies

- `CObject` (`0x9c`) holds the class (`Class`), the archetype, a reference counter, a pointer to the IVP body, the part
  map (`PartMap`: part id -> engine instance), a list of phantoms, `PhyCollisionStateManager*`, advisors
  (`advise`). The object registry is lists per class; `Alloc(class)` creates an object of the required type
  (`0x503` - `CShip`, `0x2001` - `CEquipmentObj`, `0x67` - `CBeam`, `0xa7` - `CGuided`, ...),
  `Find`/`FindFirst`/`FindNext` walk the list. The destructor detaches advisors and phantoms,
  calls `PhySys::DeactivatePhysics` and `Root::Release`.
- A body is created by `PhySys::BuildIVP_Real(object, CreateParms)` (a hull from `.sur`, `IVP_Real_Object`
  through the builders in `src/IVP/ivp_compact_builder`) or `BuildIVP_Sphere`; `CreateParms` is 13 dwords.
  Mass, inertia, damping, impulses, `Attach`/`Detach` (compound cores), `BeamR`,
  collision groups and states go through the methods of `IVP_Real_Object`/`IVP_Core`.
- Phantoms (`PhySys::Phantom`) are an IVP body + a `CObjectCB` listener; queries are `FindRayCollisions`,
  `FindRayIntersect`, `FindSphereCollisions` (`IVP_Ray_Solver_Min_Hash`, `IVP_Sphere_Object_Query`),
  `GenerateCollisions` for rays (`PhysicalSystem::GenerateCollisions`).
- Controllers (`PhySys::Controller`, `RCBegin/RCSet/RCEnd` for remote correction) work with the first
  registered core; `CRemotePhysicsSimulation` is the network part.
- `PhyArch` stores the parts from `.sur` (extents, `!fxd`, the hardpoint list); `Root::get_phy_part(id)`
  looks up a part, `GetDefaultPart()` returns the unit box.

## IVP in the port (`src/IVP`)

| Directory | Contents |
|---|---|
| `ivp_physics` | environment, cores, objects, controllers, contact point/friction, impulses, materials, listeners, simulation manager |
| `ivp_collision` | mindist (creation, minimization, solver), listmanager, ray/sphere queries, ledge solvers |
| `ivp_compact_builder` | compact hull builders (pointsoup, ledgesoup, polygon_convex) |
| `ivp_utility` | hashes, vectors, memory, messages |

Retail <-> Ipion correspondences confirmed by the listing: `IVP_ListManager` = IVP_Friction_System,
`IVP_Mindist_PairRecord` = IVP_Contact_Point, the port's `IVP_Mindist` (0xd8) = IVP_Impact_Solver_Long_Term;
`IVP_Linear_Constraint_Solver`/`IVP_Great_Matrix_Many_Zero` (`ivp_great_matrix.*`) is the linear
friction solver (Dantzig-like). Places where IVP is not fully ported are marked in the sources
`FULLPORT-DEFERRED[IVP]` and listed in the tracker.

## Known discrepancies

- The former home-grown solver (`SolverStep` etc.) has been removed; object motion is provided by IVP only.
- Retail's `Heap::Malloc` pools are replaced by `new`; containers are modern STL; `CEquipManager` stores
  its lists by pointer.
- Per-function details (`ready_unverified`, `partial`) and open debts are in the tracker and in
  [deviations.md](deviations.md).
