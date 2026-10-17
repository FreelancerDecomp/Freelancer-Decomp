# system.dll — SystemContainer

`system.dll` — DACOM-агрегат, который владеет компонентами секции `[System]` из `DACOM.INI` (RP8, звук, текстуры, Alchemy и др.). `DALib::Startup` создаёт один экземпляр и получает от него интерфейсы движка через `QueryInterface`. Документ описывает контракт агрегации, раскладку объекта и порядок загрузки. Порт — `src/system/system.cpp`; адреса слотов и статусы функций — в decomp-tracker (листинг `EXE/Disasm/system.dll.txt`; Ghidra не декомпилировал `LoadSystemComponents`, тело читать в `.txt`).

В DLL нет именованных PE-экспортов: только `DllMain`, который регистрирует фабрику `"SystemContainer"` в `ICOManager` с приоритетом `0x40000000`. Фабрика отвечает на QI только по `"1.11_IComponentFactory"` (сравнение с учётом регистра, включая NUL).

## Дескриптор создания (`0x14` байт)

`CreateInstance` требует размер `0x14` и имя `"SystemContainer"`; неверное → `0xFFFFFFFD`. Поля: `+8` — внешний (controlling) `IUnknown`, `+0xC` — `IUnknown**` для внутреннего (пишется, если внешний задан), `+0x10` — зарезервировано (если равно `"SystemContainer"`, создание отказывает с `-1`). Нехватка памяти — `0xFFFFFFFC`. Это `FAGGDESC14` из `src/include/dacom.h` (плоский `AGGDESC` из двух полей — для обычных фабрик); у других компонентов (ILua) поле `+8` может значить другое, поэтому у `system.cpp` свой локальный `FSystemAggDesc`.

## Объект (`0x28` байт, `calloc`)

| Смещение | Поле |
|----------|------|
| `+0x00` | vptr `ISystemContainer` / `IAggregateComponent` / `ISystemComponent` (один указатель на все три) |
| `+0x04` | vptr `IDAConnectionPointContainer` |
| `+0x08` / `+0x0C` | голова / хвост списка дочерних компонентов |
| `+0x10` | внутренний неделегирующий `IUnknown` (vptr, счётчик, обратная ссылка) |
| `+0x1C` | controlling unknown (по умолчанию — внутренний) |
| `+0x20` | `bAggregated` (задан внешний unknown) |
| `+0x24` | `bLoaded` (после успешного `LoadSystemComponents`) |

QI по `ISystemContainer`, `IAggregateComponent`, `ISystemComponent` возвращает один и тот же указатель (смещение 0), по `IDAConnectionPointContainer` — смещение 4; неизвестный IID пробрасывается по дочерним внутренним unknown, иначе `0xFFFFFFFD` и `*out = 0`. Узел потомка (`0x14`): `Next`, внутренний `IUnknown*`, созданный объект, кешированные `ISystemComponent*` и `IAggregateComponent*` (без AddRef).

## Контракты интерфейсов

- `IAggregateComponent::Initialize` / `Shutdown` проходят по потомкам; при сбое `Initialize` потомок вынимается из списка. Строка `ISystemComponent` в потомках не встречается ни в одной retail-DLL (они реализуют только `IAggregateComponent`), поэтому `Shutdown` на пути загрузки FL — пустая операция; `DALib::Shutdown` освобождает Pipeline/Sound и вызывает `ICOManager::Shutdown`.
- `ISystemContainer`: `LoadSystemComponents`, `UnloadSystemComponents`, `AddComponent(AGGDESC*)`.
- `IDAConnectionPointContainer`: два метода с двумя указательными аргументами каждый — это **не** `Advise`/`Unadvise` и не OLE `EnumConnectionPoints`. Оба перебирают потомков и останавливаются на первом успехе; дефолты: поиск → `-1`, второй → `1`. Потомков с такой гранью нет, вызывающих в FL нет; имена `FindConnectionPoint` / `EnumConnectionPoints` взяты по арности, второй метод не доказан дальше арности.

## Загрузка компонентов

`DALib::Startup` создаёт контейнер (`CreateInstance("SystemContainer", 0x14)`), вызывает `Initialize` (потомков ещё нет), берёт `ISystemContainer` и вызывает `LoadSystemComponents`: QI у `ICOManager` на `"1.11_IProfileParser"` (встроенный парсер dacom, см. [dacom.md](dacom.md)), `OpenSection("System", 3)`, затем `ReadLine` по индексам; для каждой непустой строки — `CreateInstance(имя)` с контейнером в роли внешнего unknown и NOTICE через `FDUMP` («Loading '%s' [%s] returned %s»). Если контейнер не агрегирован, в конце вызывается его `Initialize` (инициализирует потомков). `CloseSection` retail не вызывает. При отказе QI/`OpenSection` функция возвращает OK, но потомков не создаёт и `bLoaded` не ставит — тогда `DALib` получит нулевые Pipeline/Sound; причина тогда не в system.dll.

Состав секции `[System]` — в `bin/DACOM.INI` (VMeshLibrary, DirectX8, SoundManager, SoundStreamer, TextureLibrary, MaterialAnimation, MaterialBatcher, Alchemy, FxRuntime).

## Что порт повторяет буквально

Порт воспроизводит поведение retail без собственных защит: нет проверок на null там, где их нет у retail (QI потомка, пересылка контейнера, фабрика), `Tail` остаётся устаревшим после сбоя `Initialize`, `Shutdown` возвращает то же значение EAX, что retail; `AddComponent` восстанавливает `Outer`/`InnerOut` и при успехе; `DllMain` `DETACH` — пустая операция. Известных расхождений нет. В `bin/DACOM.INI` библиотека указана абсолютным путём в `bin\`.
