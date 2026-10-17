/*
** $Id: ltm.c,v 1.25 1999/05/21 19:41:49 roberto Exp $
** Tag methods
** See Copyright Notice in lua.h
*/


#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltm.h"


char *luaT_eventname[] = {  /* ORDER IM */
  "gettable", "settable", "index", "getglobal", "setglobal", "add",
  "sub", "mul", "div", "pow", "unm", "lt", "le", "gt", "ge",
  "concat", "gc", "function", NULL
};


static int luaI_checkevent (char *name, char *list[]) {
  int e = luaL_findstring(name, list);
  if (e < 0)
    luaL_verror("`%.50s' is not a valid event name", name);
  return e;
}



/* events in LUA_T_NIL are all allowed, since this is used as a
*  'placeholder' for "default" fallbacks
*/
static char luaT_validevents[NUM_TAGS][IM_N] = { /* ORDER LUA_T, ORDER IM */
{1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1},  /* LUA_T_USERDATA */
{1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1},  /* LUA_T_NUMBER */
{1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},  /* LUA_T_STRING */
{0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  /* LUA_T_ARRAY */
{1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},  /* LUA_T_PROTO */
{1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},  /* LUA_T_CPROTO */
{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}   /* LUA_T_NIL */
};

/* Retail FUN_06f4e1e0: custom tags accept every event; predefined tags use
** the private validity matrix.  Keep the matrix as data owned by this Lua
** VM rather than reproducing DAT_06f60694 or its pointer arithmetic. */
static int IsTagEventAllowed (int t, int e) {  /* ORDER LUA_T */
  return (t < LUA_T_NIL) ?  1 : luaT_validevents[-t][e];
}


/*
** Modern Lua owner for retail FUN_06f4e0a0 (the 18-word tag-method row).
** The row is private to this Lua 3.2 VM; no retail pointer aggregate or
** VC6 container layout is exposed to Thorn's C++ integration.
*/
static void InitializeTagMethodRow (int tag) {
  int i;
  for (i=0; i<IM_N; i++)
    ttype(luaT_getim(tag, i)) = LUA_T_NIL;
}


void luaT_init (void) {
  int t;
  /* Retail FUN_06f4e040 owns the initial seven rows in Lua's private
  ** IMtable.  luaM_growvector is the current-CRT allocator adapter; its
  ** storage never crosses the Thorn DLL boundary. */
  L->last_tag = -(NUM_TAGS-1);
  luaM_growvector(L->IMtable, 0, NUM_TAGS, struct IM, arrEM, MAX_INT);
  for (t=L->last_tag; t<=0; t++)
    InitializeTagMethodRow(t);
}


int lua_newtag (void) {
  --L->last_tag;
  luaM_growvector(L->IMtable, -(L->last_tag), 1, struct IM, arrEM, MAX_INT);
  InitializeTagMethodRow(L->last_tag);
  return L->last_tag;
}


static void checktag (int tag) {
  if (!(L->last_tag <= tag && tag <= 0))
    luaL_verror("%d is not a valid tag", tag);
}

void luaT_realtag (int tag) {
  if (!(L->last_tag <= tag && tag < LUA_T_NIL))
    luaL_verror("tag %d was not created by `newtag'", tag);
}


/* Modern owner for retail FUN_06f4e160.  The rows are copied as TObject
** values inside Lua's private IMtable; this preserves the four-word value
** copy without exposing the retail 0x10-byte storage ABI. */
static int CopyTagMethodRows (int tagto, int tagfrom) {
  int e;
  checktag(tagto);
  checktag(tagfrom);
  for (e=0; e<IM_N; e++) {
    if (IsTagEventAllowed(tagto, e))
      *luaT_getim(tagto, e) = *luaT_getim(tagfrom, e);
  }
  return tagto;
}


int lua_copytagmethods (int tagto, int tagfrom) {
  return CopyTagMethodRows(tagto, tagfrom);
}


/* Modern value decoder for retail FUN_06f4e240.  TObject remains a private
** Lua VM value; callers receive only the semantic effective tag. */
static int DecodeEffectiveTag (TObject *o) {
  int t;
  switch (t = ttype(o)) {
    case LUA_T_ARRAY:
      return o->value.a->htag;
    case LUA_T_USERDATA: {
      int tag = o->value.ts->u.d.tag;
      return (tag >= 0) ? LUA_T_USERDATA : tag;
    }
    case LUA_T_CLOSURE:
      return o->value.cl->consts[0].ttype;
#ifdef DEBUG
    case LUA_T_PMARK: case LUA_T_CMARK:
    case LUA_T_CLMARK: case LUA_T_LINE:
      LUA_INTERNALERROR("invalid type");
#endif
    default:
      return t;
  }
}


int luaT_effectivetag (TObject *o) {
  return DecodeEffectiveTag(o);
}


/* Modern owner for retail FUN_06f4e280.  Name lookup and the validity
** decision stay in the private Lua tag table; the nil object is a stable
** VM-owned fallback rather than a retail DAT pointer. */
static TObject *FindTagMethod (int t, char *event) {
  int e = luaI_checkevent(event, luaT_eventname);
  checktag(t);
  if (IsTagEventAllowed(t, e))
    return luaT_getim(t,e);
  else
    return &luaO_nilobject;
}


TObject *luaT_gettagmethod (int t, char *event) {
  return FindTagMethod(t, event);
}


void luaT_settagmethod (int t, char *event, TObject *func) {
  TObject temp;
  int e = luaI_checkevent(event, luaT_eventname);
  checktag(t);
  if (!IsTagEventAllowed(t, e))
    luaL_verror("cannot change tag method `%.20s' for type `%.20s'%.20s",
                luaT_eventname[e], luaO_typenames[-t],
                (t == LUA_T_ARRAY || t == LUA_T_USERDATA) ? " with default tag"
                                                          : "");
  /* Retail LAB_06f4e343 swaps all four value words.  Copying TObject by
  ** value gives the same operation while keeping the Lua VM's row storage
  ** private to this build. */
  temp = *func;
  *func = *luaT_getim(t,e);
  *luaT_getim(t, e) = temp;
}


/* Modern owner for retail FUN_06f4e3d0: walk event columns first, then all
** predefined/custom tag rows, and return the first matching event name. */
static char *TraverseTagMethods (int (*fn)(TObject *)) {  /* ORDER IM */
  int e;
  for (e=IM_GETTABLE; e<=IM_FUNCTION; e++) {
    int t;
    for (t=0; t>=L->last_tag; t--)
      if (fn(luaT_getim(t,e)))
        return luaT_eventname[e];
  }
  return NULL;
}


char *luaT_travtagmethods (int (*fn)(TObject *)) {
  return TraverseTagMethods(fn);
}


/*
* ===================================================================
* compatibility with old fallback system
*/
#ifdef LUA_COMPAT2_5

#include "lapi.h"
#include "lstring.h"

static void errorFB (void)
{
  lua_Object o = lua_getparam(1);
  if (lua_isstring(o))
    fprintf(stderr, "lua: %s\n", lua_getstring(o));
  else
    fprintf(stderr, "lua: unknown error\n");
}


static void nilFB (void) { }


static void typeFB (void) {
  lua_error("unexpected type");
}


static void fillvalids (IMS e, TObject *func) {
  int t;
  for (t=LUA_T_NIL; t<=LUA_T_USERDATA; t++)
    if (IsTagEventAllowed(t, e))
      *luaT_getim(t, e) = *func;
}


void luaT_setfallback (void) {
  static char *oldnames [] = {"error", "getglobal", "arith", "order", NULL};
  TObject oldfunc;
  lua_CFunction replace;
  char *name = luaL_check_string(1);
  lua_Object func = lua_getparam(2);
  luaL_arg_check(lua_isfunction(func), 2, "function expected");
  switch (luaL_findstring(name, oldnames)) {
    case 0: {  /* old error fallback */
      TObject *em = &(luaS_new("_ERRORMESSAGE")->u.s.globalval);
      oldfunc = *em;
      *em = *luaA_Address(func);
      replace = errorFB;
      break;
    }
    case 1:  /* old getglobal fallback */
      oldfunc = *luaT_getim(LUA_T_NIL, IM_GETGLOBAL);
      *luaT_getim(LUA_T_NIL, IM_GETGLOBAL) = *luaA_Address(func);
      replace = nilFB;
      break;
    case 2: {  /* old arith fallback */
      int i;
      oldfunc = *luaT_getim(LUA_T_NUMBER, IM_POW);
      for (i=IM_ADD; i<=IM_UNM; i++)  /* ORDER IM */
        fillvalids(i, luaA_Address(func));
      replace = typeFB;
      break;
    }
    case 3: {  /* old order fallback */
      int i;
      oldfunc = *luaT_getim(LUA_T_NIL, IM_LT);
      for (i=IM_LT; i<=IM_GE; i++)  /* ORDER IM */
        fillvalids(i, luaA_Address(func));
      replace = typeFB;
      break;
    }
    default: {
      int e;
      if ((e = luaL_findstring(name, luaT_eventname)) >= 0) {
        oldfunc = *luaT_getim(LUA_T_NIL, e);
        fillvalids(e, luaA_Address(func));
        replace = (e == IM_GC || e == IM_INDEX) ? nilFB : typeFB;
      }
      else {
        luaL_verror("`%.50s' is not a valid fallback name", name);
        replace = NULL;  /* to avoid warnings */
      }
    }
  }
  if (oldfunc.ttype != LUA_T_NIL)
    luaA_pushobject(&oldfunc);
  else
    lua_pushcfunction(replace);
}
#endif

