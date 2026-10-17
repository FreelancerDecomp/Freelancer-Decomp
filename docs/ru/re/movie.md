# movie.dll

`movie.dll` — автономная (не DACOM) DLL интро-роликов с семью cdecl-экспортами `MediaPlayer::*`. Нужна тому, кто трогает запуск видео при старте клиента. Порт — `src/movie/`; публичного заголовка в `src/include` нет, поэтому `src/freelancer/Client/engine_init.cpp` (`StartupMediaAndIntroMovies`) берёт экспорты через `LoadLibrary`/`GetProcAddress`, а retail-exe импортирует их статически. Адреса и глобалы функций — в decomp-tracker (модуль `movie.dll`).

Живой путь — **подход 0** (DirectShow): `SetApproach` аргумент не читает и всегда выставляет 0. `PlayMovie` строит граф `CLSID_FilterGraph` + `CLSID_WMAsfReader` (`IFileSourceFilter::Load`, `EnumPins`/`Render`, `IMediaControl::Run`) и возвращается, пока ролик ещё идёт; окно подклассируется (уведомление `0x8001`, Shift/Escape завершают ролик), а затем COM-объекты освобождаются и оконная процедура восстанавливается. `UpdateMovie` делает `Sleep(0x12C)` и возвращает байт «ролик закончился», `IMediaEvent` не опрашивает. На граф ставится `IServiceProvider`, который на `IID_IWMReader` отдаёт `IWMAuthorizer`.

Подход 2 (MCI: `MCIWndCreateA`, класс `MPMCIParent`) выполняется, только если флаг подхода уже равен 2; `Startup` регистрирует класс родительского окна лишь тогда.
