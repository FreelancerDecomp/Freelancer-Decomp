# DACOM (dacom.dll)

`dacom.dll` — корень всей архитектуры: менеджер компонентов `COManager` (`ICOManager`), реестр фабрик, загрузчик библиотек из `DACOM.INI`, встроенный INI-парсер (`IProfileParser`) и аварийный вывод `FDUMP`. Документ нужен тому, кто трогает загрузку модулей или разбирает INI. Порт — `src/dacom/dacom.cpp`, публичный ABI — `src/include/dacom.h`; адреса и слоты отдельных функций — в decomp-tracker (листинг: `EXE/Disasm/dacom.dll.txt`).

## Соглашения

- Экспорты `DACOM_Acquire` / `DACOM_GetDllVersion` / `DACOM_GetVersion` — `__cdecl`; методы интерфейсов — `__stdcall`, `this` на стеке. Всего 15 экспортов (в т.ч. `DACOM_CRC::*`, `LogStream::*`, данные `FDUMP`, `stricmp`); `src/dacom/dacom.def` перечисляет все.
- IID — строки `"1.11_ICOManager"`, `"1.11_IProfileParser"` и т.д.; в коде — `Iids::` из `src/include/dacom_iids.h`.
- `AddRef`/`Release` у `COManager` всегда возвращают 1 (синглтон, живёт, пока жив процесс).

## Кто отвечает на `IProfileParser`

**Встроенный парсер dacom, а не `luaprofile.dll`.** `COManager::COManager` (`065b1f80`) регистрирует фабрики `"ProfileParser"` и `"ProfileParser2"` с приоритетом `0x80000000` и создаёт `ProfileParser2` агрегированным объектом (outer = `COManager`): `COManager+0x28` — его `IProfileParser*`, `+0x2c` — внутренний `IUnknown`. `COManager::QueryInterface` (`065b2090`) отвечает на `"1.11_ICOManager"` сам, всё остальное отдаёт внутреннему `IUnknown`, который знает `"1.11_IProfileParser"` и `"1.11_IProfileParser2"`. Потребители (EngBase, system, alchemy, rendcomp, rp8, shading, звук, deformable2) берут парсер через `DACOM_Acquire()->QueryInterface`, а не `CreateInstance`. Фабрики парсера (`065b4750`) принимают только расширенный дескриптор `{0x14, имя, outer, …}`; обычный 8-байтный AGGDESC даёт `-3`.

`luaprofile.dll` регистрирует только `"ILua"`; ни один retail-модуль такой компонент не создаёт, и в retail она не грузится (не в `[Libraries]`). В нашем `bin/DACOM.INI` её тоже нет.

## Загрузка библиотек

- `DllPath` (из `[DACOM]` INI): абсолютное значение (`\`, `/`, `X:`) берётся как есть; относительное строится от каталога exe, ведущие `..\` поднимаются вверх. `DllPath = .\` даёт каталог exe. Если `IgnoreDACOMEnv` не `true`, переменная окружения `DACOM` подменяет `DllPath`.
- `AddLibrary`: единственный поиск — `SearchPathA(DllPath, name, …)`. Нет запасных путей (`..\EXE` и т.п.): не найденное имя даёт ошибку через `FDUMP` и `-1`; ошибка `LoadLibrary` — `-2`. Успех пишет `{HMODULE, имя}` в список библиотек; пока DLL грузится, её имя (без расширения) лежит в `COManager+0x30` и помечает компоненты, которые она зарегистрирует из `DllMain`.
- Поэтому в нашей сборке имя, не найденное в `bin\`, должно падать громко, а не подхватываться из `EXE\`.

## Реестр компонентов

- `RegisterComponent(factory, name, priority)`: ссылка вставляется **перед первой ссылкой с priority <= новой** (беззнаково) — среди равных побеждает последняя зарегистрированная. Реестр — `map<CRC32(имя), list<ссылка>>` в порядке возрастания CRC (в порте — `std::map`, тот же порядок обхода); CRC сворачивает регистр по таблице retail (индекс знаковый, байты >= 0x80 читают хвост таблицы).
- `CreateInstance(desc, out)`: `*out = 0`; `desc == NULL`, нет имени или имя `"COManager"` → сам менеджер; иначе корзина по CRC имени, нет корзины → `-3`, фабрики пробуются по порядку, первый успех побеждает, иначе код последней.
- `SetINIConfig(text, flags)`: `flags & 2` — `text` это INI в памяти (`IProfileParser2::LoadFromMemory`), иначе путь к файлу; после загрузки выполняется `LoadConfiguration` (`[DACOM]` и `[Libraries]`), ошибки загрузки отдельных библиотек на результат не влияют.

## `FDUMP`

`DllMain` ставит обработчик по умолчанию: `vsprintf` в буфер 0x1000, `OutputDebugStringA`; уровни 0, 1 и 2 (пока не нажато Ignore) показывают `MessageBoxA` «DACOM::STANDARD_DUMP» (Abort/Retry/Ignore): Abort → `PostQuitMessage(-1)` + `abort()`, Retry → `INT3`, Ignore на уровне 2 → больше предупреждений не показывать. Common.dll (`ErrorHandler::activate`) затем подменяет `FDUMP` своим. Известное расхождение: наш обработчик по умолчанию возвращает `void`, retail — `int` 0.

## Компонент `RegExp`

Регистрируется из `DllMain` (поэтому `COManager` конструируется уже при attach dacom.dll): `CreateInstance` требует 8-байтный AGGDESC `{8, "RegExp"}`; интерфейсы `IRegExp` и `IDAComponent`. Внутри — regexp Генри Спенсера с расширениями (`\<`, `\>`, экранирование `\` в литералах и классах, `\d \n \r \s \t`). Компилятор и matcher делят глобальные курсоры, поэтому механизм не реентерабелен; строки выражения и исходная заимствованы, программа и буфер захвата принадлежат объекту. Известное расхождение порта: `isalnum` в matcher применяется к беззнаковому байту.

## Встроенный INI-парсер (формат)

Объект парсера содержит гранью `IProfileParser` (+0), `IProfileParser2` (+4, виден только внутри dacom), базу секций и счётчик внутреннего `IUnknown`; QI/AddRef/Release обеих граней уходят во outer.

- Слоты `IProfileParser`: загрузка файла (`flags & 1` → `-1`), обход секций `cb(this, имя, ctx)` (0 — стоп), `OpenSection` (`flags != 3` → NULL), `CloseSection` → 1, `ReadLine`/`GetString` без буфера возвращают требуемую длину. `IProfileParser2` добавляет `LoadFromMemory`, `EnumerateSections` и `EnumerateKeys` (visitor с методом `Visit`).
- Лексер: разделители `\t \r ' '`; `;` — комментарий до конца строки; токены `\n`, `[`, `]`, `=`; слово заканчивается на `" \r\n\t[];="`; слово `@include` (без учёта регистра) — отдельный токен. Строка `ключ = значение`: ключ — слова до `=`, значение — слова до конца строки (повторный `=` входит в значение). `@include файл` — путь относительно каталога текущего файла, если в имени нет диска и оно не начинается с `\`.
- Секции хранятся отсортированными по `_stricmp`, дубликаты сохраняются (новая — после равных), поиск возвращает первую.
- Ключ вне секции разыменует NULL (в retail — падение), неожиданный токен — `INT3`: порт повторяет это поведение.
