# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# Win32 only: D3DX 8.1 has no 64-bit version (the x64 client renders with RPGPU).
if(NOT FL_WIN32)
  return()
endif()

fl_add_module(RP8 DLL RETAIL NO_SAFESEH
  SOURCES
    RP8.cpp
    Mgsdb.cpp
    RP8.rc
)

# DX8.1 SDK d3dx8.lib objects carry -defaultlib:libci (the VC6 single-threaded iostream library),
# which no current toolset ships; RP8 does not use iostreams.
# DX8.1 SDK d3dx8.lib has no SafeSEH tables; Release otherwise links with /SAFESEH.
fl_link(RP8
  LIBS dacom.lib ddraw.lib d3d8.lib d3dx8.lib legacy_stdio_definitions.lib
  REFERENCES dacom
  LIBDIRS ${FL_DX8_DIR}/lib)
target_link_options(RP8 PRIVATE /NODEFAULTLIB:libci.lib)
