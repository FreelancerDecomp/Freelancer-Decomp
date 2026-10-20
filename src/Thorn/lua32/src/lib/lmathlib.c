/*
** $Id: lmathlib.c,v 1.17 1999/07/07 17:54:08 roberto Exp $
** Lua standard mathematical library
** See Copyright Notice in lua.h
*/


#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "PortableMath.h"


#undef PI
#define PI	(3.14159265358979323846)
#define RADIANS_PER_DEGREE	(PI/180.0)



/*
** If you want Lua to operate in radians (instead of degrees),
** define RADIANS
*/
#ifdef RADIANS
#define FROMRAD(a)	(a)
#define TORAD(a)	(a)
#else
#define FROMRAD(a)	((a)/RADIANS_PER_DEGREE)
#define TORAD(a)	((a)*RADIANS_PER_DEGREE)
#endif


static void math_abs (void) {
  lua_pushnumber(fabs(luaL_check_number(1)));
}

static void math_sin (void) {
  const double Input = luaL_check_number(1);
  lua_pushnumber(ThornLuaSin(Input));
}

static void math_cos (void) {
  const double Input = luaL_check_number(1);
  lua_pushnumber(ThornLuaCos(Input));
}

static void math_tan (void) {
  const double Input = luaL_check_number(1);
  lua_pushnumber(ThornLuaTan(Input));
}

static void math_asin (void) {
  const unsigned long long ScaleBits = 0x404ca5dc1a63c1f8ULL;
  double Scale;
  memcpy(&Scale, &ScaleBits, sizeof(Scale));
  lua_pushnumber(asin(luaL_check_number(1)) * Scale);
}

static void math_acos (void) {
  const unsigned long long ScaleBits = 0x404ca5dc1a63c1f8ULL;
  double Scale;
  memcpy(&Scale, &ScaleBits, sizeof(Scale));
  lua_pushnumber(acos(luaL_check_number(1)) * Scale);
}

/* Retail address binding: LAB_06f565e0 -> math_atan.
** FPATAN with FLD1 is atan(argument), then converted from radians. */
static void math_atan (void) {
  const double Input = luaL_check_number(1);
  lua_pushnumber(ThornLuaAtan(Input));
}

/* Check argument 1 before argument 2; the API returns degrees. */
static void math_atan2 (void) {
  const double First = luaL_check_number(1);
  const double Second = luaL_check_number(2);
  lua_pushnumber(ThornLuaAtan2(First, Second));
}

static void math_ceil (void) {
  lua_pushnumber(ceil(luaL_check_number(1)));
}

static void math_floor (void) {
  lua_pushnumber(floor(luaL_check_number(1)));
}

/* LAB_06f56670 checks argument 1, stores it, then checks argument 2.
** Arithmetic is delegated to the required current CRT, like the retail import. */
static void math_mod (void) {
  const double First = luaL_check_number(1);
  const double Second = luaL_check_number(2);
  lua_pushnumber(fmod(First, Second));
}

/* Retail address binding: LAB_06f566a0 -> math_sqrt. */
static void math_sqrt (void) {
  const double Input = luaL_check_number(1);
  lua_pushnumber(ThornLuaSqrt(Input));
}

static void math_pow (void) {
  const double First = luaL_check_number(1);
  const double Second = luaL_check_number(2);
  lua_pushnumber(pow(First, Second));
}

/* Retail address binding: LAB_06f566c0 -> math_log. */
static void math_log (void) {
  const double Input = luaL_check_number(1);
  lua_pushnumber(ThornLuaLog(Input));
}

/* Retail address binding: LAB_06f566e0 -> math_log10. */
static void math_log10 (void) {
  const double Input = luaL_check_number(1);
  double Result;
  __asm {
    fld Input
    fldlg2
    fxch st(1)
    fyl2x
    fstp Result
  }
  lua_pushnumber(Result);
}

/* Retail address binding: LAB_06f56700 -> math_exp. */
static void math_exp (void) {
  const double Input = luaL_check_number(1);
  double Result;
  __asm {
    fld Input
    fldl2e
    fmulp st(1), st(0)
    fld st(0)
    frndint
    fxch st(1)
    fsub st(0), st(1)
    f2xm1
    fld1
    faddp st(1), st(0)
    fscale
    fstp st(1)
    fstp Result
  }
  lua_pushnumber(Result);
}

static void math_deg (void) {
  const unsigned long long ScaleBits = 0x404ca5dc1a63c1f8ULL;
  double Scale;
  memcpy(&Scale, &ScaleBits, sizeof(Scale));
  lua_pushnumber(luaL_check_number(1) * Scale);
}

static void math_rad (void) {
  lua_pushnumber(luaL_check_number(1)*RADIANS_PER_DEGREE);
}

static void math_frexp (void) {
  int e;
  lua_pushnumber(frexp(luaL_check_number(1), &e));
  lua_pushnumber(e);
}

static void math_ldexp (void) {
  int Exponent = luaL_check_int(2);
  double Mantissa = luaL_check_number(1);
  double Result = ldexp(Mantissa, Exponent);
  lua_pushnumber(Result);
}



static void math_min (void) {
  int i = 1;
  double dmin = luaL_check_number(i);
  while (lua_getparam(++i) != LUA_NOOBJECT) {
    double d = luaL_check_number(i);
    /* 56816 TEST AH,1: less and unordered both replace the minimum. */
    if (!(d >= dmin))
      dmin = d;
  }
  lua_pushnumber(dmin);
}


static void math_max (void) {
  int i = 1;
  double dmax = luaL_check_number(i);
  while (lua_getparam(++i) != LUA_NOOBJECT) {
    double d = luaL_check_number(i);
    if (d > dmax)
      dmax = d;
  }
  lua_pushnumber(dmax);
}


/* MSVCRT _ftol returns the low DWORD of a chopped signed qword.
** A normal int64 cast selects the compiler's qword converter, including
** its indefinite low word, instead of lrint's ambient rounding mode. */
static int MathRetailFtol (double Value) {
  const __int64 Converted = (__int64)Value;
  const unsigned int Word = (unsigned int)Converted;
  int Result;
  memcpy(&Result, &Word, sizeof(Result));
  return Result;
}

static void math_random (void) {
  /* FUN_06f568c0 stores this sample as double before Lua callbacks. */
  const double Random = (double)(rand() % 0x7fff) * 3.051850947599719e-05;
  int Lower = MathRetailFtol(luaL_opt_number(1, 0.0));
  if (Lower == 0) {
    lua_pushnumber(Random);
  }
  else {
    int Upper = MathRetailFtol(luaL_opt_number(2, 0.0));
    unsigned int WidthBits;
    int Width;
    __int64 Sample;
    unsigned int ResultBits;
    int Result;
    if (Upper == 0) {
      Upper = Lower;
      Lower = 1;
    }
    luaL_arg_check(Lower <= Upper, 1, "interval is empty");
    /* Retail SUB/INC/ADD wrap as DWORDs, then FILD reads signed words. */
    WidthBits = (unsigned int)Upper - (unsigned int)Lower + 1u;
    memcpy(&Width, &WidthBits, sizeof(Width));
    Sample = (__int64)(Random * (double)Width);
    ResultBits = (unsigned int)Sample + (unsigned int)Lower;
    memcpy(&Result, &ResultBits, sizeof(Result));
    lua_pushnumber((double)Result);
  }
}

static void math_randomseed (void) {
  srand((unsigned int)MathRetailFtol(luaL_check_number(1)));
}


static struct luaL_reg mathlib[] = {
{"abs",   math_abs},
{"sin",   math_sin},
{"cos",   math_cos},
{"tan",   math_tan},
{"asin",  math_asin},
{"acos",  math_acos},
{"atan",  math_atan},
{"atan2", math_atan2},
{"ceil",  math_ceil},
{"floor", math_floor},
{"mod",   math_mod},
{"frexp", math_frexp},
{"ldexp", math_ldexp},
{"sqrt",  math_sqrt},
{"min",   math_min},
{"max",   math_max},
{"log",   math_log},
{"log10", math_log10},
{"exp",   math_exp},
{"deg",   math_deg},
{"rad",   math_rad},
{"random",     math_random},
{"randomseed", math_randomseed}
};

/*
** Open math library
*/
void lua_mathlibopen (void) {
  luaL_openlib(mathlib, (sizeof(mathlib)/sizeof(mathlib[0])));
  lua_pushcfunction(math_pow);
  lua_pushnumber(0);  /* to get its tag */
  lua_settagmethod(lua_tag(lua_pop()), "pow");
  lua_pushnumber(PI); lua_setglobal("PI");
}

