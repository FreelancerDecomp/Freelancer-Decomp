# Карта адрес -> DLL (retail)

Сгенерировано `tools/abi.py build` из PE-заголовков `EXE/` — руками не править.
Увидел в чужом дизасме адрес — найди строку, чей диапазон его содержит,
или спроси `python tools/abi.py addr 0x06f12098`.

| Модуль | ImageBase | Конец | Размер | Дизасм | Наш порт |
|--------|-----------|-------|--------|--------|----------|
| `FLServer.exe` | `0x00400000` | `0x00432000` | 200 KiB | `EXE/Disasm/flserver.exe.txt` | `src/FLServer` |
| `Freelancer_Original.EXE` | `0x00400000` | `0x0068d000` | 2612 KiB | — | — |
| `freelancer.exe` | `0x00400000` | `0x00686000` | 2584 KiB | `EXE/Disasm/freelancer.exe.txt` | `src/freelancer` |
| `implib.exe` | `0x00400000` | `0x00445000` | 276 KiB | — | — |
| `alchemy.dll` | `0x06200000` | `0x06260000` | 384 KiB | `EXE/Disasm/alchemy.dll.txt` | `src/alchemy` |
| `Common.dll` | `0x06260000` | `0x065a4000` | 3344 KiB | `EXE/Disasm/common.dll.txt` | `src/Common` |
| `dacom.dll` | `0x065b0000` | `0x065bb000` | 44 KiB | `EXE/Disasm/dacom.dll.txt` | `src/dacom` |
| `DALib.dll` | `0x065c0000` | `0x065cd000` | 52 KiB | `EXE/Disasm/dalib.dll.txt` | `src/DALib` |
| `debuglib.dll` | `0x065d0000` | `0x065eb000` | 108 KiB | `EXE/Disasm/debuglib.dll.txt` | — |
| `deformable2.dll` | `0x065f0000` | `0x06606000` | 88 KiB | `EXE/Disasm/deformable2.dll.txt` | `src/Deformable2` |
| `engbase.dll` | `0x06610000` | `0x0662e000` | 120 KiB | `EXE/Disasm/engbase.dll.txt` | `src/EngBase` |
| `equipresources.dll` | `0x06630000` | `0x066f0000` | 768 KiB | `EXE/Disasm/equipresources.dll.txt` | — |
| `flmaterials.dll` | `0x066f0000` | `0x0670a000` | 104 KiB | `EXE/Disasm/flmaterials.dll.txt` | `src/flmaterials` |
| `imeui.dll` | `0x06710000` | `0x06720000` | 64 KiB | `EXE/Disasm/imeui.dll.txt` | `src/imeui` |
| `infocards.dll` | `0x06720000` | `0x067ff000` | 892 KiB | `EXE/Disasm/infocards.dll.txt` | — |
| `luaprofile.dll` | `0x06800000` | `0x06811000` | 68 KiB | `EXE/Disasm/luaprofile.dll.txt` | `src/luaprofile` |
| `misctext.dll` | `0x06820000` | `0x06a57000` | 2268 KiB | `EXE/Disasm/misctext.dll.txt` | — |
| `misctextinfo2.dll` | `0x06a60000` | `0x06ab7000` | 348 KiB | `EXE/Disasm/misctextinfo2.dll.txt` | — |
| `movie.dll` | `0x06ac0000` | `0x06ae7000` | 156 KiB | `EXE/Disasm/movie.dll.txt` | `src/movie` |
| `nameresources.dll` | `0x06af0000` | `0x06b3b000` | 300 KiB | `EXE/Disasm/nameresources.dll.txt` | — |
| `RemoteClient.dll` | `0x06b30000` | `0x06b79000` | 292 KiB | `EXE/Disasm/remoteclient.dll.txt` | `src/RemoteClient` |
| `offerbriberesources.dll` | `0x06b40000` | `0x06b62000` | 136 KiB | `EXE/Disasm/offerbriberesources.dll.txt` | — |
| `readfile.dll` | `0x06b70000` | `0x06b7d000` | 52 KiB | `EXE/Disasm/readfile.dll.txt` | `src/ReadFile` |
| `remoteserver.dll` | `0x06bd0000` | `0x06c19000` | 292 KiB | `EXE/Disasm/remoteserver.dll.txt` | `src/RemoteServer` |
| `rendcomp.dll` | `0x06c20000` | `0x06c3b000` | 108 KiB | `EXE/Disasm/rendcomp.dll.txt` | `src/RendComp` |
| `resources.dll` | `0x06c40000` | `0x06cf7000` | 732 KiB | `EXE/Disasm/resources.dll.txt` | `src/Resources` |
| `Server.dll` | `0x06ce0000` | `0x06da0000` | 768 KiB | `EXE/Disasm/server.dll.txt` | `src/Server` |
| `rp8.dll` | `0x06d00000` | `0x06d7b000` | 492 KiB | `EXE/Disasm/rp8.dll.txt` | `src/RP8` |
| `rpclocal.dll` | `0x06d80000` | `0x06dc1000` | 260 KiB | `EXE/Disasm/rpclocal.dll.txt` | `src/rpclocal` |
| `serverresources.dll` | `0x06e90000` | `0x06eaa000` | 104 KiB | `EXE/Disasm/serverresources.dll.txt` | — |
| `shading.dll` | `0x06eb0000` | `0x06edb000` | 172 KiB | `EXE/Disasm/shading.dll.txt` | `src/Shading` |
| `soundmanager.dll` | `0x06ee0000` | `0x06eef000` | 60 KiB | `EXE/Disasm/soundmanager.dll.txt` | `src/SoundManager` |
| `soundstreamer.dll` | `0x06ef0000` | `0x06f05000` | 84 KiB | `EXE/Disasm/soundstreamer.dll.txt` | `src/SoundStreamer` |
| `system.dll` | `0x06f10000` | `0x06f16000` | 24 KiB | `EXE/Disasm/system.dll.txt` | `src/system` |
| `thorn.dll` | `0x06f20000` | `0x06f66000` | 280 KiB | `EXE/Disasm/thorn.dll.txt` | `src/Thorn` |
| `x86math.dll` | `0x06f70000` | `0x06f77000` | 28 KiB | `EXE/Disasm/x86math.dll.txt` | `src/x86math` |
| `ximage.dll` | `0x06f80000` | `0x06f87000` | 28 KiB | `EXE/Disasm/ximage.dll.txt` | `src/ximage` |
| `ebueula.dll` | `0x10000000` | `0x10013000` | 76 KiB | `EXE/Disasm/ebueula.dll.txt` | — |
| `gundll.dll` | `0x10000000` | `0x1007e000` | 504 KiB | `EXE/Disasm/gundll.dll.txt` | — |
| `zlib.dll` | `0x10000000` | `0x10011000` | 68 KiB | `EXE/Disasm/zlib.dll.txt` | `src/zlib` |
| `dw15.exe` | `0x30000000` | `0x3002d000` | 180 KiB | `EXE/Disasm/dw15.exe.txt` | — |
| `unicows.dll` | `0x7f2d0000` | `0x7f30c000` | 240 KiB | `EXE/Disasm/unicows.dll.txt` | — |

