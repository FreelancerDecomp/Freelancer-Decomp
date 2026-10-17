# rpclocal: транспорт `IServer` / `IClient` и пакеты `CDAPacket`

`rpclocal.dll` — локальный RPC-прокси между `freelancer.exe` (клиентская половина) и `Server.dll`
(игровая логика) в одиночной игре. Он же задаёт модель пакетов, которую повторяют сетевые
`RemoteServer.dll` / `RemoteClient.dll`. Страница нужна тому, кто добавляет или правит пакет, диспетчер
или слот `IClient`/`IServer`. Общую картину модулей и последовательность подключения смотрите в
[server.md](server.md). Статусы функций, слоты и раскладки полезной нагрузки конкретных пакетов — в
decomp-tracker, заголовках и коде: реестр всех типов — `src/rpclocal/rpc_packet_registry.cpp`, нагрузки —
`rpc_payloads.h`, `rpc_payload_ch1/2/3.cpp`, сериализаторы кодеков — `rpc_wire_*.cpp`, контейнеры —
`rpc_containers.{h,cpp}`. Открытые расхождения с retail — в [deviations.md](deviations.md).

## Что это такое

Retail `rpclocal.dll` — не тонкая обёртка вокруг `Server.dll`, а двунаправленный прокси:

- `freelancer.exe` получает `IServer`; `IServerImpl` rpclocal почти каждым методом **сериализует вызов в
  пакет** и кладёт его в очередь «клиент → сервер» (`PushToServer`, vtable `+0x170`). Исключения, которые
  идут напрямую в `Server.dll`: `Startup`, `Shutdown`, `ElapseTime`, `Connect`, `DisConnect`.
- `Server.dll` получает `IClient` — собственный объект rpclocal (`GetClientInterface`, 126 слотов) и каждым
  слотом ставит в очередь «сервер → клиент» пакет; слот 9 (`+0x24`) — `PushToClient`.
- `IServerImpl::Update` вычерпывает очередь серверу-клиенту и `HandlePacket` вызывает слот **настоящего**
  `IClient` из exe (и `IChatClient` для канала 1 opcode 5); вычерпывание второй очереди идёт в
  `RpcDispatchToServer` (retail `FUN_06d86a70`), который вызывает слоты настоящего `IServer` из `Server.dll`.
- Заглушки в самом retail: `GetServerID` (всегда 0), `GetServerSig` (пустая строка), `GetServerStats` /
  `OnConnect` / `SetActiveConnection` (no-op), `SwapConnections` (всегда `true`).
- Экспорты `rpclocal.dll`: `GetClientInterface`, `GetChatClientInterface` (второй базовый класс того же
  объекта, один слот), `GetServerInterface`, `GetChatServerInterface`.

## Пакет `CDAPacket`

Базовый класс пакета — ровно `0x20` байт (`FPacketBase`, `src/include/rpc_packet.h`), дальше идут поля
конкретного типа:

| Смещение | Содержимое |
|----------|-----------|
| `+0x00` | `vptr`, 4 слота: деструктор, Encode (кодек A), Decode (кодек B), dump |
| `+0x04` | счётчик использования (конструктор ставит 1) |
| `+0x08` | зарезервировано под связь очереди |
| `+0x0C` | **`PacketId` = `Channel << 16 \| Opcode`**; диспетчеры читают именно его |
| `+0x20` | начало полезной нагрузки конкретного типа |

Полный размер объекта у каждого типа свой (аргумент его `operator new`, от `0x24` до `0xEC`). Пакеты в SP не
превращаются в байты: очередь хранит **указатели** (`FPacketQueue`/`FRpcConnection`), копии не делаются;
слоты Encode/Decode нужны сетевому транспорту. Уничтожение — слот 0 (`scalar deleting destructor`).

`Channel = PacketId >> 16`, `Opcode = PacketId & 0xFFF`; неизвестный канал диспетчер отбрасывает.

| Канал | Направление | Типов | Opcode | Кто разбирает |
|-------|-------------|------:|--------|---------------|
| 1 | в обе стороны («быстрый»: физика/бой/торговля/буферы состояния) | 21 | 1..0x1D | клиент: `HandlePacket`; сервер: `RpcDispatchToServer` |
| 2 | сервер → клиент (крупные игровые события) | 81 | 1..0x5B | `HandlePacket` |
| 3 | клиент → сервер (запросы игрока) | 60 | 1..0x47 | `RpcDispatchToServer` |

Всего **162** типа `FLPACKET_*`; регистрируют их три регистратора (`FUN_06d9ace0` — канал 1,
`FUN_06d9bc80` — канал 2, `FUN_06d9f100` — канал 3), а реестр хранится в `FPacketSerializer`
(`RegisterType(Id, Name, Factory)`), создаваемом при `Startup` и общем для всех экземпляров в DLL.
Таблица «opcode → слот `IClient`/`IServer`» читается из диспетчеров, а не хранится отдельно: в
`rpc_dispatch.cpp` и `rpc_payload_ch1/2/3.cpp` каждый case — столбец Deliver кодека соответствующего типа.

## Доставка

- **Очереди.** `CDAConnection` (`FRpcConnection`) держит три очереди указателей под одной критической
  секцией; `Connect` создаёт его и стартует (`PushToServer` без соединения молча теряет пакет).
- **Синхронная или в очередь.** Решение принимается по типу (`bSelfContained` кодека): самодостаточные
  пакеты (владеют всеми узлами, записями и строками) уходят в очередь, как в retail; «буферное семейство»
  канала 1 и `POP_UP_DIALOG` несут в нагрузке указатель вызывающей стороны (так делает сам retail) и
  доставляются синхронно; для типов без восстановленной раскладки нагрузки в порте лежат сырые dword
  аргументов — внутри процесса это самосогласованно, но не retail-формат.
- **Контейнеры.** Списки (снаряжение, группы столкновений, имена, миссии и др.) сериализуются
  `rpc_containers`: пакет владеет и узлами, и записями, и строками. Через границу `Common.dll` в нескольких
  местах остаётся приведение типов (`std::string`/`std::vector`) — их перечень ведёт deviations.
- **Модель потоков.** Run permission (`GiveServerRunPermission` / `WaitForClientRunPermission`) — обычные
  импорты `Common.dll`; порядок шагов `Update` описан в [server.md](server.md).

## Чего ещё нет

- Раскладка полезной нагрузки по типам пакетов ведётся в `rpc_payload_ch1/2/3.cpp` и в трекере; там, где она
  не восстановлена, в нагрузке лежат сырые dword аргументов (см. «Доставка»). Единственный тип канала 3 без
  отправителя и обработчика в самом retail — `GFREQUESTSHIPINFO` (0x0A).
- Распаковщики `HandleCharacterInfo` и `HandleDamageObject` (канал 2, op `0x03` / `0x05`) в retail вызываются
  мимо `HandlePacket`.
- Числовой вывод `RpcDumpServerLaunch` / `RpcDumpServerCharSelectVerified` отличается от MSVCP60 форматом
  `float`/`double` (см. [crt_compat.md](crt_compat.md)); на двоичную сериализацию это не влияет.
