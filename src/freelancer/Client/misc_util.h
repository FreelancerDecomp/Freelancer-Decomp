// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// MiscUtil -- E:\FL\Scratch\Source\Client\cmn\MiscUtil.cpp.
//
// The client-side FmtStr formatter ("FmtStr::sprintf()" in its own FDUMP text): expands the
// mad-lib template named by FmtStr::MadLibId against the FmtStr's Val list, resolving each
// value through Universe / GoodList / Reputation into display text.

#include "common_text.h"
#include "client_cliobj.h"

#include <vector>

struct FAnimScriptSet;

// FUN_0041c940 -- __thiscall on the FmtStr: the first Val whose TypeTag equals Code and whose
// Flags (the per-kind running index) equals Index, or null.
FmtStr::Val const* FmtStrFindVal(FmtStr const* Fmt, unsigned char Index, unsigned short Code);

// FUN_0041c970 -- __thiscall on the FmtStr, RET 8: writes at most DestChars wide characters
// (terminator included) into Dest and returns the number written, terminator excluded.
int FmtStrSprintf(FmtStr const* Fmt, wchar_t* Dest, int DestChars);

// FUN_0041d440 -- the %R expansion: the reputation instance's display name, rebuilt from its
// second name FmtStr (and cached back through Reputation::Vibe::SetName) when it is empty.
// End is taken but never read, as in retail.
void MiscUtilAppendRepInstanceName(int RepId, wchar_t** Cursor, wchar_t* End);

// FUN_0041d5b0 -- append one ids string at *Cursor, FDUMP on a miss, and re-terminate the last
// character when the copy lands exactly on End.
void MiscUtilAppendIds(wchar_t** Cursor, wchar_t* End, unsigned Ids);

// FUN_004779a0 -- a number with thousands separators ("1,234,567"), after an optional ids
// prefix: PrefixIds < 0 loads ids 0x2140, PrefixIds > 0 loads that ids, 0 starts empty.
// The prefix is loaded with a 0xff-character limit whatever Out's real size is.
void FormatGroupedNumber(wchar_t* Out, int Value, int PrefixIds);

// FUN_0041c770 -- load ids Ids into a 0x400-character buffer (FDUMP an error on a miss) and
// heap-copy it into *Out with Common's malloc_wstrcpy; returns what malloc_wstrcpy returns.
wchar_t const* MiscUtilLoadIdsCopy(wchar_t** Out, unsigned Ids);

// FUN_0041c7e0 (cdecl) -- open File as a "UTF" file system, load every texture below its
// "Texture library" node (DALib::TextureLib slot 5) and return the texture group id. Callers:
// FUN_00557550 (fx\efx.txm), 0x0045f56a.
int MiscUtilLoadTextureLibrary(char const* File);

// FUN_0041c830 (cdecl) -- DALib::TextureLib slot 7 on Group: release a group
// MiscUtilLoadTextureLibrary loaded. Callers: 0x0045e426, 0x0046c7b6.
void MiscUtilReleaseTextureLibrary(int Group);

// FUN_0041c850 (cdecl) -- open File as a file system (implementation 0), load its material
// library into a new group (DALib::MaterialLib slot 8), apply-verify it at 1.0 (slot 11) and
// resolve its textures (slot 12); returns the group id (-1 when the load wrote none). Released
// with MaterialLib slot 10 (DeleteGroup). Callers: NN_MissionVendor::Create 0x0048bb7f,
// 0x004a4597.
int MiscUtilLoadMaterialLibrary(char const* File);

// FUN_0041c540 (cdecl, 7 arguments) -- create an engine instance of the .cmp/.3db File.
// When the engine does not know the archetype yet: open File's own file system (FDUMP a
// warning and return -1 when it does not exist), take its material library entry
// (FUN_0041ade0) and the at most 5 extra material files in MaterialPaths (FUN_0041af40), load
// the archetype (FDUMP and return -1 on failure), instance it, read the animation scripts
// into Scripts, and hand the material entries to Entries -- or release them when Entries is
// null. When it does: instance it, and read Scripts from a fresh file system. *OutHandle
// receives the archetype handle when OutHandle is not null. Data is never read.
void* MiscUtilLoadEngineInstance(char const* File, IFileSystem* Data, FAnimScriptSet* Scripts,
                                 void* Unused,
                                 Archetype::FCliVc6Vector<Archetype::FCliMaterialPathPair> const*
                                     MaterialPaths,
                                 std::vector<Archetype::FCliMaterialEntry*>* Entries,
                                 long* OutHandle);

struct ICamera;

// FUN_0041c8d0 (cdecl) -- the sphere (Center, Radius) through the camera's view transform
// (ICamera slot 16) with 3DMathEngine TransformXform (the file's DAT_00616720, acquired on
// first use), then ICamera slot 15 ClassifySphere; true when it answers 3 or more (straddle or
// inside). Callers: CharacterObject::DrawOpaque (0x004451d6) and the LightsCamera particle draw
// FUN_005574d0 (0x00557527).
bool MiscUtilSphereInView(ICamera* Camera, float const* Center, float Radius);
