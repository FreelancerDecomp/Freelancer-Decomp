# Опкоды сюжетных скриптов: `Cnd_*` и `Act_*`

Справочник аргументов всех ключей `Cnd_*` (условия) и `Act_*` (действия) секции `[Trigger]` сюжетных
миссий (`DATA\MISSIONS\M01A\m01a.ini` … `M13\m13.ini`). Устройство движка триггеров и формат
`[Trigger]` — в [content_mission_script.md](content_mission_script.md); реализация — классы
`src/Content/MissionCnd.*` и `MissionAct.*` (на каждый ключ свой класс с разбором строки и
исполнением). Таблицы получены из листинга `Content.dll`; при расхождении верны листинг и исходник.

Обозначения аргументов: `sN` — `get_value_string(N)`, `iN` — целое, `fN` — число с плавающей точкой,
`bN` — логическое. Необязательные параметры проверяются как «не задан». Имена ключей сравниваются без
учёта регистра. Объекты в аргументах — nickname (`CreateID`) либо метка из секций
`[MsnShip]`/`[MsnSolar]`/…; игрок — `Player`.

Общие значения:

- Репутация (`REP_*`): `REP_HOSTILE_MAXIMUM`, `REP_HOSTILE_THRESHOLD`, `REP_NEUTRAL_HOSTILE`,
  `REP_NEUTRAL`, `REP_NEUTRAL_FRIENDLY`, `REP_FRIEND_THRESHOLD`, `REP_FRIEND_MAXIMUM`; иначе число.
- Дистанция (`Cnd_DistShip`, `Cnd_DistVec`): `inside` / `outside`; частота проверки `TICK_AWAY` /
  `TICK_TOWARDS` / `TICK_ALWAYS`.
- `no_params` означает «аргументов нет».

## Условия (44)

| Ключ | Аргументы | Назначение |
|---|---|---|
| `Cnd_BaseEnter` | `s0` база \| `ANY` | Игрок вошёл на базу (`GetBaseID`) |
| `Cnd_BaseExit` | то же | Игрок покинул базу |
| `Cnd_CargoScanned` | `s0` сканер, `s1` цель (оба не empty) | Скан груза пары объектов. INI: `Player, PEscort1` |
| `Cnd_CharSelect` | `s0` NPC, `s1` room, `s2` база | Выбор персонажа в комнате базы (`FindLocation`). INI: `juni, Bar, Li01_01_Base` |
| `Cnd_CommComplete` | `s0` comm id (`GetNicknameId`); опц. `s1` | Коммуникация/оверлей доиграла. INI: `DX_M01_0700_KING` |
| `Cnd_CmpToPlane` | `s0` inside\|outside, `s1` объект, `f2…f5` плоскость | Объект по одну сторону плоскости (в SP INI не встречен) |
| `Cnd_Destroyed` | `s0` объект `CreateID`; опц. `i1` счёт; `s2` `EXPLODE`\|`SILENT`; `s3` `ALL`\|`ALL_IGNORE_LANDING` | Уничтожение. INI: `escort, 1, EXPLODE` |
| `Cnd_DistCircle` | `s0` кто, `s1` кольцо/объект | Внутри радиуса объекта (`GetLocation`/`GetRadius`). INI: `Player, Br05_track_ring_2` |
| `Cnd_DistShip` | `s0` inside\|outside, `s1` A, `s2` B, `f3` дист.; опц. tick | Дистанция корабль–корабль/объект. INI: `inside, Player, Li01_Trade_Lane_Ring_146, 2000` |
| `Cnd_DistVec` | `s0` inside\|outside, `s1` объект, `f2,f3,f4` xyz, `f5` r; опц. tick | Дистанция до точки. INI: `inside, transport1, 28465, 165, -64500, 7500` |
| `Cnd_DistVecLbl` | `s0` inside\|outside\|any, `s1` A, `s2` label `CreateID`, `f3…` | Как DistVec, якорь — label (в m0N.ini нет; есть `Cnd_DistVecLbL`) |
| `Cnd_EncLaunched` | `s0` nickname encounter `CreateID` | Запущен случайный/сюжетный encounter |
| `Cnd_HasMsn` | `s0` `yes` \| `no` | Есть ли у игрока активный оффер/миссия. INI: `yes` |
| `Cnd_HealthDec` | `s0` объект, `f1` порог доли HP; опц. `s2` | HP упало ниже порога. INI: `prisonship, 0.04` |
| `Cnd_InSpace` | `b0` | Игрок в космосе vs на базе. INI: `true` |
| `Cnd_InTradelane` | `b0`, опц. `s1` кольцо | В (или не в) tradelane. INI: `false, Lambda4` |
| `Cnd_InZone` | `b0`, `s1` кто; опц. `s2` `OBJECT_ZONE`\|`EXCLUSION_ZONE`\|`NEBULA_ZONE`\|`DUST_ZONE` | В зоне. INI: `false, Player, OBJECT_ZONE` |
| `Cnd_JumpgateAct` | `s0` gate `GetNicknameId` | Активация прыжковых ворот |
| `Cnd_JumpInComplete` | `s0` система \| `ANY` (`GetSystemID`) | Прыжок в систему завершён. INI: `Li02` |
| `Cnd_LaunchComplete` | `s0` кто (`GetNicknameId`); опц. `s1` | Старт с базы/дока завершён. INI: `Player` |
| `Cnd_LocEnter` | `s0` room, `s1` база | Вход в комнату базы. INI: `Bar, Li01_01_Base` |
| `Cnd_LocExit` | то же | Выход из комнаты. INI: `Deck2, Li01_03_base` |
| `Cnd_LootAcquired` | `s0` лут `CreateID`, `s1` кто; опц. `i2` | Подобран лут. INI: `ashcroft_pod, Player` |
| `Cnd_MsnResponse` | `s0` `accept` \| `reject` | Ответ на оффер миссии |
| `Cnd_NPCSystemEnter` | `s0` система или `ANY`; затем до 50 имён NPC (`s1…`); `Player` в список не заносится, но всегда считается участником | NPC вошёл в систему. INI: `Li03, King, Player` |
| `Cnd_NPCSystemExit` | `s0` система \| `ANY`; опц. список NPC | NPC вышел из системы |
| `Cnd_PlayerLaunch` | нет (`no_params`) | Игрок стартовал в космос |
| `Cnd_PlayerManeuver` | `s0` тип (`BehaviorTypes::get_behavior_id`); опц. `s1` цель; токен `inverse` | Манёвр игрока. INI: `Dock, Li01_Trade_Lane_Ring_146` |
| `Cnd_PopUpDialog` | `s0` `YES`\|`NO`\|`LATER`\|`CLOSE` | Закрытие popup с данным ответом |
| `Cnd_ProjHit` | `s0` цель, `i1` число попаданий; опц. `s2` стрелок | Попадание снарядом. INI: `r_cruiser, 1, o_hostile1` |
| `Cnd_ProjHitShipToLbl` | `s0` корабль, `i1` hits, опц. `s2` label `CreateID` | Попадание корабля по label. INI: `us, 3, base_hostile1` |
| `Cnd_RTCDone` | `s0` путь RTC ini | RTC-сцена закончилась |
| `Cnd_RumorHeard` | как CharSelect (NPC / room / база) | Слух услышан (в m0N.ini нет) |
| `Cnd_SpaceEnter` | система \| `no_params` | Игрок вышел в космос системы |
| `Cnd_SpaceExit` | то же | Игрок ушёл из космоса (на базу / прыжок) |
| `Cnd_SystemEnter` | `s0` dest \| `ANY`; опц. `s1` prev | Вход в систему. INI: `Li02, Li01` |
| `Cnd_SystemExit` | то же | Выход из системы. INI: `ANY` |
| `Cnd_TetherBroke` | `s0` A, `s1` B, `f2` dist, `f3` time; опц. `f4` | Трос/эскорт-привязка порвана. INI: `juni, Player, 1500, 30, 2500` |
| `Cnd_Timer` | `f0` секунды; опц. `f1` — верхняя граница (случайное значение между `f0` и `f1`); опц. `s2` `RUN_ON_BASE` / `RUN_IN_SPACE_ONLY` | Таймер; `s2` задаёт, считает ли он на базе или только в космосе. INI обычно `5` |
| `Cnd_TLEntered` | `s0` кто, `s1` кольцо A, `s2` кольцо B (`GetNicknameId`) | Вход в tradelane |
| `Cnd_TLExited` | `s0` кто; `s1` кольцо или `ANY`; опц. `s2` второе кольцо | Выход из tradelane. INI: `Player, Li01_Trade_Lane_Ring_141` |
| `Cnd_True` | нет | Всегда истинно при активации триггера (сообщение `0x1000`) |
| `Cnd_WatchTrigger` | `s0` nickname чужого триггера; `s1` его состояние (`ON` в INI — просто nickname состояния) | Ждёт, пока другой триггер перейдёт в указанное состояние. INI: `com_0380, ON` |
| `Cnd_WatchVibe` | `s0`, `s1` объекты; `s2` порог (`REP_*` или число); опц. `s3` сравнение `lt` / `gt` / `eq` | Репутация A к B против порога. INI: `dexter, Player, REP_HOSTILE_THRESHOLD, eq` |

## Действия (85)

| Ключ | Аргументы | Назначение |
|---|---|---|
| `Act_ActTrig` | `s0` nickname триггера | Включает другой `[Trigger]` |
| `Act_AddAmbient` | `s0` путь `.thn`; `s1` комната; `s2` база | Добавляет ambient THN в комнату базы. INI: `scripts\Bases\…thn, Equipment, Li01_01_Base` |
| `Act_AddRTC` | `s0` путь RTC; опц. `s1` `repeatable` | Ставит RTC |
| `Act_AdjAcct` | `i0` дельта кредитов | Меняет счёт игрока. INI: `750` |
| `Act_AdjHealth` | `s0` объект; `f1` доля HP | Устанавливает относительное здоровье объекта. INI: `escort, 0.3` |
| `Act_CallThorn` | `s0` путь `.thn`; опц. `s1` объект камеры (по умолчанию `Player`) либо три числа (позиция) | Проигрывает THN-сцену. INI: `missions\m01a\m01a_fp7_01.thn, fp7base` |
| `Act_ChangeState` | `s0` токен состояния `FAIL` / `SUCCEED` / `NO_STATE`; опц. `i1` ids сообщения | Меняет `MissionStateNum` (+ ids для NN). INI: `FAIL, 21960` |
| `Act_Cloak` | `s0` объект, `b1` | Плащ. INI: `fp7base_attacker1, true` |
| `Act_DeactTrig` | `s0` триггер | Выключает триггер |
| `Act_DebugMsg` | вся строка значения | Отладочное сообщение |
| `Act_Destroy` | `s0` объект; опц. `s1` `EXPLODE` / `SILENT` | `pub::SpaceObj::Destroy`. INI: `fp7base_escort1, SILENT` |
| `Act_DisableEnc` | `s0` encounter | Выключает encounter (в m0N нет) |
| `Act_DisableFriendlyFire` | список имён объектов (до первого пустого параметра) | Снимает friendly-fire с перечисленных. INI: `navy_patrol1` |
| `Act_DisableTradelane` | `s0` кольцо `GetNicknameId` | Отключает tradelane |
| `Act_DockRequest` | `s0` док | Запрос стыковки (в m0N нет) |
| `Act_EnableEnc` | `s0` encounter | Включает encounter |
| `Act_EnableManeuver` | `s0`; `b1` | Разрешает/запрещает манёвр |
| `Act_EtherComm` | `s0` голос; `i1`; `s2` цель; `s3` comm; `i4` (1…10); `s5` голова (`NO_HEAD`); `s6` тело; опц. аксессуары | Эфирная коммуникация (INI-ключ часто `Act_Ethercomm`). INI: `M01a_newark, 196773, Player, DX_M01_0140_…` |
| `Act_ForceLand` | `s0` база → `GetBaseID` | Принудительная посадка |
| `Act_GCSClamp` | `b0` | Clamp GCS (групповой ИИ). INI: `true` |
| `Act_GiveMB` | `s0` корабль, `s1` metabehavior `CreateID` | Выдаёт metabehavior (в m0N нет) |
| `Act_GiveNNObjs` | `no_params` | Выдаёт накопленный список NN-objectives |
| `Act_GiveObjList` | `s0` объект; `s1` список objective | Вешает objective list на корабль. INI: `fp7base_escort1, ol_fp7_goto_convoy1` |
| `Act_HostileClamp` | `b0` | Clamp враждебности мира. INI: `true` |
| `Act_Invulnerable` | `s0` объект, `b1`; опц. `b2`, `f3` HP. INI: `escort, true, false, 0.99` | Неуязвимость |
| `Act_Jumper` | `s0` объект; `b1` | Флаг jumper (как ключ `[MsnShip]`) |
| `Act_LightFuse` | `s0` объект, `s1` fuse; опц. `f2`. INI: `fp7base, freeport7_main_fuse1` | Зажигает fuse |
| `Act_LockDock` | `s0` кто; `s1` док; `s2` `lock` / `unlock` | Блокирует или открывает док |
| `Act_LockManeuvers` | `b0` | `pub::AI::lock_maneuvers`. INI: `true` |
| `Act_MarkObj` | `s0` объект, `i1` флаг. INI: `escort, 1` | Маркер на HUD/карте |
| `Act_MovePlayer` | `f0,f1,f2` позиция; `f3` | Телепорт игрока |
| `Act_NagClamp` | `b0` | Глобальный clamp nag-реплик |
| `Act_NagDistLeaving` | `s0` id nag `CreateID`; `s1` кто; `s2` цель или `position`; ids/дист.; `NAG_IN_COMBAT`\|`NAG_OUT_OF_COMBAT`\|`NAG_ALWAYS` | Nag «уходишь». INI: `std, escort, escort, 21970, 4000, NAG_IN_COMBAT` |
| `Act_NagDistTowards` | `s0` `OBJ` / `POS`; `s1` id nag; `s2` кто; для `OBJ` — `s3` цель, для `POS` — координаты; затем опц. ids, дистанция, `NAG_IN_COMBAT` / `NAG_OUT_OF_COMBAT` / `NAG_ALWAYS` | Nag «приближаешься» |
| `Act_NagGreet` | `s0` id nag; `s1` объект | Приветственный nag (`nag_greet_combat` / `nag_greet_normal`). INI: `greet, escort` |
| `Act_NagOff` | `s0` id nag | Выключает nag. INI: `greet` |
| `Act_NNPath` | `i0`, `i1` ids; затем либо `s2` объект, либо `f2,f3,f4` позиция и опц. `s5` система | Путь в навигаторе |
| `Act_NNIds` | `i0` ids; опц. `s1` `OBJECTIVE`\|`HISTORY`\|`OBJECTIVE_HISTORY` | `pub::Player::DisplayMissionMessage`. INI: `30000, HISTORY` |
| `Act_PlayerCanDock` | `b0`; опц. список доков (`s1…`) | Разрешение стыковки игрока |
| `Act_PlayerCanTradelane` | `b0`; опц. список колец (`s1…`) | Разрешение tradelane игроку |
| `Act_PlayerEnemyClamp` | `i0`; `i1` | `pub::AI::set_player_enemy_clamp`: ограничение числа врагов на игроке |
| `Act_PlayerForm` | `s0` формация `CreateID` | Игрок в формации (в m0N нет) |
| `Act_PlayMusic` | `s0` `no_params` или до четырёх cue (`none` — пропуск); `s4` fade или `none`; `s5` флаг или `none` | Музыка |
| `Act_PlayNN` | `s0` nickname NN | Проиграть NN-ролик (в m0N нет) |
| `Act_PlaySoundEffect` | `s0` эффект `GetNicknameId`. INI: `depot_wallah_srtc` | Звук |
| `Act_PilotParams` | `s0` корабль, `s1` набор пилота | Меняет AI-пилота (в m0N нет) |
| `Act_PobjIdle` | `no_params` | Idle player-object. INI-ключ часто `Act_PObjIdle` |
| `Act_PopUpDialog` | `i0`, `i1` ids; опц. `s2` `YES_NO` / `YES_NO_LATER` / `CLOSE` | `pub::Player::PopUpDialog` |
| `Act_SetPriority` | `s0` label `CreateID`, `s1` `NORMAL`\|`ALWAYS_EXECUTE` | Приоритет objective. INI: `lbl_friends, NORMAL` |
| `Act_RandomPop` | `b0` | Вкл/выкл random population. INI: `false` |
| `Act_RandomPopSphere` | `f0,f1,f2` центр; `f3` радиус; `s4` `on` / `off` | Сфера исключения random pop: `on` снимает сферу, `off` добавляет её |
| `Act_RelocateForm` | формация + pos (+ ornt) | Перенос формации (в m0N нет) |
| `Act_RelocateShip` | корабль + xyz. INI: `fp7_transport1, 100000, 102000, 100000` | Перенос корабля |
| `Act_RemoveAmbient` | `s0` путь `.thn` | Снимает ambient (`pub::GF::AmbientScriptDestroy`) |
| `Act_RemoveCargo` | `s0` good; опц. `i1`. INI: `ashcroft_pod` | Удаляет груз |
| `Act_RemoveRTC` | `s0` путь RTC | Снимает RTC |
| `Act_RepChangeRequest` | `s0` система; затем до 50 групп репутации | Запрос смены репутации |
| `Act_RevertCam` | нет (`no_params`) | Камера обратно (`pub::Player::RevertCamera`) |
| `Act_RpopAttClamp` | `b0` | Clamp атак random pop |
| `Act_RpopTLAttacksEnabled` | `b0` | Атаки random pop по tradelane (в m0N нет) |
| `Act_Save` | `s0` nickname; опц. `i1` | `pub::Save` и сообщение миссии |
| `Act_SendComm` | `s0` говорящий; `s1` слушатель (`Player` — отдельная ветка); `s2` comm; опц. `i3` (1…10); опц. `s4` голос; опц. `i5`; опц. `s6` голова (`NO_HEAD`), `s7` тело и далее аксессуары | Коммуникация (радиореплика) |
| `Act_SetFlee` | `s0` `CLEAR` / `SET_POS` / `SET_OBJ`; `s1` объект; для `SET_OBJ` — цель и числа, для `SET_POS` — координаты | Поведение бегства |
| `Act_SetInitialPlayerPos` | `f0,f1,f2` позиция; `f3…f6` ориентация | Стартовая поза игрока (`SetInitialPos`/`SetInitialOrnt`) |
| `Act_SetLifeTime` | `s0` объект, `f1` сек. INI: `harmony, 100000` (ключ INI часто `Act_SetLifetime`) | Время жизни объекта |
| `Act_SetNNHidden` | `s0` objective (`all` — все); `b1` | Скрыть NN objective |
| `Act_SetNNObj` | `s0` objective; опц. `s1` `HISTORY` / `OBJECTIVE` / `OBJECTIVE_HISTORY` | Текущий NN objective |
| `Act_SetNNState` | `s0` obj `CreateID`; `s1` `ACTIVE`\|`INACTIVE`\|`COMPLETE`\|`FAILED` (INI: `active`) | Состояние NN |
| `Act_SetOffer` | `i0` ids | Текст оффера (`DisplayMissionMessage`). INI: `21805` |
| `Act_SetOrient` | `s0` объект, `f1…f4` кватернион. INI: `PEscort1, 1, 0, 0, 0` | Ориентация |
| `Act_SetRep` | `s0` кто, `s1` группа (`GetReputationGroup`); `f2` или `REP_*` | Репутация. INI: `Player, li_n_grp, 0.91` |
| `Act_SetShipAndLoadout` | `s0` корабль (`none` — не менять); `s1` loadout (`none` — не менять) | Корабль и loadout игрока |
| `Act_SetTitle` | `i0` ids. INI: `21800` | Заголовок миссии |
| `Act_SetVibe` | `s0`, `s1` объекты; `s2` `REP_*` или число; опц. `s3` второй порог | Vibe корабль к кораблю |
| `Act_SetVibeLbl` | `s0`, `s1` метки/объекты; `s2` `REP_*` или число; опц. `s3` | Vibe label к label/объекту |
| `Act_SetVibeLblToShip` | `s0` метка; `s1` корабль; `s2` `REP_*` или число; опц. `s3` | Vibe label к кораблю (в INI встречается `Act_SetVibeLbltoShip`) |
| `Act_SetVibeShipToLbl` | `s0` корабль; `s1` метка; `s2` `REP_*` или число; опц. `s3` | Vibe корабль к label |
| `Act_SetVibeOfferBaseHack` | `s0` solar | Vibe к базе оффера. INI: `dock_ring_Li01_01` |
| `Act_SpawnFormation` | `s0` nickname `[MsnFormation]`; опц. `f1,f2,f3` позиция; опц. `f4…f7` ориентация | Спавн формации |
| `Act_SpawnLoot` | `s0` nickname `[MsnLoot]`; опц. `f1,f2,f3` позиция; опц. `f4…f7` ориентация | Спавн лута |
| `Act_SpawnShip` | `s0` nickname `[MsnShip]`; опц. `s1` система; опц. `f2,f3,f4` позиция; опц. `f5…f8` кватернион (нормализуется) | Спавн корабля |
| `Act_SpawnShipRel` | `s0` nickname; `s1` система; `s2` якорь; `s3` `DIRECTIONAL` / `POINT` / `RELATIVE`; `f4,f5,f6`; опц. `f7`, `f8` | Спавн относительно другого объекта |
| `Act_SpawnSolar` | `s0` nickname `[MsnSolar]`; опц. `f1,f2,f3` позиция; опц. `f4…f7` ориентация | Спавн солара. INI: `fp7base` |
| `Act_StartDialog` | `s0` nickname диалога; опц. `i1` (1…10) | Стартует `[Dialog]`. INI: `FP7_Rendezvous_transport` |
| `Act_StaticCam` | `f0…f2` позиция; `f3…f6` ориентация; опц. `f7,f8` | Статичная камера (`SetCamera` + `SetInvincible`) |

## Секции объектов мира в `m0N.ini`

Эти секции (не `Cnd`/`Act`) читаются в начале разбора файла; спавнеры `Act_Spawn*` ссылаются на них
по nickname.

- `[MsnLoot]`: `nickname`, `archetype`, `string_id`, `position`, `rel_pos_obj`, `rel_pos_offset`,
  `orientation`, `velocity`, `angular_velocity`, `equip_amount`, `health`, `label`, `can_jettison`.
- `[MsnSolar]`: `nickname`, `position`, `orientation`, `faction`, `string_id`, `pilot`, `archetype`,
  `loadout`, `system`, `base`, `voice`, `radius`, `label`, `costume`, `visit`.
- `[MsnShip]` / `[MsnFormation]`: `nickname`, `npc`, `random_name`, `label`, `init_objectives`,
  `init_metabehavior`, `jumper`, `cargo`, `hit_points`, `radius`, `system`, `position`, `rel_pos`,
  `orientation`, `arrival_obj`, `formation`, `ship` и др.
- `[MsnRandEnc]`: `nickname`, `encounter_type` (`TL_Attack` / `SP_Attack`, …).
- `[Mission]`: `mission_title`, `mission_offer`, `reward`, `npc_ship_file`.

## Ключи, которых нет в кампанийных `m0N.ini`

Движок их принимает, но в `m01a.ini` … `m13.ini` они не встречаются: `Cnd_CmpToPlane`,
`Cnd_DistVecLbl`, `Cnd_EncLaunched`, `Cnd_JumpgateAct`, `Cnd_NPCSystemExit`, `Cnd_RumorHeard`;
`Act_DebugMsg`, `Act_DisableEnc`, `Act_DockRequest`, `Act_EnableEnc`, `Act_GiveMB`, `Act_PilotParams`,
`Act_PlayNN`, `Act_PlayerForm`, `Act_RelocateForm`, `Act_RepChangeRequest`, `Act_RpopTLAttacksEnabled`,
`Act_SetFlee`, `Act_SpawnShipRel`.
