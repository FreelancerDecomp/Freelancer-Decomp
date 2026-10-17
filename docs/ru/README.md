# Freelancer Decomp — документация

[English](../README.md) · **Русский**

Freelancer Decomp — это переписанный с нуля по исходному коду клиент игры **Freelancer**
(Digital Anvil / Microsoft, 2003). Каждый модуль оригинального клиента заново собирается из
исходников на C++, которые сверяются с листингом retail-дизассемблера. Цель — рабочий клиент,
поведение которого совпадает с оригиналом, с задокументированными внутренностями, которые можно
изменять. См. [MISSION.md](MISSION.md).

Проект **не содержит игровых ресурсов и retail-бинарников**. Чтобы собрать и запустить его,
нужна собственная установка Freelancer (см. [Локальная раскладка](#локальная-раскладка)).

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
| Сборка и CRT | [crt_compat](re/crt_compat.md) (современные CRT/STL против среды выполнения retail на VC6) · [refactoring](re/refactoring.md) (правила кода) |

## Локальная раскладка

```
Freelancer\
  src\        Visual Studio solution (Freelancer.sln) — all project source code
  bin\        Release output + the dev *.ini files (source of truth for the INI files)
  bin_debug\  Debug output       binasan\  DebugASan output
  EXE\        retail binaries and Ghidra listings (not in git; reference only, never loaded)
  DATA\       retail game data (not in git)
  DATA_EXTRA\ the port's own data next to DATA (RP9 shaders, volumetric.ini; in git)
  docs\       this documentation
```

`EXE\` и `DATA\` берутся из вашей собственной установки Freelancer 1.1. Ни одна retail DLL не
загружается во время работы: `bin\DACOM.INI` направляет каждую запись `[Libraries]` в `bin\`.

## Сборка

Требуются Visual Studio 2022 (MSVC v143, цель Win32) и заголовки DirectX 8 SDK (`dx8\`).

```
MSBuild.exe src\Freelancer.sln -t:Build -p:Configuration=Debug -v:minimal
```

| Конфигурация | Выходной каталог |
|---------------|------------------|
| Debug | `bin_debug\` |
| Release | `bin\` |
| DebugASan | `binasan\` (AddressSanitizer; копирует среду выполнения `clang_rt.asan_*` рядом с exe) |

Запускайте `freelancer.exe` из каталога той конфигурации, которую вы собрали. Сборки Debug и ASan
копируют dev-файлы `*.ini` из `bin\` с переписанными путями (`src/copy_dev_ini.py`) — правьте
файлы в `bin\`, а не копии. Retail DLL в `EXE\` не инструментированы ASan.

Поиск утечек памяти: [memory_diagnostics.md](memory_diagnostics.md).
