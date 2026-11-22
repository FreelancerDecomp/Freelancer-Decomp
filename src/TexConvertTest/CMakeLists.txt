# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# TexConvertTest.exe: port-only check of TexConvert against D3DX 8.1 (docs/re/texconvert.md).
# Win32 only: D3DX 8.1 has no 64-bit version.
if(NOT FL_WIN32)
  return()
endif()

fl_add_module(TexConvertTest EXE TOOL CONSOLE NO_SAFESEH
  SOURCES
    TexConvertTest.cpp
)

# The DX8.1 SDK's d3dx8.lib asks for libci (VC6 iostreams) and has no SafeSEH tables, as in
# RP8.
fl_includes(TexConvertTest ../TexConvert)
fl_link(TexConvertTest
  LIBS TexConvert.lib d3d8.lib d3dx8.lib legacy_stdio_definitions.lib
  REFERENCES TexConvert
  LIBDIRS ${FL_DX8_DIR}/lib)
target_link_options(TexConvertTest PRIVATE /NODEFAULTLIB:libci.lib)
