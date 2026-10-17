# Server stack: Server.dll, rpclocal, RemoteServer/RemoteClient, FLServer

В Freelancer клиент и игровая логика общаются через два абстрактных интерфейса: `IServer` (клиент →
сервер, 92 метода + `PushToServer`) и `IClient` (сервер → клиент, 126 слотов). Одиночная игра запускает
сервер в том же процессе; сетевая играет те же вызовы через DirectPlay 8. Эта страница описывает пять
модулей, которые это обеспечивают, и способ их соединения; протокол пакетов вынесен в
[rpclocal.md](rpclocal.md). Нужна тому, кто работает над одиночной кампанией, сетевым режимом или
выделенным сервером. Статусы функций, слоты, раскладки аргументов и адреса — в decomp-tracker и в
заголовках `src/include/`; открытые расхождения с retail — в [deviations.md](deviations.md).

| Модуль | Исходники | Роль |
|--------|-----------|------|
| `Server.dll` | `src/Server/` | Игровая логика: игроки, корабли, базы, миссии, `pub::*` для Content |
| `rpclocal.dll` | `src/rpclocal/` | Локальный транспорт SP: прокси `IServer` для клиента и `IClient` для сервера, пакеты в очередях |
| `RemoteServer.dll` | `src/RemoteServer/` | Загружается **клиентом**: `IServer` поверх DirectPlay (с точки зрения клиента — «удалённый сервер») |
| `RemoteClient.dll` | `src/RemoteClient/` | Загружается **сервером**: `IClient`/`IChatClient` поверх DirectPlay (удалённые клиенты) |
| `FLServer.exe` | `src/FLServer/` | MFC-оболочка выделенного сервера |

Имена DLL обманчивы: `RemoteServer` стоит на стороне клиента, `RemoteClient` — на стороне сервера. Обе
собраны из одной кодовой базы (`Network\FLServerPacketHandler.cpp`) с дополняющими наборами `#define`; общие
кодеки лежат в `src/RemoteCommon/` и компилируются в обе DLL отдельными единицами. Это не DACOM-компоненты
(они не регистрируются в `COManager`), а обычные DLL с экспортами.

## Server.dll

Экспортирует `GetServerInterface()` (глобальный `IServerImpl`), `GetChatServerInterface()` и около
270 именованных символов (`SERVER_API`, определение по макросу `SERVER_EXPORTS`; `.def`-файла нет).
Поверхность сверяется с `dumpbin -exports EXE\Server.dll`. Content.dll импортирует из Server порядка двух
сотен символов пространства `pub::` (создание объектов, игроки, рынок, звук, AI, контроллеры, фантомы,
группы игроков) — это и есть контракт между игровой логикой и скриптами миссий.

Игровые объекты и базы данных (`PlayerDB`, `CObject`/`CEqObj`/`CShip`, `DataConfig`, `Reputation`, таймеры)
находятся в `Common.dll`, Server импортирует их; фасады порта — `src/include/server_*.h`, `common_server*.h`.
Подробности лежат в [common.md](common.md).

`IServerImpl::Startup(SStartupInfo)` инициализирует тайминг, `PlayerDB` (максимум игроков, флаг SP),
`DataConfig::startup` (читает `freelancer.ini`, секцию `[Initial SP DLLs]`, и подгружает модули Content:
GameSupport, SpaceSupport, BaseSupport и др.) и вызывает у `IClient` слот 10 (в rpclocal он подгружает exe и забирает настоящий `IClient`; должен вернуть `true`).
Значимые поля `SStartupInfo` (размер `0x214`): `+0x000` путь модуля (`0x104` байта), `+0x208` максимум
игроков, `+0x210` `bAllowPvp`, `+0x211` `bAllowNewPlayers`; сервер использует их для урона между игроками и
создания аккаунтов. `Update` в Server — лишь heartbeat; физику проталкивает `ElapseTime`.

Начальное состояние SP: `Restart.fl` собирается из шаблона `NewPlayer.fl` рядом с exe и `initialworld.ini`
текущего `DATA` и затем переиспользуется (смена набора DATA его не пересоздаёт). Блокировки прыжковых дыр
хранятся в `[locked_gates]`/`[mPlayer]` и привязаны к конкретному набору миссий. Неразрушаемость статических
объектов защищает их корпус `SUBOBJ_ID_ROOT`, который приходит из `CEqObj::inst_to_subobj_id`.

## Одиночная игра: как клиент подключается к серверу

```
freelancer.exe  (server_connect.cpp)
  LoadLibrary("rpclocal.dll")  -> GetServerInterface() -> IServerImpl (rpclocal)
  IServer::Startup(SStartupInfo)                     [rpclocal]
    запоминает путь exe из SStartupInfo (+0x000)
    LoadLibrary("Server.dll") -> GetServerInterface / GetChatServerInterface
    копирует 0x214 байта SStartupInfo, ПОДМЕНЯЕТ первые 0x104 байта путём rpclocal.dll
    Server::Startup(копия)                           [Server.dll]
      Server делает LoadLibrary(ModulePath) = rpclocal.dll,
      GetProcAddress("GetClientInterface") -> IClient rpclocal (126 слотов)
      IClient слот 10: LoadLibrary(путь exe) -> GetProcAddress("GetClientInterface")
                       -> настоящий IClient клиента
    создаёт сериализатор пакетов и регистрирует типы
  IServer::Login / CharacterSelect / ... -> пакеты канала 3 -> Server.dll
  каждый кадр: IServer::Update, ElapseTime
```

Следствия: (1) оба — exe и rpclocal — экспортируют `GetClientInterface`, и это не противоречие; (2) `ModulePath`
лежит по смещению `+0x000`; (3) нельзя смешивать retail `rpclocal` из `EXE\` с нашим `Server` — они обязаны
грузиться из одной директории (`bin\`), а ненайденное имя должно падать громко, без fallback в `EXE\`;
(4) если слот 10 `IClient` не вернёт `true`, подключение не состоится.

Каждый кадр `IServerImpl::Update` (rpclocal): `GiveServerRunPermission` → вычерпать очередь пакетов
сервера клиенту (`HandlePacket` вызывает слоты настоящего `IClient`) → `WaitForClientRunPermission` →
обновить клиентский прокси и вычерпать очередь клиента серверу. `ElapseTime` — прямой хвостовой переход в
`Server::ElapseTime`. Эти разрешения (`Give…`/`Wait…RunPermission`) — обычные импорты `Common.dll`.

## rpclocal и протокол

Подробно — [rpclocal.md](rpclocal.md). Кратко: rpclocal не тонкая обёртка, а двунаправленный RPC-прокси.
Почти каждый метод `IServerImpl` сериализует вызов в `CDAPacket` (канал 1 или 3) и кладёт указатель в очередь;
слоты `IClient` rpclocal делают то же в канале 2. Внутри процесса пакеты не превращаются в байты: очередь
хранит указатели; кодеки нужны сетевому транспорту.

## Сетевой режим: RemoteServer и RemoteClient

**Клиент → сервер.** `RemoteServer.dll` экспортирует `Server` (производный `IServerImpl` с 93+1 слотом).
Исходящий вызов (`FireWeapon` и т. п.) выбирает тип пакета в сериализаторе (канал/opcode), заполняет поля и
отправляет через `CDPClient::Send` (DALib, DirectPlay 8). Кадровый `Update`: `GiveServerRunPermission` →
`CDPClient::DispatchMsgs(100)` → `WaitForClientRunPermission`; полученный буфер несёт двухбайтовый
заголовок (opcode, затем channel), приёмник создаёт пакет нужного типа, декодирует и вызывает
`HandlePacket`, который освобождает пакет и дергает слот `Client` (`IClient` из `freelancer.exe`).
`CDPClient` (`0x90` байт) — клиентская сессия DP8: две очереди сообщений (входящая и рабочая кадра),
GUID приложения, список попыток подключения, обработчики `OnReceive`/`OnConnect`/`OnDisconnect`.

**Сервер → клиент.** `RemoteClient.dll` экспортирует `Client` и административный API. `Startup` создаёт
`CDPServer`, запускает хостинг, регистрирует типы COMMON, SERVER, CLIENT (следующий этап — только если
предыдущий успешен). Подключение занимает первый свободный слот (идентификаторы игрока 1–128 — это не
`PlayerId` DirectPlay), отправляет `CONNECTRESPONSE` и уведомляет локальный `Server.dll`. Входящий пакет
декодируется, поля превращаются в вызовы `Server`; ошибка выделения или декодирования вызывает `Disconnect`.
Исходящий пакет кодируется в буфер `0x2800` (при неудаче `0x19000`) и уходит в `CDPClientProxy::Send`.

Общие правила сетевых пакетов: формат на проводе определяют только кодеки полей (раскладка контейнеров VC6,
padding структур и указатели в сеть не попадают; наши контейнеры — современные STL с поэлементной
сериализацией). Особенности retail, которые сохранены намеренно: длина второго блока диалога
(`FmtStr::flatten`) объявляется, но копируется по длине первого; при принятии сделки идентификатор
записывается во входящий буфер, а не читается; вместимость имени персонажа различается (`RemoteClient` — 24
символа, `RemoteServer` — 64), поэтому декодер каждой DLL имеет свой порядок копирования; в цикле приёма
`RemoteClient` указатель продвигается только на заголовок. Буферные COMMON-пакеты различают заимствование,
передачу владения и копирование; в провод идут длина и содержимое.

`SetRemoteClientResponseData` формирует ответ обнаружения хоста: ASCII-префикс с флагами PvP и допуска
новых игроков, версией, ID и сигнатурой сервера и UTF-16 описание.

## FLServer.exe

MFC-приложение (в порте — динамический MFC 14, multibyte), одно документо-ориентированное окно:
консоль (`CFLServerView`) и форма сводки (`CFLSummaryForm`), которые переключает `SwitchView`.
`InitInstance` загружает `ServerResources.dll` (`src/Resources/serverresources`) как ресурсный handle: все
меню и диалоги берутся оттуда.

- **Экспорт.** Retail экспортирует 71 имя: 70 — собственные копии членов классов общих заголовков
  (`CAccount`, `PlayerDB`, `IServerImpl`, `CDPServer`, `CGunWrapper`, `CDeadReckonedVector`, …), побайтно
  совпадающие с копиями DLL-владельцев, и `ServerLogf`. Порт воспроизводит поверхность через
  `src/FLServer/FLServer.def`: 70 форвардеров на экспорты DLL-владельцев (порядковые номера как в retail)
  и один собственный; `.def` здесь ничего не урезает, а лишь пересылает.
- **Потоки.** Стартовый (запускает `IServerImpl::Startup`, итог собирает `PollStartupThread`); серверный
  (грузит `motd.dat`, цикл `Update` / `ElapseTime` под `GServerMutex` с handshake run permission и записью
  времени кадра/`ServerStats` в документ); «gun» (раз в секунду ведёт `CGunWrapper` — list server/
  matchmaking; только если включена публикация); завершающий (`IServerImpl::Shutdown` при модальном диалоге
  ожидания). `LockServer`/`UnlockServer` сериализуют доступ UI-потока к данным Server.dll.
- **Конфиг.** Диалог настроек хранит `FLServer.cfg` в пользовательском каталоге (двоичный, без padding):
  версия (записывается 3), имя сервера (UTF-16), описание, пароль, «Allow New Players», «Internet
  Accessible», максимум игроков 1..128, «Allow Players to Harm Other Players» (с версии 3). Ключ командной
  строки `/C` принимает сохранённые настройки без показа диалога.
- **Журнал.** `ServerLogf(ErrorCode, fmt, …)` фильтрует по уровню (младший ниббл) и категории, пишет в
  `OutputDebugString`, консоль, опциональный файл и (уровни 0/1) в `c:\fl-server-errors.log`; установлен как
  `FDUMP`-хук DACOM на время жизни консоли и передан `RemoteClient` через `SetServerLogFunction`.
- **Краш-отчёт.** Необработанное исключение закрывает DP-сессию и запускает `dw15.exe` с общим блоком (как
  клиентский фильтр в `freelancer.exe`; две копии, общей библиотеки нет).
- **Чат.** Окна чата (аккаунт / система / вся вселенная) шлют `IServerImpl::SubmitChat` с двухузловым
  rich-text списком в `BinaryRDLWriter`; хуки приёма устанавливаются при инициализации.
- **Код, который retail собирает, но не вызывает**, не портирован либо перенесён как «только класс»:
  диалог «Server Settings» (130), второй загрузчик `motd.dat`, две функции `CFLServerApp`; `CCreateAccountDlg`
  и `CServerInfoDlg` существуют как классы без конструкторов в графе вызовов.
- Разделение заголовков: `server_iserver.h` (`IServer`, `IChatServer`, хуки чата) и
  `server_iserverimpl.h` (`IServerImpl` Server.dll) выделены так, чтобы MFC-код не тянул `CObject` Freelancer
  и DX8-заголовки; `common_server.h` не должен включать `server_iserverimpl.h` (rpclocal объявляет свой
  `IServerImpl`).

## Известные ограничения

- `IServer` — ровно 92 слота (`+0x000..+0x16c`); 93-й слот `PushToServer` (`+0x170`) принадлежит каждому
  `IServerImpl`, а не интерфейсу. Раскладки аргументов (`XFireWeaponInfo`, `SSPObjUpdateInfo`, …) и сигнатуры —
  `common_server.h`, `common_server_types.h`, карточка `IServer` в трекере.
- Многие `pub::SpaceObj`/`pub::Player` функции упрощены (создание объектов без полного пайплайна лодаута,
  AI-формации, физические импульсы через сообщения вместо прямых вызовов IVP и т. п.) — по-функционально это
  `simplified` в трекере, архитектурно — раздел Server.dll в deviations.
- Из 162 типов пакетов раскладка payload восстановлена не для всех (см. [rpclocal.md](rpclocal.md)).
- Сетевые пути SERVER-канала требуют сверки «отправитель — кодек — приёмник» для каждого типа; наличие
  класса в реестре само по себе не доказывает совпадение с retail.
- Игровая логика `IServer` остаётся в `Server.dll`; её нельзя подменять загрузкой `RemoteServer.dll` или
  локальным SP-транспортом.
