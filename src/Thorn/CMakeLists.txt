# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(Thorn DLL RETAIL
  SOURCES
    ThornModernAdapters.cpp
    Thorn.cpp
    ThornScene.cpp
    # Keep the four scalar REVERB arguments as raw word transfers.
    ThornControlCalls.cpp
    ParamCurve.cpp
    SplinePath.cpp
    ThornLua.cpp
    lua32/src/lapi.c
    lua32/src/lauxlib.c
    lua32/src/lbuffer.c
    lua32/src/lbuiltin.c
    lua32/src/ldo.c
    lua32/src/lfunc.c
    lua32/src/lgc.c
    lua32/src/llex.c
    lua32/src/lmem.c
    lua32/src/lobject.c
    lua32/src/lstate.c
    lua32/src/lstring.c
    lua32/src/ltable.c
    lua32/src/ltm.c
    lua32/src/lundump.c
    lua32/src/lvm.c
    lua32/src/lzio.c
    lua32/src/lib/lmathlib.c
    lua32/src/lib/liolib.c
    lua32/src/lib/lstrlib.c
    lua32/lparser_stub.c
    Thorn.rc
)

# Keep the four scalar REVERB arguments as raw word transfers (SSE2 on Win32 only; x64 always
# uses SSE2).
fl_item(ThornControlCalls.cpp
  Optimization MaxSpeed BasicRuntimeChecks Default DebugInformationFormat ProgramDatabase
  WholeProgramOptimization false)
if(FL_WIN32)
  fl_item(ThornControlCalls.cpp EnableEnhancedInstructionSet StreamingSIMDExtensions2)
endif()
fl_item(ThornLua.cpp INCLUDE_DIRECTORIES lua32/include)
# The Lua 3.2 sources (every .c item): C, with their own warnings off.
get_target_property(_lua Thorn SOURCES)
list(FILTER _lua INCLUDE REGEX "\\.c$")
fl_item(${_lua} INCLUDE_DIRECTORIES lua32/include lua32/src)
set_property(SOURCE ${_lua} APPEND PROPERTY COMPILE_OPTIONS
  /wd4013 /wd4101 /wd4189 /wd4244 /wd4267
  /wd4311 /wd4312 /wd4057 /wd4131 /wd4127 /wd4456 /wd4701 /wd4702 /wd4706 /wd4996)

fl_link(Thorn
  LIBS dacom.lib
  REFERENCES dacom)
