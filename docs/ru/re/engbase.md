# EngBase.dll

`EngBase.dll` — ядро анимации и сцены: архетипы и деревья экземпляров, их преобразования, каналы анимации, точки крепления (hardpoints). Это основа `IEngine`, `IAnimation2`, `IChannel`, `IHardpoint`, `IEngineComponent`. Документ описывает архитектуру и общие форматы; статусы, адреса и доказательства по функциям и слотам — в decomp-tracker (листинг `EXE/Disasm/engbase.dll.txt`). Порт — `src/EngBase/` (`EngBase.cpp`, `EngBaseMixer.cpp`), публичные интерфейсы — `src/include/engine/engbase.h`.

`DALib::Startup` создаёт `Engine`, затем `LoadEngineComponents` читает секцию `[Engine]` через встроенный INI-парсер dacom (участие `luaprofile` не требуется), агрегирует перечисленные компоненты и собирает список `IEngineComponent`. Обратные вызовы EngBase в другие DLL идут через `I3DMathEngine` (x86math) и `IFileSystem` (ReadFile).

## Компоненты и их регистрация

`DllMain` регистрирует четыре фабрики (после `RegisterComponent` фабрика `Release`-ится): `Animation` (приоритет `0x40000000`), `ChannelArchetype` (`0x80000000`), `Hardpoint` (`0x80000000`), `Engine` (`0x40000000`). Фабрика — объект `0xC` байт `{vtable, refcount, имя}`.

| Компонент | AGGDESC | Объект |
|-----------|---------|--------|
| Animation | `0x18` | `calloc(0x60)`: фасеты `IAnimation2` (+0), `IAggregateComponent` (+4), `IChannel` (+8), `IRootData` (+0xC); controlling unknown +0x50, inner +0x54 |
| Hardpoint | `0x18` | `calloc(0x44)`: `IHardpoint` (+0), `IEngineComponent` (+4), controlling +0x34, inner +0x38 |
| Engine | `0x08` | `calloc(0x64)`: `IEngine` (+0), refcount +0x60 |
| ChannelArchetype | `0x08` | `operator new(0x20)`, refcount +0x1C, без inner; не в `[Engine]`, создаётся при чтении узла `"Channel"` |

Расширенный дескриптор `0x18` для компонентов из `[Engine]`: `+4` имя фабрики (токен строки), `+8` controlling unknown (= сам Engine), `+0xC` `void**` для inner (фабрика пишет туда inner), `+0x10` значение после `=` (может быть NULL), `+0x14` `ISystemContainer`. Фабрика Animation/Hardpoint: если `desc+8 == 0`, controlling unknown — собственный inner, иначе в `*desc+0xC` пишется inner. Хук после конструктора всегда возвращает 0.

Inner `QueryInterface` Animation: `IAnimation2` и `IAnimation` → +0, `IChannel` → +8, `IRootData` → +0xC, `IAggregateComponent` → +4; Hardpoint: `IHardpoint` → +0, `IEngineComponent` → +4, `IAggregateComponent` → +4 (тот же фасет). Фасеты, смещённые от начала объекта, реализуются thunk'ами `this -= N`.

## `load_engine_components` (`IEngine` слот 3)

1. Если уже инициализировано — выход; нулевой `ISystemContainer` — NOTICE через `FDUMP`.
2. QI у `ICOManager` на `"1.11_IProfileParser"`; провал → ERROR и `-1` (тогда Hardpoint/Animation в `DALib` будут нулевыми).
3. `OpenSection("Engine", 3)`, затем `ReadLine` по индексам (буфер 0x100). Пустая секция — успех. `CloseSection` retail не вызывает.
4. Каждая строка: токен имени (+ значение после `=`), AGGDESC `0x18`, `CreateInstance`, NOTICE «aggregation of '%s' [%s] returned %s». Inner (`*Outer`) добавляется в список **всех** компонентов (+0x4C), это тот список, который обходит `QueryInterface` Engine; в него кладётся именно inner, иначе QI рекурсивно вернётся в Engine.
5. Проход 2: QI `IAggregateComponent` на каждом узле и `Initialize`; при сбое — WARNING и узел выбрасывается из списка (имя и указатель обнуляются, `Release` retail не делает).
6. Проход 3: для оставшихся QI `IEngineComponent` (немедленный `Release`); успех — узел в отдельном списке +0x58 (указатель не owning). Hardpoint реализует `IEngineComponent`, Animation нет.

Состав `[Engine]` — в `bin/DACOM.INI` (Animation, Hardpoint, Deformable, RenderManager, FLAppearance, VMeshWire). Узел списка `0x3C`: Next, Prev, имя (до +0x38), указатель на компонент (+0x38).

`set_search_path`: создаёт `SearchPath` (`{size 8, "SearchPath"}`) и вызывает `ISearchPath::SetPath(path)`; retail передаёт перед вызовом ещё один стековый аргумент, не принадлежащий этому фрейму, порт передаёт два (`this, path`), как требует сторона ReadFile.

## Архетипы и иерархия экземпляров

- Архетип идентифицируется CRC имени (`FArchetypeName{Text, Crc}`; `Crc == 0` → lookup возвращает `-1`). Карта архетипов — `std::map<unsigned long, FArchetypeHandle*>` по CRC; handle `0x38`: имя +4, CRC +0x10, `RefCount` +0x14, дети +0x1C. Пулы свободных блоков (`{size, head, limit 0x40, count}`) для handle (0x38), экземпляров (0xEC) и собственных трансформов (0x48) повторяют retail.
- `create_compound_archetype`: при `Crc == 0` имя генерируется `anonymous-%d` до уникального CRC. Загрузка составного архетипа (слот 12) создаёт вложенную `FileSystem` с `DataPath = "Cmpnd"` и читает `Root`/`Part*`/`Cons` через вложенные UTF-файловые системы ReadFile; провал `Cmpnd` — загрузка узла как листа `.3db` через `IEngineComponent`.
- Дерево экземпляров: `IEngine::create_joint`/`destroy_joint` связывают родителя и ребёнка, ведут карту **корней** иерархий (`std::map<long, FEngineInstance*>`, ключ — handle), кэш поиска и счётчики попаданий. Одиночный экземпляр в карту не вставляется. Вычисление мира (`FUN_0661f250`): локальная матрица сустава умножается на мировую родителя через `I3DMathEngine`; запись мира только в `transform+4`.
- Типы суставов — ровно семь, **FK** (Fixed, Prismatic, Revolute, Cylindrical, Spherical, Translation, Loose); **IK-решателя в EngBase нет** (в листинге нет ни строк, ни кода). Угловые параметры `0.5` в jump-таблице — полуугол, не перевод градусов.
- `IEngine::EnumerateArchetypeChildren` вызывает колбэк как cdecl `(parentHandle, childHandle, 0, context)`; от этого порядка зависит `freelancer.exe` (`ArchLod`). Не менять без сверки с листингом.
- Строка `".."` в `DAT_0662a03c` (листинг показывает только первый байт) — аргумент `OpenNode`, которым retail возвращается из каталога иерархии `Hardpoints/<тип>` и при обходе карт анимации; `"."` не поднимает на уровень выше (см. [readfile.md](readfile.md)).

## Hardpoint

Хранит `std::map<int handle, std::list<запись>>` с кэшем итератора и счётчиками (+0x08 / +0x0C). Запись: Crc, `_strdup`+`_strlwr`-имя, блок `0x48` (позиция, ориентация, ось, min/max). Тип точки читается из каталога `Hardpoints/Fixed|Revolute|Prismatic`; перечисление использует `"*"` снаружи и `"*.*"` внутри без фильтра `.`/`..`. Соединение (`IHardpoint` слот 6) вычисляет трансформ через `I3DMathEngine` (ориентация B транспонируется) и создаёт сустав `IEngine::create_joint`; деструктор через `FDUMP` сообщает о «dangling hardpoints».

## ChannelArchetype (формат)

| Смещение | Поле |
|----------|------|
| +0x04 | число кадров |
| +0x08 | частота (`> 0` — равномерная, `< 0` — метка времени в начале кадра) |
| +0x0C | флаги: 1 скаляр, 2 позиция, 4 кватернион, 8 только скаляр |
| +0x10 | блок кадров |
| +0x14 | шаг (из флагов; +4 для меток времени) |
| +0x18 | длительность |

Кадры с флагами `& 0x1F0` распаковываются (0x10 нулевая позиция, 0x20 единичный кватернион, 0x40 три short с `W = √(1−n)`, 0x80 ось-угол, 0x100 четыре short, масштаб `½(3−‖q‖²)`) в общий realloc-пул `DAT_0662b104` (старый блок не освобождается, как в retail); несжатые — `malloc`. Кватернионы везде **WXYZ**. Узел `"Event map"` при загрузке скрипта пропускается (событий в EngBase нет); `"Root height"` читается по хэндлу поиска.

## Анимация и микшер

- Воспроизведение: `std::map<long, FAnimPlayback>` (`Animation+0x14`), идентификатор воспроизведения начинается с `0xED` от счётчика `Animation+0x10`. Методы `IAnimation2`, принимающие «скрипт» (длительность, `DestroyScript`, `FilterObjects`), работают по идентификатору воспроизведения. Имена анимаций — список, отсортированный по CRC.
- Микшер: `Animation+0x3C` — карта по ключу привязки, внутри списки узлов с типами leaf (0x18), blend (0x48) и overlay (0x34); узлы тикаются через собственную vtable узла. Шаг времени — `Dt·Weight`; масштаб семейства type-6 — `Scale·Field_0x30`, вес в него не входит; индекс таблицы смягчения (256 значений `(1−cos(i·π/255))·0.5`) — `ftol(floor(InvDuration·Time·255))` по времени blend-узла. Подробности численных путей — в карточках трекера.
- Переход через конец клипа (wrap): учитывается `LoopFlags & 2` и положение времени вне `[0, Duration]`; шаг = `(EndA − Now) + (L·F − EndB)`.

## Отклонения и принятые пробелы

- Контейнеры — современные `std::map`/`std::list`/`std::set`, не деревья MSVC6; имя архетипа — `_strdup`, retail использует `basic_string`.
- Часть тел (микшерные пути `0x0661b120…0x0661d8d0`, кэш охватывающего объёма `0x0661ed90`, размер Sphere-узла) в трекере отмечена как неполностью сверенная.
- Слот `IEngine` 34 (охватывающий объём): без `I3DMathEngine` векторы копируются без трансформации.
- Участки, которые в проекте обслуживают другие модули: вложенный UTF `CreateInstance` — ReadFile; мешевая часть — `IEngineComponent` вложенной FS; пост-спавн меню Thorn — клиент.
- Для IEngine слоты 41/43 и `IEngineComponent` 7–12 — retail no-op, а не дыры порта; слот 57 всегда возвращает `-1`.
