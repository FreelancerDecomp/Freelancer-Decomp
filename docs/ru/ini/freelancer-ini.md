# freelancer.ini

`freelancer.ini` — главный конфигурационный файл игры. Он задаёт папку данных, ресурсные DLL, настройки сервера и list-сервера, каналы журнала ошибок и в секции `[Data]` — каждый остальной INI-файл, который клиент и сервер загружают при старте.

**Расположение файла / как он находится.** Клиент открывает `Freelancer.ini` в своём текущем рабочем каталоге (имя зашито в код; ни один ключ командной строки его не меняет). Выделенный сервер (`FLServer.exe`) открывает `freelancer.ini` в каталоге своего исполняемого файла и дополнительно файл `SrvLocal-freelancer.ini` рядом с ним, который проходит через тот же reader. В одиночной игре сервер работает внутри процесса клиента и получает путь клиента, поэтому один файл обслуживает обе стороны. Файл текстовый и читается через `INI_Reader` (см. [parsers.md](parsers.md): регистронезависимые имена, комментарии `;`, повторяющиеся ключи выдаются по порядку). Dev-копия, используемая в этом репозитории, — `bin\freelancer.ini`; сборки Debug и ASan копируют её в свою выходную папку.

## Какой код читает какую секцию

| Секция | Кто читает | Примечания |
|---|---|---|
| `[Freelancer]` | клиентские `LoadStartupConfig`, `CLoginMgr::LoadConfig`, серверные `CFLServerApp::ReadFreelancerIni`, `Server_ReadWorldIni` | по одному ключу, см. ниже |
| `[Startup]` | клиентский `LoadStartupConfig` | вступительные ролики |
| `[ListServer]` | клиентский `CLoginMgr::LoadConfig`, серверный `ReadFreelancerIni` | адрес list-сервера (GUN) |
| `[Login]` | клиентский `CLoginMgr::LoadConfig` | необязательные значения автовхода |
| `[Server]` | серверный `ReadFreelancerIni` | читается и отбрасывается |
| `[Initial SP DLLs]` / `[Initial MP DLLs]` | серверный `Server_LoadInitialDlls` | запускаемые контроллеры |
| `[Resources]` | клиентский `LoadClientIniData`, серверный `ReadFreelancerIni` | ресурсные DLL |
| `[Data]` | `DataConfig::startup` (обе стороны), клиентские `LoadClientIniData` и `LoadStartupConfig` | все остальные INI-файлы |
| `[Error]` | клиентский `ErrorHandler::activate` | каналы журнала |

Секции, не перечисленные здесь (например, закомментированный блок `[;Display]` retail-файла), из `freelancer.ini` не читаются. Параметры отображения берутся из `PerfOptions.ini` (см. [client-ui-config.md](client-ui-config.md)).

## `[Freelancer]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `data path` | путь (каталог) | клиент `..\Data`; сервер: нет (сервер настраивает файловую систему данных, только если ключ существует) | Корневая папка игровых данных. Используется вся строка параметра, на клиенте до 259 символов. В коде сервера ключ называется `Data path`, а имена регистронезависимы. Относительное значение ОС интерпретирует относительно рабочего каталога процесса. | [startup_config.cpp](../../../src/freelancer/Client/startup_config.cpp), [FLServer.cpp](../../../src/FLServer/FLServer.cpp) |
| `local_server` | строка | `Server.dll` | DLL, реализующая внутрипроцессный (однопользовательский) сервер; в dev-файле — `rpclocal.dll`. Обрезается до 31 символа. Мультиплеер всегда загружает `RemoteServer.dll` независимо от этого ключа. | [login_mgr.cpp](../../../src/freelancer/Client/login_mgr.cpp), [server_connect.cpp](../../../src/freelancer/Client/server_connect.cpp) |
| `initial_world` | путь (относительно Data path) | пусто | Файл описания мира (обычно `InitialWorld.ini`, см. [saves-and-initial-world.md](saves-and-initial-world.md)). **Побеждает первое вхождение**; reader прекращает сканирование, найдя его. Используется при старте сервера (мультиплеер) и при создании restart-файла. | [IServerImpl.cpp](../../../src/Server/IServerImpl.cpp) |
| `AppGUID` | строка `{GUID}` | нет | GUID приложения для протокола list-сервера, разбирается через `ParseGUID`. Применяется, только если ключ присутствует. | [login_mgr.cpp](../../../src/freelancer/Client/login_mgr.cpp), [FLServer.cpp](../../../src/FLServer/FLServer.cpp) |

## `[Startup]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `movie_file` | путь (повторяется) | нет | Вступительные ролики, воспроизводятся по порядку при старте движка. Сохраняются только **первые три** вхождения (считая по всему файлу); остальные игнорируются. Путь собирается как `<data path>\<значение>`. | [startup_config.cpp](../../../src/freelancer/Client/startup_config.cpp), [engine_init.cpp](../../../src/freelancer/Client/engine_init.cpp) |

## `[ListServer]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `hostname` | строка | клиент `FLListServer2`; сервер `localhost` | Имя хоста или IP list-сервера («GUN»), куда мультиплеерные серверы отправляют отчёты и который опрашивают клиенты. Не проверяется. | [login_mgr.cpp](../../../src/freelancer/Client/login_mgr.cpp), [ServerControl.cpp](../../../src/FLServer/ServerControl.cpp) |
| `port` | int | клиент `28810` (0x708a); сервер `39304` (0x9988) | Порт list-сервера. Читается как float и усекается до целого; диапазон не проверяется. | то же |

## `[Login]`

Необязательная; в поставляемых файлах отсутствует.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `name` | wide-строка | `SinglePlayer` | Имя учётной записи для менеджера входа. | [login_mgr.cpp](../../../src/freelancer/Client/login_mgr.cpp) |
| `pwd` | wide-строка | пусто | Пароль учётной записи, хранится в файле открытым текстом. | то же |
| `char` | wide-строка | пусто | Имя выбираемого персонажа. | то же |

## `[Server]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `death_penalty` | int | н/д | Серверный reader проверяет наличие этого ключа и **отбрасывает его** (retail делает то же). Поставляемый комментарий описывает его как процент груза, теряемого при смерти в мультиплеере, но значение нигде в коде не используется. | [FLServer.cpp](../../../src/FLServer/FLServer.cpp) |
| `name`, `description` | строка | н/д | Закомментированы в dev-файле; из этого файла не читаются. | н/д |

## `[Initial SP DLLs]` и `[Initial MP DLLs]`

Сервер читает секцию, соответствующую текущему режиму (одиночная игра или мультиплеер); другая пропускается. Каждая строка `DLL` создаёт один *контроллер* — объект-плагин, получающий серверные сообщения. Все поставляемые записи указывают в `Content.dll`.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `path` | путь (каталог) | пусто | Каталог, добавляемый перед относительными именами DLL (`.` в dev-файле). Сохраняется до конца сеанса. | [IServerImpl.cpp](../../../src/Server/IServerImpl.cpp), [server_pub.cpp](../../../src/Server/server_pub.cpp) |
| `DLL` | `[файл dll, имя экземпляра, приоритет]` (повторяется) | нет | Один контроллер на строку: загружаемая DLL, имя, передаваемое в её `CreateInstance`, и приоритет. `Content.dll` распознаёт 23 имени экземпляров; поставляемые `StoryLauncher`, `GameSupport`, `SpaceSupport`, `BaseSupport`, `SpacePop` и `BasePop` среди них, а `AISandbox` и `TestAutomation` — нет, поэтому эти две строки ничего не создают (наполовину созданный контроллер удаляется, ошибка не выдаётся). | то же |
| приоритет (третий параметр `DLL`) | enum | значение предыдущей строки, изначально `HIGHEST` | `HIGHEST`=0, `ABOVE_NORMAL`=1, `NORMAL`=2, `BELOW_NORMAL`=3, `LOWEST`=4, сравнение без учёта регистра. Неизвестное или пустое слово сохраняет приоритет **предыдущей** строки `DLL`. Контроллеры хранятся отсортированными по этому числу, меньшие первыми. | то же |

## `[Resources]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `DLL` | строка (повторяется) | нет | Ресурсная DLL, загружаемая через `LoadLibrary` (таблицы строк, infocards). Порядок определяет индекс ресурсного модуля, по которому игра ищет ID строк, поэтому его нельзя менять без нужды. На клиенте неудачная загрузка пишет ошибку, но всё равно резервирует свой индекс; сервер регистрирует возвращённый handle напрямую. | [dataconfig_progress.cpp](../../../src/freelancer/Client/dataconfig_progress.cpp), [FLServer.cpp](../../../src/FLServer/FLServer.cpp) |

## `[Error]`

Настраивает каналы журнала обработчика ошибок (вывод `FDUMP`). Клиент читает один раз при старте.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `log` | `[куда, вид, серьёзность]` (повторяется) | нет | Один канал вывода на строку. **куда**: `$Window` (окно сообщения), `$Text` (вывод отладчика) или имя файла (файл создаётся/обнуляется при каждом запуске). **вид**: число или символьный литерал вроде `'c'`, по умолчанию `-1` = все виды. **серьёзность**: по умолчанию `100`; сообщения, номер серьёзности которых больше этого предела, отбрасываются (меньше число — серьёзнее; поставляемый комментарий перечисляет 0 fatal, 1 error, 2 warning, 3 notice, 4+ trace). Вид и серьёзность разбираются как числа, поэтому `65540`, `'I'` и `0x10004` принимаются одинаково. | [common_errorhandler.cpp](../../../src/Common/common_errorhandler.cpp), [main.cpp](../../../src/freelancer/Client/main.cpp) |

После этой секции клиент также открывает фиксированный канал `FLSpew.txt` в пользовательской локальной папке данных (вид -1, серьёзность 7); INI-файлом он не настраивается. Каждый канал хранит только **первые 127 символов** своей строки `where`. Однобуквенные виды из поставляемого комментария (`R` render, `n` network, `w` warning, `i` создание экземпляров, `r` reputation, `a` архетипы, `c` контроллеры, `m` миссии, `s` второй поток, `f` файловый ввод-вывод, `e` explosion/fuse, `d` damage, `A` консоль перезагрузки) взяты из этого комментария и из мест вызова, которые их поднимают; reader их не проверяет.

## `[Data]`

Каждый ключ `[Data]` называет файл (или, для `bases`, шаг загрузки). Значения — имена файлов относительно Data path. **Порядок строк — это порядок загрузки**, и от него зависят несколько загрузчиков: таблицы ID архетипов общие у клиента и сервера и должны идти в одном порядке на обеих сторонах, `solar` должен идти перед `universe`, а `effects = fx\effect_types.ini` и `beam_effects.ini` — перед остальными файлами эффектов. Имена ключей регистронезависимы. Ключи, которых не знает ни один из диспетчеров, молча игнорируются.

Загрузка идёт в два прохода по одной и той же секции:

1. `DataConfig::startup` ([common_dataconfig.cpp](../../../src/Common/common_dataconfig.cpp)) выполняется и на клиенте, и на сервере (**обе** стороны). Он обрабатывает ключи, помеченные ниже *both*. Использует счётчик ссылок: повторный вызов лишь увеличивает счётчик и не перечитывает файл. После цикла он также загружает бинарный `AI\state_graph.db` из папки Data (не INI-файл). На клиенте callback прогресса видит каждое значение первым и может прервать загрузку.
2. Затем клиент запускает `LoadClientIniData` ([dataconfig_progress.cpp](../../../src/freelancer/Client/dataconfig_progress.cpp)) для ключей, помеченных *client*. `gate_tunnels` и `jump_effect` перехватываются раньше в `LoadStartupConfig` и позднее загружаются собственными модулями.

| Ключ | Количество в dev-файле | Сторона | Что содержит файл | Загрузчик | Описано в |
|---|---|---|---|---|---|
| `solar` | 1 | both | Архетипы solar-объектов (планеты, станции, врата, ...) | `Archetype::LoadSolar` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `universe` | 1 | both | `universe.ini`: список звёздных систем и баз и файлы, которые их описывают | `Universe::Startup` | [world-data.md](world-data.md) |
| `constants` | 1 | both | Глобальные игровые константы | `ReadConstants` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `equipment` | 7 | both | Архетипы оборудования (свет, двигатели, оружие, щиты, тракторы и т. д.); каждый файл может содержать любые виды секций оборудования. Файл, не дающий новых определений, пишет предупреждение. | `Archetype::LoadEquipment` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `ships` | 2 | both | Архетипы кораблей (`shiparch.ini`, `rtc_shiparch.ini`) | `Archetype::LoadShips` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `asteroids` | 1 | both | Архетипы астероидов (`solar\asteroidarch.ini`) | `Archetype::LoadAsteroids` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `explosions` | 1 | both | Архетипы взрывов и обломков (`fx\explosions.ini`) | `Archetype::LoadSimples` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `loadouts` | 4 | both | Стандартные комплектации кораблей и solar-объектов | `Loadout::Load` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `bodyparts` | 1 | both | Определения частей тела персонажей | `LoadBodypartDescriptions` | [world-data.md](world-data.md) |
| `costumes` | 1 | both | Определения костюмов персонажей | `LoadCostumeDescriptions` | [world-data.md](world-data.md) |
| `bases` | 1, **без значения** | both | Не файл: позиция ключа определяет, *когда* загружаются файлы баз, названные в `universe.ini`. | `BaseDataList_load` | [world-data.md](world-data.md) |
| `goods` | 5 | both | Торговые товары и цены (товары, оборудование, корабли) | `GoodList_load` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `markets` | 3 | both | Что продаёт каждая база (разное, корабли, товары) | `BaseDataList_load_market_data` | [world-data.md](world-data.md) |
| `PetalDB` | 1 | both | База petal (интерфейса) | `PetalDB_load` | [world-data.md](world-data.md) |
| `Groups` | 1 | both | Группировки и их матрица репутации: указывает на `InitialWorld.ini` (тот же файл, что и `initial_world`) | `Reputation::Startup` | [saves-and-initial-world.md](saves-and-initial-world.md) |
| `fuses` | 16 | both | Определения fuse (скриптовые последовательности эффектов); здесь ставятся в очередь, позднее разбираются серверной и клиентской базами fuse | `FuseDB::AddFuseINIFile` | [world-data.md](world-data.md) |
| `WeaponModDB` | 1 | both | Таблица модификаторов оружия | `WeaponModDB_load` | [world-data.md](world-data.md) |
| `NewCharDB` | 1 | both | Таблица новых персонажей: фракции, пакеты и пилоты, предлагаемые при создании персонажа | `NewChar::Database::Load` | [world-data.md](world-data.md) |
| `RtcSlider` | 1 (пишется `rtcslider`) | both | Таблица слайдеров для скриптов real-time-cutscene | `RtcSlider::load` | [world-data.md](world-data.md) |
| `fonts` | 1 | client | Определения шрифтов | `LoadFonts` | [client-ui-config.md](client-ui-config.md) |
| `rich_fonts` | 1 | client | Определения rich-text шрифтов | `LoadRichFontsFromIni` | [client-ui-config.md](client-ui-config.md) |
| `sounds` | 7 | client | Таблицы звуковых архетипов (ambience, sfx, engines, interface, music, story, gameflow) | `LoadClientSoundArchIni` | [client-audio-misc.md](client-audio-misc.md) |
| `voices` | 18 | client | Голосовые профили и таблицы реплик диалогов | `LoadClientVoicesIni` | [client-audio-misc.md](client-audio-misc.md) |
| `effects` | 12 | client | Таблицы типов визуальных эффектов и библиотеки эффектов; `effect_types.ini` и `beam_effects.ini` первыми | `EffectArchLoadIni` | [client-world-fx.md](client-world-fx.md) |
| `effect_shapes` | 1 | client | Таблица текстурных форм для эффектов; ставится в очередь и разбирается в конце старта движка | `AddEffectShapesFile` | [client-audio-misc.md](client-audio-misc.md) |
| `debris` | 1 | client | Архетипы обломков: обычно тот же файл, что и `explosions` | `LoadDebrisArchFile` | [archetypes-and-equipment.md](archetypes-and-equipment.md) |
| `intro` | 1 | client | Конфигурация вступительного экрана (секция `[Images]`) | `LoadIntroConfigFromIni` | [client-ui-config.md](client-ui-config.md) |
| `HUD` | 1 | client | Список элементов HUD; путь сохраняется, элементы строятся позднее | `SetHudIniPath` | [client-ui-config.md](client-ui-config.md) |
| `igraph` | 1 | client | Именованные кривые ключевых кадров (секции `[IGraph]`), на которые источники света ссылаются по nickname | `GraphLibraryLoad` | [client-audio-misc.md](client-audio-misc.md) |
| `gate_tunnels` | 1 | client | Параметры эффекта туннеля прыжковых врат | `GateTunnelIniLoad` | [client-world-fx.md](client-world-fx.md) |
| `jump_effect` | 1 | client | Параметры эффекта прыжка | `JumpEffectIniLoad` | [client-world-fx.md](client-world-fx.md) |

Клиент передаёт в `DataConfig::startup` фабрику объектов, поэтому файлы архетипов, помеченные *both*, порождают клиентские подклассы с дополнительными ключами записей (читаются через `ReadUnknown`); сервер фабрику не передаёт. См. [archetypes-and-equipment.md](archetypes-and-equipment.md).

### Ключи retail-файла, которые порт игнорирует

Retail-`freelancer.ini` содержит ещё несколько строк `[Data]`, которых диспетчеры порта не знают и потому пропускают: `fonts_dir` (каталог файлов шрифтов внутри процесса), `concave` (`concave.ini`) и `stars` (`solar\StarArch.ini`). Файл star-arch тем не менее загружается — по **фиксированному имени** (`solar\StarArch.ini`) в коде космической сцены, а не через ключ `stars`. После прохода по `[Data]` клиент также загружает файл с фиксированным именем `audio\SoundCFG.ini` (см. [client-audio-misc.md](client-audio-misc.md)).

## Пример

Dev-копия `bin\freelancer.ini` в сокращении (`...` обозначает пропущенные строки):

```ini
[Freelancer]
data path = ..\DATA
local_server = rpclocal.dll
initial_world = InitialWorld.ini      ;relative to Data path
AppGUID = {A690F026-26F0-4e57-ACA0-ECF868E48D21}

[Startup]
movie_file = movies\MGS_Logo_Final.wmv
...
[ListServer]
hostname = gun.fllistserver.com
port = 2300

[Initial SP DLLs]
path = .
DLL = Content.dll, GameSupport, HIGHEST
DLL = Content.dll, SpaceSupport, NORMAL
...
DLL = Content.dll, BasePop, LOWEST

[Resources]
DLL = InfoCards.dll
...
[Data]
       solar = solar\solararch.ini
    universe = universe\universe.ini
   constants = constants.ini
...
       bases                        ; no file name, the key sets the load order
     markets = equipment\market_misc.ini
...
[Error]
log = freelancer_messages.log,-1
log = $Window,65540
log = $Text, 65536, 5
log = $Text, 'c'
```

Отличия dev-копии от retail-файла: dev-копия использует `path = .` в двух секциях DLL (в retail `..\dlls\bin`), указывает `hostname` на community list-сервер и опускает закомментированный блок `[;Display]`, `fonts_dir`, `concave`, `stars` и закомментированные строки снимков вех. Секция `[Error]` dev-копии добавляет первой строку `log`, которая пишет каждое сообщение в `freelancer_messages.log`.

## См. также

* [parsers.md](parsers.md) — грамматика reader и разрешение путей.
* [server.md](server.md) — взгляд сервера на этот файл, включая `SrvLocal-freelancer.ini` и последовательность старта.
* [client-ui-config.md](client-ui-config.md) — собственные ключи клиента в этом же файле.
* [world-data.md](world-data.md) — диспетчер `[Data]` и маршрутизация `[Error]`, как они реализованы в Common.
* [engine-dacom.md](engine-dacom.md) — `DACOM.INI`, другой файл с другим парсером.
