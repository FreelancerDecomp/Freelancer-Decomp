# Аудит cross-DLL ABI

Сгенерировано `tools/abi.py build` — руками не править.
Правила и порядок действий: [../cross_dll_protocol.md](../cross_dll_protocol.md).

## 1. Объявлено приватно, но нужно другим модулям

Такой тип обязан переехать в `src/include/<module>.h` (образец — `src/include/dacom.h`): пока он приватный, агент из чужого модуля не может позвать метод и вбивает номер слота числом.

Пусто.

## 2. Один тип объявлен телом в нескольких файлах

Расхождение числа слотов = вызов через границу DLL уходит не в тот слот. Лечится так же: одно объявление в `src/include/`, остальные — `#include`.

| Тип | Слотов по версиям | Объявления |
|-----|-------------------|------------|
| `EqObj` | 4, 39 — **расходятся** | `src/include/common_archetype.h:598` (4)<br>`src/include/server_gameobject.h:638` (39) |
| `Mine` | 1, 4 — **расходятся** | `src/include/common_archetype.h:1502` (4)<br>`src/include/server_gameobject.h:1445` (1) |
| `Ship` | 1, 4 — **расходятся** | `src/include/common_archetype.h:755` (4)<br>`src/include/server_gameobject.h:1060` (1) |
| `Solar` | 2, 4 — **расходятся** | `src/include/common_archetype.h:715` (4)<br>`src/include/server_gameobject.h:1336` (2) |
| `IServerImpl` | 1 — совпадают | `src/include/rpc_iserver.h:49` (1)<br>`src/include/server_iserverimpl.h:44` (1) |

## Итог

- приватных, но общих типов: **0**
- типов с несколькими объявлениями: **5**, из них с расхождением слотов: **4**

