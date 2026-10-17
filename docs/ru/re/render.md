# Render stack: RP8, RendComp, ximage

Эта страница описывает, как устроен графический стек клиента Freelancer в нашем порте: Direct3D 8
пайплайн (`RP8.dll`), слой мешей и рендерера (`RendComp.dll`), тайлер больших картинок (`ximage.dll`)
и то, как они связаны с материалами. Нужна тому, кто собирается менять отрисовку или искать, какая
DLL отвечает за конкретный этап кадра. Материалы, текстуры и их фабрики описаны в
[shading.md](shading.md); эффекты и частицы — в [alchemy.md](alchemy.md).

Источник поведения — листинги retail DLL (`EXE/Disasm/*.dll.txt`); статусы функций, номера слотов и
адреса — в decomp-tracker, а заголовки слотов лежат в `src/include/render/`. Долг порта относительно
retail ведёт [deviations.md](deviations.md). Ветка D3D9 / объёмных туманностей (RP9) приостановлена и здесь
не описывается: всё ниже относится к D3D8-пайплайну, который реально собирается из `src/`.

## Состав

| DLL | Исходники | Что делает |
|-----|-----------|-----------|
| `RP8.dll` | `src/RP8/` | Обёртка над Direct3D 8: устройство, текстуры, VB/IB, состояния, огни, draw. Фабрика `DirectX8` |
| `Shading.dll` | `src/Shading/` | Библиотеки текстур и материалов, батчер, анимация материалов — см. [shading.md](shading.md) |
| `RendComp.dll` | `src/RendComp/` | `RenderManager` (интерфейс `IRenderer`), плагины мешей `VMesh` / `TriMesh` / `SphereMesh`, `VMeshLibrary`, `VMeshWire` |
| `ximage.dll` | `src/ximage/` | `BigImage`: режет растр на текстурные тайлы и отдаёт их клиентскому callback |
| `flmaterials.dll` | `src/flmaterials/` | Игровые материалы Freelancer и `FLAppearance` — см. [shading.md](shading.md) |

Все пять — DACOM-плагины без именованных экспортов: `DllMain` только регистрирует фабрики
(`RegisterComponent`), а объекты создаются позже через `CreateInstance` по имени. Друг на друга они
не линкуются; связь идёт через DACOM `QueryInterface` по строковым IID (`Iids::*` из
`src/include/dacom_iids.h`). Публичные интерфейсы объявлены один раз в `src/include/render/`
(`IRenderPipeline.h`, `shading.h`, `rendcomp.h`, `IMaterial.h`, `IFLAppearance.h`, `mesh_iface.h`) и
в `src/include/ximage.h`.

## Как стек собирается при старте

`bin/DACOM.INI` грузит `[Libraries]` в порядке: `ReadFile`, `x86math`, `EngBase`, `system`, `RP8`,
`SoundStreamer`, `SoundManager`, `Deformable2`, `Thorn`, `Shading`, `RendComp`, `alchemy`, `ximage`,
`flmaterials` (все из `bin\`). Затем `DALib::Startup` создаёт два агрегата, и они читают свои секции INI:

```
SystemContainer  ([System])
  VMeshLibrary                      RendComp   IVMeshLibrary
  DirectX8                          RP8        IRenderPipeline8B, IVertexBufferManager, ...
  TextureLibrary                    Shading    ITextureLibrary2
  MaterialAnimation                 Shading    IMatAnimLibrary
  MaterialBatcher                   Shading    IMaterialBatcher2 + вложенный IMaterialLibrary
  Alchemy, FxRuntime                alchemy

Engine           ([Engine])
  RenderManager   ([RenderManager] -> VMesh, TriMesh, SphereMesh)      RendComp   IRenderer2
  FLAppearance                                                         flmaterials
  VMeshWire                                                            RendComp
  Animation, Hardpoint, Deformable                                     EngBase / Deformable2

По требованию (не в [System]/[Engine]):
  BigImage         ximage       создаёт Intro / RichDisplay клиента
  <тип материала>  Shading / flmaterials   создаёт MaterialLibrary по [MaterialMap]
```

На клиенте отдельной записи `MaterialLibrary` в `[System]` нет: библиотека материалов живёт внутри
`MaterialBatcher` (QI `IMaterialLibrary` возвращает вложенный объект). Серверный INI перечисляет
`MaterialLibrary` отдельно.

Кто у кого запрашивает интерфейсы в `Initialize`:

| Компонент | Берёт |
|-----------|-------|
| `DALib` | `IRenderPipeline8B` (затем у пайплайна `IRPDraw`, `IRPIndexBuffer`, `IRPVertexBuffer`) и `IVertexBufferManager` с контейнера |
| `RenderManager` | у родителя `IRenderPipeline8B`, `IVertexBufferManager`, `IMaterialLibrary`; у пайплайна `IRPVertexBuffer`, `IRPIndexBuffer`, `IRPDraw`; у себя `IEngine`. Промах по `IVertexBufferManager`, VB/IB/Draw или `IEngine` — `Initialize` возвращает `-1`; без `IMaterialLibrary` только пишет `FDUMP` |
| Материалы (`IMaterial::Initialize`) | `ITextureLibraryA`, `IRenderPipeline8B`, `IRPDraw`, `IVertexBufferManager` |
| `FLAppearance` | `IMaterialLibrary`, `IRenderPipeline8B`, `IVertexBufferManager`, `ITextureLibraryA`, `IRenderer` |
| `BigImage` | у пайплайна из дескриптора: `query_device_ability`, `ITextureLibraryA` (запрос уходит в SystemContainer и попадает в Shading) |

## RP8: пайплайн Direct3D 8

Один объект `DirectX8` отдаёт сразу восемь интерфейсов (каждый — свой подобъект со своим vtable):
`IRenderPipeline8B`, `IVertexBufferManager`, `IRPDraw`, `IRPIndexBuffer`, `IRPVertexBuffer`,
`IGammaControl`, `IRPTexture`, `IAggregateComponent`. Основной интерфейс — 77 слотов; имена слотов,
сигнатуры и retail-адреса — в `src/include/render/IRenderPipeline.h`, `slots.h` и в карточках
decomp-tracker. Имя метода известно только там, где в теле retail-функции есть `FDUMP`-строка с ним.

Жизненный цикл устройства:

- `Startup` (слот 3, вызывает `engine_init`) создаёт `IDirect3D8`, перечисляет адаптеры, читает секцию
  `[RenderPipeline]` и MGSDB, но **не создаёт устройство**.
- `CreateBuffers` (слот 12) — единственное место, где создаётся/сбрасывается `IDirect3DDevice8`.
  Его зовёт клиентский код выбора режима (`RenderMgrCreateBuffers`, retail `FUN_004249c0`), а не
  DALib. Готовность устройства — бит в состоянии пайплайна; методы, требующие устройство, проверяют
  его по трём разным условиям, каждое со своим текстом `FDUMP` (outside of startup / create_buffers /
  device lifetime).
- Кадр: `begin_scene` (57) / `end_scene` (58) вызывает клиент; `swap_buffers` делает `Present`.
- Слот 4 завершает пайплайн (DALib `Shutdown`), слот 14 освобождает GPU-ресурсы, не трогая устройство.

Устройство конфигурируется ключами `[RenderPipeline]` (`DeviceId`, `DeviceType`, флаги vertex
processing, `FPU_PRESERVE`, `MULTITHREADED`, ключи состояний по умолчанию) и файлом MGSDB
(`FLConfigDatabase.txt`): это список «плохих» драйверов/карт, по которым пайплайн выставляет флаги
обходов (`FL_BAD_*`, `NoAlphaTest`, `DefaultBltNotFlip` и т. п.) и массив возможностей устройства.
Возможности читаются через `query_device_ability(index)`: ximage использует индексы 5/6/7
(`TEXTURE_SQUARE_ONLY`, `TEXTURE_MAX_WIDTH`, `TEXTURE_MAX_HEIGHT`), загрузка cubemap проверяет
`TEXTURE_CUBEMAPS`.

Что стоит знать, прежде чем править код RP8:

- **Текстуры.** Хэндл текстуры — 8-байтная обёртка `{IDirect3DTexture8* | cube, kind}`. Загрузка
  файлов (`IRPTexture`) идёт через статически подлинкованный D3DX 8.1 (`d3dx8.lib` из `dx8\lib`);
  retail тоже линковал D3DX статически (импорта `d3dx8.dll` в retail нет). Байты файла читаются через
  переданный `IFileSystem` (map/view), а не копируются.
- **Состояния.** Запросы render/texture-stage states откладываются и сливаются в `FlushDrawState`
  перед каждым draw (как в retail); `get_pipeline_state` читает отдельное зеркало, а не очередь.
  Менеджер огней (`StateCache`) повторяет retail: записи огней, пул из 64 индексов, отложенный
  `LightEnable`.
- **Вершинные буферы.** `create_vb` с флагом пула возвращает буферы по FVF из общего пула
  `IVertexBufferManager` (кольцо `DISCARD` / `NOOVERWRITE`); индексный буфер для
  `draw_indexed_primitive_vb` — внутренний scratch IB, не тот, что создаёт `create_ib`.
- **Draw.** Слоты 69–72 (`draw_primitive`, `draw_indexed_primitive`, `draw_*_vb`) и `IRPDraw` слот 3
  перед каждым draw сливают отложенное состояние (`FlushDrawState`: потоки, матрицы, огни). Матрицы
  (`set_world`, `set_view`, `set_modelview`, `set_projection`) принимают 12/16 float; `set_view` /
  `set_modelview` применяют ZFlip.

## RendComp: рендерер и меши

`RenderManager` реализует `IRenderer` / `IRenderer2` (23 слота, один vtable по смещению `+4` объекта)
и владеет списком плагинов `IRenderComponent`, прочитанным из `[RenderManager]` в порядке INI
(**VMesh, TriMesh, SphereMesh**). Для нового архетипа движок спрашивает плагины по очереди (слот 3
каждого — «claim»); первый, кто не вернул `-1`, получает архетип и создаёт экземпляры. Поддержаны также
LOD-таблицы `Switch2` / `MultiLevel` / `Level<N>` в UTF.

Рисование меша — одна цепочка: `IRenderer` слот 7 → `IRenderComponent` слот 17 (bind) и 16 (submit) →
`IMaterial` слоты **8 (bind) → 10 (submit primitives) → 9 (teardown)**. Перед submit плагин ставит
матрицы: если состояние пайплайна 7 (`VIEWSPACE_LIGHTS`) равно нулю — `set_world(world)`, иначе
считает `view × world` через `3DMathEngine` (`x86math.dll`) и вызывает `set_modelview`. Материал
находится по CRC имени в `IMaterialLibrary` (`FindMaterialByCrc`, без создания); при claim плагин сам
подгружает `Texture library` / `Material library` из того же UTF.

Кто рисует GPU-примитивы:

- `VMesh` и `SphereMesh` сами примитивы не рисуют: submit-слот материала (10) идёт в слот 72 пайплайна
  (`draw_indexed_primitive_vb`) либо, если у submit выставлен флаг `0x100` (`VMesh` добавляет `0x10C`),
  в `IRPDraw` слот 3.
- `IRPDraw` напрямую зовут только `VMeshWire` (каркас/коллизия, примитив `2`) и клиентский callback
  `BigImage` (экранные тайлы).
- Контекст submit — структура `FMaterialSubmitContext` (0x1C байт); её поля (индексы, листинг VB,
  8-байтовый scratch с базой индексов) объявлены в `shading.h` / `rendcomp.h` и заполняются плагинами по
  retail-раскладке.

### Форматы мешей, которые читает RendComp

Все — узлы UTF (`.cmp`, `.3db`, `.vms`), читаются через `IFileSystem` (ReadFile.dll).

**VMesh.** Узел `VMeshPart` указывает на запись `VMeshRef` (0x3C байта: стартовая вершина/индекс,
число вершин, диапазон facegroup, bounding-данные); сами вершины и индексы лежат в `VMeshData` из
`VMeshLibrary` (кэш по CRC32 имени). Заголовок `VMeshData`: `+0x00` флаги (бит 0 = «новый формат»,
иначе FDUMP «mesh in old format»), `+0x08` число facegroup, `+0x0A` индексов, `+0x0C` битовая маска FVF,
`+0x0E` вершин; далее facegroup по 0xC байт (CRC материала, диапазон вершин, длина индексов), индексы
`uint16`, вершины. Раскладка FVF-маски и таблицы смещений — `src/include/render/rendcomp.h` и
`src/RendComp/VMeshLibrary.cpp`.

**TriMesh.** Узел `Mesh` обязателен. Внутри `Geometry`: пулы `Points` (3 float), `Vertex_normals`,
`UV0`/`UV1` (2 float), `Color0` (ARGB) и парные `*_indices` — массивы `uint32` «углов» с общим числом
(угол = индекс в каждый пул). `Face_groups`: `Count`, затем `Group<N>`, в каждом `Material_name`
(CRC от имени в нижнем регистре), `Tristrip_indices` (проверяется первым) или `Face_indices`
(`uint16`, номера углов), опционально `Edge_indices` / `Edge_angles`. Анимация UV — `UV0_anim_lookup`,
`UV0_frame_count`, `UV0_anim`, `UV0_fps`, `UV0_interpolate`. `[TriMesh] tristrips = 0` заставляет
конвертировать полосы в списки треугольников. При реверсе в поставляемых `DATA/` узлов `Mesh`/`Geometry` не нашлось, поэтому
эта ветка сверена только по листингу, не на реальном контенте. Вершины рисуются из заблокированной памяти
`IVertexBufferManager` (`CopyVertexData` каждый кадр), собственных VB/IB у TriMesh нет.

**SphereMesh.** Узел `Sphere` с `Radius`, `Sides` (число материалов `M<N>`), опциональные `M0..`; сетка
строится в рантайме (куб→сфера, шесть граней, уровень детализации ограничен). Если `Sides > 6`, вторая
петля рисует атмосферу материалом `AtmosphereMaterial`.

## ximage: BigImage

Модуль не декодирует файлы (TGA/DDS читает Shading, декодированный растр приходит уже готовым). Он
принимает растр (сырой буфер либо `IBigImageSource`), режет его на тайлы по лимитам устройства
(`query_device_ability` 6/7; без `TEXTURE_SQUARE_ONLY` хвостовые тайлы округляются вверх до степени
двойки), создаёт по текстуре на тайл через пайплайн (`create_texture`, `set_texture_level_data`) и
хранит UV-квады (8 float на тайл). `Draw` считает UV для пары прямоугольников и для каждого тайла
вызывает клиентский cdecl-callback (`FBigImageDrawTile`: хэндл, packed ARGB, 4 экранные координаты со
сдвигом `-0.5`, 8 UV). Сам рисует клиентский 2D-батчер, а не ximage.

Дескриптор создания — `FBigImageDesc` (0x2C байта, **не** общий 0x14-байтовый `AGGDESC`), см.
`src/include/ximage.h`. `IBigImage2`: 12 слотов, `IBigImage` — тот же объект. Потребители в клиенте:
`Intro` (логотип) и `RichDisplay`.

## Известные ограничения

- Итог по каждой функции (verified / simplified / partial) — в decomp-tracker; открытые расхождения с
  retail — в [deviations.md](deviations.md) (разделы RP8, RendComp, Shading).
- Пиксельного сравнения с retail в репозитории нет: эталонные GPU-захваты меню (DXVK `.rdc`) лежат в gitignored `EXE\`, а
  сравнение кадров делает владелец.
- `jflp.dll` и `HudShift.dll` нельзя добавлять в `bin/DACOM.INI`: они патчат фиксированные адреса
  (в том числе внутри RendComp) и роняют не-retail exe.
- Объёмные туманности и D3D9 (ветка RP9) в этом дереве отсутствуют.
