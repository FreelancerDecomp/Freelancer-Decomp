# Freelancer Decomp: the CMake build. See docs/building.md.
#
# Windows (from an x86 or x64 Developer Command Prompt, or Visual Studio's Open Folder):
#   cmake --preset release            && cmake --build --preset release            (Win32, bin\)
#   cmake --preset release-x64        && cmake --build --preset release-x64        (x64, bin_x64\)
# Linux and macOS (the portable modules only, see cmake/Freelancer.cmake):
#   cmake --preset portable && cmake --build --preset portable && ctest --preset portable

# 3.27: `cmake -E env --modify` (the DX8 include order, cmake/Freelancer.cmake).
cmake_minimum_required(VERSION 3.27)

# ---------------------------------------------------------------------------------------------
# vcpkg, for the port-only modules: one manifest (src/vcpkg.json), installed by the toolchain
# file while project() runs, so it is set up here. Only a module that is on needs it: today
# RPGPU (FreelancerRpGpu, on by default on x64, whose client has no other renderer; RP8 needs
# D3DX8, which is 32-bit only). With nothing on, a new build tree does not use vcpkg at all; a
# tree first configured with one on keeps vcpkg's toolchain, which then installs nothing.
# ---------------------------------------------------------------------------------------------
if(CMAKE_HOST_WIN32)
  if("$ENV{VSCMD_ARG_TGT_ARCH}" STREQUAL "x64")
    set(_fl_rpgpu_default 1)
  else()
    set(_fl_rpgpu_default 0)
  endif()
  set(FreelancerRpGpu "${_fl_rpgpu_default}" CACHE STRING
      "PORT(rpgpu): RPGPU.dll, the SDL3 + SDL_GPU render pipeline (0 or 1; default 1 on x64)")
  option(FreelancerRequireVcpkg
         "A port-only module that is on fails the build without vcpkg, instead of being skipped" OFF)

  set(_fl_vcpkg_features)
  if(FreelancerRpGpu)
    list(APPEND _fl_vcpkg_features rpgpu)
  endif()
  if(_fl_vcpkg_features AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    # vcpkg's root: VCPKG_ROOT (Visual Studio's developer prompt), VCPKG_INSTALLATION_ROOT
    # (GitHub's Windows runners) or Visual Studio's own copy.
    foreach(_root "$ENV{VCPKG_ROOT}" "$ENV{VCPKG_INSTALLATION_ROOT}" "$ENV{VSINSTALLDIR}VC/vcpkg")
      if(_root AND EXISTS "${_root}/scripts/buildsystems/vcpkg.cmake")
        file(TO_CMAKE_PATH "${_root}/scripts/buildsystems/vcpkg.cmake" _fl_toolchain)
        # A toolchain only takes effect in a new build tree: CMake keeps the one the tree was
        # first configured with (its CMakeSystem.cmake, written by the first project()).
        if(EXISTS "${CMAKE_BINARY_DIR}/CMakeFiles/${CMAKE_VERSION}/CMakeSystem.cmake")
          message(FATAL_ERROR "A vcpkg module was turned on in a build tree configured without "
                              "vcpkg. Configure it afresh, keeping the switch and any other -D "
                              "options: cmake --fresh --preset <preset> "
                              "-DFreelancerRpGpu=${FreelancerRpGpu} ...")
        endif()
        set(CMAKE_TOOLCHAIN_FILE "${_fl_toolchain}" CACHE FILEPATH "vcpkg's toolchain file")
        set(_fl_vcpkg_ours ON CACHE INTERNAL "CMAKE_TOOLCHAIN_FILE is vcpkg's, set by this file")
        break()
      endif()
    endforeach()
  endif()
  if(_fl_vcpkg_ours AND NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR "This build tree was configured with vcpkg at '${CMAKE_TOOLCHAIN_FILE}', "
                        "which is gone. Configure it afresh: cmake --fresh --preset <preset>, "
                        "repeating any -D options.")
  endif()
  if(DEFINED CMAKE_TOOLCHAIN_FILE)
    set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src" CACHE PATH "")
    # The manifest features of every module that is on: they all install into one folder.
    set(VCPKG_MANIFEST_FEATURES "${_fl_vcpkg_features}" CACHE STRING "" FORCE)
    # Nothing on: no install step. A module copies the package DLLs it needs itself (RPGPU), so
    # vcpkg's own copy step after each link is not used.
    if(_fl_vcpkg_features)
      set(VCPKG_MANIFEST_INSTALL ON CACHE BOOL "" FORCE)
    else()
      set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "" FORCE)
    endif()
    set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "" FORCE)
  endif()
endif()

project(FreelancerDecomp LANGUAGES C CXX)
if(WIN32)
  enable_language(RC)
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Freelancer)

if(FL_WINDOWS)
  # Ordered so that every module is defined before the modules that link it.
  set(FL_PROJECTS
    dacom zlib qhull DALib Common Server Content
    EngBase ReadFile alchemy Deformable2 x86math luaprofile system rpclocal Thorn
    SoundManager SoundStreamer RP8 RP9 TexConvert RPGPU RendComp Shading flmaterials ximage
    movie imeui freelancer RemoteClient RemoteServer
    Resources/resources Resources/InfoCards Resources/MiscText Resources/NameResources
    Resources/EquipResources Resources/OfferBribeResources Resources/MiscTextInfo2
    Resources/serverresources
    FLServer FLDedicated
    FLRegress RpcCodecTest TexConvertTest DPNetCheck MemoryDiagnostics)
else()
  # The modules that use no Windows API (fl_add_module's PORTABLE), and DPNet with its tests.
  set(FL_PROJECTS qhull TexConvert DPNet)
  enable_testing()
endif()
foreach(_project IN LISTS FL_PROJECTS)
  add_subdirectory(src/${_project})
endforeach()

# The dev *.ini files of bin\ into every other output directory (cmake/Freelancer.cmake).
fl_copy_dev_ini()
