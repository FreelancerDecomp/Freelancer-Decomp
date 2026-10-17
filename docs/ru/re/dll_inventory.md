# Инвентарь DLL Freelancer

Карта модулей клиента: что делает каждая DLL retail-игры, где лежит её порт и портирована ли она вообще. Нужна, чтобы быстро понять, в какой каталог `src/` идти и что из `EXE\` — только эталон. Подробности по модулю — `docs/re/<модуль>.md` (если есть); поэлементный статус функций, глобалов и vtable — в decomp-tracker, прогресс-цифр здесь нет намеренно. Вызов по чужому vtable — [cross_dll_protocol.md](cross_dll_protocol.md).

Статус: **порт** — есть `src/<модуль>/` и сборка в `bin\`; **нет** — порта нет. Полнота порта — в трекере и в `deviations.md`, не здесь.

Retail-DLL никогда не загружаются: `bin/DACOM.INI` задаёт `DllPath = .\`, а каждый `[Libraries]` указывает в `bin\`. Список `[Libraries]` в retail — `EXE/DACOM.INI`; `FL_DevOnlyLibs.ini` оттуда в репозитории отсутствует.

## Движок (цепочка DACOM, порядок — `[Libraries]`)

| DLL | Роль | Исходники | Статус |
|-----|------|-----------|--------|
| `dacom.dll` | ядро: `COManager`, встроенный INI-парсер (`IProfileParser`), CRC-реестр, `FDUMP`, `LogStream` | `src/dacom` | порт |
| `DALib.dll` | `DALib::Startup/Shutdown`, поднимает остальной движок; в retail тянет zlib по ординалам | `src/DALib` | порт |
| `ReadFile.dll` | `FileSystem` (DOS/UTF/MEM), `SearchPath` | `src/ReadFile` | порт |
| `x86math.dll` | `3DMathEngine` | `src/x86math` | порт |
| `EngBase.dll` | `Animation`, `Hardpoint`, `Engine`, `ChannelArchetype` | `src/EngBase` | порт |
| `system.dll` | `SystemContainer`, загрузка `[System]`-компонентов | `src/system` | порт |
| `RP8.dll` | D3D8-конвейер (`DirectX8`) | `src/RP8` | порт |
| `SoundManager.dll` / `SoundStreamer.dll` | SFX (DirectSound/EAX) и потоковое аудио (ACM); создают DirectSound через `CoCreateInstance` | `src/SoundManager`, `src/SoundStreamer` | порт |
| `Deformable2.dll` | деформируемые меши, скининг, IK | `src/Deformable2` | порт |
| `Thorn.dll` | THORN: скрипты сцен `.thn`, встроенный Lua 3.2, кривые | `src/Thorn` | порт |
| `Shading.dll` | текстуры, материалы, батчер | `src/Shading` | порт |
| `RendComp.dll` | `VMesh`, `TriMesh`, `SphereMesh`, `RenderManager` | `src/RendComp` | порт |
| `alchemy.dll` | частицы / FX | `src/alchemy` | порт |
| `ximage.dll` | `BigImage` (крупные растры интро/RichDisplay) | `src/ximage` | порт |
| `flmaterials.dll` | игровые материалы FL, `FLAppearance` | `src/flmaterials` | порт |
| `zlib.dll` | C API (не DACOM, не в `[Libraries]`); retail `DALib` импортирует его по ординалам | `src/zlib` | порт |
| `Common.dll` | общий рантайм: `Csys`, INI, сеть, hardpoint, физические объекты; ~3.9k экспортов, C++ классы, не фабрики. `freelancer.exe` линкуется с нашим `Common.lib` | `src/Common` (+ `src/IVP`, `src/qhull`) | порт |
| `luaprofile.dll` | Lua 3.2 VM, DACOM-компонент `"ILua"`; в retail не грузится, в `[Libraries]` нет. `IProfileParser` отвечает встроенный парсер dacom | `src/luaprofile` | порт |
| `movie.dll` | интро-видео (`MediaPlayer::*`, граф WM ASF Reader) | `src/movie` | порт |
| `imeui.dll` | IME UI (CJK) | `src/imeui` | порт |

`IVP` (физика) и `qhull` — часть сборки `Common`; общий кросс-модульный ABI публикуется в `src/include/`.

## Клиент, игровой мир, сервер

| DLL | Роль | Исходники | Статус |
|-----|------|-----------|--------|
| `freelancer.exe` | точка входа, меню, космос, UI | `src/freelancer` | порт |
| `Content.dll` | миссии, AI, популяторы; грузится из `[Initial SP DLLs]` после инициализации движка; лежит в `DLLS/BIN/` | `src/Content` | порт |
| `rpclocal.dll` | локальный SP-сервер, тонкая обёртка над `Server.dll` (`IServer`) | `src/rpclocal` | порт |
| `Server.dll` | серверная часть мира (SP и dedicated) | `src/Server` | порт |
| `FLServer.exe` | dedicated-сервер | `src/FLServer` | порт |
| `remoteserver.dll` / `RemoteClient.dll` | MP-транспорт (клиентская и серверная стороны одной кодовой базы `Network\`) | `src/RemoteServer`, `src/RemoteClient`, `src/RemoteCommon` | порт |

## Ресурсные DLL

`resources`, `InfoCards`, `MiscText`, `MiscTextInfo2`, `NameResources`, `EquipResources`, `OfferBribeResources`, `serverresources` — чисто ресурсные (`/NOENTRY`: STRINGTABLE, RT_HTML, меню), пересобираются `rc.exe` из `src/Resources/` в `bin\`; статус **порт**. Подробнее — [resources.md](resources.md).

## Без порта

| DLL | Роль | Комментарий |
|-----|------|-------------|
| `jflp.dll`, `HudShift.dll` | моды: патчат код по жёстким VA | **нельзя** добавлять в `bin/DACOM.INI` — падение; статус: нет |
| `gundll.dll` | GUN list server (COM) | нет |
| `ebueula.dll` | EULA-диалог | нет; сторонний retail-бинарник, зовётся из `freelancer.exe` как есть |
| `unicows.dll` | Layer for Unicode (Win9x) | нет, не нужен на Win10+ |
| `debuglib.dll` | dev-оверлей отладки | нет |

## Порядок старта клиента

`freelancer.exe` `WinMain` → `DALib::Startup` (читает `DACOM.INI`, грузит `[Libraries]`) → открытие `FileSystem` и проверка наличия компонентов (`IHardpoint`, `IRenderer`, `ITextureLibrary`, `IMaterialLibrary`, `IAlchemy`, `IMaterialBatcher`) → инициализация RP8/D3D8 → ресурсные DLL из `freelancer.ini` → модули Content → соединение через `rpclocal`. Участки, где `freelancer.exe` делает работу сам, а не вызывает порт модуля, — в [substitutions.md](substitutions.md).
