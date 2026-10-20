/*
** $Id: lobject.c,v 1.19 1999/04/13 19:28:49 roberto Exp $
** Some generic functions over Lua objects
** See Copyright Notice in lua.h
*/

#include <ctype.h>
#include <stdlib.h>

#include "lobject.h"
#include "lua.h"


char *luaO_typenames[] = { /* ORDER LUA_T */
    "userdata", "number", "string", "table", "function", "function",
    "nil", "function", "mark", "mark", "mark", "line", NULL
};


TObject luaO_nilobject = {LUA_T_NIL, {NULL}};



/* hash dimensions values */
static long dimensions[] =
 {5L, 11L, 23L, 47L, 97L, 197L, 397L, 797L, 1597L, 3203L, 6421L,
  12853L, 25717L, 51437L, 102811L, 205619L, 411233L, 822433L,
  1644817L, 3289613L, 6579211L, 13158023L, MAX_INT};


int luaO_redimension (int oldsize)
{
  int i;
  for (i=0; dimensions[i]<MAX_INT; i++) {
    if (dimensions[i] > oldsize)
      return dimensions[i];
  }
  lua_error("tableEM");
  return 0;  /* to avoid warnings */
}


int luaO_equalval (TObject *t1, TObject *t2) {
  switch (ttype(t1)) {
    case LUA_T_NIL: return 1;
    case LUA_T_NUMBER: {
      double Number1 = nvalue(t1);
      double Number2 = nvalue(t2);
      /* Retail TEST AH,40 / JZ rejects ordered unequal only. */
      return !(Number1 < Number2) && !(Number1 > Number2);
    }
    case LUA_T_STRING: case LUA_T_USERDATA: return svalue(t1) == svalue(t2);
    case LUA_T_ARRAY: return avalue(t1) == avalue(t2);
    case LUA_T_PROTO: return tfvalue(t1)  == tfvalue(t2);
    case LUA_T_CPROTO: return fvalue(t1)  == fvalue(t2);
    case LUA_T_CLOSURE: return t1->value.cl == t2->value.cl;
    default:
     LUA_INTERNALERROR("invalid type");
     return 0; /* UNREACHABLE */
  }
}


void luaO_insertlist (GCnode *root, GCnode *node)
{
  node->next = root->next;
  root->next = node;
  node->marked = 0;
}


#ifdef OLD_ANSI
void luaO_memup (void *dest, void *src, int size) {
  while (size--)
    ((char *)dest)[size]=((char *)src)[size];
}

void luaO_memdown (void *dest, void *src, int size) {
  int i;
  for (i=0; i<size; i++)
    ((char *)dest)[i]=((char *)src)[i];
}
#endif



/* These numeric boundaries retain retail's live x87 extended results.  /Od
** spills them to double even with IA32 enabled; keep optimization local. */
#pragma runtime_checks("", off)
#pragma optimize("gt", on)

static __declspec(noinline) double Expten (unsigned int Exponent) {
  double Base = 10.0;
  double Result = 1.0;
  for (; Exponent; Exponent>>=1) {
    if (Exponent & 1) Result *= Base;
    Base *= Base;  /* Retail also squares after the last exponent bit. */
  }
  return Result;
}


__declspec(noinline) double luaO_str2d (char *Text) {  /* LUA_NUMBER */
  double Mantissa = 0.0;
  unsigned int PointBits = 0;
  while (isdigit((unsigned char)*Text)) {
    Mantissa = 10.0*Mantissa + (*(Text++)-'0');
  }
  if (*Text == '.') {
    Text++;
    while (isdigit((unsigned char)*Text)) {
      Mantissa = 10.0*Mantissa + (*(Text++)-'0');
      PointBits++;
    }
  }
  if (toupper((unsigned char)*Text) == 'E') {
    unsigned int Exponent = 0;
    int bNegativeExponent = 0;
    Text++;
    if (*Text == '-') {
      Text++;
      bNegativeExponent = 1;
    }
    else if (*Text == '+') Text++;
    if (!isdigit((unsigned char)*Text)) return -1.0;
    do {
      Exponent = 10u*Exponent + (unsigned int)(*(Text++)-'0');
    } while (isdigit((unsigned char)*Text));
    /* Retail accumulates and adjusts in wrapping 32-bit registers. */
    PointBits -= bNegativeExponent ? 0u-Exponent : Exponent;
  }
  while (isspace((unsigned char)*Text)) Text++;
  if (*Text != '\0') return -1.0;
  if ((int)PointBits > 0)
    return Mantissa / Expten(PointBits);
  if ((int)PointBits < 0)
    return Mantissa * Expten(0u-PointBits);
  return Mantissa;
}


__declspec(noinline) int LuaParseStoredNumber (char *Text, double *Number) {
  double Parsed = luaO_str2d(Text);
  *Number = Parsed;
  /* Retail FST QWORD precedes FCOMP of the still-live extended value. */
  return Parsed >= 0.0;
}


__declspec(noinline) int LuaParseSignedNumberObject (char *Text, int Sign,
                                                  TObject *Object) {
  double Parsed = luaO_str2d(Text);
  if (!(Parsed >= 0.0)) return 2;
  ttype(Object) = LUA_T_NUMBER;
  nvalue(Object) = Parsed*Sign;
  /* Portable C follows the runtime's exception-delivery rules; no x87-only
  ** waiting instruction is needed for the parsed value or object result. */
  return 0;
}

#pragma optimize("", on)
#pragma runtime_checks("", restore)

