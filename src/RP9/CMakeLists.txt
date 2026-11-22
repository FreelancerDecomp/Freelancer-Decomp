# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# RP9.dll: the port-only Direct3D 9 renderer (docs/re/rp9.md), Win32 only, built only with
# FreelancerRp9=1, which also turns on the client's hooks. Off, it needs no DirectX SDK June 2010.
if(NOT FL_WIN32 OR NOT FreelancerRp9)
  return()
endif()

# DirectX SDK June 2010 ($(Dx9SdkDir), cmake/Freelancer.cmake): D3DX9, DxErr and the d3dx9_43
# redist cab.
if(FL_DX9_DIR STREQUAL "" OR NOT EXISTS "${FL_DX9_DIR}/Include/d3dx9.h")
  message(FATAL_ERROR "RP9 needs the DirectX SDK (June 2010): set DXSDK_DIR or pass "
                      "-DDx9SdkDir=... (looked for '${FL_DX9_DIR}/Include/d3dx9.h').")
endif()

fl_add_module(RP9 DLL PORT
  SOURCES
    RP9.cpp
    RP9Ex.cpp
    Mgsdb.cpp
    RP9.rc
)

# Selects the Direct3D 9 branch of render/d3d8_min.h (the public render interfaces seen through
# D3D9 types).
fl_defines(RP9 RP_DIRECT3D9)

# d3d9.h / d3d9.lib come from the Windows SDK. The June 2010 Include dir (D3DX9, DxErr) also ships
# an older d3d9.h, so it goes after the Windows SDK (and before the DX8 headers); its Lib\x86 is
# not searched (it holds an old d3d9.lib too): d3dx9.lib and DxErr.lib are linked by full path.
set_property(TARGET RP9 PROPERTY CXX_COMPILER_LAUNCHER
  "${CMAKE_COMMAND}" -E env
  "--modify" "INCLUDE=path_list_append:${FL_DX9_DIR}/Include"
  "--modify" "INCLUDE=path_list_append:${FL_DX8_DIR}/include")

# legacy_stdio_definitions.lib: DxErr.lib (VS2008-built) calls the pre-UCRT stdio entry points.
fl_link(RP9
  LIBS dacom.lib ddraw.lib d3d9.lib version.lib "${FL_DX9_DIR}/Lib/x86/d3dx9.lib"
       "${FL_DX9_DIR}/Lib/x86/DxErr.lib" legacy_stdio_definitions.lib
  REFERENCES dacom)

# The 32-bit D3DX9 runtime next to the exe: SysWOW64 has no d3dx9_43.dll. Extracted once from
# the June 2010 redist cab, with Windows' expand.exe by full path: from a Git Bash/MSYS shell a
# bare "expand" is coreutils' (/usr/bin/expand, earlier on PATH), which fails.
file(TO_NATIVE_PATH "$ENV{SystemRoot}/System32/expand.exe" _expand)
file(TO_NATIVE_PATH "${FL_DX9_DIR}/Redist/Jun2010_d3dx9_43_x86.cab" _cab)
add_custom_command(TARGET RP9 POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -DEXPAND=${_expand} -DCAB=${_cab} -DOUT=$<TARGET_FILE_DIR:RP9>
          -P "${CMAKE_CURRENT_SOURCE_DIR}/extract_d3dx9.cmake"
  COMMENT "RP9: d3dx9_43.dll (x86) into the output directory"
  VERBATIM)
