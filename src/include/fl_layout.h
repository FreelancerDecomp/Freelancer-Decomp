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
#if defined(__cplusplus)
#if defined(_WIN64)
#define FL_LAYOUT_ASSERT(...) static_assert(true, "")
#else
#define FL_LAYOUT_ASSERT(...) static_assert(__VA_ARGS__)
#endif
#endif
