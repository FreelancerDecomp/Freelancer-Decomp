# movie.dll

`movie.dll` is a standalone (non-DACOM) DLL for the intro movies, with seven cdecl `MediaPlayer::*` exports. Read this if you touch the movie playback at client startup. The port is `src/movie/`; there is no public header in `src/include`, so `src/freelancer/Client/engine_init.cpp` (`StartupMediaAndIntroMovies`) obtains the exports through `LoadLibrary`/`GetProcAddress`, while the retail exe imports them statically. Function addresses and globals are in decomp-tracker (module `movie.dll`).

The live path is **approach 0** (DirectShow): `SetApproach` ignores its argument and always sets 0. `PlayMovie` builds a `CLSID_FilterGraph` + `CLSID_WMAsfReader` graph (`IFileSourceFilter::Load`, `EnumPins`/`Render`, `IMediaControl::Run`) and returns while the movie is still playing; the window is subclassed (notification `0x8001`, Shift/Escape end the movie), after which the COM objects are released and the window procedure is restored. `UpdateMovie` does `Sleep(0x12C)` and returns a "movie finished" byte, without polling `IMediaEvent`. The graph gets an `IServiceProvider` that returns an `IWMAuthorizer` for `IID_IWMReader`.

Approach 2 (MCI: `MCIWndCreateA`, class `MPMCIParent`) runs only if the approach flag is already 2; `Startup` registers the parent window class only in that case.
