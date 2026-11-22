# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# Win32 only.
if(NOT FL_WIN32)
  return()
endif()

# DPNetCheck.exe: PORT(dpnet), a DirectPlay interoperability check (no retail counterpart). It
# joins a running server through Microsoft's DirectPlay 8 client, as a retail client does; see
# docs/dpnet.md. Win32 only: it must use Microsoft's dpnet.dll, not DPNet.

fl_add_module(DPNetCheck EXE TOOL NO_WPO CONSOLE
  SOURCES
    DPNetCheck.cpp
)

fl_defines(DPNetCheck _CONSOLE)
fl_link(DPNetCheck
  LIBS ole32.lib)
