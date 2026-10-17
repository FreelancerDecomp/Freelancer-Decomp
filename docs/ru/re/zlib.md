# zlib.dll

`zlib.dll` — сторонняя библиотека сжатия (zlib + MiniZip), а не компонент DACOM. В retail её использует только `DALib.dll` (сжатие сетевых пакетов DirectPlay). Документ нужен, чтобы понять, почему наш порт — это вендорный zlib 1.3.1 с особым ABI, и что нельзя менять в `src/zlib/`. Порт — `src/zlib/` → `bin\zlib.dll`; исходники zlib и MiniZip — `src/zlib/vendor/`.

## Что такое retail-DLL

Стандартный zlib 1.1.4 (Windows-сборка Gilles Vollant / Alessandro Iacopetti, 2002) плюс MiniZip (`unz*`/`zip*`), без обёрток Digital Anvil. Импортирует CRT и KERNEL32; `DllMain` — обычный CRT, игровой инициализации нет. `zlibVersion()` возвращает `"1.1.4"`, а `deflateInit2_`/`inflateInit_` принимают только версию, начинающуюся с `'1'`, и `stream_size == 0x38` (56 байт).

**Соглашение вызова — `__stdcall` (WINAPI), а не cdecl**, в отличие от стандартной `zlib1.dll`. Имена экспортов не декорированы (задаются `.def`), `gzprintf` — varargs (cdecl). Retail `DALib` вызывает `deflateInit_` с четырьмя push и без `ADD ESP` — cdecl-сборка разрушила бы стек.

## Поверхность экспорта

Ordinal base 1, 87 слотов, 65 именованных экспортов; пустые (RVA 0) ординалы 42–60, 69, 79, 85 зарезервированы и **не заполняются**. Ядро zlib 1.1.4 — ординалы 1–41, MiniZip — 61–87 (сигнатуры 2002 года: `unzGetCurrentFileInfo` — 8 аргументов, `zipOpenNewFileInZip` — 10, `zipOpenNewFileInZip2` — 11). Лишних API (`inflateBack`, `compressBound`, `*64`) не экспортируется. Проверка: `dumpbin /EXPORTS EXE\zlib.dll` против `bin\zlib.dll` должна давать одинаковые имена и ординалы.

`DALib` импортирует функции **по ординалам** (4 `deflate`, 6 `deflateEnd`, 8 `deflateInit_`, 10 `deflateReset`, 19 `inflate`, 20 `inflateEnd`, 22 `inflateInit_`, 23 `inflateReset`); ординалы менять нельзя. Потребитель в retail — класс сетевого кодека (`FUN_065c1000`): два `z_stream` подряд (deflate со степенью 6 и inflate), одноразовые вызовы с `Z_FINISH` и последующим `Reset`; сжатие отвергает вход короче `0x20` байт.

## Принцип порта

Не переписывать inflate/deflate, а вендорить совместимую C-реализацию:

- zlib 1.3.1 + MiniZip из того же архива: битовый поток совместим с 1.1.4, `z_stream` по-прежнему 56 байт на Win32, `inflateInit_` принимает `"1.1.4"`.
- Сборка с `ZLIB_WINAPI` (`ZEXPORT` = `WINAPI`), без `ZLIB_DLL` (чтобы не конфликтовал `__declspec(dllexport)` с `.def`).
- `src/zlib/zlib.def` задаёт недекорированные имена и точные ординалы; `zlibVersion` экспортируется как алиас `FreelancerZlibVersion` (`src/zlib/zlib_version_compat.c`), который возвращает `"1.1.4"`.
- Известные расхождения: `FILEVERSION` в `zlib.rc` — `1.3.1.0` (retail `1.1.4.0`; никто не читает); `gzFile` — непрозрачный указатель 1.3.1, а не `FILE*`; внутри MiniZip есть Zip64-типы, но экспортируемые 32-битные структуры сохраняют раскладку 2002 года.

Наш `DALib` (`src/DALib/CDPNetCodec.cpp`) собирается с `ZLIB_WINAPI`, включает `vendor/zlib.h` и линкуется с `zlib.lib`. DLL в `[Libraries]` не загружается — только как PE-зависимость `DALib`. Не ставить её на путь поиска приложений, ожидающих cdecl-`zlib1.dll`.
