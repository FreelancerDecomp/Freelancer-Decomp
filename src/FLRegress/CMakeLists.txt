# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# FLRegress.exe: PORT(flregress), the headless regression harness (no retail counterpart). It
# starts Server.dll as the single-player client does and dumps the game state to a text file.

fl_add_module(FLRegress EXE TOOL CONSOLE
  SOURCES
    FLRegress.cpp
    regress_client.cpp
    regress_crash.cpp
    regress_dump.cpp
    regress_hooks.cpp
)

fl_includes(FLRegress .)
fl_defines(FLRegress _CONSOLE)
fl_link(FLRegress
  LIBS dacom.lib DALib.lib Common.lib Server.lib ole32.lib winmm.lib psapi.lib dbghelp.lib
  REFERENCES dacom DALib Common Server)
