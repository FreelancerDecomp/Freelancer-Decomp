# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(luaprofile DLL RETAIL
  SOURCES
    luaprofile.cpp
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
    lua32/src/lparser.c
    lua32/src/lstate.c
    lua32/src/lstring.c
    lua32/src/ltable.c
    lua32/src/ltm.c
    lua32/src/lundump.c
    lua32/src/lvm.c
    lua32/src/lzio.c
    lua32/lua32_layout.c
    luaprofile.rc
)

fl_includes(luaprofile lua32/include lua32/src)
fl_cl_options(luaprofile /wd4996)
# The Lua 3.2 sources (every .c item): C, with their own warnings off.
get_target_property(_lua luaprofile SOURCES)
list(FILTER _lua INCLUDE REGEX "\\.c$")
set_property(SOURCE ${_lua} APPEND PROPERTY COMPILE_OPTIONS
  /wd4013 /wd4101 /wd4189 /wd4244 /wd4267
  /wd4311 /wd4312 /wd4057 /wd4131 /wd4127 /wd4456 /wd4701 /wd4702 /wd4706 /wd4996)
fl_link(luaprofile
  LIBS dacom.lib
  REFERENCES dacom)
