# Конфигурация FLServer.exe и Server.dll

На этой странице перечислены все места, где выделенный сервер (`FLServer.exe`) и библиотека игровой логики (`Server.dll`, которая в одиночной игре служит также локальным сервером) читают INI-подобные файлы и прочую стартовую конфигурацию: `DACOMsrv.ini`, `freelancer.ini` (и его двойник `SrvLocal-`), `InitialWorld.ini`, файлы сохранений персонажей (`.fl`), шаблоны новых персонажей, файлы fuse, файлы систем и астероидных полей, открываемые на стороне сервера, и командную строку. Настройки движка внутри `DACOMsrv.ini` описаны в [engine-dacom.md](engine-dacom.md); общая таблица `[Data]` — в [freelancer-ini.md](freelancer-ini.md); сам читатель — в [parsers.md](parsers.md).

**Расположение файла / как он находится.** `FLServer.exe` вычисляет путь к ini по собственному расположению: `ServerControl.SetIniPath(<path of FLServer.exe>)` сохраняет каталог и задаёт имена файлов `freelancer.ini` и `SrvLocal-freelancer.ini` в том же каталоге ([ServerControl.cpp:98](../../../src/FLServer/ServerControl.cpp#L98)). `DACOMsrv.ini` передаётся в `DALib::Startup(NULL, "DACOMsrv.ini")` ([FLServer.cpp:549](../../../src/FLServer/FLServer.cpp#L549)) и разрешается загрузчиком DACOM (текущий каталог). `Server.dll` получает путь к `freelancer.ini` от хоста в `SStartupInfo.IniPath` ([IServerImpl.cpp:229](../../../src/Server/IServerImpl.cpp#L229)); для локального сервера одиночной игры хостом служит игровой клиент, а путь — это `freelancer.ini` клиента.

---

## `DACOMsrv.ini` (только FLServer)

`FLServer.exe` запускает движок DACOM из `DACOMsrv.ini` вместо `DACOM.INI` ([FLServer.cpp:549](../../../src/FLServer/FLServer.cpp#L549)); при сбое запуска управление возвращается из `InitInstance`, и сервер не работает. У файла та же грамматика и те же секции, что у клиентского `DACOM.INI`, и его потребляют только модули движка (`[DACOM]`, `[Libraries]`, `[System]`, `[Engine]`, `[RenderManager]`): см. [engine-dacom.md](engine-dacom.md). Код сервера не читает из него ни одного ключа напрямую. Dev-копия `bin\dacomsrv.ini` начинается с `@include FL_Dev.ini`; такого файла в репозитории нет, а отсутствующий include молча игнорируется.

---

## `freelancer.ini` в чтении `FLServer.exe`

`CFLServerApp::ReadFreelancerIni` вызывается при старте дважды: сначала для `freelancer.ini`, затем для `SrvLocal-freelancer.ini` (отсутствующий файл молча пропускается), поэтому значения из второго файла переопределяют значения первого ([FLServer.cpp:554](../../../src/FLServer/FLServer.cpp#L554), [читатель L757](../../../src/FLServer/FLServer.cpp#L757)). Здесь читаются только перечисленные ниже секции; остальную часть файла (`[Data]`, `[Initial MP DLLs]`, ...) позже читает `Server.dll`.

| Секция | Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `[Freelancer]` | `Data path` | str (всё значение) | нет | Каталог данных; открывается через `DALib::OpenData` как корень данных игры (`DALib::Data`). В dev-файле записан как `data path`; сопоставление не чувствительно к регистру. | [L765](../../../src/FLServer/FLServer.cpp#L769) |
| `[Freelancer]` | `AppGUID` | текст GUID `{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}` | нули | GUID приложения для сессии DirectPlay. Разбирается `ParseGUID` (текст из одних нулей даёт нулевой GUID). | [L770](../../../src/FLServer/FLServer.cpp#L774) |
| `[ListServer]` | `hostname` | str (всё значение) | `localhost` | Имя хоста или адрес Gun-сервера (списка серверов), которому сервер отправляет отчёты. | [L779](../../../src/FLServer/FLServer.cpp#L785) |
| `[ListServer]` | `port` | float, усекается до int (`[0]`) | 39304 (0x9988) | Порт сервера списка. Диапазон не проверяется. | [L783](../../../src/FLServer/FLServer.cpp#L789) |
| `[Server]` | `death_penalty` | - | - | Ключ проверяется, а ответ отбрасывается, как в retail; в этом порте он ни на что не влияет. (`name` и `description` закомментированы в dev-файле и нигде не читаются; имя и описание сервера берутся из стартового диалога.) | [L797](../../../src/FLServer/FLServer.cpp#L800) |
| `[Resources]` | `DLL` | str (всё значение) | нет | Ресурсная DLL, загружаемая через `LoadLibraryA` и регистрируемая для поиска строк (`AddResourceDll`). Повторяющиеся ключи загружают несколько DLL. | [L804](../../../src/FLServer/FLServer.cpp#L807) |

Функция возвращает, найден ли `Data path`; стартовый код этот результат не использует.

### Прочая стартовая конфигурация, не являющаяся INI

* **Командная строка** ([FLServerCmdLine.cpp:26](../../../src/FLServer/FLServerCmdLine.cpp#L26)). Параметр, начинающийся с `-` или `/`; регистр буквы не учитывается. Отвергнутое значение пишется в лог и игнорируется.

  | Параметр | Аргумент | Действие (по умолчанию) |
  |---|---|---|
  | `B` | необязательный путь | Писать лог пропускной способности; без пути — `FLServerBWLog-<unix time>.txt`. |
  | `P` | десятичный порт | Игровой порт (по умолчанию 14041, 0x36D9). Диапазон не проверяется. |
  | `L` | имя файла | Открыть текстовый лог-файл (сообщает об ошибке, если создать его не удалось). |
  | `S` | десятичный уровень | Уровень логирования, допустимый диапазон 1..7 (по умолчанию 4). |
  | `M` | десятичное число | Максимум строк, хранимых в консоли, 1..100000 (по умолчанию 100000). |
  | `T` | десятичное число | Сколько строк обрезается при заполнении консоли, от 1 до `M`-1 (по умолчанию 200). |
  | `H` | float | Частота обновления сервера в обновлениях в секунду, допустимый диапазон 1..60; хранится как период `1/rate` (период по умолчанию 0,05 с, то есть 20 в секунду). |
  | `E` | один символ | Добавить категорию лога. |
  | `X` | `+` (или ничего) / `-` | Снять / установить маску исключений FPU (по умолчанию замаскированы). |
  | `C` | нет | Принять сохранённую конфигурацию сервера, не показывая стартовый диалог. (Также проваливается в сообщение «unknown switch», как в retail.) |

* **`FLServer.cfg`** в каталоге пользовательских данных — **двоичный** файл, который записывает стартовый диалог (версия 3; имя сервера, описание, пароль в виде широких строк, флаги новых игроков / публикации в интернете / PvP, `MaxPlayers` типа int). Значения: максимум игроков 1..128 (по умолчанию в диалоге 16), имя сервера до 32 символов, описание до 128, пароль до 16 ([ServerConfigDlg.cpp:52](../../../src/FLServer/ServerConfigDlg.cpp#L52)). INI-файлом он не является.
* **`motd.dat`** в каталоге пользовательских данных: текст UTF-16 (не более 0x13FF символов), показываемый игрокам при входе; читается потоком сервера ([FLServer.cpp:342](../../../src/FLServer/FLServer.cpp#L342)).

---

## `freelancer.ini` в чтении `Server.dll`

`IServerImpl::Startup` ([IServerImpl.cpp:202](../../../src/Server/IServerImpl.cpp#L202)) выполняет по порядку, с переданным ему путём к ini:

1. `DataConfig::startup(iniPath)` — обходит каждый ключ `[Data]` ([freelancer-ini.md](freelancer-ini.md)); при неудаче последующие шаги пропускаются, и `Startup` возвращает false.
2. `Server_ReadWorldIni(iniPath)` — `[Freelancer] initial_world`.
3. `Server_LoadInitialDlls(iniPath)` — `[Initial SP DLLs]` / `[Initial MP DLLs]`.
4. Только мультиплеер: `Server_LoadWorldState()` — читает файл начального мира, названный на шаге 2.

### `[Freelancer] initial_world`  ([IServerImpl.cpp:2825](../../../src/Server/IServerImpl.cpp#L2825))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `initial_world` | str (всё значение, до 259 символов) | пусто | Имя файла начального мира относительно каталога данных (в dev-ini — `InitialWorld.ini`). Побеждает первое вхождение; последующие игнорируются. Используется `Server_LoadWorldState` (старт мультиплеера) и при создании файла перезапуска (см. ниже). | [L2831](../../../src/Server/IServerImpl.cpp#L2831) |

### `[Initial SP DLLs]` и `[Initial MP DLLs]`  ([IServerImpl.cpp:2922](../../../src/Server/IServerImpl.cpp#L2922))

Используется секция, соответствующая режиму работы: `[Initial SP DLLs]`, когда `SinglePlayer()` истинно, иначе `[Initial MP DLLs]`. Другая секция пропускается. Они перечисляют **DLL-контроллеры** (модули миссий/ИИ/населения, в поставляемой конфигурации все в `Content.dll`) в порядке создания.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `path` | str (всё значение, до 259 символов) | пусто | Каталог, добавляемый перед относительными именами DLL-контроллеров (`pub::DLL_LoadLibrary`). Остаётся заданным на всю сессию. | [L2950](../../../src/Server/IServerImpl.cpp#L2950) |
| `DLL` | `[0]` файл DLL, `[1]` имя экспортируемого интерфейса, `[2]` приоритет | нет | Один контроллер на строку; повторяющиеся ключи создают контроллеры в порядке файла. `[0]` и `[1]` копируются с ограничением 259 символов, `[2]` — с ограничением 31 символ. `[2]` — одно из `HIGHEST` (0), `ABOVE_NORMAL` (1), `NORMAL` (2), `BELOW_NORMAL` (3), `LOWEST` (4), без учёта регистра. **Отсутствующий или неизвестный приоритет сохраняет приоритет предыдущей строки `DLL`** (HIGHEST/0 для первой строки). Приоритет определяет порядок контроллеров в списке диспетчеризации. | [L2958](../../../src/Server/IServerImpl.cpp#L2958) |

Пример (синтетический):

```ini
[Initial MP DLLs]
path = .
DLL = Content.dll, GameSupport, HIGHEST
DLL = Content.dll, SpaceSupport, NORMAL
DLL = Content.dll, SpacePop, LOWEST   ; priority tokens, comments after ';'
```

### `[Time]` и репутация в `InitialWorld.ini` / файлах сохранений  ([IServerImpl.cpp:117](../../../src/Server/IServerImpl.cpp#L117), [:2862](../../../src/Server/IServerImpl.cpp#L2862))

`Server_LoadWorldState` (мультиплеер) открывает `<data directory>\<initial_world>`; `Server_LoadSinglePlayerState` (одиночная игра, после выбора персонажа) использует открытый читатель файла сохранения персонажа. Оба выполняют одни и те же три шага: (1) находят первую секцию `[Time]` и берут **первую строку значения в ней, как бы ни назывался ключ** (`float` `[0]`; сохранённые игры пишут `seconds = <float>`; 0 по умолчанию, если секции `[Time]` нет) в качестве базы часов сервера; (2) `Reputation::LoadFeelings` читает данные репутации `[Group]` из того же читателя ([saves-and-initial-world.md](saves-and-initial-world.md)); (3) читатель, всё ещё открытый, передаётся всем DLL-контроллерам как сообщение `LoadGame`, так что каждый контроллер читает из него собственные секции ([missions.md](missions.md), [ai-and-encounters.md](ai-and-encounters.md), [saves-and-initial-world.md](saves-and-initial-world.md)). Об отсутствующем файле начального мира при вызове открытия не сообщается.

---

## Файлы систем и баз на сервере

* `pub::BuildSystemReader` / `pub::BuildBaseReader` / `pub::System::EnumerateZones` лишь **открывают** файл системы или файл базы для вызывающей DLL-контроллера и передают ей читатель (или по одному читателю на зону); сами они не читают ни одного ключа ([server_pub.cpp:349](../../../src/Server/server_pub.cpp#L349), [:367](../../../src/Server/server_pub.cpp#L367), [:6827](../../../src/Server/server_pub.cpp#L6827)). Путь к файлу системы — это `universe.ini` `[system] file`, превращённый в абсолютный относительно каталога вселенной (файл, который не удаётся открыть, пишет в лог «missing System file»); путь к файлу базы — каталог данных плюс `[Base] file` (если открыть не удалось, сообщения нет). См. [systems-and-bases.md](systems-and-bases.md).
* **Загрузка системы в мультиплеере** (`StarSystem::load`, [server_classes.cpp:6779](../../../src/Server/server_classes.cpp#L6779)): файл системы открывается, и каждая секция `[Object]` создаётся как solar через `CSolar::ReadObj` (ключи — в [archetypes-ships-solars-asteroids.md](archetypes-ships-solars-asteroids.md#секции-object-системных-файлов-через-архетипы-solar-csolarreadobj)). В одиночной игре сервер повторно использует solar-объекты и астероидные поля, уже построенные клиентом, и ничего не читает из файла системы.

### Астероидные поля на сервере

`SrvAsteroidSystem::load` ([server_classes.cpp:5798](../../../src/Server/server_classes.cpp#L5798), только при загрузке системы в мультиплеере) читает каждую секцию `[Asteroids]` файла системы:

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `file` | str (всё значение, до 255 символов) | пусто | Файл астероидного поля относительно каталога данных. | [L5817](../../../src/Server/server_classes.cpp#L5817) |
| `zone` | str (всё значение, до 63 символов) | пусто | Nickname зоны, которую заполняет поле. | [L5825](../../../src/Server/server_classes.cpp#L5825) |

Если любое из значений пусто, секция молча пропускается. Неизвестная зона пишет в лог «AsteroidField linked to missing Zone (%s)», после чего сервер разыменовывает её (поведение retail).

Затем `FSrvAsteroidField::load` ([server_classes.cpp:5668](../../../src/Server/server_classes.cpp#L5668)) открывает файл поля и делает первый проход по его секции(ям) `[Field]`, читая эти ключи как **целые числа** (число разбирается как float и усекается, так что `0.5` становится 0):

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `cube_size` | int | 250 (или текущий размер поля) | Длина ребра кубика сетки. | [L5685](../../../src/Server/server_classes.cpp#L5685) |
| `fill_dist` | int | 0 | Расстояние вокруг игрока, в пределах которого заполняются кубики. (Значение из файла применяется всегда, поэтому отсутствующий ключ даёт 0.) | [L5687](../../../src/Server/server_classes.cpp#L5687) |
| `empty_cube_frequency` | int | 0 | Доля пустых кубиков; на сервере усекается до целого. | [L5689](../../../src/Server/server_classes.cpp#L5689) |

а затем перечитывает файл с начала через `CAsteroidField::load` для секций `[Cube]`, `[Exclusion Zones]` и `[LootableZone]` ([archetypes-ships-solars-asteroids.md](archetypes-ships-solars-asteroids.md#файлы-полей-астероидов-casteroidfieldload)).

---

## Файлы fuse

Повторяющиеся ключи `[Data] fuses` (dev-ini: `fx\fuse.ini` и `fx\fuse_*.ini`) ставятся в очередь `DataConfig` и загружаются `ServerFuseDB::LoadFile` в порядке списка ([server_fuse.cpp:1246](../../../src/Server/server_fuse.cpp#L1246)); клиент загружает те же файлы собственным читателем ([client-world-fx-effects.md](client-world-fx-effects.md)). На fuse ссылаются по nickname из ключей `fuse` архетипов кораблей/solar-объектов и групп столкновений ([archetypes-ships-solars-asteroids.md](archetypes-ships-solars-asteroids.md)).

**Структура.** Файл — это последовательность секций `[fuse]`, за каждой из которых идут секции *действий* до следующего `[fuse]` ([ServerFuse::Read, L82](../../../src/Server/server_fuse.cpp#L82)). Заголовки, отличные от `fuse`, встречающиеся вне fuse, пропускаются. Fuse сохраняется под своим `name`; если fuse с таким именем уже загружен, более поздний отбрасывается (побеждает первый). Секция действия, заголовка которой нет в списке ниже, игнорируется. Действия упорядочиваются по времени срабатывания `at_t`.

### `[fuse]`  ([common_fuses.cpp:538](../../../src/Common/common_fuses.cpp#L538))

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `name` | id (всё значение) | 0 | Nickname fuse. | [L551](../../../src/Common/common_fuses.cpp#L550) |
| `lifetime` | float | -1.0 | Длительность fuse в секундах; позиция горения продвигается на `dt / lifetime`. Не проверяется (значение 0 или меньше используется как есть). | [L553](../../../src/Common/common_fuses.cpp#L552) |
| `death_fuse` | bool | false | Fuse является fuse смерти. | [L555](../../../src/Common/common_fuses.cpp#L554) |

### Ключи каждой секции действия

| Ключ | Тип | По умолчанию | Значение | Источник |
|---|---|---|---|---|
| `at_t` | float `[0]`, необязательный float `[1]` | -1.0 (не задано) | Позиция срабатывания на временной шкале fuse (0..1 от lifetime). При втором значении позиция — **случайная** величина между двумя значениями, выбираемая при поджиге fuse. | [common_fuses.cpp:448](../../../src/Common/common_fuses.cpp#L448) |

### Секции действий

Каждая секция читает `at_t` и перечисленные ниже ключи; неизвестные ключи игнорируются. Имена не чувствительны к регистру, если сравнение явно не указано как чувствительное к регистру.

| Секция | Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `[ignite_fuse]` | `fuse` | id (всё значение) | 0 | Fuse, который нужно поджечь. | [L216](../../../src/Server/server_fuse.cpp#L217) |
| | `fuse_t` | float | 0.0 | Начальная позиция, выдаваемая поджигаемому fuse. | [L218](../../../src/Server/server_fuse.cpp#L219) |
| `[destroy_root]` | - | - | - | Собственных ключей нет; уничтожает объект. | [L282](../../../src/Server/server_fuse.cpp#L282) |
| `[damage_root]` | `damage_type` | `absolute` или другое | может убить | `absolute` (без учёта регистра) означает, что урон не может убить объект; любой другой текст означает, что может. | [L329](../../../src/Server/server_fuse.cpp#L329) |
| | `hitpoints` | int | 0 | Снимаемые очки здоровья. | [L337](../../../src/Server/server_fuse.cpp#L337) |
| `[destroy_group]` | `group_name` | cstr | пусто | Имя группы столкновений (`obj` из `[CollisionGroup]`); текст `random` (без учёта регистра) выбирает случайную живую группу. | [L400](../../../src/Server/server_fuse.cpp#L400) |
| | `fate` | `debris` \| `disappear` | 0 | С учётом регистра, не более 31 символа. `debris` = 2, `disappear` = 3; любое другое значение оставляет 0. | [L404](../../../src/Server/server_fuse.cpp#L404) |
| `[damage_group]` | `group_name` | cstr | пусто | Как выше (без обработки `random`). | [L517](../../../src/Server/server_fuse.cpp#L517) |
| | `damage_type` | `absolute` или другое | может убить | `absolute` (здесь с учётом регистра) = не может убить. | [L521](../../../src/Server/server_fuse.cpp#L521) |
| | `hitpoints` | int | 0 | Снимаемые очки здоровья. | [L532](../../../src/Server/server_fuse.cpp#L532) |
| `[destroy_hp_attachment]` | `hardpoint` | cstr | пусто | Hardpoint, навесное оборудование которого уничтожается; `random` (без учёта регистра) выбирает любой. | [L596](../../../src/Server/server_fuse.cpp#L596) |
| | `fate` | `debris` \| `disappear` \| `loot` | 0 | С учётом регистра: `debris` = 2, `disappear` = 3, `loot` = 5; остальные оставляют 0. | [L600](../../../src/Server/server_fuse.cpp#L600) |
| `[damage_hp_attachment]` | `hardpoint` | cstr | пусто | Hardpoint, навесное оборудование которого повреждается. | [L725](../../../src/Server/server_fuse.cpp#L725) |
| | `damage_type` | `absolute` или другое | может убить | `absolute` (с учётом регистра) = не может убить. | [L729](../../../src/Server/server_fuse.cpp#L729) |
| | `hitpoints` | int | 0 | Снимаемые очки здоровья. | [L739](../../../src/Server/server_fuse.cpp#L739) |
| `[impulse]` | `hardpoint` | cstr | пусто | Hardpoint, в котором прикладывается импульс. | [L809](../../../src/Server/server_fuse.cpp#L809) |
| | `force` | float | 0.0 | Величина импульса. | [L813](../../../src/Server/server_fuse.cpp#L813) |
| | `radius` | float | 0.0 | Радиус действия. | [L817](../../../src/Server/server_fuse.cpp#L817) |
| | `damage` | float | 0.0 | Наносимый урон. | [L821](../../../src/Server/server_fuse.cpp#L821) |
| | `pos_offset` | float x3 | 0, 0, 0 | Смещение от hardpoint. | [L825](../../../src/Server/server_fuse.cpp#L825) |
| `[tumble]` | `ang_drag_scale` | float | 1.0 | Масштаб углового сопротивления при кувыркании. | [L961](../../../src/Server/server_fuse.cpp#L961) |
| | `turn_throttle_x`, `_y`, `_z` | float `[0]` нижняя, float `[1]` верхняя | 0..1 | Случайный диапазон входа поворота по этой оси. | [L965](../../../src/Server/server_fuse.cpp#L965), [L970](../../../src/Server/server_fuse.cpp#L970), [L975](../../../src/Server/server_fuse.cpp#L975) |
| | `throttle` | float `[0]` нижняя, float `[1]` верхняя | 0, 0 | Случайный диапазон тяги. | [L980](../../../src/Server/server_fuse.cpp#L980) |
| | `engine_kill` | bool | false | Двигатель отключается. | [L985](../../../src/Server/server_fuse.cpp#L985) |
| `[make_invincible]` | `turn_on` | bool | true | Включить или выключить неуязвимость. | [L1108](../../../src/Server/server_fuse.cpp#L1108) |
| `[dump_cargo]` | `origin_hardpoint` | cstr (`[0]`) | пусто | Hardpoint, из которого выбрасывается груз. | [L1171](../../../src/Server/server_fuse.cpp#L1171) |

(Клон действия `Tumble` сохраняет диапазоны turn-throttle из конструктора; это важно только для кода, копирующего fuse.)

---

## Файлы сохранений персонажей (`.fl`) и шаблоны

Сохранения персонажей, `Restart.fl`, `AutoSave.fl`, шаблоны новых персонажей и `NewPlayer.fl` — это INI-**текст**, разбираемый через `INI_Reader::open_memory`; формат BINI для них не распознаётся. Подробности о том, как записываются файлы и какие секции контроллеров они содержат: [saves-and-initial-world.md](saves-and-initial-world.md). В этом разделе перечислено то, что читает сам `Server.dll`.

**Контейнер.** Первые четыре байта выбирают формат ([server_savegame.cpp:352](../../../src/Server/server_savegame.cpp#L352)): `FLS1` = зашифрованный текст (остаток файла побайтно XOR-ится с ключом, выведенным из строки «Gene...» и индекса байта, `mask = (key[i & 3] + i) | 0x80`; [server_savefile.cpp:33](../../../src/Server/server_savefile.cpp#L33)); `FLSG` = отвергается (загрузка завершается неудачей); всё остальное = обычный текст с байта 0.

**Расположение.** Сохранения персонажей — `<hash>.fl` (хеш выводится из имени персонажа) в `<user data>\Accts[\MultiPlayer]\<account directory>`; `Restart.fl` и `AutoSave.fl` лежат в том же каталоге. `NewCharacter.fl` (одиночная игра), `MPNewCharacter.fl` (мультиплеер) и `NewPlayer.fl` ищутся **рядом с исполняемым файлом** (`bin\`; сборка копирует `mpnewcharacter.fl`).

### Ключи секции `[Player]`  ([PlayerData_LoadSaveRecord, server_savegame.cpp:345](../../../src/Server/server_savegame.cpp#L345))

Читаются только секции `[Player]` (остальные принадлежат DLL-контроллерам). Ключи с пометкой «repeat» могут встречаться многократно. Текст между строками с ключом `/*` и `*/` пропускается (вложенность учитывается). В начале каждой секции `[Player]` позиция, ориентация, система, база, location, описание, `descrip_strid`, группы оружия и список посещённого сбрасываются, деньги устанавливаются в 1968, а `interface` — в 3.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `name` | hex-текст или обычный текст (всё значение) | пусто | Имя персонажа. Если все символы — шестнадцатеричные цифры, оно декодируется как кодовые единицы UTF-16 по четыре hex-цифры каждая (`0041` = `A`), иначе читается как обычный многобайтовый текст. | [L419](../../../src/Server/server_savegame.cpp#L419) |
| `description` | как `name` | пусто | Описание сохранённой игры. | [L425](../../../src/Server/server_savegame.cpp#L425) |
| `descrip_strid` | int | 0 | Идентификатор строки встроенного описания. | [L431](../../../src/Server/server_savegame.cpp#L431) |
| `tstamp` | int `[0]` старшее, int `[1]` младшее | 0, 0 | Отметка времени файла; нужны оба значения, иначе оба равны 0. | [L435](../../../src/Server/server_savegame.cpp#L435) |
| `ship_archetype` | десятичный id **или** nickname (всё значение) | нет | Корабль. Значение, разбираемое как ненулевое целое, — это id архетипа, иначе значение хешируется. Должен предшествовать строкам `collision_group`. | [L445](../../../src/Server/server_savegame.cpp#L445) |
| `system` | nickname системы | 0 | Текущая система; неизвестное имя пишет в лог «invalid system name». | [L461](../../../src/Server/server_savegame.cpp#L461) |
| `base` | nickname базы | 0 | База, в которой пристыкован игрок; неизвестное имя пишет ошибку в лог. | [L471](../../../src/Server/server_savegame.cpp#L471) |
| `last_base` | nickname базы | не сбрасывается | Последняя посещённая база; неизвестное имя пишет ошибку в лог. | [L481](../../../src/Server/server_savegame.cpp#L481) |
| `location` | int | 0 | Id location внутри базы (location «Deck2» отображается в «Deck»). | [L491](../../../src/Server/server_savegame.cpp#L491) |
| `rank` | int | не сбрасывается | Ранг игрока. | [L495](../../../src/Server/server_savegame.cpp#L495) |
| `group` или `initial_rep` | nickname группы (всё значение, 15 символов) | - | Строит отношения игрока со всеми репутационными группами по чувствам группы; чувство 1.0 или больше заменяется на 0.95. Очищает предыдущие записи `house`. | [L499](../../../src/Server/server_savegame.cpp#L499) |
| `rep_group` | nickname группы (`[0]`) | - | Собственная группа игрока; пустое значение означает пустую группу. | [L510](../../../src/Server/server_savegame.cpp#L510) |
| `house` | float `[0]` чувство, str `[1]` группа | - | Добавляет одну запись репутации (repeat). | [L518](../../../src/Server/server_savegame.cpp#L518) |
| `voice` | str (всё значение, 31 символ) | пусто | Nickname голоса. | [L527](../../../src/Server/server_savegame.cpp#L527) |
| `com_body`, `com_head`, `com_lefthand`, `com_righthand`, `com_accessory`, `com_costume` | str (всё значение) | - | Костюм в окне связи: имена частей тела/головы/рук из `bodyparts.ini`, имя аксессуара (repeat) или nickname костюма целиком. Неизвестные части пишут предупреждения. **Сопоставление этих имён ключей чувствительно к регистру.** `com_hair` пишет «hair is no longer a valid bodypart». | [L233](../../../src/Server/server_savegame.cpp#L233) |
| `body`, `head`, `lefthand`, `righthand`, `accessory`, `costume`, `hair` | как выше | - | Те же ключи без префикса `com_` описывают костюм на базе (`hair` вызывает предупреждение и игнорируется). | [L233](../../../src/Server/server_savegame.cpp#L233) |
| `money` | float, усекается до int | 1968 | Кредиты. | [L557](../../../src/Server/server_savegame.cpp#L557) |
| `num_kills`, `num_misn_successes`, `num_misn_failures` | float, усекается до int | 0 | Статистика. | [L559](../../../src/Server/server_savegame.cpp#L559) |
| `pos` | float x3 | 0, 0, 0 | Позиция в космосе. | [L565](../../../src/Server/server_savegame.cpp#L565) |
| `rotate` | float x3, градусы | единичная | Углы Эйлера. | [L569](../../../src/Server/server_savegame.cpp#L569) |
| `starting_ring` | id (всё значение) | 0 | Кольцо trade lane, в котором начинает игрок. | [L576](../../../src/Server/server_savegame.cpp#L576) |
| `equip`, `base_equip` | `[0]` предмет, необязательный `[1]` hardpoint, необязательный `[2]` здоровье | - | Установленное оборудование в космосе / на базе (repeat); тот же формат строки, что у `equip` в loadout ([goods-loadouts-constants.md](goods-loadouts-constants.md#файлы-loadout-loadout)). | [L578](../../../src/Server/server_savegame.cpp#L578), [L580](../../../src/Server/server_savegame.cpp#L580) |
| `cargo`, `base_cargo` | `[0]` предмет, `[1]` количество, `[2]` hardpoint, `[3]` здоровье, `[4]` флаг миссии | - | Груз в космосе / на базе (repeat); тот же формат, что у `cargo` в loadout. | [L582](../../../src/Server/server_savegame.cpp#L582), [L584](../../../src/Server/server_savegame.cpp#L584) |
| `collision_group`, `base_collision_group` | `[0]` имя `obj` группы, `[1]` float здоровье | - | Состояние повреждений одной группы столкновений корабля (repeat). До `ship_archetype` или для имени, которого у корабля нет, пишется предупреждение, а строка пропускается. | [L586](../../../src/Server/server_savegame.cpp#L586), [L588](../../../src/Server/server_savegame.cpp#L588) |
| `wg` | сырая строка | - | Текст группы оружия; каждая строка добавляется с переводом строки (repeat). | [L590](../../../src/Server/server_savegame.cpp#L590) |
| `hull_status`, `base_hull_status` | float | не сбрасывается | Состояние корпуса в космосе / на базе. | [L594](../../../src/Server/server_savegame.cpp#L594) |
| `visit` | int `[0]` id, int `[1]` флаги (8 бит) | - | Запись о посещённой системе/базе/дыре (repeat). | [L598](../../../src/Server/server_savegame.cpp#L598) |
| `interface` | int | 3 | Биты состояния интерфейса. | [L604](../../../src/Server/server_savegame.cpp#L604) |
| `log` | `[0]` id mad-lib, `[1]` количество, затем количество x (данные, тег, флаги) | - | Одна запись журнала миссий (repeat). Тег 0x21 начинает вложенную запись, параметры которой идут в той же строке. | [L606](../../../src/Server/server_savegame.cpp#L606) |
| `logid` | - | - | Проверяется и игнорируется. | [L611](../../../src/Server/server_savegame.cpp#L611) |

`PlayerDB::ReadCharacterName` ([server_classes.cpp:3669](../../../src/Server/server_classes.cpp#L3669)) читает только `name` (то же hex-правило, тот же пропуск комментариев `/*` ... `*/`) первой секции `[Player]`, чтобы построить список персонажей; файл, который не удаётся загрузить, даёт пустое имя. После загрузки читатель выбранного персонажа также передаётся контроллерам как сообщение `PlayerLoad`.

### Шаблоны новых персонажей  ([PlayerData_CreateNewCharacter, server_savegame.cpp:1082](../../../src/Server/server_savegame.cpp#L1082))

`NewCharacter.fl` / `MPNewCharacter.fl` копируется секция за секцией в файл нового персонажа: каждый заголовок и каждая строка `key = value` переписываются, параметры объединяются через `, `, и применяются подстановки. Заполнители сопоставляются без учёта регистра с **параметром целиком**; код знает такие:

| Заполнитель | Где | Чем заменяется |
|---|---|---|
| ключ `%%Package%%` | как имя ключа | Строка `ship_archetype` выбранного пакета, за которой следует строка `equip = <id>, <hardpoint>` / `cargo = <id>, <count>` для каждого предмета из loadout пакета (предметы, чьего архетипа не существует, пропускаются). |
| `%%Name%%` | значение | Имя персонажа в hex-форме. |
| `%%Faction%%` | значение | Репутационная группа фракции. |
| `%%Money%%` | значение | Стартовые деньги пакета. |
| `%%Base_Costume%%`, `%%Comm_Costume%%`, `%%Voice%%` | значение | Костюм тела, костюм для связи и голос выбранного пилота. |
| `%%Home_System%%`, `%%Home_Base%%` | значение | Nickname системы домашней базы / самой домашней базы. Если база или система не найдена, текст заполнителя остаётся без изменений. |

Данные фракции/пакета/пилота/базы берутся из базы данных персонажей, указанной как `[Data] NewCharDB` ([data-driven-files.md](data-driven-files.md)). Если отсутствует любой из четырёх выборов, файл не создаётся; если шаблон не удаётся открыть или он является файлом `FLSG`, записывается пустой файл. Ключ без значений повторно выводит объединённое значение предыдущего ключа (поведение retail).

`NewPlayer.fl` — шаблон `Restart.fl`: `PlayerDB::create_restart_file` копирует его в `Restart.fl` и дописывает весь текст файла начального мира ([server_classes.cpp:3506](../../../src/Server/server_classes.cpp#L3506)).
