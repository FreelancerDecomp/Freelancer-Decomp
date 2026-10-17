# Физика: `PhySys`, `CObject`, IVP

В retail физику выполняет Ipion Virtual Physics (IVP), статически влинкованная в `Common.dll`; `PhySys`
— тонкий слой между игровыми объектами (`CObject`/`CShip`/`CBeam`/…) и IVP. В порте `PhySys` лежит в
`src/Common/common_physys*.cpp`, мир — в `common_physicalsystem.cpp`, а сам IVP — отдельным деревом
`src/IVP/` (линкуется в `Common.dll`). Документ описывает устройство слоя и контракты; статус
конкретных функций — в decomp-tracker.

## Мир (`PhysicalSystem`)

- `PhySys::Startup(systemId)` создаёт (или находит) `PhysicalSystem` в `std::map<unsigned, PhysicalSystem>`
  и увеличивает счётчик ссылок; `Shutdown` уменьшает его и при нуле удаляет окружение IVP (материал —
  когда не осталось ни одного мира). Класс (RTTI подтверждён): `PhysicalSystem : IVP_Listener_Object,
  IVP_Listener_Collision, IVP_Collision_Filter_Coll_Group_Ident`; поля — id системы, `IVP_Environment*`,
  вектор `PhySys::CollisionEvent` (заполняет `event_pre_collision`, очищается каждый тик, читает
  `GetCollisions`), `set<CObject*>` движущихся объектов (`MovingObjectCount`), счётчик ссылок.
- Первое `AddRef` строит окружение: параметры создания с фильтром столкновений мира и счётчиком
  `PhyPerfCounter`, `create_environment("digitalanvil", 0x11c2de34)`, шаг PSI = `1/PHYSICAL_SIM_RATE`,
  нулевая гравитация, оба слушателя, пределы аномалий и время упреждения менеджера диапазонов
  (`RMGR_LOOK_AHEAD_*`; значения из `[PhySysConsts]`, до загрузки INI они равны
  `2.1 / PHYSICAL_SIM_RATE`).
- `PhySys::Update(systemId, dt)`: в одиночной игре — один шаг `simulate_dtime` с `dt`, не больше
  `0.0999`; в мультиплеере — накопитель `double` с шагами по `0.0999`. После шага поза каждого
  движущегося объекта копируется из тела IVP в `CObject`.
- Константы физики — `[PhySysConsts]` в `constants.ini` (`PHYSICAL_SIM_RATE`, `ANOM_LIMITS_*`,
  `RMGR_*`, `DEFAULT_*_DAMPING`, `GOLEM_*`, `MATERIAL_FRICTION/ELASTICITY`, `MAX_SPAWNED_MINDIST_COUNT`, `RC_*`);
  значение `MAX_SPAWNED_MINDIST_COUNT` копируется в `IVP_Mindist_Settings` (глобал `.data:0658aa10`).

## Объекты и тела

- `CObject` (`0x9c`) хранит класс (`Class`), архетип, счётчик ссылок, указатель на тело IVP, карту частей
  (`PartMap`: id части → экземпляр движка), список фантомов, `PhyCollisionStateManager*`, советников
  (`advise`). Реестр объектов — списки по классам; `Alloc(class)` создаёт объект нужного типа
  (`0x503` — `CShip`, `0x2001` — `CEquipmentObj`, `0x67` — `CBeam`, `0xa7` — `CGuided`, …),
  `Find`/`FindFirst`/`FindNext` обходят список. Деструктор отцепляет советников и фантомы,
  вызывает `PhySys::DeactivatePhysics` и `Root::Release`.
- Тело создаётся `PhySys::BuildIVP_Real(object, CreateParms)` (оболочка из `.sur`, `IVP_Real_Object`
  через построители `src/IVP/ivp_compact_builder`) или `BuildIVP_Sphere`; `CreateParms` — 13 dword.
  Масса, инерция, демпфирование, импульсы, `Attach`/`Detach` (составные ядра), `BeamR`,
  коллизионные группы и состояния идут через методы `IVP_Real_Object`/`IVP_Core`.
- Фантомы (`PhySys::Phantom`) — тело IVP + слушатель `CObjectCB`; запросы — `FindRayCollisions`,
  `FindRayIntersect`, `FindSphereCollisions` (`IVP_Ray_Solver_Min_Hash`, `IVP_Sphere_Object_Query`),
  `GenerateCollisions` для лучей (`PhysicalSystem::GenerateCollisions`).
- Контроллеры (`PhySys::Controller`, `RCBegin/RCSet/RCEnd` для удалённой коррекции) работают с первым
  зарегистрированным ядром; `CRemotePhysicsSimulation` — сетевая часть.
- `PhyArch` хранит части из `.sur` (экстенты, `!fxd`, список hardpoint'ов); `Root::get_phy_part(id)`
  ищет часть, `GetDefaultPart()` отдаёт единичный бокс.

## IVP в порте (`src/IVP`)

| Каталог | Содержимое |
|---|---|
| `ivp_physics` | окружение, ядра, объекты, контроллеры, contact point/трение, импульсы, материалы, слушатели, менеджер симуляции |
| `ivp_collision` | mindist (создание, минимизация, решатель), listmanager, ray/sphere-запросы, ledge-решатели |
| `ivp_compact_builder` | построители компактных оболочек (pointsoup, ledgesoup, polygon_convex) |
| `ivp_utility` | хэши, векторы, память, сообщения |

Соответствия retail ↔ Ipion, подтверждённые листингом: `IVP_ListManager` = IVP_Friction_System,
`IVP_Mindist_PairRecord` = IVP_Contact_Point, `IVP_Mindist` порта (0xd8) = IVP_Impact_Solver_Long_Term;
`IVP_Linear_Constraint_Solver`/`IVP_Great_Matrix_Many_Zero` (`ivp_great_matrix.*`) — линейный
решатель трения (Dantzig-подобный). Места, где IVP портирован не до конца, помечены в исходниках
`FULLPORT-DEFERRED[IVP]` и перечислены в tracker.

## Известные расхождения

- Прежний самописный решатель (`SolverStep` и пр.) удалён; движение объектов обеспечивает только IVP.
- `Heap::Malloc`-пулы retail заменены `new`; контейнеры — современный STL; `CEquipManager` хранит
  списки по указателю.
- Подробности по функциям (`ready_unverified`, `partial`) и открытые долги — в tracker и в
  [deviations.md](deviations.md).
