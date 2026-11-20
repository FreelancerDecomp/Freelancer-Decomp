// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// FL_LAYOUT_ASSERT(condition, message): a check that a structure has retail Freelancer's
// size or a field its retail offset. Every project includes this header (/FI, set in
// src/Directory.Build.props).
//
// The retail layouts are those of the 32-bit build, so the checks apply to Win32 only. The
// port-only x64 build (docs/x64.md) has 8-byte pointers, and with them other sizes and
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
#if defined(__cplusplus)
#if defined(_WIN64)
#define FL_LAYOUT_ASSERT(...) static_assert(true, "")
#define FL_LAYOUT_PAD(Bytes) 1
#define FL_RETAIL_NEW_SIZE(Bytes, Type) (sizeof(Type) > (Bytes) ? sizeof(Type) : (Bytes))
#else
#define FL_LAYOUT_ASSERT(...) static_assert(__VA_ARGS__)
#define FL_LAYOUT_PAD(Bytes) (Bytes)
#define FL_RETAIL_NEW_SIZE(Bytes, Type) (Bytes)
#endif
#endif
