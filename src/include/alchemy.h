// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"

// alchemy.dll public ABI. Source: EXE/Disasm/alchemy.dll, docs/re/alchemy.md.
// Internal node implementations, particle pools and .ale chunks stay in src/alchemy/.
// The shared node-library contract is declared here and inherited by IAlchemy.
//
// These are declared at GLOBAL scope on purpose. DALib exports ::IAlchemy* / ::IFxRuntime* /
// ::IFxEffectLibrary* (the mangled names retail uses, see src/include/DALib.h), while alchemy
// declared the same interfaces inside namespace AlchemyImpl -- two unrelated types with one
// name, which is why consumers could only ever call them by raw slot number. alchemy_types.h
// now pulls these into AlchemyImpl with using-declarations, so both sides mean one type.

struct IFxEffect;
struct IFileSystem;

// File / vtable rec16 is 4 dwords. AddRec16 stores [in2, id, in0, in1]; GetRec16 un-reorders.
// These are node IDs / edges, not factory names — no emitter/field objects from them.
struct FAleRec16
{
    unsigned long A;
    unsigned long B;
    unsigned long C;
    unsigned long D;
};

struct FAleRec8
{
    unsigned long A;
    unsigned long B;
};

struct IFxEffectArchetype : IDAComponent
{
    virtual GENRESULT DACOM_API ResetRecords() = 0;                              // 3  +0x0C LAB_0621e670 RET 4
    virtual GENRESULT DACOM_API SetName(const char* Name) = 0;                   // 4  +0x10 FUN_0621e6f0 RET 8
    virtual const char* DACOM_API GetName() = 0;                                 // 5  +0x14 LAB_0621e7b0 RET 4
    virtual GENRESULT DACOM_API AddRec16(const FAleRec16* Rec, unsigned long* OutId,
                                         unsigned long Flag) = 0;                // 6  +0x18 FUN_0621e7d0 RET 0x10
    virtual GENRESULT DACOM_API RemoveRec16(unsigned long Id) = 0;               // 7  +0x1C FUN_0621e890 RET 8
    virtual GENRESULT DACOM_API Slot8(unsigned long Id, unsigned long Value) = 0; // 8  +0x20 LAB_0621e950 RET 0xC
    virtual GENRESULT DACOM_API AddRec8(const FAleRec8* Rec) = 0;                // 9  +0x24 FUN_0621e990 RET 8
    virtual GENRESULT DACOM_API RemoveRec8(const FAleRec8* Rec) = 0;             // 10 +0x28 FUN_0621eb70 RET 8
    // 11/12: retail LAB_06220e40 / LAB_06220e60 are thiscall (RET 0). No retail DLL is loaded,
    // so the convention only has to agree between our caller and callee: DACOM_API like every
    // other interface method (AGENTS.md), as done for IMatAnimLibrary 3-6.
    virtual unsigned long DACOM_API GetRec16Count() = 0;               // 11 +0x2C LAB_06220e40
    virtual unsigned long DACOM_API GetRec8Count() = 0;                // 12 +0x30 LAB_06220e60
    virtual GENRESULT DACOM_API GetRec16(unsigned long Index, FAleRec16* Out) = 0; // 13 +0x34 LAB_0621ec80 RET 0xC
    virtual GENRESULT DACOM_API GetRec8(unsigned long Index, FAleRec8* Out) = 0; // 14 +0x38 LAB_0621ecc0 RET 0xC
    virtual GENRESULT DACOM_API GetRec16ById(unsigned long Id, FAleRec16* Out) = 0; // 15 +0x3C LAB_0621ebe0 RET 0xC
    virtual GENRESULT DACOM_API FindRec8Index(const FAleRec8* Rec,
                                             unsigned long* OutIndex) = 0;      // 16 +0x40 LAB_0621ec20 RET 0xC
    virtual GENRESULT DACOM_API RemapRec16FieldD(unsigned long OldId,
                                                 unsigned long NewId) = 0;      // 17 +0x44 LAB_0621ecf0 RET 0xC
    virtual GENRESULT DACOM_API GetHeader16(unsigned char* Out16) = 0;           // 18 +0x48 LAB_0621ed20 RET 8
    virtual GENRESULT DACOM_API SetHeader16(const unsigned char* Header16) = 0;  // 19 +0x4C FUN_0621ed50 RET 8
    virtual GENRESULT DACOM_API Slot20(void* Arg) = 0;                           // 20 +0x50 LAB_0621e660 RET 8
};

// Node-library contract shared by the standalone component and IAlchemy.
// The port keeps its existing COM prefix and method indices; all DLLs use this declaration.
using FNodeLibraryEnumCallback = int(__cdecl*)(IDAComponent* Node, void* Context);

struct IFxNodeLibrary : IDAComponent
{
    virtual GENRESULT DACOM_API LoadNodeLibrary(IFileSystem* FileSystem) = 0;   // 3  RET 8
    virtual GENRESULT DACOM_API SaveNodeLibrary(IFileSystem* FileSystem) = 0;   // 4  RET 8
    virtual unsigned long DACOM_API HashNodeName(const char* Name) = 0;         // 5  RET 8 → FUN_0623ef70
    virtual GENRESULT DACOM_API NodeLibSlot6(const char* Name, IDAComponent* Node,
                                           unsigned long* OutHash) = 0;        // 6 RET 0x10
    virtual GENRESULT DACOM_API NodeLibSlot7(unsigned long Hash) = 0;          // 7 RET 8
    virtual GENRESULT DACOM_API NodeLibSlot8(FNodeLibraryEnumCallback Callback,
                                           void* Context) = 0;                // 8 RET 0xC
    virtual GENRESULT DACOM_API ClearNodeLibrary() = 0;                         // 9  RET 4
    virtual GENRESULT DACOM_API NodeLibSlot10(void* A, void* B) = 0;            // 10 RET 0xC
    virtual GENRESULT DACOM_API NodeLibSlot11(unsigned long Hash, IDAComponent** Out) = 0; // 11 RET 0xC
    virtual GENRESULT DACOM_API NodeLibSlot12(unsigned long Hash, IDAComponent** Out) = 0; // 12 RET 0xC
};

// IAlchemy retains the same slots 0–40; slots 3–12 are inherited unchanged.
struct IAlchemy : IFxNodeLibrary
{
    virtual GENRESULT DACOM_API CreateVertexBuffer() = 0;                       // 13 RET 4
    virtual GENRESULT DACOM_API ReleaseVertexBuffer() = 0;                      // 14 RET 4
    virtual GENRESULT DACOM_API Slot15(void* A, void* B) = 0;                   // 15 RET 0xC
    virtual GENRESULT DACOM_API Slot16(void* A, void* B) = 0;                   // 16 RET 0xC
    virtual GENRESULT DACOM_API DrawNodes(void* A, void* B) = 0;                // 17 RET 0xC
    virtual GENRESULT DACOM_API GetDrawCookie() = 0;                            // 18 RET 4
    virtual GENRESULT DACOM_API Slot19(const char* Name, void* Unused) = 0;    // 19 +0x4C RET 0xC
    virtual GENRESULT DACOM_API AttachNode(void* A, void* B) = 0;               // 20 RET 0xC
    virtual GENRESULT DACOM_API Slot21(void* A, void* B) = 0;                   // 21 RET 0xC
    virtual GENRESULT DACOM_API DetachNode(void* Node) = 0;                     // 22 RET 8
    virtual GENRESULT DACOM_API SubmitNode(void* Node) = 0;                     // 23 RET 8
    virtual GENRESULT DACOM_API SubmitChildNodes(void* Node) = 0;               // 24 RET 8
    virtual GENRESULT DACOM_API GetRootNode(void** Out) = 0;                    // 25 RET 8
    virtual GENRESULT DACOM_API FindNode(void* A, void* B, void* C) = 0;        // 26 RET 0x10
    virtual GENRESULT DACOM_API SetUpdateCallback(void* Fn) = 0;                // 27 RET 8
    virtual GENRESULT DACOM_API SetFilterCallback(void* Fn) = 0;                // 28 RET 8
    virtual GENRESULT DACOM_API BeginDraw() = 0;                                // 29 RET 4
    virtual GENRESULT DACOM_API EndDraw() = 0;                                  // 30 RET 4
    virtual GENRESULT DACOM_API SetUseMaterialBatcher(unsigned char Value) = 0; // 31 RET 8
    virtual GENRESULT DACOM_API GetUseMaterialBatcher() = 0;                    // 32 RET 4
    virtual GENRESULT DACOM_API EnqueueAppearance(void* A, void* B) = 0;        // 33 RET 0xC
    virtual void* DACOM_API GetVertexBufferHandle() = 0;                        // 34 RET 4
    virtual GENRESULT DACOM_API SetDrawFlags(void* Pair) = 0;                   // 35 RET 8
    virtual GENRESULT DACOM_API CopyCounters(void* Out9Dwords) = 0;             // 36 RET 8
    virtual GENRESULT DACOM_API Initialize(AGGDESC* Desc) = 0;                  // 37 +0x94
    virtual GENRESULT DACOM_API Slot38() = 0;                                   // 38 RET 4
    virtual GENRESULT DACOM_API GetFlagA() = 0;                                 // 39 RET 4
    virtual GENRESULT DACOM_API GetFlagB() = 0;                                 // 40 RET 4
};

// The mesh appearance's instance object. Retail's only implementation is freelancer.exe's
// FLFxMesh (RTTI `FLFxMesh : IFxMesh`, vtable 005d98ec); alchemy.dll consumes it. Slot names are
// not recovered -- each comment says what FLFxMesh does in that slot. Arity from RET imm16.
struct IFxMesh : IDAComponent
{
    virtual GENRESULT DACOM_API Slot3(void* Transform) = 0; // +0x0C LAB_004f6910 RET 8: IEngine::SetInstanceMatrix
    virtual float* DACOM_API Slot4() = 0;                   // +0x10 LAB_004f6930 RET 4: IEngine::GetInstanceTransform
    virtual GENRESULT DACOM_API Slot5(void* Arg) = 0;       // +0x14 LAB_004f6940 RET 8: IEngine::UpdateInstance(inst, 0, Arg)
    virtual GENRESULT DACOM_API Slot6(void* Camera) = 0;    // +0x18 FUN_004f6970 RET 8: draw (ZWRITEENABLE 0, ZFUNC 4 LESSEQUAL)
    virtual GENRESULT DACOM_API Slot7() = 0;                // +0x1C FUN_004f6a70 RET 4: drop material + instance
    virtual GENRESULT DACOM_API Slot8(void* Arg) = 0;       // +0x20 LAB_004f6ab0 RET 8: returns -1
};

// The factory alchemy.dll keeps in DAT_06257b9c (IFxRuntime::BindMeshFactory) and calls through
// slot 3 at 0x06224d56 with (appearance +0xc0 -> +0x60, +0x74, a local position, &out).
// Retail's only implementation is freelancer.exe's FLFxMeshFactory (vtable 005d98bc). Retail
// has the scalar deleting destructor as slot 4 (+0x10, FUN_004f6b30), which Release calls; the
// port leaves it out of the interface (no virtual destructors on DACOM interfaces) and the
// implementation deletes itself.
struct IFxMeshFactory : IDAComponent
{
    virtual GENRESULT DACOM_API Slot3(void* ArgA, void* ArgB, float const* Position,
                                      IFxMesh** Out) = 0; // +0x0C FUN_004f6810 RET 0x14
};

struct IFxRuntime : IDAComponent
{
    virtual GENRESULT DACOM_API BindAlchemy(void* Alchemy) = 0;            // +0x0C RET 8
    virtual GENRESULT DACOM_API BindUnknown(void* Value) = 0;              // +0x10 RET 8
    virtual GENRESULT DACOM_API BindEffectLibrary(void* Library) = 0;      // +0x14 RET 8
    virtual GENRESULT DACOM_API GetAlchemy(void** Out) = 0;                // +0x18 RET 8
    virtual GENRESULT DACOM_API GetUnknown(void** Out) = 0;                // +0x1C RET 8
    virtual GENRESULT DACOM_API GetEffectLibrary(void** Out) = 0;          // +0x20 RET 8
    virtual GENRESULT DACOM_API BindMeshFactory(void* Factory) = 0;        // +0x24 LAB_0623b6a0 DAT_06257b9c
    virtual GENRESULT DACOM_API GetMeshFactory(void** Out) = 0;            // +0x28 LAB_0623b740
};

using FEffectLibraryEnumCallback = int(__cdecl*)(IFxEffectArchetype* Archetype, void* Context);

struct IFxEffectLibrary : IDAComponent
{
    // LAB_0621fd30: independent raw bindings DAT_06257a3c and DAT_06257a38. RET 0xC.
    virtual GENRESULT DACOM_API BindAlchemy(IAlchemy* Alchemy, IFxNodeLibrary* NodeLibrary) = 0;
    virtual GENRESULT DACOM_API InsertArchetype(IFxEffectArchetype* Archetype,
                                                unsigned long* OutHash) = 0; // 4 FUN_0621fd50 RET 0xC
    virtual GENRESULT DACOM_API RemoveByHash(unsigned long Hash) = 0;   // 5 LAB_0621ff90 RET 8
    virtual GENRESULT DACOM_API CreateEffect(void* Unused, void* Key,
                                             IFxEffect** Out) = 0;      // FUN_06220020 RET 0x10
    virtual GENRESULT DACOM_API SaveAle(IFileSystem* FileSystem) = 0;   // FUN_06220140 RET 8
    virtual GENRESULT DACOM_API LoadAle(IFileSystem* FileSystem) = 0;   // LAB_06220310 RET 8
    virtual GENRESULT DACOM_API ClearEffects() = 0;                     // FUN_06220600 RET 4
    virtual GENRESULT DACOM_API EnumerateArchetypes(FEffectLibraryEnumCallback Callback,
                                                    void* Context) = 0; // 10 FUN_062206d0 RET 0xC
    virtual GENRESULT DACOM_API FindByHash(unsigned long Hash,
                                           IFxEffectArchetype** Out) = 0; // 11 LAB_06220760 RET 0xC
    virtual GENRESULT DACOM_API MakeUniqueName(const char* Name, const char** Out) = 0; // 12 RET 0xC
    virtual GENRESULT DACOM_API RemapAllRec16FieldD(unsigned long OldId,
                                                    unsigned long NewId) = 0; // 13 FUN_06220950 RET 0xC
    virtual GENRESULT DACOM_API Slot14(void* Unused) = 0;               // LAB_0621fd20 RET 8 → ClearEffects
};

struct IFxEffect : IDAComponent
{
    virtual GENRESULT DACOM_API Slot3(void* A, void* B) = 0;           // FUN_0621f610 RET 0xC
    virtual GENRESULT DACOM_API Slot4(void* A, void* B) = 0;           // LAB_0621f6a0 JMP node+0x40 RET 0xC
    virtual GENRESULT DACOM_API Slot5(float Age) = 0;                  // LAB_0621f7f0 RET 8: node +0x64
    virtual GENRESULT DACOM_API Slot6(float OuterTime) = 0;            // LAB_0621f820 RET 8: node +0x5C
    virtual GENRESULT DACOM_API Slot7(void* Arg) = 0;                  // FUN_0621f6d0 RET 8
    virtual GENRESULT DACOM_API Slot8(void* Arg) = 0;                  // FUN_0621f730 RET 8
    virtual GENRESULT DACOM_API Slot9(void* Arg) = 0;                  // FUN_0621f790 RET 8
    virtual GENRESULT DACOM_API Slot10(void* A, void* B) = 0;          // LAB_0621fa40 RET 0xC
    virtual GENRESULT DACOM_API Slot11(void* Arg) = 0;                 // LAB_0621fa80 RET 8
    virtual GENRESULT DACOM_API Slot12(void* A, void* B) = 0;          // LAB_0621fb50 RET 0xC
    virtual GENRESULT DACOM_API Slot13(void* Arg) = 0;                 // FUN_0621fba0 RET 8 (Ghidra thiscall)
    virtual GENRESULT DACOM_API Slot14(float LodInput) = 0;            // LAB_0621f850 RET 8: node +0x88
    virtual GENRESULT DACOM_API Slot15(void* Arg) = 0;                 // LAB_0621f880 RET 8
    virtual GENRESULT DACOM_API GetBoundComponent(void** Out) = 0;     // 16 LAB_0621fd00 RET 8
    virtual GENRESULT DACOM_API Slot17() = 0;                          // FUN_06220d70 dtor-with-flag
    virtual GENRESULT DACOM_API Initialize(AGGDESC* Desc) = 0;         // +0x48 FUN_0621eed0 RET 8
};
