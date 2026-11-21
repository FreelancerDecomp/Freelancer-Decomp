# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# RPGPU: the port-only SDL3 + SDL_GPU render pipeline (docs/re/rpgpu.md), built with
# FreelancerRpGpu=1 (on by default on x64). SDL3 comes from the shared vcpkg manifest,
# src/vcpkg.json (feature rpgpu), which the root CMakeLists.txt installs. Off, or without vcpkg,
# RPGPU.dll is not built; nothing else depends on it.
if(NOT FreelancerRpGpu)
  return()
endif()

fl_add_module(RPGPU DLL PORT
  SOURCES
    RpGpu.cpp
    RpGpuDds.cpp
    RpGpuResources.cpp
    RpGpuState.cpp
    backend/RpGpuNullBackend.cpp
    backend/RpGpuSdlBackend.cpp
)

# render/d3d8_min.h's Direct3D 9 branch: RP8's interface types from the Windows SDK's d3d9.h
# (identical layouts), so no DirectX 8 SDK is needed on either platform. NOMINMAX: windows.h's min
# / max macros would break std::max.
fl_includes(RPGPU ../TexConvert)
fl_defines(RPGPU RP_DIRECT3D9 NOMINMAX)
fl_link(RPGPU
  LIBS dacom.lib TexConvert.lib
  REFERENCES TexConvert dacom)

# SDL3 after the module's own libraries, where vcpkg's MSBuild integration put it.
fl_vcpkg_package(RPGPU SDL3 SDL3::SDL3)
if(NOT RPGPU_VCPKG_OK)
  set_target_properties(RPGPU PROPERTIES EXCLUDE_FROM_ALL ON)
  return()
endif()

# SDL3.dll next to RPGPU.dll, as vcpkg's MSBuild integration copies it.
add_custom_command(TARGET RPGPU POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:RPGPU> $<TARGET_FILE_DIR:RPGPU>
  COMMAND_EXPAND_LISTS VERBATIM)
