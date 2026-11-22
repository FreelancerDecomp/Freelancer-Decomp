# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# Win32 only.
if(NOT FL_WIN32)
  return()
endif()

fl_add_module(MemoryDiagnostics EXE TOOL NO_WPO UNICODE NO_INCREMENTAL WARNING_LEVEL 4 ITEM_DEFAULTS DebugInformationFormat ProgramDatabase
  SOURCES
    main.cpp
    diagnostics.cpp
)

fl_cl_options(MemoryDiagnostics /utf-8)
fl_link(MemoryDiagnostics
  LIBS user32.lib gdi32.lib advapi32.lib shell32.lib ole32.lib comdlg32.lib psapi.lib)
