# x86math.dll — 3DMathEngine

`x86math.dll` — маленький DACOM-модуль с быстрыми 3D-операциями (матрицы, трансформы, кватернионы, `InvSqrt`), которым пользуются EngBase, Common, RP8, RendComp, Shading, Thorn, alchemy, Deformable2, flmaterials, Content и Server. Документ нужен, чтобы понять, как получить объект, какие у него типы и какие численные особенности порт обязан сохранять. Порт — `src/x86math/`, публичный ABI — [src/include/x86math.h](../../../src/include/x86math.h) (`I3DMathEngine`, `FVector`/`FMatrix`/`FTransform`/`FQuaternion`, `FMathDesc`); слоты и статусы функций — в decomp-tracker (листинг `EXE/Disasm/x86math.dll.txt`). Правила вызова чужих интерфейсов — [cross_dll_protocol.md](cross_dll_protocol.md).

## Как получить объект

PE-экспортов нет (только `DllMain`); другие модули не линкуются с x86math, а создают объект через `ICOManager::CreateInstance` с дескриптором `{size = 0xC, interface_name = "3DMathEngine", implementation = NULL}` (поле `+8`: NULL или `"x86"`). Фабрика зарегистрирована с приоритетом `0x40000000`. Коды: успех — `GR_OK`, `*out` — объект с AddRef; `desc == 0` или нет имени — `0xFFFFFFFE`; неверный размер/имя/implementation — `0xFFFFFFFD`. Имя сравнивается побайтно с `"3DMathEngine"` (13 байт). `QueryInterface` на самом объекте всегда возвращает `*out = 0` и `GR_GENERIC`: IID `"1.11_I3DMathEngine"` в retail не используется, единственный путь к объекту — `CreateInstance`. `AddRef`/`Release` всегда 1 (синглтон). Если `CreateInstance` сломан, EngBase получает нулевые указатели math и падает при первом вызове.

`DllMain` (attach): создаёт объект с двумя vfptr (интерфейс math + фабрика, 8 байт), строит таблицу `InvSqrt` и регистрирует компонент. При неудаче `new` возвращает TRUE, не строя таблицу.

## Типы

`Vector` — 12 байт `{X,Y,Z}`; `Matrix` — 36 байт, row-major `d[row][col]`, `out = M * v` даёт `out.x = row0·v`; `Transform` — 48 байт, `Matrix` + translation по `+0x24`; `Quaternion` — 16 байт `{W,X,Y,Z}` (скаляр первым). Порядок слотов `I3DMathEngine` не совпадает с реконструкцией Liberty `3dMath.h`; источник истины — vtable retail. В конец интерфейса порт добавил слот `QuatSlerpValue` с `const float* T` — контракт между нашими модулями, не совместимость с retail (прежний `QuatSlerp(float)` делегирует ему).

## Численные особенности, которые нельзя «упрощать»

- Большинство методов — x87 с промежуточными значениями в 80-битной точности; порядок сложений, раннее округление до `float`, чтение источника после записи приёмника (alias входа и выхода) влияют на результат и воспроизводятся буквально. Подходит только сверка по листингу; «эквивалентное» выражение на `float` округляет иначе.
- `MIN_DET = 1e-8f`: при `|det| <= MIN_DET` `inverse` и `general_inverse` пишут adjugate без деления и возвращают `GR_INVALID_PARMS`.
- **`InvSqrt`/`Sqrt`**: таблица на 10 бит мантиссы (`0x400` байт) + две итерации Ньютона `(3 - y²x) * y * 0.5`; `Sqrt` = тот же результат × x. Таблица в PE лежит нулями (BSS) и строится в `DllMain` x87-генератором (`FLD (i|0xFC00)<<14`, `FSQRT`, `FDIVR 1.0`, `(bits+0x2000)>>15`), после чего `table[0x200] = 0xFF` (слот мантиссы 1.0, где вычисленный байт переполняется). Сравнивать с PE-файлом бессмысленно, сверять надо с живой таблицей после `DllMain`; возможны единичные расхождения при другом x87 control word.
- **`transform_list` (Matrix)** — «FastDot»: каждая строка матрицы классифицируется как `==0` / `==1` / иное (27 вариантов ядра, порядок FPU: Z·C, затем Y·B, затем X·A); если сумма «сэкономленных операций» трёх строк `< 4`, используется общее тройное MAD, иначе — три косвенных вызова ядер. Не SIMD. Вариант `Transform` добавляет translation на x87 до финального `float`-store (при нулевой translation — хвост на Matrix-версию).
- **`quat_slerp`** — не Shoemake slerp, а nlerp с нормализацией: при `dot >= 0` lerp `A + T*(B-A)`, иначе `A - T*(A+B)` (lerp к `-B`); нормализация через `FSQRT` + `FDIVR 1.0`, **не** через табличный `InvSqrt`; нулевая длина не проверяется.
- `matrix_to_quaternion` / `quaternion_to_matrix` — стандартные формулы (след / наибольшая диагональ; для `matrix_to_quaternion` выбор ветви по сравнению x87, включая unordered), с округлением до `float` только на записях результата.

## Отклонения порта

1. Нет CRT `_initterm`/onexit: таблица строится явно в `DllMain` после успешного `new`.
2. Ядра FastDot — naked thiscall-члены `FFastDot` (результат в ST0), потому что MSVC не умеет свободный `__thiscall`; пороги и раскладка те же. Слоты 16/17 остаются `DACOM_API` stdcall.
3. Объект создаётся `::operator new(8, nothrow)` + placement new (MSVC пишет vfptr сам, retail — вручную; `sizeof == 8`).
4. `QueryInterface` и `CreateInstance` порта проверяют выходной указатель на NULL; retail пишет без проверки.
5. Статус отдельных методов (часть остаётся неполностью сверенной по подготовке аргументов FP-стека) — в tracker, а не здесь.
