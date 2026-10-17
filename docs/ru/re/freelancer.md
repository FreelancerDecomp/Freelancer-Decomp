# freelancer.exe — клиент

`src/freelancer/` собирается в `freelancer.exe`: точка входа, окно и цикл сообщений, стейт-машина
клиента, игровой поток меню (Gf\Script, Thorn-сцены), интерфейсные виджеты (NeuroNet `NN_*`,
`WinControl`), HUD, космос (камеры, отрисовка, объекты клиента), звук, эффекты, подключение к
серверу. Документ нужен тому, кто портирует или правит клиент: он описывает архитектуру и
контракты между подсистемами, а не отдельные функции — их статус и построчные заметки лежат в
decomp-tracker (модуль `freelancer.exe`, листинг `EXE/Disasm/freelancer.exe.{txt,c}`, образ
`0x00400000`). Рендер, материалы, сцены и физика живут в других модулях (RP8, RendComp, Shading,
Thorn, EngBase, alchemy, Common) — клиент даёт им данные и вызывает их интерфейсы, а не
дублирует их.

Смежные документы: [freelancer_cliobj.md](freelancer_cliobj.md) (клиентские компаньоны архетипов),
[equip_fx_factory.md](equip_fx_factory.md) (FX оборудования), [options_screen.md](options_screen.md),
[dealer_equip_stats.md](dealer_equip_stats.md), [winmain.md](winmain.md), [deviations.md](deviations.md).

## Старт и главный цикл

- `WinMain` (`main.cpp`): мьютекс экземпляра, проверка DATA, `LoadStartupConfig`
  (`startup_config.cpp`), создание окна, `InitializeEngine` (`engine_init.cpp`), цикл
  (`message_pump.cpp`). `InitializeEngine` поднимает `DALib`, пайплайн рендера и буферы,
  вызывает `DataConfig::startup` с `ICliObjFactory` и `ProgressCB` (`dataconfig_progress.cpp`: ключи
  `[Data]`, которые Common не знает — шрифты, звуки, голоса, эффекты, HUD, `intro` — обрабатывает
  клиент по мере чтения), показывает заставку, подгружает `movie.dll` через `GetProcAddress`.
- **Состояния клиента** — объекты `IEnvironment : IInputEvent` (15 слотов, `client_environment.h`):
  `GF` (игровой поток базы, `gameflow.cpp`), `SpaceApp` (`space_app.h`), `IntroFocus`
  (`client_state.cpp`), `MissionBriefing` (`mission_briefing.cpp`). `EnterClientState(id, prev)`
  (`client_state.cpp`): 1 — выход, 2 — космос, 3 — база (GF), 4/5 — слот 10 GF/SpaceApp (пустой),
  6 — меню, 7 — загрузка игры, 8 — брифинг миссии; другие id возвращают прежнее состояние.
  Очередной переход ставит `G_PendingClientStateId`.
- **Кадр**: `TickClientFrame` обновляет таймер (`client_timing.cpp`), текущее состояние, свет, HUD и
  отрисовку; в меню показ кадра делает сам state (3D → очистка Z → UI).
- **Порядок статических инициализаторов — контракт ввода.** Обработчики KeyMap-событий одного id
  вызываются в обратном порядке регистрации, первый вернувший `true` «съедает» событие; часть
  обработчиков регистрируется статическими инициализаторами, поэтому порядок таблицы `_initterm`
  определяет, кто видит мышь первым (в retail окно NeuroNet перехватывает клик раньше хотспота
  комнаты). В порте порядок сегмента `.CRT$XCU` задаёт линковка, поэтому `gameflow.cpp` закреплён
  `#pragma init_seg(lib)`; новый статический обработчик, делящий id с существующим, требует сверки
  с таблицей `0x0060f000..0x006101bc`.

## Игровой поток меню и базы

Сцены баз — Thorn-скрипты (`.thn`), исполняемые `Thorn.dll`; клиент создаёт для их сущностей
объекты `FAttachObject`/`Gf\Script` (`gf_script*.cpp`, `gf_thorn_*.cpp`): типы 1–9 и 13 (compound,
deformable, camera, light, scene, psys, marker, sound, hardpoint-обёртка). `RoomControl`
(`room_control*.cpp`) рисует комнату: слои, обход объектов, проход Alchemy (`DAT_00679a10` — реестр
`FRoomObject`), камеры комнаты (`room_camera.cpp`, `find_camera` после `RoomData::load`),
хотспоты. Камера, `SetView` и `GeoSetPos/Orient` приходят от Thorn/EngBase: если после слота 22
`SET_CAMERA` кадр пуст, виноваты они, а не exe. Расположение и состояние базы —
`menu_universe_base.cpp` (`LocationController`: вход/выход, музыка и эмбиент комнаты из
`RoomData`). Экраны выбора персонажа/сервера/аккаунта — `page_*.cpp`, подключение — `connect_*.cpp`,
`server_connect.cpp`, `login_mgr.cpp`, `intro_connect.cpp`.

## Интерфейсный слой

- **Win3D / WinControl**: `win3d*.cpp` — вьюпорт-хост (перспектива, матрицы); `wincontrol_base`,
  `wincontrol_children` — база GUI-деревьев (список детей, каскад create/render/destroy, ~98
  конкретных vtable в retail).
- **NeuroNet (`NN_*`)**: фабрики окон `NN_Main`, `NN_Inventory`, `NN_Dealer`, `NN_Trade`,
  `NN_Repair`, `NN_NavMap`, `NN_Pause`, `NN_LoadSave`, `NN_Preferences`, `NN_Scanner`, `NN_News`,
  `NN_Exit`, `NN_Help` … (`nn_*.cpp`); контролы — `nn_text_control`, `nn_static_control`,
  `nn_button_control`, `nn_group_control`. Подписи кнопок рисует `font_atlas`/`font_imp`
  (`FontAtlasRasterize` использует `ETO_OPAQUE`, иначе буквы предыдущих кнопок остаются в общей
  битовой карте).
- **KeyMap**: `interface/KeyMap.ini` + пользовательский `UserKeyMap.ini` (`menu_keymap.cpp`),
  страница настроек управления — часть `NN_Preferences` (`nn_preferences.*`, см.
  [options_screen.md](options_screen.md)); `perf_options.cpp` — реестр опций производительности
  (`PerfOptions.ini` в каталоге пользователя; секции `[PerfVersion]`, `[PerfCount]`, `[PerfOptions]`,
  `[Display]`).
- **Текст**: `ids_*`/`infocard_map` (IDS-строки ресурсных DLL), `rich_text_win`/`ui_rich_text_win`
  (RDL-текст), `font_string` (`FontString::Paint` берёт клип из размеров поверхности и сдвигает тень
  на 1 или 2 пикселя по ширине экрана). Свойство `TextControl 0x23` задаёт горизонтальную обрезку
  (левую и правую, в пикселях) — на ней держится бегущая строка новостей.
- **Настройка переходных сцен**: реестр PerfOptions и проигрыватель `GfScriptSequence` разделяют
  одно состояние `skip_transition_scripts` (значение ниже 0.5 включает автопропуск посадки и
  вылета; 1.0 показывает сцены).
- **Звук**: `menu_sound_arch.cpp` — клиентский микшер, источники (`Sound::Source`, потоковые,
  пространственные), ducking, bgm; `sound_config.cpp` — `audio\SoundCFG.ini` (`[CFG]`) и Gf-фейдер;
  `voice_profiles.cpp` — реестр голосов (`[Data] voices=`, `[Voice]`, `[VoiceSound]`).

## HUD

`HUD=` в `[Data]` задаёт путь к `interface\hud.ini`; при инициализации кабины
`LoadHudElementsFromIni` (`hud_element_list.cpp`) читает его и по заголовкам секций создаёт элементы
через цепочку фабрик; активные элементы получают рассылку Notify/Event/Render/Shutdown, кадр рисует
`RenderHudElements`. Секции/классы: `[Steer]` (стрелка мышиного полёта), `[Core]` и `[Status]`
(шкалы корабля, `hud_status_core.cpp`, группы оружия — `hud_weapon_groups.cpp`), `[Target]`
(`hud_target*.cpp`), `[Targetable_Objects]` (скобки и маркеры упреждения, `hud_targetable_objects.cpp`),
`[DamageIndicator]`, `[Receiver]` (`HUD_Comm`, радио-очередь и портрет, `hud_comm.cpp`),
`[ContactList]`, `[Maneuvers]`, `[Waypoint]`, `[CruiseProgress]`. Путь `HUD=` запоминается отдельно от самой загрузки реестра. Угловые окна HUD в широкоэкранном режиме
раздвигаются к краям (`hud_widescreen.*`).

## Космос

- **Вход в систему**: `space_scene_load.cpp` читает INI системы (`[Object]`, `[Archetype]`,
  `[Nebula]`, звёзды, `star_sys`) и строит сцену мебели; в одиночной игре `StarSystem::add_player`
  Server'а сам шлёт `CREATESHIP`/`CREATELOOT` (слоты `IClient` 47/48), а `CREATESOLAR` — только в
  мультиплеере. Позы чужих объектов по пакету `UPDATEOBJECT` приходят только в MP (в SP
  `SystemPlayerView_Update` выходит сразу, объекты делят процесс с сервером). Клиентские игровые объекты —
  `client_gameobj.*` (`FClientGameobj`, ~0x208 байт), реестр `IObjDB` — `client_objdb.cpp`,
  спавн игрока — `player_spawn.cpp`.
- **Камеры**: базовый класс и производные (`space_camera_*`): first/third/rear/turret/script/static/
  docking/death; параметры из `shiparch.ini` и компаньона `CliShip` (`camera_offset`, углы,
  `camera_turn_look_ahead_slerp_amount`), кокпитные ini-загрузчики (`cockpit_load.cpp`).
- **Проход отрисовки** (`space_scene_obj_draw.cpp`, `space_scene_draw.cpp`): очередь объектов
  сортируется по убыванию квадрата дистанции (далёкие первыми) — как в retail, это сортировка
  MSVC6 `_Sort`: вставками при `< 17` элементов, иначе медиана-из-трёх + разбиение Хоара (строгие
  сравнения, неустойчиво) + финальная вставка; заменять её простой сортировкой нельзя. Каждый
  «слив» очереди начинается с повторной установки перспективы камеры (`Win3DSetPerspective`).
  Порядок слива: меши, астероиды (`cli_asteroid*.cpp` — кубы поля, затухание, погружение в зону
  исключения), туманности, эффекты; `FUN_0051f950` — слив мешей и обход реестра астероидов.
- **Туманности** (`cli_nebula.cpp`, retail `JumpEffect.cpp`): секция `[Nebula]` системного INI
  создаёт объект 0x2d0; он грузит файл туманности (`vtable[0](file, zone)`), хранит два дерева
  (`+0x04` узлы 0x5c, `+0x38` узлы 0x18 с мешами), оболочку зоны (`Exclusion::NebulaExclusionZone`)
  и молнии (сегменты-billboard'ы); ворота прыжка — `gate_tunnel.cpp`. Кольца планет
  (`texture_panels.cpp`) и звёздное свечение (`star_sphere.cpp`) — отдельные классы, не части
  туманности.
- **Огни и туман**: огни оборудования идут через общую пакетную очередь экранных четырёхугольников
  (вершины `XYZRHW | DIFFUSE | SPECULAR | TEX1`), туман для аддитивных эффектов чёрный; выбор между
  вершинным и табличным туманом (`FogApplyColorAndRange`) зависит от `QueryDeviceAbility(13)` (W-fog
  устройства), а не от `GetPipelineState(13)`. При вершинном тумане alpha берётся из `SPECULAR`,
  которую пакет обнуляет, — неверный режим скрывает блики даже у близких объектов.
- **Эффекты** (`effect_type_*.cpp`, `effect_base`, `particle_effect.cpp`, `beam_effect.cpp`,
  `effect_vertex_batch.cpp`): эффекты лежат в «вёдрах» по типу (`G_EffectTypeBuckets`, тип `0x2c` —
  луч), которые обходит `SpaceEffectsDrawFrame` в трёх точках кадра; луч (`BeamEffect`) рисуется как
  крест из двух перпендикулярных billboard'ов (текстура `code_beam`, FVF `0x142`, аддитивное
  смешивание) в общем растущем CPU-буфере вершин/индексов, который сбрасывается один раз за кадр
  (`DrawIndexedPrimitiveUP`). Цепочка выстрела: `CliShipFireWeapons` → гнёзда `FCliLauncherFx`/`FCliGunFx`
  → `CliLauncherFireProjectiles` → `FClientBeamGameobj` → `CliBeamActivateEffects` → `BeamEffectStart`
  → слив ведра `0x2c` (см. [equip_fx_factory.md](equip_fx_factory.md)).
- **FX и частицы Alchemy** (`vis_effect_arch.cpp`, `.ale`-файлы): `LoadAlePath` открывает `.ale`
  как UTF `IFileSystem`; внутри два узла-каталога `ALEffectLib\ALEffectLib` и
  `AlchemyNodeLibrary\AlchemyNodeLibrary` — данные читаются «внутри» каталога (`OpenNode` с
  последующим `OpenByDesc` того же имени), `..` вызывается только в конце. `LoadNodeLibrary` и
  `LoadAle` не очищают накопленные библиотеки, а сливают их (дубликаты отбрасываются; повторная
  загрузка уже закэшированного пути пропускается). Эффект создаётся `CreateEffect(lib, FxNode, crc)`,
  где `crc` — INI `effect_crc` = хэш имени эффекта таблицей CRC alchemy (с учётом регистра, **не**
  `DACOM_CRC::GetCRC32` и не IEEE); `FxNode` присоединяется к корню Alchemy и остаётся там при
  остановке. Каждый кадр ScriptPsys вызывает `Slot3` (трансформы Thorn → узлы), `Slot7`, `Slot8`
  (`SubmitNode` для узла с флагом); `Slot5` (эмиссия) в меню никто не зовёт.

## Одиночная игра и сервер

SP-сервер работает в процессе клиента (`Server.dll` + `rpclocal`), клиентская сторона протокола —
`client_iclient*.cpp` (`IClient`, ~90 слотов; пакеты `CREATE*`, `UPDATEOBJECT`, `DESTROY*`,
`LAND`/`LAUNCH`, `SYSTEM_SWITCH_*`, `SETREPUTATION` …). Ответ на `RequestCreateShip` сервер шлёт
прямым вызовом `REQUESTCREATESHIPRESP` (слот 61), а не сообщением контроллера. `connect_flow.cpp` строит соединение,
`character_select_equip.cpp` — снаряжение из `Loadout`/`EquipDescList`, `player_spawn.cpp` —
первый космос.

## Широкоэкранный режим

`FREELANCER_WIDESCREEN_SUPPORT` (свойство `FreelancerWidescreenSupport`, по умолчанию 1; читает
только клиент): камеры работают как «Hor+» — половина горизонтального угла означает угол на
поверхности 4:3 той же высоты, а при вьюпорте на всю ширину и поверхности шире 4:3 фрустум
расширяется во множитель `WidescreenHorPlusScale`; вертикальный угол и весь интерфейс Win3D остаются
как на 4:3, лишняя ширина добавляется по бокам (`widescreen.h`, `hud_widescreen.*`). Со значением 0
собирается retail-4:3-клиент. Расхождения с retail записаны в [deviations.md](deviations.md).
См. также [../hd_edition.md](../hd_edition.md).

## Код вне инвентаря Ghidra

Часть `.text`, которую Ghidra не дизассемблировала, не имеет строк в tracker, и метрика её не видит;
такие диапазоны разобраны по сырым байтам PE (по прологу и месту вызова), а вывод оставлен в
комментарии у ближайшей отслеживаемой функции порта. Известные диапазоны:

| Диапазон | Что это |
|---|---|
| `0x00412e41..0x0041353f` | `FontString` vt+0x24 `Paint` (клип по размерам поверхности, тень 1–2 пикселя, левая/правая обрезка строки) |
| `0x00504e5e..0x00504faf` | голова загрузки туманности (`Nebula` vt[0] `Load`) до цикла секций |
| `0x00534e5c..0x00534f43` | `IObjAffect` слот 16 — трактор на одну цель |
| `0x00573eae..0x00573f75` | драйвер `IntroFocus`: цикл выхода/входа в состояние и опрос |
| `0x00574380..0x0057490c` | `EnterState` меню: пролог, таблица случаев, случай 1 (колонка TypeSelect, вход в SP) |
| `0x00574fbe..0x00575293` | случай 18: `PageCharacterSelectSingle` |
| `0x005753ac..0x0057542f` | `TextMsgButton::SetFlagsE4E5`, `Init` |
| `0x0059a69e..0x0059a75f` | отладочная рамка `UIListBox` слота 13 (читает неизменный байт `.bss`, эквивалентно отсутствию) |
| `0x005a1cb8..0x005a1d3f` | `TextWin::SetFont` |
| `0x005b089a..0x005b09af` | обработчик `DALib::FatalErrorFn` |
| `0x005b8220..0x005b82cf` | не код: 11 записей `DIOBJECTDATAFORMAT` из `dinput8.lib` |

## Что закрывается не здесь

| Симптом на кадре | Где причина |
|---|---|
| Нет живого `Csys` после слота 22 (`SET_CAMERA`, spatial) | Thorn.dll (IGeo 3/4) |
| Fix/Cons-шарниры, звёздная сфера не в начале координат | EngBase (IEngine 47/48) |
| Нет вспышек PSYS, `CreateEffect hr=-8` | alchemy / библиотека эффектов |
| Чёрная туманность, шов SphereMesh | Shading / RendComp |
| `front_button.cmp` VMesh handle=0 | ReadFile/RendComp (`OpenNode`) |

## Известные пробелы

- Не все классы `WinControl` (≈98 в retail) и экраны NeuroNet портированы или портированы целиком;
  места, где порт делает своё вместо вызова портированных модулей, разобраны в
  [substitutions.md](substitutions.md) и записаны в [deviations.md](deviations.md).
- Построчный статус функций клиента (включая `UpdateSpaceState`, размещение камеры, `JumpEffect`,
  FX tradelane) — в decomp-tracker; при расхождении с этим документом верен листинг.
