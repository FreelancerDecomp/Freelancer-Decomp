# RP9 post-build step (RP9/CMakeLists.txt): extract d3dx9_43.dll
# from the DirectX SDK (June 2010) redist cab into the output directory, once.
#   cmake -DEXPAND=<expand.exe> -DCAB=<Jun2010_d3dx9_43_x86.cab> -DOUT=<dir> -P extract_d3dx9.cmake
if(EXISTS "${OUT}/d3dx9_43.dll")
  return()
endif()
file(TO_NATIVE_PATH "${OUT}" _out)
execute_process(COMMAND "${EXPAND}" "${CAB}" -F:d3dx9_43.dll "${_out}" RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "expand.exe failed (${_rc}) extracting d3dx9_43.dll from ${CAB}")
endif()
