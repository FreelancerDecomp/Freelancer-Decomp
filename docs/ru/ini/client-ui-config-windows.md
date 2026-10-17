# Конфигурация клиента: определения окон, HUD и таблицы подстановки UI

Эта страница описывает INI-файлы, которые задают раскладку и управляют 3D-окнами пользовательского интерфейса клиента: формат определения окон (`navbar.ini` и классы элементов управления, которые он может создавать), файл элементов HUD `hud.ini` и небольшие таблицы подстановки для всплывающей справки, информационных карточек, знаний и значков кнопок. Клавиатура, параметры, экран, шрифты, курсоры, камеры и файлы запуска описаны в [client-ui-config.md](client-ui-config.md); базы форм (`[Texture]`/`[Shape]`) — в [client-audio-misc.md](client-audio-misc.md#5-базы-форм-texture-и-shape).

## Расположение файла / как он находится

Все файлы читаются через `INI_Reader` ([parsers.md](parsers.md)); имена разделов и ключей не зависят от регистра; повторяющиеся ключи выдаются в порядке следования в файле. Соглашения те же, что в [client-ui-config.md](client-ui-config.md#расположение-файлов--как-они-находятся): *Тип* называет геттер (*строка*, *int*, *число* = `get_value_float`, *bool*); ключ-**флаг** истинен, когда `get_value_float(0) != 0` (то есть `1` — включено, `0` или пустое значение — выключено, а слова `true`/`false` не понимаются); ключ-**вектор** `x, y, z` — это три числа в таком порядке; в столбце «По умолчанию» указано значение, которое конструктор элемента оставляет, когда ключ отсутствует. «Не проверяется» означает, что код не проверяет значение.

Пути отсчитываются от пути к данным (`[Freelancer] data path` в `Freelancer.ini`, по умолчанию `..\Data`), если не указано иное. Поставляемые файлы — бинарные `BINI`; текстовые файлы с теми же разделами работают так же.

| Файл | Как находится | Кто читает | Раздел |
|---|---|---|---|
| `interface\baseside\navbar.ini` | фиксированное имя, относительно Data | `NavBar::Create` → `WinControl::LoadIni` | 1, 2, 3 |
| `hud.ini` | `[Data] HUD = ...` в `Freelancer.ini` | `LoadHudElementsFromIni` | 4 |
| `interface\Rollover.ini` | фиксированное имя | `NnRolloverControl::LoadRolloverTable` | 5.1 |
| `interface\InfocardMap.ini` | фиксированное имя | `FInfocardMap::Load` | 5.2 |
| `interface\KnowledgeMap.ini` | фиксированное имя | `FKnowledgeMap::Load` | 5.3 |
| `interface\ButtonMontage.ini` | фиксированное имя | `LoadButtonMontage` | 5.4 |

Скрипты, которые управляют теми же окнами (текстовые файлы `PAGE_*`, экраны персонажа и базы), не являются INI и здесь не рассматриваются.

---

## 1. Формат определения окна (`WinControl::LoadIni`)

Источник: [wincontrol_base.cpp:336](../../../src/freelancer/Client/wincontrol_base.cpp#L336). Единственный INI с определением окна, который клиент открывает по имени, — `interface\baseside\navbar.ini` (открывается в [nav_bar.cpp:303](../../../src/freelancer/Client/nav_bar.cpp#L303)); сам загрузчик универсален (любой `WinControl` может запустить его на заданном пути), а разделы элементов ниже — это то, что он понимает.

Структура файла определения:

```ini
[BaseFrame]            ; the window itself
mesh   = interface\hud\example_frame.cmp
offset = 0.0, 0.1, -0.5

[SomeChild]            ; any other section creates one child control
class_name = ButtonControl
mesh   = interface\hud\example_button.3db
offset = 0.1, 0.0, 0.0
tooltip = 1234
```

Правила:

* Файл открывается через `open(path, false)`: отсутствующий файл или отсутствующий раздел `[BaseFrame]` молча завершает загрузку.
* `[BaseFrame]` находится через `find_header` и должен быть разделом, значения которого читаются первыми. Его ключи настраивают само окно (таблица 1.1).
* **Каждый последующий раздел** (в порядке следования в файле) создаёт дочерний элемент. **Имя раздела становится именем потомка** (усекается до 31 символа). Сканирование ищет в разделе значение `class_name`; первое найденное выбирает класс, а фабрика вызывается с читателем, позиционированным *после* этой строки, поэтому **`class_name` должен быть первым ключом раздела**: любой ключ, записанный до него, пропускается, любой ключ после него читается собственным разборщиком элемента (раздел 2). Разделы без `class_name` (например, `[NavBar]`, раздел 3) ничего не создают.
* `class_name` сопоставляется **с учётом регистра** с зарегистрированными фабриками окон (ниже). Неизвестное имя класса молча игнорируется (ни потомка, ни сообщения).

Зарегистрированные значения `class_name` (статики `WINFACTORY`): `BlockingDialog`, `ButtonControl`, `CargoBarControl`, `ChatControl`, `ChatTextInputDisplay`, `EditControl`, `MissionLogMoveFromNNWindow`, `MissionLogMoveToNNWindow`, `MissionObjectiveWindow`, `MsgWin`, `NN_ArrivalText`, `NN_CommodityByBase`, `NN_Confirm`, `NN_Dealer`, `NN_Error`, `NN_Exit`, `NN_GroupControl`, `NN_Help`, `NN_Inventory`, `NN_LoadSave`, `NN_MainButton`, `NN_MissionVendor`, `NN_NetworkOperation`, `NN_News`, `NN_OfferDialog`, `NN_Pause`, `NN_PlayerInfo`, `NN_Preferences`, `NN_Prompt`, `NN_Repair`, `NN_Scanner`, `NN_ShipTrader`, `NN_Trade`, `NavBar`, `NeuroNetMissionMessage`, `NeuroNetNavMap`, `NeuroNetStoryStar`, `RichTextButtonControl`, `RichTextEditWin`, `RichTextWin`, `RichTextWireButtonControl`, `Rollover`, `ScrollControl`, `ServerFilterDialog`, `ShipViewControl`, `StaticControl`, `TextControl`, `ToggleButtonControl`, `ToolTip`, `UICycle`, `UIFastTable`, `UIListBox`, `UIRichTextWin`, `UIScroll`, `UIScrollTable`, `UIShipViewControl`, `UIStaticText`, `UITable`, `UITextEdit`, `UITextMsgButton`, `UIWinDeform`, `UIWinObject`, `WinControl`, `WireButtonControl`, `WireStaticControl` (каждый — статик `WINFACTORY<T>`, см. [win_factory.h](../../../src/freelancer/Client/win_factory.h); поиск с учётом регистра — это `WinFactoryCreate` в [win_factory.cpp](../../../src/freelancer/Client/win_factory.cpp#L80)).

Ключи из раздела определения читают только классы из раздела 2. Все остальные классы игнорируют полезную нагрузку и строят своё содержимое в коде; указание их в `class_name` создаёт объект, но остальные ключи этого раздела не действуют.

### 1.1 Ключи `[BaseFrame]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `mesh` | строка | нет | 3D-модель (`.3db`/`.cmp`) рамки окна. | [wincontrol_base.cpp:347](../../../src/freelancer/Client/wincontrol_base.cpp#L347) |
| `offset` | вектор | `0, 0, 0` | Позиция окна относительно родителя. | [wincontrol_base.cpp:352](../../../src/freelancer/Client/wincontrol_base.cpp#L352) |
| `mouse_size` | вектор | `0, 0, 0` | Полуразмеры области попадания мыши. | [wincontrol_base.cpp:372](../../../src/freelancer/Client/wincontrol_base.cpp#L372) |
| `no_z_enable` | флаг | выкл | Рисовать без проверки глубины. | [wincontrol_base.cpp:382](../../../src/freelancer/Client/wincontrol_base.cpp#L382) |
| `z_order` | число → unsigned | значение Win3D по умолчанию (не меняется) | Слой отрисовки. Не проверяется. | [wincontrol_base.cpp:387](../../../src/freelancer/Client/wincontrol_base.cpp#L387) |
| `tooltip` | число → int | `0` | Id строкового ресурса всплывающей подсказки при наведении; `0` = нет. | [wincontrol_base.cpp:392](../../../src/freelancer/Client/wincontrol_base.cpp#L392) |
| `behavior`, `subcontrol`, `class_name` | – | – | Распознаются/игнорируются в `[BaseFrame]` (без эффекта). | [wincontrol_base.cpp:345](../../../src/freelancer/Client/wincontrol_base.cpp#L345), [wincontrol_base.cpp:370](../../../src/freelancer/Client/wincontrol_base.cpp#L370) |

---

## 2. Разделы элементов управления

Каждый класс ниже создаётся, когда в разделе есть `class_name = <Class>`; ключи после него читаются до конца раздела. Все неизвестные ключи игнорируются. `hardpoint = <name>` в любом классе означает: прикрепить к hardpoint с именем `<name>` на меше **родительского** элемента (позиция элемента следует за этим hardpoint); если родитель не является оконным элементом или hardpoint не существует, прикрепление остаётся с тождественным преобразованием.

### 2.1 `ButtonControl`, `ToggleButtonControl`, `RichTextButtonControl`

Все три используют один разборщик ([nn_button_control.cpp:335](../../../src/freelancer/Client/nn_button_control.cpp#L335)); `ToggleButtonControl` дополнительно использует `mesh2` как вторую (переключённую) модель.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `mesh` | строка | нет | Модель кнопки. Та же строка (без учёта регистра) служит ключом в `ButtonMontage.ini` (раздел 5.4): если плитка с таким именем существует, кнопка рисуется из монтажа значков. | [nn_button_control.cpp:347](../../../src/freelancer/Client/nn_button_control.cpp#L347), [nn_button_control.cpp:464](../../../src/freelancer/Client/nn_button_control.cpp#L464) |
| `mesh2` | строка | пусто | Вторая модель (используется `ToggleButtonControl`). | [nn_button_control.cpp:352](../../../src/freelancer/Client/nn_button_control.cpp#L352) |
| `button_sound` | строка | нет | Имя слота звука UI, который проигрывает кнопка; пустое значение игнорируется; не проверяется. | [nn_button_control.cpp:357](../../../src/freelancer/Client/nn_button_control.cpp#L357) |
| `event` | строка | нет | Связывает кнопку с игровой командой. Распознаваемые значения (без учёта регистра): `tractor`, `next`, `prev`, `next_subtarget`, `prev_subtarget`. Другие значения не дают кнопке ни команды, ни подсказки. Распознанное значение также задаёт подсказку. | [nn_button_control.cpp:366](../../../src/freelancer/Client/nn_button_control.cpp#L366), [nn_button_control.cpp:198](../../../src/freelancer/Client/nn_button_control.cpp#L198) |
| `offset` | вектор | `0, 0, 0` | Позиция относительно родителя (или hardpoint). | [nn_button_control.cpp:378](../../../src/freelancer/Client/nn_button_control.cpp#L378) |
| `hardpoint` | строка | нет | См. выше. | [nn_button_control.cpp:398](../../../src/freelancer/Client/nn_button_control.cpp#L398) |
| `mouse_size` | вектор | `0, 0, 0` | Полуразмеры области щелчка. | [nn_button_control.cpp:422](../../../src/freelancer/Client/nn_button_control.cpp#L422) |
| `no_mesh_render` | флаг | выкл | Не рисовать модель (вместо неё рисует значок монтажа или код). | [nn_button_control.cpp:432](../../../src/freelancer/Client/nn_button_control.cpp#L432) |
| `action_button` | флаг | выкл | Помечает кнопку как кнопку действия. | [nn_button_control.cpp:438](../../../src/freelancer/Client/nn_button_control.cpp#L438) |
| `no_color_change` | флаг | выкл | Не подкрашивать кнопку при наведении/нажатии. | [nn_button_control.cpp:444](../../../src/freelancer/Client/nn_button_control.cpp#L444) |
| `no_z_enable` | флаг | выкл | Рисовать без проверки глубины. | [nn_button_control.cpp:450](../../../src/freelancer/Client/nn_button_control.cpp#L450) |
| `tooltip` | число → int | `0` | Id строкового ресурса подсказки. | [nn_button_control.cpp:455](../../../src/freelancer/Client/nn_button_control.cpp#L455) |
| `tooltip2` | число → int | `0` | Id второй подсказки (альтернативное состояние). | [nn_button_control.cpp:460](../../../src/freelancer/Client/nn_button_control.cpp#L460) |

### 2.2 `StaticControl`

[nn_static_control.cpp:275](../../../src/freelancer/Client/nn_static_control.cpp#L275). (Возможно, вращающаяся) модель без взаимодействия.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `mesh` | строка | нет | Модель. | [nn_static_control.cpp:287](../../../src/freelancer/Client/nn_static_control.cpp#L287) |
| `mouse_size` | вектор | `0, 0, 0` | Полуразмеры области попадания. | [nn_static_control.cpp:292](../../../src/freelancer/Client/nn_static_control.cpp#L292) |
| `offset` | вектор | `0, 0, 0` | Позиция относительно родителя. | [nn_static_control.cpp:302](../../../src/freelancer/Client/nn_static_control.cpp#L302) |
| `hardpoint` | строка | нет | См. выше. | [nn_static_control.cpp:314](../../../src/freelancer/Client/nn_static_control.cpp#L314) |
| `no_z_enable` | флаг | выкл | Рисовать без проверки глубины. | [nn_static_control.cpp:343](../../../src/freelancer/Client/nn_static_control.cpp#L343) |
| `zclear` | флаг | выкл | Очищать буфер глубины перед отрисовкой. | [nn_static_control.cpp:348](../../../src/freelancer/Client/nn_static_control.cpp#L348) |
| `no_mesh_render` | флаг | выкл | Не рисовать сплошную модель. | [nn_static_control.cpp:353](../../../src/freelancer/Client/nn_static_control.cpp#L353) |
| `no_wire_render` | флаг | выкл | Не рисовать каркас. | [nn_static_control.cpp:358](../../../src/freelancer/Client/nn_static_control.cpp#L358) |
| `no_coloring` | флаг | выкл | Не применять цветовой оттенок HUD. | [nn_static_control.cpp:363](../../../src/freelancer/Client/nn_static_control.cpp#L363) |
| `render_to_window` | флаг | выкл | Рисовать модель в собственный viewport окна. | [nn_static_control.cpp:368](../../../src/freelancer/Client/nn_static_control.cpp#L368) |
| `no_clear_render` | флаг | **вкл** | Не очищать цель перед отрисовкой (по умолчанию включено; задайте `0`, чтобы очищать). | [nn_static_control.cpp:373](../../../src/freelancer/Client/nn_static_control.cpp#L373) |
| `force_radius` | число | `0` | Переопределение ограничивающего радиуса, по которому модель вписывается в вид. | [nn_static_control.cpp:378](../../../src/freelancer/Client/nn_static_control.cpp#L378) |
| `rotation_speed` | число | `0` | Скорость вращения; `0` = не вращается. | [nn_static_control.cpp:383](../../../src/freelancer/Client/nn_static_control.cpp#L383) |
| `enable_rotation` | флаг | выкл | Разрешить вращение пользователем. | [nn_static_control.cpp:388](../../../src/freelancer/Client/nn_static_control.cpp#L388) |
| `starting_yaw_pitch_roll` | число ×3 | `0, 0, 0` | Начальная ориентация: рыскание, тангаж, крен (единицы — как их потребляет код вращения; не проверяется). | [nn_static_control.cpp:393](../../../src/freelancer/Client/nn_static_control.cpp#L393) |

### 2.3 `TextControl` и `EditControl`

[nn_text_control.cpp:207](../../../src/freelancer/Client/nn_text_control.cpp#L207). `EditControl` наследует тот же разборщик (без переопределения).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `font` | строка | системный по умолчанию | Nickname шрифта из файла `fonts` ([client-ui-config.md](client-ui-config.md#51-файл-шрифтов-truetype)); неизвестный nickname оставляет текущий шрифт. | [nn_text_control.cpp:219](../../../src/freelancer/Client/nn_text_control.cpp#L219) |
| `offset` | вектор | `0, 0, 0` | Позиция относительно родителя. | [nn_text_control.cpp:227](../../../src/freelancer/Client/nn_text_control.cpp#L227) |
| `hardpoint` | строка | нет | См. выше. | [nn_text_control.cpp:237](../../../src/freelancer/Client/nn_text_control.cpp#L237) |
| `width` | число | `0` | Ширина текстового блока. | [nn_text_control.cpp:263](../../../src/freelancer/Client/nn_text_control.cpp#L263) |
| `height` | число | `0` | Высота текстового блока. | [nn_text_control.cpp:267](../../../src/freelancer/Client/nn_text_control.cpp#L267) |
| `font_scale` | – | – | Распознаётся и пропускается (без эффекта). | [nn_text_control.cpp:271](../../../src/freelancer/Client/nn_text_control.cpp#L271) |
| `stringID` | число → int | нет | Id строкового ресурса отображаемого текста. | [nn_text_control.cpp:275](../../../src/freelancer/Client/nn_text_control.cpp#L275) |
| `justification` | число → int | `0` | Горизонтальное выравнивание: `1` = по центру, `2` = по правому краю, всё остальное = по левому. | [nn_text_control.cpp:280](../../../src/freelancer/Client/nn_text_control.cpp#L280), [font_string.h](../../../src/freelancer/Client/font_string.h#L298) |
| `tooltip` | число → int | `0` | Id строкового ресурса подсказки; ненулевое значение также делает текстовый блок реагирующим на наведение. | [nn_text_control.cpp:284](../../../src/freelancer/Client/nn_text_control.cpp#L284) |

### 2.4 `ScrollControl`

[scroll_control.cpp:613](../../../src/freelancer/Client/scroll_control.cpp#L613). Полоса прокрутки из четырёх частей (*фон*, *кнопка «вперёд»*, *кнопка «назад»*, *ползунок*). У каждой части одинаковая группа ключей с префиксом: `back_`, `nextb_`, `prevb_`, `thumb_`.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `back_mesh`, `nextb_mesh`, `prevb_mesh`, `thumb_mesh` | строка | нет | Модель части. | [scroll_control.cpp:626](../../../src/freelancer/Client/scroll_control.cpp#L626) |
| `back_offset`, `nextb_offset`, `prevb_offset` | вектор | `0, 0, 0` | Позиция части относительно её hardpoint. | [scroll_control.cpp:630](../../../src/freelancer/Client/scroll_control.cpp#L630) |
| `thumb_offset0`, `thumb_offset1` | вектор | `0, 0, 0` | Позиция ползунка в начале и в конце дорожки. | [scroll_control.cpp:703](../../../src/freelancer/Client/scroll_control.cpp#L703) |
| `back_hp`, `nextb_hp`, `prevb_hp`, `thumb_hp0`, `thumb_hp1` | строка | тождественное | Имя hardpoint на родителе, для которого создаётся элемент; часть прикрепляется к нему. | [scroll_control.cpp:636](../../../src/freelancer/Client/scroll_control.cpp#L636) |
| `back_mouse`, `nextb_mouse`, `prevb_mouse`, `thumb_mouse` | вектор | `0, 0, 0` | Полуразмеры области щелчка части. | [scroll_control.cpp:643](../../../src/freelancer/Client/scroll_control.cpp#L643) |
| `back_no_mesh_render`, `nextb_no_mesh_render`, `prevb_no_mesh_render`, `thumb_no_mesh_render` | флаг | выкл | Не рисовать модель части. | [scroll_control.cpp:647](../../../src/freelancer/Client/scroll_control.cpp#L647) |
| `min_range` | число → int | `0` | Наименьшее значение. | [scroll_control.cpp:735](../../../src/freelancer/Client/scroll_control.cpp#L735) |
| `max_range` | число → int | `0` | Наибольшее значение. | [scroll_control.cpp:739](../../../src/freelancer/Client/scroll_control.cpp#L739) |
| `page_size` | число → int | `1` | Шаг значения при щелчке по странице. | [scroll_control.cpp:743](../../../src/freelancer/Client/scroll_control.cpp#L743) |
| `slider_behavior` | флаг | выкл | Вести себя как слайдер (ползунок прыгает/перетаскивается), а не как полоса прокрутки. | [scroll_control.cpp:747](../../../src/freelancer/Client/scroll_control.cpp#L747) |
| `no_z_enable` | флаг | выкл | Рисовать без проверки глубины. | [scroll_control.cpp:751](../../../src/freelancer/Client/scroll_control.cpp#L751) |

### 2.5 `CargoBarControl`

[cargo_bar_control.cpp:185](../../../src/freelancer/Client/cargo_bar_control.cpp#L185). Полоса вместимости трюма. Примечание: `mesh` не делает `continue`, поэтому та же строка проверяется и по следующим за ним ключам (практического эффекта нет).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `mesh` | строка | нет | Модель. | [cargo_bar_control.cpp:196](../../../src/freelancer/Client/cargo_bar_control.cpp#L196) |
| `offset` | вектор | `0, 0, 0` | Позиция относительно родителя. | [cargo_bar_control.cpp:202](../../../src/freelancer/Client/cargo_bar_control.cpp#L202) |
| `hardpoint` | строка | нет | См. выше. | [cargo_bar_control.cpp:212](../../../src/freelancer/Client/cargo_bar_control.cpp#L212) |
| `mouse_size` | вектор | `0, 0, 0` | Полуразмеры области наведения. | [cargo_bar_control.cpp:229](../../../src/freelancer/Client/cargo_bar_control.cpp#L229) |
| `horizontal` | флаг | выкл | Полоса заполняется горизонтально, а не вертикально. | [cargo_bar_control.cpp:239](../../../src/freelancer/Client/cargo_bar_control.cpp#L239) |
| `tooltip` | флаг | выкл | Включить подсказку при наведении. | [cargo_bar_control.cpp:245](../../../src/freelancer/Client/cargo_bar_control.cpp#L245) |
| `tooltip2` | число → int | `0` | Id строкового ресурса подсказки; также включает подсказку. | [cargo_bar_control.cpp:250](../../../src/freelancer/Client/cargo_bar_control.cpp#L250) |
| `color` | число ×4 | `0, 0, 0, 0` | R, G, B, A полосы, каждое **`0 ... 255`** (усекается до байта; не масштабируется). | [cargo_bar_control.cpp:256](../../../src/freelancer/Client/cargo_bar_control.cpp#L256) |
| `color_highlight` | число ×4 | `255, 255, 0, 255` | R, G, B, A выделенной (занятой) части, каждое `0 ... 255`. | [cargo_bar_control.cpp:271](../../../src/freelancer/Client/cargo_bar_control.cpp#L271) |

### 2.6 `ShipViewControl`

[ship_view_control.cpp:363](../../../src/freelancer/Client/ship_view_control.cpp#L363). Вращающийся предпросмотр корабля. Каждый ключ проверяется независимо (строка может совпасть с несколькими проверками, но имена ключей различны, поэтому на результат это не влияет).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `rot_speed` | вектор | `0, 0, 0` | Скорость вращения по каждой оси. | [ship_view_control.cpp:375](../../../src/freelancer/Client/ship_view_control.cpp#L375) |
| `offset` | вектор | `0, 0, 0` | Смещение вида. | [ship_view_control.cpp:384](../../../src/freelancer/Client/ship_view_control.cpp#L384) |
| `object_ypr` | вектор | тождественная ориентация | Начальные рыскание, тангаж и крен корабля. Единицы — как их потребляет построитель ориентации; не проверяется. | [ship_view_control.cpp:393](../../../src/freelancer/Client/ship_view_control.cpp#L393) |
| `viewSize` | вектор | `0, 0, 0` | Размер области предпросмотра. | [ship_view_control.cpp:403](../../../src/freelancer/Client/ship_view_control.cpp#L403) |
| `hardpoint` | строка | нет | См. выше. | [ship_view_control.cpp:412](../../../src/freelancer/Client/ship_view_control.cpp#L412) |
| `show_rect` | флаг | выкл | Рисовать прямоугольник рамки. | [ship_view_control.cpp:433](../../../src/freelancer/Client/ship_view_control.cpp#L433) |
| `show_wireframe` | флаг | выкл | Рисовать наложение каркаса. | [ship_view_control.cpp:439](../../../src/freelancer/Client/ship_view_control.cpp#L439) |
| `rect_color` | число ×4 | цвет оформления HUD | R, G, B, A, каждое **`0 ... 1`** (умножается на 255 и усекается). | [ship_view_control.cpp:444](../../../src/freelancer/Client/ship_view_control.cpp#L444) |
| `wire_color` | число ×4 | цвет оформления HUD | R, G, B, A, каждое `0 ... 1`. | [ship_view_control.cpp:456](../../../src/freelancer/Client/ship_view_control.cpp#L456) |

Обратите внимание: соглашение о цвете у элементов различается: цвета `CargoBarControl` лежат в диапазоне `0 ... 255`, а цвета `ShipViewControl` — в диапазоне `0 ... 1`.

---

## 3. `[NavBar]` (в `navbar.ini`)

Панель навигации на экранах базы ([nav_bar.cpp:303](../../../src/freelancer/Client/nav_bar.cpp#L303)). После `LoadIni` тот же файл открывается повторно и читается раздел `[NavBar]` ([nav_bar.cpp:337](../../../src/freelancer/Client/nav_bar.cpp#L337)): **сохраняется каждый ключ** в виде пары *имя hotspot → путь к мешу*. Ключ — это имя hotspot (объекта комнаты) в текущей комнате; значение — 3D-модель, используемая для навигационной кнопки этого hotspot (буфер значения — 128 байт, код копирует без проверки длины — держите значения короче 127 символов). Hotspot без записи использует `interface\baseside\unknown.3db`, и игра запоминает этот выбор на время сессии. Фиксированных имён ключей и проверки нет.

---

## 4. `hud.ini`

Находится по `[Data] HUD = <path>` в `Freelancer.ini` (путь сохраняется при запуске, а файл читается, когда игрок входит в космос; [cockpit_load.cpp:122](../../../src/freelancer/Client/cockpit_load.cpp#L122), [hud_element_list.cpp:445](../../../src/freelancer/Client/hud_element_list.cpp#L445)). Отсутствующий файл пишет в лог `hud file not found`. Каждый раздел, кроме `[HUD]`, создаёт один элемент HUD; имя раздела выбирает тип элемента (без учёта регистра).

### 4.1 `[HUD]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `shapes` | строка | нет | База форм (файл `[Texture]`/`[Shape]`, см. [client-audio-misc.md](client-audio-misc.md#5-базы-форм-texture-и-shape)) с текстурами HUD, относительно Data. | [hud_element_list.cpp:463](../../../src/freelancer/Client/hud_element_list.cpp#L463) |
| `static_txm` | строка | нет | Текстура для наложения статики/шума HUD. | [hud_element_list.cpp:468](../../../src/freelancer/Client/hud_element_list.cpp#L468) |
| `static_blend` | число → int | `2` | Id режима смешивания наложения статики. Не проверяется. | [hud_element_list.cpp:473](../../../src/freelancer/Client/hud_element_list.cpp#L473) |

### 4.2 Разделы элементов

| Раздел | Элемент | Читаемые ключи |
|---|---|---|
| `[Button]` | кнопка HUD | `action`, `object`, `object_pos`, `tool_tip_id` (4.3) |
| `[Receiver]` | приёмник связи / портрет чата | 4.4 |
| `[Maneuvers]` | панель кнопок манёвров | `maneuver` (4.5) |
| `[Steer]` | стрелка управления | `radius`, `range` (4.6) |
| `[ContactList]`, `[CruiseProgress]`, `[DamageIndicator]`, `[Core]`, `[Status]`, `[Target]`, `[Targetable_Objects]`, `[Waypoint]` | соответствующие элементы HUD | **ключи не читаются**; раздел должен лишь существовать, чтобы элемент был создан |

Имя раздела, которое не принимает ни один элемент, пишет в лог `HUD - unknown INI entry` и пропускается. Если существует несколько разделов с одним именем, для каждого создаётся свой элемент. Элемент, у которого `Init` завершается неудачей, отбрасывается.

### 4.3 `[Button]`

[hud_button.cpp:249](../../../src/freelancer/Client/hud_button.cpp#L249).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `action` | строка | пусто | Команда, запускаемая кнопкой. Точные слова (с учётом регистра): `radio`, `status`, `zoom`, `navmap`, `toggle_manuevers` (sic, написание как в коде). Любое другое значение пишет в лог `unknown HUD_Button action`, и у кнопки нет команды. | [hud_button.cpp:264](../../../src/freelancer/Client/hud_button.cpp#L264) |
| `object` | строка | нет | Модель кнопки. | [hud_button.cpp:282](../../../src/freelancer/Client/hud_button.cpp#L282) |
| `object_pos` | вектор | `0, 0, -5` | Позиция кнопки. | [hud_button.cpp:287](../../../src/freelancer/Client/hud_button.cpp#L287) |
| `tool_tip_id` | число → int | `0` | Id строкового ресурса подсказки. | [hud_button.cpp:302](../../../src/freelancer/Client/hud_button.cpp#L302) |

### 4.4 `[Receiver]`

[hud_comm.cpp:1581](../../../src/freelancer/Client/hud_comm.cpp#L1581). Ключи окна связи (`corner` ... `static`) применяются только к окну **первого** канала связи.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `corner` | строка | нет | Модель угловой рамки окна связи; подкрашивается цветом HUD. | [hud_comm.cpp:745](../../../src/freelancer/Client/hud_comm.cpp#L745) |
| `btn_pos` | вектор | `0, 0, -2` | Позиция кнопки связи. | [hud_comm.cpp:756](../../../src/freelancer/Client/hud_comm.cpp#L756) |
| `win_pos` | вектор | `0.3, 0.3, -1` | Позиция окна. Разбирается, но позиция окна первого канала затем перезаписывается фиксированным `-1.068, 0.65, -2.392`, поэтому здесь ключ не действует. | [hud_comm.cpp:767](../../../src/freelancer/Client/hud_comm.cpp#L767), [hud_comm.cpp:1679](../../../src/freelancer/Client/hud_comm.cpp#L1679) |
| `win_size` | число, число | `0.2, 0.2` | Ширина и высота окна. | [hud_comm.cpp:777](../../../src/freelancer/Client/hud_comm.cpp#L777) |
| `fly_time` | число | `0.5` | Время пролёта анимации открытия окна (хранится как задано; не проверяется). | [hud_comm.cpp:783](../../../src/freelancer/Client/hud_comm.cpp#L783) |
| `static` | флаг | вкл | Показывать наложение статики/шума на портрете. | [hud_comm.cpp:788](../../../src/freelancer/Client/hud_comm.cpp#L788) |
| `speaker_offset` | – | – | Устарел: пишет предупреждение с требованием использовать два ключа ниже; без эффекта. | [hud_comm.cpp:1612](../../../src/freelancer/Client/hud_comm.cpp#L1612) |
| `male_speaker_offset` | вектор | `0, 0, 0` | Позиция модели головы говорящего мужчины. | [hud_comm.cpp:1619](../../../src/freelancer/Client/hud_comm.cpp#L1619) |
| `female_speaker_offset` | вектор | `0, 0, 0` | Позиция модели головы говорящей женщины. | [hud_comm.cpp:1628](../../../src/freelancer/Client/hud_comm.cpp#L1628) |
| `speaker_rotate` | число ×3 | нулевой поворот | Углы Эйлера модели говорящего (единицы — как их потребляет `EulerMatrix`; не проверяется). | [hud_comm.cpp:1637](../../../src/freelancer/Client/hud_comm.cpp#L1637) |
| `video_fovx` | число | по умолчанию камеры | **Половина** горизонтального угла обзора, в градусах, камеры портрета говорящего (учтите: `fovx` в `cameras.ini` — это *полный* угол). | [hud_comm.cpp:1646](../../../src/freelancer/Client/hud_comm.cpp#L1646) |
| `backdrop` | строка | нет | Текстура, рисуемая позади говорящего. | [hud_comm.cpp:1650](../../../src/freelancer/Client/hud_comm.cpp#L1650) |

### 4.5 `[Maneuvers]`

[hud_maneuvers.cpp:182](../../../src/freelancer/Client/hud_maneuvers.cpp#L182). Одна строка `maneuver` на каждую кнопку, по порядку.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `maneuver` | **повторяется**; 5 позиционных параметров | нет | `maneuver = <name>, <tooltip ids>, <ids2>, <mesh>, <gray mesh>` — `name`: имя поведения (разрешается в id поведения через `BehaviorTypes::get_behavior_id`; имена `Dock`, `Goto`, `Formation`, `FreeFlight` (без учёта регистра) дополнительно привязывают кнопку к фиксированной команде, у остальных команды нет); `tooltip ids`, `ids2`: id строковых ресурсов (числа); `mesh`: модель включённой кнопки; `gray mesh`: модель выключенной кнопки. Обе строки мешей также ищутся (без учёта регистра) в `ButtonMontage.ini` (раздел 5.4). Не проверяется. | [hud_maneuvers.cpp:191](../../../src/freelancer/Client/hud_maneuvers.cpp#L191), [hud_maneuvers.cpp:47](../../../src/freelancer/Client/hud_maneuvers.cpp#L47) |

### 4.6 `[Steer]`

[hud_element_list.cpp:563](../../../src/freelancer/Client/hud_element_list.cpp#L563).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `radius` | число | `40` | Радиус окружности стрелки управления. | [hud_element_list.cpp:587](../../../src/freelancer/Client/hud_element_list.cpp#L587) |
| `range` | число | `200` | Дальность полёта мышью. | [hud_element_list.cpp:589](../../../src/freelancer/Client/hud_element_list.cpp#L589) |

---

## 5. Таблицы подстановки

Все четыре таблицы крошечные и имеют одну форму: один раздел с повторяющимся ключом `map`. Отсутствующий файл оставляет таблицу пустой.

### 5.1 `interface\Rollover.ini` — `[RolloverTable]`

Сопоставляет id строки всплывающей подсказки с id более длинного текста справки «rollover» ([nn_rollover.cpp:153](../../../src/freelancer/Client/nn_rollover.cpp#L153)).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `map` | **повторяется**; `a, b` | нет | `a` = id строки показываемой подсказки (значение `tooltip` элемента под курсором), `b` = id строки текста rollover, показываемого вместо неё. Оба преобразуются в целые числа. Повторяющееся `a` сохраняет свою **первую** строку. | [nn_rollover.cpp:170](../../../src/freelancer/Client/nn_rollover.cpp#L170) |

### 5.2 `interface\InfocardMap.ini` — `[InfocardMapTable]`

Расширяет информационные карточки объектов ([infocard_map.cpp:8](../../../src/freelancer/Client/infocard_map.cpp#L8), семантика — в [infocard_map.h](../../../src/freelancer/Client/infocard_map.h)).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `map` | **повторяется**; `key, extra [, a, b, c]` | нет | `key` = `ids_info` объекта; `extra` = второй id строки, добавляемый к информационной карточке этого объекта; `a`, `b`, `c` = переключатели (по умолчанию `1`) для списка оборудования, списков товаров и списка кораблей на продажу, показываемых в карточке. Все значения преобразуются в целые числа. Повторяющийся `key` сохраняет свою **первую** строку. Не проверяется. | [infocard_map.cpp:27](../../../src/freelancer/Client/infocard_map.cpp#L27) |

### 5.3 `interface\KnowledgeMap.ini` — `[KnowledgeMapTable]`

Делает так, что чтение текста раскрывает объекты ([knowledge_map.cpp:51](../../../src/freelancer/Client/knowledge_map.cpp#L51)): когда показывается текст с id строки `key`, у каждого объекта, перечисленного под этим ключом, поднимаются флаги посещения.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `map` | **повторяется**; `key, nickname, flags` | нет | `key` = id строки текста (целое число); `nickname` = nickname объекта (хешируется через `CreateID`); `flags` = байт флагов посещения (младшие 8 бит целого числа). Если `flags & 0x40`, nickname интерпретируется как nickname **фракции** и разрешается через `Reputation::get_id`. Несколько строк с одним и тем же `key` сохраняются все (новейшая первой). | [knowledge_map.cpp:69](../../../src/freelancer/Client/knowledge_map.cpp#L69) |

### 5.4 `interface\ButtonMontage.ini` — `[Montage]`

Описывает атлас значков, из которого рисуются модели кнопок ([nn_montage_draw.cpp:32](../../../src/freelancer/Client/nn_montage_draw.cpp#L32)). Сами текстуры берутся из `Interface\ButtonTextures.ini` — базы форм ([client-audio-misc.md](client-audio-misc.md#5-базы-форм-texture-и-shape)), формы `Montage32`, `Montage64`, `HUD_targetingelement` и `HUD_talkelement` которой ищутся по имени.

Внутри каждого раздела `[Montage]` ключи обрабатываются по порядку:

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `size` | int | `0` | Ребро плитки в пикселях на 256-пиксельной странице атласа; плиток в ряду = `256 / size` (4, если `size <= 0`). Сбрасывает курсор плитки в начало страницы. Выбирает форму: `32` → `Montage32`, `64` → `Montage64`. | [nn_montage_draw.cpp:70](../../../src/freelancer/Client/nn_montage_draw.cpp#L70) |
| `texture` | int | `0` | Селектор атласа: `2` → `HUD_targetingelement`, `3` → `HUD_talkelement` (шаг плитки фиксирован `0.5`); любое другое значение выбирает форму по `size`. | [nn_montage_draw.cpp:81](../../../src/freelancer/Client/nn_montage_draw.cpp#L81) |
| `num_pages` | int | – | Читается и отбрасывается. | [nn_montage_draw.cpp:85](../../../src/freelancer/Client/nn_montage_draw.cpp#L85) |
| *любой другой ключ* | `<anything> [, scale]` | – | **Имя ключа — это имя плитки**: оно сопоставляется (без учёта регистра) со строкой `mesh` кнопки (или со строками мешей манёвра). Плитки назначаются ячейкам атласа в порядке следования в файле, слева направо, затем сверху вниз (после последнего ряда страница возвращается к началу). Первый параметр игнорируется; необязательный второй параметр — масштаб значка (по умолчанию `0`). | [nn_montage_draw.cpp:89](../../../src/freelancer/Client/nn_montage_draw.cpp#L89) |

Поскольку имя плитки должно совпадать с путём к мешу, `mesh = interface\...\example.3db` в разделе кнопки использует плитку монтажа с именем `interface\...\example.3db`, если такая существует.
