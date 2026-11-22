# Freelancer Decomp: the shared build settings of every module (src/<module>/CMakeLists.txt).
#
# Two kinds of build:
#
#   Windows (MSVC, Ninja): every module, Win32 or x64 by the compiler's target. This re-creates,
#   flag for flag, what MSBuild (platform toolset v143) passes to cl, rc, lib and link for the
#   projects in src\Freelancer.sln, including MSBuild's own defaults (Microsoft.Cpp.Default.props,
#   Microsoft.Cl.Common.props, Microsoft.Link.Common.props), which differ from CMake's: CMake
#   adds /DNDEBUG, /D_WINDOWS, /GR, /Ob2, <target>_EXPORTS and /subsystem:console, and drops
#   /Oy-, /Zc:inline, /SAFESEH and /sdl-, none of which match. src\Directory.Build.props and
#   src\Directory.Build.targets are the MSBuild side of everything below;
#   .github/scripts/build_parity.py compares the two builds command line by command line and
#   binary by binary.
#
#   Anything else (GCC or Clang on Linux and macOS): the portable modules only, the ones that
#   use no Windows API (fl_add_module's PORTABLE). Every other module is Windows-only until the
#   non-Windows port (on hold) gives it a platform backend. See docs/building.md.

include_guard(GLOBAL)

set(FL_SRC_DIR "${PROJECT_SOURCE_DIR}/src")

if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(FL_WINDOWS TRUE)
else()
  set(FL_WINDOWS FALSE)
endif()

# ---------------------------------------------------------------------------------------------
# Portable build (GCC / Clang): plain CMake targets for the PORTABLE modules.
# ---------------------------------------------------------------------------------------------
if(NOT FL_WINDOWS)
  if(WIN32)
    message(FATAL_ERROR "On Windows, Freelancer builds with MSVC (cl.exe) only; use the presets "
                        "in CMakePresets.json from a Developer Command Prompt.")
  endif()
  set(FL_PLATFORM "${CMAKE_SYSTEM_NAME}")
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Release or Debug" FORCE)
  endif()
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(FL_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
  option(FL_SANITIZE "AddressSanitizer and UndefinedBehaviorSanitizer for the portable modules" OFF)
  if(FL_SANITIZE)
    # No recovery: undefined behaviour stops the test with a failure instead of a warning.
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all
                        -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()

  # fl_add_module(<name> <DLL|EXE|LIB> <RETAIL|PORT|TOOL> PORTABLE ... SOURCES <files...>)
  # The Windows-only arguments (.rc and .def files, linker and character-set flags) are accepted
  # and ignored, so that a module's CMakeLists.txt has one form for every platform.
  function(fl_add_module name kind)
    cmake_parse_arguments(PARSE_ARGV 2 A
      "RETAIL;PORT;TOOL;PORTABLE;NO_WPO;UNICODE;MFC;CONSOLE;NO_SAFESEH;NO_INCREMENTAL;EXPORTS;PCH"
      "OUTPUT_NAME;WARNING_LEVEL;DEF" "SOURCES;ITEM_DEFAULTS")
    if(NOT A_PORTABLE)
      message(FATAL_ERROR "fl_add_module(${name}): not PORTABLE, so Windows-only; leave it out "
                          "of FL_PORTABLE_PROJECTS (CMakeLists.txt).")
    endif()
    set(_sources ${A_SOURCES})
    list(FILTER _sources EXCLUDE REGEX "\\.(rc|def)$")
    if(kind STREQUAL "EXE")
      add_executable(${name} ${_sources})
    elseif(kind STREQUAL "DLL")
      add_library(${name} SHARED ${_sources})
    elseif(kind STREQUAL "LIB")
      add_library(${name} STATIC ${_sources})
    else()
      message(FATAL_ERROR "fl_add_module(${name}): unknown kind ${kind}")
    endif()
    if(A_OUTPUT_NAME)
      set_property(TARGET ${name} PROPERTY OUTPUT_NAME "${A_OUTPUT_NAME}")
    endif()
    set_target_properties(${name} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${FL_BIN_DIR}"
      LIBRARY_OUTPUT_DIRECTORY "${FL_BIN_DIR}"
      ARCHIVE_OUTPUT_DIRECTORY "${FL_BIN_DIR}")
    target_include_directories(${name} PRIVATE "${FL_SRC_DIR}/include" "${FL_SRC_DIR}/IVP")
    target_compile_definitions(${name} PRIVATE FREELANCER_ENABLE_TICK_PROFILE=0)
    target_compile_options(${name} PRIVATE
      "SHELL:-include \"${FL_SRC_DIR}/include/fl_layout.h\""
      $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
  endfunction()

  function(fl_includes name)
    cmake_parse_arguments(PARSE_ARGV 1 A "AFTER_COMMON" "" "")
    target_include_directories(${name} PRIVATE ${A_UNPARSED_ARGUMENTS})
  endfunction()
  function(fl_defines name)
    target_compile_definitions(${name} PRIVATE ${ARGN})
  endfunction()
  function(fl_link name)
    cmake_parse_arguments(PARSE_ARGV 1 A "" "" "LIBS;REFERENCES;LIBDIRS")
    if(A_REFERENCES)
      target_link_libraries(${name} PRIVATE ${A_REFERENCES})
    endif()
  endfunction()
  # MSVC-only settings: nothing to do here.
  foreach(_fn fl_item fl_cl_options fl_rc fl_order_after fl_copy_dev_ini fl_copy_asan_runtime)
    cmake_language(EVAL CODE "function(${_fn})\nendfunction()")
  endforeach()
  return()
endif()

# ---------------------------------------------------------------------------------------------
# Windows (MSVC): platforms and configurations, as Freelancer.sln has them.
# ---------------------------------------------------------------------------------------------
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "X86")
  set(FL_PLATFORM Win32)
  set(FL_WIN32 TRUE)
  set(FL_X64 FALSE)
  set(FL_MACHINE X86)
  set(FL_CONFIGS Release Debug DebugASan)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
  # PORT(x64): the x64 build, in parallel with Win32, which stays the retail reference
  # (docs/x64.md). No DebugASan: Freelancer.sln has none for x64.
  set(FL_PLATFORM x64)
  set(FL_WIN32 FALSE)
  set(FL_X64 TRUE)
  set(FL_MACHINE X64)
  set(FL_CONFIGS Release Debug)
else()
  message(FATAL_ERROR "Freelancer builds for Win32 (x86) or x64: configure from an x86 or x64 "
                      "Developer Command Prompt, or use the presets in CMakePresets.json.")
endif()
# The presets name the platform they are for; the compiler comes from the developer prompt, which
# the command line does not check against the preset (its architecture is only a hint for IDEs).
set(FL_EXPECTED_PLATFORM "" CACHE STRING "Win32 or x64: fail when the compiler targets another")
if(FL_EXPECTED_PLATFORM AND NOT FL_EXPECTED_PLATFORM STREQUAL FL_PLATFORM)
  message(FATAL_ERROR "This preset builds ${FL_EXPECTED_PLATFORM}, but the compiler targets "
                      "${FL_PLATFORM}: configure from the matching Developer Command Prompt "
                      "(vcvarsall.bat x86 for Win32, x64 for x64).")
endif()
if(CMAKE_GENERATOR MATCHES "^Visual Studio")
  message(FATAL_ERROR "Use the Ninja generator (see CMakePresets.json): the Visual Studio "
                      "generators hand the flags back to MSBuild, which adds its own defaults.")
endif()

get_property(_fl_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_fl_multi)
  set(CMAKE_CONFIGURATION_TYPES ${FL_CONFIGS} CACHE STRING "" FORCE)
else()
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  endif()
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${FL_CONFIGS})
  if(NOT CMAKE_BUILD_TYPE IN_LIST FL_CONFIGS)
    message(FATAL_ERROR "CMAKE_BUILD_TYPE must be one of ${FL_CONFIGS} on ${FL_PLATFORM} "
                        "(got '${CMAKE_BUILD_TYPE}')")
  endif()
endif()

set(_fl_cfg_debug "$<CONFIG:Debug>")
set(_fl_cfg_asan "$<CONFIG:DebugASan>")
set(_fl_cfg_release "$<CONFIG:Release>")
set(_fl_cfg_debuglibs "$<CONFIG:Debug,DebugASan>")

# Output directories (Directory.Build.props): Release -> bin, Debug -> bin_debug, DebugASan ->
# binasan; x64 Release -> bin_x64, Debug -> bin_x64_debug. All are siblings at the repo root, so
# every ..\DATA / ..\EXE path the game resolves from its exe directory works from each of them.
if(FL_WIN32)
  set(FL_BIN_DIR_NAME "$<IF:${_fl_cfg_debug},bin_debug,$<IF:${_fl_cfg_asan},binasan,bin>>")
else()
  set(FL_BIN_DIR_NAME "$<IF:${_fl_cfg_debug},bin_x64_debug,bin_x64>")
endif()
set(FL_BIN_DIR "${PROJECT_SOURCE_DIR}/${FL_BIN_DIR_NAME}")

# ---------------------------------------------------------------------------------------------
# SDK locations and feature switches, under the names of the MSBuild properties
# (Directory.Build.props): -D<Name>=0|1 sets one as /p:<Name>=0|1 does.
# ---------------------------------------------------------------------------------------------
set(Dx8SdkDir "${PROJECT_SOURCE_DIR}/dx8" CACHE PATH "DirectX 8.1 SDK (holds include/ and lib/)")
set(Dx9SdkDir "$ENV{DXSDK_DIR}" CACHE PATH "DirectX SDK (June 2010), RP9 only")
file(TO_CMAKE_PATH "${Dx8SdkDir}" FL_DX8_DIR)
file(TO_CMAKE_PATH "${Dx9SdkDir}" FL_DX9_DIR)
string(REGEX REPLACE "/+$" "" FL_DX9_DIR "${FL_DX9_DIR}")
if(NOT EXISTS "${FL_DX8_DIR}/include/d3d8.h" OR NOT EXISTS "${FL_DX8_DIR}/lib/d3d8.lib")
  message(FATAL_ERROR "DirectX 8.1 SDK not found under '${FL_DX8_DIR}' (needs include/d3d8.h and "
                      "lib/d3d8.lib). Copy it to dx8/ at the repo root or pass -DDx8SdkDir=<path>.")
endif()

# fl_switch(<name> <default> <doc>): a 0/1 cache variable.
function(fl_switch name default doc)
  set(${name} "${default}" CACHE STRING "${doc} (0 or 1)")
  set_property(CACHE ${name} PROPERTY STRINGS 0 1)
  if(NOT ${name} MATCHES "^[01]$")
    message(FATAL_ERROR "${name} must be 0 or 1 (got '${${name}}')")
  endif()
  message(STATUS "${name}=${${name}}")
endfunction()

fl_switch(FreelancerEnableTickProfile 0 "Tick profiler, FREELANCER_ENABLE_TICK_PROFILE")
# PORT(diag): "Beta 2" on-screen label, FREELANCER_BETA_LABEL.
fl_switch(FreelancerBetaLabel 0 "PORT(diag): the Beta 2 on-screen label")
# PORT(widescreen): widescreen (Hor+) support; 0 builds retail's 4:3-only client.
fl_switch(FreelancerWidescreenSupport 1 "PORT(widescreen): widescreen (Hor+) support")
# PORT(rp9): Direct3D 9 rendering (RP9.dll + the client's Ex hooks: volumetric nebula); 0
# builds neither RP9.dll nor the client hooks (docs/re/rp9.md).
fl_switch(FreelancerRp9 0 "PORT(rp9): RP9.dll and the client's Direct3D 9 hooks")
# PORT(walk): walk mode in base rooms (docs/walk_mode.md).
fl_switch(FreelancerWalkMode 1 "PORT(walk): walk mode in base rooms")
# PORT(rpgpu): the SDL3 + SDL_GPU render pipeline (RPGPU.dll, docs/re/rpgpu.md). The default
# (on for x64, off for Win32) and vcpkg are set up before project() in the root CMakeLists.txt.
message(STATUS "FreelancerRpGpu=${FreelancerRpGpu}")

# ---------------------------------------------------------------------------------------------
# Global flags: cleared, so that only what fl_add_module() sets reaches the tools. Normal
# variables shadow the cache, so -DCMAKE_CXX_FLAGS=... cannot quietly make the builds differ.
# ---------------------------------------------------------------------------------------------
foreach(_lang C CXX RC)
  set(CMAKE_${_lang}_FLAGS "")
  set(CMAKE_${_lang}_STANDARD_LIBRARIES "")
  foreach(_cfg ${FL_CONFIGS} MinSizeRel RelWithDebInfo)
    string(TOUPPER "${_cfg}" _cfg)
    set(CMAKE_${_lang}_FLAGS_${_cfg} "")
  endforeach()
endforeach()
foreach(_kind EXE SHARED MODULE STATIC)
  set(CMAKE_${_kind}_LINKER_FLAGS "")
  foreach(_cfg ${FL_CONFIGS} MinSizeRel RelWithDebInfo)
    string(TOUPPER "${_cfg}" _cfg)
    set(CMAKE_${_kind}_LINKER_FLAGS_${_cfg} "")
  endforeach()
endforeach()

# The subsystem comes from fl_add_module (/SUBSYSTEM:WINDOWS or CONSOLE, as MSBuild writes it).
foreach(_lang C CXX)
  set(CMAKE_${_lang}_CREATE_WIN32_EXE "")
  set(CMAKE_${_lang}_CREATE_CONSOLE_EXE "")
endforeach()

# rc: MSBuild's ResourceCompile defaults (Culture 0x0409). CMake applies no target compile
# options to rc, so they go here.
set(CMAKE_RC_FLAGS "/l0x0409 /nologo")

# UACExecutionLevel AsInvoker: MSBuild writes it for DLLs as well as exes.
set(FL_MANIFESTUAC "/MANIFESTUAC:level='asInvoker' uiAccess='false'")

# The toolset's own directory (VC\Tools\MSVC\<version>): the DPI-awareness manifest of MFC
# projects and MSBuild's spelling of the MFC include directory.
get_filename_component(_fl_vctools "${CMAKE_CXX_COMPILER}/../../../.." ABSOLUTE)
set(FL_DPIAWARE_MANIFEST "${_fl_vctools}/include/manifest/dpiaware.manifest")

# RuntimeLibrary (Directory.Build.props): /MD, /MDd for Debug and DebugASan.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<${_fl_cfg_debuglibs}:Debug>DLL")
# /Zi or /ZI is an item setting here (see _fl_item_options), not a target one.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "")

# The DirectX 8.1 headers go after the Windows SDK, not before it (the SDK's basetsd.h must win
# over the DX8 one): MSBuild appends them to IncludePath, i.e. to the INCLUDE environment of cl
# (Directory.Build.targets, for every project). A /I option would search them first, so cl is
# started through `cmake -E env` with the same INCLUDE edit.
set(FL_DX8_INCLUDE_LAUNCHER
    "${CMAKE_COMMAND}" -E env "--modify" "INCLUDE=path_list_append:${FL_DX8_DIR}/include")
set(CMAKE_C_COMPILER_LAUNCHER ${FL_DX8_INCLUDE_LAUNCHER})
set(CMAKE_CXX_COMPILER_LAUNCHER ${FL_DX8_INCLUDE_LAUNCHER})

# ---------------------------------------------------------------------------------------------
# Compile settings that a .vcxproj item may override (ClCompile metadata). MSBuild resolves each
# one per source file: the item's value, else the project's ItemDefinitionGroup, else the
# default; one cl option comes out of it. Appending a per-file option to a project-wide one is
# not the same (/RTC1 with /O2 is an error, /GL cannot be taken back), so these options are set
# on every source file, never on the target. The names are the MSBuild metadata names.
# ---------------------------------------------------------------------------------------------
set(_fl_item_keys Optimization IntrinsicFunctions FloatingPointModel EnableEnhancedInstructionSet
                  BasicRuntimeChecks DebugInformationFormat SupportJustMyCode
                  WholeProgramOptimization SDLCheck)

# MSBuild's value for a configuration before any project or item setting: Microsoft.Cl.Common.props
# for v143, then Directory.Build.props (x87 on Win32, /sdl off) and WholeProgramOptimization=true
# in Release (every project sets it, except the ones with NO_WPO).
function(_fl_item_default out key cfg)
  set(_debuglibs TRUE)
  if(cfg STREQUAL "Release")
    set(_debuglibs FALSE)
  endif()
  if(key STREQUAL "Optimization")
    if(_debuglibs)
      set(_v Disabled)
    else()
      set(_v MaxSpeed)
    endif()
  elseif(key STREQUAL "IntrinsicFunctions")
    set(_v false)
  elseif(key STREQUAL "FloatingPointModel")
    set(_v Precise)
  elseif(key STREQUAL "EnableEnhancedInstructionSet")
    # Directory.Build.props: x87 (/arch:IA32) on Win32, as the VC6-built retail modules. Retail
    # leaves FPU_PRESERVE off (EXE\dacom.ini; rp8.dll reads the key at .text:06d06bb4), so
    # Direct3D 8 CreateDevice drops the main thread's x87 precision control to 24-bit and never
    # restores it. x87 code inherits that; SSE2 code ignores the control word and would keep
    # 53-bit doubles where retail has 24-bit. x64 has no x87 code generation: it always uses SSE2
    # (docs/x64.md), and gets no /arch option.
    if(FL_WIN32)
      set(_v NoExtensions)
    else()
      set(_v NotSet)
    endif()
  elseif(key STREQUAL "BasicRuntimeChecks")
    if(_debuglibs)
      set(_v EnableFastChecks)
    else()
      set(_v Default)
    endif()
  elseif(key STREQUAL "DebugInformationFormat")
    # /ZI with UseDebugLibraries; DebugASan's /Zi is set in _fl_finalize.
    if(_debuglibs)
      set(_v EditAndContinue)
    else()
      set(_v ProgramDatabase)
    endif()
  elseif(key STREQUAL "SupportJustMyCode")
    if(_debuglibs)
      set(_v true)
    else()
      set(_v false)
    endif()
  elseif(key STREQUAL "WholeProgramOptimization")
    if(_debuglibs)
      set(_v false)
    else()
      set(_v true)
    endif()
  elseif(key STREQUAL "SDLCheck")
    # Directory.Build.props: no /sdl, for every project. /sdl zero-fills the members of a class
    # allocated with new before its constructor runs and poisons pointers after delete, which
    # hides the uninitialised-member behaviour the VC6-built retail modules have, and gives
    # every dllexport class an exported __autoclassinit2 that retail does not export.
    set(_v false)
  endif()
  set(${out} "${_v}" PARENT_SCOPE)
endfunction()

function(_fl_item_option out key value)
  set(_map_Optimization_MaxSpeed /O2)
  set(_map_Optimization_Disabled /Od)
  set(_map_Optimization_MinSpace /O1)
  set(_map_Optimization_Full /Ox)
  set(_map_IntrinsicFunctions_true /Oi)
  set(_map_IntrinsicFunctions_false "")
  set(_map_FloatingPointModel_Precise /fp:precise)
  set(_map_FloatingPointModel_Fast /fp:fast)
  set(_map_FloatingPointModel_Strict /fp:strict)
  set(_map_EnableEnhancedInstructionSet_NotSet "")
  set(_map_EnableEnhancedInstructionSet_NoExtensions /arch:IA32)
  set(_map_EnableEnhancedInstructionSet_StreamingSIMDExtensions /arch:SSE)
  set(_map_EnableEnhancedInstructionSet_StreamingSIMDExtensions2 /arch:SSE2)
  set(_map_BasicRuntimeChecks_EnableFastChecks /RTC1)
  set(_map_BasicRuntimeChecks_Default "")
  set(_map_DebugInformationFormat_EditAndContinue /ZI)
  set(_map_DebugInformationFormat_ProgramDatabase /Zi)
  set(_map_DebugInformationFormat_OldStyle /Z7)
  set(_map_DebugInformationFormat_None "")
  set(_map_SupportJustMyCode_true /JMC)
  set(_map_SupportJustMyCode_false "")
  set(_map_WholeProgramOptimization_true /GL)
  set(_map_WholeProgramOptimization_false "")
  set(_map_SDLCheck_true /sdl)
  set(_map_SDLCheck_false /sdl-)
  if(NOT DEFINED _map_${key}_${value})
    message(FATAL_ERROR "No cl option for ${key}=${value}")
  endif()
  set(${out} "${_map_${key}_${value}}" PARENT_SCOPE)
endfunction()

# fl_item(<files...> [<Key> <Value>]... [INCLUDE_DIRECTORIES <dirs...>])
#   ClCompile metadata of single items, e.g.
#   fl_item(alchemy_math.cpp Optimization MaxSpeed BasicRuntimeChecks Default)
#   INCLUDE_DIRECTORIES is the item's AdditionalIncludeDirectories, searched before the
#   project's (MSBuild's "<dirs>;%(AdditionalIncludeDirectories)").
#   PrecompiledHeader NotUsing keeps a file of a PCH module off the precompiled header.
function(fl_item)
  set(_files)
  set(_args ${ARGN})
  while(_args)
    list(GET _args 0 _a)
    if(_a IN_LIST _fl_item_keys OR _a STREQUAL "INCLUDE_DIRECTORIES"
       OR _a STREQUAL "PrecompiledHeader")
      break()
    endif()
    list(APPEND _files "${_a}")
    list(REMOVE_AT _args 0)
  endwhile()
  while(_args)
    list(POP_FRONT _args _key)
    if(_key STREQUAL "INCLUDE_DIRECTORIES")
      set(_dirs)
      foreach(_dir IN LISTS _args)
        get_filename_component(_dir "${_dir}" ABSOLUTE)
        list(APPEND _dirs "${_dir}")
      endforeach()
      set_property(SOURCE ${_files} APPEND PROPERTY FL_INCLUDES ${_dirs})
      break()
    endif()
    list(POP_FRONT _args _value)
    if(_key STREQUAL "PrecompiledHeader")
      if(NOT _value STREQUAL "NotUsing")
        message(FATAL_ERROR "fl_item: PrecompiledHeader takes NotUsing only")
      endif()
    else()
      _fl_item_option(_check ${_key} "${_value}")
    endif()
    set_property(SOURCE ${_files} PROPERTY FL_${_key} "${_value}")
  endwhile()
endfunction()

# Applied once the module's CMakeLists.txt is done, so that fl_item() and fl_includes() calls
# after fl_add_module() count.
#
# Include directories are resolved per source file, as MSBuild resolves AdditionalInclude
# Directories per item: the item's own, then the project's, then src\include and src\IVP
# (Directory.Build.props), then the project's "%(AdditionalIncludeDirectories);<dir>" ones. They
# go to cl as /I options in that order (target include directories would come out in another
# order, and source ones after the target's).
#
# The forced includes are per source file too: fl_layout.h for every one (Directory.Build.props),
# and in front of it, in a PCH module, the precompiled header (Directory.Build.targets: fl_pch.h,
# built once by src\pch\fl_pch.cpp with /Yc and used by the other files with /Yu).
function(_fl_finalize name)
  get_target_property(_early ${name} FL_INCLUDES)
  get_target_property(_late ${name} FL_INCLUDES_LATE)
  foreach(_v _early _late)
    if(NOT ${_v})
      set(${_v})
    endif()
  endforeach()
  set(_common "${FL_SRC_DIR}/include" "${FL_SRC_DIR}/IVP")
  get_target_property(_pch ${name} FL_PCH)
  file(TO_NATIVE_PATH "${FL_SRC_DIR}/pch/fl_pch.h" _pch_header)
  set(_pch_file "${CMAKE_CURRENT_BINARY_DIR}/fl_pch.pch")
  file(TO_NATIVE_PATH "${_pch_file}" _pch_file_native)

  get_target_property(_sources ${name} SOURCES)
  foreach(_src IN LISTS _sources)
    if(NOT _src MATCHES "\\.(c|cpp)$")
      continue()
    endif()
    get_property(_item_dirs SOURCE "${_src}" PROPERTY FL_INCLUDES)
    set(_dirs ${_item_dirs} ${_early} ${_common} ${_late})
    list(REMOVE_DUPLICATES _dirs)
    set(_opts)
    foreach(_dir IN LISTS _dirs)
      file(TO_NATIVE_PATH "${_dir}" _dir)
      list(APPEND _opts "/I${_dir}")
    endforeach()
    get_property(_pch_item SOURCE "${_src}" PROPERTY FL_PrecompiledHeader)
    if(_pch AND NOT _pch_item STREQUAL "NotUsing")
      get_filename_component(_abs "${_src}" ABSOLUTE)
      if(_abs STREQUAL "${FL_SRC_DIR}/pch/fl_pch.cpp")
        list(APPEND _opts "/Yc${_pch_header}")
        set_property(SOURCE "${_src}" APPEND PROPERTY OBJECT_OUTPUTS "${_pch_file}")
      else()
        list(APPEND _opts "/Yu${_pch_header}")
        set_property(SOURCE "${_src}" APPEND PROPERTY OBJECT_DEPENDS "${_pch_file}")
      endif()
      list(APPEND _opts "/Fp${_pch_file_native}" "/FI${_pch_header}")
    endif()
    list(APPEND _opts /FIfl_layout.h)

    set(_per_cfg)
    foreach(_cfg IN LISTS FL_CONFIGS)
      set(_cfg_opts)
      foreach(_key IN LISTS _fl_item_keys)
        _fl_item_default(_v ${_key} ${_cfg})
        get_property(_t_set TARGET ${name} PROPERTY FL_${_key} SET)
        if(_t_set)
          get_property(_v TARGET ${name} PROPERTY FL_${_key})
        endif()
        # Directory.Build.targets, after the projects: DebugASan has no /RTC and /Zi.
        if(_cfg STREQUAL "DebugASan" AND _key STREQUAL "BasicRuntimeChecks")
          set(_v Default)
        elseif(_cfg STREQUAL "DebugASan" AND _key STREQUAL "DebugInformationFormat")
          set(_v ProgramDatabase)
        endif()
        get_property(_s_set SOURCE "${_src}" PROPERTY FL_${_key} SET)
        if(_s_set)
          get_property(_v SOURCE "${_src}" PROPERTY FL_${_key})
        endif()
        _fl_item_option(_o ${_key} "${_v}")
        list(APPEND _cfg_opts ${_o})
      endforeach()
      list(JOIN _cfg_opts "\;" _cfg_opts)
      list(APPEND _per_cfg "$<$<CONFIG:${_cfg}>:${_cfg_opts}>")
    endforeach()
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_OPTIONS ${_opts} ${_per_cfg})
  endforeach()
endfunction()

# ---------------------------------------------------------------------------------------------
# fl_add_module(<name> <DLL|EXE|LIB|RESOURCE_DLL> <RETAIL|PORT|TOOL>
#               [PORTABLE]                 also built on Linux and macOS (no Windows API)
#               [OUTPUT_NAME <name>]       TargetName, when it is not the project name
#               [NO_WPO]                   no <WholeProgramOptimization>true (Release /GL, /LTCG)
#               [UNICODE]                  <CharacterSet>Unicode (default MultiByte)
#               [MFC]                      <UseOfMfc>Dynamic
#               [CONSOLE]                  <SubSystem>Console (default Windows)
#               [NO_SAFESEH]               <ImageHasSafeExceptionHandlers>false
#               [NO_INCREMENTAL]           <LinkIncremental>false in every configuration
#               [EXPORTS]                  an exe that exports names (and so has an import library)
#               [WARNING_LEVEL <n>]        default 3
#               [DEF <file.def>]           ModuleDefinitionFile
#               [PCH]                      the precompiled header (src\pch\fl_pch.h, see there)
#               [ITEM_DEFAULTS <Key> <Value>...]  ClCompile ItemDefinitionGroup (see fl_item)
#               SOURCES <files...>)        compile items in the order of the old .vcxproj
#                                          (= link order = static-initialisation order)
#
# RETAIL / PORT / TOOL is the module's kind (was FlModuleKind): a rebuild of a retail module or
# resource DLL, whose Win32 output stays the retail reference; a port-only module; or a test or
# diagnostic tool. Retail modules must not use vcpkg packages (fl_vcpkg_package checks it).
# ---------------------------------------------------------------------------------------------
function(fl_add_module name kind)
  cmake_parse_arguments(PARSE_ARGV 2 A
    "RETAIL;PORT;TOOL;PORTABLE;NO_WPO;UNICODE;MFC;CONSOLE;NO_SAFESEH;NO_INCREMENTAL;EXPORTS;PCH"
    "OUTPUT_NAME;WARNING_LEVEL;DEF" "SOURCES;ITEM_DEFAULTS")
  if(A_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "fl_add_module(${name}): unknown arguments ${A_UNPARSED_ARGUMENTS}")
  endif()
  set(_kinds 0)
  foreach(_k RETAIL PORT TOOL)
    if(A_${_k})
      math(EXPR _kinds "${_kinds} + 1")
      set(_module_kind ${_k})
    endif()
  endforeach()
  if(NOT _kinds EQUAL 1)
    message(FATAL_ERROR "fl_add_module(${name}): give exactly one of RETAIL, PORT or TOOL")
  endif()
  if(NOT DEFINED A_WARNING_LEVEL)
    set(A_WARNING_LEVEL 3)
  endif()

  if(A_PCH)
    # Directory.Build.targets adds the /Yc item after the project's own: it links last.
    list(APPEND A_SOURCES "${FL_SRC_DIR}/pch/fl_pch.cpp")
  endif()
  if(kind STREQUAL "EXE")
    add_executable(${name} ${A_SOURCES})
    set_property(GLOBAL APPEND PROPERTY FL_EXE_TARGETS ${name})
  elseif(kind STREQUAL "DLL" OR kind STREQUAL "RESOURCE_DLL")
    add_library(${name} SHARED ${A_SOURCES})
  elseif(kind STREQUAL "LIB")
    add_library(${name} STATIC ${A_SOURCES})
  else()
    message(FATAL_ERROR "fl_add_module(${name}): unknown kind ${kind}")
  endif()
  set_property(TARGET ${name} PROPERTY FL_MODULE_KIND ${_module_kind})
  set_property(TARGET ${name} PROPERTY FL_PCH ${A_PCH})
  # CMake would define <name>_EXPORTS in a DLL or an exe with exports; MSBuild defines no such
  # symbol.
  set_property(TARGET ${name} PROPERTY DEFINE_SYMBOL "")
  if(A_OUTPUT_NAME)
    set_property(TARGET ${name} PROPERTY OUTPUT_NAME "${A_OUTPUT_NAME}")
  endif()

  # OutDir: bin\ (or the configuration's sibling) for the module, its import library and its
  # PDB, as MSBuild. Intermediate files stay in the CMake build tree.
  set_target_properties(${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FL_BIN_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${FL_BIN_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${FL_BIN_DIR}"
    PDB_OUTPUT_DIRECTORY "${FL_BIN_DIR}")

  if(kind STREQUAL "RESOURCE_DLL")
    _fl_resource_dll(${name})
    # ResourceDll.props: the imported .rc scripts are UTF-8; the host ANSI code page would
    # corrupt punctuation and non-ASCII names. rc takes no per-target options from CMake, so
    # this is the directory's CMAKE_RC_FLAGS (one resource DLL per directory).
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /c65001" PARENT_SCOPE)
    return()
  endif()

  # Project-wide ClCompile settings that items may override.
  if(A_NO_WPO)
    set_property(TARGET ${name} PROPERTY FL_WholeProgramOptimization false)
  endif()
  while(A_ITEM_DEFAULTS)
    list(POP_FRONT A_ITEM_DEFAULTS _key _value)
    _fl_item_option(_check ${_key} "${_value}")
    set_property(TARGET ${name} PROPERTY FL_${_key} "${_value}")
  endwhile()
  # DEFER evaluates its arguments when it runs: bake the name in now.
  cmake_language(EVAL CODE "cmake_language(DEFER CALL _fl_finalize [[${name}]])")

  # Preprocessor: Directory.Build.props, Directory.Build.targets and the implicit MSBuild ones
  # (_WINDLL for a DynamicLibrary, _MBCS / _UNICODE;UNICODE from CharacterSet, _AFXDLL from
  # UseOfMfc=Dynamic). No NDEBUG and no _WINDOWS: MSBuild adds neither.
  #
  # _ITERATOR_DEBUG_LEVEL=1 in Release: with any level above 0 a modern std container carries a
  # proxy pointer in the dword where VC6 STL kept its allocator, so std::string / vector / list /
  # map are retail-sized and the retail field offsets asserted across src/include hold. Level 0
  # drops that dword and shifts every field after a container by 4. Debug and DebugASan keep
  # the _DEBUG default (2), which has the same layout.
  set(_defs WIN32 _CRT_SECURE_NO_WARNINGS
            "FREELANCER_ENABLE_TICK_PROFILE=${FreelancerEnableTickProfile}"
            "$<${_fl_cfg_release}:_ITERATOR_DEBUG_LEVEL=1>" "$<${_fl_cfg_asan}:FL_ASAN>")
  if(kind STREQUAL "DLL")
    list(APPEND _defs _WINDLL)
  endif()
  if(A_UNICODE)
    list(APPEND _defs _UNICODE UNICODE)
  else()
    list(APPEND _defs _MBCS)
  endif()
  if(A_MFC)
    list(APPEND _defs _AFXDLL)
    set_property(TARGET ${name} PROPERTY FL_MFC TRUE)
    # MFC's inline headers record their own path (__FILE__, with /FC) in the binary. MSBuild's
    # include path spells the directory "atlmfc\include", vcvarsall's INCLUDE "ATLMFC\include":
    # put MSBuild's spelling first (it is second in MSBuild's list, after VC\include, and the
    # two directories share no header names).
    file(TO_NATIVE_PATH "${_fl_vctools}/atlmfc/include" _atlmfc)
    set_property(TARGET ${name} PROPERTY CXX_COMPILER_LAUNCHER
      "${CMAKE_COMMAND}" -E env
      "--modify" "INCLUDE=path_list_prepend:${_atlmfc}"
      "--modify" "INCLUDE=path_list_append:${FL_DX8_DIR}/include")
  endif()
  target_compile_definitions(${name} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${_defs}>")

  # The rest of the cl options MSBuild passes (the per-item ones come from _fl_finalize).
  # Directory.Build.props: /std:c++20 with /permissive (ConformanceMode false; /std:c++20 alone
  # implies /permissive-), fl_layout.h forced into every source (FL_LAYOUT_ASSERT, the retail
  # struct size and offset checks; per source, in _fl_finalize), C4251 / C4275 off (exported classes hold STL members and
  # derive from non-exported bases; their consumers do not need the dll-interface).
  set(_cl
    /W${A_WARNING_LEVEL} /WX- /diagnostics:column /Gm- /EHsc
    "$<${_fl_cfg_asan}:/fsanitize=address>"
    /GS /Zc:wchar_t /Zc:forScope /Zc:inline
    /std:c++20 /permissive
    /external:W${A_WARNING_LEVEL} /Gd /FC /wd4251 /wd4275)
  if(FL_WIN32)
    # MSBuild passes these two for Win32 only.
    list(APPEND _cl /Oy- /analyze-)
  endif()
  target_compile_options(${name} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${_cl}>")

  # rc gets no project defines or include dirs from MSBuild, only its own ResourceCompile
  # settings (CMAKE_RC_FLAGS above). MSBuild runs rc in the project directory, which is where a
  # script's relative file names (ICON "res\\x.ico") are found; Ninja runs it in the build tree,
  # so that directory is passed as /I instead.
  target_include_directories(${name} PRIVATE "$<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}>")

  set(_wpo "$<AND:${_fl_cfg_release},$<NOT:$<BOOL:${A_NO_WPO}>>>")
  if(kind STREQUAL "LIB")
    set_property(TARGET ${name} APPEND PROPERTY STATIC_LIBRARY_OPTIONS
      /MACHINE:${FL_MACHINE} "$<${_wpo}:/LTCG>" /NOLOGO)
    return()
  endif()

  if(A_CONSOLE)
    set(_subsystem CONSOLE)
  else()
    set(_subsystem WINDOWS)
  endif()
  # LinkIncremental: MSBuild writes /INCREMENTAL in Debug and nothing in Release (where /OPT:REF
  # turns incremental linking off anyway); DebugASan (Directory.Build.targets) and
  # LinkIncremental=false projects get /INCREMENTAL:NO.
  if(A_NO_INCREMENTAL)
    set(_incremental /INCREMENTAL:NO)
  else()
    set(_incremental "$<${_fl_cfg_debug}:/INCREMENTAL>" "$<${_fl_cfg_asan}:/INCREMENTAL:NO>")
  endif()
  # /SAFESEH: Win32 Release only (x64 has table-based exception handling).
  if(NOT FL_WIN32)
    set(_safeseh)
  elseif(A_NO_SAFESEH)
    set(_safeseh /SAFESEH:NO)
  else()
    set(_safeseh "$<${_fl_cfg_release}:/SAFESEH>")
  endif()
  target_link_options(${name} PRIVATE
    /MANIFEST "$<${_wpo}:/LTCG:incremental>" /NXCOMPAT /DYNAMICBASE /DEBUG /MACHINE:${FL_MACHINE}
    "$<${_wpo}:/OPT:REF;/OPT:ICF>" ${_safeseh} ${_incremental} /SUBSYSTEM:${_subsystem}
    "${FL_MANIFESTUAC}" /ERRORREPORT:PROMPT /NOLOGO /TLBID:1)
  if(A_MFC)
    # MSBuild gives an MFC application the toolset's DPI-awareness manifest.
    if(EXISTS "${FL_DPIAWARE_MANIFEST}")
      target_sources(${name} PRIVATE "${FL_DPIAWARE_MANIFEST}")
    else()
      message(WARNING "${FL_DPIAWARE_MANIFEST} not found: ${name} is built without it")
    endif()
  endif()

  if(A_DEF)
    get_filename_component(_def "${A_DEF}" ABSOLUTE)
    target_link_options(${name} PRIVATE "/DEF:${_def}")
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS "${_def}")
  endif()
  if(A_EXPORTS)
    # An exe with exports gets an import library next to it, as MSBuild.
    set_property(TARGET ${name} PROPERTY ENABLE_EXPORTS ON)
  endif()
endfunction()

# Resource-only DLLs: src\Resources\ResourceDll.props (no code, no CRT, no entry point, no PDB).
function(_fl_resource_dll name)
  set_property(TARGET ${name} PROPERTY LINKER_LANGUAGE CXX)
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  set(_safeseh)
  if(FL_WIN32)
    set(_safeseh "$<${_fl_cfg_release}:/SAFESEH>")
  endif()
  target_link_options(${name} PRIVATE
    /MANIFEST /NXCOMPAT /DYNAMICBASE /MACHINE:${FL_MACHINE} /NOENTRY /NODEFAULTLIB
    ${_safeseh} "$<${_fl_cfg_debug}:/INCREMENTAL>"
    /SUBSYSTEM:WINDOWS "${FL_MANIFESTUAC}" /ERRORREPORT:PROMPT /NOLOGO /TLBID:1)
  target_link_libraries(${name} PRIVATE ${FL_DEFAULT_LIBS})
  target_link_directories(${name} PRIVATE "${FL_BIN_DIR}")
endfunction()

# The CoreLibraryDependencies MSBuild puts in place of %(AdditionalDependencies).
set(FL_DEFAULT_LIBS kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib
                    shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib)

# fl_link(<name> [LIBS <AdditionalDependencies...>] [REFERENCES <projects...>]
#                [LIBDIRS <AdditionalLibraryDirectories...>])
#
# LIBS is the project's AdditionalDependencies before %(AdditionalDependencies), in order: an
# entry that names a module (dacom.lib, Common.lib, ...) links that module's import library,
# anything else is a system or SDK library. MSBuild then adds the default libraries and the
# import library of every ProjectReference not already named; REFERENCES lists the project
# references.
#
# LIBDIRS is the project's AdditionalLibraryDirectories before %(AdditionalLibraryDirectories);
# the output directory (Directory.Build.props) is searched after them. The order decides
# which copy of a library two directories hold is linked (dsound.lib: DirectX 8.1 SDK or
# Windows SDK).
#
# The link order of libraries decides which one resolves a symbol two of them define, so it is
# kept exactly: module libraries go in as files ($<TARGET_LINKER_FILE>), which CMake leaves in
# place, where linking the targets would let CMake reorder them by dependency. The build order
# comes from add_dependencies, the relink from LINK_DEPENDS.
function(fl_link name)
  cmake_parse_arguments(PARSE_ARGV 1 A "" "" "LIBS;REFERENCES;LIBDIRS")
  set(_items)
  foreach(_lib IN LISTS A_LIBS)
    get_filename_component(_base "${_lib}" NAME_WE)
    if(NOT IS_ABSOLUTE "${_lib}" AND TARGET "${_base}")
      list(APPEND _items "@${_base}")
    else()
      list(APPEND _items "${_lib}")
    endif()
  endforeach()
  # An MFC project gets no default libraries from MSBuild: the MFC headers name the ones they
  # need (#pragma comment(lib)), in their own order.
  get_target_property(_mfc ${name} FL_MFC)
  if(NOT _mfc)
    list(APPEND _items ${FL_DEFAULT_LIBS})
  endif()
  foreach(_ref IN LISTS A_REFERENCES)
    if(NOT TARGET "${_ref}")
      message(FATAL_ERROR "fl_link(${name}): ${_ref} is not a module")
    endif()
    list(APPEND _items "@${_ref}")
  endforeach()
  list(REMOVE_DUPLICATES _items)
  set(_libs)
  foreach(_item IN LISTS _items)
    if(_item MATCHES "^@(.*)$")
      set(_target "${CMAKE_MATCH_1}")
      list(APPEND _libs "$<TARGET_LINKER_FILE:${_target}>")
      add_dependencies(${name} ${_target})
      set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS "$<TARGET_LINKER_FILE:${_target}>")
    else()
      list(APPEND _libs "${_item}")
    endif()
  endforeach()
  target_link_libraries(${name} PRIVATE ${_libs})

  set(_dirs)
  foreach(_dir IN LISTS A_LIBDIRS)
    if(NOT _dir MATCHES "^\\$<")
      get_filename_component(_dir "${_dir}" ABSOLUTE)
    endif()
    list(APPEND _dirs "${_dir}")
  endforeach()
  # AdditionalLibraryDirectories: $(OutDir) for every project (Directory.Build.props).
  target_link_directories(${name} PRIVATE ${_dirs} "${FL_BIN_DIR}")
endfunction()

# A ProjectReference with LinkLibraryDependencies=false: build order only.
function(fl_order_after name)
  add_dependencies(${name} ${ARGN})
endfunction()

# fl_defines(<name> <defines...>): the project's ClCompile PreprocessorDefinitions. They reach
# cl only: MSBuild's rc gets its own ResourceCompile settings.
function(fl_defines name)
  target_compile_definitions(${name} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${ARGN}>")
endfunction()

# fl_includes(<name> [AFTER_COMMON] <dirs...>): the project's ClCompile
# AdditionalIncludeDirectories, searched before src\include and src\IVP
# ("<dirs>;%(AdditionalIncludeDirectories)"), or after them with AFTER_COMMON
# ("%(AdditionalIncludeDirectories);<dirs>"). cl only, as fl_defines.
function(fl_includes name)
  cmake_parse_arguments(PARSE_ARGV 1 A "AFTER_COMMON" "" "")
  set(_dirs)
  foreach(_dir IN LISTS A_UNPARSED_ARGUMENTS)
    get_filename_component(_dir "${_dir}" ABSOLUTE)
    list(APPEND _dirs "${_dir}")
  endforeach()
  if(A_AFTER_COMMON)
    set_property(TARGET ${name} APPEND PROPERTY FL_INCLUDES_LATE ${_dirs})
  else()
    set_property(TARGET ${name} APPEND PROPERTY FL_INCLUDES ${_dirs})
  endif()
endfunction()

# fl_cl_options(<name> <options...>): cl-only options of the project (DisableSpecificWarnings,
# AdditionalOptions such as /utf-8). Off Windows they are left out.
function(fl_cl_options name)
  target_compile_options(${name} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${ARGN}>")
endfunction()

# fl_rc(<name> [INCLUDES <dirs...>] [DEFINES <defines...>]): the project's ResourceCompile
# AdditionalIncludeDirectories / PreprocessorDefinitions.
function(fl_rc name)
  cmake_parse_arguments(PARSE_ARGV 1 A "" "" "INCLUDES;DEFINES")
  set(_dirs)
  foreach(_dir IN LISTS A_INCLUDES)
    get_filename_component(_dir "${_dir}" ABSOLUTE)
    list(APPEND _dirs "${_dir}")
  endforeach()
  target_include_directories(${name} PRIVATE "$<$<COMPILE_LANGUAGE:RC>:${_dirs}>")
  target_compile_definitions(${name} PRIVATE "$<$<COMPILE_LANGUAGE:RC>:${A_DEFINES}>")
endfunction()

# The include directories an imported target hands on, through the targets it links
# (SDL3::SDL3 is a wrapper around SDL3::SDL3-shared, which holds them).
function(_fl_package_includes out target)
  set(_dirs ${${out}})
  get_target_property(_aliased ${target} ALIASED_TARGET)
  if(_aliased)
    set(target ${_aliased})
  endif()
  get_target_property(_own ${target} INTERFACE_INCLUDE_DIRECTORIES)
  if(_own)
    list(APPEND _dirs ${_own})
  endif()
  get_target_property(_links ${target} INTERFACE_LINK_LIBRARIES)
  foreach(_link IN LISTS _links)
    if(TARGET "${_link}")
      _fl_package_includes(_dirs "${_link}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _dirs)
  set(${out} ${_dirs} PARENT_SCOPE)
endfunction()

# fl_vcpkg_package(<name> <package> <targets...>): link a port-only module to a package of the
# shared vcpkg manifest (src/vcpkg.json; the root CMakeLists.txt installs it). Returns FALSE in
# <name>_VCPKG_OK, with a warning or (FreelancerRequireVcpkg=ON) an error, when vcpkg is not
# set up; the module then skips itself.
function(fl_vcpkg_package name package)
  get_target_property(_kind ${name} FL_MODULE_KIND)
  if(_kind STREQUAL "RETAIL")
    message(FATAL_ERROR "${name}: a retail module must not use vcpkg packages; they are for "
                        "port-only modules and tools.")
  endif()
  find_package(${package} CONFIG)
  if(NOT ${package}_FOUND)
    set(_msg "${name}: ${package} not found (vcpkg: set VCPKG_ROOT, see docs/building.md)")
    if(FreelancerRequireVcpkg)
      message(FATAL_ERROR "${_msg}, and FreelancerRequireVcpkg=ON.")
    endif()
    message(WARNING "${_msg}; ${name} is not built.")
    set(${name}_VCPKG_OK FALSE PARENT_SCOPE)
    return()
  endif()
  # The package's headers go after every other include directory, where vcpkg's MSBuild
  # integration put them (an imported target's would come first).
  foreach(_target IN LISTS ARGN)
    set(_dirs)
    _fl_package_includes(_dirs ${_target})
    set_property(TARGET ${name} APPEND PROPERTY FL_INCLUDES_LATE ${_dirs})
    target_link_libraries(${name} PRIVATE "$<LINK_ONLY:${_target}>")
  endforeach()
  set(${name}_VCPKG_OK TRUE PARENT_SCOPE)
endfunction()

# Every output directory except bin\ (bin_debug\, binasan\, bin_x64\, bin_x64_debug\) gets the
# dev *.ini and character files of bin\, with the absolute bin\ paths rewritten
# (src/copy_dev_ini.py). A target of its own that runs on every build, as MSBuild's step after
# each build did, so an edit to a file in bin\ reaches the copies without relinking anything.
function(fl_copy_dev_ini)
  find_package(Python3 COMPONENTS Interpreter)
  if(NOT Python3_Interpreter_FOUND)
    message(WARNING "Python 3 not found: the dev *.ini files are not copied into bin_*\\")
    return()
  endif()
  if(FL_WIN32)
    set(_cond "$<NOT:${_fl_cfg_release}>")
  else()
    set(_cond 1)
  endif()
  add_custom_target(dev_ini ALL
    COMMAND "$<IF:${_cond},${Python3_EXECUTABLE};${FL_SRC_DIR}/copy_dev_ini.py;${FL_BIN_DIR},${CMAKE_COMMAND};-E;true>"
    COMMAND_EXPAND_LISTS VERBATIM)
  # Building one program (--target freelancer) refreshes the copies too.
  get_property(_exes GLOBAL PROPERTY FL_EXE_TARGETS)
  foreach(_exe IN LISTS _exes)
    add_dependencies(${_exe} dev_ini)
  endforeach()
endfunction()

# DebugASan: the AddressSanitizer runtime DLLs next to the client (Directory.Build.targets).
function(fl_copy_asan_runtime name)
  if(NOT FL_WIN32)
    return()
  endif()
  get_filename_component(_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  file(GLOB _asan_rt "${_cl_dir}/clang_rt.asan*_dynamic-i386.dll")
  if(NOT _asan_rt)
    message(STATUS "AddressSanitizer runtime DLLs not found next to ${CMAKE_CXX_COMPILER}")
    return()
  endif()
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND "$<IF:${_fl_cfg_asan},${CMAKE_COMMAND};-E;copy_if_different;${_asan_rt};$<TARGET_FILE_DIR:${name}>,${CMAKE_COMMAND};-E;true>"
    COMMAND_EXPAND_LISTS VERBATIM)
endfunction()
