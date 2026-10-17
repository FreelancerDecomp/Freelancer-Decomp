# Audio: SoundManager и SoundStreamer

Звук в Freelancer — два DACOM-плагина поверх DirectSound и MSACM32. `SoundManager.dll` проигрывает
короткие звуки (эффекты, пространственные источники, EAX), `SoundStreamer.dll` стримит музыку и озвучку
(WAV и MP3 через ACM). Страница нужна тому, кто трогает звуковой код порта или подключает клиентские
звуковые подсистемы. Исходники — `src/SoundManager/`, `src/SoundStreamer/`; публичные интерфейсы —
`src/include/soundmanager.h`, `src/include/soundstreamer.h`. Статусы функций, слоты и адреса — в
decomp-tracker, отклонения от retail — в [deviations.md](deviations.md).

Ни Miles, ни `mss32`, ни отдельной обёртки над `dsound.dll` в retail нет: обе DLL создают
`CLSID_DirectSound` через `CoCreateInstance`. Видео-интро (`movie.dll`, DirectShow) к этому стеку
не относится.

## Загрузка и регистрация

`[Libraries]` грузит `SoundStreamer.dll`, затем `SoundManager.dll`; в `[System]` они стоят как
`SoundManager`, `SoundStreamer`. `DllMain` каждой регистрирует фабрику (`RegisterComponent`, имена
`"SoundManager"` и `"SoundStreamer"`); `CreateInstance` требует `AGGDESC` размера `0x14`.
Объекты создаёт `SystemContainer`; `DALib::Startup` забирает их интерфейсы в `DALib::Sound`
(`ISoundManager`) и `DALib::Streamer` (`IStreamer2`).

Инициализация устройства — не в `IAggregateComponent::Initialize` (у SoundManager он возвращает 0
сразу), а в `DALib::Startup`:

1. `ISoundManager::StartupDevice(hwnd, 0x113)` — читает INI, создаёт DirectSound, первичный буфер,
   поток микшера, при необходимости включает EAX. Ошибка — `ShutdownDevice`, `Release`, `Sound = nullptr`.
2. `ISoundManager::GetDirectSound(&ds)` — отдаёт тот же `IDirectSound*` с `AddRef`.
3. `IStreamer2::InitializeDevice({size = 0x18, ds, hwnd, 0, 0, gain})` — возвращает BOOL (1/0). Если `ds`
   пуст, Streamer сам делает `CoInitialize` + `CoCreateInstance`. Ошибка — `Release`, `Streamer = nullptr`.
4. Любая из двух неудач вызывает `CriticalWarning(0xD3)`; его вердикт хоста становится результатом
   `DALib::Startup`.

`Shutdown` сначала освобождает Streamer, затем зовёт `ShutdownDevice` менеджера (Streamer должен вернуть
`AddRef`, взятый у менеджера).

## SoundManager

Объект (`0x14C` байт) отдаёт `ISoundManager` (56 слотов), `ISoundManagerStatus` (4 слота; клиент его не
использует) и вложенный подобъект на `+0x08`, который лениво запрашивает `IProfileParser` в кэш. Архетипы
звуков — отдельные объекты `ISoundArchetype` (`0x30` байт), создаются менеджером из WAV
(несжатый PCM или ACM через WavLib), не фабрикой DACOM.

Секции INI (читаются встроенным парсером DACOM; его отсутствие не мешает `StartupDevice` — ключи просто
пропускаются): `[SoundManager]` — `deviceID`, `3D_SW_Algorithm` (`BASIC`/`LIGHT`/`FULL`), `useNoHW`,
`use2DHW`/`use3DHW`, `createAll2DInSoftware`, `rightHandedCoordinates`, `speakerConfiguration`,
`maxSoundChannels`, `max3DPan`, `ALLOW_HW_TO_SW`, `ALLOW_SW_TO_HW`, `FORCE_FREQ_CONTROL_TO_SW`,
`NEVER_SET_START_POS`, `DISPLAY_EXTRA_DEBUG_INFO`, `useEAX`; `[AudioProperties]` — `useEAX`,
`reverbBaseEnvironment`, `reverbMasterVolume`, `reverbDecayTime`, `reverbDamping`, `reverbMix`. Булевы значения
понимают `true/yes/false/no`, числа — десятичные и `0x`-шестнадцатеричные.
`speakerConfiguration` — только число (в комментарии retail-INI: HEADPHONES 1, STEREO 2, QUAD 7,
SURROUND 8); текстовых значений код не разбирает.

Что делает `StartupDevice`: `CoInitialize` → `CoCreateInstance(IDirectSound)` → `Initialize` (сначала
устройство по `deviceID`, потом по умолчанию) → `SetCooperativeLevel(DSSCL_PRIORITY)` → `GetCaps`
(из числа свободных HW-буферов выбирается максимум каналов) → первичный буфер → проверка HW-возможностей
пробным буфером 22050 Гц/16 бит/стерео → слушатель 3D, формат первичного буфера, EAX.

Микшер (`MixerTick`, слот 46, `+0xB8`; клиент вызывает его каждый кадр): удаляет инстансы, чьи play-key исчезли, создаёт буферы
для новых, отбрасывает слишком далёкие, считает затухание по расстоянию (обратный корень и `10·log10`,
если у буфера нет DS3D), добавляет усиление архетипа, мастер-усиление и громкость клиента, программный пан
(`CTRLPAN`) вместо 3D, частоту, тик EAX слушателя. Потерянные буферы (`DSERR_BUFFERLOST`) логируются
как ошибка, но не восстанавливаются. Play/Stop инстанса — слоты 47/48; зацикливание берётся у клиентского
source-объекта.

**Play key.** Менеджер сам источники не создаёт: клиент (`src/freelancer/Client/menu_sound_arch.cpp`)
передаёт свой объект `ISoundSource` (`AttachPlayKey`), и микшер каждый тик опрашивает его (позиция,
громкость, дальность, жив ли). Слот 33 (WAV) принимает `IFileSystem*`, а не путь.

**EAX.** Через `IKsPropertySet`: четыре ключа `reverb*` уходят одним `Set` (`DSPROPSETID_EAX20_BufferProperties`,
`ulId = 0`, 16 байт); `reverbMix` читается, но нигде не используется. Параметры слушателя пишутся в
`DSPROPSETID_EAX20_ListenerProperties`.

**GUID-ловушка.** Хвост retail-IID `IDirectSound` / `IDirectSound3DListener` / `IDirectSound3DBuffer` —
`E5 60`, а не `A7 70`, как в SDK-заголовке `dsound.h`; CLSID совпадает с SDK. Порт копирует 16 байт retail
(`SoundManagerInternal.h`, `audio/dsound8.h`), а не макросы SDK. Такое же расхождение у `IKsPropertySet`.

## SoundStreamer

Объект (`0x7C` байта) отдаёт `IStreamer2` (15 слотов; `IStreamer` — тот же vtable) и
`IAggregateComponent`. Это синглтон: второй экземпляр не создаётся. Handle потока — непрозрачный
`StreamingSound*` (`0x110` байт), не DACOM-интерфейс.

- **Слоты.** Инициализация устройства (3), `CreateStream(path, IFileSystem*, flags)` (4), уничтожить /
  остановить / запустить (5/6/7), громкость (8/9), статус (10), пан (13/14); слоты 11 и 12 всегда
  возвращают 0. В `flags` значимы только бит 0 (автозапуск) и бит 1 (цикл).
- **CreateStream.** Без `IFileSystem` создаёт файловую систему сам (`AGGDESC 0x34`, `"FileSystem"`); с
  переданной проверяет через слот `+0x70` (атрибуты файла: `-1` или каталог — отказ). Файл мапится
  (`+0x7C` / `+0x3C` / `+0x40`) и разбирается как RIFF/WAVE: обязательные `fmt ` и `data`, необязательные
  `fact` и `trim`. `trim` — реальный MP3-чанк «encoder delay»: на первом блоке декодирования отбрасывается
  `trim × байт/сэмпл` декодированного PCM. PCM кладётся в буфер DirectSound сразу, остальное — через ACM
  (`acmFormatSuggest`/`acmStreamOpen`/`Convert`).
- **Кодек MP3.** Ищется драйвер ACM с именем `Fraunhofer IIS MPEG Layer-3 Codec (advanced)`. Современная
  Windows часто показывает `... (decode only)`; по просьбе владельца порт принимает и это имя
  (в SoundStreamer и в SoundManager). Без кодека — `FDUMP` «missing MP3 codec?».
- **Рабочий поток.** `CreateThread` (4 КБ стека, приоритет highest; `CoInitialize(NULL)`, т. е. STA), цикл
  с паузой 40 мс: под критической секцией заполняет активные потоки (состояние 2 или 3), выбирая самый
  пустой, пока в буфере меньше ~0,12 с, затем переносит завершённые в список ожидающих. Поток подтверждает
  старт битом в слове состояния (до 5 с ожидания); `Shutdown` ставит бит остановки и ждёт до 2 с.
- **Состояния handle** (слово `+0x80`): 0 мёртв/ошибка, 1 готов, 2 запрошен старт, 3 играет, 6 «слив»
  разового потока до маркеров конца, 8 закончился сам, 9 остановлен клиентом. Значения 4, 5, 7 в DLL не
  присваиваются никогда. Остановленный (8/9) поток можно запустить повторно.
- **Дескриптор `InitializeDevice`** (`0x18` байт): `+0` размер, `+4` `IDirectSound*`, `+8` `HWND`, `+0x0C` и
  `+0x10` не читаются, `+0x14` усиление (0 → 1.0). Другой размер или `NULL` — `FDUMP` «Invalid
  parameters», результат 0.

## Клиентский слой и данные

Клиентская часть (в retail `Client/cmn/Sound.cpp`, `Gf/ScriptSound.cpp`, `Voice.cpp`) живёт в
`src/freelancer/Client/` (`menu_sound_arch.cpp`, `sound_config.cpp`, `voice_profiles.cpp`,
`gf_thorn_sound.cpp`); она вызывает методы интерфейсов, а не номера слотов. Thorn запрашивает у объектов
сцены `ISoundControl` — он реализован клиентом, не этими DLL. Каталоги звуков и озвучки перечислены в
`freelancer.ini` (`[Startup] sounds = audio\*.ini`, `voices = audio\voices_*.ini`) и лежат в
`DATA\AUDIO\`.

## Известные ограничения

- Хранилище архетипов в SoundManager — линейный массив вместо `std::map` retail (интерфейсно не видно).
- Слушатель не двигается: клиент вызывает `MixerTick(nullptr)`, а `ISoundListenerPose` (поза камеры,
  скорость, коэффициенты) в порте пока никто не реализует (`menu_sound_arch.cpp`, `soundmanager.h`).
- Если менеджер не стартовал, Streamer создаёт собственный DirectSound (как в retail), т. е. два объекта
  DirectSound на одно окно.
- Остальное по функциям — в decomp-tracker и разделе SoundManager / SoundStreamer
  [deviations.md](deviations.md).
