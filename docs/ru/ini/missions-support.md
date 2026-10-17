# Вспомогательные таблицы миссий: RTC-скрипты, голоса, generic-скрипты, записи NPC, сюжетные данные

Вспомогательные INI-файлы, которые `Content.dll` читает для слоя миссий: RTC-скрипты баров (`[CharacterEncounter]`), именованные сюжетные NPC, generic-скрипты баров и их исключения по голосам, длительности реплик и свойства голосов, таблица toughness (ранга) игрока, записи предложений NPC баз (`bribe`/`know`/`rumor`) и названия этапов сюжета. Сами скрипты миссий описаны в [missions.md](missions.md).

## Расположение файлов / как они находятся

Все файлы открываются через `OpenContentDataIni(относительный путь)` ([ContentDataPath.cpp:17](../../../src/Content/ContentDataPath.cpp#L17)), то есть относительно каталога Data из `freelancer.ini` ([freelancer-ini.md](freelancer-ini.md)). Таблицы с фиксированными именами загружаются один раз при старте через `LoadContentGameTables` ([Encounters.cpp:495](../../../src/Content/Encounters.cpp#L495)) в таком порядке: `audio\voices_space_male.ini`, `audio\voices_space_female.ini`, `audio\voices_recognizable.ini` (каждый читается дважды: один раз для `[Voice]`/`[Sound]`, один раз для `[mVoiceProp]`), `missions\empathy.ini`, `missions\voice_properties.ini`, `missions\formations.ini`, `missions\pilots_*.ini`, `missions\npcships*.ini`, `missions\faction_prop.ini`, `equipment\commodities_per_faction.ini`, `missions\shipclasses.ini`, `ships\shiparch.ini` (второй проход), `missions\lootprops.ini`, `scripts\GCS\GCSExcl.ini`, `missions\PTough.ini`, `missions\RankDiff.ini`, `missions\news.ini`. Результат открытия не проверяется: отсутствующий файл просто ничего не загружает. Этот документ охватывает: voices, voice_properties, GCSExcl, PTough; остальное описано в [ai-and-encounters.md](ai-and-encounters.md), [random-missions.md](random-missions.md) и [systems-and-bases.md](systems-and-bases.md). `missions\specific_npc.ini` загружается контроллером `StoryLauncher` ([StoryLauncher.cpp:41](../../../src/Content/StoryLauncher.cpp#L41)), а `scripts\gcs\genericScripts.ini` — контроллером поддержки баз ([BaseSupport.cpp:211](../../../src/Content/BaseSupport.cpp#L211)).

Разбор выполняет `INI_Reader` (см. [parsers.md](parsers.md)); ключи и заголовки не зависят от регистра, повторяющиеся ключи доставляются все.

## RTC-скрипты (файлы `[CharacterEncounter]`)

Скрипт «cutscene реального времени» управляет персонажем бара: где он стоит, какие Thorn-скрипты проигрывает и что предлагает. Файлы запускаются через `Act_AddRTC` и конструктором сюжета (`Mission_01b`: `missions\m01a\M001a_s006x_Li01_02_nrml.ini`, `missions\m01a\rtc_success.ini`). Парсер: `FCharacterEncounter::Load` ([RtcScripts.cpp:43](../../../src/Content/RtcScripts.cpp#L43)). Если файл не открывается, ничего не загружается.

### `[CharacterEncounter]`

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `Location` | 0: nickname базы<br>1: имя комнаты | str (31)<br>str (63) | не задано | База и комната, к которым относится скрипт (`pub::GetBaseID`, `pub::GF::FindLocation`). Последующие `relocate_player`/`start_room` по умолчанию берут базу отсюда. Скрипт запускается, только если эта база — текущая база игрока. | [L63](../../../src/Content/RtcScripts.cpp#L63) |
| `greet`, `walkup`, `action`, `offer`, `decision`, `accept`, `decline`, `reject` | 0: имя Thorn-скрипта | str (63) | нет | Скрипт, проигрываемый на этом этапе встречи. Внутренние коды типов: `greet` 0, `walkup` 1, `action` 2, `offer` 3, `decision` 4, `accept` 5, `decline`/`reject` 6 (оба слова дают 6). Используется первая подходящая запись типа. | [L75](../../../src/Content/RtcScripts.cpp#L75) |
| `autoplay` | нет | - | false | Наличие ключа (с любым значением) включает автозапуск. | [L92](../../../src/Content/RtcScripts.cpp#L92) |
| `mission_text_id` | 0: id строки | int | 0 | Текст миссии, которую предлагает персонаж. | [L96](../../../src/Content/RtcScripts.cpp#L96) |
| `nooffer_text_id` | 0: id строки | int | 0 | Текст, показываемый при отсутствии предложения. | [L102](../../../src/Content/RtcScripts.cpp#L102) |
| `relocate_player` | 0: имя комнаты или `Spaceflight`<br>1: nickname базы (необязательно) | str (63)<br>str | 0 | Куда перемещается игрок при выполнении скрипта; `Spaceflight` (без учёта регистра) = старт в космос. | [L106](../../../src/Content/RtcScripts.cpp#L106) |
| `start_room` | 0: имя комнаты<br>1: nickname базы (необязательно) | str (63)<br>str | 0 | Комната, в которой стартует скрипт. | [L123](../../../src/Content/RtcScripts.cpp#L123) |

Любой другой ключ игнорируется (но всё равно создаёт и отбрасывает запись действия).

### `[Reserve]`

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `spot` | 0: имя размещения | str | нет | Размещение персонажа («spot») в комнате скрипта, зарезервированное для сцены (`pub::GF::GetCharacterPlacementByName`); имена, которые не разрешаются, отбрасываются. Можно повторять. | [L152](../../../src/Content/RtcScripts.cpp#L152) |

### `[Char]`

Одна секция на роль персонажа.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `actor` | 0: имя роли | str (31) | пусто | Имя роли. Если `npc` ещё не задан, персонажем становится конкретный NPC (из `specific_npc.ini`) с тем же nickname. | [L172](../../../src/Content/RtcScripts.cpp#L172) |
| `spot` | 0: имя размещения | str | -1 | Размещение, где стоит роль (результат поиска не проверяется). | [L185](../../../src/Content/RtcScripts.cpp#L185) |
| `npc` | 0: nickname конкретного NPC | id | нет | Выбирает персонажа из `specific_npc.ini`; заменяет любого предыдущего. Неизвестный nickname игнорируется. | [L191](../../../src/Content/RtcScripts.cpp#L191) |
| `fidget` | 0: имя fidget-скрипта | str | пусто | Скрипт idle-анимации. | [L199](../../../src/Content/RtcScripts.cpp#L199) |

## `specific_npc.ini`

`missions\specific_npc.ini` определяет именованных сюжетных персонажей; читается один раз через `FStoryLauncher` ([StoryLauncher.cpp:36](../../../src/Content/StoryLauncher.cpp#L36)). Каждая секция `[NPC]` становится описанием персонажа через `LoadNpcCharacterDesc` ([NpcShips.cpp:228](../../../src/Content/NpcShips.cpp#L228)); повторяющийся nickname сохраняет первую запись. Те же ключи используются для записей персонажей в `mBases.ini` (`[GF_NPC]`, [systems-and-bases.md](systems-and-bases.md)). Запись «полная» только если её часть тела, голос и индивидуальное имя ненулевые; результат загрузчика не проверяется, поэтому неполные записи всё равно сохраняются.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `nickname` | 0 | id | пусто | Ключ записи. | [NpcShips.cpp:196](../../../src/Content/NpcShips.cpp#L196) |
| `base_appr` | 0: nickname костюма | str | нет | Начинает с именованного костюма (`pub::GetCostumeID`); применяется, только если имя разрешилось. Последующие `head`/`body`/... переопределяют отдельные части. | [L162](../../../src/Content/NpcShips.cpp#L162) |
| `head` | 0: nickname части тела | str | 0 | Часть «голова» (`pub::GF::GetBodyPart`, тип 1). | [L170](../../../src/Content/NpcShips.cpp#L170) |
| `body` | 0 | str | 0 | Часть «тело» (тип 0). | [L175](../../../src/Content/NpcShips.cpp#L175) |
| `lefthand`, `righthand` | 0 | str | 0 | Части «руки» (типы 2 и 3). | [L180](../../../src/Content/NpcShips.cpp#L180) |
| `accessory` | 0: nickname аксессуара | str | нет | Добавляет аксессуар (можно повторять, без проверки границ). | [L190](../../../src/Content/NpcShips.cpp#L190) |
| `voice` | 0: nickname голоса | str | 0 | `pub::GetVoiceID`. | [L203](../../../src/Content/NpcShips.cpp#L203) |
| `individual_name` | 0: id строки | int | 0 | Отображаемое имя. | [L208](../../../src/Content/NpcShips.cpp#L208) |
| `affiliation` | 0: nickname фракции | str | `0xFFFFFFFF` | Группа репутации. | [L213](../../../src/Content/NpcShips.cpp#L213) |

## Generic-скрипты баров (`genericScripts.ini`)

`scripts\gcs\genericScripts.ini` перечисляет Thorn-скрипты, которые персонажи баров выбирают для idle/«fidget»-поведения. Читается через `LoadGenericScripts` ([GenericScripts.cpp:82](../../../src/Content/GenericScripts.cpp#L82)) один раз на контроллер поддержки баз. Читаются только секции `[GenericScripts]`; значения состояния `set_*` сбрасываются в «не задано» в начале каждой секции.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `set_gender` | 0 | str | не задано (2) | `male` (без учёта регистра) = 0; любое другое слово (например, `female`) = 1. | [L100](../../../src/Content/GenericScripts.cpp#L100) |
| `set_posture` | 0 | str | не задано (2) | `stand` (без учёта регистра) = 0; любое другое слово (например, `sit`) = 1. | [L104](../../../src/Content/GenericScripts.cpp#L104) |
| `set_segment` | 0: имя сегмента | str | не задано (0x42) | Должно быть одним из 66 имён ниже (без учёта регистра); неизвестное имя оставляет сегмент не заданным. | [L108](../../../src/Content/GenericScripts.cpp#L108) |
| `script` | 0: имя файла Thorn-скрипта | str | нет | Добавляет скрипт в список для (posture, gender, segment), заданных предшествующими строками `set_*`. Можно повторять. **Все три ключа `set_*` должны идти первыми**: при не заданном/неизвестном значении код индексирует за пределами своей таблицы (неопределённое поведение, как в retail). Существование имён не проверяется. | [L121](../../../src/Content/GenericScripts.cpp#L121) |

Допустимые имена `set_segment` ([GenericScripts.cpp:38](../../../src/Content/GenericScripts.cpp#L38)): `FIDGET`, `A01aG`, `A02aG`, `A02cG`, `A02dG`, `A02eB`, `A03aG`, `A04aG`, `C01aR`, `D01aN`, `D01bG`, `D01cG`, `D01dG`, `D01fG`, `D01fGm`, `D01hG`, `D01kG`, `D01kGm`, `D02aP`, `D02aPm`, `D02bP`, `D02dP`, `D02eP`, `D03aN`, `D04aG`, `D04bB`, `D05aP`, `D05bP`, `D06aG`, `D07aG`, `D07bG`, `F01aB`, `F01bB`, `F01dG`, `F01eG`, `F01fG`, `F01jG`, `F02aP`, `F03aR`, `F03bR`, `F03cR`, `F03dR`, `F04aG`, `F04dG`, `F04fG`, `F04gP`, `F05aG`, `F05bG`, `F05cG`, `F06aP`, `F07aP`, `F07bP`, `F07cP`, `F08dR`, `F08bR`, `F08cR`, `F09bP`, `F10aR`, `F10bR`, `F10bRm`, `W01cG`, `W01bB`, `W02aF`, `W03bB`, `W04bP`, `W04cP`. Набор — фиксированная таблица в коде; `FIDGET` — пул, используемый для idle-fidget'ов.

## `GCSExcl.ini`

`scripts\GCS\GCSExcl.ini` перечисляет скрипты, которые нельзя давать данному голосу. Читается через `LoadGcsExclusions` ([GenericScripts.cpp:726](../../../src/Content/GenericScripts.cpp#L726)); читаются только секции `[GCS_Exclusions]`.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `voice` | 0: nickname голоса | str | - | Начинает (или продолжает) список исключений этого голоса (`pub::GetVoiceID`). Пустое имя оставляет в силе id предыдущего голоса. | [L747](../../../src/Content/GenericScripts.cpp#L747) |
| `script` | 0: имя скрипта | str | - | Скрипт, исключённый для текущего голоса (можно повторять). Сравнение с именами из `genericScripts.ini` выполняется без учёта регистра и по полному имени. `script` до любого `voice` пишет через недействительный итератор (поведение retail; реальные файлы начинаются с `voice`). | [L761](../../../src/Content/GenericScripts.cpp#L761) |

## Голоса: `audio\voices_*.ini`, `missions\voice_properties.ini`

### Длительности реплик (`voices_space_male.ini`, `voices_space_female.ini`, `voices_recognizable.ini`)

Читаются через `LoadVoiceLines` ([Voices.cpp:28](../../../src/Content/Voices.cpp#L28)) и сортируются по (голос, сообщение), чтобы можно было искать длительность реплик связи. Все три файла делят одну таблицу голосов.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `[Voice]` `nickname` | 0: nickname голоса | str | предыдущий голос (0 в начале файла) | Выбирает голос, которому принадлежат следующие секции `[Sound]` (`pub::GetVoiceID`). | [L35](../../../src/Content/Voices.cpp#L35) |
| `[Sound]` `msg` | 0: nickname сообщения | nick | 0 | Id голосового сообщения (`pub::GetNicknameId`). | [L49](../../../src/Content/Voices.cpp#L49) |
| `[Sound]` `duration` | 0: секунды | float | -1.0 | Длина реплики. Секция `[Sound]`, у которой duration так и осталась ровно -1.0, отбрасывается. | [L51](../../../src/Content/Voices.cpp#L51) |

### Свойства голосов (`[mVoiceProp]`)

Читаются из тех же трёх файлов голосов **и** из `missions\voice_properties.ini` через `LoadVoiceProperties` ([RandomMissionGen.cpp:1693](../../../src/Content/RandomMissionGen.cpp#L1693)). Секция для голоса, у которого уже есть запись, обновляет эту запись; флаги и счётчики объединяются.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `voice` | 0: nickname голоса | str | нет | Выбирает запись. **Должен быть первым ключом** секции: последующие ключи пишут через запись, созданную `voice` (секция без `voice` разыменовывает нулевую запись, как в retail). | [L1711](../../../src/Content/RandomMissionGen.cpp#L1711) |
| `gender` | 0 | str | нет флага | `male` (без учёта регистра) выставляет флаг 0x1; `female` (**с учётом регистра**) выставляет 0x2; другие слова ничего не делают. | [L1740](../../../src/Content/RandomMissionGen.cpp#L1740) |
| `supports_roles` | 0...: слова ролей | str | нет | Слова с учётом регистра, каждое добавляет флаги через OR: `combat_lev_1` 0x4000, `combat_lev_2` 0x4, `combat_lev_3` 0xc004, `scan` 0x8, `hail_hailee` 0x10, `hail_dock` 0x20, `dockrequest_dock` 0x40, `dockrequest_docker` 0x80, `behavior` 0x100, `misc_chatter` 0x200, `fidget` 0x400, `combat_dock` 0x800, `random_mission_ship` 0x1000, `mission_commission` 0x2000. Остальные слова игнорируются. | [L1749](../../../src/Content/RandomMissionGen.cpp#L1749) |
| `permutation_count` | 0: nickname сообщения<br>1: количество | nick<br>int | нет | Сколько вариантов (перестановок) этого сообщения существует у голоса; повторяющийся nickname сохраняет первое количество. | [L1786](../../../src/Content/RandomMissionGen.cpp#L1786) |

## `PTough.ini`

`missions\PTough.ini` сопоставляет капитал игрока его рангу («toughness»). Читается через `LoadPlayerToughnessScale` ([Support.cpp:133](../../../src/Content/Support.cpp#L133)): читается только первая секция `[PlayerToughnessScale]`.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `ptough_graph_pt` | 0: опыт (капитал)<br>1: toughness (ранг) | int, int | нет | Одна точка графика. Можно повторять. Опыт принудительно делается неубывающим (меньшее значение заменяется предыдущим максимумом); toughness ограничивается диапазоном 1..40 (0x28). В мультиплеере таблица продлевается за последнюю точку повторяющимися шагами `x + x/8`, пока не превысит 999 999 999, при этом toughness равен индексу в таблице. | [L150](../../../src/Content/Support.cpp#L150) |

## Записи NPC баз (`bribe`, `know`, `rumor`)

Эти ключи встречаются внутри секций `[GF_NPC]` файла `missions\mBases.ini` (сама секция и её остальные ключи — `misn`, `room`, ключи внешности — относятся к [systems-and-bases.md](systems-and-bases.md)); парсеры находятся в [BaseNpcRecords.cpp](../../../src/Content/BaseNpcRecords.cpp), цикл — в `LoadBaseNpc` ([BaseDB.cpp:55](../../../src/Content/BaseDB.cpp#L55)). Запись, для которой `Parse` возвращает false, **молча отбрасывается**. Уровни репутации (`level` ниже) индексируют таблицу порогов 0.0, 0.2, 0.4, 0.6 ([BaseNpcRecords.cpp:37](../../../src/Content/BaseNpcRecords.cpp#L37)): отношение игрока к фракции NPC должно быть не меньше этого значения.

| Ключ / запись | Значения (позиционные) | Тип | По умолчанию | Значение / допустимые значения | Источник |
|---|---|---|---|---|---|
| `bribe` | 0: nickname фракции<br>1: стоимость<br>2: id текста | str<br>int<br>int | - | NPC предлагает за `cost` откупиться от враждебности указанной фракции. Отбрасывается, если фракция неизвестна или стоимость равна 0. Предлагается, только пока отношение игрока к фракции ниже 0.6. Значение 2 — текст предложения (id таблицы строк). | [L40](../../../src/Content/BaseNpcRecords.cpp#L40) |
| `know` | 0: id текста A<br>1: id слуха/информации<br>2: цена<br>3: уровень | int | - | NPC продаёт информацию. Отбрасывается, если `level >= 4` (без знака). Предлагается, пока отношение достигает уровня и игрок ещё не знает этот элемент (id слуха в значении 1). Значение 2 — цена; значения 0 и 1 — id таблицы строк. | [L72](../../../src/Content/BaseNpcRecords.cpp#L72) |
| `rumor`, `rumor_type2` | 0: с этапа<br>1: до этапа<br>2: уровень<br>3...: id слухов (до первого пустого значения) | str<br>str<br>int<br>int | см. текст | Слух, который NPC может рассказать между двумя этапами сюжета (имена из [названий этапов](#названия-этапов-сюжета), без учёта регистра). Имя этапа, которого нет в таблице, повторно использует предыдущий результат поиска (значение 0: неопределённое значение; значение 1: результат значения 0). `level` **не проверяется** (индекс >= 4 читает за пределами таблицы порогов). В записи умещается не более 8 id, но цикл не проверяет границу. Запись загружается всегда. Вес: 0x40 для слуха, ограниченного с обеих сторон (с этапа > 0 и до этапа < 42), иначе `(level + 1) * 5`. | [L82](../../../src/Content/BaseNpcRecords.cpp#L82) |

## Названия этапов сюжета

Названия, используемые в `rumor`/`rumor_type2` и в `RankDiff.ini` (`rank_diff`); таблица — `GContentStageNames` ([RandomMissionDB.cpp:64](../../../src/Content/RandomMissionDB.cpp#L64)). Значения — номера этапов сюжета (те же числа встречаются в `MissionNum` сохранённой игры). Сравнение без учёта регистра.

| Имя | Значение | Имя | Значение | Имя | Значение |
|---|---|---|---|---|---|
| `base_0_rank` | 0 | `mission_01a_loaded` | 1 | `mission_01a_accepted` | 2 |
| `mission_01b_loaded` | 3 | `mission_01b_accepted` | 4 | `freetime_01_02` | 5 |
| `mission_02_loaded` | 7 | `mission_02_accepted` | 8 | `freetime_02_03` | 9 |
| `mission_03_loaded` | 10 | `mission_03_accepted` | 11 | `freetime_03_04` | 12 |
| `mission_04_loaded` | 13 | `mission_04_accepted` | 14 | `freetime_04_05` | 15 |
| `mission_05_loaded` | 16 | `mission_05_accepted` | 17 | `freetime_05_06` | 18 |
| `mission_06_loaded` | 19 | `mission_06_accepted` | 20 | `freetime_06_07` | 21 |
| `mission_07_loaded` | 22 | `mission_07_accepted` | 23 | `freetime_07_08` | 24 |
| `mission_08_loaded` | 25 | `mission_08_accepted` | 26 | `freetime_08_09` | 27 |
| `mission_09_loaded` | 28 | `mission_09_accepted` | 29 | `freetime_09_10` | 30 |
| `mission_10_loaded` | 31 | `mission_10_accepted` | 32 | `freetime_10_11` | 33 |
| `mission_11_loaded` | 34 | `mission_11_accepted` | 35 | `freetime_11_12` | 36 |
| `mission_12_loaded` | 37 | `mission_12_accepted` | 38 | `freetime_12_13` | 39 |
| `mission_13_loaded` | 40 | `mission_13_accepted` | 41 | `mission_end` | 42 |

(Значение 6 не используется.)
