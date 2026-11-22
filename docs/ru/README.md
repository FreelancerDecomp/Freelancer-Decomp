# Freelancer Decomp — документация

[English](../README.md) · **Русский**

Freelancer Decomp — это переписанный с нуля по исходному коду клиент игры **Freelancer**
(Digital Anvil / Microsoft, 2003). Каждый модуль оригинального клиента заново собирается из
исходников на C++, которые сверяются с листингом retail-дизассемблера. Цель — рабочий клиент,
поведение которого совпадает с оригиналом, с задокументированными внутренностями, которые можно
изменять. См. [MISSION.md](MISSION.md).

Проект **не содержит игровых ресурсов**, а из retail-бинарников в нём только два сторонних
компонента, которые грузятся без изменений: `EBUEula.dll` и `EXE\gundll.dll` (см.
[корневой README](../../README.ru.md)). Чтобы собрать и запустить его, нужна собственная
установка Freelancer (см. [Локальная раскладка](#локальная-раскладка)).

## С чего начать

| Вы хотите… | Читать |
|--------------|------|
| Узнать, что настраивается и где читается каждый INI-файл | [ini/README.md](ini/README.md) — справочник по каждому INI-файлу, секции и ключу, которые читает код |
| Понять, как модули связаны между собой | [re/dll_inventory.md](re/dll_inventory.md) — единая таблица всех DLL, их ролей и каталогов исходников |
| Вызвать другой модуль / выяснить, что такое слот интерфейса | [re/cross_dll_protocol.md](re/cross_dll_protocol.md) — методика |
| Увидеть, чем порт отличается от retail | [re/deviations.md](re/deviations.md) — реестр известных расхождений; [re/substitutions.md](re/substitutions.md) объясняет понятие «подстановка» |
| Работать над одной подсистемой | документы по модулям ниже |
| Проверить статус отдельной функции | decomp-tracker (MCP-сервер), а не markdown — markdown хранит только то, что не помещается в запись по функции |

## Документы по модулям (`re/`)

| Область | Документы |
|------|-----------|
| Ядро движка | [dacom](re/dacom.md) (менеджер объектов, встроенный INI-парсер) · [dalib](re/dalib.md) · [readfile](re/readfile.md) (файловая система) · [x86math](re/x86math.md) · [system](re/system.md) · [engbase](re/engbase.md) · [zlib](re/zlib.md) · [winmain](re/winmain.md) · [resources](re/resources.md) · [movie](re/movie.md) · [imeui](re/imeui.md) · [luaprofile](re/luaprofile.md) |
| Рендеринг | [render](re/render.md) (конвейер, RendComp, ximage) · [shading](re/shading.md) (текстуры, материалы, FLAppearance) · [alchemy](re/alchemy.md) (частицы / FX) · [deformable2](re/deformable2.md) · [rp9](re/rp9.md) (рендер на Direct3D 9 и объёмные туманности, только порт; `FreelancerRp9`) |
| Звук | [audio](re/audio.md) |
| Скрипты | [thorn](re/thorn.md) (сценарные скрипты, Lua 3.2) · [thorn_thn_format](re/thorn_thn_format.md) (формат файлов `.thn`) |
| Игровые данные и логика | [common](re/common.md) · [common_archetype](re/common_archetype.md) · [common_physys](re/common_physys.md) · [content](re/content.md) · [формат миссионных скриптов](re/content_mission_script.md) · [опкоды миссий](re/content_mission_opcodes.md) |
| Исполняемый файл клиента | [freelancer](re/freelancer.md) · [freelancer_cliobj](re/freelancer_cliobj.md) · [equip_fx_factory](re/equip_fx_factory.md) · [dealer_equip_stats](re/dealer_equip_stats.md) · [options_screen](re/options_screen.md) · [hd_edition](hd_edition.md) (широкоформатный экран) · [walk_mode](walk_mode.md) (ходьба по базам, только порт; `FreelancerWalkMode`) |
| Сервер / сеть | [server](re/server.md) (Server.dll, FLServer, SP-подключение, удалённые DLL) · [rpclocal](re/rpclocal.md) (протокол локальных RPC-пакетов) |
| Сборка и CRT | [building](../building.md) (CMake, пресеты, переключатели, Linux и macOS; на английском) · [crt_compat](re/crt_compat.md) (современные CRT/STL против среды выполнения retail на VC6) · [refactoring](re/refactoring.md) (правила кода) |

## Локальная раскладка

```
Freelancer\
  src\        all source code, a CMakeLists.txt per module
  bin\        Release output + the dev *.ini files (source of truth for the INI files)
  bin_debug\  Debug output       binasan\  DebugASan output
  EXE\        gundll.dll (in git, loaded); your retail binaries and Ghidra listings (not in git;
              reference only, never loaded)
  DATA\       retail game data (not in git)
  DATA_EXTRA\ the port's own data next to DATA (RP9 shaders, volumetric.ini; in git)
  docs\       this documentation
```

`DATA\` и справочные бинарники в `EXE\` берутся из вашей собственной установки Freelancer 1.1.
`bin\DACOM.INI` направляет каждую запись `[Libraries]` в `bin\`, поэтому ни одна retail DLL
движка не загружается; во время работы грузятся только retail DLL `EBUEula.dll` и
`EXE\gundll.dll`.

## Сборка

[building.md](../building.md) (на английском): пресеты CMake для Win32 (`release`, `debug`,
`debugasan`) и x64 (`release-x64`, `debug-x64`), переключатели сборки, vcpkg и переносимые модули,
которые собираются и на Linux и macOS. Из x86 Developer Command Prompt:

```
cmake --preset debug && cmake --build --preset debug
```

| Пресет | Выходной каталог |
|--------|------------------|
| `debug` | `bin_debug\` |
| `release` | `bin\` |
| `debugasan` | `binasan\` (AddressSanitizer; копирует среду выполнения `clang_rt.asan_*` рядом с exe) |
| `release-x64` / `debug-x64` | `bin_x64\` / `bin_x64_debug\` |

Запускайте `freelancer.exe` из каталога той конфигурации, которую вы собрали. Каждая сборка не в `bin\`
копирует dev-файлы `*.ini` из `bin\` с переписанными путями (`src/copy_dev_ini.py`) — правьте
файлы в `bin\`, а не копии. Retail DLL в `EXE\` не инструментированы ASan.

Поиск утечек памяти: [memory_diagnostics.md](memory_diagnostics.md).
