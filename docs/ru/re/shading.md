# Shading: текстуры, материалы, FLAppearance

Страница про две DLL, которые вместе дают клиенту материалы: `Shading.dll` (библиотеки текстур и
материалов, батчер, анимация материалов, базовые типы материалов) и `flmaterials.dll` (игровые
материалы Freelancer и компонент `FLAppearance`). Нужна, чтобы понять, откуда берётся конкретный
материал, какой контракт у `IMaterial` и где лежат данные. Общую схему стека и пайплайн смотрите в
[render.md](render.md). Статусы функций, слоты и адреса — в decomp-tracker; открытые расхождения с retail —
в [deviations.md](deviations.md).

Исходники: `src/Shading/`, `src/flmaterials/`; публичные интерфейсы — `src/include/render/IMaterial.h`,
`IBatchedMaterial.h`, `IFLAppearance.h`, `shading.h`, `factory_names.h`. `Deformable2.dll` реализует
собственный `EyeMaterial` того же интерфейса (`src/Deformable2/EyeMaterial.*`).

## Модель: фиксированный конвейер, не шейдеры

В `shading.dll` и `flmaterials.dll` нет ни HLSL, ни vertex/pixel shader. Материал при bind выставляет
состояния Direct3D 8 через пайплайн (`set_material`, `set_render_state`, `set_texture_stage_state`,
`set_texture_stage_texture`, `set_texture_stage_transform`) и при submit вызывает draw. Роль «техники»
играет **тип материала** — имя зарегистрированной DACOM-фабрики (`DcDt`, `Nebula`, `NomadMaterial` и т. д.).
Аббревиатуры в именах (`Dc`, `Dt`, `Ec`, `Et`, `Oc`, `Ot`, `Bt`) retail нигде не расшифровывает; по именам
свойств `Dc`/`Dt` — diffuse color/texture, `Ec`/`Et` — emissive, `Bt` — detail texture, `Oc`/`Ot`, по-видимому,
opacity. Суффикс `Two` — отдельный зарегистрированный тип (вероятно, двусторонний вариант).

## Откуда берётся материал

1. Меш или комната ссылается на материал по имени (CRC имени — ключ в `IMaterialLibrary`).
2. `IMaterialLibrary` загружает узел UTF `Material library` (`Material count`, затем дочерние узлы-каталоги;
   у каждого поле `Type`) через `IFileSystem` и создаёт объект через `CreateInstance` по имени типа.
3. Тип можно переопределить секцией `[MaterialMap]` в `DACOM.INI`: строки `Тип = Тип` (переименование) и
   `name = <regex> = Тип` (подстановка по имени материала). Так, например, `^nomad.*$` →
   `NomadMaterialNoBendy`, `^ui_.*` → `HUDIconMaterial`, `^sea_anim.*$` → `PlanetWaterMaterial`.
   Фабрики Shading и flmaterials регистрируются в один общий реестр DACOM, поэтому карта не различает,
   какой DLL принадлежит тип.
4. `[BatchedMaterials]` (`Type = .*OcOt`, `Name = ...`) — правила `MaterialBatcher`: подходящие материалы
   отправляются в батчер, а не рисуются сразу.
5. `FLAppearance.Initialize` создаёт через библиотеку *override-материалы* — `ColorizeMaterial`,
   `WireMaterial`, `WireMaterial2`, `CloakMaterial`, `EnvMapMaterial`, `BackdropMaterial`,
   `StarsphereMaterial`, `ShadowMaterial`, `VisibleCounterMaterial` — и регистрирует их по CRC имени.
   Движок обращается к ним по индексу (`IFLAppearance::GetOverrideMaterial`).

## Фабрики

**Shading** (`RegisterComponent`, приоритет `0x80000000`): сервисы `TextureLibrary`, `MaterialLibrary`,
`MaterialBatcher`, `MaterialAnimation`, `BatchedMaterial`; материалы `DcDt`, `DcDtEc`, `DcDtOcOt`,
`DcDtEcOcOt`, их `…Two`-варианты, `DcDtEt`, `EcEt`, `EcEtTwo`, `AtmosphereMaterial`, `Nebula`,
`NebulaTwo`, `NullMaterial`, `LightMap`, `DetailMapMaterial`, `DetailMap2Dm1Msk2PassMaterial`,
`IllumDetailMapMaterial`, `Masked2DetailMapMaterial`, `BtDetailMapMaterial`, `BtDetailMapTwoMaterial`.
Имена `DcDtEtTwo`, `EcEtOcOt`, `DcDtEcEt` фабрик не имеют, это только источники ремапа в `[MaterialMap]`.

**flmaterials**: `NomadMaterial`, `NomadMaterialNoBendy`, `HUDIconMaterial`, `HUDAnimMaterial`,
`ColorizeMaterial`, `CloakMaterial`, `WireMaterial`, `WireMaterial2`, `ExclusionZoneMaterial`,
`GlassMaterial`, `HighGlassMaterial`, `GFGlassMaterial`, `EnvMapMaterial`, `BackdropMaterial`,
`StarsphereMaterial`, `ShadowMaterial`, `EngineGlowMaterial`, `VisibleCounterMaterial`,
`PlanetWaterMaterial` — 19 типов — и компонент `FLAppearance` (агрегат движка, `[Engine]`).

Несколько имён — один класс: Backdrop и Starsphere; Glass, HighGlass и GFGlass; Nomad и NomadNoBendy
(различаются только флагом, который выставляет слот инициализации по имени из дескриптора). Все материальные
фабрики требуют `AGGDESC` размера `0x14` с именем типа по `+4`; `FLAppearance` — `0x18`.

## Контракт `IMaterial`

Интерфейс объявлен один раз (`render/IMaterial.h`), 32 слота; его реализуют Shading, flmaterials и
Deformable2, а вызывают RendComp, Alchemy и клиент. Порядок слотов фиксирован retail-таблицами; **имена
слотов даны по поведению**, а не по оригиналу (retail их не публикует), и вместе с сигнатурами хранятся в
карточке интерфейса в трекере.

Основной жизненный цикл: `Initialize(outer)` (слот 3: QI `ITextureLibraryA`, `IRenderPipeline8B`, `IRPDraw`,
`IVertexBufferManager` у внешнего объекта) → `LoadFromFileSystem` (4, читает свойства из UTF) → в кадре
`Bind(8) → Submit(10) → Teardown(9)`. `ApplyVerify` (6) оборачивает bind и проверку состояния
пайплайна (`verify_state`). `Submit` принимает контекст, тип примитива, начало и число вершин, старт и
длину индексов и флаги (7 аргументов); флаг `0x100` отправляет рисование в `IRPDraw`, иначе — в слот
`draw_indexed_primitive_vb` пайплайна. Контекст — `FMaterialSubmitContext` (0x1C), структура объявлена в
`shading.h`.

**Соглашения о вызовах в одной таблице смешаны.** Слоты 3..19 (включая bind/submit/teardown 8, 9, 10) —
`__stdcall` с явным `this` (стиль DACOM, `DACOM_API`), а слоты 20..28 (имя класса, деструктор,
`InitializeFromDescriptor`, `ApplyInitialRenderStates`, `Setup`, проверка диапазона свойства, запись
значения, таблица параметров, адрес свойства) в retail — `__thiscall` (`this` в ECX), и в заголовке они
объявлены именно так; 29..31 — внутренний QI/AddRef/Release агрегата. Это определено по вызывающей стороне
(`RET n` один не различает соглашения); слот, чьё соглашение не подтверждено вызовом, нельзя
угадывать. `ResolveTextures(float Delta)` принимает `float` по значению.

**Система параметров.** Большинство материалов описывают свои свойства таблицей записей по `0x4C` байт
(`FShadingPropertyRecord`: четыре служебных dword и три типизированных значения по `0x14` байт —
текущее, по умолчанию, сохранённое). Тег типа значения: 1/8/9/10 — 32-битное слово; 2 — float; 3 и 4 —
вектор из трёх / четырёх float; 5 и 7 — строка с владением (куча процесса); 6 — `IDAComponent*` с
`AddRef`/`Release`. Слоты 16/17/18/19 — число параметров, дескриптор, чтение и запись значения по индексу;
27 отдаёт таблицу и число записей, 25 — проверку диапазона, 26 — запись значения в запись таблицы,
28 — адрес поля. Загрузчик параметров читает типизированные
листья UTF (`Dc`, `Ac`, `Dt_name`, `Dt_flags`, `Opacity`, `Texture Name`, `NomadTexture Name` …) через
`IFileSystem::OpenByDesc` / `Read` / `CloseFile`; сохранение (`SaveProperties`) пишет их обратно.

**Общий префикс bind** в семействе DcDt, LightMap, Nebula, Atmosphere: `set_material(this+0x20)`,
`COLORVERTEX` при наличии цвета вершин, привязка текстуры стадии 0, `TEXCOORDINDEX` и адресация U/V из
блока текстуры. Дальше каждое семейство задаёт свои RS/TSS (режим смешивания, `LIGHTING`, вторая стадия
для detail-карт). Конкретные таблицы значений — в карточках трекера.

## TextureLibrary

`TextureLibrary` (`ITextureLibraryA` + `ITextureLibrary2`) хранит именованные текстуры и группы.
Загрузка из UTF узла `Texture library` (`.txm`): для каждой дочерней записи читается либо `MIPS` (DDS,
DXT1–5 и несжатые 16/24/32 бит), либо `MIP0..MIP9` (TGA типов 2/3/10/11, 8–32 бит), либо `CUBE`
(cubemap, через `IRPTexture` загрузчик D3DX). Уровни TGA заливаются в исходном пиксельном формате (с палитрой для 8-бит) через
`create_texture` / `set_texture_level_data`. Записи одной загрузки образуют **группу** с возрастающим
номером; запись может быть анимированной (несколько кадров, FPS, прямоугольники кадров — «Texture count»,
«Frame count», «Frame rects»), в этом случае библиотека ведёт состояние воспроизведения отдельно от
каноничной записи.

Формат пикселя выбирается таблицей, которую заполняют 8 строк по умолчанию и секция
`TextureFormatMaps` из INI (пары FourCC/маска → внутренний id формата; `DXT1–5`, `DAOP`/`DAOT`/`DAAA`/
`DAAL`/`DAA1`/`DAA4`/`DAA8`). Ключи `[TextureLibrary] TEXTURE_LOD_LOAD` и `TEXTURE_LOD_LOAD_MIN`
ограничивают загружаемые уровни. Поиск идёт по CRC имени; `FindTextureByCrc` возвращает запись со
ссылкой, `GetTextureFrame` — GPU-хэндл и UV кадра.

## MaterialBatcher и MaterialAnimation

`MaterialBatcher` (`IMaterialBatcher2`) читает `BufferSize` и `Bin1SortOrder` и правила `[BatchedMaterials]`
(regex по `Type` или `name`), копит подходящие материалы в двух «корзинах» и при сбросе сортирует
(`IMB_SO_PRIM_BACK_TO_FRONT` и т. п.). Внутри него живёт `IMaterialLibrary`. `BatchedMaterial` — обёртка,
перенаправляющая слоты `IMaterial` во внутренний материал.

`MaterialAnimation` (`IMatAnimLibrary`) загружает данные анимации UV из узлов `MaterialAnim`
(`MACount`, `MAFlags`, `MADeltas`, `MAKeys`) и создаёт экземпляры, обновляемые тиком; материал с
активной анимацией включает преобразование координат текстуры (`TEXTURETRANSFORMFLAGS`).

## FLAppearance (`IFLAppearance`)

Агрегат движка: хранит карту id→значение (EnvMap кладёт указатель, Nomad — float), направленный свет
сцены, массив override-материалов и методы отрисовки сервисных объектов: фон (`DrawBackdrop`), небо
(`DrawStarsphere`), тени (`DrawFadingShadows`, `DrawFixedShadows`) и счётчик видимой геометрии
(`CountVisibleGeometry`). Текстура тени берётся по имени `efxshadow` из `DATA/FX/efx.txm`.

## Известные ограничения

- Per-function статус (verified / simplified / partial) — в decomp-tracker. Из того, что видно на уровне
  архитектуры: схемы свойств перенесены не для всех семейств материалов (см. карточки слотов 17/18/25/26/27),
  а сами значения RS/TSS по семействам сверены по листингу, но на реальном рендере не сравнивались.
- Пиксельное сравнение с retail и запуск клиента делает владелец проекта.
- Подробности и расхождения — разделы Shading и flmaterials в [deviations.md](deviations.md).
