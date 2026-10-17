# Конфигурация клиента: раскладка клавиш, настройки, параметры производительности, запуск, шрифты, курсоры, камеры

На этой странице описаны INI-файлы, которые клиент Freelancer (`freelancer.exe`) читает для ввода, экранов настроек, параметров дисплея и производительности, запуска и входа, шрифтов, курсоров мыши и камер. Файлы, которые обычно редактируют конечные пользователи (`UserKeyMap.ini`, `PerfOptions.ini`, клиентская часть `Freelancer.ini`), описаны первыми и наиболее подробно; файлы элементов управления окнами, HUD и таблицы поиска — в [client-ui-config-windows.md](client-ui-config-windows.md), звук и прочие таблицы — в [client-audio-misc.md](client-audio-misc.md).

## Расположение файлов / как они находятся

Все файлы читаются через `INI_Reader` (грамматика и семантика геттеров — в [parsers.md](parsers.md)). Факты, справедливые для каждой таблицы ниже:

* Имена секций и ключей сравниваются **без учёта регистра** (DACOM `stricmp`), кроме случаев, когда в строке сказано иное (имена параметров в `PerfOptions.ini` сравниваются точным `strcmp`).
* В читателе нет механизма значений по умолчанию: столбец «По умолчанию» — это значение, которое целевая переменная хранит до чтения файла (статический инициализатор или конструктор). Отсутствующий ключ сохраняет это значение.
* «Тип» называет геттер, используемый в коде: *строка* = `get_value_string`, *целое* = `get_value_int` (в стиле `atoi`), *число* = `get_value_float` (принимает `0x..`, десятичные числа, дроби) и часто затем преобразуется в целое, *bool* = `get_value_bool` (`true`/`1`/`false`/`0`; всё остальное пишет ошибку в журнал и читается как false). «Флаг» означает, что код проверяет `get_value_float(0) != 0`, то есть ложью являются только числовой `0` (или пустое значение); слова `true`/`false` ключами-флагами **не** понимаются.
* Если код не проверяет значение, в столбце «Значение» указано *не проверяется*.
* Текстовые файлы: строка, первый непробельный символ которой `;`, — комментарий; завершающий `; text` отбрасывается (поэтому значение не может содержать `;`); `[` в начале строки начинает секцию; пустые строки пропускаются. Файлы, поставляемые в `DATA\`, — бинарные `BINI`; читатель принимает то же содержимое как текст, поэтому текстовый файл с теми же секциями и ключами работает идентично.
* Пути: **путь Data** — ключ `data path` секции `[Freelancer]` в `Freelancer.ini` (по умолчанию `..\Data`, см. [раздел 4](#4-клиентские-ключи-freelancerini)); «относительно Data» означает, что путь присоединяется к нему через `GetFileSysPath` ([common_misc.cpp:935](../../../src/Common/common_misc.cpp#L935)). **Каталог пользователя** — `<My Documents>\My Games\Freelancer`, создаётся по требованию через `GetUserDataPath` ([common_misc.cpp:1000](../../../src/Common/common_misc.cpp#L1000)).

| Файл | Расположение | Читается | Секция |
|---|---|---|---|
| `Freelancer.ini` | текущий каталог | [startup_config.cpp](../../../src/freelancer/Client/startup_config.cpp), [login_mgr.cpp](../../../src/freelancer/Client/login_mgr.cpp), [dataconfig_progress.cpp](../../../src/freelancer/Client/dataconfig_progress.cpp) | 4 |
| `interface\KeyMap.ini` | относительно Data | `LoadKeyMapIni` | 1 |
| `UserKeyMap.ini` | каталог пользователя | `LoadUserKeyMapIni` / `SaveUserKeyMapIni` | 1 |
| `../Data/interface/keylist.ini`, `../Data/interface/optlist.ini` | относительно **текущего каталога** | `NnPreferences` | 2 |
| `PerfOptions.ini` | каталог пользователя | `LoadPerfOptions` / `SavePerfOptions` | 3 |
| файлы, названные в `[Data]` `fonts`, `rich_fonts` | относительно Data | `LoadFonts`, `LoadRichFontsFromIni` | 5 |
| `mouse.ini` | относительно Data | `MouseInitCursors` | 6 |
| `cameras.ini`, `.ini` кабины каждого корабля | относительно Data | `SpaceAppLoadCamerasIni`, четыре загрузчика камер | 7 |
| файл `[Data] intro` | относительно Data | `LoadIntroConfigFromIni` | 8 |

---

## 1. Раскладка клавиш

Раскладка клавиш — это два файла одного формата: базовая раскладка `interface\KeyMap.ini` (поставляется, определяет каждую команду и её клавиши по умолчанию) и пользовательская раскладка `UserKeyMap.ini` (записывается страницей клавиш в игре, заменяет привязки клавиш базовой раскладки). Источники: [menu_keymap.cpp](../../../src/freelancer/Client/menu_keymap.cpp), константы в [menu_keymap_config.h](../../../src/freelancer/Client/menu_keymap_config.h), таблица имён [menu_keymap_nicknames.inc](../../../src/freelancer/Client/menu_keymap_nicknames.inc).

Порядок загрузки ([menu_keymap.cpp:530](../../../src/freelancer/Client/menu_keymap.cpp#L530)): при первом запросе группы акселераторов загружается базовая раскладка с `bFatal = true` (отсутствие базовой раскладки — фатальная ошибка; это единственный читатель в клиенте, передающий `bFatal = true`), затем поверх неё загружается пользовательская. Имена групп, которые запрашивает код: `IDR_ALWAYS_PRESENT`, `IDR_COCKPIT`, `IDR_SPACE` (строковые литералы в `main.cpp`, `space_camera_game.cpp`, `space_state.cpp`).

### 1.1 Строка заголовка

**Первый заголовок секции** обоих файлов должен быть ровно `[KeyMap=1.1]` (версия входит в имя секции).

* Базовая раскладка: отсутствующий или иной первый заголовок пишет в журнал `KeyMap file (...) is not version 1.1`, и загрузка **продолжается** ([menu_keymap.cpp:251](../../../src/freelancer/Client/menu_keymap.cpp#L251)).
* Пользовательская раскладка: без точного первого заголовка файл игнорируется (загрузка возвращает 0, ничего не меняется) ([menu_keymap.cpp:314](../../../src/freelancer/Client/menu_keymap.cpp#L314)).

### 1.2 `[KeyCmd]` (повторяющаяся секция)

Одна команда с её привязками клавиш. Каждая секция `[KeyCmd]` базовой раскладки добавляется; секции никогда не объединяются.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | строка | нет | Имя команды, одно из 204 имён [таблицы nickname](#13-таблица-nickname) (без учёта регистра, побеждает первое совпадение). Имя, которого нет в таблице, разрешается в `USER_NONE`, и **вся секция отбрасывается**. Секция без `nickname` также отбрасывается. | [menu_keymap.cpp:149](../../../src/freelancer/Client/menu_keymap.cpp#L149) |
| `ids_name` | число → целое | `0` | Идентификатор строкового ресурса подписи команды на странице клавиш. `0` = подписи нет (строка тогда показывает пустой заголовок). Не проверяется. | [menu_keymap.cpp:156](../../../src/freelancer/Client/menu_keymap.cpp#L156) |
| `ids_info` | число → целое | `0` | Идентификатор строкового ресурса справочного текста. Не проверяется. | [menu_keymap.cpp:161](../../../src/freelancer/Client/menu_keymap.cpp#L161) |
| `state` | список слов через запятую | `keydown` | Какие события клавиши запускают команду. Слова (без учёта регистра): `keydown`, `keyup`, `repeat`, `shift_ignore`. Они объединяются через OR; неизвестные слова ничего не добавляют; пустое значение или нулевой результат означает `keydown`. `repeat` необходим, чтобы срабатывали нажатия с автоповтором; `shift_ignore` заставляет команду срабатывать при любых удерживаемых модификаторах. | [menu_keymap.cpp:165](../../../src/freelancer/Client/menu_keymap.cpp#L165), [menu_keymap.cpp:209](../../../src/freelancer/Client/menu_keymap.cpp#L209) |
| `key` | **повторяется**; см. синтаксис клавиши ниже | нет | Добавляет команде одну привязку в порядке следования в файле. Привязки, которые не удалось разобрать, молча пропускаются. Команда может содержать из файла любое число привязок; страница клавиш в игре показывает первые две (основную и альтернативную) и отказывается добавлять третью. | [menu_keymap.cpp:192](../../../src/freelancer/Client/menu_keymap.cpp#L192) |

Команды с одним и тем же nickname могут встречаться несколько раз; поиск по nickname использует **первую**.

#### Синтаксис значения key (`key = <code>[, <modifier>]`)

Разбирается `ParseKeyValue` ([menu_keymap.cpp:87](../../../src/freelancer/Client/menu_keymap.cpp#L87)); тот же разборщик используется для пользовательской раскладки.

| Форма первого параметра | Результат |
|---|---|
| `"c` (двойная кавычка и один символ) | код виртуальной клавиши = код второго символа, **не** приводится к верхнему регистру (поэтому `"a` — это код 97, а не `A`) |
| начинается с `0` (`0x41`, `041`, `0`) | разбирается как **шестнадцатеричное** через `sscanf("%X")`; ошибка разбора отбрасывает привязку |
| всё остальное | десятичное целое (`MsvcrtAtoi`); значение вне `-8 ... 255` только пишет в журнал `invalid KeyMap key value` — привязка всё равно сохраняется |

Коды `0 ... 255` — коды виртуальных клавиш Windows. Отрицательные коды — события мыши: `-1` ... `-8` = кнопка мыши с индексом 0 ... 7, `-9` = щелчок колеса мыши ([input_manager.cpp:224](../../../src/freelancer/Client/input_manager.cpp#L224), [menu_keymap.cpp:757](../../../src/freelancer/Client/menu_keymap.cpp#L757)).

Второй параметр (необязательный): слово-модификатор. Он сопоставляется по **префиксу**, без учёта регистра: `shift`, `control`, `alt`. Каждое распознанное слово *заменяет* предыдущий модификатор (на привязку остаётся только один модификатор; побеждает последний). Любое другое слово (например, `ctrl`) делает всю привязку недействительной, и она отбрасывается. Внутренние маски модификаторов: Shift = 1, Control = 4, Alt = 0x10; привязка срабатывает, только когда текущее состояние модификаторов в точности равно её маске (если у команды нет `shift_ignore`).

### 1.3 Таблица nickname

Значения `[KeyCmd] nickname` (индекс = внутренний идентификатор; `USER_NONE` = 0 означает «неизвестно»):

```
USER_NONE, USER_KEY, USER_CHAR, USER_BUTTON, USER_WHEEL, USER_MOVE, USER_BUTTON0, USER_BUTTON1, USER_BUTTON2,
USER_BUTTON3, USER_BUTTON4, USER_BUTTON5, USER_BUTTON6, USER_BUTTON7, USER_EVENTS, USER_CANCEL,
USER_STACKABLE_CANCEL, USER_ENTER, USER_NO, USER_YES, USER_PAUSE, USER_RESET, USER_SAVE_GAME,
USER_FULLSCREEN, USER_USE_ITEM, USER_ACTIVATE_ITEM, USER_NEXT_ITEM, USER_PREV_ITEM, USER_SET_TARGET,
USER_NEXT_TARGET, USER_PREV_TARGET, USER_NEXT_WEAPON_SUBTARGET, USER_PREV_WEAPON_SUBTARGET,
USER_NEXT_SUBTARGET, USER_PREV_SUBTARGET, USER_CLEAR_TARGET, USER_NEXT_ENEMY, USER_PREV_ENEMY,
USER_CLOSEST_ENEMY, USER_ZOOM_IN, USER_ZOOM_OUT, USER_ZOOM_TOGGLE, USER_X_ROTATE, USER_X_UNROTATE,
USER_Y_ROTATE, USER_Y_UNROTATE, USER_Z_ROTATE, USER_Z_UNROTATE, USER_VIEW_RESET, USER_COCKPIT_CAMERA_MODE,
USER_LOOK_ROTATE_CAMERA_LEFT, USER_LOOK_ROTATE_CAMERA_RIGHT, USER_LOOK_ROTATE_CAMERA_UP,
USER_LOOK_ROTATE_CAMERA_DOWN, USER_LOOK_ROTATE_CAMERA_RESET, USER_CHASE_CAMERA_MODE,
USER_REARVIEW_CAMERA_MODE, USER_REARVIEW_CAMERA_MODE_OFF, USER_AFTERBURN, USER_AFTERBURN_OFF,
USER_AFTERBURN_ON, USER_TOGGLE_AUTO_AVOIDANCE, USER_TOGGLE_AUTO_LEVEL, USER_TOGGLE_LEVEL_CAMERA, USER_WARP,
USER_ENGINE_TOGGLE, USER_SET_THROTTLE, USER_INC_THROTTLE, USER_DEC_THROTTLE, USER_CRUISE, USER_THROTTLE_0,
USER_THROTTLE_10, USER_THROTTLE_20, USER_THROTTLE_25, USER_THROTTLE_30, USER_THROTTLE_40, USER_THROTTLE_50,
USER_THROTTLE_60, USER_THROTTLE_70, USER_THROTTLE_75, USER_THROTTLE_80, USER_THROTTLE_90, USER_THROTTLE_100,
USER_RADAR, USER_CONTACT_LIST, USER_RADAR_ZOOM, USER_SWITCH_TO_PLAYER_SHIP, USER_SWITCH_TO_WEAPON_LIST,
USER_SWITCH_TO_TARGET, USER_FORMATION_LIST, USER_RADAR_ZOOM_IN, USER_RADAR_ZOOM_OUT, USER_RADIO, USER_CHAT,
USER_STATUS, USER_TARGET, USER_STATUS_MODE, USER_TOGGLE_MAINFRAME, USER_NN, USER_NAV_MAP, USER_NAVMAP,
USER_PLAYER_STATS, USER_INVENTORY, USER_STORY_STAR, USER_CHAT_WINDOW, USER_GROUP_WINDOW, USER_HELP,
USER_INVENTORY_CLOSE, NN_TOGGLE_OPEN, USER_MINIMIZE_HUD, USER_DISPLAY_LAST_OBJECTIVE, USER_COLLECT_LOOT,
USER_ACTIONS, USER_MANEUVER, USER_SET_MANEUVER_DIRECTION, USER_MANEUVER_WINDOW, USER_MANEUVER_TRAIL,
USER_MANEUVER_AFTERBURNER, USER_MANEUVER_EVADE, USER_MANEUVER_ENGINEKILL, USER_MANEUVER_BRAKE_REVERSE,
USER_MANEUVER_DOCK, USER_MANEUVER_GOTO, USER_MANEUVER_FACE, USER_MANEUVER_CRUISE, USER_MANEUVER_TRADE_LANE,
USER_MANEUVER_DRASTIC_EVADE, USER_MANEUVER_FORMATION, USER_MANEUVER_STRAFE, USER_MANEUVER_TRAIL_CLOSER,
USER_MANEUVER_TRAIL_FARTHER, USER_MANEUVER_CORKSCREW_EVADE, USER_MANEUVER_SLIDE_EVADE,
USER_MANEUVER_SLIDE_EVADE_LEFT, USER_MANEUVER_SLIDE_EVADE_RIGHT, USER_MANEUVER_SLIDE_EVADE_UP,
USER_MANEUVER_SLIDE_EVADE_DOWN, USER_ACTIVATE_MANEUVER, USER_SCAN_CARGO, USER_TRACTOR_BEAM, USER_JAMMER,
USER_STEALTH, USER_CLOAK, USER_REPAIR_HEALTH, USER_REPAIR_SHIELD, USER_WEAPON_GROUP1, USER_WEAPON_GROUP2,
USER_WEAPON_GROUP3, USER_WEAPON_GROUP4, USER_WEAPON_GROUP5, USER_WEAPON_GROUP6, USER_TOGGLE_WEAPON1,
USER_TOGGLE_WEAPON2, USER_TOGGLE_WEAPON3, USER_TOGGLE_WEAPON4, USER_TOGGLE_WEAPON5, USER_TOGGLE_WEAPON6,
USER_TOGGLE_WEAPON7, USER_TOGGLE_WEAPON8, USER_TOGGLE_WEAPON9, USER_TOGGLE_WEAPON10, USER_FIRE_WEAPON1,
USER_FIRE_WEAPON2, USER_FIRE_WEAPON3, USER_FIRE_WEAPON4, USER_FIRE_WEAPON5, USER_FIRE_WEAPON6,
USER_FIRE_WEAPON7, USER_FIRE_WEAPON8, USER_FIRE_WEAPON9, USER_FIRE_WEAPON10, USER_FIRE_WEAPON_GROUP1,
USER_FIRE_WEAPON_GROUP2, USER_FIRE_WEAPON_GROUP3, USER_FIRE_WEAPON_GROUP4, USER_FIRE_WEAPON_GROUP5,
USER_FIRE_WEAPON_GROUP6, USER_ASSIGN_WEAPON_GROUP1, USER_ASSIGN_WEAPON_GROUP2, USER_ASSIGN_WEAPON_GROUP3,
USER_ASSIGN_WEAPON_GROUP4, USER_ASSIGN_WEAPON_GROUP5, USER_ASSIGN_WEAPON_GROUP6, USER_REMAPPABLE_LEFT,
USER_REMAPPABLE_RIGHT, USER_REMAPPABLE_UP, USER_REMAPPABLE_DOWN, USER_FIRE_FORWARD, USER_LAUNCH_MISSILES,
USER_LAUNCH_MINES, USER_LAUNCH_COUNTERMEASURES, USER_AUTO_TURRET, USER_LAUNCH_TORPEDOS,
USER_LAUNCH_CRUISE_DISRUPTORS, USER_TURN, USER_NEXT_OBJECT, USER_PREV_OBJECT, USER_EXIT_GAME,
USER_MANEUVER_FREE_FLIGHT, USER_FIRE_WEAPONS, USER_TURN_SHIP, USER_GROUP_INVITE, USER_TRADE_REQUEST,
USER_SCREEN_SHOT
```

### 1.4 `[KeyMap]` (повторяющаяся секция, только базовая раскладка)

Определяет *группу акселераторов*: именованный набор команд, который включается и выключается как единое целое (имена групп см. в абзаце о загрузке). В пользовательской раскладке секций `[KeyMap]` нет (любые встретившиеся игнорируются).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | строка (всё значение) | пусто | Имя группы, код ищет его без учёта регистра. Используется первая группа с совпадающим именем. | [menu_keymap.cpp:283](../../../src/freelancer/Client/menu_keymap.cpp#L283) |
| `key` | **повторяется**, строка | нет | **nickname из `[KeyCmd]`, а не код клавиши.** Добавляет в группу эту команду (первую `[KeyCmd]` с этим nickname); неизвестные имена игнорируются. Группа, зарегистрированная позже, опрашивается раньше более старых. | [menu_keymap.cpp:288](../../../src/freelancer/Client/menu_keymap.cpp#L288) |

### 1.5 `UserKeyMap.ini` (каталог пользователя)

Записывается `SaveUserKeyMapIni` ([menu_keymap.cpp:352](../../../src/freelancer/Client/menu_keymap.cpp#L352)), когда игрок подтверждает страницу клавиш; имя файла — `<user directory>/UserKeyMap.ini` ([menu_keymap.cpp:404](../../../src/freelancer/Client/menu_keymap.cpp#L404)). Раскладка, которую создаёт игра:

```ini
; Freelancer - User Key Commands

[KeyMap=1.1]

[KeyCmd]
nickname = USER_FULLSCREEN
key = 13,ALT

[KeyCmd]
nickname = USER_PAUSE
key = 19
```

Записываются только команды, у которых остаётся хотя бы одна привязка, а `key` всегда записывается десятичным кодом с необязательным суффиксом `,SHIFT`, `,CONTROL` или `,ALT`. Когда игра читает файл обратно (`LoadUserKeyMapIni`, [menu_keymap.cpp:303](../../../src/freelancer/Client/menu_keymap.cpp#L303)):

| Ключ | Тип | Значение | Источник |
|---|---|---|---|
| `[KeyCmd]` / `nickname` | строка | Выбирает существующую команду с этим nickname (первое совпадение). Если имя неизвестно, команда не выбирается, и строки `key` этой секции игнорируются. Более поздний `nickname` в той же секции выбирает заново. | [menu_keymap.cpp:333](../../../src/freelancer/Client/menu_keymap.cpp#L333) |
| `[KeyCmd]` / `key` (повторяется) | см. синтаксис клавиши | Добавляется к выбранной команде. `ids_name`, `ids_info` и `state` из пользовательской раскладки **не читаются**. | [menu_keymap.cpp:337](../../../src/freelancer/Client/menu_keymap.cpp#L337) |

Важное поведение: как только первый заголовок — корректный `[KeyMap=1.1]`, **привязки каждой команды из базовой раскладки стираются** до применения пользовательских секций. Поэтому написанный вручную пользовательский файл заменяет всю раскладку: любая команда, не перечисленная в нём, после этого остаётся без клавиши. Файл с заголовком, но без секции `[KeyCmd]` снимает все привязки. Отсутствующий файл (или неверный первый заголовок) ничего не меняет. На странице клавиш *По умолчанию* перезагружает базовую раскладку так, как если бы она была пользовательской, а *Отмена* перезагружает пользовательский файл, возвращаясь к базовой раскладке, если в пользовательском файле нет секции `[KeyCmd]` ([nn_preferences.cpp:698](../../../src/freelancer/Client/nn_preferences.cpp#L698)).

Клавиши, которые страница клавиш в игре отказывается привязывать (коды виртуальных клавиш `0x10` Shift, `0x11` Ctrl, `0x12` Alt, `0x1B` Esc, `0x70` F1, `0x5B`/`0x5C` клавиши Windows, а также `-1`, кнопка мыши с индексом 0), фильтруются только интерфейсом; файл, отредактированный вручную, не проверяется ([nn_preferences.cpp:66](../../../src/freelancer/Client/nn_preferences.cpp#L66)).

---

## 2. Описание экрана настроек: `keylist.ini` и `optlist.ini`

Оба файла описывают строки экрана настроек в игре (класс `NN_Preferences`). Оба пути жёстко заданы как `../Data/interface/keylist.ini` и `../Data/interface/optlist.ini` относительно **текущего каталога** — они не следуют `data path` ([nn_preferences.cpp:68](../../../src/freelancer/Client/nn_preferences.cpp#L68), [nn_preferences.cpp:2044](../../../src/freelancer/Client/nn_preferences.cpp#L2044)). Файл, который не удалось открыть, пишет в журнал `Could not open` и оставляет страницу пустой.

### 2.1 `keylist.ini`

Читается `NnPreferences::LoadKeyList` ([nn_preferences.cpp:1577](../../../src/freelancer/Client/nn_preferences.cpp#L1577)): три списка строк очищаются, затем читается файл.

| Секция | Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `[Group]` | `group_num` | целое | `-1` | Слот группы, которой принадлежат следующие строки. Код предполагает `0 ... 2`; не проверяется. Группа `1` сначала получает фиксированную строку-заголовок. | [nn_preferences.cpp:1598](../../../src/freelancer/Client/nn_preferences.cpp#L1598) |
| `[Group]` | `name` | целое | нет | Идентификатор строкового ресурса заголовка вкладки группы, сохраняется в слоте, заданном `group_num` (поэтому `group_num` должен идти первым). | [nn_preferences.cpp:1600](../../../src/freelancer/Client/nn_preferences.cpp#L1600) |
| `[Key]` (повторяется, по одной строке) | `id` | строка | `-1` | nickname из `[KeyCmd]` раскладки клавиш или текст `-1` для пустой строки-разделителя. Имя, не найденное в раскладке клавиш, оставляет строку пустой. Строка добавляется в группу, заданную последней `[Group]`. | [nn_preferences.cpp:1623](../../../src/freelancer/Client/nn_preferences.cpp#L1623) |
| `[Key]` | `name` | целое | нет | Читается и отбрасывается (подпись берётся из `ids_name` раскладки клавиш). | [nn_preferences.cpp:1637](../../../src/freelancer/Client/nn_preferences.cpp#L1637) |

Строки добавляются, только когда номер группы равен `0 ... 3`. Строка команды с внутренним идентификатором `0x14` (`USER_PAUSE`) показывается нередактируемой, когда у неё есть подпись (см. `AddKeyRow`, [nn_preferences.cpp:1648](../../../src/freelancer/Client/nn_preferences.cpp#L1648)).

### 2.2 `optlist.ini`

Читается `NnPreferences::LoadOptList` ([nn_preferences.cpp:1886](../../../src/freelancer/Client/nn_preferences.cpp#L1886)); при первом запуске также читается один раз в «режиме посева» для вычисления настроек производительности по умолчанию (`SeedGraphicsDefaults`, [nn_preferences.cpp:2102](../../../src/freelancer/Client/nn_preferences.cpp#L2102)). Три секции, каждая — список строк `option`:

| Секция | Страница |
|---|---|
| `[Interface]` | Страница интерфейса |
| `[Video]` | Страница видео |
| `[Audio]` | Страница звука |

Каждая секция дополняется пустыми строками до 15 строк, если в ней 14 или меньше строк `option`; страница показывает 14 строк. Ключи, отличные от `option`, игнорируются.

`option` (повторяется, по одной строке) принимает **позиционные** параметры ([nn_preferences.cpp:1822](../../../src/freelancer/Client/nn_preferences.cpp#L1822)):

| Параметр | Тип | По умолчанию | Значение / допустимые значения |
|---|---|---|---|
| 0 | целое | `-1` | Идентификатор строкового ресурса подписи строки. Одновременно служит идентичностью параметра: сопоставляется со столбцом *IDS* [таблицы параметров производительности](#32-ключи-параметров-производительности), чтобы найти настройку, которой управляет строка. Два идентификатора особые: `0x536` (выбор разрешения экрана) и `0x767` (ползунок графической предустановки). |
| 1 | целое | `-1` | Идентификатор строкового ресурса текста подсказки при наведении. |
| 2 | целое | `-1` | Значение по умолчанию в процентах (`0 ... 100`; `-1` = нет). При посеве соответствующий параметр производительности получает значение `Default * 0.01`. Не проверяется. |
| 3 | bool | `0` | `1` = ползунок, `0` = переключатель вкл/выкл. |
| 4 | bool | `0` | `1` = строка серая, пока экран настроек открыт в полёте («нельзя менять в игре»). |
| 5 | целое | `-1` | Нижняя граница диапазона предустановки (используется только для строк, управляемых графической предустановкой, идентификатор `0x767`). |
| 6 | целое | `-1` | Верхняя граница диапазона предустановки. |

Строка с **единственным** параметром — строка-заголовок секции (только подпись). Параметры 5 и 6 читаются, только когда в строке больше пяти параметров. Поведение предустановки ([nn_preferences.cpp:2192](../../../src/freelancer/Client/nn_preferences.cpp#L2192)): для строки с диапазоном предустановки ползунок устанавливается в `0`, когда значение предустановки `<= min`, в `100`, когда `>= max`, и линейно между ними; переключатель включается, когда `min <= value <= max`.

---

## 3. `PerfOptions.ini` (каталог пользователя)

Хранит настройки игрока для графики, звука и игрового комфорта. Читается один раз при запуске движка (`LoadPerfOptions`, [perf_options.cpp:494](../../../src/freelancer/Client/perf_options.cpp#L494), вызывается из [engine_init.cpp:748](../../../src/freelancer/Client/engine_init.cpp#L748)) и перезаписывается `SavePerfOptions` ([perf_options.cpp:607](../../../src/freelancer/Client/perf_options.cpp#L607)) при запуске и всякий раз, когда экран настроек закрывается или подтверждается. Путь: `<user directory>\PerfOptions.ini`.

Файлу доверяют только целиком. Он отвергается — все параметры возвращаются к значениям по умолчанию, и снова выполняется посев первого запуска (значения по умолчанию из `optlist.ini` плюс проверка оборудования), — когда:

* `[PerfVersion] version` отсутствует или `< 5.0`;
* распознанное значение `[PerfOptions]` отрицательно;
* `[PerfCount] count` не равен числу **распознанных** ключей, найденных в `[PerfOptions]`;
* файл не удалось открыть.

Порядок секций важен: `[PerfVersion]` должна идти **до** `[PerfOptions]` (иначе версия всё ещё не задана, секция `[PerfOptions]` останавливает просмотр, и файл отвергается), а `[PerfCount]` — в любом месте до конца файла. Пример (синтетический):

```ini
[PerfVersion]
version=5
SkipMachineWarnings=FALSE

[PerfCount]
count=36

[PerfOptions]
mouse_speed = 0.28
audio_music = 0.80
; ... all 36 options ...

[Display]
size= 1024, 768
color_bpp= 32
depth_bpp= 24
```

### 3.1 Секции

| Секция | Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `[PerfVersion]` | `version` | число | не задано (= отвергается) | Версия формата файла; принимается при `>= 5`. Игра записывает `5`. | [perf_options.cpp:525](../../../src/freelancer/Client/perf_options.cpp#L525) |
| `[PerfVersion]` | `SkipMachineWarnings` | bool | `false` | `true`/`1`/`false`/`0`. `true` подавляет диалог предупреждения о низкой спецификации машины при запуске. Игра записывает `TRUE`/`FALSE`. | [perf_options.cpp:527](../../../src/freelancer/Client/perf_options.cpp#L527) |
| `[PerfCount]` | `count` | число → целое | `0` | Должен равняться числу распознанных ключей параметров в `[PerfOptions]` (игра записывает 36). | [perf_options.cpp:540](../../../src/freelancer/Client/perf_options.cpp#L540) |
| `[PerfOptions]` | по одному ключу на параметр (ниже) | число | `0` до применения | Одно число с плавающей точкой на параметр, обычно `0.0 ... 1.0`. **Имена параметров сопоставляются с учётом регистра** и точно. Неизвестные имена игнорируются и не учитываются. Отрицательное значение отвергает файл. Повторяющиеся ключи: каждое распознанное вхождение учитывается в `count`; побеждает последнее значение. Верхние границы не проверяются. | [perf_options.cpp:558](../../../src/freelancer/Client/perf_options.cpp#L558) |
| `[Display]` | `size` | целое, целое | `0, 0` (авто) | Ширина и высота окна/экрана в пикселях, два параметра через запятую. | [perf_options.cpp:581](../../../src/freelancer/Client/perf_options.cpp#L581) |
| `[Display]` | `color_bpp` | целое | `-1` | Глубина цвета в битах (игра записывает `16`/`32`); `-1` = автоматически. Не проверяется. | [perf_options.cpp:586](../../../src/freelancer/Client/perf_options.cpp#L586) |
| `[Display]` | `depth_bpp` | целое | `-1` | Биты буфера глубины; `-1` = автоматически. Не проверяется. | [perf_options.cpp:588](../../../src/freelancer/Client/perf_options.cpp#L588) |

Значения по умолчанию `[Display]` — это значения, которые `InitializeEngine` устанавливает перед загрузкой ([engine_init.cpp:731](../../../src/freelancer/Client/engine_init.cpp#L731)). Переключатели дисплея командной строки применяются после файла ([startup_config.cpp:195](../../../src/freelancer/Client/startup_config.cpp#L195)).

### 3.2 Ключи параметров производительности

Столбец `IDS` — идентификатор строкового ресурса, по которому строки `optlist.ini` ссылаются на параметр (`0` = строки на экране настроек нет). «Применяется» описывает, что значение делает в реализации; реестр — [perf_options.cpp:298](../../../src/freelancer/Client/perf_options.cpp#L298). Записывается обратно только для параметров, которые были применены хотя бы раз; записывается с двумя десятичными знаками (`name = 0.50`).

| Ключ | IDS | Применяется / значение |
|---|---|---|
| `mouse_speed` | 0x5dd | Ограничивается до `0 ... 1`, множитель скорости мыши `v * 1.75 + 0.25` ([input_manager.cpp:1209](../../../src/freelancer/Client/input_manager.cpp#L1209)) |
| `gamma_ramp` | 0x534 | Значения `>= 0`: гамма рендера = `v + 1.0`; отрицательные значения ничего не делают ([engine_init.cpp:496](../../../src/freelancer/Client/engine_init.cpp#L496)) |
| `DitherControl` | 0x67f | Дизеринг включён при `v >= 0.3` ([engine_init.cpp:507](../../../src/freelancer/Client/engine_init.cpp#L507)) |
| `MipFilterControlCallback` | – | Устанавливает флаг mip-фильтра в 1 при любом значении ([engine_init.cpp:516](../../../src/freelancer/Client/engine_init.cpp#L516)) |
| `TurnSensitivity` | 0x614 | Передаётся менеджеру поведения корабля игрока (`set_turn_sensitivity`) ([perf_options.cpp:169](../../../src/freelancer/Client/perf_options.cpp#L169)) |
| `generic_effect_perf` | 0x5cb | Сохраняется как `1.0 - v`; рисуются типы эффектов, у которых `priority` `>=` этого уровня (см. `[EffectType]` в [client-audio-misc.md](client-audio-misc.md#4-типы-эффектов-и-lod-effecttype-effectlod)) |
| `Nebula` | 0x57d | Глобальная детализация туманностей (`NebulaApplyGlobalDetail`, [cli_nebula.cpp](../../../src/freelancer/Client/cli_nebula.cpp)) |
| `SunEffect` | 0x5ca | `min(1, v * 1.33333)` масштабирует детализацию тела солнца каждой звезды ([perf_options.cpp:127](../../../src/freelancer/Client/perf_options.cpp#L127)) |
| `Asteroids` | 0x57c | Глобальная детализация астероидных полей (`CliAsteroidApplyGlobalDetail`, [cli_asteroid.cpp](../../../src/freelancer/Client/cli_asteroid.cpp)) |
| `space_dust_detail` | 0x57a | Сохраняется без изменений (по умолчанию `1.0`) как детализация космической пыли |
| `star_sphere` | 0x57b | Детализация звёздной сферы (`StarSphereApplyDetail`, [star_sphere.cpp](../../../src/freelancer/Client/star_sphere.cpp)) |
| `AtmosphereMaterialConfig` | 0x5c9 | Глобальная запись 1 библиотеки материалов включается при `v > 0.999` |
| `HUDAnimConfig` | 0x65e | Глобальная запись 4 библиотеки материалов включается при `v > 0.999` |
| `level_of_detail` | 0x5d5 | `v < 0.75` выбирает «низкий» режим; масштаб LOD равен `v * 2/3 + 0.5` (низкий) или `(v - 0.75) * 4 + 1` ([perf_options.cpp:252](../../../src/freelancer/Client/perf_options.cpp#L252)) |
| `use_environment_maps` | 0x657 | Сохраняется, только если `v > 0` **и** видеоустройство сообщает о поддержке карт окружения; иначе принудительно `0` |
| `high_detail_textures` | 0x658 | Пороги по `v`: `> 0.6` (и достаточно видеопамяти) текстуры высокой детализации, `> 0.5` состояние конвейера 4, `> 0.4` / `> 0.3` записи 0 / 2 библиотеки материалов ([perf_options.cpp:201](../../../src/freelancer/Client/perf_options.cpp#L201)) |
| `sound_max_dist_scale` | 0x77a | Сохраняется (слот масштаба дистанции звука) |
| `sound_level_of_detail` | 0x77b | Уровень LOD звука `floor(2 * (1 - v))` |
| `USE_3D_SOUNDS` | 0x77e | Сохраняется (слот 3D-звука) |
| `audio_music` | 0x539 | Громкость музыки (`SetClientAudioMusicVolume`) |
| `audio_ambient` | 0x584 | Громкость фона |
| `audio_sfx` | 0x538 | Громкость эффектов |
| `audio_interface` | 0x583 | Громкость звуков интерфейса |
| `audio_voice` | 0x581 | Громкость голоса |
| `rtc_performance` | 0x656 | Передаётся в `RtcSlider::set_slider` (производительность катсцен реального времени) |
| `general_graphics` | – | Сохраняется как целое (`ftol`); следует за ползунком графической предустановки |
| `advanced_graphics` | – | Флаг: `v != 0` |
| `default_graphics` | – | Сохраняется как целое (`ftol`); результат проверки машины (см. ниже) |
| `skip_transition_scripts` | 0x5d7 | Фиксирует пропуск последовательностей скриптов перехода |
| `enable_tooltips` | 0x578 | `v > 0.5` (по умолчанию включено) |
| `enable_rollover` | 0x579 | `v > 0.5` (по умолчанию включено) |
| `cockpit` | 0x5da | `v > 0.5` показывает вид из кабины |
| `USE_DYNAMIC_COMM_HEAD` | 0x631 | Сохраняется (слот анимированной comm-head) |
| `DIFFICULTY_SCALE` | 0x57f | Сохраняется (слот сложности) |
| `autohide_maneuver_bar` | 0x613 | `v > 0.5` |
| `max_submitted_sounds` | 0x687 | Бюджет голосов `int(v * 100 + 0.5)` (по умолчанию 32) |

Посев первого запуска: `default_graphics` устанавливается из `ProbeMachineGraphicsLevel` ([perf_options.cpp:353](../../../src/freelancer/Client/perf_options.cpp#L353)) — оценки из `{0.25, 0.5, 0.6, 0.75, 1.0}`, выводимой из поддержки аппаратной трансформации и освещения, объёма видеопамяти и скорости CPU; остальные параметры затем следуют из процентов `optlist.ini` ([nn_preferences.cpp:2102](../../../src/freelancer/Client/nn_preferences.cpp#L2102)).

---

## 4. Клиентские ключи `Freelancer.ini`

`Freelancer.ini` открывается из **текущего каталога** по имени; переопределения пути из командной строки нет ([startup_config.cpp:44](../../../src/freelancer/Client/startup_config.cpp#L44)). Тот же файл содержит также секции сервера, ресурсов и данных, описанные в [freelancer-ini.md](freelancer-ini.md); в этом разделе перечислено только то, что читает стартовый код клиента. Клиент читает файл как минимум три раза в таком порядке: `LoadStartupConfig`, `CLoginMgr::LoadConfig` (оба в `WinMain`), затем `LoadClientIniData` при инициализации движка.

### 4.1 Запуск (`LoadStartupConfig`, [startup_config.cpp:36](../../../src/freelancer/Client/startup_config.cpp#L36))

| Секция | Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `[Freelancer]` | `data path` | строка | `..\Data` | Корень всех игровых данных. Усекается до 259 символов. | [startup_config.cpp:79](../../../src/freelancer/Client/startup_config.cpp#L79) |
| `[Startup]` | `movie_file` | строка, **повторяется, не более 3** | нет | Стартовые ролики, проигрываются в порядке следования в файле. Четвёртый и последующие `movie_file` игнорируются; счётчик идёт по всем секциям `[Startup]`. | [startup_config.cpp:104](../../../src/freelancer/Client/startup_config.cpp#L104) |
| `[Data]` | `gate_tunnels` | строка | пусто | Файл с определениями слоёв туннеля прыжка (`[gate_tunnel]`, см. [client-world-fx.md](client-world-fx.md)). | [startup_config.cpp:128](../../../src/freelancer/Client/startup_config.cpp#L128) |
| `[Data]` | `jump_effect` | строка | пусто | Файл с эффектами прыжковых врат / прыжкового корабля (см. [client-world-fx.md](client-world-fx.md)). | [startup_config.cpp:142](../../../src/freelancer/Client/startup_config.cpp#L142) |

Токены командной строки `+Q`/`-Q` (быстрый старт) и `+X`/`-X` (ловушка исключений плавающей точки) читаются в той же функции; INI-ключами они не являются.

### 4.2 Вход и сервер списка (`CLoginMgr::LoadConfig`, [login_mgr.cpp:1512](../../../src/freelancer/Client/login_mgr.cpp#L1512))

| Секция | Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `[Freelancer]` | `local_server` | строка | `Server.dll` | Имя модуля внутрипроцессного одиночного сервера. Усекается до 31 символа. Не проверяется; неверное имя приводит к ошибке при загрузке модуля. | [login_mgr.cpp:1530](../../../src/freelancer/Client/login_mgr.cpp#L1530) |
| `[Freelancer]` | `AppGUID` | строка | нет | GUID приложения в текстовой форме со скобками `{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}`, разбирается `ParseGUID`; применяется к сетевому клиенту, только если ключ присутствует. | [login_mgr.cpp:1545](../../../src/freelancer/Client/login_mgr.cpp#L1545) |
| `[ListServer]` | `hostname` | строка | `FLListServer2` | Имя хоста или IP сервера списка многопользовательской игры. | [login_mgr.cpp:1556](../../../src/freelancer/Client/login_mgr.cpp#L1556) |
| `[ListServer]` | `port` | число → беззнаковое | `28810` (0x708a) | Порт TCP/UDP сервера списка. Не проверяется. | [login_mgr.cpp:1560](../../../src/freelancer/Client/login_mgr.cpp#L1560) |
| `[Login]` | `name` | широкая строка | `SinglePlayer` | Имя учётной записи, используемое для входа. | [login_mgr.cpp:1572](../../../src/freelancer/Client/login_mgr.cpp#L1572) |
| `[Login]` | `pwd` | широкая строка | пусто | Пароль учётной записи открытым текстом. | [login_mgr.cpp:1576](../../../src/freelancer/Client/login_mgr.cpp#L1576) |
| `[Login]` | `char` | широкая строка | пусто | Имя персонажа, выбираемого автоматически. | [login_mgr.cpp:1580](../../../src/freelancer/Client/login_mgr.cpp#L1580) |

`[Login]` нет в стандартном `Freelancer.ini`; она существует для автоматического входа. Если файл вообще не удаётся прочитать, `LoadConfig` молча возвращается, и все значения по умолчанию остаются.

### 4.3 Загрузка файлов данных

Читается `LoadClientIniData` ([dataconfig_progress.cpp:177](../../../src/freelancer/Client/dataconfig_progress.cpp#L177)). Для каждой строки `[Data]` ниже значение — путь. Все они, кроме `fonts`, `rich_fonts` и `intro` (разрешаются собственным загрузчиком), сначала разрешаются через `GetFileSysPath(value, Data root)`. Каждый ключ читается один раз на вхождение; **повтор ключа загружает ещё один файл** в тот же реестр (они накапливаются). Остальные ключи `[Data]` (архетипы, вселенная, ...) обрабатываются другими модулями ([freelancer-ini.md](freelancer-ini.md)).

| Секция | Ключ | Загрузчик | Формат описан в |
|---|---|---|---|
| `[Data]` | `fonts` | `LoadFonts` | [раздел 5.1](#51-файл-шрифтов-truetype) |
| `[Data]` | `rich_fonts` | `LoadRichFontsFromIni` | [раздел 5.2](#52-файл-rich-шрифтов) |
| `[Data]` | `sounds` | `LoadClientSoundArchIni` | [client-audio-misc.md](client-audio-misc.md#1-архетипы-звуков-sound) |
| `[Data]` | `voices` | `LoadClientVoicesIni` | [client-audio-misc.md](client-audio-misc.md#2-профили-голосов-voice-sound) |
| `[Data]` | `effects` | `EffectArchLoadIni` | [client-world-fx.md](client-world-fx.md); `[EffectType]`/`[EffectLOD]` в [client-audio-misc.md](client-audio-misc.md#4-типы-эффектов-и-lod-effecttype-effectlod) |
| `[Data]` | `effect_shapes` | ставится в очередь, загружается в конце запуска движка как БД форм | [client-audio-misc.md](client-audio-misc.md#5-базы-форм-texture-и-shape) |
| `[Data]` | `debris` | `LoadDebrisArchFile` | [client-audio-misc.md](client-audio-misc.md#6-обломки-debris) |
| `[Data]` | `intro` | `LoadIntroConfigFromIni` | [раздел 8](#8-конфигурация-экрана-заставки-data-intro) |
| `[Data]` | `HUD` | путь сохраняется; элементы создаются, когда игрок входит в космос | [client-ui-config-windows.md](client-ui-config-windows.md#4-hudini) |
| `[Data]` | `igraph` | `GraphLibraryLoad` | [client-audio-misc.md](client-audio-misc.md#8-библиотека-графиков-igraph) |
| `[Resources]` | `DLL` (повторяется) | `LoadLibraryA` каждой DLL ресурсов по порядку | [freelancer-ini.md](freelancer-ini.md) |

Для `[Resources] DLL` неудачный `LoadLibrary` пишет ошибку в журнал, но слот сохраняется в списке модулей строковых ресурсов, поэтому идентификаторы последующих DLL сохраняют свой индекс. После чтения секций `audio\SoundCFG.ini` (относительно Data) безусловно загружается, если `Freelancer.ini` открылся ([client-audio-misc.md](client-audio-misc.md#3-soundcfgini)).

---

## 5. Шрифты

### 5.1 Файл шрифтов (`[TrueType]`)

`[Data] fonts = ...` (например, `fonts\fonts.ini`). Загружается `LoadFonts` ([font_imp.cpp:578](../../../src/freelancer/Client/font_imp.cpp#L578)). Одна секция `[TrueType]` на каждый именованный шрифт (остальные секции игнорируются).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | строка (≤127 символов) | пусто | Имя, которое определения интерфейса используют в своих ключах `font = ...`; ищется без учёта регистра, побеждает **первый** шрифт с этим именем ([font_imp.cpp:669](../../../src/freelancer/Client/font_imp.cpp#L669)). | [font_imp.cpp:604](../../../src/freelancer/Client/font_imp.cpp#L604) |
| `font` | строка (≤127 символов) | пусто | Имя гарнитуры GDI. | [font_imp.cpp:617](../../../src/freelancer/Client/font_imp.cpp#L617) |
| `fixed_height` | число | `0.0` | Высота шрифта; шрифт создаётся не жирным, не курсивным, не подчёркнутым. Смысл `0` определяется кодом шрифта; не проверяется. | [font_imp.cpp:630](../../../src/freelancer/Client/font_imp.cpp#L630) |

Шрифт, который не удалось создать, пишет в журнал `failed to create FONT font <nickname>` и исключается из счётчика; загрузка продолжается.

### 5.2 Файл rich-шрифтов

`[Data] rich_fonts = ...`. Загружается `LoadRichFontsFromIni` ([menu_fonts_load.cpp:46](../../../src/freelancer/Client/menu_fonts_load.cpp#L46)); определяет 32 нумерованных шрифта и стили текста, используемые окнами с форматированным текстом.

`[TrueType]` (повторяется; каждая строка `font` определяет один нумерованный шрифт):

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `font` (повторяется) | целое, строка, целое | нет | `font = <index>, <face name>, <size>`. Принимается индекс `0 ... 31`; бо́льшие индексы молча игнорируются. Размер — целое. Всегда не жирный, не курсивный, не подчёркнутый. | [menu_fonts_load.cpp:79](../../../src/freelancer/Client/menu_fonts_load.cpp#L79) |

`[Style]` (повторяется; один стиль текста на секцию; стиль регистрируется в конце секции):

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `name` | строка | идентификатор предыдущей секции или `0` для первой | Имя стиля; его **16-битный хеш** является идентификатором стиля (само имя не сохраняется). `[Style]` без `name` регистрируется под идентификатором предыдущего стиля (или `0`), заменяя его. | [menu_fonts_load.cpp:119](../../../src/freelancer/Client/menu_fonts_load.cpp#L119) |
| `font` | целое | не задано | Индекс шрифта (в таблицу `[TrueType]`, `0 ... 31`; хранится в 5 битах, не проверяется). | [menu_fonts_load.cpp:123](../../../src/freelancer/Client/menu_fonts_load.cpp#L123) |
| `bold` | bool | не задано | `true`/`1`/`false`/`0`. | [menu_fonts_load.cpp:134](../../../src/freelancer/Client/menu_fonts_load.cpp#L134) |
| `italic` | bool | не задано | как выше. | [menu_fonts_load.cpp:143](../../../src/freelancer/Client/menu_fonts_load.cpp#L143) |
| `underline` | bool | не задано | как выше. | [menu_fonts_load.cpp:152](../../../src/freelancer/Client/menu_fonts_load.cpp#L152) |
| `color` | целое, целое, целое | не задано | Красный, зелёный, синий, каждый усекается до байта (`0 ... 255`). Альфа в стиль не входит. | [menu_fonts_load.cpp:161](../../../src/freelancer/Client/menu_fonts_load.cpp#L161) |
| `justify` | строка | не задано | `left`, `center` или `right` (без учёта регистра); любое другое слово игнорируется. Может встречаться несколько раз; каждое допустимое вхождение добавляет настройку выравнивания. | [menu_fonts_load.cpp:170](../../../src/freelancer/Client/menu_fonts_load.cpp#L170) |

«Не задано» означает, что атрибут стилем не переопределяется, поэтому текст сохраняет то, что задаёт окружающий стиль.

---

## 6. Курсоры мыши: `mouse.ini`

Фиксированное имя `mouse.ini`, относительно Data. Один и тот же файл загружается дважды: как база данных форм (секции `[Texture]`/`[Shape]`, см. [client-audio-misc.md](client-audio-misc.md#5-базы-форм-texture-и-shape)) и `LoadCursors` ([input_manager.cpp:834](../../../src/freelancer/Client/input_manager.cpp#L834)) для секций `[Cursor]`. Сохраняется не более **20** курсоров; последующие секции `[Cursor]` разбираются, но не сохраняются. Имена курсоров ищутся без учёта регистра; код запрашивает `DEFAULT` (курсор, последним установленный по умолчанию), `Arrow`, `Cross`, `Stretch`, `fire`, `fire_friendly`, `fire_neutral`, `friendly`, `hostile`, `neutral` ([input_manager.cpp:1142](../../../src/freelancer/Client/input_manager.cpp#L1142)). Несуществующий курсор пишет в журнал `missing cursor`.

`[Cursor]` (повторяется). Ключи, идущие в секции **до** `nickname`, пропускаются, а секция без `nickname` отбрасывается, поэтому `nickname` нужно писать первым.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | строка | нет | Имя курсора. | [input_manager.cpp:862](../../../src/freelancer/Client/input_manager.cpp#L862) |
| `shape` | строка (≤23 символов) | пусто | Имя формы из записей `[Shape]` того же файла. | [input_manager.cpp:887](../../../src/freelancer/Client/input_manager.cpp#L887) |
| `hotspot` | число, число | `0, 0` | Смещение точки щелчка в пикселях. | [input_manager.cpp:901](../../../src/freelancer/Client/input_manager.cpp#L901) |
| `blend` | число → целое | `1` | Режим смешивания: `0` = альфа-смешивание выключено; `1 ... 5` выбирают одну из пяти пар коэффициентов смешивания источника/приёмника; любое другое значение использует пару по умолчанию. | [input_manager.cpp:907](../../../src/freelancer/Client/input_manager.cpp#L907), [input_manager.cpp:778](../../../src/freelancer/Client/input_manager.cpp#L778) |
| `color` | число ×4 | `255,255,255,255` | R, G, B, A, каждое ограничивается до `0 ... 255`; альфа `0` заменяется на `255`. | [input_manager.cpp:911](../../../src/freelancer/Client/input_manager.cpp#L911) |
| `anim` | строка, число, число | нет | `anim = <format>, <frames>, <fps>`: анимированный курсор. Формат — шаблон имени формы в стиле `printf`, принимающий номер кадра; форма заменяется каждый кадр. Индекс кадра равен `int(time * fps) % frames`; `fps`, равный `0`, замораживает курсор на кадре 0. Не проверяется (число кадров `0` при ненулевом fps приводит к делению на ноль). | [input_manager.cpp:924](../../../src/freelancer/Client/input_manager.cpp#L924), [input_manager.cpp:625](../../../src/freelancer/Client/input_manager.cpp#L625) |
| `angle` | число | `0` | Начальный поворот в радианах. | [input_manager.cpp:939](../../../src/freelancer/Client/input_manager.cpp#L939) |
| `spin` | число | `0` | Скорость вращения в радианах в секунду; при ненулевом значении угол становится `time * spin`. | [input_manager.cpp:943](../../../src/freelancer/Client/input_manager.cpp#L943) |
| `scale` | число | `1.0` | Множитель размера. | [input_manager.cpp:947](../../../src/freelancer/Client/input_manager.cpp#L947) |

---

## 7. Камеры и кабины

### 7.1 `cameras.ini`

Фиксированное имя `cameras.ini` ([space_state.cpp:552](../../../src/freelancer/Client/space_state.cpp#L552)), разрешается через `GetFullFilename` (корень Data). Каждая секция называет одну камеру, и каждый ключ передаётся установщику свойств этой камеры как необработанная строка.

| Секция | Камера |
|---|---|
| `[WinCamera]` | камера пользовательского интерфейса (окон) |
| `[CockpitCamera]` | камера кабины от первого лица |
| `[ThirdPersonCamera]` | камера преследования |
| `[TurretCamera]` | камера турели |
| `[RearViewCamera]` | камера заднего вида |
| `[DeathCamera]` | камера, используемая после уничтожения корабля игрока |

Неизвестное имя секции пишет в журнал `Unknown camera type?`, и секция пропускается. Каждый ключ передаётся в `SetProperty(name, value)`; нераспознанный ключ пишет в журнал `unknown property`, и загрузка продолжается. Распознаваемые ключи (одни и те же четыре для каждой камеры; используется только первый параметр значения, он преобразуется через `atof`, [space_camera.cpp:162](../../../src/freelancer/Client/space_camera.cpp#L162)):

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `fovx` | число | зависит от камеры | **Полный** горизонтальный угол обзора в градусах; код делит его пополам. Пересчитывает вертикальный угол для текущего соотношения сторон. | [space_camera.cpp:164](../../../src/freelancer/Client/space_camera.cpp#L164) |
| `fovy` | число | зависит от камеры | **Полный** вертикальный угол обзора в градусах (код делит пополам); горизонтальный угол следует из соотношения сторон. | [space_camera.cpp:189](../../../src/freelancer/Client/space_camera.cpp#L189) |
| `znear` | число | зависит от камеры | Ближняя плоскость отсечения. | [space_camera.cpp:216](../../../src/freelancer/Client/space_camera.cpp#L216) |
| `zfar` | число | зависит от камеры | Дальняя плоскость отсечения. | [space_camera.cpp:221](../../../src/freelancer/Client/space_camera.cpp#L221) |

Не проверяется. Игровые камеры (`CockpitCamera`, `ThirdPersonCamera`, `TurretCamera`, `RearViewCamera`, `DeathCamera`) дополнительно запоминают угол обзора, заданный `fovx`/`fovy`, как свой угол покоя ([space_camera_game.cpp:282](../../../src/freelancer/Client/space_camera_game.cpp#L282)).

### 7.2 Файлы кабины

Имя файла кабины берётся из архетипа корабля: клиентский ключ корабля `cockpit = <path>` ([client_cliobj.cpp:2459](../../../src/freelancer/Client/client_cliobj.cpp#L2459), см. [archetypes-and-equipment.md](archetypes-and-equipment.md)), разрешается относительно Data. Когда у корабля нет кабины, игра пишет в журнал `ship has no cockpit definition` и использует `cockpits\cockpit.ini` ([cockpit_load.cpp:108](../../../src/freelancer/Client/cockpit_load.cpp#L108)). Один и тот же файл по очереди читают четыре загрузчика; каждый берёт свою секцию и игнорирует остальные.

**От первого лица** — `FirstPersonCameraLoad` ([space_camera_first.cpp:585](../../../src/freelancer/Client/space_camera_first.cpp#L585)). Отсутствующий файл пишет предупреждение в журнал, но загрузка продолжается. Секция `[Camera]` пишет в журнал `no longer supported`.

| Секция | Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `[Cockpit]` | `mesh` | строка | пусто | Модель кабины. Пустое значение не загружает меш. Если модель содержит часть, в имени которой есть `cam`, эта часть задаёт положение и ориентацию головы. | [space_camera_first.cpp:612](../../../src/freelancer/Client/space_camera_first.cpp#L612) |
| `[Cockpit]` | `material_library` | строка | пусто | Библиотека материалов для меша кабины. | [space_camera_first.cpp:627](../../../src/freelancer/Client/space_camera_first.cpp#L627) |
| `[Cockpit]` | `head_turn` | число, число | `30, 25` | Максимальный поворот головы: горизонтальный и вертикальный угол (градусы; не проверяется). Эти два значения общие для камеры кабины. | [space_camera_first.cpp:642](../../../src/freelancer/Client/space_camera_first.cpp#L642) |

После загрузки угол обзора камеры кабины должен быть 4:3 (`tan(half fovx) / tan(half fovy)` в пределах 0.05 от 1.333), иначе пишется в журнал `cockpit aspect ... is not PC-compatible?` (только предупреждение).

**От третьего лица** — `ThirdPersonCameraLoad` ([space_camera_third.cpp:941](../../../src/freelancer/Client/space_camera_third.cpp#L941)). Ни один ключ не читается. Секция `[ThirdPerson]` пишет в журнал `obsolete`; параметры камеры преследования берутся из архетипа корабля (раздел 7.3). Применяется та же проверка 4:3.

**Турель** — `TurretCameraLoad` ([space_camera_turret.cpp:354](../../../src/freelancer/Client/space_camera_turret.cpp#L354)). `[Turret]` пишет в журнал `obsolete`.

| Секция | Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `[TurretCamera]` | `tether` | число ×3 | `0, 5, 25` | Смещение камеры от крепления турели (x, y, z). | [space_camera_turret.cpp:388](../../../src/freelancer/Client/space_camera_turret.cpp#L388) |
| `[TurretCamera]` | `yaw_rotate_speed` | число | `2.0` | Скорость вращения по рысканию. | [space_camera_turret.cpp:398](../../../src/freelancer/Client/space_camera_turret.cpp#L398) |
| `[TurretCamera]` | `pitch_rotate_speed` | число | `1.5` | Скорость вращения по тангажу. | [space_camera_turret.cpp:402](../../../src/freelancer/Client/space_camera_turret.cpp#L402) |
| `[TurretCamera]` | `accel_speed` | число | `5.0` | Скорость сглаживания/ускорения следования камеры. | [space_camera_turret.cpp:406](../../../src/freelancer/Client/space_camera_turret.cpp#L406) |

**Задний вид** — `RearViewCameraLoad` ([space_camera_rear.cpp:137](../../../src/freelancer/Client/space_camera_rear.cpp#L137)). `[RearView]` пишет в журнал `obsolete`.

| Секция | Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `[RearViewCamera]` | `view_position` | число ×3 | `0, 5, 20` | Положение камеры позади корабля (x, y, z). Отсутствующая компонента читается как `0`. | [space_camera_rear.cpp:171](../../../src/freelancer/Client/space_camera_rear.cpp#L171) |

### 7.3 Ключи камеры в архетипе корабля

Камера преследования (от третьего лица) настраивается для каждого корабля в архетипе корабля (записи `shiparch.ini`, читаются клиентским `FCliShip::ReadUnknown`, [client_cliobj.cpp:2440](../../../src/freelancer/Client/client_cliobj.cpp#L2440)). Ограничение углов находится в [ship_camera_params.cpp:23](../../../src/freelancer/Client/ship_camera_params.cpp#L23) (вспомогательная функция `ShipCameraParamsReadValue` в том же файле не имеет вызывающего; живой разборщик — `client_cliobj.cpp`).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `cockpit` | строка | пусто | Файл кабины корабля (раздел 7.2). | [client_cliobj.cpp:2459](../../../src/freelancer/Client/client_cliobj.cpp#L2459) |
| `camera_offset` | число, число | `0, 0` | Два параметра, сохраняются как `camera_offset[0]`, `camera_offset[1]`; камера преследования строит смещение взгляда как `{0, camera_offset[0], camera_offset[1]}` (y и z). Не проверяется. | [client_cliobj.cpp:2471](../../../src/freelancer/Client/client_cliobj.cpp#L2471), [space_camera_third.h:63](../../../src/freelancer/Client/space_camera_third.h#L63) |
| `camera_angular_slerp_multiplier` | число | `0.04` | Множитель следования за ориентацией. | [client_cliobj.cpp:2478](../../../src/freelancer/Client/client_cliobj.cpp#L2478) |
| `camera_angular_acceleration` | число | `0.07` | Ускорение следования за ориентацией. | [client_cliobj.cpp:2484](../../../src/freelancer/Client/client_cliobj.cpp#L2484) |
| `camera_horizontal_turn_angle` | число (градусы) | `0.2` (хранимое значение, радианы) | Ограничивается до `0 ... 90` (отрицательное значение становится `0`, больше 90 становится `90`), затем преобразуется в радианы. | [client_cliobj.cpp:2490](../../../src/freelancer/Client/client_cliobj.cpp#L2490), [ship_camera_params.cpp:23](../../../src/freelancer/Client/ship_camera_params.cpp#L23) |
| `camera_vertical_turn_up_angle` | число (градусы) | `0.2` (хранимое значение, радианы) | То же ограничение и преобразование. | [client_cliobj.cpp:2496](../../../src/freelancer/Client/client_cliobj.cpp#L2496) |
| `camera_vertical_turn_down_angle` | число (градусы) | `0.2` (хранимое значение, радианы) | То же ограничение и преобразование. | [client_cliobj.cpp:2502](../../../src/freelancer/Client/client_cliobj.cpp#L2502) |
| `camera_turn_look_ahead_slerp_amount` | число | `0.5` | Сохраняется без изменений. | [client_cliobj.cpp:2508](../../../src/freelancer/Client/client_cliobj.cpp#L2508) |

Значения по умолчанию — это значения, устанавливаемые конструктором `FCliShip` ([client_cliobj.cpp:2416](../../../src/freelancer/Client/client_cliobj.cpp#L2416)). Остальные клиентские ключи корабля, читаемые в той же функции (`bay_doors_open_snd`, `bay_doors_close_snd`, `pilot_mesh`), — идентификаторы, создаваемые из имени через `CreateID`.

---

## 8. Конфигурация экрана заставки: `[Data] intro`

`LoadIntroConfigFromIni` ([dataconfig_progress.cpp:53](../../../src/freelancer/Client/dataconfig_progress.cpp#L53)); путь разрешается относительно Data. Отсутствующий файл оставляет всё пустым. Читается только `[Images]`.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `TgaPath` | строка (≤63 символов) | пусто | Каталог/префикс изображений заставки. | [dataconfig_progress.cpp:69](../../../src/freelancer/Client/dataconfig_progress.cpp#L69) |
| `ScreenType` | строка (≤63) | пусто | Имя изображения для экрана типа. | [dataconfig_progress.cpp:71](../../../src/freelancer/Client/dataconfig_progress.cpp#L71) |
| `ScreenExit` | строка (≤63) | пусто | Изображение экрана выхода. | [dataconfig_progress.cpp:73](../../../src/freelancer/Client/dataconfig_progress.cpp#L73) |
| `ScreenReset` | строка (≤63) | пусто | Изображение экрана сброса. | [dataconfig_progress.cpp:75](../../../src/freelancer/Client/dataconfig_progress.cpp#L75) |
| `Screen1` ... `Screen4` | строка (≤63) | пусто | Четыре выбираемых экрана заставки (после загрузки код случайно выбирает базовый индекс 0, 1 или 2). | [dataconfig_progress.cpp:77](../../../src/freelancer/Client/dataconfig_progress.cpp#L77) |
| `bDrawScripts` | bool | `true` | `true`/`1`/`false`/`0`. | [dataconfig_progress.cpp:85](../../../src/freelancer/Client/dataconfig_progress.cpp#L85) |

Любой другой ключ пишет в журнал `unknown ini parameter`. Отсутствующий ключ сохраняет своё прежнее значение, поэтому файл можно загружать более одного раза.
