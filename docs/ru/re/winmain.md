# WinMain: порядок старта клиента

Как `freelancer.exe` поднимается до игрового цикла и в каком порядке зависит от остальных модулей. Источник — `WinMain` retail-exe (`FUN_005b2f50`, исходник `E:\FL\Scratch\Source\Client\main.cpp`); наш код — `src/freelancer/Client/main.cpp`, `engine_init.cpp`. Статус отдельных функций — в decomp-tracker; пробелы exe — [freelancer.md](freelancer.md).

## Порядок

1. Mutex `FreelancerClient`; `SetCurrentDirectory` на каталог exe (рабочий каталог в VS после этого не действует).
2. Проверка `..\DATA\CHARACTERS\ANIMATIONS\FACIALMALE.ANM`; при отсутствии retail запускает `FLServer.exe` и выходит.
3. EULA (`FUN_005b2d60`, `RunEulaGate`): грузится сторонний `EBUEula.dll` из `bin\` как есть, вызывается его экспорт `EBUEula`; отказ → код возврата 1.
4. `CoInitialize`, `LoadLibrary(resources.dll)`, `DACOM_GetDllVersion`, spew-лог.
5. `LoadStartupConfig` (`FUN_005621a0`): `freelancer.ini`, путь к данным; затем разбор параметров дисплея (`ApplyDisplayCmdlineOverrides`) — **до** создания окна, как в retail.
6. `RegisterClass("Freelancer")`, `CreateGameWindow` (`FUN_004246e0`).
7. `InitializeEngine` (`FUN_005b1300`): `DALib::Startup` / `OpenData(data_path)` (загрузка `[Libraries]` из `DACOM.INI`), проверка наличия обязательных компонентов (`FDUMP` при ошибке), `Async::Startup`, `Timing::init`, `Pipeline::Init`, `PerfOptions.ini`, проверка render device, `DataConfig::startup`.
8. Игровой цикл: `UpdateClientTiming` (`FUN_005b2360`) + `PumpMessages` (`FUN_005b0b60`).

## Командная строка

Разбор в `FUN_005b2cb0` (`ApplyClientCommandLineFlags`): `+G`/`-G`, `+P<порт>`, `+S<host:port>`; знак игнорируется, регистр тоже, токен без знака ничего не значит. Параметры дисплея (оконный/полноэкранный режим) разбирает `ApplyDisplayCmdlineOverrides`. Других строковых проверок командной строки в retail `WinMain` нет; быстрый старт в SP — через автологин `[Login]` в `freelancer.ini` (`FUN_005b20a0`).

Выходной каталог сборки — `bin\` (рядом с `DATA` через `..\DATA`).
