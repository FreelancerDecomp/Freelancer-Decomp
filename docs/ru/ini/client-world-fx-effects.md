# Клиентские визуальные эффекты, анимации света, эффекты врат и прыжков и fuse

Эта страница описывает клиентские конфигурационные файлы эффектов: файлы архивов эффектов, названные в `[Data] effects` (`[Effect]`, `[VisEffect]`, `[BeamSpear]`, `[BeamBolt]`, `[EffectType]`, `[EffectLOD]`), `fx\lightanim.ini`, файл туннелей врат (`[Data] gate_tunnels`), файл эффекта прыжка (`[Data] jump_effect`) и файлы fuse (`[Data] fuses`) в том виде, как их читает клиент. Читалки файлов систем, звёзд, астероидов и туманностей описаны в [client-world-fx.md](client-world-fx.md); клиентские ключи записей архетипов (которые ссылаются на эффекты по nickname) — в [client-world-fx-archetypes.md](client-world-fx-archetypes.md).

**Расположение файла / как он находится.** Все файлы разрешаются относительно пути данных (`GetFileSysPath(.., nullptr)`, см. [freelancer-ini.md](freelancer-ini.md)). Какой файл читать, определяет `freelancer.ini [Data]`:

| Ключ `[Data]` | Кто читает | Когда |
|---|---|---|
| `effects` (повторяемый) | `EffectArchLoadIni(path, false)` ([vis_effect_arch.cpp:775](../../../src/freelancer/Client/vis_effect_arch.cpp#L775)) через `LoadClientDataValue` ([dataconfig_progress.cpp:129](../../../src/freelancer/Client/dataconfig_progress.cpp#L129)) | Запуск клиента, в порядке следования ключей в `[Data]`. |
| `effect_shapes` | ставится в очередь, загружается в библиотеку форм эффектов (формат в [client-ui-config.md](client-ui-config.md)) | Конец инициализации движка; имена форм слоёв туннелей врат ищутся там. |
| `gate_tunnels` | `GateTunnelIniLoad` ([gate_tunnel.cpp:1048](../../../src/freelancer/Client/gate_tunnel.cpp#L1048)); значение захватывает `LoadStartupConfig` ([startup_config.cpp:128](../../../src/freelancer/Client/startup_config.cpp#L128)) | При каждой загрузке системы игрока (вход в космос), до чтения файла системы ([space_state.cpp:927](../../../src/freelancer/Client/space_state.cpp#L927)). |
| `jump_effect` | `JumpEffectIniLoad` ([jump_effect.cpp:220](../../../src/freelancer/Client/jump_effect.cpp#L220)); значение захватывается так же | При каждом входе в систему, до чтения файла системы ([space_scene_load.cpp:24](../../../src/freelancer/Client/space_scene_load.cpp#L24)). |
| `fuses` (повторяемый) | ставится в очередь Common (`FuseDB::AddFuseINIFile`), загружается `ClientFuseDB::LoadAll` ([client_fuse.cpp:941](../../../src/freelancer/Client/client_fuse.cpp#L941)); сервер читает те же файлы ([server.md](server.md)) | При каждой загрузке системы игрока (вход в космос), до чтения файла системы ([space_state.cpp:925](../../../src/freelancer/Client/space_state.cpp#L925)). |
| (литерал) `fx\lightanim.ini` | `LightAnimationLoad` ([engine_light.cpp:1387](../../../src/freelancer/Client/engine_light.cpp#L1387)) | При каждой загрузке системы игрока (вход в космос), до чтения файла системы ([space_state.cpp:922](../../../src/freelancer/Client/space_state.cpp#L922)). |

Файл, который не открывается, всеми этими загрузчиками молча пропускается (без сообщений). Имена заголовков и ключей нечувствительны к регистру; обозначения (`str`, `id`, `float`, ...) — те, что объяснены в [client-world-fx.md](client-world-fx.md#обозначения).

## 1. Файлы архивов эффектов (`[Data] effects`)

`EffectArchLoadIni` читает один файл как плоский список секций в порядке файла и диспетчеризует по заголовку; неизвестные заголовки пропускаются. Все ссылки между секциями разрешаются **в момент чтения ключа**, поэтому цель должна быть уже загружена: более ранняя секция того же файла или более ранний файл `effects` (а для `snd_effect` — более ранний файл `sounds` в `[Data]`). Поэтому `[VisEffect]` или beam должны стоять раньше `[Effect]`, который их называет.

| Заголовок | Читалка | Назначение |
|---|---|---|
| `[Effect]` | `LoadEffectSection` ([L658](../../../src/freelancer/Client/vis_effect_arch.cpp#L658)) | Архетип эффекта: связывает визуальный эффект, звук и свет под одним nickname. Именно на этот nickname ссылаются оружие, двигатели, взрывы, fuse и т. д. |
| `[VisEffect]` | `VisEffectLoadSection` ([L727](../../../src/freelancer/Client/vis_effect_arch.cpp#L727)) | Визуальный эффект частиц/alchemy и его библиотеки текстур. |
| `[BeamSpear]`, `[BeamBolt]` | `LoadBeamSection` ([L359](../../../src/freelancer/Client/vis_effect_arch.cpp#L359)) | Внешний вид луча (следы снарядов). |
| `[EffectType]` | `LoadEffectTypeSection` ([effect_type_info.cpp:125](../../../src/freelancer/Client/effect_type_info.cpp#L125)) | Переопределяет встроенные параметры по типам (приоритет, LOD, радиус, ...). |
| `[EffectLOD]` | `LoadEffectLodSection` ([effect_type_info.cpp:225](../../../src/freelancer/Client/effect_type_info.cpp#L225)) | Пороги экранного размера для классов LOD. |

### 1.1 `[Effect]`

[EffectArchParseValue, vis_effect_arch.cpp:401](../../../src/freelancer/Client/vis_effect_arch.cpp#L401). Реестр индексируется по `nickname`. Уже существующий nickname пишет в лог «[Effect] nickname = %s value is not unique. Aborted second version.», и вторая секция отбрасывается (путь перезагрузки/слияния загрузчик данных никогда не выбирает). Неизвестные ключи игнорируются без сообщения. У `nickname` нет особой позиции, но эффект без него регистрируется под id 0.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | id (`str`) | 0 | Ключ реестра. | [L404](../../../src/freelancer/Client/vis_effect_arch.cpp#L404) |
| `effect_type` | str (всё значение), точное совпадение | `EFT_NONE` (индекс 0) | Одно из встроенных имён типов из таблицы ниже, сравнивается через `strcmp` **с учётом регистра**. Неизвестное имя пишет в лог ошибку «unknown effect type» (плюс сообщение-ссылку `INI_Reader`), и эффект сохраняется с недопустимым индексом 47. Если `run_time` типа отрицателен, бит 0 флагов эффекта устанавливается (иначе сбрасывается). | [L410](../../../src/freelancer/Client/vis_effect_arch.cpp#L410) |
| `vis_effect` | str (всё значение) | нет | Nickname уже загруженного `[VisEffect]`. Промах пишет в лог «[Effect] %s couldn't find [VisEffect] %s.» | [L442](../../../src/freelancer/Client/vis_effect_arch.cpp#L442) |
| `vis_generic` | str (всё значение) | нет | Второй `[VisEffect]`, «generic»-вариант: заменяет `vis_effect`, когда уровень производительности generic-эффектов выше `priority` типа, но не выше его `generic_priority` ([particle_effect.cpp:283](../../../src/freelancer/Client/particle_effect.cpp#L283)). Тот же поиск и то же сообщение, что у `vis_effect`. | [L457](../../../src/freelancer/Client/vis_effect_arch.cpp#L457) |
| `vis_beam` | str (всё значение) | нет | Nickname уже загруженного `[BeamSpear]`/`[BeamBolt]`. Промах пишет в лог «[Effect] %s couldn't find [VisBeam] %s.» | [L469](../../../src/freelancer/Client/vis_effect_arch.cpp#L469) |
| `snd_effect` | id (`str`) | нет | Nickname архетипа звука. Промах пишет в лог «[Effect] %s couldn't find [Sound] %s.» | [L481](../../../src/freelancer/Client/vis_effect_arch.cpp#L481) |
| `lgt_effect` | str (всё значение), макс. 31 символ | пусто | Имя анимации света (`[LightAnim]` `name`, раздел 2), проигрываемой вместе с эффектом. Имя ищется при запуске эффекта, а не при загрузке ([particle_effect.cpp:1093](../../../src/freelancer/Client/particle_effect.cpp#L1093)). | [L497](../../../src/freelancer/Client/vis_effect_arch.cpp#L497) |
| `lgt_range_scale` | float | 1.0 | Масштаб радиуса света. | [L515](../../../src/freelancer/Client/vis_effect_arch.cpp#L515) |
| `lgt_time_scale` | float | 1.0 | Масштаб времени анимации света. | [L520](../../../src/freelancer/Client/vis_effect_arch.cpp#L520) |
| `lgt_radius` | float | 0 | Радиус света. | [L525](../../../src/freelancer/Client/vis_effect_arch.cpp#L525) |
| `lgt_color` | byte x3 (`float[0..2]`) | `255, 255, 255` | RGB света, 0..255 (младшие 8 бит усечённого значения). | [L531](../../../src/freelancer/Client/vis_effect_arch.cpp#L531) |

Встроенные имена типов эффектов (47; индекс = позиция в этом списке): `EFT_NONE`, `EFT_EXPLOSION_SMALL`, `EFT_EXPLOSION_MEDIUM`, `EFT_EXPLOSION_LARGE`, `EFT_EXPLOSION_DEBRIS`, `EFT_EXPLOSION_SHIP`, `EFT_EXPLOSION_MISSILE`, `EFT_EXPLOSION_MINE`, `EFT_MUZZLE_FLASH`, `EFT_MUZZLE_LARGE_FLASH`, `EFT_MISSILE_DRIVE`, `EFT_WEAPON_PROJ`, `EFT_WEAPON_LARGE_PROJ`, `EFT_WEAPON_IMPACT`, `EFT_WEAPON_LARGE_IMPACT`, `EFT_WEAPON_COUNTER_MEASURE`, `EFT_WEAPON_MINE`, `EFT_DAMAGE_HULL`, `EFT_DAMAGE_LARGE_SHIELD`, `EFT_DAMAGE_SHIELD`, `EFT_DAMAGE_CRUISE`, `EFT_DAMAGE_SMOKE`, `EFT_DAMAGE_LARGE_SMOKE`, `EFT_DEBRIS_TRAIL`, `EFT_ENGINE_CONTRAIL`, `EFT_ENGINE_TRAIL`, `EFT_ENGINE_LARGE_TRAIL`, `EFT_ENGINE_FIRE`, `EFT_ENGINE_LARGE_FIRE`, `EFT_ENGINE_THRUSTER`, `EFT_EQUIP_CLOAK`, `EFT_EQUIP_LARGE_CLOAK`, `EFT_EQUIP_ATTACHED`, `EFT_EQUIP_ATTACHED_LARGE`, `EFT_EQUIP_ATTACHED_INSANELY_LARGE`, `EFT_MISC_DUST`, `EFT_MOTION_DUST`, `EFT_JUMP_EFFECTS`, `EFT_JUMP_GATE_EFFECTS`, `EFT_LANE_ACTIVE`, `EFT_LANE_TUBE`, `EFT_LANE_SHIP`, `EFT_LANE_SHIP_TRAVEL`, `EFT_CUSTOM_EFFECTS`, `EFT_CODE_PROJ_EFFECT`, `EFT_TRACTORBEAM`, `EFT_FLASHLIGHT` ([effect_type_info.cpp:38](../../../src/freelancer/Client/effect_type_info.cpp#L38)). Числовые значения по умолчанию для каждого типа лежат в этой таблице, а не в файлах данных.

Пример (синтетический):

```ini
[Effect]
nickname = demo_muzzle
effect_type = EFT_MUZZLE_FLASH
vis_effect = demo_muzzle_vis
snd_effect = demo_muzzle_snd
lgt_effect = demo_flash_light
lgt_color = 255, 220, 160
lgt_radius = 12
```

### 1.2 `[VisEffect]`

[VisEffectLoadSection, L727](../../../src/freelancer/Client/vis_effect_arch.cpp#L727). Секция индексируется по своему **первому `nickname`**; ключи перед ним отбрасываются. Записи ищутся без учёта регистра, а существующая запись **изменяется, а не заменяется**: вторая секция с тем же nickname перезаписывает скалярные ключи и дописывает дальнейшие `textures`. Неизвестные ключи игнорируются.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | str (всё значение) | - | Ключ записи (без учёта регистра). | [L734](../../../src/freelancer/Client/vis_effect_arch.cpp#L734) |
| `effect_crc` | int (`atoi`) | 0 | Сохраняется без изменений (имя намекает на CRC alchemy-эффекта, который он проигрывает; загрузчик его не проверяет). | [L747](../../../src/freelancer/Client/vis_effect_arch.cpp#L747) |
| `has_alpha` | bool | false | Эффект использует альфа-смешение. | [L752](../../../src/freelancer/Client/vis_effect_arch.cpp#L752) |
| `alchemy` | str (всё значение), макс. 259 символов | пусто | Путь к библиотеке alchemy-эффектов (`.ale`), из которой берётся эффект. | [L757](../../../src/freelancer/Client/vis_effect_arch.cpp#L757) |
| `textures` | str (всё значение) | - | Одно имя библиотеки текстур на ключ; повторяемый, имена хранятся по порядку. Каждое разрешается при первой загрузке эффекта; то, что загрузить нельзя, пишет в лог «[VisEffect] ...: Failed to load texture library: %s. Removing from txm list.» и отбрасывается. | [L762](../../../src/freelancer/Client/vis_effect_arch.cpp#L762), [L129](../../../src/freelancer/Client/vis_effect_arch.cpp#L129) |

### 1.3 `[BeamSpear]` и `[BeamBolt]`

Оба заголовка используют [LoadBeamSection, L359](../../../src/freelancer/Client/vis_effect_arch.cpp#L359); заголовок определяет вид (`BeamSpear` = 0, `BeamBolt` = 1). Ключи перед первым `nickname` отбрасываются; уже существующий nickname (без учёта регистра) обновляет существующую запись. Все числовые поля начинаются с 1.0, два режима текстур — с 0. Когда секция заканчивается, значения корректируются: `tip_length` поднимается минимум до `core_width`; `tail_length` минимум до `core_width` для spear и минимум до `sec_core_width` для bolt; `head_width` минимум до `core_width`; `head_brightness` и `trail_brightness` ограничиваются диапазоном `0.01..1.0` ([L184](../../../src/freelancer/Client/vis_effect_arch.cpp#L184)). Неизвестные ключи игнорируются.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | str (всё значение) | - | Ключ записи; на него ссылается `vis_beam` секции `[Effect]`. | [L365](../../../src/freelancer/Client/vis_effect_arch.cpp#L365) |
| `tip_color` | float x3 (0..255, /255) | `1, 1, 1` | Цвет. | [L235](../../../src/freelancer/Client/vis_effect_arch.cpp#L235) |
| `tail_color` | float x3 (/255) | `1, 1, 1` | Цвет. | [L241](../../../src/freelancer/Client/vis_effect_arch.cpp#L241) |
| `core_color` | float x3 (/255) | `1, 1, 1` | Цвет. | [L247](../../../src/freelancer/Client/vis_effect_arch.cpp#L247) |
| `sec_core_color` | float x3 (/255) | `1, 1, 1` | Цвет. | [L253](../../../src/freelancer/Client/vis_effect_arch.cpp#L253) |
| `outter_color` | float x3 (/255) | `1, 1, 1` | Цвет (ключ пишется с двумя `t`). | [L259](../../../src/freelancer/Client/vis_effect_arch.cpp#L259) |
| `sec_outter_color` | float x3 (/255) | `1, 1, 1` | Цвет (две `t`). | [L265](../../../src/freelancer/Client/vis_effect_arch.cpp#L265) |
| `tip_length` | float | 1.0 | Длина; корректируется, как описано выше. | [L271](../../../src/freelancer/Client/vis_effect_arch.cpp#L271) |
| `tail_length` | float | 1.0 | Длина; корректируется, как описано выше. | [L276](../../../src/freelancer/Client/vis_effect_arch.cpp#L276) |
| `core_length` | float | 1.0 | Длина. | [L281](../../../src/freelancer/Client/vis_effect_arch.cpp#L281) |
| `core_width` | float | 1.0 | Ширина; нижняя граница для `tip_length`, `tail_length` (spear) и `head_width`. | [L286](../../../src/freelancer/Client/vis_effect_arch.cpp#L286) |
| `sec_core_width` | float | 1.0 | Ширина; нижняя граница для `tail_length` у bolt. | [L291](../../../src/freelancer/Client/vis_effect_arch.cpp#L291) |
| `head_brightness` | float | 1.0 | Ограничивается диапазоном `0.01..1.0`. | [L296](../../../src/freelancer/Client/vis_effect_arch.cpp#L296) |
| `trail_brightness` | float | 1.0 | Ограничивается диапазоном `0.01..1.0`. | [L301](../../../src/freelancer/Client/vis_effect_arch.cpp#L301) |
| `flash_size` | float | 1.0 | Размер вспышки выстрела. | [L306](../../../src/freelancer/Client/vis_effect_arch.cpp#L306) |
| `head_width` | float | 1.0 | Ширина головы; корректируется, как описано выше. | [L311](../../../src/freelancer/Client/vis_effect_arch.cpp#L311) |
| `head_texture` | enum | режим 0 | Ровно `ball` выбирает режим 1; `star` или любой другой текст выбирает режим 0 (с учётом регистра). | [L316](../../../src/freelancer/Client/vis_effect_arch.cpp#L316) |
| `trail_texture` | enum | режим 0 | Ровно `thin` выбирает режим 1; `wide` или любой другой текст выбирает режим 0 (с учётом регистра). | [L326](../../../src/freelancer/Client/vis_effect_arch.cpp#L326) |

### 1.4 `[EffectType]`

[LoadEffectTypeSection, effect_type_info.cpp:125](../../../src/freelancer/Client/effect_type_info.cpp#L125). Правит одну строку встроенной таблицы типов. `nickname` выбирает строку (точное совпадение с учётом регистра с 47 именами из раздела 1.1); неизвестное имя пишет в лог предупреждение «EffectTypes: unknown type», а следующие ключи **продолжают изменять предыдущую строку** (или игнорируются, если её не было). Ключи перед первым допустимым `nickname` игнорируются. Это единственная читалка на этой странице, которая сообщает о неизвестном ключе: она пишет в лог ошибку «EffectType (%s) has unknown parameter (%s)».

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | str (всё значение) | - | Имя типа (см. 1.1). | [L136](../../../src/freelancer/Client/effect_type_info.cpp#L136) |
| `priority` | float | по типу | Эффект создаётся с полной детализацией, пока уровень производительности generic-эффектов (опция `generic_effect_perf`) не превышает это значение. | [L154](../../../src/freelancer/Client/effect_type_info.cpp#L154) |
| `generic_priority` | float | по типу | Верхний уровень, до которого вместо этого используется вариант `vis_generic` (см. `vis_generic`). | [L156](../../../src/freelancer/Client/effect_type_info.cpp#L156) |
| `lod_type` | enum | по типу | `EFT_LOD_TRAIL`, `EFT_LOD_WEAPON`, `EFT_LOD_SMALL`, `EFT_LOD_MEDIUM`, `EFT_LOD_LARGE` или `EFT_LOD_SMALL_DISTANT` (без учёта регистра). `EFT_LOD_NONE` не принимается; любой другой текст оставляет значение без изменений и без сообщения. | [L158](../../../src/freelancer/Client/effect_type_info.cpp#L158) |
| `radius` | float | по типу | Радиус эффекта, также используется при вычислении LOD по экранному размеру. Отрицательное значение пишет в лог «effect type (%s) has negative radius» и заменяется на 1.0. | [L171](../../../src/freelancer/Client/effect_type_info.cpp#L171) |
| `visibility` | enum | по типу | `EXIST_OFFSCREEN` (без учёта регистра) = эффект существует за экраном; любой другой текст = не существует. | [L184](../../../src/freelancer/Client/effect_type_info.cpp#L184) |
| `update` | enum | по типу | `UPDATE_OFFSCREEN` (без учёта регистра) = обновляется за экраном; любой другой текст = нет. | [L190](../../../src/freelancer/Client/effect_type_info.cpp#L190) |
| `run_time` | float | по типу | Время работы в секундах; отрицательное значение помечает непрерывный эффект. | [L196](../../../src/freelancer/Client/effect_type_info.cpp#L196) |
| `fps` | float | по типу | Частота обновления. | [L198](../../../src/freelancer/Client/effect_type_info.cpp#L198) |
| `pbubble` | float x2 | по типу | Пузырь дистанции видимости: рабочая дистанция интерполируется от первого значения (полная детализация эффектов) ко второму (наименьшая детализация) по настройке производительности эффектов. Far меньше near пишет в лог предупреждение «pbubble ranges backwards», но значение сохраняется. | [L200](../../../src/freelancer/Client/effect_type_info.cpp#L200) |

### 1.5 `[EffectLOD]`

[LoadEffectLodSection, effect_type_info.cpp:225](../../../src/freelancer/Client/effect_type_info.cpp#L225). **Первым ключом должен быть `type`**; если его нет, секция игнорируется без сообщения.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `type` | enum | - | `lod_type_trail`, `lod_type_weapon`, `lod_type_small`, `lod_type_medium`, `lod_type_large` или `lod_type_small_distant` (без учёта регистра). Всё остальное пишет в лог «invalid LOD type (%s)» и отбрасывает секцию. | [L229](../../../src/freelancer/Client/effect_type_info.cpp#L229) |
| `max_lod_screen_size` | float | -1.0 | Экранный размер (радиус / дистанция, масштабируется камерой), при котором и выше которого эффект имеет полное смешивание LOD 1.0. Между `min_lod_screen_size` и этим значением смешивание растёт линейно. | [L255](../../../src/freelancer/Client/effect_type_info.cpp#L255) |
| `min_lod_screen_size` | float | -1.0 | Экранный размер, при котором смешивание LOD равно 0. | [L257](../../../src/freelancer/Client/effect_type_info.cpp#L257) |
| `min_screen_size` | float | -1.0 | Экранный размер, при котором и ниже которого эффект не рисуется вообще. | [L259](../../../src/freelancer/Client/effect_type_info.cpp#L259) |

Ключи `[EffectType]`/`[EffectLOD]` также описаны в блоке, которому принадлежит `effect_type_info.cpp`; эта таблица сверена с тем же кодом.

## 2. Анимации света: `fx\lightanim.ini`

[LightAnimationLoad, engine_light.cpp:1387](../../../src/freelancer/Client/engine_light.cpp#L1387). Каждая секция `[LightAnim]` — одна анимация; прочие заголовки пропускаются. Анимации ищутся по имени (без учёта регистра) по ключу лаунчера `light_anim` ([client-world-fx-archetypes.md](client-world-fx-archetypes.md)) и по `lgt_effect` секции `[Effect]`. Секция без кадров всё равно регистрируется.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `name` | str (всё значение), макс. 31 символ | пусто | Имя анимации. | [L1403](../../../src/freelancer/Client/engine_light.cpp#L1403) |
| `frame` | float, byte x3, float | - | `frame = <time>, <r>, <g>, <b>, <intensity>`. Повторяемый; кадры хранятся в порядке файла, загрузчик не сортирует и не проверяет времена. `r`, `g`, `b` — 0..255 (младшие 8 бит усечённого значения). `intensity` 0 или меньше заменяется на 0.01. Последний кадр секции помечается как конечный. | [L1416](../../../src/freelancer/Client/engine_light.cpp#L1416) |

Пример (синтетический):

```ini
[LightAnim]
name = demo_flash_light
frame = 0.0, 255, 240, 200, 1.0
frame = 0.15, 255, 120, 40, 0.6
frame = 0.4, 0, 0, 0, 0
```

## 3. Туннели врат (`[Data] gate_tunnels`)

[GateTunnelIniLoad, gate_tunnel.cpp:1048](../../../src/freelancer/Client/gate_tunnel.cpp#L1048). Каждая секция `[gate_tunnel]` создаёт туннель; следующие за ней секции `[layer]` (не более двух) относятся к последнему туннелю. Туннель ищется по его `nickname` из `jump_tunnel` эффекта прыжковых врат (раздел 4). Неизвестные ключи игнорируются. `[layer]` перед любым `[gate_tunnel]` не защищён (поведение retail).

### 3.1 `[gate_tunnel]`

Если `num_spline_control_points` больше 0, случайные пути (радиус, вращение, смещения цвета, яркость) генерируются один раз по окончании секции.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | id (`str`) | id от `default` | Ключ туннеля. | [L1065](../../../src/freelancer/Client/gate_tunnel.cpp#L1065) |
| `write_depth_buffer` | float, проверяется на 0 | включено | Ровно 0 отключает запись глубины; всё остальное оставляет её включённой. | [L1067](../../../src/freelancer/Client/gate_tunnel.cpp#L1067) |
| `num_spline_control_points` | float, усекается до int | 11 | Число случайных контрольных точек пути туннеля. 0 или меньше пропускает генерацию пути. | [L1072](../../../src/freelancer/Client/gate_tunnel.cpp#L1072) |
| `x_range` | float | 0 | Контрольные точки берутся из `-x_range/2 .. +x_range/2` вбок. | [L1074](../../../src/freelancer/Client/gate_tunnel.cpp#L1074) |
| `y_range` | float | 0 | То же по вертикали. | [L1076](../../../src/freelancer/Client/gate_tunnel.cpp#L1076) |
| `z_range` | float | 0 | Длина: контрольная точка `i` лежит в `-(i * z_range / count) - 5` вдоль оси. | [L1078](../../../src/freelancer/Client/gate_tunnel.cpp#L1078) |
| `min_radius` | float | 50 | Нижняя граница случайного радиуса кольца на контрольную точку. | [L1080](../../../src/freelancer/Client/gate_tunnel.cpp#L1080) |
| `max_radius` | float | 50 | Верхняя граница. | [L1082](../../../src/freelancer/Client/gate_tunnel.cpp#L1082) |
| `far_radius_factor` | float | 1.0 | Радиус смешивается к `radius * factor` по мере удаления. | [L1084](../../../src/freelancer/Client/gate_tunnel.cpp#L1084) |
| `min_speed` | float | 3e-5 | Скорость движения в момент времени 0. | [L1086](../../../src/freelancer/Client/gate_tunnel.cpp#L1086) |
| `max_speed` | float | 0.06 | Скорость, достигаемая через `time_to_max_speed`. | [L1088](../../../src/freelancer/Client/gate_tunnel.cpp#L1088) |
| `time_to_max_speed` | float | 8 | Секунды разгона от `min_speed` до `max_speed`. | [L1090](../../../src/freelancer/Client/gate_tunnel.cpp#L1090) |
| `num_t_steps` | float | 200 | Число подразделений вдоль длины туннеля. | [L1092](../../../src/freelancer/Client/gate_tunnel.cpp#L1092) |
| `num_s_steps` | float | 12 | Число сегментов по окружности. | [L1094](../../../src/freelancer/Client/gate_tunnel.cpp#L1094) |
| `min_rotation` | float | -pi | Нижняя граница случайного закручивания на контрольную точку (радианы). | [L1096](../../../src/freelancer/Client/gate_tunnel.cpp#L1096) |
| `max_rotation` | float | +pi | Верхняя граница. | [L1098](../../../src/freelancer/Client/gate_tunnel.cpp#L1098) |
| `min_rgb` | int x3 (0..255, /255) | `0, 0, 0` | Нижняя граница случайного смещения цвета. Читается через `get_value_int`. | [L1100](../../../src/freelancer/Client/gate_tunnel.cpp#L1100) |
| `max_rgb` | int x3 (0..255, /255) | `0, 0, 0` | Верхняя граница. | [L1113](../../../src/freelancer/Client/gate_tunnel.cpp#L1113) |

### 3.2 `[layer]`

Третий слой того же туннеля пишет в лог «more than two layers specified in gate tunnel %s» и пропускается.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `color` | int x3 (0..255, /255) | `0.5, 0.5, 0.5` | Цвет слоя. | [L1172](../../../src/freelancer/Client/gate_tunnel.cpp#L1172) |
| `near_alpha` | float | 1.0 | Альфа на ближнем конце. | [L1184](../../../src/freelancer/Client/gate_tunnel.cpp#L1184) |
| `far_alpha` | float | 0 | Альфа на дальнем конце. | [L1186](../../../src/freelancer/Client/gate_tunnel.cpp#L1186) |
| `radius_factor` | float | 1.0 | Множитель радиуса туннеля для этого слоя. | [L1188](../../../src/freelancer/Client/gate_tunnel.cpp#L1188) |
| `u_offset` | float | 0 | Начальное смещение текстуры по U. | [L1190](../../../src/freelancer/Client/gate_tunnel.cpp#L1190) |
| `v_offset` | float | 0 | Начальное смещение текстуры по V. | [L1192](../../../src/freelancer/Client/gate_tunnel.cpp#L1192) |
| `du` | float | 0.09 | Прокрутка текстуры в секунду по U. | [L1194](../../../src/freelancer/Client/gate_tunnel.cpp#L1194) |
| `dv` | float | 0.07 | Прокрутка текстуры в секунду по V. | [L1196](../../../src/freelancer/Client/gate_tunnel.cpp#L1196) |
| `v_scale` | float | 10 | Число повторов текстуры вдоль длины туннеля. | [L1198](../../../src/freelancer/Client/gate_tunnel.cpp#L1198) |
| `texture` | str (всё значение) | нет | Имя формы в библиотеке форм эффектов (`[Data] effect_shapes`). Промах пишет в лог предупреждение библиотеки и «Couldn't find Effect shape %s for the Gate Tunnel»; тогда у слоя нет текстуры. | [L1200](../../../src/freelancer/Client/gate_tunnel.cpp#L1200) |

Пример (синтетический):

```ini
[gate_tunnel]
nickname = demo_tunnel
num_spline_control_points = 12
z_range = 800
min_radius = 40
max_radius = 60
min_rgb = 20, 20, 60
max_rgb = 80, 60, 160

[layer]
color = 200, 200, 255
texture = demo_tunnel_shape
```

## 4. Эффекты прыжка (`[Data] jump_effect`)

[JumpEffectIniLoad, jump_effect.cpp:220](../../../src/freelancer/Client/jump_effect.cpp#L220). Два вида секций. Дескриптор выбирается во время выполнения: solar прыжковых врат использует ключ `jump_effect` своего `[Object]` ([client-world-fx.md](client-world-fx.md#18-object-клиентская-часть)) как `nickname` секции `[JumpGateEffect]`; если ключа нет, используется nickname `jump_effect_default`; отсутствующий дескриптор пишет в лог «Missing jump effect: %s» ([jump_effect.cpp:94](../../../src/freelancer/Client/jump_effect.cpp#L110)).

### 4.1 `[JumpGateEffect]`

На вратах в заданные моменты времени создаётся до пяти «светящихся колец». **Состояние переносится между секциями одного файла:** запись не очищается между секциями `[JumpGateEffect]`, кроме nickname, id эффектов/хардпойнтов колец, `jump_tunnel_effect` и `jump_tunnel`; три счётчика на кольцо никогда не сбрасываются. Поэтому более поздняя секция, в которой ключ опущен, наследует значение этого ключа от более ранней секции.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | id (`str`) | пусто | Ключ дескриптора. | [L265](../../../src/freelancer/Client/jump_effect.cpp#L265) |
| `glow_ring_effect` | до пяти id (`str[0..4]`) | пусто | Nickname `[Effect]` колец. Каждый непустой параметр `i` поднимает число эффектов до `i + 1`. | [L269](../../../src/freelancer/Client/jump_effect.cpp#L269) |
| `glow_ring_hp` | до пяти имён (`str[0..4]`) | пусто | Хардпойнт врат, к которому прикреплено каждое кольцо. | [L283](../../../src/freelancer/Client/jump_effect.cpp#L283) |
| `glow_create_time` | до пяти float | 0 | Секунды после старта, в которые создаётся каждое кольцо. | [L296](../../../src/freelancer/Client/jump_effect.cpp#L296) |
| `jump_out_time` | float | 0 | Длительность (секунды) фазы выхода из прыжка. | [L308](../../../src/freelancer/Client/jump_effect.cpp#L308) |
| `jump_out_tunnel_time` | float | 0 | Длительность (секунды) фазы туннеля при выходе (экран затемняется в оставшееся время). | [L312](../../../src/freelancer/Client/jump_effect.cpp#L312) |
| `jump_in_tunnel_time` | float | 0 | Длительность (секунды) фазы туннеля при входе. | [L316](../../../src/freelancer/Client/jump_effect.cpp#L316) |
| `jump_in_time` | float | 0 | Длительность (секунды) фазы прибытия. | [L320](../../../src/freelancer/Client/jump_effect.cpp#L320) |
| `kill_time_before_done` | float | 0 | Секунды до конца фазы прибытия (`jump_in_time`), когда эффекты прибытия останавливаются. | [L324](../../../src/freelancer/Client/jump_effect.cpp#L324) |
| `jump_tunnel_effect` | id (`str`) | пусто | Nickname `[Effect]` эффекта частиц туннеля. | [L328](../../../src/freelancer/Client/jump_effect.cpp#L328) |
| `jump_tunnel` | id (`str`) | пусто | Nickname `[gate_tunnel]` (раздел 3), который показывается игроку во время прыжка. Промах пишет в лог «gate tunnel '%s' not found.» | [L332](../../../src/freelancer/Client/jump_effect.cpp#L332) |
| `jump_ambient` | byte x3 | `0, 0, 0` | Ambient RGB во время прыжка (младшие 8 бит усечённого значения). | [L336](../../../src/freelancer/Client/jump_effect.cpp#L336) |
| `jump_background_color` | byte x3 | `0, 0, 0` | RGB фона во время прыжка. | [L345](../../../src/freelancer/Client/jump_effect.cpp#L345) |

Число колец дескриптора — наименьший из трёх счётчиков (эффекты, хардпойнты, времена создания). Любой другой ключ пишет в лог предупреждение «UNKNOWN jumpeffect parameter». Приведённые значения по умолчанию для ключей в порте равны нулю; в первой секции retail оставляет их неинициализированными.

### 4.2 `[JumpShipEffect]`

Каждая секция заменяет предыдущую (дескриптор пересобирается), поэтому побеждает **последняя** секция `[JumpShipEffect]`. Если в файле её нет, при первом использовании создаётся дескриптор по умолчанию с пустыми id и пишется предупреждение «no JumpShipEffectDesc was loaded, creating a default». Любой другой ключ пишет в лог «UNKNOWN jump ship effect parameter».

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `jump_out_effect` | id (`str`) | пусто | Nickname `[Effect]`, проигрываемый на корабле, который уходит в прыжок. | [L388](../../../src/freelancer/Client/jump_effect.cpp#L388) |
| `jump_in_effect` | id (`str`) | пусто | Проигрывается на корабле, который прибывает. | [L392](../../../src/freelancer/Client/jump_effect.cpp#L392) |
| `jump_done_effect_player` | id (`str`) | пусто | Проигрывается по завершении прыжка игрока. | [L396](../../../src/freelancer/Client/jump_effect.cpp#L396) |
| `jump_done_effect_nonplayer` | id (`str`) | пусто | Проигрывается по завершении прыжка корабля не-игрока. | [L400](../../../src/freelancer/Client/jump_effect.cpp#L400) |

Пример (синтетический):

```ini
[JumpGateEffect]
nickname = jump_effect_default
glow_ring_effect = demo_ring_a, demo_ring_b
glow_ring_hp = HpRing01, HpRing02
glow_create_time = 0.0, 1.5
jump_tunnel = demo_tunnel
jump_ambient = 40, 60, 120

[JumpShipEffect]
jump_out_effect = demo_jump_out
jump_in_effect = demo_jump_in
```

## 5. Клиентские fuse (`[Data] fuses`)

Fuse — это скрипт по таймеру, который прикрепляет к объекту эффекты, взрывы и импульсы, пока он горит (например, когда корабль гибнет). Те же файлы читает сервер для своих действий ([server.md](server.md)); клиент распознаёт только перечисленные ниже секции действий и молча пропускает любой другой заголовок. База индексируется по id `name` fuse; fuse с уже существующим именем игнорируется (побеждает первый).

Структура файла: секция `[fuse]` начинает fuse; каждая следующая секция до очередного `[fuse]` — действие этого fuse ([ClientFuse::Read, client_fuse.cpp:90](../../../src/freelancer/Client/client_fuse.cpp#L90), [ClientFuseDB::LoadFile, L949](../../../src/freelancer/Client/client_fuse.cpp#L949)). Действия хранятся отсортированными по времени срабатывания `at_t`.

### 5.1 `[fuse]` и ключи, общие для всех действий

Они реализованы в Common ([common_fuses.cpp:538](../../../src/Common/common_fuses.cpp#L538), [:448](../../../src/Common/common_fuses.cpp#L448)) и разделяются с сервером; см. [world-data.md](world-data.md).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `name` (`[fuse]`) | id (`str`) | пусто | Nickname fuse (ключ базы). | [common_fuses.cpp:550](../../../src/Common/common_fuses.cpp#L550) |
| `lifetime` (`[fuse]`) | float | -1 | Длительность в секундах; прогресс горения растёт на `dt / lifetime`. Не проверяется. | [common_fuses.cpp:552](../../../src/Common/common_fuses.cpp#L552) |
| `death_fuse` (`[fuse]`) | bool | false | Fuse является death fuse. | [common_fuses.cpp:554](../../../src/Common/common_fuses.cpp#L554) |
| `at_t` (каждое действие) | float, необязательный второй float | 0 | Время срабатывания. Со вторым значением время — случайный выбор из диапазона `[first, second]`. Обрабатывается раньше собственных ключей действия. | [common_fuses.cpp:453](../../../src/Common/common_fuses.cpp#L453) |

### 5.2 `[start_explosion]`

[StartExplosionAction::Slot08, L188](../../../src/freelancer/Client/client_fuse.cpp#L188). Действие запускает только архетип взрыва, у которого `process` равен `none`.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `explosion` | id (`str`) | пусто | Nickname архетипа `[Explosion]` ([archetypes-and-equipment.md](archetypes-and-equipment.md)); ищется при загрузке fuse и снова при срабатывании. | [L195](../../../src/freelancer/Client/client_fuse.cpp#L195) |
| `hardpoint` | cstr | пусто | Хардпойнт хоста, в котором размещается взрыв. Если он не найден, ничего не запускается. | [L199](../../../src/freelancer/Client/client_fuse.cpp#L199) |
| `pos_offset` | float x3 | `0, 0, 0` | Смещение, повёрнутое ориентацией хардпойнта. | [L203](../../../src/freelancer/Client/client_fuse.cpp#L203) |

### 5.3 `[start_cam_particles]`

[L312](../../../src/freelancer/Client/client_fuse.cpp#L312): запускает эффект частиц, закреплённый за камерой.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `effect` | id (`str`) | пусто | Nickname `[Effect]` (раздел 1.1). Пустое значение ничего не запускает. | [L319](../../../src/freelancer/Client/client_fuse.cpp#L319) |

### 5.4 `[start_effect]`

[L418](../../../src/freelancer/Client/client_fuse.cpp#L418). `pos_offset` и `ori_offset` относятся к **последнему** ключу `hardpoint`; до первого `hardpoint` они игнорируются и пишут в лог «StartEffectAction::Read() pos_offset precedes any hardpoint.  Ignored.» (и тот же текст для `ori_offset`). При срабатывании из списка пробуется один хардпойнт, начиная со случайного индекса, и используется первый существующий; имя `ROOT` (без учёта регистра) означает собственное начало координат хоста. Без ключей `hardpoint` эффект запускается в начале координат хоста.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `effect` | id (`str`) | пусто | Nickname `[Effect]`. | [L425](../../../src/freelancer/Client/client_fuse.cpp#L425) |
| `attached` | bool | false | `true`: эффект следует за хостом; `false`: он один раз размещается в мире и движется со скоростью хоста. | [L429](../../../src/freelancer/Client/client_fuse.cpp#L429) |
| `hardpoint` | cstr | - | Добавляет кандидата-хардпойнт (повторяемый). | [L433](../../../src/freelancer/Client/client_fuse.cpp#L433) |
| `pos_offset` | float x3 | `0, 0, 0` | Смещение последнего хардпойнта, повёрнутое ориентацией хардпойнта. | [L446](../../../src/freelancer/Client/client_fuse.cpp#L446) |
| `ori_offset` | float x3 (градусы) | единичная | Ориентация последней записи хардпойнта. Значение 2 применяется первым как поворот вокруг оси Y, затем значение 0 поворачивает строки матрицы 1 и 2, затем значение 1 — строки 0 и 1. Не проверяется. | [L463](../../../src/freelancer/Client/client_fuse.cpp#L463) |

### 5.5 `[impulse]`

[L701](../../../src/freelancer/Client/client_fuse.cpp#L701): отталкивает ближайшие объекты от точки хоста. Ничего не происходит, если `radius` и `force` одновременно не больше 0.0001.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `hardpoint` | cstr | пусто | Хардпойнт хоста, являющийся центром толчка. | [L708](../../../src/freelancer/Client/client_fuse.cpp#L708) |
| `force` | float | 0 | Сила в центре; линейно спадает до 0 на `radius`. | [L712](../../../src/freelancer/Client/client_fuse.cpp#L712) |
| `radius` | float | 0 | Радиус действия толчка. | [L716](../../../src/freelancer/Client/client_fuse.cpp#L716) |
| `pos_offset` | float x3 | `0, 0, 0` | Смещение от хардпойнта (применяется с транспонированной ориентацией хардпойнта). | [L720](../../../src/freelancer/Client/client_fuse.cpp#L720) |

### 5.6 `[ignite_fuse]`

[L860](../../../src/freelancer/Client/client_fuse.cpp#L860): запускает другой fuse.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `fuse` | id (`str`) | пусто | Имя другого fuse из клиентской базы; он загружается вместе с этим. | [L867](../../../src/freelancer/Client/client_fuse.cpp#L867) |

Пример (синтетический):

```ini
[fuse]
name = demo_death_fuse
lifetime = 4
death_fuse = true

[start_explosion]
at_t = 0.0
explosion = demo_ship_blast
hardpoint = HpEngine01

[start_effect]
at_t = 0.5, 1.5
effect = demo_smoke
attached = true
hardpoint = HpEngine01
pos_offset = 0, 0, -1
```
