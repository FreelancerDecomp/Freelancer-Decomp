# luaprofile.dll

`luaprofile.dll` — DACOM-плагин со встроенным Lua 3.2, регистрирующий единственный компонент `"ILua"`. **В retail он на игровом пути не участвует**: его нет в `[Libraries]`, ни один retail-модуль не создаёт `"ILua"`, а загрузка INI идёт встроенным парсером dacom. Документ нужен, чтобы не путать этот модуль с `IProfileParser` и с Lua из Thorn. Порт — `src/luaprofile/` (отдельная private Lua VM в `src/luaprofile/lua32/`), публичный ABI — `src/include/luaprofile.h` (`IProfileParser`, `ILua`, `FLuaTaggedValue`, `FSectionEnumCallback`, `AcquireProfileParser()`); адреса и статусы функций — в decomp-tracker (листинг `EXE/Disasm/luaprofile.dll.txt`).

## Роль и границы

- `DllMain` регистрирует фабрику `"ILua"` (приоритет `0x40000000`) и больше ничего; PE-экспортов нет. Строка `"ILua"` есть только в самой `luaprofile.dll` (проверено по всем retail PE и INI); `CreateInstance("ILua")` не делает никто. В нашем `bin/DACOM.INI` библиотека тоже не загружается.
- `IProfileParser` отдаёт встроенный парсер dacom (`ProfileParser2`, агрегированный в `COManager`), см. [dacom.md](dacom.md). `CreateInstance` выбирает фабрику по CRC имени, поэтому `"ILua"` достижим только по собственному имени; фабрики `"ProfileParser"`/`"ProfileParser2"` (приоритет `0x80000000`) — отдельные компоненты dacom.
- Эта VM — **не** Thorn (`src/Thorn/lua32/` — другая копия Lua 3.2) и не общий `lua_State`: каждый экземпляр `ILua` создаёт свой `lua_State` (`lua_setstate(0)` + `lua_open`). Общий публичный заголовок не должен раскрывать внутренности (`kRegisterPriority` конфликтует с константой Shading).

## Создание объекта

`CreateInstance` требует AGGDESC размером `0x14` и имя, побайтно равное `"ILua"`. Поля: `+8` — **внешний `IUnknown`** (controlling unknown, объект `+0x18`), `+0xC` — `IUnknown**` для inner (объект `+0x1C`), `+0x10` не читается. Это та же раскладка, что у `SystemContainer`; комментарий в `dacom.h` про «IID по `+8`» относится к другому дескриптору. Результат — база объекта (фасет `ILua` на +0), не фасет парсера; парсер получают через QI inner (фасет `IProfileParser` на `+4`, `IAggregateComponent` на `+8`). OOM → `0xFFFFFFFC`, неверный размер/имя → `0xFFFFFFFD`.

Объект — `calloc(0x28)`. Конструктор подготавливает VM: Lua-хелперы `__get_profile_line(i)` и `__get_table_line(table, i)` (`next`/`nextvar`, индекс **0-based**, считаются только строки и числа), регистрирует C-колбэк `_ERRORMESSAGE`, который пишет через `FDUMP` строку `"%s(%d) : ERROR:General:%s"`. Последний `Release` закрывает VM.

## Контракты слотов `IProfileParser` (фасет ILua)

| Слот | Поведение |
|------|-----------|
| 3 | `flags != 0` → `-1`; иначе `lua_dofile(path)`, результат отбрасывается, всегда `GR_OK`. Имени у слота нет (в бинаре нет строк метода) — не именовать |
| 4 | обход глобальных **таблиц** Lua через `lua_nextvar`, колбэк `cb(parser, name, ctx)`; `0` — стоп (метод возвращает 0), конец обхода — 1 |
| 5 `OpenSection` | разрешает только `flags == 3`; имя `"$"` → ref `-1`, иначе `getglobal` + `istable` + `lua_ref` |
| 6 `CloseSection` | снять ref и освободить handle (8 байт: next, ref) |
| 7 `ReadLine` | `key=value`; строки берутся через `__get_table_line`; на handle `"$"` путь **мёртв** (в PE на `06801413` лежит `MOV EAX, lua_callfunction` вместо `CALL`, поэтому ветка ниже недостижима) |
| 8 `GetString` | длина/0; на `"$"` — `getglobal(key)` |

Встроенный парсер dacom имеет тот же контракт слотов 5–8 (EngBase, system, RP8, Shading, alchemy, Deformable2, SoundManager, RendComp открывают секцию с `flags == 3`); слоты 3–4 у него другие (загрузка INI-файла и обход имён секций), поэтому «не именовать». Конвертера INI-секций в Lua-таблицы в этом DLL нет: на свежем `ILua` `OpenSection("Engine")` вернёт 0, пока слот 3 не выполнит Lua с таблицей `Engine`.

Фасет `ILua` (слот 3): вызвать глобальную Lua-функцию `(name, out_rec*, argc, argv**)`; не функция → `0xFFFFFFFE`, и предыдущий `lua_State` **не** восстанавливается; аргументы — записи `tag` + payload (tag 9 — строка, 5 — float, 6 — double, бит `0x1000` — целое). Вызывающих в retail нет. В порту `InvokeGlobal` задаёт только активный `Payload.String` (retail копирует в неактивные байты union неинициализированное слово — побайтной совместимости с этим мусором порт не заявляет).

## Известные расхождения

- `luaV_tostring` печатает число через `sprintf("%.16g")` UCRT, retail использует MSVCRT: для `1e20` получаются `"1e+20"` и `"1e+020"`. Результат интернируется и виден Lua; расхождение зафиксировано в [crt_compat.md](crt_compat.md).
- Порт ничего не знает про `FL_DevOnlyLibs.ini` (в репозитории нет); что грузил retail-dev через него, не доказано.
