# Конфигурация клиентского мира, космоса и эффектов (файл системы, звезда, астероиды, туманность)

На этой странице описано, как клиент игры (`freelancer.exe`) читает те части INI-файла системы, которые описывают видимое игроком в космосе, и файлы, на которые он ссылается: файл архетипов звёзд (`solar\StarArch.ini`), файлы астероидных полей и файлы туманностей. Архетипы визуальных эффектов, эффекты прыжков и врат, анимации света и клиентские fuse — в [client-world-fx-effects.md](client-world-fx-effects.md); клиентские ключи записей архетипов (`ICliObj::ReadUnknown`, `[LOD]`) — в [client-world-fx-archetypes.md](client-world-fx-archetypes.md).

**Расположение файла / как он находится.** Файл системы — это файл, названный ключом `file =` в соответствующем блоке `[system]` файла `universe.ini` (разрешается через `Universe::get_filename` относительно каталога `universe.ini`; см. [world-data.md](world-data.md) и [systems-and-bases.md](systems-and-bases.md)). Клиент открывает его, когда игрок входит в систему (`StarSysEnter`, [star_sys.cpp:811](../../../src/freelancer/Client/star_sys.cpp#L811)). Файлы, на которые ссылается файл системы (`[Asteroids] file`, `[Nebula] file`, `[Object] ring`), разрешаются относительно пути данных (`GetFileSysPath(.., DALib::Data)`, `[Freelancer] data path` в [freelancer-ini.md](freelancer-ini.md)); абсолютные пути там допускаются. Записи `[TexturePanels] file` разрешаются через `GetFullFilename` (путь данных присоединяется безусловно, без короткого пути для абсолютных имён). `solar\StarArch.ini` — буквальное имя, тоже относительно пути данных. Все читатели используют `INI_Reader` ([parsers.md](parsers.md)), поэтому любой файл может быть текстовым или BINI, заголовки и имена ключей нечувствительны к регистру, а повторяющиеся ключи и секции доставляются все, в порядке следования в файле.

## Обозначения

| Тип | Значение |
|---|---|
| `float` | `get_value_float(i)`; `ParseNumber` одного параметра, разделённого запятыми, хранится как `float`. `float[0..2]` означает три параметра. |
| `int` | `get_value_int(i)` (MSVCRT `atoi`: десятичное, с переполнением по модулю 2^32). |
| `bool` | `get_value_bool(i)`: текстовые файлы принимают ровно `true`/`1`/`false`/`0`; всё остальное пишет в лог "Invalid bool value" и даёт `false`. |
| `str` (всё значение) | `get_value_string()`: вся правая часть, включая запятые. |
| `str[i]` | `get_value_string(i)`: один параметр, разделённый запятыми, без крайних пробелов. |
| `id` | `CreateID(string)`: 32-битный хэш текста; никнеймы сопоставляются через этот хэш. |
| `byte` | Float, прочитанный из файла и преобразованный усекающим `ftol`; сохраняются только младшие 8 бит, поэтому 300 становится 44, а -1 становится 255. Не проверяется. |

«По умолчанию» — значение, присутствующее до разбора файла (инициализация в конструкторе или загрузчике). Общего механизма значений по умолчанию нет; отсутствующий ключ оставляет это значение, повторяющийся скалярный ключ сохраняет последнее вхождение, если не оговорено иное. Диапазоны нигде не проверяются, если это не указано в таблице («не проверяется» указано там, где пропуск, вероятно, важен).

## 1. Файл системы: клиентский читатель (`StarSysLoadIni`)

[star_sys.cpp:241](../../../src/freelancer/Client/star_sys.cpp#L241). Над одним и тем же файлом выполняются два прохода:

1. `PreloadSystemObjectArchetypes` ([L196](../../../src/freelancer/Client/star_sys.cpp#L196)) читает только `[Object]` / `archetype =` и закрепляет каждый названный архетип solar до конца загрузки.
2. `StarSysLoadIni` один раз обходит каждую секцию в порядке следования в файле и сверяет заголовок со списком ниже. Любой другой заголовок (`[Zone]`, `[EncounterParameters]`, неизвестные имена вроде `[Music]`, ...) здесь пропускается; их читают Common, Content и сервер (см. [world-data.md](world-data.md), [ai-and-encounters.md](ai-and-encounters.md), [systems-and-bases.md](systems-and-bases.md)).

Если файл не удаётся открыть, клиент пишет в лог предупреждение "unknown system (%s)" и продолжает, ничего не читая. Обрабатывается каждое вхождение повторяемой секции (`[Object]`, `[LightSource]`, `[Asteroids]`, `[Nebula]`, `[Archetype]`, `[TexturePanels]`). Неизвестные ключи внутри распознанной секции игнорируются без сообщения, кроме отмеченных случаев.

### 1.1 `[SystemInfo]` (псевдоним `[System]`)

Оба написания заголовка выполняют один и тот же код ([L259](../../../src/freelancer/Client/star_sys.cpp#L259)).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `space_color` | byte x3 (`float[0..2]`) | `0, 0, 64` | RGB-цвет очистки космоса. | [L263](../../../src/freelancer/Client/star_sys.cpp#L263) |
| `space_farclip` | float | 21000 (дальность отрисовки 20000 + 1000) | Дальность дальней плоскости отсечения. Значения меньше 1000 поднимаются до 1000; дальность отрисовки становится значением минус 1000. | [L269](../../../src/freelancer/Client/star_sys.cpp#L269), [space_scene_obj_draw.cpp:2799](../../../src/freelancer/Client/space_scene_obj_draw.cpp#L2799) |

Значения по умолчанию для дальней плоскости применяются заново (`SpaceResetFarClip`) в начале каждой загрузки системы. Эта же секция разбирается также Common и Content (см. [world-data.md](world-data.md)).

### 1.2 `[Music]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `space` | id (`str`) | 0 | Никнейм музыкальной дорожки для обычного полёта. | [L279](../../../src/freelancer/Client/star_sys.cpp#L279) |
| `danger` | id (`str`) | 0 | Дорожка, когда рядом враждебные корабли. | [L281](../../../src/freelancer/Client/star_sys.cpp#L281) |
| `battle` | id (`str`) | 0 | Дорожка во время боя. | [L283](../../../src/freelancer/Client/star_sys.cpp#L283) |
| `explore` | (значение не используется) | - | Устарел. Пишет в лог предупреждение "explore music is obsolete in system file %s, valid types are space, danger and battle". | [L285](../../../src/freelancer/Client/star_sys.cpp#L285) |

Три id обнуляются при каждом входе в систему ([L824](../../../src/freelancer/Client/star_sys.cpp#L824)) и передаются музыкальному автомату состояний ([L981](../../../src/freelancer/Client/star_sys.cpp#L981)); скриптовое переопределение имеет приоритет, а зона с ключом `music` (маска свойств зоны 0x400000) в позиции игрока заменяет дорожку `space`. Никнеймы — это никнеймы звуковых архетипов (см. [client-audio-misc.md](client-audio-misc.md)); их существование при загрузке не проверяется.

### 1.3 `[Archetype]` (список предзагрузки)

Закрепляет архетипы и звуки, чтобы они были резидентны до прибытия игрока. Секция повторяемая, и каждый ключ может повторяться; каждое вхождение добавляет одну запись. Записи снова освобождаются при выходе из системы (`ReleaseSystemPreloads`, [L465](../../../src/freelancer/Client/star_sys.cpp#L465)).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `snd` | id (`str`) | - | Никнейм звука для предзагрузки (клиентский звуковой архетип). | [L297](../../../src/freelancer/Client/star_sys.cpp#L297) |
| `voice` | id (`str[0]`), id (`str[1]`) | - | `voice = <никнейм голоса>, <никнейм сообщения>`. Несуществующий голос пишет в лог "Voice %u is invalid"; сообщение, которого нет у этого голоса, пишет "Voice message %u is invalid for voice %u". | [L305](../../../src/freelancer/Client/star_sys.cpp#L305), [L171](../../../src/freelancer/Client/star_sys.cpp#L171) |
| `ship` | id (`str`) | - | Никнейм архетипа корабля. | [L315](../../../src/freelancer/Client/star_sys.cpp#L315) |
| `equipment` | id (`str`) | - | Никнейм архетипа оборудования. | [L317](../../../src/freelancer/Client/star_sys.cpp#L317) |
| `simple` | id (`str`) | - | Никнейм простого архетипа (взрыв/обломки). | [L319](../../../src/freelancer/Client/star_sys.cpp#L319) |
| `solar` | id (`str`) | - | Никнейм архетипа solar. | [L321](../../../src/freelancer/Client/star_sys.cpp#L321) |
| `asteroid` | id (`str`) | - | Никнейм архетипа астероида. | [L323](../../../src/freelancer/Client/star_sys.cpp#L323) |

Любой другой ключ пишет в лог предупреждение "unknown (or old-style) preload archetype (<line>)". Имя, не являющееся известным архетипом данного вида, пишет "Failed to preload archetype". Имена ищутся среди уже загруженного файлами архетипов ([archetypes-and-equipment.md](archetypes-and-equipment.md)).

### 1.4 `[Ambient]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `color` | byte x3 (`float[0..2]`) | `0, 0, 0` | RGB фонового освещения системы. Рендерер прибавляет пользовательскую игровую опцию фонового света (с ограничением на 255). | [L350](../../../src/freelancer/Client/star_sys.cpp#L350), [L798](../../../src/freelancer/Client/star_sys.cpp#L798) |

### 1.5 `[Background]` (меши звёздной сферы)

В начале каждой секции `[Background]` все три имени слоёв сбрасываются в пустую строку, поэтому секция полностью заменяет предыдущую ([L358](../../../src/freelancer/Client/star_sys.cpp#L358)). Каждое значение — имя меша (архетипа модели), загружаемого как слой звёздной сферы; имя, которое не удаётся загрузить, пишет в лог ошибку "couldnt create starsphere %s" при первой необходимости слоя.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nebulae` | str (всё значение) | пусто | Слой туманностей (слой 0). | [L370](../../../src/freelancer/Client/star_sys.cpp#L370) |
| `basic_stars` | str (всё значение) | пусто | Слой базовых звёзд (слой 1). | [L366](../../../src/freelancer/Client/star_sys.cpp#L366) |
| `complex_stars` | str (всё значение) | пусто | Слой сложных звёзд (слой 2). | [L368](../../../src/freelancer/Client/star_sys.cpp#L368) |
| `file` | str (всё значение) | пусто | Псевдоним: пишет слой 2 точно так же, как `complex_stars`; побеждает последний из двух ключей в файле. | [L372](../../../src/freelancer/Client/star_sys.cpp#L372) |

Какие слои рисуются, определяется настройкой детализации звёздной сферы ([star_sys.cpp:698](../../../src/freelancer/Client/star_sys.cpp#L698)): детализация выше 0.75 рисует туманности и сложные звёзды, выше 0.5 — туманности и базовые звёзды, иначе только туманности; 0.001 или меньше не рисует ничего. Слои рисуются в порядке: базовые, сложные, туманности. Всю сферу можно также отключить игровыми опциями 0x19 / 0x1a.

### 1.6 `[Dust]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `spacedust` | str (всё значение) | пусто | Имя общесистемного эффекта космической пыли, передаётся в `SpaceDustSetSystemDust`. (Ключ `spacedust` уровня зоны читает Common, см. [world-data.md](world-data.md).) | [L382](../../../src/freelancer/Client/star_sys.cpp#L382) |

### 1.7 `[LightSource]`

Каждая секция `[LightSource]` создаёт один игровой источник света, передаёт каждый ключ разборщику света ниже и затем включает его ([star_sys.cpp:388](../../../src/freelancer/Client/star_sys.cpp#L388), `GameLightLoad` [engine_light.cpp:1803](../../../src/freelancer/Client/engine_light.cpp#L1803)). Имена ключей здесь сравниваются строковым сравнением без учёта регистра; нераспознанный ключ игнорируется без сообщения. Позиционные числа читаются через `atof` из текста значения, разделённого запятыми (`ValueNum`, [common_ini.cpp:326](../../../src/Common/common_ini.cpp#L326)), поэтому шестнадцатеричная и N-ричная формы, которые принимает `get_value_float`, здесь **не** понимаются.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `type` | enum, одно слово | нет (-1) | `AMBIENT`, `POINT`, `CONE` или `DIRECTIONAL` (без учёта регистра; внутренние id -2, 1, 2, 3). Всё остальное пишет в лог ошибку "invalid [LightSource] type = '%s'" и задаёт тип -1. Задавайте его до `pos`/`direction`: эти ключи ведут себя по-разному для разных типов. | [L749](../../../src/freelancer/Client/engine_light.cpp#L749), [L591](../../../src/freelancer/Client/engine_light.cpp#L591) |
| `pos` | число x3 | `0, 0, 0` | Позиция света. Ось Z инвертируется для копии Direct3D; для источников `DIRECTIONAL` меняется только сохранённая позиция. | [L701](../../../src/freelancer/Client/engine_light.cpp#L701) |
| `range` | число | 0 | Дальность действия света. | [L720](../../../src/freelancer/Client/engine_light.cpp#L720) |
| `color` | число x3 | `1, 1, 1` | RGB в диапазоне 0..255; ограничивается до 0..255 на канал и хранится целыми шагами 1/255. | [L726](../../../src/freelancer/Client/engine_light.cpp#L726) |
| `attenuation` | число x3 | `0, 1, 0` | Постоянное, линейное и квадратичное затухание. | [L757](../../../src/freelancer/Client/engine_light.cpp#L757) |
| `direction` | число x3 | нет | Вектор направления, нормализуется при чтении (нулевой вектор приводит к делению на ноль, защиты нет). Игнорируется для копии Direct3D у источников `POINT`. | [L767](../../../src/freelancer/Client/engine_light.cpp#L767) |
| `ambient_curve` | str (всё значение) | нет | Имя графика (из `igraph.ini`, `[Data] igraph`), сохраняется как кривая фонового света источника. Неизвестное имя показывает окно сообщения "Error: unknown graph name". | [L795](../../../src/freelancer/Client/engine_light.cpp#L795) |
| `atten_curve` | имя графика (`str[0]`), необязательный тип (`str[1]`) | нет | `atten_curve = <график>[, <тип>]`. Необязательный второй параметр — тип света (те же слова, что у `type`), применяется первым. График становится кривой спада с расстоянием (вычисляется на `0..range`); у света `AMBIENT` он сохраняется как кривая фонового света. Неизвестный график: окно сообщения. | [L813](../../../src/freelancer/Client/engine_light.cpp#L813) |
| `color_curve` | имя графика (`str[0]`), число (`[1]`) | нет | `color_curve = <график>, <период в секундах>`: цвет графика умножает цвет света во времени; время зацикливается по периоду. Неизвестный график: окно сообщения. | [L823](../../../src/freelancer/Client/engine_light.cpp#L823) |

Имена графиков определяются секциями `[IGraph]` файла `igraph.ini` (клиентский блок UI/конфигурации: [client-ui-config.md](client-ui-config.md)).

### 1.8 `[Object]` (клиентская часть)

Секции `[Object]` потребляются трижды: Common (`CSolar::ReadObj`, который создаёт объект по собственным ключам, таким как `nickname`, `pos`, `archetype`; см. [world-data.md](world-data.md)), клиентским предварительным проходом (`archetype`, раздел 1) и клиентским игровым объектом solar `FClientSolarGameobj::ReadIni` ([client_gameobj.cpp:2148](../../../src/freelancer/Client/client_gameobj.cpp#L2148)), который перечитывает ту же секцию после завершения Common ([space_solar_world.cpp:26](../../../src/freelancer/Client/space_solar_world.cpp#L26)). Клиентская часть проверяет ключи ниже в указанном порядке, и побеждает первый совпавший ключ; все остальные ключи игнорируются.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `archetype` | никнейм solar (`str`) | - | Только предварительный проход: закрепляет архетип solar на время загрузки системы. Неизвестный никнейм пишет в лог ошибку "unknown Solar archetype (%s)" и после этого не защищается. | [star_sys.cpp:210](../../../src/freelancer/Client/star_sys.cpp#L210) |
| `ring` | `str[0]`, `str[1]` | - | `ring = <никнейм зоны>, <ini-файл>`: создаёт кольцо из текстурных панелей вокруг названной зоны этой системы, читается из INI-файла (путь относительно пути данных). Повторяемый; каждый ключ создаёт одно кольцо. Несуществующая зона разыменовывается без проверки. Формат файла кольца (`[TexturePanels]`, `[Exterior]`, `[Clouds]`) читает `FTexturePanels::Load` ([texture_panels.cpp:961](../../../src/freelancer/Client/texture_panels.cpp#L961)); см. [client-ui-config.md](client-ui-config.md). | [L2159](../../../src/freelancer/Client/client_gameobj.cpp#L2159) |
| `burn_color` | float x3 | не задан | Цвет выгорания объекта, 0..255 на канал (при чтении делится на 255). | [L2171](../../../src/freelancer/Client/client_gameobj.cpp#L2171) |
| `dock_script` | str (всё значение) | пусто | Сохраняется как имя скрипта стыковки объекта; усекается до 259 символов. | [L2183](../../../src/freelancer/Client/client_gameobj.cpp#L2183) |
| `voice` | id (`str`) | 0 | Никнейм голоса, сохраняемый в объекте solar. | [L2189](../../../src/freelancer/Client/client_gameobj.cpp#L2189) |
| `jump_effect` | id (`str`) | 0 (используется `jump_effect_default`) | Никнейм блока `[JumpGateEffect]` файла эффектов прыжка ([client-world-fx-effects.md](client-world-fx-effects.md)). | [L2197](../../../src/freelancer/Client/client_gameobj.cpp#L2197) |
| `star` | str (всё значение) | - | Никнейм блока `[star]` файла `solar\StarArch.ini` (раздел 2). Создаёт звёздную сферу для этого объекта. | [L2202](../../../src/freelancer/Client/client_gameobj.cpp#L2202) |

После ключей объект ищет туманность, которая его содержит (ключ не участвует).

### 1.9 `[TexturePanels]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `file` | str (всё значение) | - | INI-библиотека форм (записи `[Texture]`/`[Shape]`), добавляемая в базу форм системы. Повторяемый; разрешается через `GetFullFilename`. Формат: [client-ui-config.md](client-ui-config.md). | [L405](../../../src/freelancer/Client/star_sys.cpp#L405) |

### 1.10 `[Asteroids]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `file` | str (всё значение), максимум 255 символов | пусто | INI астероидного поля (раздел 3), путь относительно пути данных. | [L423](../../../src/freelancer/Client/star_sys.cpp#L423) |
| `zone` | str (всё значение), максимум 63 символа | пусто | Никнейм зоны этой системы, которую заполняет поле. Используются последние `file` и `zone` секции, поэтому одна секция создаёт ровно одно поле. | [L425](../../../src/freelancer/Client/star_sys.cpp#L425) |

Первая секция `[Asteroids]` загрузки также загружает астероидные данные Common для системы (`CmnAsteroid::Load`). Несуществующая зона пишет в лог ошибку "AsteroidField (%s) linked to missing Zone (%s)", и поле не загружается. Ту же секцию читают сервер и Common ([server.md](server.md), [world-data.md](world-data.md)).

### 1.11 `[Nebula]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `file` | str (всё значение), максимум 255 символов | - | INI туманности (раздел 4), путь относительно пути данных. Каждый ключ `file` создаёт новый объект туманности; если в секции их несколько, загружается только последний (предыдущие отбрасываются без освобождения). Отсутствие ключа `file` означает, что ничего не загружается. | [L439](../../../src/freelancer/Client/star_sys.cpp#L439) |
| `zone` | str (всё значение), максимум 63 символа | пусто | Никнейм зоны, ограничивающей туманность (побеждает последнее вхождение). Отсутствующая зона пишет в лог ошибку "Nebula linked to missing Zone (%s)"; затем загрузчик продолжает без защиты. | [L446](../../../src/freelancer/Client/star_sys.cpp#L446) |

Пример (синтетический):

```ini
[Asteroids]
file = solar\asteroids\demo_field.ini
zone = Zone_Demo_field

[Nebula]
file = solar\nebula\demo_nebula.ini
zone = Zone_Demo_nebula
```

## 2. Архетипы звёзд: `solar\StarArch.ini`

Объект получает видимую звезду, если в его `[Object]` есть `star = <никнейм>` (раздел 1.8). Клиент загружает `solar\StarArch.ini` как библиотеку форм в начале каждого входа в космос (`LoadSolarStarArchIni`, [star_sphere.cpp:2740](../../../src/freelancer/Client/star_sphere.cpp#L2740)); тот же файл содержит и описанные здесь записи `[star]`, `[star_glow]`, `[lens_glow]`, `[lens_flare]` и `[spines]`, которые читает `FStarSphere::Load` ([star_sphere.cpp:1548](../../../src/freelancer/Client/star_sphere.cpp#L1548)). Используемые ниже имена форм — это имена `[Shape]` этого же файла (формат в [client-ui-config.md](client-ui-config.md)).

**Правило поиска записи (для всех пяти типов записей).** Файл просматривается заголовок за заголовком. Для заголовка с нужным именем читатель ищет **первый ключ `nickname`** блока и сравнивает его значение с запрошенным никнеймом (без учёта регистра). Если они различаются, блок отбрасывается; если совпадают, применяются только ключи **после** `nickname`. Поэтому ключ, расположенный до `nickname`, никогда не используется, и `nickname` должен идти первым. Читается только первый подходящий блок. Об отсутствующей записи или файле ничего не пишется в лог.

### 2.1 `[star]`

Выбирается по никнейму `star =` объекта.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | str (всё значение) | - | Ключ записи. | [L1577](../../../src/freelancer/Client/star_sphere.cpp#L1577) |
| `radius` | float | 1000 | Радиус звезды в метрах. | [L1590](../../../src/freelancer/Client/star_sphere.cpp#L1590) |
| `star_glow` | str (всё значение) | встроенное свечение | Никнейм блока `[star_glow]` (внешний биллборд свечения). Применяется один раз: после первого успешного применения последующие ключи `star_glow` игнорируются; неудачный поиск может быть повторён следующим ключом. | [L1594](../../../src/freelancer/Client/star_sphere.cpp#L1594) |
| `star_center` | str (всё значение) | нет | Никнейм блока `[star_glow]`, используемого как яркий центр. Каждое вхождение заменяет предыдущее; неудачный поиск не оставляет центра. | [L1600](../../../src/freelancer/Client/star_sphere.cpp#L1600) |
| `lens_glow` | str (всё значение) | встроенное тело | Никнейм блока `[lens_glow]` (корона/тело). Применяется один раз, как и `star_glow`. | [L1623](../../../src/freelancer/Client/star_sphere.cpp#L1623) |
| `lens_flare` | str (всё значение) | пустой блик | Никнейм блока `[lens_flare]`. Применяется один раз. | [L1617](../../../src/freelancer/Client/star_sphere.cpp#L1617) |
| `spines` | str (всё значение) | пусто | Никнейм блока `[spines]`. Применяется один раз. | [L1629](../../../src/freelancer/Client/star_sphere.cpp#L1629) |
| `intensity_fade_in` | float | 1.0 | Секунды нарастания яркости звезды; ровно 0 заменяется на 1e-7. | [L1635](../../../src/freelancer/Client/star_sphere.cpp#L1635) |
| `intensity_fade_out` | float | 1.0 | Секунды затухания; ровно 0 становится 1e-7. | [L1643](../../../src/freelancer/Client/star_sphere.cpp#L1643) |
| `zone_occlusion_fade_in` | float | 2.0 | В реализации (совпадающей с листингом retail) значение записывается в поле нарастания **intensity**; на 0 проверяется и получает 1e-7 только поле zone-occlusion. Само поле zone-occlusion сохраняет 2.0. | [L1650](../../../src/freelancer/Client/star_sphere.cpp#L1650) |
| `zone_occlusion_fade_out` | float | 2.0 | Тот же эффект для полей затухания. | [L1659](../../../src/freelancer/Client/star_sphere.cpp#L1659) |

### 2.2 `[star_glow]`

Используется и для `star_glow`, и для `star_center` ([star_sphere.cpp:2211](../../../src/freelancer/Client/star_sphere.cpp#L2211)). Цвета — обычные float (0..1), а не 0..255.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | str (всё значение) | - | Ключ записи (см. правило поиска). | [L2228](../../../src/freelancer/Client/star_sphere.cpp#L2228) |
| `shape` | str (всё значение), максимум 31 символ | пусто | Имя формы в библиотеке форм StarArch. | [L2240](../../../src/freelancer/Client/star_sphere.cpp#L2240) |
| `scale` | float | 1.0 | Множитель размера (умножается на радиус звезды). | [L2253](../../../src/freelancer/Client/star_sphere.cpp#L2253) |
| `inner_color` | float x3 | `1, 1, 1` | Цвет в центре биллборда. | [L2257](../../../src/freelancer/Client/star_sphere.cpp#L2257) |
| `outer_color` | float x3 | `1, 1, 1` | Цвет на краю. | [L2266](../../../src/freelancer/Client/star_sphere.cpp#L2266) |
| `alpha` | float | 1.0 | Непрозрачность. | [L2275](../../../src/freelancer/Client/star_sphere.cpp#L2275) |

### 2.3 `[lens_glow]`

[star_sphere.cpp:2288](../../../src/freelancer/Client/star_sphere.cpp#L2288). После чтения блока смешанный цвет пересчитывается из `inner_color`, `outer_color` и `interior_tween`, и геометрия короны перестраивается.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | str (всё значение) | - | Ключ записи. | [L2306](../../../src/freelancer/Client/star_sphere.cpp#L2306) |
| `shape` | str (всё значение), максимум 31 символ | пусто | Имя формы. | [L2318](../../../src/freelancer/Client/star_sphere.cpp#L2318) |
| `radius_scale` | float | 2.0 | Размер короны относительно экранного размера звезды (умножает пиксельный масштаб звезды). | [L2334](../../../src/freelancer/Client/star_sphere.cpp#L2334) |
| `inner_color` | float x3 | `1, 1, 1` | Внутренний цвет (0..1). | [L2338](../../../src/freelancer/Client/star_sphere.cpp#L2338) |
| `outer_color` | float x3 | `1, 1, 1` | Внешний цвет (0..1). | [L2347](../../../src/freelancer/Client/star_sphere.cpp#L2347) |
| `glow_fade_in_seconds` | float | 0.25 | Время нарастания свечения. | [L2356](../../../src/freelancer/Client/star_sphere.cpp#L2356) |
| `glow_fade_out_seconds` | float | 0.25 | Время затухания. | [L2360](../../../src/freelancer/Client/star_sphere.cpp#L2360) |
| `interior_tween` | float | 0.5 | Коэффициент смешивания внутреннего и внешнего цвета для внутренних слотов; также доля внутреннего/внешнего радиуса внутреннего кольца. Не проверяется. | [L2364](../../../src/freelancer/Client/star_sphere.cpp#L2364) |
| `interior_ray_tween` | float | 1.0 | Масштаб, применяемый к экранным смещениям внешних точек короны (используется начиная с уровня детализации 3). Не проверяется. | [L2368](../../../src/freelancer/Client/star_sphere.cpp#L2368) |
| `max_LOD` | float, преобразуется в unsigned | 5 | Наивысший уровень детализации короны; активный уровень также понижается настройкой производительности `SunEffect`. Не проверяется (отрицательные значения не определены). | [L2372](../../../src/freelancer/Client/star_sphere.cpp#L2372) |

### 2.4 `[lens_flare]`

[star_sphere.cpp:2511](../../../src/freelancer/Client/star_sphere.cpp#L2511). Прежний список бусин освобождается при поиске блока, даже если поиск затем не удался.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | str (всё значение) | - | Ключ записи. | [L2531](../../../src/freelancer/Client/star_sphere.cpp#L2531) |
| `bead` | float x6 | - | Одна бусина блика на ключ (повторяемый). Шесть чисел сохраняются без изменений; код их не именует. | [L2543](../../../src/freelancer/Client/star_sphere.cpp#L2543) |
| `min_radius` | float | 5.0 | Нижняя граница радиуса блика. | [L2550](../../../src/freelancer/Client/star_sphere.cpp#L2550) |
| `max_radius` | float | 800 | Верхняя граница радиуса блика. | [L2554](../../../src/freelancer/Client/star_sphere.cpp#L2554) |
| `shape` | str (всё значение), максимум 31 символ | пусто | Имя формы текстуры бусины. | [L2558](../../../src/freelancer/Client/star_sphere.cpp#L2558) |

### 2.5 `[spines]`

[star_sphere.cpp:2600](../../../src/freelancer/Client/star_sphere.cpp#L2600). Список лучей освобождается при поиске блока.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | str (всё значение) | - | Ключ записи. | [L2621](../../../src/freelancer/Client/star_sphere.cpp#L2621) |
| `radius_scale` | float | 1.0 | Множитель, применяемый к первым двум числам каждого луча после чтения блока. | [L2633](../../../src/freelancer/Client/star_sphere.cpp#L2633) |
| `shape` | str (всё значение), максимум 31 символ | пусто | Имя формы. | [L2637](../../../src/freelancer/Client/star_sphere.cpp#L2637) |
| `min_radius` | float | 1.0 | Нижняя граница радиуса луча. | [L2653](../../../src/freelancer/Client/star_sphere.cpp#L2653) |
| `max_radius` | float | 1000 | Верхняя граница. | [L2657](../../../src/freelancer/Client/star_sphere.cpp#L2657) |
| `spine` | float x9 | - | Один луч на ключ (повторяемый), девять чисел сохраняются по порядку (в коде не именуются). Лучи равномерно распределяются по окружности в порядке следования в файле (угол `2*pi*index/count`). | [L2661](../../../src/freelancer/Client/star_sphere.cpp#L2661) |

Пример (синтетический):

```ini
[star]
nickname = demo_star
radius = 2500
star_glow = demo_glow
lens_glow = demo_corona
intensity_fade_in = 1.5

[star_glow]
nickname = demo_glow
shape = demo_glow_shape
scale = 4
inner_color = 1, 0.9, 0.6
outer_color = 1, 0.5, 0.2
```

## 3. Файлы астероидных полей (`[Asteroids] file =`)

Загружается `CliAsteroidLoadFieldIni` ([cli_asteroid.cpp:858](../../../src/freelancer/Client/cli_asteroid.cpp#L858)). Тот же файл полностью читают и несколько других читателей: сервер сам читает `[Field]` (`cube_size`, `fill_dist`, `empty_cube_frequency`) ([server.md](server.md)), а `CAsteroidField::load` из Common ([common_casteroidfield.cpp:74](../../../src/Common/common_casteroidfield.cpp#L74)) читает `[Cube]`, `[Exclusion Zones]` и `[LootableZone]` из того же открытого файла после клиентских секций (см. [archetypes-and-equipment.md](archetypes-and-equipment.md) и [server.md](server.md)). Секция `[DynamicAsteroids]` описана в 3.5.

Поведение:

* Файл, который не открывается, пишет в лог "asteroid field ini file (%s) could not be opened"; объект поля остаётся зарегистрированным. Поле без зоны пишет "Asteroid field (%s) must be hooked up to a zone".
* Секции обрабатываются в порядке следования в файле. `[TexturePanels]` должна идти **до** любых `[Band]`, `[ExclusionBand]` и `[AsteroidBillboards]`, называющих формы: формы ищутся во время разбора секции, в собственной библиотеке форм поля, построенной из уже прочитанных записей `[TexturePanels] file =`.
* Значения оттенка из `[Field]` применяются к `[Band]` и `[AsteroidBillboards]` в момент чтения этой секции, поэтому `[Field]` должна идти первой; секции `[ExclusionBand]` получают итоговый оттенок после чтения всего файла.
* Неизвестные секции и ключи игнорируются. Числовые значения, преобразуемые в целые, усекаются к нулю.

### 3.1 `[TexturePanels]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `file` | str (всё значение) | - | INI-библиотека форм для этого поля (`GetFullFilename`). Повторяемый. | [L907](../../../src/freelancer/Client/cli_asteroid.cpp#L907) |

### 3.2 `[Field]`

[ParseFieldSection, cli_asteroid.cpp:779](../../../src/freelancer/Client/cli_asteroid.cpp#L779).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `cube_size` | float, усекается до int | 250 | Длина ребра одного куба поля; также передаётся загрузчику поля в Common. | [L788](../../../src/freelancer/Client/cli_asteroid.cpp#L788) |
| `fill_dist` | float, усекается до int | 1200 | Расстояние вокруг камеры, в пределах которого заполняются кубы; перед передачей общему полю умножается на масштаб ползунка детализации. | [L792](../../../src/freelancer/Client/cli_asteroid.cpp#L792) |
| `LOD` | float, усекается до int | 0 | Принудительный уровень детализации мешей астероидов; при применении ограничивается диапазоном `0..(число LOD - 1)` каждого меша. | [L796](../../../src/freelancer/Client/cli_asteroid.cpp#L796) |
| `tint_field` | float x3 (0..255) | `1, 1, 1` (после /255) | Задаёт **оба** оттенка — диффузный и фоновый (каждый /255). | [L802](../../../src/freelancer/Client/cli_asteroid.cpp#L802) |
| `diffuse_color` | float x3 (0..255) | `1, 1, 1` | Только диффузный оттенок; побеждает более поздний из `tint_field`/`diffuse_color`. | [L814](../../../src/freelancer/Client/cli_asteroid.cpp#L814) |
| `ambient_color` | float x3 (0..255) | `1, 1, 1` | Только фоновый оттенок. | [L820](../../../src/freelancer/Client/cli_asteroid.cpp#L820) |
| `ambient_increase` | float x3, усекается до байтов | `0, 0, 0` | Дополнительный фоновый свет, добавляемый к поясам (каждый /255 при применении). | [L826](../../../src/freelancer/Client/cli_asteroid.cpp#L826) |
| `empty_cube_frequency` | float | 0 | Доля пустых кубов; передаётся общему полю после загрузки (`set_empty_freq`). Не проверяется. | [L832](../../../src/freelancer/Client/cli_asteroid.cpp#L832) |
| `max_alpha` | float | 1.0 | Максимальная альфа астероидов поля («полная» альфа до затухания). Не проверяется. | [L836](../../../src/freelancer/Client/cli_asteroid.cpp#L836) |
| `test_asteroid_placement` | только наличие | выкл. | Значение игнорируется. Если ключ присутствует, проход отрисовки ближних астероидов сразу возвращается ([L2616](../../../src/freelancer/Client/cli_asteroid.cpp#L2616)): астероиды кубов не рисуются (отладочный переключатель размещения). | [L840](../../../src/freelancer/Client/cli_asteroid.cpp#L840) |
| `contains_fog_zone` | только наличие | выкл. | Значение игнорируется. Если ключ присутствует и камера находится внутри туманности, фоновый проход поля берёт коэффициент тумана из этой туманности ([L2131](../../../src/freelancer/Client/cli_asteroid.cpp#L2131)). | [L844](../../../src/freelancer/Client/cli_asteroid.cpp#L844) |

### 3.3 `[Band]` и `[ExclusionBand]`

Пояс — это кольцо из мешей-«спиц» вокруг зоны. `[Band]` (осмысленна не более чем одна; более поздняя заново разбирается в тот же пояс) принадлежит собственной зоне поля ([L924](../../../src/freelancer/Client/cli_asteroid.cpp#L924)); каждая `[ExclusionBand]` создаёт дополнительный пояс и берёт зону, названную её ключом `zone` ([L938](../../../src/freelancer/Client/cli_asteroid.cpp#L938)). Обе используют один и тот же разборщик значений ([BandParseIni, L169](../../../src/freelancer/Client/cli_asteroid.cpp#L169)).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `shape` | str (всё значение) | нет | Имя формы в библиотеке форм поля. Ненайденное имя пишет в лог предупреждение библиотеки форм и не оставляет формы. | [L181](../../../src/freelancer/Client/cli_asteroid.cpp#L181) |
| `texture_aspect` | float | 1.0 (хранимое значение — ключ, умноженный на 1.15) | Соотношение сторон одной спицы; длина окружности кольца делится на `aspect * height`, чтобы получить число точек кольца. | [L187](../../../src/freelancer/Client/cli_asteroid.cpp#L187), [L3133](../../../src/freelancer/Client/cli_asteroid.cpp#L3133) |
| `height` | float | 1000 | Высота спицы. Четыре расстояния `fade` при построении пояса умножаются на неё, затем она делится пополам для использования. | [L193](../../../src/freelancer/Client/cli_asteroid.cpp#L193), [L3159](../../../src/freelancer/Client/cli_asteroid.cpp#L3159) |
| `offset_dist` | float | 0 | Режим расстояния: радиус кольца равен размеру зоны (по каждой оси, X и Z) **минус** это значение. Сбрасывает процентный режим. | [L197](../../../src/freelancer/Client/cli_asteroid.cpp#L197), [L3189](../../../src/freelancer/Client/cli_asteroid.cpp#L3189) |
| `offset_percent` | float | - | Процентный режим: радиус кольца равен размеру зоны **умножить** на это значение (1.0 = край зоны). Режим выбирает последний в секции из `offset_dist`/`offset_percent`. | [L202](../../../src/freelancer/Client/cli_asteroid.cpp#L202), [L3196](../../../src/freelancer/Client/cli_asteroid.cpp#L3196) |
| `color_shift` | float x3 | `1, 1, 1` | Покомпонентный множитель оттенка пояса. | [L207](../../../src/freelancer/Client/cli_asteroid.cpp#L207) |
| `fade` | float x4 | `1500, 2500, 100000, 130000` | Четыре расстояния a, b, c, d **в единицах `height` пояса** (умножаются на неё). Спица невидима ближе `a`, нарастает между `a` и `b`, полностью видима от `b` до `c`, затухает между `c` и `d` и невидима за `d`. Не проверяется (предполагается возрастающий порядок). | [L213](../../../src/freelancer/Client/cli_asteroid.cpp#L213), [L1530](../../../src/freelancer/Client/cli_asteroid.cpp#L1530) |
| `vert_increase` | float, усекается до int | 1 | Множитель числа точек кольца; значения меньше 1 поднимаются до 1. | [L220](../../../src/freelancer/Client/cli_asteroid.cpp#L220) |
| `render_parts` | float, усекается до int | 1 | Число частей, на которые кольцо делится для отрисовки; значения меньше 1 поднимаются до 1, а значение ограничивается половиной числа точек кольца. | [L227](../../../src/freelancer/Client/cli_asteroid.cpp#L227), [L3288](../../../src/freelancer/Client/cli_asteroid.cpp#L3288) |
| `cull_mode` | float, усекается до int | 0 | Режим отсечения меша пояса (передаётся как есть; допустимые числа не проверяются). | [L234](../../../src/freelancer/Client/cli_asteroid.cpp#L234) |
| `ambient_intensity` | float | 1.0 | Множитель фонового освещения. | [L238](../../../src/freelancer/Client/cli_asteroid.cpp#L238) |
| `zone` (только `[ExclusionBand]`) | str (всё значение) | зона поля | Никнейм зоны исключения, которую окружает этот пояс; повторяемый, побеждает последний допустимый. Неизвестная зона пишет в лог "Exband (exclusion zone band) linked to missing Zone (%s)". | [L961](../../../src/freelancer/Client/cli_asteroid.cpp#L961) |

### 3.4 `[AsteroidBillboards]`

Далёкие камни-биллборды ([BillboardParseIni, L427](../../../src/freelancer/Client/cli_asteroid.cpp#L427)). Читается один раз; более поздняя секция заново разбирается в тот же блок.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `shape` | str (всё значение) | нет | Имя формы в библиотеке форм поля. | [L437](../../../src/freelancer/Client/cli_asteroid.cpp#L437) |
| `count` | float, усекается до int | 100 | Число камней-биллбордов. Поле может поднять его до максимума менеджера исключений. | [L443](../../../src/freelancer/Client/cli_asteroid.cpp#L443) |
| `size` | float x2 | 500 / 100 | Минимальный и максимальный размер камня. Два параметра меняются местами, если второй меньше, так что порядок не важен. | [L447](../../../src/freelancer/Client/cli_asteroid.cpp#L447) |
| `start_dist` | float | 1500 | Расстояние, с которого начинаются биллборды. | [L460](../../../src/freelancer/Client/cli_asteroid.cpp#L460) |
| `fade_dist_percent` | float | 0.3 | Доля расстояния, используемая для плавного перехода; ограничивается диапазоном `0..1`. | [L464](../../../src/freelancer/Client/cli_asteroid.cpp#L464) |
| `ambient_intensity` | float | 1.0 | Множитель фонового освещения. | [L473](../../../src/freelancer/Client/cli_asteroid.cpp#L473) |
| `color_shift` | float x3 | `1, 1, 1` | Покомпонентный множитель оттенка. | [L477](../../../src/freelancer/Client/cli_asteroid.cpp#L477) |

### 3.5 `[DynamicAsteroids]`

Движущиеся астероиды, порождаемые вокруг камеры ([CliDynamicAsteroidParseIni, cli_asteroid_dynamic.cpp:420](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L420)). Каждая секция создаёт один менеджер. Оттенок объемлющего поля применяется к нему после разбора.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `asteroid` | str[0] | нет | Никнейм архетипа `[DynamicAsteroid]` ([archetypes-and-equipment.md](archetypes-and-equipment.md)). Промах пишет в лог предупреждение "dynamic asteroid archetype (%s) not found", и ничего не порождается. | [L427](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L427) |
| `count` | float, усекается до int | 10 | Число одновременно живых астероидов; перед использованием масштабируется настройкой детализации астероидов (`count * scale + 0.5`, масштаб от 0.5 до 1). | [L449](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L449) |
| `placement_radius` | float | 150 | Радиус вокруг камеры, в котором размещаются астероиды; из него выводится внутренний радиус, равный 70% от него. | [L454](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L454) |
| `max_velocity` | float | 50 | Верхняя граница случайной линейной скорости. | [L461](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L461) |
| `max_angular_velocity` | float | 5 | Верхняя граница случайного вращения. | [L465](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L465) |
| `color_shift` | float x3 | `1, 1, 1` | Покомпонентный множитель оттенка. | [L469](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L469) |
| `zone` | str (всё значение) | зона поля | Никнейм зоны этой системы, в которой живут астероиды; читается из той же секции загрузчиком поля, побеждает последний допустимый, а промах пишет в лог "DynamicAsteroids, linked to missing Zone (%s)". Без допустимого `zone` используется собственная зона поля. | [cli_asteroid.cpp:1025](../../../src/freelancer/Client/cli_asteroid.cpp#L1025) |

Пример (синтетический):

```ini
[TexturePanels]
file = solar\asteroids\demo_shapes.ini

[Field]
cube_size = 400
fill_dist = 2000
tint_field = 200, 190, 180

[AsteroidBillboards]
shape = demo_rock
count = 150
size = 300, 800
```

## 4. Файлы туманностей (`[Nebula] file =`)

Загружается `FNebulaEffect::Load(file, zone)` ([cli_nebula.cpp:1829](../../../src/freelancer/Client/cli_nebula.cpp#L1829)); файл разрешается относительно пути данных, а нечитаемый файл пишет в лог "unable to open nebula file %s" (туманность всё равно создаётся со значениями по умолчанию). Секции обрабатываются в порядке следования в файле, применяется каждое вхождение секции, а имена форм, используемые `[Exterior]`/`[Clouds]`, ищутся **в момент чтения секции** в собственной библиотеке форм туманности, поэтому `[TexturePanels]` должна идти первой. Значения цвета — float 0..255, делённые на 255, если таблица не говорит иного. Неизвестные секции пропускаются; `[ObjectFog]` распознаётся, но её ключи никогда не читаются. Целочисленные переключатели читаются как float и усекаются; любое ненулевое значение включает их.

| Секция | Назначение |
|---|---|
| `[TexturePanels]` | Библиотеки форм туманности. |
| `[Exterior]` | Пышные «кусочки», формирующие внешнюю часть облака. |
| `[Clouds]` | Один слой облаков из спрайтов-клубов внутри туманности (повторяемая; каждая создаёт слой). |
| `[Fog]` | Параметры тумана внутри туманности. |
| `[Lightning]`, `[BackgroundLightning]`, `[DynamicLightning]` | Три независимых эффекта молний. |
| `[NebulaLight]` | Фоновое освещение и пробивание солнца. |
| `[Exclusion Zones]` | Зоны, в которых туманность разрежается. |

### 4.1 `[TexturePanels]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `file` | str (всё значение) | - | INI-библиотека форм (`GetFullFilename`). Повторяемый. | [L1913](../../../src/freelancer/Client/cli_nebula.cpp#L1913) |

### 4.2 `[Exterior]`

[ParseExteriorSection, L1220](../../../src/freelancer/Client/cli_nebula.cpp#L1220). Значения по умолчанию — те, что задаются при создании туманности (`FNebulaEffect::FNebulaEffect`, `Load`).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `bit_radius` | float | не задан | Радиус одного внешнего кусочка. | [L1225](../../../src/freelancer/Client/cli_nebula.cpp#L1225) |
| `color` | float x3 (/255) | не задан | Оттенок кусочков. | [L1229](../../../src/freelancer/Client/cli_nebula.cpp#L1229) |
| `shape` | str (всё значение) | - | Добавляет одну форму (имя из библиотеки форм). Хранится не более восьми; последующие ключи `shape` игнорируются. Отсутствующая форма даёт нулевую запись и предупреждение библиотеки. | [L1234](../../../src/freelancer/Client/cli_nebula.cpp#L1234) |
| `shape_weights` | float, по одному параметру на каждую прочитанную форму | все 1.0 | Относительный вес выбора каждой формы; должен идти после ключей `shape`. | [L1247](../../../src/freelancer/Client/cli_nebula.cpp#L1247) |
| `fill_shape` | str (всё значение) | нет | Форма, используемая для заполнения внутренней части. | [L1253](../../../src/freelancer/Client/cli_nebula.cpp#L1253) |
| `plane_slices` | float, усекается до int | 1 | Число срезов. | [L1259](../../../src/freelancer/Client/cli_nebula.cpp#L1259) |
| `min_bits` | float, усекается до int | 4 | Минимальное число кусочков. | [L1263](../../../src/freelancer/Client/cli_nebula.cpp#L1263) |
| `max_bits` | float, усекается до int | 8 | Максимальное число кусочков. | [L1267](../../../src/freelancer/Client/cli_nebula.cpp#L1267) |
| `bit_radius_random_variation` | float | 0 | Относительный случайный разброс размера; ограничивается диапазоном `0..0.9999` (хранится как `1-v` и `1+v`). | [L1271](../../../src/freelancer/Client/cli_nebula.cpp#L1271) |
| `equator_bias` | float | хранится 0 (эквивалентно `equator_bias = 1`) | Хранится как `1 - значение`. Не проверяется. | [L1285](../../../src/freelancer/Client/cli_nebula.cpp#L1285) |
| `move_bit_percent` | float | 0.75 | Доля движущихся кусочков. Не проверяется. | [L1291](../../../src/freelancer/Client/cli_nebula.cpp#L1291) |
| `opacity` | float | 0.75 | Непрозрачность внешней части (оба хранимых значения непрозрачности). | [L1295](../../../src/freelancer/Client/cli_nebula.cpp#L1295) |

### 4.3 `[Clouds]`

[LocalNebulaCloudsParseIni, L916](../../../src/freelancer/Client/cli_nebula.cpp#L916). Все значения по умолчанию применяются заново в начале каждой секции `[Clouds]`. Ключи-переключатели (`no_cloud_blend` ... `disable_batcher`) — float, усекаемые до int, 0 = выкл.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `puff_count` | float, усекается до int | 0 | Число спрайтов-клубов. | [L972](../../../src/freelancer/Client/cli_nebula.cpp#L972) |
| `puff_radius` | float | 50 | Радиус одного клуба. | [L976](../../../src/freelancer/Client/cli_nebula.cpp#L976) |
| `puff_colora`, `puff_colorb` | float x3 (/255) | не задан | Два цвета, между которыми смешиваются клубы. **Оба написания заполняют следующий свободный слот в порядке следования в файле**: первый прочитанный ключ становится цветом 1 (и также задаёт цвет 2), второй становится цветом 2, остальные игнорируются. | [L980](../../../src/freelancer/Client/cli_nebula.cpp#L980) |
| `puff_shape` | str (всё значение) | - | Добавляет одну форму клуба, не более восьми; последующие ключи игнорируются. Отсутствующая форма пишет в лог "Texture Shape (%s) not found for [Clouds] in file (%s)", и нулевая форма всё равно сохраняется. | [L1003](../../../src/freelancer/Client/cli_nebula.cpp#L1003) |
| `puff_weights` | float, по одному параметру на каждую прочитанную форму | все 1.0 | Веса выбора; после секции нормализуются до суммы 1. Должен идти после ключей `puff_shape`. | [L1026](../../../src/freelancer/Client/cli_nebula.cpp#L1026) |
| `puff_opacity` | (значение игнорируется) | - | Распознаётся, но игнорируется. | [L1031](../../../src/freelancer/Client/cli_nebula.cpp#L1031) |
| `puff_max_alpha` | float | 0.75 | Максимальная альфа клуба. | [L1035](../../../src/freelancer/Client/cli_nebula.cpp#L1035) |
| `puff_drift` | float | 0 | Диапазон скорости дрейфа. | [L1039](../../../src/freelancer/Client/cli_nebula.cpp#L1039) |
| `lightning_shape` | str[0..3] (четыре имени форм) | нулевые формы | Четыре формы для спрайта молнии, каждое имя до 31 символа; промах сохраняет null. | [L1043](../../../src/freelancer/Client/cli_nebula.cpp#L1043) |
| `lightning_color` | float x3 (/255) | не задан | Цвет молнии, встроенной в облака. | [L1064](../../../src/freelancer/Client/cli_nebula.cpp#L1064) |
| `lightning_gap` | float | не задан | Секунды между вспышками. | [L1068](../../../src/freelancer/Client/cli_nebula.cpp#L1068) |
| `lightning_duration` | float | 0 | Длительность вспышки. | [L1072](../../../src/freelancer/Client/cli_nebula.cpp#L1072) |
| `lightning_size` | float | 1.0 | Размер молнии. | [L1076](../../../src/freelancer/Client/cli_nebula.cpp#L1076) |
| `llight_range` | float | 0 | Дальность света, порождаемого вспышкой. | [L1080](../../../src/freelancer/Client/cli_nebula.cpp#L1080) |
| `llight_color` | float x3 (/255) | не задан | Цвет этого света. | [L1084](../../../src/freelancer/Client/cli_nebula.cpp#L1084) |
| `lightning_intensity` | float | 1.0 | Интенсивность вспышки. | [L1088](../../../src/freelancer/Client/cli_nebula.cpp#L1088) |
| `max_distance` | float | 1500 | Протяжённость объёма облаков; клубы размещаются в радиусе `max_distance/2 + puff_radius` вокруг центра слоя. | [L1092](../../../src/freelancer/Client/cli_nebula.cpp#L1092) |
| `near_fade_distance` | float x2 | `50, 100` | Начало и конец ближнего затухания. Если второе меньше, они меняются местами, и в лог пишется предупреждение "near_fade_distances are flipped". | [L1096](../../../src/freelancer/Client/cli_nebula.cpp#L1096) |
| `no_cloud_blend` | переключатель | выкл. | Отключить смешивание спрайтов облаков. | [L1112](../../../src/freelancer/Client/cli_nebula.cpp#L1112) |
| `zwrite_clouds` | переключатель | выкл. | Записывать спрайты облаков в буфер глубины. | [L1116](../../../src/freelancer/Client/cli_nebula.cpp#L1116) |
| `disable_clouds` | переключатель | выкл. | Не рисовать клубы. | [L1120](../../../src/freelancer/Client/cli_nebula.cpp#L1120) |
| `disable_wisps` | переключатель | не инициализируется загрузчиком | Не рисовать дымки. В отличие от других переключателей значение по умолчанию не назначается, поэтому задавайте его явно. | [L1124](../../../src/freelancer/Client/cli_nebula.cpp#L1124) |
| `disable_lightning_flash` | переключатель | выкл. | Не рисовать вспышку. | [L1128](../../../src/freelancer/Client/cli_nebula.cpp#L1128) |
| `disable_lightning_bolts` | переключатель | выкл. | Не рисовать разряды. | [L1132](../../../src/freelancer/Client/cli_nebula.cpp#L1132) |
| `disable_batcher` | переключатель | выкл. | Рисовать без пакетировщика материалов. | [L1136](../../../src/freelancer/Client/cli_nebula.cpp#L1136) |

### 4.4 `[Fog]`

[ParseFogSection, L1306](../../../src/freelancer/Client/cli_nebula.cpp#L1306).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `distance` | float | 10000 | Дальняя дистанция тумана. | [L1311](../../../src/freelancer/Client/cli_nebula.cpp#L1311) |
| `near` | float | 0 | Ближняя дистанция тумана. | [L1315](../../../src/freelancer/Client/cli_nebula.cpp#L1315) |
| `color` | float x3 (/255) | не задан | Цвет тумана; каждый канал ограничивается значением 1.0. | [L1319](../../../src/freelancer/Client/cli_nebula.cpp#L1319) |

### 4.5 `[Lightning]`

Молнии всей туманности ([LightningLoadIni, L1575](../../../src/freelancer/Client/cli_nebula.cpp#L1575)). Загрузчик не назначает значений по умолчанию: отсутствующие ключи сохраняют то, что хранил объект. Первая вспышка планируется со случайным отрицательным обратным отсчётом, масштабируемым `gap`.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `gap` | float | не задан | Секунды между вспышками. | [L1583](../../../src/freelancer/Client/cli_nebula.cpp#L1583) |
| `color` | float x3 (/255) | не задан | Цвет вспышки. | [L1585](../../../src/freelancer/Client/cli_nebula.cpp#L1585) |
| `size` | float | не задан | Размер. | [L1591](../../../src/freelancer/Client/cli_nebula.cpp#L1591) |
| `duration` | float | не задан | Длительность вспышки. | [L1593](../../../src/freelancer/Client/cli_nebula.cpp#L1593) |

### 4.6 `[BackgroundLightning]`

[L1332](../../../src/freelancer/Client/cli_nebula.cpp#L1332): вспыхивание неба позади туманности.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `gap` | float | 10 | Секунды между вспышками (рядом хранится значение с обратным знаком). | [L1337](../../../src/freelancer/Client/cli_nebula.cpp#L1337) |
| `duration` | float | 0.5 | Длительность вспышки. | [L1344](../../../src/freelancer/Client/cli_nebula.cpp#L1344) |
| `color` | float x3 (/255) | `0.25, 0.25, 0.25` | Цвет вспышки. | [L1348](../../../src/freelancer/Client/cli_nebula.cpp#L1348) |
| `intensity` | float | 1.0 | Интенсивность вспышки. | [L1352](../../../src/freelancer/Client/cli_nebula.cpp#L1352) |

### 4.7 `[DynamicLightning]`

[L1360](../../../src/freelancer/Client/cli_nebula.cpp#L1360): вспышки динамического освещения сцены. Если `duration` больше 0 (а по умолчанию 0.5), после загрузки для туманности создаётся белый направленный игровой свет.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `gap` | float | 3.0 | Секунды между вспышками. | [L1365](../../../src/freelancer/Client/cli_nebula.cpp#L1365) |
| `duration` | float | 0.5 | Длительность вспышки. | [L1367](../../../src/freelancer/Client/cli_nebula.cpp#L1367) |
| `color` | float x3 (/255) | `1, 1, 1` | Цвет света. | [L1369](../../../src/freelancer/Client/cli_nebula.cpp#L1369) |
| `ambient_intensity` | float | 0.25 | Вклад фонового света. | [L1371](../../../src/freelancer/Client/cli_nebula.cpp#L1371) |
| `intensity_increase` | float | 0.75 | Дополнительная интенсивность в пике вспышки. | [L1373](../../../src/freelancer/Client/cli_nebula.cpp#L1373) |

### 4.8 `[NebulaLight]`

[L1379](../../../src/freelancer/Client/cli_nebula.cpp#L1379).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `ambient` | byte x3 | не задан (флаг выкл.) | Фоновый цвет внутри туманности (0..255 на канал, хранится как байты). Указание ключа также включает переопределение. | [L1384](../../../src/freelancer/Client/cli_nebula.cpp#L1384) |
| `sun_burnthrough_intensity` | float | 0.5 | Насколько сильно солнце просвечивает сквозь туманность. | [L1393](../../../src/freelancer/Client/cli_nebula.cpp#L1393) |
| `sun_burnthrough_scaler` | float | 1.75 | Масштаб пробивания. | [L1397](../../../src/freelancer/Client/cli_nebula.cpp#L1397) |

### 4.9 `[Exclusion Zones]`

[ParseExclusionZonesSection, L1408](../../../src/freelancer/Client/cli_nebula.cpp#L1408). Заголовок этой секции пишется именно так (с пробелом). Ключи образуют небольшой конечный автомат: `exclusion` добавляет зону, а следующие за ним ключи (`fog_far`, `zone_shell`, затем три ключа оболочки) описывают эту зону. Эти ключи **игнорируются**, если в текущей секции не был принят ни один `exclusion`, а три ключа оболочки игнорируются, пока `zone_shell` не создал оболочку.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `exclusion` | str (всё значение) | - | Никнейм зоны этой системы. Неизвестная зона пишет в лог "AsteroidField linked to missing Exclusion Zone (%s)" (в тексте сказано AsteroidField; это читатель туманности) и пропускается. | [L1418](../../../src/freelancer/Client/cli_nebula.cpp#L1418) |
| `fog_far` | float | не задан | Дальняя дистанция тумана внутри этой зоны исключения. | [L1433](../../../src/freelancer/Client/cli_nebula.cpp#L1433) |
| `zone_shell` | str (всё значение) | нет | Имя меша (архетипа модели) оболочки, рисуемой вокруг зоны. Оболочка, уже загруженная для этой туманности, используется повторно; меш, который не удаётся загрузить, не оставляет оболочки. | [L1441](../../../src/freelancer/Client/cli_nebula.cpp#L1441) |
| `shell_scalar` | float | 1.0 | Масштаб оболочки; значения меньше 1.0 поднимаются до 1.0. | [L1491](../../../src/freelancer/Client/cli_nebula.cpp#L1491) |
| `max_alpha` | float | 1.0 | Максимальная альфа оболочки; значения больше 1.0 понижаются до 1.0. | [L1501](../../../src/freelancer/Client/cli_nebula.cpp#L1501) |
| `exclusion_tint` | float x3 (/255) | `1, 1, 1` | Оттенок оболочки; каждый канал ограничивается значением 1.0. | [L1511](../../../src/freelancer/Client/cli_nebula.cpp#L1511) |

Пример (синтетический):

```ini
[TexturePanels]
file = solar\nebula\demo_shapes.ini

[Exterior]
shape = puff_a
shape = puff_b
shape_weights = 2, 1
color = 120, 90, 160
opacity = 0.6

[Clouds]
puff_count = 40
puff_shape = puff_a
puff_colora = 140, 100, 180
puff_colorb = 100, 70, 150

[Fog]
distance = 6000
color = 40, 30, 60
```
