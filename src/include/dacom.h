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
#include <cstddef> // offsetof

#include "dacom_iids.h"  // every "1.11_*" interface id, in one place

typedef FlLong GENRESULT;

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
    virtual FlULong DACOM_API AddRef() = 0;
    virtual FlULong DACOM_API Release() = 0;
};

// Aggregate initialization is defined by the concrete component.
struct IAggregateComponent : IDAComponent
{
    virtual GENRESULT DACOM_API Initialize() = 0; // +0x0C
};

// Aggregate descriptors (AGGDESC). ICOManager::CreateInstance and every component factory take
// an AGGDESC*; the size field says which of the longer layouts below the caller filled in, and a
// factory reads only the prefix its size allows. These are the only definitions: modules cast the
// AGGDESC* they receive to one of them instead of declaring their own copy.

// 0x08: size and the registered component / interface name (Digital Anvil's field names).
struct AGGDESC
{
    FlULong size;
    const char* interface_name;
};

// 0x0C: the "3DMathEngine" request (DA3DMATHDESC). Callers leave Implementation null.
struct FMathDesc
{
    FlULong Size;          // +0x00  0x0C
    const char* InterfaceName;   // +0x04
    const char* Implementation;  // +0x08
};

FL_LAYOUT_ASSERT(sizeof(FMathDesc) == 0xC, "retail AGGDESC / DA3DMATHDESC size 0xC");

// 0x14: an aggregatable component. When Outer is set the new object delegates its identity to
// it and writes its inner (non-delegating) IDAComponent to *InnerOut; when Outer is null the
// object is its own outer. SystemContainer fills Value from the "Name = Value" line of its
// [System] section.
struct FAGGDESC14
{
    FlULong Size;          // +0x00  0x14
    const char* Name;            // +0x04
    IDAComponent* Outer;         // +0x08  controlling outer, or null
    IDAComponent** InnerOut;     // +0x0C  receives the inner IDAComponent when Outer is set
    const char* Value;           // +0x10  the profile line's value, or null
};

FL_LAYOUT_ASSERT(sizeof(FAGGDESC14) == 0x14, "retail AGGDESC size 0x14");

// 0x18: FAGGDESC14 plus the SystemContainer that creates the component (Engine children,
// RenderManager, Deformable, FLAppearance, VMeshWire).
struct FAGGDESC18
{
    FlULong Size;              // +0x00  0x18
    const char* Name;                // +0x04
    IDAComponent* Outer;             // +0x08
    IDAComponent** InnerOut;         // +0x0C
    const char* Value;               // +0x10
    IDAComponent* SystemContainer;   // +0x14
};

FL_LAYOUT_ASSERT(sizeof(FAGGDESC18) == 0x18, "retail AGGDESC size 0x18");

// The FileSystem component's descriptor (0x30 or 0x34 for DOS and UTF, 0x40 for MEM).
// DALib::OpenData / ReadFile.dll.c.
struct FFileSystemDesc
{
    FlULong Size;              // +0x00  0x30 or 0x34
    const char* ComponentName;       // +0x04  "FileSystem"
    const char* Implementation;      // +0x08  "DOS" / "UTF" / "MEM"
    const char* Path;                // +0x0C
    FlULong Access;            // +0x10  GENERIC_READ 0x80000000
    FlULong ShareMode;         // +0x14  FILE_SHARE_READ
    void* Security;                  // +0x18
    FlULong Creation;          // +0x1C  OPEN_EXISTING 3
    FlULong Flags;             // +0x20  0x08000080
    void* TemplateFile;              // +0x24
    IDAComponent* Parent;            // +0x28
    FlULong Extra;             // +0x2C
    FlULong Field30;           // +0x30  0xFFFFFFFF when Size==0x34
    void* MemBuffer;                 // +0x34  MEM size 0x40 only (FUN_06b777f0)
    FlULong MemSize;           // +0x38
    FlULong MemFlags;          // +0x3C  bit0=wrap pointer, bit1=flag+1
};

// Callers fill `Size` (0x30 / 0x34 for DOS and UTF, 0x40 for MEM) and the callee reads
// only that much, so the DOS-sized prefix layout is the invariant that matters.
FL_LAYOUT_ASSERT(offsetof(FFileSystemDesc, Field30) == 0x30, "DOS/UTF desc prefix is 0x34");
FL_LAYOUT_ASSERT(sizeof(FFileSystemDesc) == 0x40, "MEM tail extends the desc to 0x40");

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
    FlULong __cdecl GetCRC32(const char* str);
    // Retail FUN_065b60b0 (export ordinal 3, ?GetCRC32@DACOM_CRC@@YAKPBD0@Z). begin == end
    // returns 0; any other range never terminates (retail has no exit test). No importer.
    FlULong __cdecl GetCRC32(const char* begin, const char* end);
    // Retail FUN_065b6140 — continues a prior GetCRC32/GetContinuedCRC32 result over another
    // string (null str -> initial_crc unchanged; GetContinuedCRC32(0, s) == GetCRC32(s)).
    FlULong __cdecl GetContinuedCRC32(FlULong initial_crc, const char* str);
    // Retail FUN_065b6190 — single-character overload, one CRC table step over ch (case-folded).
    FlULong __cdecl GetContinuedCRC32(FlULong initial_crc, char ch);
    int __cdecl CompareStringsI(const char* a, const char* b);
}

namespace LogStream
{
    // ?LogEvent@LogStream@@YAXPBDMK@Z (dacom.dll export, dacom.def). freelancer.exe imports it
    // through .rdata:005c6d1c (sole caller 0x0052fcc5, "Timing_cu.collide.num_bullets").
    void __cdecl LogEvent(const char* Name, float Value, FlULong Flags);
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
