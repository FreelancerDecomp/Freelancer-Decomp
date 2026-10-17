# Audio: SoundManager and SoundStreamer

Sound in Freelancer is two DACOM plugins on top of DirectSound and MSACM32. `SoundManager.dll` plays
short sounds (effects, spatial sources, EAX), `SoundStreamer.dll` streams music and voice-over
(WAV and MP3 through ACM). This page is for whoever touches the port's audio code or wires up the client's
sound subsystems. Sources are in `src/SoundManager/`, `src/SoundStreamer/`; the public interfaces are
`src/include/soundmanager.h`, `src/include/soundstreamer.h`. Function statuses, slots and addresses are in
decomp-tracker, deviations from retail are in [deviations.md](deviations.md).

Retail has neither Miles, nor `mss32`, nor a separate wrapper over `dsound.dll`: both DLLs create
`CLSID_DirectSound` through `CoCreateInstance`. The video intro (`movie.dll`, DirectShow) is not part of
this stack.

## Loading and registration

`[Libraries]` loads `SoundStreamer.dll`, then `SoundManager.dll`; in `[System]` they are listed as
`SoundManager`, `SoundStreamer`. Each `DllMain` registers a factory (`RegisterComponent`, names
`"SoundManager"` and `"SoundStreamer"`); `CreateInstance` requires an `AGGDESC` of size `0x14`.
The objects are created by `SystemContainer`; `DALib::Startup` takes their interfaces into `DALib::Sound`
(`ISoundManager`) and `DALib::Streamer` (`IStreamer2`).

Device initialization is not in `IAggregateComponent::Initialize` (SoundManager's returns 0
immediately) but in `DALib::Startup`:

1. `ISoundManager::StartupDevice(hwnd, 0x113)` - reads the INI, creates DirectSound, the primary buffer,
   the mixer thread and, if needed, enables EAX. On failure: `ShutdownDevice`, `Release`, `Sound = nullptr`.
2. `ISoundManager::GetDirectSound(&ds)` - returns the same `IDirectSound*` with `AddRef`.
3. `IStreamer2::InitializeDevice({size = 0x18, ds, hwnd, 0, 0, gain})` - returns BOOL (1/0). If `ds`
   is empty, the Streamer does `CoInitialize` + `CoCreateInstance` itself. On failure: `Release`, `Streamer = nullptr`.
4. Either failure calls `CriticalWarning(0xD3)`; the host's verdict becomes the result of
   `DALib::Startup`.

`Shutdown` first releases the Streamer, then calls the manager's `ShutdownDevice` (the Streamer must return
the `AddRef` it took from the manager).

## SoundManager

The object (`0x14C` bytes) exposes `ISoundManager` (56 slots), `ISoundManagerStatus` (4 slots; the client does not
use it) and a nested sub-object at `+0x08` that lazily queries `IProfileParser` into a cache. Sound
archetypes are separate `ISoundArchetype` objects (`0x30` bytes), created by the manager from WAV
(uncompressed PCM or ACM through WavLib), not by the DACOM factory.

INI sections (read by DACOM's built-in parser; its absence does not stop `StartupDevice` - the keys are simply
skipped): `[SoundManager]` - `deviceID`, `3D_SW_Algorithm` (`BASIC`/`LIGHT`/`FULL`), `useNoHW`,
`use2DHW`/`use3DHW`, `createAll2DInSoftware`, `rightHandedCoordinates`, `speakerConfiguration`,
`maxSoundChannels`, `max3DPan`, `ALLOW_HW_TO_SW`, `ALLOW_SW_TO_HW`, `FORCE_FREQ_CONTROL_TO_SW`,
`NEVER_SET_START_POS`, `DISPLAY_EXTRA_DEBUG_INFO`, `useEAX`; `[AudioProperties]` - `useEAX`,
`reverbBaseEnvironment`, `reverbMasterVolume`, `reverbDecayTime`, `reverbDamping`, `reverbMix`. Boolean values
understand `true/yes/false/no`, numbers are decimal and `0x` hexadecimal.
`speakerConfiguration` is a number only (per the retail INI comment: HEADPHONES 1, STEREO 2, QUAD 7,
SURROUND 8); the code does not parse textual values.

What `StartupDevice` does: `CoInitialize` -> `CoCreateInstance(IDirectSound)` -> `Initialize` (first the
device from `deviceID`, then the default one) -> `SetCooperativeLevel(DSSCL_PRIORITY)` -> `GetCaps`
(the channel maximum is chosen from the number of free HW buffers) -> primary buffer -> HW capability check
with a trial buffer 22050 Hz/16 bit/stereo -> 3D listener, primary buffer format, EAX.

The mixer (`MixerTick`, slot 46, `+0xB8`; the client calls it every frame): removes instances whose play-key is gone, creates buffers
for new ones, drops ones that are too far away, computes distance attenuation (inverse square root and `10*log10`
if the buffer has no DS3D), adds the archetype gain, the master gain and the client volume, software pan
(`CTRLPAN`) instead of 3D, frequency, the listener's EAX tick. Lost buffers (`DSERR_BUFFERLOST`) are logged
as an error but not restored. Instance Play/Stop are slots 47/48; looping is taken from the client's
source object.

**Play key.** The manager does not create sources itself: the client (`src/freelancer/Client/menu_sound_arch.cpp`)
passes its own `ISoundSource` object (`AttachPlayKey`), and the mixer polls it every tick (position,
volume, range, whether it is alive). Slot 33 (WAV) takes an `IFileSystem*`, not a path.

**EAX.** Through `IKsPropertySet`: the four `reverb*` keys go out in a single `Set` (`DSPROPSETID_EAX20_BufferProperties`,
`ulId = 0`, 16 bytes); `reverbMix` is read but not used anywhere. Listener parameters are written to
`DSPROPSETID_EAX20_ListenerProperties`.

**GUID trap.** The tail of the retail IIDs `IDirectSound` / `IDirectSound3DListener` / `IDirectSound3DBuffer` is
`E5 60`, not `A7 70` as in the SDK header `dsound.h`; the CLSID matches the SDK. The port copies the 16 bytes of
retail (`SoundManagerInternal.h`, `audio/dsound8.h`), not the SDK macros. `IKsPropertySet` has the same
discrepancy.

## SoundStreamer

The object (`0x7C` bytes) exposes `IStreamer2` (15 slots; `IStreamer` is the same vtable) and
`IAggregateComponent`. It is a singleton: a second instance is not created. The stream handle is an opaque
`StreamingSound*` (`0x110` bytes), not a DACOM interface.

- **Slots.** Device initialization (3), `CreateStream(path, IFileSystem*, flags)` (4), destroy /
  stop / start (5/6/7), volume (8/9), status (10), pan (13/14); slots 11 and 12 always
  return 0. In `flags` only bit 0 (autostart) and bit 1 (loop) are meaningful.
- **CreateStream.** Without an `IFileSystem` it creates the file system itself (`AGGDESC 0x34`, `"FileSystem"`); with a
  passed one it checks through slot `+0x70` (file attributes: `-1` or a directory - rejected). The file is mapped
  (`+0x7C` / `+0x3C` / `+0x40`) and parsed as RIFF/WAVE: `fmt ` and `data` are mandatory, `fact` and `trim`
  optional. `trim` is a real MP3 "encoder delay" chunk: on the first decode block `trim * bytes/sample` of the
  decoded PCM is discarded. PCM goes into the DirectSound buffer immediately, the rest through ACM
  (`acmFormatSuggest`/`acmStreamOpen`/`Convert`).
- **MP3 codec.** An ACM driver named `Fraunhofer IIS MPEG Layer-3 Codec (advanced)` is searched for. Modern
  Windows often shows `... (decode only)`; at the owner's request the port accepts this name as well
  (in SoundStreamer and in SoundManager). Without a codec - `FDUMP` "missing MP3 codec?".
- **Worker thread.** `CreateThread` (4 KB stack, highest priority; `CoInitialize(NULL)`, i.e. STA), a loop
  with a 40 ms pause: under a critical section it fills the active streams (state 2 or 3), picking the emptiest,
  while the buffer holds less than ~0.12 s, then moves finished ones to the waiting list. The thread confirms
  startup with a bit in the state word (up to 5 s wait); `Shutdown` sets a stop bit and waits up to 2 s.
- **Handle states** (word `+0x80`): 0 dead/error, 1 ready, 2 start requested, 3 playing, 6 "drain"
  of a one-shot stream up to the end markers, 8 ended by itself, 9 stopped by the client. Values 4, 5, 7 are never
  assigned in the DLL. A stopped (8/9) stream can be started again.
- **`InitializeDevice` descriptor** (`0x18` bytes): `+0` size, `+4` `IDirectSound*`, `+8` `HWND`, `+0x0C` and
  `+0x10` are not read, `+0x14` gain (0 -> 1.0). Another size or `NULL` - `FDUMP` "Invalid
  parameters", result 0.

## Client layer and data

The client part (in retail `Client/cmn/Sound.cpp`, `Gf/ScriptSound.cpp`, `Voice.cpp`) lives in
`src/freelancer/Client/` (`menu_sound_arch.cpp`, `sound_config.cpp`, `voice_profiles.cpp`,
`gf_thorn_sound.cpp`); it calls interface methods, not slot numbers. Thorn asks scene objects for
`ISoundControl` - it is implemented by the client, not by these DLLs. The catalogs of sounds and voice-over are listed in
`freelancer.ini` (`[Startup] sounds = audio\*.ini`, `voices = audio\voices_*.ini`) and live in
`DATA\AUDIO\`.

## Known limitations

- The archetype storage in SoundManager is a linear array instead of retail's `std::map` (not visible through the
  interface).
- The listener does not move: the client calls `MixerTick(nullptr)`, and `ISoundListenerPose` (camera pose,
  velocity, coefficients) is not yet implemented by anyone in the port (`menu_sound_arch.cpp`, `soundmanager.h`).
- If the manager did not start, the Streamer creates its own DirectSound (as in retail), i.e. two DirectSound
  objects on one window.
- The rest, per function, is in decomp-tracker and the SoundManager / SoundStreamer section of
  [deviations.md](deviations.md).
