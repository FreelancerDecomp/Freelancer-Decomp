# DACOM.INI / dacomsrv.ini: конфигурация движка

Этот документ — полный справочник по конфигурационному файлу движка, который менеджер компонентов DACOM загружает при старте (`DACOM.INI` для клиента, `DACOMsrv.ini` для сервера): грамматика файла, способ его поиска, какие DLL он загружает, а также каждая `[Section]` и каждый ключ, которые модули движка читают через встроенный `IProfileParser`. Здесь же сравниваются dev-файлы в `bin\` с тем, что код реально читает.

**Расположение файла / как он ищется.** Клиент вызывает `DALib::Startup(window, nullptr)`, который использует `DACOM.INI` в текущем рабочем каталоге, если он читается, и иначе `DACOM.INI` в каталоге `DALib.dll`. Сервер (`FLServer.exe`) вызывает `DALib::Startup(NULL, "DACOMsrv.ini")` — путь относительно рабочего каталога. Явный путь передаётся прямо в `SetINIConfig`. Если файл не удаётся загрузить, `DALib::Startup` завершается ошибкой. В этом репозитории dev-копии лежат в `bin\` (`DACOM.INI`, `dacomsrv.ini`); копии в `bin_debug\` и `binasan\` генерируются из них скриптом `src/copy_dev_ini.py`, который заменяет абсолютный префикс `D:\Freelancer\bin\` на выходной каталог, поэтому править нужно только `bin\*.ini`. Конфигурация игровых данных (`freelancer.ini`, `[Data]`) — это другой файл и другой парсер, см. [freelancer-ini.md](freelancer-ini.md) и [parsers.md](parsers.md).

Исходные файлы: [dacom.cpp](../../../src/dacom/dacom.cpp), [DALib.cpp](../../../src/DALib/DALib.cpp) и потребители, перечисленные в каждой секции ниже. «Не проверяется» означает, что код не применяет к значению никакой проверки диапазона или перечисления.

---

## 1. Грамматика файла (`IProfileParser`, реализован в `dacom.dll`)

Парсер собственный для движка: `FIniLexer` ([dacom.cpp](../../../src/dacom/dacom.cpp#L331)) разбивает текст на токены, `FIniDatabase::Parse` ([dacom.cpp](../../../src/dacom/dacom.cpp#L653)) строит базу секций, а `FProfileParserObject` ([dacom.cpp](../../../src/dacom/dacom.cpp#L792)) её предоставляет. Весь файл сначала читается в память (`fopen` с `"rb"`). Это **не** `INI_Reader`, используемый для игровых данных, и **не** Win32 API `GetPrivateProfile*` (проект его никогда не вызывает).

**Токены.**

* Пробел, табуляция и возврат каретки разделяют токены и в остальном игнорируются; перевод строки завершает строку.
* `;` начинает комментарий до конца строки (в том числе после значения).
* `[`, `]`, `=` — односимвольные токены. Любая другая последовательность символов до одного из ` \r\n\t[];=` — «слово». Кавычек нет.
* Слово `@include` (без учёта регистра) — директива включения.

**Операторы.**

* `[Name]` начинает секцию. Всё между `[` и `]` читается как слова, объединённые одиночными пробелами (так что `[ Libraries ]` — это `Libraries`). Имена секций сравниваются без учёта регистра. Строка заголовка, у которой первый токен после `[` — комментарий (`[;Name]`), превращает остаток строки в комментарий: **новая секция не начинается, и следующие ключи остаются в предыдущей секции.**
* `key = value`: первое слово — ключ, любые дальнейшие слова до `=` присоединяются к нему через одиночные пробелы (ключ — вся левая фраза), а значение — правая фраза до конца строки. В обеих фразах подряд идущие слова объединяются ровно **одним пробелом**, так что последовательности пробелов и табуляций схлопываются; второй `=` в значении сохраняется как текст `=`. Фразы ограничены примерно 1 КБ.
* Строка без `=` — голый ключ без значения (форма, используемая для имён DLL в `[Libraries]` и имён компонентов в `[System]`, `[Engine]`, `[RenderManager]`).
* `@include <file>`: разбирает другой файл в этом месте. Путь без диска и без ведущего `\` берётся относительно каталога **включающего файла** (как он записан в командной строке, то есть относительно рабочего каталога, если включающий файл задан без каталога). Отсутствующий include молча игнорируется (результат вложенного разбора отбрасывается).
* Ключ до первого заголовка секции разыменовывает нулевую секцию и падает (поведение retail). Одиночная `]` или неожиданный токен вызывает debug break.

**Правила поиска.**

* Секции хранятся отсортированными по имени; дублирующиеся секции **сохраняются**, новая встаёт позади равных. `OpenSection` возвращает первое совпадение, то есть самое раннее в порядке разбора, поэтому второй блок `[Name]` в том же файле потребители никогда не видят (секция из `@include`, разобранная раньше, побеждает более позднюю во включающем файле).
* `ReadLine(section, index, buffer, size)` возвращает n-ю строку секции (с нуля, в порядке файла, включая дубликаты) как `key = value` или как голый ключ. `GetString(section, key, buffer, size)` возвращает значение **первой** строки, ключ которой совпадает (без учёта регистра), а в качестве результата — его длину; результат равен 0, когда ключа нет **и когда он присутствует с пустым значением** (`Key =`), так что потребители не могут их различить. Голый ключ (без `=`), найденный `GetString`, также даёт пустую строку с результатом 0.
* `OpenSection` отвечает только когда аргумент flags равен 3 (все потребители передают 3).
* Второй, приватный интерфейс `IProfileParser2` (загрузка из памяти, перечисление секций и ключей) существует внутри `dacom.dll`; он используется только `SetINIConfig` с флагом 2 (тогда аргумент — сам текст INI). `luaprofile.dll` из репозитория — альтернативная реализация `IProfileParser` (раздел 14).

### 1.1 Парсер «числового значения», общий для модулей движка

Большинство модулей копируют одну и ту же процедуру превращения значения в число (здесь она называется **retail-целое**). Она применяется к тексту, возвращённому `GetString`:

1. Взять самый длинный префикс, состоящий только из символов `0123456789truefalsynoxTRUEFALSYNOX+-`, и обрезать текст на этом месте (если префикс пуст, ничего не обрезается).
2. Если результат равен (без учёта регистра) `true` или `yes`, значение 1; `false` или `no` даёт 0.
3. Иначе, если текст содержит `x` или `X`, он разбирается через `strtoul(text, 0, 16)` (так что `0x10` читается как шестнадцатеричное 16).
4. Иначе он разбирается MSVCRT `atoi` (которая при переполнении заворачивается по модулю 2^32 и возвращает 0 для текста вроде `Light`).

Синтаксиса с плавающей точкой нет: значение вроде `1.5` читается как 1. Там, где состояние D3D имеет тип float, значение нужно задавать как «сырой» битовый шаблон IEEE-754 в шестнадцатеричном виде (встроенные значения по умолчанию, например `0x3F800000` для 1.0, записаны именно так). Ключи реверберации звука используют отдельный парсер float (раздел 8.3). Реализации: [RP8.cpp](../../../src/RP8/RP8.cpp#L319), [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L211), [TextureLibrary.cpp](../../../src/Shading/TextureLibrary.cpp#L38), [MaterialBatcher.cpp](../../../src/Shading/MaterialBatcher.cpp#L341), [Plugins.cpp](../../../src/RendComp/Plugins.cpp#L210), [alchemy_common.cpp](../../../src/alchemy/alchemy_common.cpp#L1707), [Deformable2.cpp](../../../src/Deformable2/Deformable2.cpp#L4658).

---

## 2. Последовательность старта и управляющие ею секции

1. `DALib::Startup` загружает файл (см. выше) через `SetINIConfig(path, 0)`.
2. `FCOManager::LoadConfiguration` ([dacom.cpp](../../../src/dacom/dacom.cpp#L1463)) читает `[DACOM]`, а затем загружает DLL из `[Libraries]`. Выполняется один раз на процесс.
3. `DALib::Startup` создаёт `SystemContainer`, который читает `[System]` и создаёт перечисленные компоненты (раздел 5).
4. Движок (`EngBase`) читает `[Engine]`; менеджер рендера (`RendComp`) читает `[RenderManager]` (разделы 6, 7). Каждый компонент читает собственную секцию при инициализации (разделы 8–13).

Сбои в компоненте логируются и пропускаются, а не прерывают работу, если не указано иное.

---

## 3. `[DACOM]`

Читается `FCOManager::LoadConfiguration` ([dacom.cpp](../../../src/dacom/dacom.cpp#L1463)).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `DllPath` | путь | нет (пусто: `SearchPath` использует порядок по умолчанию: каталог exe, системные каталоги, `PATH`) | Каталог, в котором ищутся DLL из `[Libraries]`. Абсолютное значение (первый символ `\` или `/`, либо второй символ `:`) используется как есть. Любое другое значение отсчитывается от **каталога исполняемого файла**: ведущие сегменты `.\` (или `./`) пропускаются, а каждый ведущий `..\` (или `../`) поднимается на один каталог, затем присоединяется остаток (`.\` даёт сам каталог exe). Относительное значение, начинающееся с `.`, но не являющееся одной из этих форм (например `.x` или одиночная `.`), никогда не продвигается и **зависает** (поведение retail). | [dacom.cpp](../../../src/dacom/dacom.cpp#L1479) |
| `IgnoreDACOMEnv` | bool | false | Определяет, может ли переменная окружения `DACOM` заменить `DllPath`. Значение, первые четыре символа которого `true` (без учёта регистра, так что `trueish` тоже считается), заставляет игнорировать переменную. Значение, которое не начинается с `false` и у которого `atoi` ненулевой (например `1`), также заставляет её игнорировать. Всё остальное, включая `yes` и пустое значение, оставляет переменную действующей. | [dacom.cpp](../../../src/dacom/dacom.cpp#L1545) |

**Переменная окружения `DACOM`.** Если она не игнорируется, как описано выше, и окружение процесса содержит `DACOM`, её текст **полностью заменяет** `DllPath` (используется дословно как каталог поиска, что бы ни говорил INI). Она читается через `getenv("DACOM")` ([dacom.cpp](../../../src/dacom/dacom.cpp#L1556)); других переменных окружения нет.

---

## 4. `[Libraries]`

Загружается `FCOManager::LoadLibraries` ([dacom.cpp](../../../src/dacom/dacom.cpp#L1577)) и `AddLibrary` ([dacom.cpp](../../../src/dacom/dacom.cpp#L1292)).

| Запись | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| одна DLL на строку | голое слово | нет | Повторяемая и упорядоченная: DLL загружаются в порядке файла, и этот порядок наблюдаем (фабрики модуля регистрируются из его `DllMain`; последующие секции создают из них компоненты по имени). Первое слово строки — имя; всё после первого пробела, табуляции или `;` отбрасывается. Имя должно содержать расширение (`ReadFile.dll`), так как расширение по умолчанию не добавляется, и не может содержать пробелов. Оно разрешается через `SearchPathA(DllPath, name)`: голое имя ищется в `DllPath`; имя, заданное абсолютным путём (например записи `D:\Freelancer\bin\...` в dev-файле), используется как есть — так dev-файл не даёт `DllPath` подхватить retail DLL. Имя, которое не найдено, логирует «unable to find dll» и пропускается; DLL, которая не загрузилась, логирует «unable to load dll» и пропускается. Менеджер компонентов считается инициализированным, если загрузилась хотя бы одна DLL. Строки читаются в буфер на 260 байт. | [dacom.cpp](../../../src/dacom/dacom.cpp#L1580) |

Если секция отсутствует, менеджер логирует «[Libraries] group not found» и остаётся неинициализированным (`SetINIConfig` всё равно сообщает об успехе, так что `DALib::Startup` продолжает без единой библиотеки). **Без отката**: имя, которое не удаётся разрешить через `DllPath`, никогда не ищется где-либо ещё (в частности, не в `EXE\`).

---

## 5. `[System]`

Читается `FSystemContainer::LoadSystemComponents` ([system.cpp](../../../src/system/system.cpp#L332)).

| Запись | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `Name` или `Name = extra` | имя компонента + необязательный текст | нет | Один системный компонент на строку, по порядку; отсутствие секции означает отсутствие компонентов. Каждое имя передаётся в `CreateInstance` как агрегатное описание вместе с необязательным текстом после `=`. Строка, которая пуста или первый символ которой `;`, пропускается; имя обрезается по первому пробелу или табуляции. Результат каждой строки логируется как уведомление (`OK` или `FAILED`); компонент, завершившийся ошибкой, отбрасывается, а следующая строка всё равно обрабатывается. После последней строки все компоненты инициализируются (`Initialize`), а компонент, инициализация которого не удалась, удаляется. Буфер строки — 256 символов. Имена — это имена компонентов, зарегистрированных загруженными библиотеками, и не проверяются. | [system.cpp](../../../src/system/system.cpp#L332) |

Текст `extra` потребляется компонентом. Описанный здесь потребитель — конвейер рендера `DirectX8` (RP8): `DirectX8 = MySection` заставляет RP8 читать настройки из `[MySection]`; без этого секция — `RenderPipeline` ([RP8.cpp](../../../src/RP8/RP8.cpp#L1137)).

Имена компонентов, используемые в поставляемых/dev-файлах: `DirectX8`, `TextureLibrary`, `MaterialLibrary`, `MaterialBatcher`, `SoundManager`, `SoundStreamer`, `Alchemy`, `VMeshLibrary`, `MaterialAnimation`, `FxRuntime`.

---

## 6. `[Engine]`

Читается `EngineLoadEngineComponents` ([EngBase.cpp](../../../src/EngBase/EngBase.cpp#L3296)); парсер строк — `ParseEngineProfileLine` ([EngBase.cpp](../../../src/EngBase/EngBase.cpp#L3262)).

| Запись | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `Name` или `Name = value` | имя компонента + необязательный токен | нет | Один подкомпонент движка на строку, по порядку (`Animation`, `Hardpoint`, `Deformable`, `RenderManager`, `FLAppearance`, `VMeshWire` в поставляемых файлах). Строка делится по пробелам и табуляциям: первый токен — имя; токен `=` с ещё одним токеном за ним задаёт значение, передаваемое компоненту (только один токен, без пробелов); токен, начинающийся с `;`, завершает строку. Каждый компонент создаётся по имени и агрегируется в движок; сбой создания логируется, а строка пропускается. Во втором проходе инициализируется каждый созданный компонент; тот, чья инициализация не удалась, снова удаляется (предупреждение). | [EngBase.cpp](../../../src/EngBase/EngBase.cpp#L3296) |

Если `[Engine]` отсутствует, движок молча не загружает подкомпонентов. Если парсер профиля получить не удаётся, загрузка завершается ошибкой.

---

## 7. `[RenderManager]` и `[TriMesh]`

### 7.1 `[RenderManager]`

Читается `FRenderManager::LoadPlugins` ([RenderManager.cpp](../../../src/RendComp/RenderManager.cpp#L313)).

| Запись | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `PluginName` | голое слово | нет | Один плагин рендера на строку, создаются в перечисленном порядке (`VMesh`, `TriMesh`, `SphereMesh`). Строки, начинающиеся с `;`, пропускаются; имя обрезается по первому пробелу, табуляции или `;`. Порядок значим: поставляемый файл отмечает, что `TriMesh` должен идти перед `SphereMesh`, поскольку «subsystems attempt to create objects in order listed». Плагин, который не удаётся создать, молча пропускается. | [RenderManager.cpp](../../../src/RendComp/RenderManager.cpp#L338) |

Отсутствующая секция логирует «[RenderManager] section not found!», и менеджер не инициализируется.

### 7.2 `[TriMesh]`

Читается `TriMeshPluginReadProfile` ([Plugins.cpp](../../../src/RendComp/Plugins.cpp#L210)).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `Tristrips` | bool / retail-целое | 1 | Ненулевое значение оставляет рендеринг треугольными полосами включённым, 0 отключает его. Значение разбирается как в 1.1. В коде ключ пишется `Tristrips`; dev-файлы пишут `tristrips` (сопоставление без учёта регистра). | [Plugins.cpp](../../../src/RendComp/Plugins.cpp#L235) |

---

## 8. `[RenderPipeline]` (RP8, конвейер Direct3D 8)

Имя секции — `RenderPipeline`, если только `[System]` не содержит `DirectX8 = OtherName` (раздел 5). Все ключи читаются один раз в `FDirectX8::Startup` ([RP8.cpp](../../../src/RP8/RP8.cpp#L2569)) парсером retail-целых (1.1), если не указано иное. Клиент запускает его через `Pipeline->Startup(nullptr)`.

### 8.1 Создание устройства и флаги

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `DeviceId` | текст GUID | `{00000000-0000-0000-0000-000000000000}` | Читается (до 127 символов), когда имя секции отличается от ранее запущенной; в порте значение **после этого не используется**. | [RP8.cpp](../../../src/RP8/RP8.cpp#L2585) |
| `DeviceType` | строка | `Hardware` | Читается как `DeviceId`; **после этого не используется**. | там же |
| `DeviceTypeFallback` | int | 0 | Читается как `DeviceId`; **после этого не используется**. | там же |
| `MGSDB` | путь | пусто | База данных особенностей оборудования (например `FLConfigDatabase.txt`); до 127 символов, используется как записано. Пустое значение пропускает базу. Файл использует собственный синтаксис RP8, а не INI. | [RP8.cpp](../../../src/RP8/RP8.cpp#L2596) |
| `FPU_PRESERVE` | bool | 0 | Ненулевое значение добавляет `D3DCREATE_FPU_PRESERVE` (0x2). | [RP8.cpp](../../../src/RP8/RP8.cpp#L2605) |
| `MULTITHREADED` | bool | 0 | Ненулевое значение добавляет `D3DCREATE_MULTITHREADED` (0x4). | там же |
| `PUREDEVICE` | bool | 0 | Ненулевое значение добавляет `D3DCREATE_PUREDEVICE` (0x10). | там же |
| `SOFTWARE_VERTEXPROCESSING` | bool | 0 | Проверяется первым: ненулевое значение выбирает программную обработку вершин (0x20). | [RP8.cpp](../../../src/RP8/RP8.cpp#L2613) |
| `MIXED_VERTEXPROCESSING` | bool | 0 | Проверяется, только если программный ключ равен 0: ненулевое значение выбирает смешанную (0x80). | там же |
| `HARDWARE_VERTEXPROCESSING` | bool | 0 | Читается, но **аппаратная обработка (0x40) выбирается всякий раз, когда ни один из двух ключей выше не ненулевой, что бы ни говорил этот ключ.** Ключи обработки вершин взаимно исключают друг друга в этом порядке приоритета. | там же |
| `LOCKABLE_BACKBUFFER` | bool | 0 | Разрешить блокировку заднего буфера. (В комментарии retail `DACOM.INI` ключ ошибочно написан `LOCAKABLE_BACKBUFFER`; эта опечатка никогда не читается.) | [RP8.cpp](../../../src/RP8/RP8.cpp#L2629) |
| `USE_SYSLOCK` | bool | 0 | Брать общесистемную блокировку Windows, пока используются ресурсы. | там же |
| `HANDLE_SWAPLOSS` | bool | **1** | Обрабатывать потерю устройства внутри конвейера. | там же |

### 8.2 Свойства конвейера (читаются для каждой записи, при отсутствии применяется значение по умолчанию)

| Ключ (id состояния конвейера) | По умолчанию | Ключ (id состояния конвейера) | По умолчанию |
|---|---|---|---|
| `TEXTURE` (0) | 0x1 | `CLEAR_COLOR` (8) | 0x0 |
| `LIGHTING` (1) | 0x1 | `CLEAR_ZDEPTH` (9) | 0x3F800000 (биты 1.0f) |
| `TEXTURE_LOD` (2) | 0x1 | `CLEAR_STENCIL` (10) | 0xFFFFFFFF |
| `TEXTURE_ALLOW_8BIT` (3) | 0x0 | `STATE_CACHE` (11) | 0x1 |
| `TEXTURE_ALLOW_32BIT` (4) | 0x1 | `BROKEN_MOD2X` (12) | 0x0 |
| `TEXTURE_ALLOW_DXT` (5) | 0x1 | `BROKEN_FLIP` (13) | 0x0 |
| `BROKEN_MULTITEXTURE` (6) | 0x1 | `VIEWSPACE_LIGHTS` (7) | 0x1 |

Источник: таблица [RP8.cpp](../../../src/RP8/RP8.cpp#L103), чтение в [RP8.cpp](../../../src/RP8/RP8.cpp#L2640). Каждое значение сохраняется в слот состояния конвейера с этим id. Когда `TEXTURE_ALLOW_8BIT`, `TEXTURE_ALLOW_32BIT` или `TEXTURE_ALLOW_DXT` равен 0, проверка формата текстуры отвергает соответственно 8-битные палитровые, 32-битные на пиксель или DXT-форматы.

### 8.3 Переопределения возможностей устройства (учитываются, только когда ключ присутствует)

Эти ключи переопределяют возможности, выведенные из caps устройства Direct3D (и из базы MGSDB). Каждый читается **только если ключ существует** в секции; отсутствующие ключи оставляют обнаруженное значение нетронутым. Значение — retail-целое. Таблица и индекс возможностей: [RP8.cpp](../../../src/RP8/RP8.cpp#L121).

`DEVICE_GEOMETRY` (4), `TEXTURE_SQUARE_ONLY` (5), `TEXTURE_MAX_WIDTH` (6), `TEXTURE_MAX_HEIGHT` (7), `TEXTURE_CUBEMAPS` (8), `DEPTH_BIAS` (9), `FOG_VERTEX` (10), `FOG_PIXEL` (11), `FOG_RANGE` (12), `FOG_W` (13), `RASTER_ANTIALIASEDGES` (14), `DEVICE_SUPPORT_LEVEL` (15), `TEXTURE_TRILINEAR` (16), `DEVICE_BAD_MODE` (17), `DEVICE_BAD_4444` (18). Смысл чисел помимо «0 = выкл / ненулевое = вкл или предел» не проверяется.

### 8.4 Состояния рендера Direct3D (при отсутствии применяется значение по умолчанию)

Каждое имя ниже читается и применяется как начальное значение состояния рендера Direct3D с тем же именем; значение — сырой DWORD, передаваемый устройству (без проверки; состояния типа float принимают битовый шаблон float в hex). Источник: таблица [RP8.cpp](../../../src/RP8/RP8.cpp#L140), чтение в [RP8.cpp](../../../src/RP8/RP8.cpp#L2687). Значения по умолчанию — в скобках.

`ZENABLE` (0x1), `FILLMODE` (0x3), `SHADEMODE` (0x2), `LINEPATTERN` (0x0), `ZWRITEENABLE` (0x1), `ALPHATESTENABLE` (0x0), `LASTPIXEL` (0x1), `SRCBLEND` (0x2), `DESTBLEND` (0x1), `CULLMODE` (0x1), `ZFUNC` (0x2), `ALPHAREF` (0x0), `ALPHAFUNC` (0x8), `DITHERENABLE` (0x0), `ALPHABLENDENABLE` (0x0), `FOGENABLE` (0x0), `SPECULARENABLE` (0x0), `ZVISIBLE` (0x1), `FOGCOLOR` (0x0), `FOGTABLEMODE` (0x1), `FOGSTART` (0x0), `FOGEND` (0x0), `FOGDENSITY` (0x0), `EDGEANTIALIAS` (0x0), `ZBIAS` (0x0), `RANGEFOGENABLE` (0x0), `STENCILENABLE` (0x0), `STENCILFAIL` (0x1), `STENCILZFAIL` (0x1), `STENCILPASS` (0x1), `STENCILFUNC` (0x8), `STENCILREF` (0xFFFFFFFF), `STENCILMASK` (0xFFFFFFFF), `STENCILWRITEMASK` (0xFFFFFFFF), `TEXTUREFACTOR` (0x0), от `WRAP0` до `WRAP7` (0x0), `CLIPPING` (0x1), `LIGHTING` (0x0), `AMBIENT` (0x0), `FOGVERTEXMODE` (0x0), `COLORVERTEX` (0x0), `LOCALVIEWER` (0x0), `NORMALIZENORMALS` (0x0), `DIFFUSEMATERIALSOURCE` (0x1), `SPECULARMATERIALSOURCE` (0x2), `AMBIENTMATERIALSOURCE` (0x2), `EMISSIVEMATERIALSOURCE` (0x0), `VERTEXBLEND` (0x0), `CLIPPLANEENABLE` (0x0), `SOFTWAREVERTEXPROCESSING` (0x0), `POINTSIZE` (0x3F800000), `POINTSIZE_MIN` (0x3F800000), `POINTSPRITEENABLE` (0x0), `POINTSCALEENABLE` (0x0), `POINTSCALE_A` (0x3F800000), `POINTSCALE_B` (0x0), `POINTSCALE_C` (0x0), `MULTISAMPLEANTIALIAS` (0x1), `MULTISAMPLEMASK` (0xFFFFFFFF), `PATCHEDGESTYLE` (0x0), `PATCHSEGMENTS` (0x0), `DEBUGMONITORTOKEN` (0x0), `POINTSIZE_MAX` (0x3F800000), `INDEXEDVERTEXBLENDENABLE` (0x0), `COLORWRITEENABLE` (0xF), `TWEENFACTOR` (0x0), `BLENDOP` (0x1), `POSITIONORDER` (0x1), `NORMALORDER` (0x1).

Примечания:

* `LIGHTING` **читается дважды по имени**: один раз как свойство конвейера (8.2, по умолчанию 1) и один раз как состояние рендера (по умолчанию 0). Поэтому одна строка `LIGHTING =` задаёт оба. `TEXTURE`, `TEXTURE_LOD` и т. д. не являются именами состояний рендера и встречаются только один раз.
* Состояния по стадиям текстур (`COLOROP`, `ALPHAOP`, `COLORARG1/2`, `ADDRESSU/V`, ...) имеют в порте встроенные значения по умолчанию, но **не читаются из INI** (карты стадий заполняются только значениями по умолчанию).
* Ключи, перечисленные в retail `DACOM.INI` как комментарии, например `DEVICE_GAMMA`, этим кодом не читаются.

### 8.5 `[TextureFormatClasses]` (необязательная)

Читается `PopulateFormatCatalog` ([RP8.cpp](../../../src/RP8/RP8.cpp#L1628)); секция необязательна и в dev-файлах отсутствует. Она корректирует, какие пиксельные форматы и в каком порядке пробуются для каждого класса текстур. Встроенные классы задаются четырёхсимвольным кодом: `DAOP`, `DAOT`, `DAA1`, `DAA4`, `DAA8`, от `DXT1` до `DXT5`.

| Строка | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `CLASS = fmt, fmt, ...` | текстовая строка, повторяемая | встроенные списки | Первые четыре байта левого токена — ключ класса (любые четыре символа; короткие токены читают устаревшие байты). Правая часть — список кандидатов через запятую; каждый кандидат имеет вид `bits:red:green:blue:alpha` (пять целых, `sscanf "%d:%d:%d:%d:%d"`). Кандидаты, не совпадающие ни с одним известным пиксельным форматом, отбрасываются. Строка для существующего класса **заменяет** его список кандидатов. `;` начинает комментарий. | [RP8.cpp](../../../src/RP8/RP8.cpp#L1696) |
| `clear` | голая строка | н/д | Строка, начинающаяся с `cle` (код сравнивает только первые три символа, без учёта регистра), опустошает весь каталог, включая встроенные классы. | там же |

---

## 9. `[SoundManager]` и `[AudioProperties]`

Читаются в `FSoundManager::StartupDevice` ([SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3186)). Многие значения по умолчанию — это *текущее значение бита флага*, берущееся из флагов запуска, которые передаёт вызывающий: `DALib::Startup` вызывает `StartupDevice(hwnd, 0x113)`, то есть программный алгоритм `LIGHT`, правосторонние координаты, включены 3D-оборудование и 2D-оборудование, и нет `useEAX` / `createAll2DInSoftware` / `useNoHW`; значения по умолчанию ниже вычислены для этого вызова. Целочисленные ключи используют парсер retail-целых (1.1).

### 9.1 `[SoundManager]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `deviceID` | текст GUID | `{00000000-0000-0000-0000-000000000000}` | GUID устройства DirectSound в фигурных скобках (не более 40 символов). Нулевой GUID выбирает устройство по умолчанию. Неверно сформированный текст не проверяется. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3314) |
| `3D_SW_Algorithm` | enum | `LIGHT` | `FULL`, `LIGHT`, `BASIC`, `USER`, `NONE` (без учёта регистра, читаются первые 32 символа). `FULL`, `LIGHT` и `BASIC` выбирают 3D-алгоритмы DirectSound: HRTF-full, HRTF-light и без виртуализации; `USER` выбирает нулевой GUID алгоритма; `NONE` также использует нулевой GUID и меняет флаги создания DirectSound3D (сбрасывает 0x10, устанавливает 0x40). Любой другой текст сбрасывает биты алгоритма и не выбирает никакого. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3329) |
| `useNoHW` | bool | 0 | Ненулевое значение принудительно выключает оба вида аппаратных буферов (сбрасывает `use2DHW` и `use3DHW` до их чтения, так что они затем по умолчанию равны 0). | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3389) |
| `rightHandedCoordinates` | bool | 1 | Правосторонняя система координат; когда установлено, менеджер также использует масштаб координат -1. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3408) |
| `createAll2DInSoftware` | bool | 0 | Создавать каждый 2D-звук как программный буфер. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3418) |
| `speakerConfiguration` | int | не применяется | Применяется после создания устройства **только если ключ присутствует с непустым значением**. Комментарий retail перечисляет `HEADPHONES 1, STEREO 2, QUAD 7, SURROUND 8` (значения конфигурации динамиков DirectSound). Не проверяется. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3435) |
| `maxSoundChannels` | int | 0 (автоматически) | Максимум одновременных звуков. 0 означает «вывести из оборудования»: большее из числа свободных аппаратных 3D-буферов и буферов микширования, но не менее 8, и без изменений, если карта не сообщает ни одного. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3454) |
| `use2DHW` | bool | 1 | Использовать аппаратные 2D-буферы. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3457) |
| `use3DHW` | bool | 1 | Использовать аппаратные 3D-буферы. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3463) |
| `ALLOW_HW_TO_SW` | bool | 0 | Разрешить пересоздание аппаратного звука в программном виде; действует, только если включён `use2DHW` или `use3DHW`. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3469) |
| `ALLOW_SW_TO_HW` | bool | 0 | Обратное; то же условие. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3475) |
| `FORCE_FREQ_CONTROL_TO_SW` | bool | 1 | Принудительно помещать звуки с управлением частотой в программные буферы. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3481) |
| `NEVER_SET_START_POS` | bool | 0 | Никогда не задавать начальную позицию буфера. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3483) |
| `DISPLAY_EXTRA_DEBUG_INFO` | bool | 0 | Дополнительные отладочные уведомления звука. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3485) |
| `max3DPan` | int | 100 (0x64) | Сохраняется как float `value * 100` (диапазон 3D-панорамы). Не проверяется. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3490) |

### 9.2 `[AudioProperties]` (реверберация EAX)

Ключи float используют отдельный парсер: берётся самый длинный префикс из `0123456789eE.+-` и преобразуется через `atof`.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `useEAX` | bool | 0 | Включить флаг реверберации EAX. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3211) |
| `reverbBaseEnvironment` | int | 0 | Базовый пресет окружения EAX. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3231) |
| `reverbMasterVolume` | float | 0.0 | Главная громкость реверберации. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3247) |
| `reverbDecayTime` | float | 0.1 | Время затухания реверберации. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3263) |
| `reverbDamping` | float | 1.0 | Демпфирование реверберации. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3279) |
| `reverbMix` | float, хранится как int | 0 | Усекается до целого. | [SoundManager.cpp](../../../src/SoundManager/SoundManager.cpp#L3295) |

Ни один из числовых диапазонов не проверяется.

---

## 10. `[Alchemy]` (эффекты частиц)

Читается `ApplyAlchemyIniAndPools` ([alchemy_common.cpp](../../../src/alchemy/alchemy_common.cpp#L1774)) парсером retail-целых. Секция читается при запуске компонента Alchemy и задаёт размеры пулов appearance.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `FxBasicAppearance.poolSize` | int | 1000 | Максимум живых базовых частиц (каждая использует 4 вершины и 6 индексов). Не проверяется; 0 выделяет пустой пул. | [alchemy_common.cpp](../../../src/alchemy/alchemy_common.cpp#L1780) |
| `FxParticleAppearance.poolSize` | int | 1000 | Максимум живых объектов эффектов частиц («generates effects, not particles»). | [alchemy_common.cpp](../../../src/alchemy/alchemy_common.cpp#L1782) |
| `FxRectAppearance.poolSize` | int | 1000 | Прямоугольные частицы. | [alchemy_common.cpp](../../../src/alchemy/alchemy_common.cpp#L1786) |
| `FxPerpAppearance.poolSize` | int | 1000 | Частицы-перпендикулярные квады. | [alchemy_common.cpp](../../../src/alchemy/alchemy_common.cpp#L1788) |
| `FxMeshAppearance.poolSize` | int | 1000 | Mesh appearance (комментарий retail: достаточно для всех колец торговых линий системы). | [alchemy_common.cpp](../../../src/alchemy/alchemy_common.cpp#L1790) |
| `FxOrientedAppearance.poolSize` | int | 1000 | Ориентированные частицы. | [alchemy_common.cpp](../../../src/alchemy/alchemy_common.cpp#L1792) |
| `FLDustAppearance.poolSize` | int | 1000 | Частицы пыли (пользовательский appearance). | [alchemy_common.cpp](../../../src/alchemy/alchemy_common.cpp#L1794) |
| `FLBeamAppearance.poolSize` | int | 1000 | Сегменты луча (по 5 вершин и 24 индекса). | [alchemy_common.cpp](../../../src/alchemy/alchemy_common.cpp#L1796) |
| `Alchemy.useMaterialBatcher` | bool | 1 (читается, затем отбрасывается) | Ключ читается, но его значение ни на что не влияет (как и в коде retail). | [alchemy_common.cpp](../../../src/alchemy/alchemy_common.cpp#L1799) |

Комментарии retail упоминают `Alchemy.maxVertices` / `Alchemy.maxIndices`; это лишь закомментированные подсказки, и они не читаются.

---

## 11. `[Deformable]`

Читается `ApplyCreateInstanceInit` ([Deformable2.cpp](../../../src/Deformable2/Deformable2.cpp#L4690)) парсером retail-целых.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `DeformRate` | int | 0 | `N` даёт для каждого экземпляра скорость деформации `1/N` в виде float; 0 (или отсутствие ключа) даёт скорость 0. Каждый новый экземпляр затем получает случайную фазу, масштабированную этой скоростью. Не проверяется. | [Deformable2.cpp](../../../src/Deformable2/Deformable2.cpp#L4720) |

---

## 12. Shading: `[MaterialBatcher]`, `[BatchedMaterials]`, `[MaterialMap]`, `[TextureLibrary]`, `[TextureFormatMaps]`

### 12.1 `[MaterialBatcher]`

Читается `FMaterialBatcher::StartupFromOuter` ([MaterialBatcher.cpp](../../../src/Shading/MaterialBatcher.cpp#L393)).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `BufferSize` | int | 0x100000 (1 МиБ) | Размер рабочего буфера батчера в байтах. Не проверяется. | [MaterialBatcher.cpp](../../../src/Shading/MaterialBatcher.cpp#L439) |
| `Bin1SortOrder` | int | 2 | Режим сортировки второго бина. Значения от 2 до 5 обрабатываются; 6 и 7 печатают «sort order unimplemented» и ничего не рисуют; 1 печатает предупреждение («sort state not well implemented»); любое другое значение ничего не делает. | [MaterialBatcher.cpp](../../../src/Shading/MaterialBatcher.cpp#L449) |

### 12.2 `[BatchedMaterials]`

Читается `ParseBatchedMaterials` ([MaterialBatcher.cpp](../../../src/Shading/MaterialBatcher.cpp#L474)); повторяемые строки, вычисляются в порядке файла.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `Type` | регулярное выражение | нет | Повторяемый. Материал, чья (переназначенная) строка типа соответствует выражению, создаётся как пакетный материал. Ключ сравнивается через `_stricmp`. | [MaterialBatcher.cpp](../../../src/Shading/MaterialBatcher.cpp#L507) |
| `Name` | регулярное выражение | нет | Повторяемый. То же, но сопоставление идёт с именем материала в нижнем регистре. | [MaterialBatcher.cpp](../../../src/Shading/MaterialBatcher.cpp#L509) |

Любой другой ключ игнорируется. Строка должна содержать `=`. Язык выражений — небольшой движок в стиле Henry Spencer в `Shading/MaterialRegex.cpp` (см. поставляемый пример `Type = .*OcOt`). Побеждает первое подошедшее правило.

### 12.3 `[MaterialMap]`

Читается `FMaterialLibrary::ParseMaterialMap` ([MaterialLibrary.cpp](../../../src/Shading/MaterialLibrary.cpp#L1081)). Каждая строка — одно правило переписывания; правила вставляются в начало списка, поэтому **более поздние строки вычисляются первыми** («put more specific last»).

| Форма строки | Значение | Источник |
|---|---|---|
| `TypePattern = NewType` | Один `=`: правило *типа*. Когда строка типа материала соответствует `TypePattern`, его тип становится `NewType`. | [MaterialLibrary.cpp](../../../src/Shading/MaterialLibrary.cpp#L1096) |
| `name = NamePattern = NewType` | Два `=`: слово перед первым `=` выбирает вид. Селектор `name` (без учёта регистра) делает правило правилом *имени* (шаблон сопоставляется с именем материала); любой другой селектор делает правило правилом типа. Шаблон — текст между первым и последним `=` (так что шаблон не может содержать `=`). | там же |

`NewType` обрезается до 63 символов. Каждое правило логирует уведомление «Adding material map». Пример (синтетический): `name = ^glass_.* = SomeGlassMaterial`.

### 12.4 `[TextureLibrary]`

Читается при запуске `FTextureLibrary` ([TextureLibrary.cpp](../../../src/Shading/TextureLibrary.cpp#L746)) парсером retail-целых. Комментарий retail в `DACOM.INI` утверждает, что `PerfOptions.ini` клиента переопределяет эти настройки (см. [client-audio-misc.md](client-audio-misc.md)).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `TEXTURE_LOD_LOAD` | int | 1 | Режим загрузки уровней детализации текстур (общий слот 0). | [TextureLibrary.cpp](../../../src/Shading/TextureLibrary.cpp#L767) |
| `TEXTURE_LOD_LOAD_MIN` | int | 0 | Наименьший загружаемый размер стороны LOD (общий слот 4), например 128. | [TextureLibrary.cpp](../../../src/Shading/TextureLibrary.cpp#L775) |

### 12.5 `[TextureFormatMaps]` (необязательная)

Читается `FormatMapLoadFromParser` ([TextureLibrary.cpp](../../../src/Shading/TextureLibrary.cpp#L174)); в dev-файлах отсутствует. Библиотека сначала устанавливает восемь встроенных строк, затем добавляет по одной строке на каждую строку файла. Строка с ключом, начинающимся с `cle` (сравниваются три символа), опустошает таблицу, включая встроенные строки.

| Строка | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `f0:f1:f2:f3:f4:f5 = FOUR` | текстовая строка, повторяемая | встроенные строки | Слева от `=`: шесть полей, разделённых пробелами или `:`; каждое — неотрицательное целое или `*` (соответствует чему угодно). Поля 0–3 сравниваются с числом бит на пиксель исходного формата, бит красного, зелёного и синего; поля 4 и 5 оба сравниваются с его битами альфы — именно так делает код. Справа от `=`: первые четыре символа — FourCC целевого класса формата (например `DAOP`, `DAA1`, `DAA4`, `DAA8`, `DAOT`); это должен быть известный FourCC формата, иначе строка отображается в «нет формата». `;` начинает комментарий. Строки пробуются по порядку, побеждает первое совпадение; исходные форматы DXT отображаются сами в себя. Строка с пустой правой частью игнорируется. | [TextureLibrary.cpp](../../../src/Shading/TextureLibrary.cpp#L139) |

---

## 13. `dacomsrv.ini` (сервер)

Сервер загружает тот же парсер и менеджер компонентов, поэтому к нему применимы все секции выше. Dev-файл `bin\dacomsrv.ini`:

```ini
@include FL_Dev.ini
[DACOM]        IgnoreDACOMEnv = true, DllPath = .\
[Libraries]    readfile.dll engbase.dll x86math.dll rendcomp.dll system.dll
               Thorn.dll DebugLib.dll rp8.dll shading.dll
[System]       DirectX8, MaterialLibrary, TextureLibrary
[Engine]       Animation, Hardpoint, RenderManager
[RenderManager] SphereMesh, TriMesh
```

* `@include FL_Dev.ini`: `FL_Dev.ini` в `bin\` отсутствует (как и в `EXE\` для retail-файлов); отсутствующий include игнорируется.
* `DebugLib.dll` этим репозиторием не собирается и в `bin\` отсутствует; `AddLibrary` логирует «unable to find dll» и пропускает её. Инициализация всё равно успешна, потому что остальные записи загружаются.
* Других секций нет, поэтому сервер использует все значения по умолчанию RP8, Shading и звука из секций выше.

---

## 14. `luaprofile.dll` (альтернативный `IProfileParser`)

`luaprofile.dll` регистрирует имя компонента `ILua` и реализует `IProfileParser` поверх приватной виртуальной машины Lua 3.2 вместо текстового INI-файла ([luaprofile.cpp](../../../src/luaprofile/luaprofile.cpp)): `LoadFile(path, 0)` выполняет файл через `lua_dofile`; `OpenSection(name, 3)` открывает **глобальную таблицу Lua** с этим именем (специальное имя `$` открывает сами глобальные переменные Lua); `ReadLine` перечисляет строковые и числовые записи таблицы; `GetString(section, key)` возвращает запись в виде текста. Ни один модуль retail не создаёт `ILua`, `luaprofile.dll` отсутствует в dev-`[Libraries]`, и она не читает никакого INI-файла; здесь она описана только потому, что разделяет интерфейс.

---

## 15. Dev-файл `bin\DACOM.INI` в сопоставлении с кодом

**Присутствует и читается** (всё корректно с точки зрения кода): `[DACOM] IgnoreDACOMEnv` и `DllPath`; `[Libraries]` (абсолютные пути для `ReadFile`, `x86math`, `EngBase`, `system`, `RP8`, `SoundStreamer`, `SoundManager`, `Deformable2`, `Thorn`, `Shading`, `RendComp`, `alchemy`, `ximage`, `flmaterials`); `[System]` (`VMeshLibrary`, `DirectX8`, `SoundManager`, `SoundStreamer`, `TextureLibrary`, `MaterialAnimation`, `MaterialBatcher`, `Alchemy`, `FxRuntime`); `[Engine]` (`Animation`, `Hardpoint`, `Deformable`, `RenderManager`, `FLAppearance`, `VMeshWire`); `[RenderManager]` (`VMesh`, `TriMesh`, `SphereMesh`); `[TriMesh] tristrips`; `[RenderPipeline]` (`MGSDB`, `ALPHAREF`, `ALPHATESTENABLE`, `ALPHAFUNC`, `LOCALVIEWER`, `zfunc`, `HARDWARE_VERTEXPROCESSING`, `USE_SYSLOCK`, `HANDLE_SWAPLOSS`); `[SoundManager]` (`createAll2dInSoftware`, `3D_SW_Algorithm`, `use2DHW`, `use3DHW`, `maxSoundChannels`, `FORCE_FREQ_CONTROL_TO_SW`, `max3DPan`); все девять ключей `[Alchemy]`; `[BatchedMaterials]` и `[MaterialMap]`.

**Примечание о dev-значениях.** `HARDWARE_VERTEXPROCESSING = true` не даёт эффекта сверх значения по умолчанию (аппаратная обработка выбирается, пока не заданы программный/смешанный ключи). `HANDLE_SWAPLOSS = false` и `USE_SYSLOCK = false` переопределяют значения по умолчанию 1 и 0 соответственно. Размеры пулов `[Alchemy]` переопределяют значение по умолчанию 1000 для шести из восьми пулов; `FxOrientedAppearance.poolSize = 0` убирает этот пул.

**Читается кодом, но отсутствует в dev-файле** (применяются значения по умолчанию из этого документа): `[Deformable] DeformRate`; `[MaterialBatcher] BufferSize`, `Bin1SortOrder`; `[TextureLibrary]`; `[TextureFormatMaps]`; `[TextureFormatClasses]`; вся секция `[AudioProperties]`; в `[SoundManager]`: `deviceID`, `useNoHW`, `rightHandedCoordinates`, `speakerConfiguration`, `ALLOW_HW_TO_SW`, `ALLOW_SW_TO_HW`, `NEVER_SET_START_POS`, `DISPLAY_EXTRA_DEBUG_INFO`; в `[RenderPipeline]` каждый ключ, не перечисленный выше (флаги устройства, свойства конвейера, переопределения возможностей и остальные состояния рендера).

**Присутствует в dev-файле, но этим кодом никогда не читается**: на уровне секций — ничего. Из закомментированных строк retail-файла `DEVICE_GAMMA`, `LOCAKABLE_BACKBUFFER` (с опечаткой) и `Alchemy.maxVertices` / `Alchemy.maxIndices` не читаются, даже если раскомментировать. Компоненты, названные в `[System]` (среди них `VMeshLibrary`, `MaterialAnimation`, `FxRuntime`), только запрашиваются по имени; если ни одна загруженная библиотека не регистрирует имя, логируется уведомление «FAILED», а строка пропускается.

**Из DACOM.INI не читается вовсе.** Параметры производительности клиента (`PerfOptions.ini`), `freelancer.ini` и все игровые данные через этот парсер не проходят; `JFLP.dll` и `HudShift.dll` нельзя добавлять в dev-`[Libraries]` (они патчат фиксированные адреса бинарников retail).

## См. также

* [parsers.md](parsers.md): два парсера (`IProfileParser` здесь, `INI_Reader` для игровых данных).
* [freelancer-ini.md](freelancer-ini.md): `freelancer.ini`, путь данных `[Freelancer]` в котором — корень всех игровых данных.
* [client-audio-misc.md](client-audio-misc.md), [client-world-fx.md](client-world-fx.md), [server.md](server.md).
