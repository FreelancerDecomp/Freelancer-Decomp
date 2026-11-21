// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Digital Anvil DACOM — EXE/Disasm/dacom.dll.c
// C exports are __cdecl. Interface methods are __stdcall (this on the stack).
// IID strings look like "1.11_ICOManager", not COM GUIDs.

#include <windows.h>

#include "dacom_iids.h"  // every "1.11_*" interface id, in one place

typedef long GENRESULT;

// Shared DACOM result codes; module-specific failures remain with their modules.
static const GENRESULT GR_OK = 0;
static const GENRESULT GR_GENERIC = (GENRESULT)0xFFFFFFFF;
static const GENRESULT GR_INVALID_PARMS = (GENRESULT)0xFFFFFFFE;
static const GENRESULT GR_INTERFACE_UNSUPPORTED = (GENRESULT)0xFFFFFFFD;
static const GENRESULT GR_OUT_OF_MEMORY = (GENRESULT)0xFFFFFFFC;

#define DACOM_API __stdcall

struct IDAComponent
{
    virtual GENRESULT DACOM_API QueryInterface(const char* iid, void** out) = 0;
    virtual unsigned long DACOM_API AddRef() = 0;
    virtual unsigned long DACOM_API Release() = 0;
};

// Aggregate initialization is defined by the concrete component.
struct IAggregateComponent : IDAComponent
{
    virtual GENRESULT DACOM_API Initialize() = 0; // +0x0C
};

struct AGGDESC
{
    unsigned long size;
    const char* interface_name;
};

// Aggregated factory descriptor: OuterUnknown controls identity; InnerUnknown receives ownership.
struct FAGGDESC14
{
    unsigned long Size;
    const char* NameAt4;
    IDAComponent* OuterUnknown;
    IDAComponent** InnerUnknown;
    unsigned long Reserved10;
};

// ICOManager::EnumerateComponents callback. Retail 065b24e0 calls it cdecl as
// (factory, registration name, priority, context); a zero return stops the walk.
typedef int(__cdecl* FComponentEnumCallback)(IDAComponent* Factory, const char* Name, void* Priority,
                                             void* Context);

// ICOManager singleton from DACOM_Acquire(), vtable 065b70cc (docs/re/dacom.md).
struct ICOManager : IDAComponent
{
    virtual GENRESULT DACOM_API CreateInstance(AGGDESC* desc, void** instance) = 0;          // +0x0C
    virtual GENRESULT DACOM_API RegisterComponent(IDAComponent* factory, const char* name,
                                                  void* priority) = 0;                       // +0x10
    // 065b23a0, RET 0xC. A null name searches every registration.
    virtual GENRESULT DACOM_API UnregisterComponent(IDAComponent* factory, const char* name) = 0; // +0x14
    // 065b24e0, RET 0x10. A null name walks every registration.
    virtual GENRESULT DACOM_API EnumerateComponents(const char* name, FComponentEnumCallback callback,
                                                    void* context) = 0;                     // +0x18
    virtual GENRESULT DACOM_API AddLibrary(const char* dll_name) = 0;                        // +0x1C
    virtual GENRESULT DACOM_API RemoveLibrary(const char* dll_name) = 0;                     // +0x20
    virtual GENRESULT DACOM_API Shutdown() = 0;                                              // +0x24
    virtual GENRESULT DACOM_API SetINIConfig(const char* ini_path, unsigned int flags) = 0;  // +0x28
};

struct IComponentFactory : IDAComponent
{
    virtual GENRESULT DACOM_API CreateInstance(AGGDESC* desc, void** instance) = 0; // +0x0C
};

// "RegExp" component: the compiled program is owned; pattern and subject are borrowed.
struct IRegExp : IDAComponent
{
    // +0x0C 065b5ac0: frees the previous program, regcomp(pattern); true when it compiled.
    virtual bool DACOM_API CompilePattern(const char* Pattern) = 0; // formerly Slot3
    // +0x10 065b5b00: remembers subject, returns regexec(program, subject) (0 when no program).
    virtual int DACOM_API Search(const char* Subject) = 0; // formerly Slot4
    // +0x14: copies to Buffer or object-owned scratch and appends NUL.
    virtual char* DACOM_API CopyCapture(int Index, char* Buffer, int Size, int Skip, int bNullOnFail) = 0; // formerly Slot5
    // +0x18 065b5be0: startp[index] - startp[0], or -1.
    virtual int DACOM_API GetCaptureOffsetFromMatch(int Index) = 0; // formerly Slot6
    // +0x1C 065b5c10: startp[index] - subject, or -1 (retail reads startp here, not endp).
    virtual int DACOM_API GetCaptureOffsetFromSubject(int Index) = 0; // formerly Slot7
};

namespace DACOM_CRC
{
    // Retail dacom.dll — CRC32 over lowercased name; keys FUN_065b21e0 / FUN_065b2100 tree.
    unsigned long __cdecl GetCRC32(const char* str);
    // Retail FUN_065b60b0 (export ordinal 3, ?GetCRC32@DACOM_CRC@@YAKPBD0@Z). begin == end
    // returns 0; any other range never terminates (retail has no exit test). No importer.
    unsigned long __cdecl GetCRC32(const char* begin, const char* end);
    // Retail FUN_065b6140 — continues a prior GetCRC32/GetContinuedCRC32 result over another
    // string (null str -> initial_crc unchanged; GetContinuedCRC32(0, s) == GetCRC32(s)).
    unsigned long __cdecl GetContinuedCRC32(unsigned long initial_crc, const char* str);
    // Retail FUN_065b6190 — single-character overload, one CRC table step over ch (case-folded).
    unsigned long __cdecl GetContinuedCRC32(unsigned long initial_crc, char ch);
    int __cdecl CompareStringsI(const char* a, const char* b);
}

namespace LogStream
{
    // ?LogEvent@LogStream@@YAXPBDMK@Z (dacom.dll export, dacom.def). freelancer.exe imports it
    // through .rdata:005c6d1c (sole caller 0x0052fcc5, "Timing_cu.collide.num_bullets").
    void __cdecl LogEvent(const char* Name, float Value, unsigned long Flags);
}

#ifdef DACOM_EXPORTS
#define DACOM_LINK __declspec(dllexport)
#else
#define DACOM_LINK __declspec(dllimport)
#endif

extern "C"
{
    ICOManager* DACOM_Acquire();
    GENRESULT __cdecl DACOM_GetDllVersion(const char* module, unsigned int* major,
                                          unsigned int* minor, unsigned int* build);
    GENRESULT __cdecl DACOM_GetVersion(unsigned int* major, unsigned int* minor,
                                       unsigned int* build);
    DACOM_LINK extern void(__cdecl* FDUMP)(int flags, const char* fmt, ...);
}
