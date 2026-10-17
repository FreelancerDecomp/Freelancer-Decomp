# Ресурсы PE (string / HTML / VERSIONINFO)

Клиентские тексты (IDS, infocard HTML, версии) хранятся в ресурсных DLL без кода, которые мы пересобираем из дампов retail. Документ объясняет, откуда берутся исходники, как соблюдается кодировка и что в solution является ресурсным проектом. Реализация — `src/Resources/` (`import_decompiled_rc.py`, `ResourceDll.props`).

Клиентские IDS живут в отдельных DLL без кода: `LoadLibrary("resources.dll")` из WinMain, затем `[Resources]` в `freelancer.ini` (`LoadResourcesFromIni`). Lookup: `FindResourceExA(RT_STRING / RT_HTML)`.

## Откуда файлы

Ghidra dump: `EXE/Disasm/DecompiledResources/<module>/<module>.rc` (+ sidecar HTML/ICO/BMP). Корневые `.rc` в той папке **игнорировать** (дамп перезаписывал одноимённые файлы).

Импорт в `src/`: `src/Resources/import_decompiled_rc.py` — конвертирует Resource Hacker VERSIONINFO в `rc.exe`, копирует sidecar.

## Кодировка

Импортёр читает экспортированный `.rc` с учётом BOM и записывает наш скрипт в
UTF-8. `ResourceDll.props` задаёт `rc.exe /c65001` для всех ресурсных DLL и всех
конфигураций. Генерируемые скрипты также содержат `#pragma code_page(65001)`,
поскольку кодные проекты не подключают эту property sheet.

Не-ASCII escape-последовательности Resource Hacker обозначают значения символов:
например, `\xFC` в названии Saarbrücken — U+00FC. Импортёр записывает такие
символы непосредственно в UTF-8, а не заменяет их однобайтовыми octal escapes:
`rc.exe` декодирует последние через кодовую страницу, что при UTF-8 даёт U+FFFD.

В собранном PE `RT_STRING` хранит UTF-16 с префиксом длины. Клиент читает эти
слова непосредственно; преобразования UTF-8 в этом пути нет. `RT_HTML` берётся
из sidecar-файлов UTF-16LE с BOM; компилятор включает их как бинарные данные,
без изменения кодировки.

Экспортированные данные также содержат отличия формулировок, XML-разметки и языковых
идентификаторов от retail-PE: совпадение с исходниками проверяет сохранность кодировки,
а не полную эквивалентность этих данных retail.

## Solution

Solution плоский (папки в VS создаются вручную; не называть папку `Resources` — VS путает её с проектом `resources`).

- **Ресурсные DLL** (`src/Resources/`, `/NOENTRY`) → `bin\`: `resources`, `InfoCards`, `MiscText`, `NameResources`, `EquipResources`, `OfferBribeResources`, `MiscTextInfo2`, `serverresources`.
- **Код:** DLL + `Common.lib` + exe. У кодных модулей, где был dump, свой `.rc` (VERSIONINFO; у Common ещё debug MENU/DIALOGEX, VERSIONINFO снят чтобы не дублировать exe).
- **freelancer** — клиентский exe; `resources/freelancer.rc` (ICON 103 + VERSIONINFO).

Нет dump у `Content.dll` — VERSIONINFO не добавлялся.

`serverresources`: бинарный `RT_TOOLBAR` (type 241 + `.bin`) rc.exe не ест как скрипт TOOLBAR — ресурс пропущен, остальное в DLL.

Набор импортируемых модулей задан `CODE_MAP`, `RESOURCE_ONLY` и `SKIP_DUMP` в
импортёре; наличие проекта в solution само по себе не означает импорт ресурсов.

## Граница ответственности

Ресурсные DLL предоставляют STRINGTABLE/HTML/VERSIONINFO. Загрузка идёт из
`bin\`; retail PE в `EXE\` служат только RE-эталоном, fallback на них запрещён.
Интерпретация RDL, построение NeuroNet-контролов и отображение текста принадлежат
клиенту и Common; наличие текстов в ресурсной DLL не доказывает корректность этих
потребителей.
