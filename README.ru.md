# Freelancer Decomp

Восстановленная реверс-инжинирингом реализация игрового клиента **Freelancer** (2003) и его
движковых DLL, собираемая из исходников современной Visual Studio. Каждый модуль, который
загружает игра, собирается из `src/`; retail-бинарники игры не загружаются, кроме двух сторонних
DLL, перечисленных ниже.

Нужна собственная копия Freelancer: игровых данных (`DATA`) в репозитории нет.

English: [README.md](README.md). Документация: [docs/ru/README.md](docs/ru/README.md).

## Сборка и запуск

1. **Склонировать** репозиторий.
2. **Добавить данные игры:** скопировать папку `DATA` установленного Freelancer (или мода) в
   корень репозитория, рядом с `src`, так чтобы `DATA\` и `DATA_EXTRA\` лежали рядом.
3. **Добавить DirectX 8.1 SDK:** скачать
   [DirectX 8.1 SDK (Final)](https://archive.org/details/dx81sdk_full), установить или распаковать
   его и скопировать папки `include` и `lib` в папку `dx8` в корне репозитория (должны существовать
   `dx8\include\d3d8.h` и `dx8\lib\d3d8.lib`). Используются только заголовки и библиотеки; другое
   место можно указать через `/p:Dx8SdkDir=<путь>`.
4. **Собрать конфигурацию Release** решения `src\Freelancer.sln` (`Release|Win32`) в Visual Studio
   или из Developer Command Prompt:

   ```
   msbuild src\Freelancer.sln -t:Build -p:Configuration=Release -p:Platform=Win32
   ```

Результат попадает в `bin\`, где уже лежат файлы конфигурации (`DACOM.INI`, `freelancer.ini`,
`FLConfigDatabase.txt`, `NewPlayer.fl`, `MPNewCharacter.fl`). Запускать `bin\freelancer.exe`
**из папки `bin`** (пути к библиотекам и данным отсчитываются от неё).

### Требования

- Visual Studio 2022 (platform toolset v143) с нагрузкой *Разработка классических приложений на
  C++*, компонентом *C++ MFC для последних средств сборки v143 (x86 и x64)* (нужен `FLServer`) и
  Windows 10/11 SDK. Всё собирается 32-битным (`Win32`).
- Для Debug-сборок (`bin_debug\`) нужен ещё Python 3 в `PATH`: шаг после сборки копирует
  конфигурацию из `bin\`.

### Необязательные возможности

Возможности, которых нет в оригинале, включаются свойствами сборки в
`src/Freelancer.Diagnostics.props` (они же — на странице свойств VS *Freelancer diagnostics*) или
`/p:<Имя>=0|1` в командной строке MSBuild:

| Свойство | По умолчанию | Что делает |
|---|---|---|
| `FreelancerWidescreenSupport` | 1 | Широкоформатные режимы экрана (Hor+) и раскладка HUD |
| `FreelancerWalkMode` | 1 | Ходьба Трентом по помещениям баз ([docs/ru/walk_mode.md](docs/ru/walk_mode.md)) |
| `FreelancerRp9` | 0 | `RP9.dll` — рендер на Direct3D 9 с объёмными туманностями ([docs/ru/re/rp9.md](docs/ru/re/rp9.md)); нужен [DirectX SDK (June 2010)](https://www.microsoft.com/en-us/download/details.aspx?id=6812) и `DirectX9` в `bin\DACOM.INI` |
| `FreelancerBetaLabel` | 0 | Надпись «Beta» в углу экрана |
| `FreelancerEnableTickProfile` | 0 | Экранный профайлер кадра |

## Структура репозитория

```
src\          решение Visual Studio (Freelancer.sln) — весь исходный код
bin\          результат Release-сборки + файлы конфигурации
DATA_EXTRA\   собственные данные порта (шейдеры RP9, volumetric.ini) рядом с DATA игры
EBUEula.dll   retail-диалог лицензии, грузится как ..\EBUEula.dll вместе с ..\EULA.rtf (как в оригинале)
EXE\          gundll.dll — retail-клиент GUN (список серверов мультиплеера), грузится как ..\exe\gundll.dll
docs\         документация (английская; русский перевод — в docs\ru)
```

`EBUEula.dll` и `gundll.dll` — единственные retail-бинарники, которые порт использует без
изменений: это сторонние компоненты оригинальной игры, они не переписывались.

## Лицензия

Исходный код Freelancer Decomp передан в общественное достояние по лицензии
[The Unlicense](LICENSE). Это не распространяется на сторонний код и файлы в репозитории:
вендорные библиотеки в `src\` (`src\zlib\vendor`, `src\qhull\vendor`) остаются под своими
лицензиями, а retail-файлы (`EBUEula.dll`, `EULA.RTF`, `EXE\gundll.dll`) и данные игры
принадлежат их правообладателям.

## Поддержать проект

Если хотите поддержать работу над Freelancer Decomp, буду рад донату:

| Валюта (сеть) | Адрес |
|---|---|
| USDT (BEP20) | `0x3bedb75137d850b8f24a42f488459a6a94489105` |
| USDT (TRC20) | `TZA2s2A7m8rCuXgDsMJs7XhyoZmX7BBc6j` |
| Bitcoin (BTC) | `1JooBx2a86hNhqdU7uYiKAfxaHVHBqD1wV` |
| ETH (ERC20) | `0x3bedb75137d850b8f24a42f488459a6a94489105` |

Отправляйте только указанную в строке валюту и только через указанную рядом сеть.

## Контакты

- Discord: `@Giperion`
- Email: [gipdereline@gmail.com](mailto:gipdereline@gmail.com)
