/*
** $Id: lauxlib.h,v 1.12 1999/03/10 14:19:41 roberto Exp $
** Auxiliary functions for building Lua libraries
** See Copyright Notice in lua.h
*/


#ifndef auxlib_h
#define auxlib_h


#include "lua.h"
#include <string.h>


struct luaL_reg {
  char *name;
  lua_CFunction func;
};


#define luaL_arg_check(cond,numarg,extramsg) if (!(cond)) \
                                               luaL_argerror(numarg,extramsg)

void luaL_openlib (struct luaL_reg *l, int n);
void luaL_argerror (int numarg, char *extramsg);
#define luaL_check_string(n)  (luaL_check_lstr((n), NULL))
char *luaL_check_lstr (int numArg, long *len);
#define luaL_opt_string(n, d) (luaL_opt_lstr((n), (d), NULL))
char *luaL_opt_lstr (int numArg, char *def, long *len);
double luaL_check_number (int numArg);
/* Retail MSVCRT _ftol converts to a chopped signed qword; Lua callers use EAX. */
static int LuaAuxRetailInt (double Value) {
  const __int64 Converted = (__int64)Value;
  const unsigned int Word = (unsigned int)Converted;
  int Result;
  memcpy(&Result, &Word, sizeof(Result));
  return Result;
}
#define luaL_check_int(n)	LuaAuxRetailInt(luaL_check_number(n))
#define luaL_check_long(n)	((long)LuaAuxRetailInt(luaL_check_number(n)))
double luaL_opt_number (int numArg, double def);
#define luaL_opt_int(n,d)	LuaAuxRetailInt(luaL_opt_number(n,d))
#define luaL_opt_long(n,d)	((long)LuaAuxRetailInt(luaL_opt_number(n,d)))
lua_Object luaL_functionarg (int arg);
lua_Object luaL_tablearg (int arg);
lua_Object luaL_nonnullarg (int numArg);
void luaL_verror (char *fmt, ...);
char *luaL_openspace (int size);
void luaL_resetbuffer (void);
void luaL_addchar (int c);
int luaL_getsize (void);
void luaL_addsize (int n);
int luaL_newbuffer (int size);
void luaL_oldbuffer (int old);
char *luaL_buffer (void);
int luaL_findstring (char *name, char *list[]);
void luaL_chunkid (char *out, char *source, int len);
void luaL_filesource (char *out, char *filename, int len);


#endif
