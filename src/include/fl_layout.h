// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// FL_LAYOUT_ASSERT(condition, message): a check that a structure has retail Freelancer's
// size or a field its retail offset. Every project includes this header (/FI, set in
// src/Directory.Build.props).
//
// The retail layouts are those of the 32-bit build, so the checks apply to Win32 only. The
// x64 build (PORT(x64), docs/x64.md) has 8-byte pointers, and with them other sizes and
// offsets; there the macro checks nothing. It still expands to a declaration, so it is valid
// wherever static_assert is, followed by the same semicolon.
//
// FL_LAYOUT_PAD(bytes): the size of a padding array that brings a member to its retail size,
// e.g. `unsigned char Pad[FL_LAYOUT_PAD(0x14 - sizeof(std::map<...>))];`. On x64 the
// container is larger than the retail span, so the pad is a single unused byte instead.
//
// FL_RETAIL_NEW_SIZE(bytes, Type): the size retail passes to operator new for a Type, e.g.
// `::operator new(FL_RETAIL_NEW_SIZE(0x30, IVP_OV_Element))`. On x64 the object holds wider
// pointers, so the block is at least sizeof(Type) there.
//
// Only the 32-bit Windows build has retail's layouts; every other target (x64 Windows, and the
// 64-bit macOS and Linux builds, docs/x64-client-plan.md phase 6) is treated as x64 is.
//
// FlULong / FlLong: Win32's `unsigned long` / `long`, 32 bits wide on every platform. Windows
// keeps `long` at 32 bits on x64 too, but macOS and Linux make it 64, which would change every
// retail structure, file format and packet that holds one. On Windows they are exactly
// `unsigned long` and `long`, so the preprocessed code, the generated code and C++ name
// mangling (the exports retail mods link against) stay as they were; elsewhere they are the
// 32-bit integers Win32 meant. Code that hands a `long` to the C library or the OS uses the
// real type.
#if defined(__cplusplus)
#if defined(_WIN32)
using FlULong = unsigned long;
using FlLong = long;
#else
using FlULong = unsigned int;
using FlLong = int;
static_assert(sizeof(FlULong) == 4 && sizeof(FlLong) == 4, "FlULong and FlLong are Win32's 32-bit long");
#endif
#if defined(_WIN64) || !defined(_WIN32)
#define FL_LAYOUT_ASSERT(...) static_assert(true, "")
#define FL_LAYOUT_PAD(Bytes) 1
#define FL_RETAIL_NEW_SIZE(Bytes, Type) (sizeof(Type) > (Bytes) ? sizeof(Type) : (Bytes))
#else
#define FL_LAYOUT_ASSERT(...) static_assert(__VA_ARGS__)
#define FL_LAYOUT_PAD(Bytes) (Bytes)
#define FL_RETAIL_NEW_SIZE(Bytes, Type) (Bytes)
#endif
#endif
