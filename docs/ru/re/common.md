# Common.dll — общая библиотека игровых данных и объектов

`Common.dll` — общий runtime клиента и сервера: идентификаторы, чтение INI, загрузка всех игровых
таблиц (архетипы, товары, вселенная, репутация, костюмы, комнаты), объектная модель кораблей и
оборудования, обёртка над физикой и контракт AI. Документ нужен тому, кто портирует или правит
модуль: он описывает архитектуру и форматы данных, а не отдельные функции — построчные факты и
статус каждой функции лежат в decomp-tracker (модуль `Common.dll`).

Связанные документы: [common_archetype.md](common_archetype.md) (архетипы и клиентские компаньоны),
[common_physys.md](common_physys.md) (физика), [cross_dll_protocol.md](cross_dll_protocol.md);
интерфейсы `IBehaviorManager`/`IObjInspect` объявлены в `src/include/common_iface.h`.

## Линковка и соглашения

- Это не DACOM-компонент: `RegisterComponent` не вызывается, в `[Libraries]` `bin/DACOM.INI` его
  нет. `freelancer.exe`, `Server.dll`, `Content.dll`, `rpclocal.dll`, `RemoteClient.dll`,
  `FLServer.exe` линкуют `Common.lib` обычным импортом. Retail экспортирует ~3860 имён (exe берёт
  ~830 из них); экспорт порта сверяется с `dumpbin -exports EXE\Common.dll`.
- Свободные функции — `__cdecl`, методы классов — `__thiscall`; DACOM-`__stdcall` здесь не
  используется. Публичный ABI лежит плоско в `src/include/common_<подсистема>.h` (`COMMON_API`),
  то, что общее только для нескольких TU одной подсистемы, — в `src/Common/common_<подсистема>_internal.h`.
- Порт не воспроизводит VC6-раскладки STL и retail-кучу: контейнеры современные, смещения полей
  держатся там, где их читают другие модули (проверяются `static_assert`; смещения за контейнерами
  верны только при размере `std::` в Debug и `_ITERATOR_DEBUG_LEVEL=1` в Release — см.
  `src/Directory.Build.targets`).
- DllMain ничего не инициализирует (`DisableThreadLibraryCalls`). Реальный старт — вызовы
  `Async::Startup`, `Timing::init`, `DataConfig::startup`, `PhySys::Startup` из exe/Server.

## Раскладка исходников

Одна единица трансляции — один retail-класс (или тесная группа, которую retail держит в одном `.cpp`).

| Подсистема | Файлы `src/Common/` |
|---|---|
| Время, async, куча, ошибки | `common_timing`, `common_async`, `common_heap`, `common_errorhandler`, `common_constants` (`ReadConstants`), `common_misc` (пути, `SinglePlayer`, математика, `DllMain`) |
| ID и INI | `common_id`, `common_ini`, `common_textfile` (`FileMap`, `LineParser32`, `TextFile32`), `common_stringcache`, `common_fmtstr` |
| Загрузка данных | `common_dataconfig`, `common_archetypeloader`, `common_goodinfo`, `common_loadout`, `common_equipdesc`, `common_basedata`, `common_baseproperties`, `common_roomdata`, `common_roominfo`, `common_universe*`, `common_reputation`, `common_vibe`, `common_costume`, `common_fuses`, `common_newchar`, `common_rtcslider`, `common_pubstategraph` |
| Архетипы | `common_archetype`, `common_archetype_ships/_equipment/_weapons/_misc`, `common_phyarch` |
| Объекты | `common_cobject`, `common_engineobject`, `common_csimple`, `common_ceqobj`, `common_cship`, `common_csolar`, `common_cloot`, `common_cmine`, `common_cprojectile`, `common_cattachment`, `common_carchgroup`, `common_casteroid*`, `common_hierarchy`, `common_hardpointsummary`, `common_basewatcher` |
| Оборудование | `common_cequip` (`CEquip`, менеджер, обход) и по файлу на класс: `common_ce*.cpp`, `common_cattachedequip`, `common_tractorarm`, `common_csteering`, `common_laneaction`, `common_launchaction` |
| Физика | `common_physys*`, `common_physicalsystem`, `common_phycontroller`, `common_phycollisionstate`, `common_animdb`, `common_csys`; IVP — в `src/IVP/` |
| AI и контракт `pub::` | `common_behavior*`, `common_*behavior.cpp`, `common_*idea.cpp`, `common_systemmanager*`, `common_targetmanager`, `common_pathfind`, `common_aisteer`, `common_pub`, `common_iobjinspectimpl`, `common_actiondb` |
| Текст и RDL | `common_rect`, `common_textrender`, `common_rdlnodes`, `common_renderdisplaylist`, `common_rdlio` |
| Сеть-смежное | `common_marshalbuffer`, `common_statemachine`, `common_watchable`, `common_mpcritsec`, `common_playeraccount`, `common_exclusionzone`, `common_deadreckonedvector`, `common_remotephysics`, `common_tradeoffer` |

## Загрузка данных: `DataConfig`

`DataConfig::startup(iniPath, ICliObjFactory*, ProgressCB*)` — оркестратор: открывает
`freelancer.ini`, секцию `[Data]`, и по каждому ключу зовёт загрузчик. **Порядок загрузки — это
порядок строк в INI** (диспетчер — цепочка `is_value`, без учёта регистра); retail ставит `solar`
перед `universe`, потому что `Universe` смотрит `OBJECT_TYPE` соларов. Возврат `0` — успех, `1` —
`ProgressCB` потребовал прервать загрузку. Повторный `startup` только увеличивает счётчик ссылок.
`ProgressCB` вызывается на каждое значение `[Data]` до диспетчера (клиент через него грузит
`HUD=`, шрифты, звуки — ключи, которые Common сам не знает); клиент передаёт `ICliObjFactory`,
Server — `nullptr`. После успешного `open` всегда грузится `AI\state_graph.db`.

| Ключ `[Data]` | Загрузчик | Примечание |
|---|---|---|
| `universe` | `Universe::Startup` | путь относительный, `GetFileSysPath` внутри |
| `constants` | `ReadConstants` | то же |
| `equipment`, `solar`, `ships`, `asteroids`, `explosions` | `Archetype::LoadEquipment/LoadSolar/LoadShips/LoadAsteroids/LoadSimples` | несколько файлов на ключ; нулевое число новых записей — предупреждение, не abort |
| `loadouts` | `Loadout::Load` | |
| `bodyparts`, `costumes` | `LoadBodypartDescriptions`, `LoadCostumeDescriptions` | |
| `bases` | `BaseDataList_load` | ключ без файла: обход баз `Universe` |
| `goods`, `markets` | `GoodList_load`, `BaseDataList_load_market_data` | |
| `PetalDB` | `PetalDB_load` | |
| `Groups` | `Reputation::Startup` | |
| `fuses` | `FuseDB::AddFuseINIFile` | только запоминает путь |
| `WeaponModDB` | загрузка `[WeaponType]` | |
| `NewCharDB`, `RtcSlider` | `NewChar::TheDB->Load`, `RtcSlider::load` | |

Остальные ключи (`fonts`, `sounds`, `voices`, `effects`, `HUD`, …) DataConfig игнорирует. Порядок
`shutdown` — обратный: state graph, loadouts, архетипы, взрывы, костюмы, товары, Petal, базы,
вселенная, репутация, fuse, WeaponMod, NewChar.

## Идентификаторы

- **`CreateID(nickname)`** — основной хэш ников. `CFLIDMaker` (синглтон, `m_pTheMaker`) лениво строит
  таблицу 256 слов и считает CRC-16-IBM, расширенный до 30 бит (регистр 32 бит, ширина 30, полином
  `0x8005`, reflect-in), после чего результат разворачивается по байтам, сдвигается и получает
  старший бит: `id | 0x80000000`. Ник приводится к нижнему регистру (в порте — только ASCII).
  Примеры: `li_elite` → `0xB746B147`, `Li01` → `0x83A78E4F`, `ge_fighter` → `0x80410B80`,
  `player` → `0x8ABCE14E`, пустая строка → `0x80000000`, `nullptr` → `0`.
  Динамические ID объектов (`AllocateObjectID`) живут в 30 битах (`& 0x3FFFFFFF`).
- **Это не `DACOM_CRC::GetCRC32`**: тот хэш (dacom.dll) нужен только архетипам/hardpoint'ам.
- **`MakeId(name)`** — 16-битный хэш фракций: CRC-16-CCITT (`0x1021`, начало `0xFFFF`) по строке в
  нижнем регистре до 127 байт; используется в `Reputation`. `MakeLocationID(base, name)` —
  `CreateID("%X_%s")`.
- `ID_String` — один `unsigned` (0 = пусто); `get_string` не обратный хэш, а `0x<hex>` из
  ротационного буфера на 4 строки.

## INI: `INI_Reader`, `FileMap`, BINI

`INI_Reader` читает как текстовый INI, так и бинарный (`'BINI'`, заголовок 12 байт; параметры
тип+4 байта: 0 — байт, 1 — int32, 2 — float, 3 — смещение в таблице строк). `open` сам определяет
формат; `open_memory` всегда трактует буфер как текст. Имена секций и ключей сравниваются без учёта
регистра. Параметры значения — список через запятую (`get_value_string(i)`), пустой параметр
проверяется `is_value_empty(i)`. Указатель из `get_value_string` действителен только до следующего
`read_value()` — загрузчики обязаны копировать строки сразу (на этом ломались костюмы и комнаты).
Retail пишет в несколько 0x100-байтных буферов без границ; порт держит строки целиком и режет
только за большими пределами (см. запись в tracker). Компоновка полей `INI_Reader` — retail
(текстовая половина с `+0`, BINI-половина с `+0x1428`, флаг формата `+0x1564`), но размеры другие.

## Файловая система и пути

`GetFileSysPath(dest, relOrAbs, IFileSystem*)`: `nullptr` → пустая строка; путь, начинающийся с `\`
или с `X:`, копируется как есть (`/` абсолютным не считается); иначе берётся корень файловой системы
(`DALib::Data`, если не передана своя) через слот `+0x84` (`GetFileSysPath(char*, unsigned)`) и
склеивается `_makepath`. Рядом: `GetFullFilename`, `GetUserDataPath` (`Documents\My Games\Freelancer`),
`GetUserLocalDataPath` (`%LOCALAPPDATA%\Freelancer`), `GetScreenShotPath` (`Pictures\FreelancerShots`),
`DAEngine_create_filesystem/release_filesystem/open_filesystem` (дочерняя `IFileSystem` от `Data`;
для `.utf`/`.ale` DOS-реализация перепоручает файл UTF-реализации).

## Архетипы

Подробно — [common_archetype.md](common_archetype.md). Кратко: `Archetype::Load*` разбирают
INI в базы `ArchDB` по `CreateID(nickname)`; каждый объект получает клиентского компаньона
(`ICliObj`) из `ICliObjFactory`; меш подгружается позже через `Root::load`. `.sur`-файл рядом с
мешем несёт физические части (`PhyArch`): `"vers"`/float 2.0, затем на каждую часть id и чанки
`surf` (IVP-оболочка), `exts` (min/max), `!fxd`, `hpid` (список hardpoint'ов).

## Товары, лоадауты, базы, рынки, константы

- **Goods** (`[Good]`): `GoodInfo` по `CreateID(nickname)` и `ArchId`; категории `Commodity`,
  `Equipment`, `ShipHull`, `Ship`; цены (`price`, `good_*`/`bad_*`), `hull`, `addon` (`low|mid|high`),
  `combinable`, `free_ammo`, `ids_name`/`ids_info`. Дубликат ника прерывает блок.
- **Loadouts** (`[Loadout]`): `nickname`, `equip` (оборудование на hardpoint), `cargo`
  (арх, количество, hardpoint); идентификаторы экземпляров `EquipDesc` (0x20 байт) выдаются с
  `0x21`; объединяемый груз складывается.
- **Bases / markets**: `BaseDataList` строится обходом баз `Universe`; секции `[Base]`/`[BaseInfo]`
  (`start_room`, `price_variance`, `ship_repair_cost`, по умолчанию 0.33) и `[Room]`
  (`nickname`, `file`; ID комнаты — `CreateID("%X_%s")` от ID базы); рынок — `[BaseGood]`
  (`base`, параметры 0–6, запись с дефолтными значениями пропускается).
- **Constants** (`ReadConstants`): секции `Constants`, `EngineEquipConsts`, `ShieldEquipConsts`,
  `ThrusterEquipConsts`, `PhySysConsts`, `CommConsts`, `CollisionConsts` (`damage`,
  `max_impact_speed`). Неизвестный ключ — предупреждение «Unknown Constant definition».

## Вселенная

`Universe::Startup(universe.ini)`: создаёт дочернюю файловую систему на каталог `universe.ini`
(через `DALib::Dacom`), читает секции `[Time]` (`seconds_per_day` → `DaysPerSecond`), `[Base]`
(`nickname`, `file`, `system`, `strid_name`, `BGCS_base_run_by`, `ship_*`, `terrain_*`,
`autosave_forbidden`) и `[System]` (`nickname`, `file`, `pos`/`real_pos`, `NavMapScale`, `visit`,
`msg_id_prefix`, `strid_name`, `ids_info`). Для каждой системы открывается её `file=` (относительно
каталога вселенной): секции `[Zone]` (`nickname`, `pos`, `rotate`, `shape`
sphere/ellipsoid/box/cylinder/ring/mesh, `size`, `edge_fraction`, `property_flags`, `drag_modifier`,
`damage`, `interference`, `music`, `spacedust`, `path_label`, `visit`, `ids_name`/`ids_info`) и
`[Object]` (`nickname`, `archetype`, `pos`, `base`, `dock_with`, `goto`, `reputation`, `next_ring`/
`prev_ring`, `star`, `visit`). `goto` кладёт объект в карту ворот (`get_gate_system`); тип ворот
берётся из `OBJECT_TYPE` архетипа соляра. Зоны системы собираются в BSP-разбиение
(`ISpatialPartition`, `common_spatial.cpp`, строится в `SYSTEM::BuildZoneTree`). Запросы: `get_system/get_base/get_zone(+_id)`,
`GetFirst/NextSystem/Base`, `first_zone/next_zone`.

## Репутация, костюмы

- **Reputation** (`Groups = InitialWorld.ini`): секции `[Group]`; ключ группы — `MakeId(nickname)`
  (`TString<16>`), далее `ids_name`, `ids_info`, `ids_short_name`; отношения `rep = float, nickname`
  читает не `Startup`, а `Reputation::LoadFeelings` (его зовёт Server при загрузке мира/сейва).
  Самоотношение всегда 1.0. `IsStoryFaction` — фиксированный список `fc_ln_grp … fc_uk_grp`. Динамические
  записи `Reputation::Vibe` (stride 0x90) освобождает `CleanUpDatabases`.
- **Костюмы**: `bodyparts.ini` (типы `Body/Head/LeftHand/RightHand`, `[Skeleton] sex`,
  `[Accessory]`) и `costumes.ini` (`[Costume]` с `nickname`, частями и `accessory`). Общий
  `Costume` хранит ID в порядке head, body, lefthand, righthand; внутренние карты и
  `costume::Parts` — body, head, lefthand, righthand: на границе первые два переставляются
  (Server, Content и клиент используют один и тот же контракт).

## Комнаты и базы

`RoomData` (`[Room]`/`[Room_Info]`, `[Camera]`, `[Sound]`, `[CharacterPlacement]`,
`[PlayerShipPlacement]`, `[ForSaleShipPlacement]`, `[Hotspot]`, `[FlashlightSet]`) читает комнатный
INI лениво (`load()` один раз), набор объектов комнаты приходит из Thorn-сцены (`set_script`):
типы сущностей COMPOUND=1, DEFORMABLE=2, CAMERA=3, LIGHT=5, SETPOINT=7. Имя setpoint'а —
`zs|zg _ pc|npc|rpc|none _ сегмент _ зона [_ суффикс]` (например `zs_pc_MainHall_00_Desk`).
Устаревшие секции и ключи (`mesh`, `background_script*`, `[Light]`, `[Fog]`, …) принимаются с
предупреждением. `ThornScriptLoad` получает `IScriptEngine` по `"1.11_IScriptEngine"` — единственный
IID, который Common запрашивает сам. `HardpointSummary` использует слоты `DALib::Engine` для
перечисления hardpoint'ов составного меша.

## Fuse, Petal, WeaponMod

- **FuseDB** в Common — только базовые классы `Fuse`/`FuseAction` и список путей
  (`m_FuseINIFiles`); сами fuse-файлы читают потребители: клиент (`client_fuse.*`) и Server.
- **PetalDB** (`[ObjectTable]`): категория по имени значения (`room`=0, `cart`=5, `prop`=6),
  параметр 0 — nickname, 1 — меш, дальше до 16 дополнительных; `PetalInterfaceDatabase::get_category`
  принимает регистрозависимые `Room/Spaceship/Solar/Asteroid/Equipment/Equipment Cart/Prop`.
- **WeaponModDB** (`[WeaponType]`): `nickname`, затем `shield_mod = shield_nick, float`; поиск
  возвращает 1.0 при промахе.

## NewChar, RtcSlider, StateGraph

- **NewCharDB** (`NewChar::TheDB`): секции `[Faction]` (`nickname`, `rep_group`, `base`, `package`,
  `pilot`), `[Package]` (`nickname`, `strid_name/desc`, `ship`, `loadout`, `money`), `[Pilot]`
  (`nickname`, `body`, `comm`, `*.anim`, `voice`, `thumb`); четыре карты по `CreateID`, дубликат —
  первый побеждает. Потребители: `Server.dll` (создание персонажа по шаблону `NewCharacter.fl`) и
  клиентский экран выбора.
- **RtcSlider** (`[RtcSlider]`): `set = name, threshold` / `set_less`; `is_enabled(name)` сравнивает
  порог с ползунком; `get_fps_cap` — 20, 30 или 200 в зависимости от `fps_20_cap`/`fps_30_cap`.
- **`pub::StateGraph`** (`AI\state_graph.db`, текст): число графов, число поведений (должно быть 21),
  затем на запись имя, тип `LEADER`/`ESCORT` и матрица 21×21.

## Текст и RDL

`FmtStr` — связный список значений с типовыми метками (0x21 вложенный `FmtStr`, 0x4E блок из
5 dword, 0x42 база, 0x46 группа, 0x47 товар, 0x49 установка, 0x4C лут, 0x4F объект, 0x52
экземпляр репутации, 0x53 система, 0x5A зона, 0x64 int, 0x73 строка) с форматом
`flatten/unflatten` для сети. `RenderDisplayList` — список узлов `RDLNode` (`TextNode`, `TRANode`,
`PositionNode`, `JustifyNode`, `ImageNode`, `StyleNode`, `ClipNode`, `RDLRefNode`, …), которые
исполняются на `TextRenderContext`/`DeviceTRC` (GDI); стили — реестр `StyleCollection`.
`BinaryRDLReader/Writer` и `XMLReader/Writer` — абстрактные интерфейсы.

## Объекты и оборудование

Иерархия: `CObject` (пул по классам, `Alloc/Find`) → `CSimple` → `CEqObj` → `CShip`/`CSolar`;
`CProjectile` → `CMunition`/`CGuided`/`CBeam`/`CMine`/`CCounterMeasure`; `CLoot`, `CAsteroid`.
`EngineObject` связывает объект с экземпляром движка (EngBase). Оборудование: `CEquip` →
`CInternalEquip` (питание, двигатель, сканер, броня, tradelane) / `CExternalEquip` → `CAttachedEquip`
(`CELauncher` → `CEGun`, `CEShieldGenerator`, `CEThruster`, `CECargoPod`, …). `CAttachedEquip::Connect`
выбирает `CNonPhysAttachment` (hardpoint есть в списке родителя) или `CPhysAttachment`
(физическое тело на IVP). Обход оборудования — `CEquipManager::Traverse` по маске типов (внешние,
затем внутренние). `Reputation::Vibe` задаёт направленное отношение объектов: отношение A к B и
B к A могут различаться; HUD и `USER_CLOSEST_ENEMY` берут отношение контакта к игроку.
Сканер: `CSimple::ScannerInterference` хранит погружение в зону, а итоговую помеху даёт
`get_scanner_interference()` (с коэффициентом зоны и верхним пределом) — её используют дальность
сканера, радар и HUD.

## AI и контракт `pub::`

`IBehaviorManager` (поведения, идеи, `SystemManager`) и `pub::AI::*` (структуры `Personality`,
операции `Directive*Op`, `ContentCallback`/`DirectiveCallback`) — контракт между Common, Server и
Content; он объявлен в `src/include/common_pub.h` и `common_iface.h`. Content подставляет свои
`ContentCallback` (см. [content.md](content.md)), Server зовёт `set_content_callback` из
`pub::AI::SubmitState`.

## Сеть-смежное и прочее

Транспорта в Common нет (он в `rpclocal`/`RemoteClient`/`Server`). Есть `MarshalBuffer` (буфер
сериализации 0x18, put/get примитивов, строк, `FmtStr`, костюма), `StateMachine*`/`MessageHandler`
(двухсостоятельный автомат и маршрутизатор сообщений на `IMessageRouter`), `Watchable`/`BaseWatcher`,
`MPCritSec`, `CPlayerAccount` (MD5-ключ аккаунта в реестре), `Exclusion::*ZoneMgr` (зоны исключения
астероидов и туманностей), `CDeadReckonedVector` и `CRemotePhysicsSimulation`.
`SinglePlayer()` — флаг с начальным значением «одиночная игра»; `SetMultiPlayer(bool)` его снимает.
`Timing::*` — QPC-часы (`read`, `seconds`, глобальное время) и `Timing::Delta` (кадровая дельта,
0x28 байт, без vtable; при расхождении QPC и `GetTickCount` более 50 мс берётся `GetTickCount`).

## Известные пробелы

- Физика: IVP портирован не целиком (см. [common_physys.md](common_physys.md)).
- Часть AI-поведений и подсистем оборудования помечена в tracker как `partial`/`simplified`;
  при расхождениях с этим документом верен листинг `EXE\Disasm\Common.dll.txt`, затем tracker.
- Актуальный список дыр по функциям — в decomp-tracker и в [deviations.md](deviations.md).
