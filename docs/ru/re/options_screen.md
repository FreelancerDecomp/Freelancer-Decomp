# Экран настроек: файлы и хранение

Окно настроек в retail — `NN_Preferences : WinControl` (одна vtable `0x005d4a5c`; порт —
`src/freelancer/Client/nn_preferences.{h,cpp}`). Документ описывает только данные, которые окно читает
и пишет, и границы с соседними таблицами. Устройство самого окна, слоты и статус функций — в
комментариях `nn_preferences.h` и в decomp-tracker.

## Файлы

| Файл | Содержимое | Код порта |
|---|---|---|
| `<user>\PerfOptions.ini` | вещественные параметры производительности/звука, опционально секция `[Display]` | `LoadPerfOptions` / `SavePerfOptions` (`perf_options.cpp`); окно сохраняет через `SavePerfOptions("PerfOptions.ini")` |
| `DATA\INTERFACE\optlist.ini` | список строк трёх страниц опций (по IDS); при первом запуске засевает значения по умолчанию | `NnPreferences` (`kOptListPath`), засев — `NnPreferences::SeedGraphicsDefaults` |
| `DATA\INTERFACE\keylist.ini` | группы и список клавиш страницы Controls (`USER_*`) | `NnPreferences` (`kKeyListPath`) |
| `DATA\INTERFACE\KeyMap.ini` | базовые `KeyCmd` и группы | `LoadKeyMapIni` (`menu_keymap.cpp`) |
| `<user>\UserKeyMap.ini` | пользовательские переопределения | `LoadUserKeyMapIni`, `SaveUserKeyMapIni`, путь — `EnsureUserKeyMapPath` |

Живая правка привязки на странице Controls — `UpdateKeyCmdBinding` над таблицей `KeyCmd`,
которой владеет `menu_keymap.cpp`.

## Что не относится к экрану настроек

- Графический пресет (`default_graphics`) при первом запуске выбирается пробой железа
  `ProbeMachineGraphicsLevel` (`perf_options.cpp`; оценка — минимум из CPU и GPU по
  `QueryDeviceAbility` и `Timing::GetMachineSpeed`). Расхождения с retail по `PerfOptions` — в
  `deviations.md`.
- `game_options` (`GetGameOption`, retail `DAT_00679be8`) — отдельная таблица отладочных
  переключателей. Меню, которое её правило, вырезано из поставляемой сборки, поэтому retail после
  инициализации в неё не пишет, а окно настроек её не трогает. Подробности — в `game_options.h` и
  [freelancer.md](freelancer.md).
