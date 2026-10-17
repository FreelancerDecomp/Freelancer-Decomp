# INI-файлы, которые находятся через другие INI-файлы

Большинство INI-файлов игры не перечислены ни в коде, ни в `freelancer.ini`: их имена записаны *внутри других файлов данных*, и читатель открывает то имя, которое нашёл. На этой странице перечислены такие файлы, ключ, который их называет, и код, который их открывает. Ключи и секции внутри каждого файла описаны на странице, которой принадлежит соответствующая тема.

**Как они находятся.** Ссылающийся ключ содержит путь. Он разрешается относительно папки Data (см. «How file paths are found» в [parsers.md](parsers.md)), если в таблице не сказано иное; единственное исключение — файлы систем, названные в `universe.ini`, пути которых отсчитываются от папки `universe.ini`. Существование цели ничем не проверяется, кроме результата `open` у самого читателя: файл, который не удалось открыть, молча пропускается (несколько читателей пишут ошибку в лог). Файлы, имена которых — литералы в исходном коде, в эту таблицу *не* входят; они описаны вместе со своей темой (список `[Data]` см. в [freelancer-ini.md](freelancer-ini.md), файлы с фиксированными именами — на соседних страницах).

## Ссылки, определяемые данными

| Файл(ы) | Кем назван | Ключ | Кто открывает |
|---|---|---|---|
| Файлы систем `Systems\<Sys>\<Sys>.ini` | `universe.ini`, каждая секция `[System]` | `file` (относительно папки `universe.ini`) | загрузчик `Universe` в [common_universe.cpp](../../../src/Common/common_universe.cpp) (`LoadSystemFile`, `LoadSystemObjectPool`); сервер перечитывает их в [server_classes.cpp](../../../src/Server/server_classes.cpp) (`StarSystem::load`) и [server_pub.cpp](../../../src/Server/server_pub.cpp); Content — в [SystemInfo.cpp](../../../src/Content/SystemInfo.cpp) (`FSystemInfo::LoadFromReader`); клиент — в [star_sys.cpp](../../../src/freelancer/Client/star_sys.cpp) (`StarSysLoadIni`) |
| Файлы баз `Universe\Systems\<Sys>\Bases\<base>.ini` | `universe.ini`, каждая секция `[Base]` | `file` | `BaseData::read_from_ini` в [common_basedata.cpp](../../../src/Common/common_basedata.cpp); серверная копия — в [server_pub.cpp](../../../src/Server/server_pub.cpp) |
| Файлы комнат `...\Bases\Rooms\*.ini` | файл базы, каждая секция `[Room]` (с `nickname`) | `file` | `RoomData::read_from_ini` в [common_roomdata.cpp](../../../src/Common/common_roomdata.cpp). Для `[Room]` нужны и `nickname`, и `file`, иначе секция отбрасывается; прочие ключи в блоке пишут в лог «Invalid ini data». |
| Файлы астероидных полей `solar\asteroids\*.ini` | файл системы, каждая секция `[Asteroids]` (ключ `file`, а `zone` называет заполняемую зону) | `file` | сервер: `SrvAsteroidSystem::load` / `FSrvAsteroidField::load` в [server_classes.cpp](../../../src/Server/server_classes.cpp); клиент: `CliAsteroidLoadFieldIni` в [cli_asteroid.cpp](../../../src/freelancer/Client/cli_asteroid.cpp); Common: `CAsteroidField::load` в [common_casteroidfield.cpp](../../../src/Common/common_casteroidfield.cpp) |
| Файлы туманностей `solar\nebula\*.ini` | файл системы, каждая секция `[Nebula]` (каждый `file` создаёт одну туманность; `zone` называет её зону) | `file` | `FNebulaEffect::Load` в [cli_nebula.cpp](../../../src/freelancer/Client/cli_nebula.cpp) (только клиент) |
| Таблицы форм / текстурных панелей (например `universe\heavens\shapes.ini`, `solar\asteroids\*_shapes.ini`) | файл системы, файл астероидного поля или файл туманности, секция `[TexturePanels]` (повторяется) | `file` | `FTexturePanels::Load` в [texture_panels.cpp](../../../src/freelancer/Client/texture_panels.cpp), который передаёт данные в `FShapeDB::Load` в [texture_shapes.cpp](../../../src/freelancer/Client/texture_shapes.cpp); читатели систем и полей/туманностей вызывают `ShapeDB::Load` напрямую с `GetFullFilename` |
| Текстурные панели колец `solar\rings\*.ini` | файл системы, секция `[Object]` | `ring = <zone>, <file>` (два параметра) | `FClientSolarGameobj::ReadIni` в [client_gameobj.cpp](../../../src/freelancer/Client/client_gameobj.cpp) |
| Файлы кокпитов `cockpits\<nation>\*.ini` | `ships\shiparch.ini`, `[Ship]` | `cockpit` | клиент сохраняет имя в [client_cliobj.cpp](../../../src/freelancer/Client/client_cliobj.cpp) (`FCliShip::ReadUnknown`); [cockpit_load.cpp](../../../src/freelancer/Client/cockpit_load.cpp) разрешает его относительно пути Data при создании корабля игрока и передаёт четырём загрузчикам камеры. Корабль без этого ключа пишет предупреждение в лог и использует `cockpits\cockpit.ini`. |
| Файлы параметров энкаунтеров `missions\encounters\*.ini` | файл системы, каждая секция `[EncounterParameters]` | две идущие подряд строки: `nickname` (ID энкаунтера), затем `filename` | `FSystemInfo::LoadFromReader` в [SystemInfo.cpp](../../../src/Content/SystemInfo.cpp) через `OpenContentDataIni`, который создаёт `FEncounterParameters` ([EncounterParams.cpp](../../../src/Content/EncounterParams.cpp)). Две строки читаются **позиционно**; имена ключей не проверяются. Загружаются только полной загрузкой системы (`FSystemInfo::LoadFull`), а не облегчённой `Load(false)`, которая используется для запросов путей. |
| Таблицы NPC-кораблей `missions\m##\npcships.ini` | файл миссии, `[Mission]` | `npc_ship_file` | `LoadMissionHeaderValues` в [MissionObject.cpp](../../../src/Content/MissionObject.cpp) через `OpenContentDataIni`, затем `LoadNpcShipArchs` |
| Скрипты внутриигровых роликов («RTC») `missions\m##\*_nrml.ini`, `*_offer.ini`, `*_reoffer.ini` | файл миссии, строки триггеров | `Act_AddRTC = <path>[, repeatable]`, `Act_RemoveRTC = <path>`, `Cnd_RTCDone = <path>` | `FCharacterEncounter::Load` в [RtcScripts.cpp](../../../src/Content/RtcScripts.cpp), запускается из [MissionScript.cpp](../../../src/Content/MissionScript.cpp) (`StartRtcScript`); скрипт также создаётся заново по строкам `rtc` сохранённой секции `[MissionState]` |
| Файлы сюжетных миссий (например `Missions\M01a\m01a.ini`) и генерируемых миссий | сюжетные контроллеры, а для генерируемой миссии — параметры создания (сохранение записывает их как второе значение `Mission` в `[StoryInfo]`) | путь, передаваемый конструктору миссии | `FMissionScript::LoadMissionIni` в [MissionScript.cpp](../../../src/Content/MissionScript.cpp), см. [missions.md](missions.md) |
| Таблица форм HUD `interface\HUD\hud_txm.ini` | `interface\hud.ini`, `[HUD]` | `shapes` | `LoadHudElementsFromIni` в [hud_element_list.cpp](../../../src/freelancer/Client/hud_element_list.cpp), который передаёт данные в `FShapeDB::Load` |
| Retail-dev include-файлы DACOM `FL_Dev.ini`, `FL_DevOnlyLibs.ini` | `DACOM.INI` / `dacomsrv.ini` | `@include <file>` | парсер DACOM, [dacom.cpp](../../../src/dacom/dacom.cpp). Отсутствующий include молча игнорируется. Dev-файл `bin\dacomsrv.ini` по-прежнему начинается с `@include FL_Dev.ini` — файла, которого в `bin` нет. |

Каждый целевой файл читается обычным `INI_Reader` (текст или BINI), кроме include-файлов DACOM. Файлы, на которые указывает каждая из строк выше, перечислены здесь только по называющему их ключу; структура самих файлов описана в:

* файлы систем, баз и комнат: [world-data.md](world-data.md) (взгляд Common) и [systems-and-bases.md](systems-and-bases.md) (взгляд Content)
* параметры энкаунтеров: [ai-and-encounters.md](ai-and-encounters.md)
* файлы миссий, таблицы NPC-кораблей и RTC-скрипты: [missions.md](missions.md)
* астероидные поля, туманности, кольца: [client-world-fx.md](client-world-fx.md)
* таблицы форм и текстурных панелей: [client-audio-misc.md](client-audio-misc.md)
* файлы кокпитов: [client-ui-config.md](client-ui-config.md)

## Файлы с фиксированными именами, создаваемые во время работы

Три таблицы *записываются* Content и считываются обратно по имени: `Universe\systems_shortest_path.ini`, `Universe\shortest_legal_path.ini` и `Universe\shortest_illegal_path.ini`. Content записывает их по жёстко заданному относительному пути `..\data\Universe\...` (относительно рабочего каталога, а не настроенного пути к данным), когда перестраивает свои таблицы связей систем, и читает их через `OpenContentDataIni`. См. [systems-and-bases.md](systems-and-bases.md).

## Файлы без читателя в исходном коде

При проверке набора данных retail были найдены INI-файлы, для которых в `src` читатель не обнаружен: `concave.ini` (в корне Data), `MISSIONS\factionsets.ini`, `MISSIONS\mshipprops.ini` и `INTERFACE\HUD\hudtarget.ini`. Их имена не встречаются в исходном коде как литералы, и ни один файл данных их не называет. Это лишь показывает, что порт их не читает; читает ли их retail-исполняемый файл, не проверялось (в retail-`freelancer.ini` указан `concave.ini`, см. [freelancer-ini.md](freelancer-ini.md)).

## Что не является читателем INI

Несколько модулей упоминают `.ini` в своих исходниках лишь как часть других слов (например `.Init(`); ни один из них не читает конфигурацию:

* Библиотеки движка и поддержки без какого-либо доступа к INI: `ximage`, `x86math`, `ReadFile`, `SoundStreamer`, `flmaterials`, `Thorn`, `IVP`, `qhull`, `zlib`, `imeui`, `movie`, `Editor`, `MemoryDiagnostics`.
* Сеть: `RemoteClient`, `RemoteServer`, `RemoteCommon` и `rpclocal` берут настройки из структуры `SStartupInfo`, которую заполняет клиент или сервер, а не из файлов.
* `luaprofile.dll` реализует интерфейс `IProfileParser` поверх глобальных переменных Lua; INI-текст он не разбирает.
* Функции профиля Win32 (`GetPrivateProfileString` и родственные) нигде в проекте не используются.
* `bin\flconfigdatabase.txt` (список особенностей оборудования RP8) и сгенерированные списки в `src\Server\retail_exports.txt` не являются INI-файлами.

Модули, которые читают `DACOM.INI` через парсер DACOM, а не через `INI_Reader` (`dacom`, `EngBase`, `RendComp`, `RP8`, `Shading`, `SoundManager`, `system`, `alchemy`, `Deformable2`), описаны в [engine-dacom.md](engine-dacom.md).
