# Конфигурация клиента: звук, голоса, типы эффектов, базы форм и прочие таблицы

На этой странице описаны INI-файлы, которые клиент читает для архетипов звуков и голосов, глобальных настроек звука, таблиц типов визуальных эффектов, баз форм текстур, обломков, динамических астероидов, графиков и солнечных объектов, создаваемых миссиями. Клавиатура, параметры, дисплей, шрифты, курсоры, камеры и запуск описаны в [client-ui-config.md](client-ui-config.md); определения окон и HUD — в [client-ui-config-windows.md](client-ui-config-windows.md); остальные файлы эффектов и мира — в [client-world-fx.md](client-world-fx.md).

## Где лежит файл и как его находят

Все файлы читаются через `INI_Reader` ([parsers.md](parsers.md)); имена секций и ключей нечувствительны к регистру; повторяющиеся ключи выдаются в порядке следования в файле (что с ними происходит, указано для каждого ключа отдельно). Имена файлов задаются ключами `[Data]` в `Freelancer.ini` (см. [client-ui-config.md](client-ui-config.md#43-загрузка-файлов-данных) и [freelancer-ini.md](freelancer-ini.md)) и разрешаются относительно пути Data (`[Freelancer] data path`, по умолчанию `..\Data`), если в строке не указано иное. Поставляемые файлы — двоичные `BINI`; текстовые файлы с теми же секциями и ключами ведут себя так же.

Соглашения: *Тип* называет геттер — *строка* (`get_value_string`), *int* (`get_value_int`), *число* (`get_value_float`, принимает десятичные дроби и `0x..`), *bool* (`get_value_bool`: `true`/`1`/`false`/`0`, всё остальное пишет ошибку в журнал и читается как false); *nickname* означает, что строка хэшируется через `CreateID` (регистронезависимый хэш текста). «По умолчанию» — значение, которое приёмник хранит до чтения файла; у самого читателя собственного механизма значений по умолчанию нет. «Не проверяется» означает, что код не проверяет значение на диапазон.

| Файл | Где задаётся | Кто читает | Раздел |
|---|---|---|---|
| архетипы звуков | `[Data] sounds` | `LoadClientSoundArchIni` | 1 |
| профили голосов | `[Data] voices` | `LoadClientVoicesIni` | 2 |
| `audio\SoundCFG.ini` | фиксированное имя | `LoadClientSoundCfgIni` | 3 |
| таблицы типов эффектов и LOD | внутри файла `[Data] effects` | `LoadEffectTypeSection`, `LoadEffectLodSection` | 4 |
| базы форм | разные | `FShapeDB::Load` | 5 |
| обломки | `[Data] debris` | `LoadDebrisArchFile` | 6 |
| динамические астероиды | секции `[DynamicAsteroids]` файлов астероидных полей | `CliDynamicAsteroidParseIni` | 7 |
| графики | `[Data] igraph` | `GraphLibraryLoad` | 8 |
| `Universe\MissionCreatedSolars.ini` | фиксированное имя | `LoadMissionCreatedSolars` | 9 |
| файл кольца планеты | ключ `ring` солара | `FTexturePanels::Load` | 10 |

---

## 1. Архетипы звуков (`[Sound]`)

`[Data] sounds = <file>`; загружается `LoadClientSoundArchIni` ([menu_sound_arch.cpp:2774](../../../src/freelancer/Client/menu_sound_arch.cpp#L2774)), ключи разбирает `ParseSoundValue` ([menu_sound_arch.cpp:2305](../../../src/freelancer/Client/menu_sound_arch.cpp#L2305)). Читаются только секции `[Sound]`; каждая определяет один архетип звука. Ключ `[Data] sounds` можно повторять, чтобы загрузить несколько файлов.

Секция без `nickname` (или с пустым nickname) пропускается с сообщением `Sound has no nickname`. Если тот же nickname определён повторно, прежний архетип **переопределяется** из новой секции (предупреждение `Redefining sound`); два разных написания с одним хэшем — ошибка коллизии, второе отбрасывается.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | nickname | нет | Имя архетипа звука; хэш служит ключом поиска (сохраняется также как текст). | [menu_sound_arch.cpp:2796](../../../src/freelancer/Client/menu_sound_arch.cpp#L2796) |
| `file` | строка | нет | Аудиофайл, передаваемый менеджеру звука. Более поздний `file` в той же секции заменяет предыдущий. Здесь не проверяется (об отсутствующем файле сообщается только при загрузке звука). | [menu_sound_arch.cpp:2307](../../../src/freelancer/Client/menu_sound_arch.cpp#L2307) |
| `attenuation` | число → int | `0` | Уровень затухания, хранится как знаковый байт. Загрузчик затем проверяет его: значения выше `0` обрезаются до `0`, ниже `-96` — до `-96`, с предупреждением `attenuation out of valid range (0, -96)`. `0` — полная громкость. | [menu_sound_arch.cpp:2315](../../../src/freelancer/Client/menu_sound_arch.cpp#L2315), [menu_sound_arch.cpp:2829](../../../src/freelancer/Client/menu_sound_arch.cpp#L2829) |
| `range` | число [, число] | `-1, -1` (не задано) | Дальность слышимости: `range = <min>[, <max>]`. Если `max` опущен, он равен `min * 30`. Отрицательный `min` сбрасывает оба значения в `-1` (не задано). При `min >= max` в журнал пишется предупреждение `invalid range`. | [menu_sound_arch.cpp:2322](../../../src/freelancer/Client/menu_sound_arch.cpp#L2322), [menu_sound_arch.cpp:2853](../../../src/freelancer/Client/menu_sound_arch.cpp#L2853) |
| `persistent` | строка | не постоянный | `space` или `gameflow` (без учёта регистра): сохранять загруженный звук на протяжении фазы космоса / хода игры (game flow). Любое другое значение пишет `unknown 'persistent' value` и игнорируется. | [menu_sound_arch.cpp:2332](../../../src/freelancer/Client/menu_sound_arch.cpp#L2332) |
| `type` | строка | `normal` | Категория громкости: `normal`, `interface`, `voice`, `music`, `ambience` (без учёта регистра; написание именно `ambience`). Категория определяет, какая пользовательская громкость применяется (`audio_sfx`, `audio_interface`, `audio_voice`, `audio_music`, `audio_ambient` в `PerfOptions.ini`). Другие значения пишут `unknown 'type' value`. | [menu_sound_arch.cpp:2346](../../../src/freelancer/Client/menu_sound_arch.cpp#L2346), [menu_sound_arch.cpp:2291](../../../src/freelancer/Client/menu_sound_arch.cpp#L2291) |
| `streamer` | bool | `false` | Потоково читать файл вместо загрузки целиком. | [menu_sound_arch.cpp:2357](../../../src/freelancer/Client/menu_sound_arch.cpp#L2357) |
| `is_2d` | bool | `false` | Воспроизводить как непозиционный (2D) звук. | [menu_sound_arch.cpp:2363](../../../src/freelancer/Client/menu_sound_arch.cpp#L2363) |
| `pitch_bendable` | bool | `false` | Разрешить изменение высоты звука эффектом Доплера / двигателем. | [menu_sound_arch.cpp:2369](../../../src/freelancer/Client/menu_sound_arch.cpp#L2369) |
| `crv_pitch` | число → int | выкл. | Включает случайное изменение высоты: при каждом воспроизведении выбирается целое смещение в диапазоне `-N ... +N` (`N` — значение, хранится как знаковый байт, поэтому действуют только `1 ... 127`; `0` или отрицательное — без вариации). | [menu_sound_arch.cpp:2375](../../../src/freelancer/Client/menu_sound_arch.cpp#L2375), [menu_sound_arch.cpp:497](../../../src/freelancer/Client/menu_sound_arch.cpp#L497) |
| `lod1` | nickname | нет | Nickname другого архетипа звука, используемого как вариант с меньшей детализацией; задаёт **оба** слота LOD. Не проверяется (неизвестное имя разрешается во время воспроизведения). | [menu_sound_arch.cpp:2388](../../../src/freelancer/Client/menu_sound_arch.cpp#L2388) |
| `lod2` | nickname | нет | Только второй слот LOD. | [menu_sound_arch.cpp:2390](../../../src/freelancer/Client/menu_sound_arch.cpp#L2390) |

Любой другой ключ пишет `unknown [Sound] parameter` и игнорируется.

---

## 2. Профили голосов (`[Voice]`, `[Sound]`)

`[Data] voices = <file>`; `LoadClientVoicesIni` ([voice_profiles.cpp:175](../../../src/freelancer/Client/voice_profiles.cpp#L175)). Секция `[Voice]` открывает профиль; следующие за ней секции `[Sound]` (псевдоним `[VoiceSound]`) описывают сообщения этого профиля.

### 2.1 `[Voice]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | nickname | нет | Имя профиля (ключ — хэш; текст сохраняется). | [voice_profiles.cpp:198](../../../src/freelancer/Client/voice_profiles.cpp#L198) |
| `extend` | строка (≤127 символов) | нет | Имя существующего профиля. Если задано, **эта секция `[Voice]` профиль не создаёт**: следующие секции `[Sound]` добавляются в указанный профиль (при неизвестном имени следующие секции отвергаются с сообщением `[Sound] listed outside [Voice] block`). | [voice_profiles.cpp:204](../../../src/freelancer/Client/voice_profiles.cpp#L204) |
| `script` | строка, **повторяется** | нет | Имя сценария, добавляемое в список сценариев профиля. | [voice_profiles.cpp:217](../../../src/freelancer/Client/voice_profiles.cpp#L217) |

Другие ключи пишут `Voice (...) has invalid parameter`. Профиль, nickname которого уже зарегистрирован, отбрасывается (побеждает первое определение). Для нового профиля загрузчик ищет `<Data>\audio\<nickname>.utf`; если такой файл существует, **он поставляет все аудиофайлы** по идентификатору сообщения, а описанные ниже ключи `file` отдельных сообщений игнорируются.

### 2.2 `[Sound]` / `[VoiceSound]` (внутри профиля)

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `msg` | nickname | нет (обязателен) | Идентификатор сообщения реплики (nickname, по которому сценарии и миссии запрашивают эту реплику). Секция без `msg` отбрасывается с сообщением `missing [Sound] msg parameter`. Идентификатор сообщения, уже определённый в профиле, сохраняет своё **первое** определение. | [voice_profiles.cpp:294](../../../src/freelancer/Client/voice_profiles.cpp#L294) |
| `file` | строка | нет | Аудиофайл сообщения. **Порядок важен:** файл привязывается к идентификатору сообщения, известному на момент чтения строки `file`, поэтому `msg` должен стоять **до** `file`; `file` до `msg` сохраняется под идентификатором сообщения 0. | [voice_profiles.cpp:276](../../../src/freelancer/Client/voice_profiles.cpp#L276) |
| `priority` | int | `0` | Приоритет воспроизведения, хранится как знаковый байт. Не проверяется. | [voice_profiles.cpp:280](../../../src/freelancer/Client/voice_profiles.cpp#L280) |
| `attenuation` | int | `0` | Затухание громкости, хранится как знаковый байт. Не проверяется. | [voice_profiles.cpp:285](../../../src/freelancer/Client/voice_profiles.cpp#L285) |
| `duration` | – | – | Принимается и игнорируется. | [voice_profiles.cpp:290](../../../src/freelancer/Client/voice_profiles.cpp#L290) |

Другие ключи пишут `invalid [Sound] parameter`.

---

## 3. `SoundCFG.ini`

Фиксированное имя `audio\SoundCFG.ini`; загружается в конце `LoadClientIniData` ([dataconfig_progress.cpp:227](../../../src/freelancer/Client/dataconfig_progress.cpp#L227)) функцией `LoadClientSoundCfgIni` ([sound_config.cpp:47](../../../src/freelancer/Client/sound_config.cpp#L47)). Читается только секция `[CFG]`; остальные секции файла (например, определения реверберации) здесь игнорируются. Настройки «ducking» (приглушения) снижают громкость музыки/окружения, пока играет комм-сообщение, кат-сцена реального времени (RTC) или диалог в космосе. Уровни заданы в децибелах (`0` — без изменений, отрицательные — тише), время — в секундах. Каждое значение — число; ни одно не проверяется.

| Ключ | По умолчанию | Значение | Источник |
|---|---|---|---|
| `COCKPIT_ATTENUATION` | `0` | Дополнительное затухание звуков в миксе кабины. | [sound_config.cpp:67](../../../src/freelancer/Client/sound_config.cpp#L67) |
| `ducking_rtc_down_by` | `-10` | Уровень приглушения во время кат-сцен реального времени. | [sound_config.cpp:69](../../../src/freelancer/Client/sound_config.cpp#L69) |
| `ducking_rtc_down_time` | `0.25` | Время достижения приглушённого уровня в кат-сцене. | [sound_config.cpp:71](../../../src/freelancer/Client/sound_config.cpp#L71) |
| `ducking_rtc_up_time` | `1` | Время возврата к норме после кат-сцены. | [sound_config.cpp:73](../../../src/freelancer/Client/sound_config.cpp#L73) |
| `ducking_spaceflight_down_by` | `-6` | Уровень приглушения для диалога в космосе. | [sound_config.cpp:75](../../../src/freelancer/Client/sound_config.cpp#L75) |
| `ducking_spaceflight_down_time` | `0.2` | Время приглушения. | [sound_config.cpp:77](../../../src/freelancer/Client/sound_config.cpp#L77) |
| `ducking_spaceflight_up_time` | `0.75` | Время восстановления. | [sound_config.cpp:79](../../../src/freelancer/Client/sound_config.cpp#L79) |
| `ducking_comm_down_by` | `-6` | Уровень приглушения, пока играет комм-сообщение. | [sound_config.cpp:81](../../../src/freelancer/Client/sound_config.cpp#L81) |
| `ducking_comm_down_time` | `0.25` | Время приглушения. | [sound_config.cpp:83](../../../src/freelancer/Client/sound_config.cpp#L83) |
| `ducking_comm_up_time` | `0.25` | Время восстановления. | [sound_config.cpp:85](../../../src/freelancer/Client/sound_config.cpp#L85) |
| `music_fade_time` | `3` | Время затухания музыки по умолчанию, в секундах. | [sound_config.cpp:87](../../../src/freelancer/Client/sound_config.cpp#L87) |
| `cross_fade_silence` | `-96` | Уровень, считающийся тишиной при кроссфейде музыки. | [sound_config.cpp:89](../../../src/freelancer/Client/sound_config.cpp#L89) |
| `spaceflight_dialogue_pan_range` | `90` | Угловой диапазон панорамирования комм-диалога в космосе в сторону говорящего. | [sound_config.cpp:91](../../../src/freelancer/Client/sound_config.cpp#L91) |
| `spaceflight_dialogue_pan_factor` | `20` | Сила этого панорамирования. | [sound_config.cpp:93](../../../src/freelancer/Client/sound_config.cpp#L93) |
| `ear_doppler_factor` | нет | Коэффициент Доплера, передаваемый менеджеру звука (`SetDopplerFactor`), если ключ присутствует и менеджер звука существует. | [sound_config.cpp:95](../../../src/freelancer/Client/sound_config.cpp#L95) |
| `ear_rolloff_factor` | нет | Коэффициент затухания с расстоянием (`SetRolloffFactor`), при тех же условиях. | [sound_config.cpp:102](../../../src/freelancer/Client/sound_config.cpp#L102) |

---

## 4. Типы эффектов и LOD (`[EffectType]`, `[EffectLOD]`)

Эти две секции находятся внутри файла визуальных эффектов, заданного ключом `[Data] effects`; остальные секции этого файла (`[Effect]`, `[VisEffect]`, `[BeamSpear]`, `[BeamBolt]`) описаны в [client-world-fx.md](client-world-fx.md). Диспетчер — `EffectArchLoadIni` ([vis_effect_arch.cpp:775](../../../src/freelancer/Client/vis_effect_arch.cpp#L775)); разбор выполняется в [effect_type_info.cpp](../../../src/freelancer/Client/effect_type_info.cpp).

### 4.1 `[EffectType]`

Переопределяет встроенные параметры одного типа эффекта ([effect_type_info.cpp:125](../../../src/freelancer/Client/effect_type_info.cpp#L125)). Встроенные значения (таблица 4.3) действуют для каждого типа, который не упомянут.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | строка | нет | Одно из 47 имён `EFT_*` из таблицы 4.3 (с учётом регистра). Все последующие ключи секции изменяют этот тип. Неизвестное имя пишет `EffectTypes: unknown type`, и тогда ключи секции изменяют **ранее выбранный** тип (или ничего, если его не было). Ключи до первого корректного `nickname` игнорируются. | [effect_type_info.cpp:136](../../../src/freelancer/Client/effect_type_info.cpp#L136) |
| `priority` | число | по типу | `EffectTypeWithinPerf` сравнивает его с уровнем детализации эффектов (`1 - generic_effect_perf`, см. [client-ui-config.md](client-ui-config.md#32-ключи-параметров-производительности)): эффект разрешён, если уровень не выше его приоритета (сейчас используется для эффектов попадания с разбросом — splash-hit). | [effect_type_info.cpp:154](../../../src/freelancer/Client/effect_type_info.cpp#L154), [effect_type_info.cpp:115](../../../src/freelancer/Client/effect_type_info.cpp#L115) |
| `generic_priority` | число | по типу | Приоритет, используемый для обобщённых (неспецифичных) эффектов этого типа. | [effect_type_info.cpp:156](../../../src/freelancer/Client/effect_type_info.cpp#L156) |
| `lod_type` | строка | по типу | `EFT_LOD_TRAIL`, `EFT_LOD_WEAPON`, `EFT_LOD_SMALL`, `EFT_LOD_MEDIUM`, `EFT_LOD_LARGE`, `EFT_LOD_SMALL_DISTANT` (без учёта регистра). Любое другое значение (включая `EFT_LOD_NONE`) оставляет тип без изменений. Выбирает строку `[EffectLOD]`, используемую для затухания с расстоянием. | [effect_type_info.cpp:158](../../../src/freelancer/Client/effect_type_info.cpp#L158) |
| `radius` | число | по типу | Ограничивающий радиус для видимости/LOD. Отрицательное значение пишет ошибку и заменяется на `1.0`. | [effect_type_info.cpp:171](../../../src/freelancer/Client/effect_type_info.cpp#L171) |
| `visibility` | строка | по типу | `EXIST_OFFSCREEN` сохраняет эффект живым за пределами экрана; любое другое слово сбрасывает флаг. | [effect_type_info.cpp:184](../../../src/freelancer/Client/effect_type_info.cpp#L184) |
| `update` | строка | по типу | `UPDATE_OFFSCREEN` продолжает обновлять эффект за пределами экрана; любое другое слово сбрасывает флаг. | [effect_type_info.cpp:190](../../../src/freelancer/Client/effect_type_info.cpp#L190) |
| `run_time` | число | по типу | Время работы в секундах; записи таблицы используют `-1` для непрерывных эффектов (отрицательное значение выставляет флаг архетипа эффекта, см. [effect_type_info.h](../../../src/freelancer/Client/effect_type_info.h)). | [effect_type_info.cpp:196](../../../src/freelancer/Client/effect_type_info.cpp#L196) |
| `fps` | число | по типу (`5`) | Частота обновления эффекта. | [effect_type_info.cpp:198](../../../src/freelancer/Client/effect_type_info.cpp#L198) |
| `pbubble` | число, число | по типу | Ближняя и дальняя дистанция диапазона «pbubble» эффекта. При far < near пишется предупреждение `pbubble ranges backwards` (значения всё равно сохраняются). | [effect_type_info.cpp:200](../../../src/freelancer/Client/effect_type_info.cpp#L200) |

Любой другой ключ пишет `EffectType (...) has unknown parameter`.

### 4.2 `[EffectLOD]`

[effect_type_info.cpp:225](../../../src/freelancer/Client/effect_type_info.cpp#L225). **Первым ключом должен быть `type`**; иначе секция игнорируется.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `type` | строка | нет | Определяемая строка: `lod_type_trail`, `lod_type_weapon`, `lod_type_small`, `lod_type_medium`, `lod_type_large`, `lod_type_small_distant` (без учёта регистра). `none` не принимается. Неизвестное имя пишет `invalid LOD type` и завершает секцию. | [effect_type_info.cpp:229](../../../src/freelancer/Client/effect_type_info.cpp#L229) |
| `max_lod_screen_size` | число | `-1` | Экранный размер (радиус / расстояние, масштабированный камерой), начиная с которого эффект имеет полную интенсивность. | [effect_type_info.cpp:255](../../../src/freelancer/Client/effect_type_info.cpp#L255) |
| `min_lod_screen_size` | число | `-1` | Экранный размер, с которого начинается нарастание (смешивание = `(size - min_lod) / (max_lod - min_lod)`, ограничено `0 ... 1`). | [effect_type_info.cpp:257](../../../src/freelancer/Client/effect_type_info.cpp#L257) |
| `min_screen_size` | число | `-1` | Эффекты, меньшие этого размера на экране, не рисуются вовсе. | [effect_type_info.cpp:259](../../../src/freelancer/Client/effect_type_info.cpp#L259) |

### 4.3 Встроенные типы эффектов

Начальная таблица (`G_EffectTypeTable`, [effect_type_info.cpp:38](../../../src/freelancer/Client/effect_type_info.cpp#L38)); значения в секциях `[EffectType]` перезаписывают эти. Столбцы: priority, generic priority, тип LOD, radius, флаг visibility, флаг update, run time, fps, pbubble near и far.

| Тип | priority | generic | тип LOD | radius | visibility | update | run_time | fps | pbubble |
|---|---|---|---|---|---|---|---|---|---|
| `EFT_NONE` | 0 | 0 | EFT_LOD_NONE | 10 | - | - | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_SMALL` | 1 | 0.5 | EFT_LOD_SMALL | 10 | - | - | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_MEDIUM` | 1 | 0.5 | EFT_LOD_MEDIUM | 50 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_LARGE` | 1 | 0.5 | EFT_LOD_LARGE | 100 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_DEBRIS` | 0 | 0 | EFT_LOD_SMALL | 10 | - | - | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_SHIP` | 1 | 0.5 | EFT_LOD_MEDIUM | 50 | - | - | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_MISSILE` | 0 | 0 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_EXPLOSION_MINE` | 0 | 0 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_MUZZLE_FLASH` | 0 | 0 | EFT_LOD_SMALL_DISTANT | 5 | - | - | 15 | 5 | 100, 500 |
| `EFT_MUZZLE_LARGE_FLASH` | 1 | 0.5 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_MISSILE_DRIVE` | 0.5 | 0.5 | EFT_LOD_WEAPON | 100 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_WEAPON_PROJ` | 1 | 0.5 | EFT_LOD_WEAPON | 15 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_WEAPON_LARGE_PROJ` | 1 | 0.5 | EFT_LOD_LARGE | 50 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_WEAPON_IMPACT` | 0.5 | 0.5 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_WEAPON_LARGE_IMPACT` | 1 | 0.5 | EFT_LOD_MEDIUM | 20 | - | - | 15 | 5 | 100, 500 |
| `EFT_WEAPON_COUNTER_MEASURE` | 1 | 0.5 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_WEAPON_MINE` | 1 | 0.5 | EFT_LOD_MEDIUM | 20 | - | - | 15 | 5 | 100, 500 |
| `EFT_DAMAGE_HULL` | 0.5 | 0.5 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_DAMAGE_LARGE_SHIELD` | 1 | 0.5 | EFT_LOD_LARGE | 100 | - | - | 15 | 5 | 100, 500 |
| `EFT_DAMAGE_SHIELD` | 0.5 | 0.5 | EFT_LOD_SMALL | 5 | - | - | 15 | 5 | 100, 500 |
| `EFT_DAMAGE_CRUISE` | 0.5 | 0.5 | EFT_LOD_SMALL_DISTANT | 7.5 | - | - | 15 | 5 | 100, 500 |
| `EFT_DAMAGE_SMOKE` | 0.5 | 0.5 | EFT_LOD_MEDIUM | 20 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_DAMAGE_LARGE_SMOKE` | 0.5 | 0.5 | EFT_LOD_LARGE | 20 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_DEBRIS_TRAIL` | 0.5 | 0.5 | EFT_LOD_SMALL | 5 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_ENGINE_CONTRAIL` | 0 | 0 | EFT_LOD_SMALL | 5 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 100, 500 |
| `EFT_ENGINE_TRAIL` | 1 | 0.5 | EFT_LOD_TRAIL | 100 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 100, 500 |
| `EFT_ENGINE_LARGE_TRAIL` | 1 | 0.5 | EFT_LOD_TRAIL | 100 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 500, 2000 |
| `EFT_ENGINE_FIRE` | 0.5 | 0.5 | EFT_LOD_SMALL_DISTANT | 7.5 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_ENGINE_LARGE_FIRE` | 0.5 | 0.5 | EFT_LOD_SMALL_DISTANT | 25 | EXIST_OFFSCREEN | - | -1 | 5 | 250, 1000 |
| `EFT_ENGINE_THRUSTER` | 0 | 0 | EFT_LOD_SMALL_DISTANT | 7.5 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_EQUIP_CLOAK` | 1 | 0.5 | EFT_LOD_SMALL | 5 | - | - | -1 | 5 | 100, 500 |
| `EFT_EQUIP_LARGE_CLOAK` | 1 | 0.5 | EFT_LOD_MEDIUM | 100 | - | - | -1 | 5 | 500, 1500 |
| `EFT_EQUIP_ATTACHED` | 1 | 0.5 | EFT_LOD_LARGE | 100 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_EQUIP_ATTACHED_LARGE` | 1 | 0.5 | EFT_LOD_LARGE | 500 | EXIST_OFFSCREEN | - | -1 | 5 | 1000, 5000 |
| `EFT_EQUIP_ATTACHED_INSANELY_LARGE` | 1 | 0.5 | EFT_LOD_LARGE | 500 | EXIST_OFFSCREEN | - | -1 | 5 | 1000, 5000 |
| `EFT_MISC_DUST` | 0.5 | 0.5 | EFT_LOD_NONE | 500 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 100, 500 |
| `EFT_MOTION_DUST` | 1 | 1 | EFT_LOD_NONE | 500 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 100, 500 |
| `EFT_JUMP_EFFECTS` | 1 | 0.5 | EFT_LOD_NONE | 0 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 100, 500 |
| `EFT_JUMP_GATE_EFFECTS` | 1 | 0.5 | EFT_LOD_NONE | 0 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 1000, 2500 |
| `EFT_LANE_ACTIVE` | 0.5 | 0.5 | EFT_LOD_NONE | 50 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_LANE_TUBE` | 1 | 0.5 | EFT_LOD_NONE | 50 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_LANE_SHIP` | 0.5 | 0.5 | EFT_LOD_MEDIUM | 50 | - | - | 15 | 5 | 100, 500 |
| `EFT_LANE_SHIP_TRAVEL` | 0.5 | 0.5 | EFT_LOD_MEDIUM | 50 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_CUSTOM_EFFECTS` | 0 | 0 | EFT_LOD_LARGE | -1 | - | - | 15 | 5 | 100, 500 |
| `EFT_CODE_PROJ_EFFECT` | 1 | 0.5 | EFT_LOD_WEAPON | 15 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |
| `EFT_TRACTORBEAM` | 1 | 1 | EFT_LOD_NONE | -1 | EXIST_OFFSCREEN | UPDATE_OFFSCREEN | -1 | 5 | 100, 500 |
| `EFT_FLASHLIGHT` | 1 | 1 | EFT_LOD_NONE | -1 | EXIST_OFFSCREEN | - | -1 | 5 | 100, 500 |

---

## 5. Базы форм (`[Texture]` и `[Shape]`)

*База форм* (shape database) сопоставляет имя формы прямоугольнику текстуры; из них рисуют UI, HUD, курсоры, свечение звёзд и многие эффекты. Все они читаются одним разборщиком, `FShapeDB::Load(path, baseDir)` ([texture_shapes.cpp:299](../../../src/freelancer/Client/texture_shapes.cpp#L299)). Если задан базовый каталог, путь присоединяется к нему; иначе используется как есть. Файл, который не удалось открыть, пишет `unable to load ShapeDB file` и ничего не загружает. Имена форм сравниваются без учёта регистра и ограничены 23 символами; дважды определённое имя пишет `shape name already found`, и сохраняется **первое** определение. Поиск формы, не нашедший ничего, пишет предупреждение `missing shape` в месте использования.

Файлы, загружаемые как базы форм:

| Файл | Чем задаётся | Источник |
|---|---|---|
| `mouse.ini` (содержит также `[Cursor]`, см. [client-ui-config.md](client-ui-config.md#6-курсоры-мыши-mouseini)) | фиксированное имя | [input_manager.cpp:1104](../../../src/freelancer/Client/input_manager.cpp#L1104) |
| `Interface\ButtonTextures.ini` | фиксированное имя | [nn_montage_draw.cpp:40](../../../src/freelancer/Client/nn_montage_draw.cpp#L40) |
| `Interface\NeuroNet\NavMap\NewNavMap\NavMapTextures.ini` | фиксированное имя | [nn_navmap.cpp:1102](../../../src/freelancer/Client/nn_navmap.cpp#L1102) |
| `Interface\UI\UITextures.ini` | фиксированное имя | [ui_scroll.cpp:163](../../../src/freelancer/Client/ui_scroll.cpp#L163) |
| `solar\StarArch.ini` | фиксированное имя (тот же файл содержит и секции свечения звёзд/линз, см. [client-world-fx.md](client-world-fx.md)) | [star_sphere.cpp:2745](../../../src/freelancer/Client/star_sphere.cpp#L2745) |
| файл `shapes` секции `[HUD]` в `hud.ini` | [client-ui-config-windows.md](client-ui-config-windows.md#41-hud) | [hud_element_list.cpp:463](../../../src/freelancer/Client/hud_element_list.cpp#L463) |
| каждый файл `[Data] effect_shapes` | `Freelancer.ini` | [dataconfig_progress.cpp:135](../../../src/freelancer/Client/dataconfig_progress.cpp#L135), [texture_shapes.cpp:728](../../../src/freelancer/Client/texture_shapes.cpp#L728) |
| каждый `file =` секции `[TexturePanels]` в файлах систем, туманностей и астероидных полей | [systems-and-bases.md](systems-and-bases.md), [client-world-fx.md](client-world-fx.md) | [star_sys.cpp:401](../../../src/freelancer/Client/star_sys.cpp#L401), [texture_panels.cpp:994](../../../src/freelancer/Client/texture_panels.cpp#L994) |

### 5.1 Секция `[Texture]` (повторяется)

Ключи образуют небольшой конечный автомат и обрабатываются по порядку. Типичное использование:

```ini
[Texture]
file         = textures\example_atlas.txm       ; texture archive to load
texture_name = example_atlas_tex                ; texture inside the archive
shape_name   = button_normal                    ; start a shape on that texture ...
dim          = 0, 0, 64, 64                     ; ... and give it a rectangle
shape_name   = button_pressed
dim          = 64, 0, 64, 64
tex_shape    = example_full_tex                 ; one more texture, whole-texture shape of the same name
```

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `file` | строка | нет | Загружаемый архив текстур (`.txm`); путь передаётся файловой системе данных. Пустое значение или то же имя, что у предыдущего `file` (без учёта регистра), игнорируется. Если архив не удалось открыть, игра пишет `file not found`. Все архивы, загруженные одним вызовом, освобождаются в конце загрузки, поэтому текстуры остаются, только пока на них ссылаются формы. Сбрасывает текущую текстуру. | [texture_shapes.cpp:349](../../../src/freelancer/Client/texture_shapes.cpp#L349) |
| `name` или `texture_name` (синонимы) | строка | нет | Выбирает текстуру по имени среди загруженных архивов (CRC-32 имени). Ненайденная текстура пишет `missing texture`; текущая текстура при этом пуста. Пишет `shape definition never followed texture`, если за предыдущим выбором текстуры не последовало формы. | [texture_shapes.cpp:399](../../../src/freelancer/Client/texture_shapes.cpp#L399) |
| `shape_name` | строка (≤23 символов) | нет | Начинает новую форму на текущей текстуре (она покрывает всю текстуру, пока не последует `dim`). Без текущей текстуры — ошибка `'shape_name' field must be preceeded by valid 'texture_name' field`. | [texture_shapes.cpp:436](../../../src/freelancer/Client/texture_shapes.cpp#L436) |
| `tex_shape` | строка | нет | Сокращение: выбирает текстуру с этим именем **и** создаёт форму с тем же именем, покрывающую всю текстуру. Отсутствующая текстура пропускается. | [texture_shapes.cpp:473](../../../src/freelancer/Client/texture_shapes.cpp#L473) |
| `dim` | от 4 до 8 чисел | нет | `dim = x, y, w, h [, ul_u, ul_v, lr_u, lr_v]` — задаёт прямоугольник формы, только что начатой `shape_name` (см. 5.3). Первые четыре значения обязательны; последние четыре при пропуске читаются как `0`. `dim` без предшествующего `shape_name` пишет ошибку и игнорируется; он завершает форму (второму `dim` нужен новый `shape_name`). | [texture_shapes.cpp:519](../../../src/freelancer/Client/texture_shapes.cpp#L519) |

Любой другой ключ пишет `unknown [Shape] parameter` (сообщение говорит `[Shape]` даже внутри `[Texture]`) и игнорируется.

### 5.2 Секция `[Shape]` (повторяется)

Определяет одну форму на текстуре, выбранной самой последней секцией `[Texture]` (выбор сохраняется от секции к секции). Если текстуры нет, пишется ошибка `bad texture id`.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `name` или `shape_name` | строка (≤23 символов) | нет (обязателен) | Имя формы. Секция без имени пишет `no shape name was specified`. | [texture_shapes.cpp:581](../../../src/freelancer/Client/texture_shapes.cpp#L581) |
| `x`, `y` | число | `0` | Левый верхний угол прямоугольника. | [texture_shapes.cpp:594](../../../src/freelancer/Client/texture_shapes.cpp#L594) |
| `w`, `h` | число | `0` | Размер прямоугольника. | [texture_shapes.cpp:602](../../../src/freelancer/Client/texture_shapes.cpp#L602) |
| `upper_left` | число, число | `-1, -1` | Угол внутренней (содержимой) области в тех же единицах, что `x`/`y`; отрицательная компонента означает «использовать край прямоугольника». | [texture_shapes.cpp:610](../../../src/freelancer/Client/texture_shapes.cpp#L610) |
| `lower_right` | число, число | `-1, -1` | Противоположный угол внутренней области; к каждой компоненте прибавляется `1` (значения — включающие пиксельные координаты). | [texture_shapes.cpp:615](../../../src/freelancer/Client/texture_shapes.cpp#L615) |
| `dim` | 8 чисел | – | Задаёт сразу `x, y, w, h, upper_left (2), lower_right (2)`; к паре `lower_right` `+1` **не** прибавляется, а пропущенные значения читаются как `0`. | [texture_shapes.cpp:622](../../../src/freelancer/Client/texture_shapes.cpp#L622) |

Любой другой ключ пишет `unknown shape parameter`.

### 5.3 Правила прямоугольника

Применяются функцией `TexShapeSetRect` ([texture_shapes.cpp:137](../../../src/freelancer/Client/texture_shapes.cpp#L137)) к каждой форме:

* `x`, `y`: если оба `<= 1`, они берутся как доли размера текстуры и умножаются на ширину/высоту текстуры; иначе это позиции в текселях.
* `w`, `h`: если любое из них равно `0`, **оба** становятся полным размером текстуры. Если оба `<= 1` (и ненулевые), они — доли размера текстуры; иначе — тексели.
* Текстурные координаты сдвигаются внутрь на `0.01` текселя с каждого края, чтобы соседние формы в атласе не протекали друг в друга.
* `upper_left` / `lower_right`: отрицательная компонента заменяется соответствующим краем прямоугольника; они задают внутреннюю область, из которой вычисляются масштаб формы и смещение центра.

---

## 6. Обломки (`[Debris]`)

`[Data] debris = <file>`; загружается `LoadDebrisArchFile` ([client_debris.cpp:152](../../../src/freelancer/Client/client_debris.cpp#L152)); читаются только секции `[Debris]`, каждая разбирается `ReadDebrisArch` ([client_debris.cpp:70](../../../src/freelancer/Client/client_debris.cpp#L70)). Файл, который не удалось открыть, пишет `Failed to open debris file`. Секция без `nickname` отбрасывается. Если nickname уже существует, сохраняется **первое** определение, а более позднее отбрасывается (текст журнала `First overwritten with second` вводит в заблуждение).

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | nickname | нет (обязателен) | Имя, по которому другие файлы (например, определения взрывов) ссылаются на этот тип обломков. | [client_debris.cpp:79](../../../src/freelancer/Client/client_debris.cpp#L79) |
| `trail` | nickname | нет | Архетип эффекта, прикрепляемый как шлейф к каждому обломку. | [client_debris.cpp:83](../../../src/freelancer/Client/client_debris.cpp#L83) |
| `lifetime` | число [, число] | min `0`, max `3.4e38` | `lifetime = <min>[, <max>]` в секундах. Одно значение задаёт и min, и max; если второе значение меньше первого, max становится равным min. | [client_debris.cpp:87](../../../src/freelancer/Client/client_debris.cpp#L87) |
| `explosion` | nickname | нет | Архетип взрыва, создаваемого, когда обломок погибает со взрывом. | [client_debris.cpp:100](../../../src/freelancer/Client/client_debris.cpp#L100) |
| `death_method` | строка | `vanishing` | `exploding` или `vanishing` (без учёта регистра). Любое другое значение оставляет прежнюю настройку (по умолчанию `vanishing`). Исчезающий обломок может быть удалён, как только он прожил дольше минимального времени жизни. | [client_debris.cpp:104](../../../src/freelancer/Client/client_debris.cpp#L104) |
| `linear_drag` | число | `0` | Линейное сопротивление. Не проверяется. | [client_debris.cpp:119](../../../src/freelancer/Client/client_debris.cpp#L119) |
| `angular_drag` | число ×3 | `0, 0, 0` | Угловое сопротивление по осям. Не проверяется. | [client_debris.cpp:123](../../../src/freelancer/Client/client_debris.cpp#L123) |

---

## 7. Динамические астероиды (`[DynamicAsteroids]`)

Секция `[DynamicAsteroids]` находится внутри файла астероидного поля (остальная часть файла поля — `[Field]`, `[Band]`, билборды и т. д. — описана в [client-world-fx.md](client-world-fx.md)); она передаётся из `ParseFieldSection` ([cli_asteroid.cpp:991](../../../src/freelancer/Client/cli_asteroid.cpp#L991)) и разбирается `CliDynamicAsteroidParseIni` ([cli_asteroid_dynamic.cpp:420](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L420)). Она настраивает пул астероидов, которые порождаются вокруг камеры и перерабатываются. Поле может содержать несколько таких секций; каждая создаёт собственный пул.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `asteroid` | строка | нет | Nickname *архетипа динамического астероида* (хэшируется). Если не найден: предупреждение `dynamic asteroid archetype ... not found`, пул остаётся пустым. | [cli_asteroid_dynamic.cpp:427](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L427) |
| `count` | число → int | `10` | Размер пула до масштабирования по детализации; реальный пул равен `int(count * detail_scale + 0.5)`, где масштаб детализации равен `(1 - d) * 0.5 + d` для глобальной детализации астероидов `d` (параметр производительности `Asteroids`, по умолчанию `1`, то есть масштаб между 0.5 и 1). Не проверяется (отрицательное значение даёт некорректное выделение памяти). | [cli_asteroid_dynamic.cpp:449](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L449), [cli_asteroid.cpp:1005](../../../src/freelancer/Client/cli_asteroid.cpp#L1005) |
| `placement_radius` | число | `150` | Радиус вокруг камеры, в котором размещаются астероиды; внутренний радиус устанавливается равным `0.7 *` этого значения (по умолчанию внутренний `75`). | [cli_asteroid_dynamic.cpp:454](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L454) |
| `max_velocity` | число | `50` | Верхняя граница случайной начальной скорости. | [cli_asteroid_dynamic.cpp:461](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L461) |
| `max_angular_velocity` | число | `5` | Верхняя граница случайного вращения. | [cli_asteroid_dynamic.cpp:465](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L465) |
| `color_shift` | число ×3 | `1, 1, 1` | Покомпонентный множитель цвета, применяемый к астероидам. | [cli_asteroid_dynamic.cpp:469](../../../src/freelancer/Client/cli_asteroid_dynamic.cpp#L469) |
| `zone` | строка | зона поля | Nickname зоны (в активной системе), в которой порождается пул. Читается вторым проходом родителем ([cli_asteroid.cpp:1025](../../../src/freelancer/Client/cli_asteroid.cpp#L1025)); неизвестная зона пишет `linked to missing Zone`; при отсутствии корректной `zone` используется собственная зона поля. | [cli_asteroid.cpp:1025](../../../src/freelancer/Client/cli_asteroid.cpp#L1025) |

Динамические астероиды симулируются, только пока включена игровая опция `DYN_ASTEROIDS`.

---

## 8. Библиотека графиков (`[IGraph]`)

`[Data] igraph = <file>`; `GraphLibraryLoad` ([graph_library.cpp:340](../../../src/freelancer/Client/graph_library.cpp#L340)). Определяет именованные кусочные кривые (`IGraph`), которые сэмплируют другие системы (цвет во времени и подобное). **Ошибки в этом файле вызывают окно сообщения Windows** (`unable to open IGraph file`, `invalid IGraph::ElemType`, `unknown IGraph parameter`, `unable to create IGraph object (duplicate?)`), поэтому опечатка сразу заметна.

Каждая секция `[IGraph]` имеет вид `nickname`, `type`, затем список строк `point`. Строка `type` начинает список точек: **каждый ключ после `type` должен быть `point`**, поэтому `nickname` обязан идти первым.

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `nickname` | строка (≤127 символов) | нет | Имя кривой (без учёта регистра; повторяющееся имя — ошибка, кривая не создаётся). | [graph_library.cpp:372](../../../src/freelancer/Client/graph_library.cpp#L372) |
| `type` | строка | нет | Тип элемента: `FLOAT`, `COLOR` или `INTEGER` (без учёта регистра). Всё остальное показывает окно ошибки, секция пропускается. | [graph_library.cpp:387](../../../src/freelancer/Client/graph_library.cpp#L387) |
| `point` | **повторяется**; `x, value` | нет | Один ключ кривой. `x` — число (позиция вдоль кривой, обычно возрастающая; не проверяется). `value` — число для `FLOAT`; для `INTEGER` и `COLOR` усекается до 32-битного целого. В кривой хранится не более 1024 точек; бо́льшее число переполняет буфер загрузчика (не проверяется). | [graph_library.cpp:418](../../../src/freelancer/Client/graph_library.cpp#L418) |

---

## 9. Солары, создаваемые миссиями: `Universe\MissionCreatedSolars.ini`

Фиксированное имя относительно пути Data; загружается один раз, при первом использовании ([mission_created_solars.cpp:12](../../../src/freelancer/Client/mission_created_solars.cpp#L12)). Задаёт позицию и текст информационной карточки для баз, которые миссии создают во время выполнения и у которых поэтому нет `[Object]` в файле системы; используется навигационной картой ([nn_navmap_update.cpp:233](../../../src/freelancer/Client/nn_navmap_update.cpp#L233)). Отсутствие файла означает отсутствие записей.

`[MissionCreatedSolar]` (повторяющаяся секция):

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `base` | nickname | нет | Nickname базы (хэшируется). | [mission_created_solars.cpp:41](../../../src/freelancer/Client/mission_created_solars.cpp#L41) |
| `ids_info` | число → int | нет | Идентификатор строкового ресурса информационной карточки. | [mission_created_solars.cpp:46](../../../src/freelancer/Client/mission_created_solars.cpp#L46) |
| `pos` | число ×3 | нет | Позиция в системе `x, y, z`. | [mission_created_solars.cpp:51](../../../src/freelancer/Client/mission_created_solars.cpp#L51) |

Запись добавляется **как только в секции встретились все три ключа** (в любом порядке), после чего флаги «встречено» сбрасываются, поэтому одна секция может породить несколько записей, если ключи повторяются. Последние значения каждого ключа переходят между секциями (не очищаются), поэтому секция, повторяющая только `base`, переиспользует прежние `ids_info` и `pos` лишь если в ней снова встретились все три. Поиск возвращает первую запись с данной базой.

---

## 10. Файл кольца планеты (`FTexturePanels::Load`)

Солар-объект в файле системы может указать кольцо через `ring = <zone nickname>, <file>` ([client_gameobj.cpp:2159](../../../src/freelancer/Client/client_gameobj.cpp#L2159), см. [systems-and-bases.md](systems-and-bases.md)). `<file>` читается `FTexturePanels::Load` ([texture_panels.cpp:961](../../../src/freelancer/Client/texture_panels.cpp#L961)); зона должна существовать в активной системе (без неё журнал пишет `planet ring must have zone`, и кольцо не строится). Кольцо — плоская полоса между двумя размерами зоны, рисуемая как меш.

### 10.1 `[TexturePanels]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `file` | строка, **повторяется** | нет | База форм ([раздел 5](#5-базы-форм-texture-и-shape)) с текстурами кольца, разрешается относительно пути Data. | [texture_panels.cpp:998](../../../src/freelancer/Client/texture_panels.cpp#L998) |

### 10.2 `[Exterior]`

| Ключ | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|
| `color` | число ×3 | предыдущее значение | R, G, B кольца, каждое `0 ... 255` (делится на 255). | [texture_panels.cpp:1010](../../../src/freelancer/Client/texture_panels.cpp#L1010) |
| `shape` | строка | нет | Форма (из баз `[TexturePanels]`), используемая для кольца. Неизвестное имя пишет `shape ... not found for rings`. | [texture_panels.cpp:1016](../../../src/freelancer/Client/texture_panels.cpp#L1016) |
| `detail_shape` | строка | нет | Необязательная вторая форма, рисуемая как деталь поверх первой. | [texture_panels.cpp:1025](../../../src/freelancer/Client/texture_panels.cpp#L1025) |
| `detail_tile` | число, число | предыдущее значение | Тайлинг формы детали; второе значение усекается до целого. | [texture_panels.cpp:1035](../../../src/freelancer/Client/texture_panels.cpp#L1035) |
| `opacity` | число | `1.0` | Непрозрачность кольца. | [texture_panels.cpp:1040](../../../src/freelancer/Client/texture_panels.cpp#L1040) |
| `thickness_edge` | число | `0.1` | Параметр толщины затухания края. | [texture_panels.cpp:1042](../../../src/freelancer/Client/texture_panels.cpp#L1042) |
| `inner_edge` | число | `0.1` | Параметр затухания внутреннего края. | [texture_panels.cpp:1044](../../../src/freelancer/Client/texture_panels.cpp#L1044) |
| `outer_edge` | число | `0.1` | Параметр затухания внешнего края. | [texture_panels.cpp:1046](../../../src/freelancer/Client/texture_panels.cpp#L1046) |
| `num_segments` | число → int | `32` | Число сегментов вокруг кольца (в меше `2 * num_segments` вершин). Не проверяется (`0` даёт деление на ноль при вычислении углового шага). | [texture_panels.cpp:1048](../../../src/freelancer/Client/texture_panels.cpp#L1048) |
| `transition_dist` | число | `150` | Расстояние перехода между кольцом и его дальним представлением. | [texture_panels.cpp:1050](../../../src/freelancer/Client/texture_panels.cpp#L1050) |

### 10.3 `[Clouds]` (повторяется)

Добавляет к кольцу слой панелей облаков. Ключи — те же, что в секции `[Clouds]` туманности, их разбирает код туманностей ([cli_nebula.cpp:1824](../../../src/freelancer/Client/cli_nebula.cpp#L1824)); см. [client-world-fx.md](client-world-fx.md). Каждая секция создаёт один слой облаков.
