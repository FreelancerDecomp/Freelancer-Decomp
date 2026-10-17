# Схема сцены `.thn`

`.thn` — исполняемый Lua 3.2 chunk, который формирует глобальные `duration`, `entities` и `events`. Вход может быть исходным текстом или Lua bytecode; сохранение сцены выдаёт текст Lua. Интеграция VM, общий контекст загрузки, native объекты и фабрики описаны в [thorn.md](thorn.md).

## Бинарный заголовок

| Смещение | Содержимое |
|---|---|
| 0 | `0x1B` — ESC |
| 1–3 | ASCII `Lua` |
| 4 | версия `0x32` |
| 5 | представление чисел: `0` — текстовое, `8` — native `double` |
| 6–13, только при размере 8 | контрольное `double`, затем тело chunk |

Для native варианта контрольное число — `3.14159265358979323846e8`; проверка сравнивает его целую часть с `314159265`, а не требует побайтового равенства `double`. PC-вариант использует little endian. При нулевом размере контрольного `double` нет. Формат Lua 4/5 этим загрузчиком не принимается.

## Корень и время

```lua
duration = 5.0
entities = {
    { entity_name = "camera", type = CAMERA,
      spatialprops = { pos = {0, 0, 0},
        orient = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}} },
      cameraprops = { fovh = 60, hvaspect = 1.333333,
        nearplane = 0.1, farplane = 100000 } }
}
events = {
    { 0.0, SET_CAMERA, {"camera"} }
}
```

| Поле | Единица во входном Lua |
|---|---|
| корневой `duration` | секунды; native длительность — целые миллисекунды |
| первый элемент события | секунды; native время начала — целые миллисекунды |
| `duration` в свойствах события | секунды; native длительность — целые миллисекунды |
| `start_time` сущности SCENE/SUB_SCENE | секунды; native значение — миллисекунды |
| `pcurve_period`, ID, типы и флаги | числа без умножения на 1000 |

`start_time` внутри события — поле конкретного типа. Оно не является первым элементом события и не имеет общего для всех типов преобразования времени. `events` может отсутствовать; `entities` должна быть таблицей. Обход использует `foreach`, поэтому ключи таблиц не являются native handles сущностей или ID событий.

## Сущности

| ID типа | Имя |
|---|---|
| 0 | `UNKNOWN_ENTITY` |
| 1 | `COMPOUND` |
| 2 | `DEFORMABLE` |
| 3 | `CAMERA` |
| 4 | `MONITOR` |
| 5 | `LIGHT` |
| 6 | `SOUND` |
| 7 | `MARKER` |
| 8 | `HARDPOINT` |
| 9 | `SCENE` |
| 10 | `SUB_SCENE` |
| 11 | `MOTION_PATH` |
| 12 | `DELETED` |
| 13 | `PSYS` |

В общем Lua-пространстве имя `HARDPOINT` в конце регистрации получает значение `1` для `target_type`. Для сущности типа hardpoint нужен числовой `type = 8`; одноимённая константа не равна этому ID. Сущность `DELETED` не создаётся при загрузке.

| Общее поле | Форма |
|---|---|
| `entity_name` | имя, на которое ссылаются события |
| `type` | числовой ID из таблицы выше |
| `template_name` | строка; литерал `"NULL"` преобразуется в пустую строку |
| `flags` | числовая битовая маска |
| `lt_grp`, `srt_grp`, `usr_flg` | числовые значения, сохраняемые в байтовых полях |
| `spatialprops` | таблица позиции и ориентации |
| `userprops` | таблица пользовательских строковых свойств; числовое значение может быть преобразовано в строку |

`pos = {x, y, z}`. `orient = {{c0x, c0y, c0z}, {c1x, c1y, c1z}, {c2x, c2y, c2z}}` задаёт **три столбца** матрицы. Загрузка пространственных свойств сущности читает `pos` и `orient`; `q_orient` и `axisrot` относятся к пространственному событию.

| Тип | Собственные свойства |
|---|---|
| COMPOUND, DEFORMABLE | `compoundprops = { floor_height = число }` |
| CAMERA, MONITOR | `cameraprops = { fovh, hvaspect, nearplane, farplane }` |
| LIGHT | `lightprops` |
| SOUND | `audioprops`; `archetype` внутри неё может дать `template_name` |
| SCENE, SUB_SCENE | `start_time`, `ambient = {r,g,b}`, `up`, `front` и плоские fog-поля |
| MOTION_PATH | `pathprops = { path_type, path_data, control_pts }` |
| PSYS | `psysprops = { sparam = число }` |

Для CAMERA/MONITOR значения по умолчанию: `fovh = 60`, `hvaspect = 4/3`, `nearplane = 0.1`, `farplane = 100000`. Fog-поля SCENE/SUB_SCENE лежат прямо в таблице сущности, а не во вложенных `fogprops` или `reverbprops`.

## Таблицы свойств

| `lightprops` | Значение |
|---|---|
| `on`, `infinite` | `Y` / `N` |
| `color` | `{r,g,b}`, целые компоненты |
| `diffuse`, `specular`, `ambient`, `direction`, `atten` | три числовые компоненты |
| `range` | число |
| `cutoff`, `theta` | градусы; native light хранит радианы |
| `type` | `L_POINT = 1`, `L_SPOT = 2`, `L_DIRECT = 3` |

`audioprops` содержит `attenuation`, `pan`, `dmin`, `dmax`, `ain`, `aout`, `atout`, `rmix`. `ain` и `aout` — целые; остальные параметры числовые. Дополнительный `archetype` используется при загрузке SOUND.

| Fog-поле | Значение |
|---|---|
| `fogon`, `fogtable` | `Y` / `N` |
| `fogcolor` | `{r,g,b}` |
| `fogmode` | `F_NONE = 0`, `F_EXP = 1`, `F_EXP2 = 2`, `F_LINEAR = 3` |
| `fogstart`, `fogend`, `fogdensity` | числа |

`reverbprops` содержит целый `env` и числовые `vol`, `decay`, `damping`. `psysprops` содержит числовой `sparam`. В property-animation событии наличие поля задаёт соответствующую маску изменения; таблица не обязана содержать все параметры.

## События

Событие имеет форму `{ начало, тип, {"primary", "secondary"}, свойства }`. Начало и тип находятся по индексам 1 и 2; таблица имён — по индексу 3, необязательная таблица свойств — по индексу 4. Имена разрешаются после создания сущностей.

Общие свойства: `duration`, `flags`, `event_flags`, `param_curve`, `pcurve_period`. Отсутствующий `duration` внутри таблицы свойств устанавливает `USE_SCRIPT_DURATION`; явно заданный `flags` затем заменяет эту маску.

| ID | Имя типа | Собственные ключи свойств |
|---|---|---|
| 0 | `UNDEFINED_EVENT` | событие не создаётся |
| 1 | `USER_EVENT` | `user_event_string` |
| 2 | `SET_CAMERA` | нет собственных ключей |
| 3 | `START_SOUND` | `start_time` |
| 4 | `START_LIGHT_PROP_ANIM` | `lightprops` |
| 5 | `START_CAMERA_PROP_ANIM` | `cameraprops = { fovh, aspect, near, far }` |
| 6 | `START_PATH_ANIMATION` | `start_percent`/`percent1`, `stop_percent`/`percent2`, `offset`, `up`, `front` |
| 7 | `START_SPATIAL_PROP_ANIM` | `spatialprops`, `target_part`, `target_type` |
| 8 | `ATTACH_ENTITY` | `offset`, `up`, `front`, `target_part`, `target_type` |
| 9 | `CONNECT_HARDPOINTS` | `hardpoint`, `parent_hardpoint` |
| 10 | `START_MOTION` | `animation`, `locked_bone`, `trans_time`, `time_scale`, `weight`, `loop`, `heading`, `trans_scale`, `start_time` |
| 11 | `START_IK` | `end_effector`, `count_to_root`, `ik_id`, `target_part`, `target_type`, `damping`, `point_at`, `move_to`, `transition_duration`, `up`, `front` |
| 12 | `START_SUB_SCENE` | `start_time` |
| 13 | `START_PSYS` | нет собственных ключей |
| 14 | `START_PSYS_PROP_ANIM` | `psysprops` |
| 15 | `START_AUDIO_PROP_ANIM` | `audioprops` |
| 16 | `START_FOG_PROP_ANIM` | `fogprops` |
| 17 | `START_REVERB_PROP_ANIM` | `reverbprops` |
| 18 | `START_FLR_HEIGHT_ANIM` | `floor_height`, `target_part`, `target_type` прямо в свойствах события |
| 19 | `SUBTITLE` | `strid` |

Перед созданием событие типа 6 для primary entity типа 1–8 превращается в `ATTACH_ENTITY`. Обобщённый `START = 23` превращается в `START_SOUND` для SOUND и в `START_MOTION` для остальных типов. `PROPERTY_ANIM = 0` не обозначает этот обобщённый START.

В `spatialprops` события допустимы `pos`, `axisrot = {угол, ось}`, `q_orient = {x,y,z,w}` и `orient` из трёх столбцов. При наличии `q_orient` матричный `orient` не читается. Для целей используются `ROOT = 0`, `HARDPOINT = 1`, `PART = 2`.

## Оси и флаги

Оси: `X_AXIS = 0`, `Y_AXIS = 1`, `Z_AXIS = 2`, `NEG_X_AXIS = 3`, `NEG_Y_AXIS = 4`, `NEG_Z_AXIS = 5`. Значения `Y = 1`, `N = 0` — числовые константы.

| Константа | Значение | Контекст |
|---|---|---|
| `REFERENCE`, `HIDDEN` | 1, 16 | сущности |
| `LIT_AMBIENT`, `LIT_DYNAMIC` | 2, 4 | COMPOUND, DEFORMABLE, PSYS |
| `FOG_PROPS_REMOVED` | 32 | SCENE |
| `SPATIAL`, `STREAM`, `LOOP` | 2, 4, 8 | SOUND и START_SOUND |
| `USE_SCRIPT_DURATION` | 1 | события |
| `POSITION`, `PATH_POSITION`, `ORIENTATION`, `LOOK_AT` | 2, 2, 4, 8 | пространственные/path/attach события |
| `ENTITY_RELATIVE`, `PARENT_CHILD`, `ORIENTATION_RELATIVE` | 32, 64, 128 | path/attach |

Прочие зарегистрированные значения: `ADD_PATH = 6`, `LOOKAT_ENTITY = 8`, `STOP = 21`, `STOP_IK = 22`. Значение маски зависит от контекста; совпадение числа у двух имён не делает их взаимозаменяемыми.

## Кривые и пути

```lua
param_curve = {
    CLSID = "CatmullRomPCurve",
    points = { {a, b, c, d}, {a2, b2, c2, d2} }
}
```

Каждая точка содержит четыре числа. Формат не именует их компоненты, поэтому переименовывать последние два в предполагаемые касательные нельзя. `CLSID` выбирает DACOM-фабрику по имени, а не display-имя кривой.

Имена фабрик кривых: `CatmullRomPCurve`, `ThornParamCurve`, `LinearPCurve`, `ThornLPCurve`, `SmoothPCurve`, `StepPCurve`, `RampUpPCurve`, `RampDownPCurve`, `BumpOutPCurve`, `BumpInPCurve`, `FreeFormPCurve`.

Для MOTION_PATH явно заданный `path_type` выбирает фабрику; `path_data` загружается через её `IPersistString`. Без `path_type` выбирается `CV_CRSplinePath`, а `control_pts = {{x,y,z}, ...}` передаются spline-интерфейсу. Эти две ветви загрузки не объединяются. Для ориентационного пути существует фабрика `CV_CROrientationSplinePath`; формат `path_data` определяется выбранным объектом.

## Асимметрии загрузки и сохранения

- `SET_CAMERA` сохраняется без четвёртой таблицы свойств.
- Загрузка читает CAMERA и MONITOR `cameraprops`, сохранение пишет их только для CAMERA. Пространственный блок MONITOR и HARDPOINT при сохранении пропускается.
- `compoundprops` читается для COMPOUND и DEFORMABLE, а сохраняется только для DEFORMABLE.
- Камера сущности использует `hvaspect`/`nearplane`/`farplane`; событие камеры — `aspect`/`near`/`far`.
- `percent1`/`percent2` — алиасы при загрузке; сохранение пишет `start_percent`/`stop_percent`.
- `ik_id` читается при загрузке IK, но не записывается в текст сцены.
- `userprops` сохраняется из той же native коллекции строк, которой пользуются операции чтения и записи свойства.
- Диагностические длинные имена событий, например `START_LIGHT_PROPERTY_ANIM`, не регистрируются как Lua-глобалы вместо кратких имён из таблицы выше.

Чтение схемы находится в [ThornLua.cpp](../../../src/Thorn/ThornLua.cpp), текстовый writer — в [ThornScene.cpp](../../../src/Thorn/ThornScene.cpp). Таблицы здесь описывают формат данных; они не являются отчётом о завершённости его обработчиков.
