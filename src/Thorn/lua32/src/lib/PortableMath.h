#ifndef THORN_PORTABLE_LUA_MATH_H
#define THORN_PORTABLE_LUA_MATH_H

#include <math.h>

/* Lua's retail math API uses degrees. These literals are the original binary64
   scale constants. Standard math replaces CPU-specific range reduction and
   extended intermediates; exception flags and last-bit identity are not APIs. */
static double ThornLuaSin(double input) { return sin(input * 0.017453292519943295); }
static double ThornLuaCos(double input) { return cos(input * 0.017453292519943295); }
static double ThornLuaTan(double input) { return tan(input * 0.017453292519943295); }
static double ThornLuaAtan(double input) { return atan(input) * 57.29577951308232; }
static double ThornLuaAtan2(double first, double second) { return atan2(first, second) * 57.29577951308232; }
static double ThornLuaSqrt(double input) { return sqrt(input); }
static double ThornLuaLog(double input) { return log(input); }
static double ThornLuaLog10(double input) { return log10(input); }
static double ThornLuaExp(double input) { return exp(input); }

#endif
