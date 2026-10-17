// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "render/d3d8_min.h"

#include <cstddef>

// IRPTexture parameter types: the FileSystem interface is declared in readfile.h, the image
// info record is the DirectX 8.1 SDK's D3DXIMAGE_INFO (d3dx8tex.h).
struct IFileSystem;
struct _D3DXIMAGE_INFO;


// RP8-only names. Every IID is Iids::* from dacom_iids.h and IAggregateComponent itself is
// declared in dacom.h, so nothing here is shared with render/shading.h any more.


static const char kRp8ComponentName[] = "DirectX8";
static const char kRp8DefaultSection[] = "RenderPipeline";
// RP9.dll: the Direct3D 9 mirror of RP8 (same IIDs, same default section). See docs/re/rp9.md.
static const char kRp9ComponentName[] = "DirectX9";


// The C++ type is IRenderPipeline; its IID string is "1.11_IRenderPipeline8B" (Iids::RenderPipeline8B).
// Retail evidence: DALib.dll exports ?Pipeline@DALib@@3PAUIRenderPipeline@@A (freelancer.exe
// imports the same name), so the pointee of DALib::Pipeline is a struct named IRenderPipeline,
// and that pointer is filled by QueryInterface for "1.11_IRenderPipeline8B" (DALib 065c12e8).
// No retail binary carries "IRenderPipeline8B" anywhere except that IID string (alchemy,
// deformable2, flmaterials, rendcomp, rp8, shading, DALib); their error texts name the IID macro
// "IID_IRenderPipeline" ("Alchemy: init: unable to acquire IID_IRenderPipeline"). "8B" is the
// IID's version suffix, not part of the type name.
struct IRenderPipeline : IDAComponent
{
    // slot 3
    virtual GENRESULT DACOM_API Startup(const char* SectionName) = 0;
    // slot 4 FUN_06d07aa8
    virtual GENRESULT DACOM_API ShutdownPipeline() = 0;
    // slot 5 FUN_06d081c2
    virtual GENRESULT DACOM_API SetPipelineState(int Id, int Value) = 0;
    // slot 6 FUN_06d0823e
    virtual GENRESULT DACOM_API GetPipelineState(int Id, int* Out) = 0;
    // slot 7 FUN_06d082d1
    virtual GENRESULT DACOM_API GetDeviceInfo(void* Out520) = 0;
    // slot 8 FUN_06d083d2
    virtual GENRESULT DACOM_API QueryDeviceAbility(unsigned int Index, unsigned long* Out) = 0;
    // slot 9 FUN_06d08461
    virtual GENRESULT DACOM_API GetDisplayModeCount(unsigned int* Out) = 0;
    // slot 10 FUN_06d084f2
    virtual GENRESULT DACOM_API GetDisplayModeEnum(unsigned int Index, void* Out) = 0;
    // slot 11 FUN_06d0869d
    virtual GENRESULT DACOM_API SelectMode(int* Mode, unsigned int* OutIndex) = 0;
    // slot 12 FUN_06d08811
    virtual GENRESULT DACOM_API CreateBuffers(HWND Hwnd, int* Mode, int* OutMode) = 0;
    // slot 13 LAB_06d098f3
    virtual GENRESULT DACOM_API GetModeBlob(void* OutAdapter, void* OutMode10Dwords) = 0;
    // slot 14 FUN_06d09982
    virtual GENRESULT DACOM_API DestroyGpuResources() = 0;
    // slot 15 FUN_06d09b4c RET 0xC
    virtual GENRESULT DACOM_API ClearBuffers(unsigned long Flags, const void* Rects) = 0;
    // slot 16 FUN_06d09d28
    virtual GENRESULT DACOM_API SwapBuffers() = 0;
    // slot 17 FUN_06d09f96 RET 0x8
    virtual GENRESULT DACOM_API LockBuffer(void* Out) = 0;
    // slot 18 FUN_06d0a6c1
    virtual GENRESULT DACOM_API UnlockBuffer() = 0;
    // slot 19 FUN_06d0a7ec RET 0xC — this + Out + unread Unused (body never
    // touches the second stack dword; every retail caller still pushes it).
    virtual GENRESULT DACOM_API GetBufferInterface(void* Out, void* Unused) = 0;
    // slot 20 FUN_06d0a863
    virtual GENRESULT DACOM_API GetDeviceStats(void* Out) = 0;
    // slot 21 FUN_06d0a8d0 RET 0x14
    virtual GENRESULT DACOM_API SetViewportRect(int X, int Y, int W, int H) = 0;
    // slot 22 FUN_06d0aabc RET 0x14
    virtual GENRESULT DACOM_API GetViewportRect(int* OutX, int* OutY, int* OutW, int* OutH) = 0;
    // slot 23 FUN_06d0abb2
    virtual GENRESULT DACOM_API SetDepthRange(float MinZ, float MaxZ) = 0;
    // slot 24 FUN_06d0aca6
    virtual GENRESULT DACOM_API GetDepthRange(float* OutMinZ, float* OutMaxZ) = 0;
    // slot 25 FUN_06d0ad7d RET 0x18
    virtual GENRESULT DACOM_API SetWindow(HWND Hwnd, int X, int Y, int W, int H) = 0;
    // slot 26 FUN_06d0ae23
    virtual GENRESULT DACOM_API GetWindow(HWND* OutHwnd, int* OutLeft, int* OutRight, int* OutW,
                                          int* OutH) = 0;
    // slot 27 FUN_06d0b09d
    virtual GENRESULT DACOM_API SetWorld(const float* M12) = 0;
    // slot 28 FUN_06d0b3dd RET 0x8
    virtual GENRESULT DACOM_API GetWorld(float* Out12) = 0;
    // slot 29 FUN_06d0b607 RET 0x8
    virtual GENRESULT DACOM_API SetView(const float* M12) = 0;
    // slot 30 FUN_06d0b939 RET 0x8
    virtual GENRESULT DACOM_API GetView(float* Out12) = 0;
    // slot 31 FUN_06d0bb58
    virtual GENRESULT DACOM_API SetModelview(const float* M12) = 0;
    // slot 32 FUN_06d0bf39 RET 0x8
    virtual GENRESULT DACOM_API GetModelview(float* Out12) = 0;
    // slot 33 FUN_06d0c29e RET 0x8
    virtual GENRESULT DACOM_API SetProjection(const float* M16) = 0;
    // slot 34 FUN_06d0c438
    virtual GENRESULT DACOM_API GetProjection(float* Out16) = 0;
    // slot 35 FUN_06d0c57c
    virtual GENRESULT DACOM_API SetLookat(float EyeX, float EyeY, float EyeZ, float AtX, float AtY,
                                          float AtZ, float UpX, float UpY, float UpZ) = 0;
    // slot 36 FUN_06d0c9c2
    virtual GENRESULT DACOM_API SetOrtho(float Left, float Right, float Bottom, float Top,
                                         float NearPlane, float FarPlane) = 0;
    // slot 37 FUN_06d0cb25
    virtual GENRESULT DACOM_API SetPerspective(float FovDeg, float Aspect, float NearPlane,
                                             float FarPlane) = 0;
    // slot 38 FUN_06d0ce57 RET 0xC
    virtual GENRESULT DACOM_API SetLight(void* Handle, const D3DLIGHT8* Light) = 0;
    // slot 39 FUN_06d0cf9c RET 0x8
    virtual GENRESULT DACOM_API DestroyLight(void* Handle) = 0;
    // slot 40 FUN_06d0d044
    virtual GENRESULT DACOM_API GetLight(void* Handle, D3DLIGHT8* OutLight) = 0;
    // slot 41 FUN_06d0d0d7
    virtual GENRESULT DACOM_API SetLightEnable(void* Handle, int Enable) = 0;
    // slot 42 FUN_06d0d157
    virtual GENRESULT DACOM_API GetLightEnable(void* Handle, int* OutEnable) = 0;
    // slot 43 FUN_06d0d1dc
    virtual GENRESULT DACOM_API SetMaterial(const D3DMATERIAL8* Material) = 0;
    // slot 44 FUN_06d0d310
    virtual GENRESULT DACOM_API GetMaterial(D3DMATERIAL8* Material) = 0;
    // slot 45 FUN_06d0d628
    virtual GENRESULT DACOM_API CreateTexture(int Width, int Height, unsigned int* Format,
                                              unsigned int Levels, unsigned int Flags,
                                              void** OutHandle) = 0;
    // slot 46 FUN_06d0d997
    virtual GENRESULT DACOM_API DestroyTexture(void* Handle) = 0;
    // slot 47 FUN_06d0dc51
    virtual GENRESULT DACOM_API IsTexture(void* Handle) = 0;
    // slot 48 FUN_06d0dce3
    virtual GENRESULT DACOM_API LockTexture(void* Handle, int Level, void* Out) = 0;
    // slot 49 FUN_06d0e1af
    virtual GENRESULT DACOM_API UnlockTexture(void* Handle, int Level) = 0;
    // slot 50 FUN_06d0e3d9 RET 0xC
    virtual GENRESULT DACOM_API GetTextureFormat(void* Handle, int* OutFormat) = 0;
    // slot 51 FUN_06d0e6d4 RET 0x14 -- four arguments, not three. 06d0e972/06d0e97a write the
    // width and the height, 06d0e9b1/06d0e9e4 write the level count (6 for a cube texture).
    virtual GENRESULT DACOM_API GetTextureDim(void* Handle, unsigned int* OutW, unsigned int* OutH,
                                              int* OutLevels) = 0;
    // slot 52 FUN_06d0ea0b RET 0x10 — this + Handle + two unread stack dwords.
    virtual GENRESULT DACOM_API GetTextureInterface(void* Handle, void* UnusedA,
                                                    void* UnusedB) = 0;
    // slot 53 FUN_06d0ea78
    virtual GENRESULT DACOM_API SetTextureLevelData(void* Handle, int Level, int SrcW, int SrcH,
                                                    unsigned int Pitch, unsigned int* SrcFormat,
                                                    const void* Pixels, void* A, void* B) = 0;
    // slot 54 FUN_06d0f1bc. RET 0x34 = 13 stdcall stack dwords (this + 12 explicit),
    // confirmed by instruction-level trace of rp8.dll.txt @ 22029-22437. Only 4 of the
    // 12 explicit dwords are ever read by retail: SrcHandle, SrcLevel, DstHandle,
    // DstLevel (roles confirmed the same way LockTexture/UnlockTexture's Extra==0/1
    // GetSurfaceLevel/GetCubeMapSurface branch was confirmed). The remaining 8 dwords
    // sit as two untouched 4-dword runs immediately after SrcLevel and after DstLevel
    // -- exactly sizeof(RECT) passed by value each -- matching an earlier RE note's "Круг
    // 2/3" (Handle,Level,Rect) x2 hypothesis, but retail's own FUN_06d2fd77 blit call
    // is invoked unconditionally with NULL rect pointers/counts (0x06d0f649), so those
    // 8 dwords are provably dead code in this build regardless of what they represent.
    // No caller of this vtable slot was found in any available disasm (rp8/shading/
    // rendcomp/ximage/freelancer.exe), so the two RECT-by-value slots are intentionally
    // NOT added to this signature (would be unfalsifiable ABI cosplay for parameters
    // that are read by nobody, on either side). If a caller ever surfaces requiring
    // partial-rect blit, extend this signature then -- do not guess it now.
    // arity-ok: decl width 0x14 vs retail RET 0x34 is intentional (see above).
    // Direction (RC-3 2026-09-30): FUN_06d2fd77 = D3DXLoadSurfaceFromSurface(pDest, 0, 0,
    // pSrc, 0, 0, D3DX_FILTER_POINT, 0) with pDest = the FIRST handle's surface (EBP+0xC/+0x10)
    // and pSrc = the SECOND's (EBP+0x24/+0x28): the first pair is the destination.
    virtual GENRESULT DACOM_API BlitTexture(void* DstHandle, int DstLevel, void* SrcHandle, int SrcLevel) = 0;
    // slot 55 FUN_06d0f698 RET 0x10 — this + Handle + two unread stack dwords.
    virtual GENRESULT DACOM_API SetRenderTarget(void* Handle, void* UnusedA,
                                                void* UnusedB) = 0;
    // slot 56 FUN_06d0f70f
    virtual GENRESULT DACOM_API GetRenderTarget(void* Handle) = 0;
    // slot 57 FUN_06d0f786
    virtual GENRESULT DACOM_API BeginScene() = 0;
    // slot 58 FUN_06d0f8b5
    virtual GENRESULT DACOM_API EndScene() = 0;
    // slot 59 FUN_06d0f9e4
    virtual GENRESULT DACOM_API ResetRenderStatesToDefaults() = 0;
    // slot 60 FUN_06d0fa68
    virtual GENRESULT DACOM_API SetRenderState(D3DRENDERSTATETYPE State, int Value) = 0;
    // slot 61 FUN_06d0fb44
    virtual GENRESULT DACOM_API GetRenderState(D3DRENDERSTATETYPE State, int* Out) = 0;
    // slot 62 FUN_06d0fbcf
    virtual GENRESULT DACOM_API SetTextureStageState(int Stage, int Type, int Value) = 0;
    // slot 63 FUN_06d0fcd5
    virtual GENRESULT DACOM_API GetTextureStageState(int Stage, int Type, int* Out) = 0;
    // slot 64 FUN_06d0fdd2
    virtual GENRESULT DACOM_API SetTextureStageTransform(int Stage, const float* Matrix16) = 0;
    // slot 65 FUN_06d0ff7d
    virtual GENRESULT DACOM_API GetTextureStageTransform(int Stage, float* OutMatrix16) = 0;
    // slot 66 FUN_06d100d3
    virtual GENRESULT DACOM_API SetTextureStageTexture(int Stage, void* Handle) = 0;
    // slot 67 FUN_06d10247
    virtual GENRESULT DACOM_API GetTextureStageTexture(int Stage, void** OutHandle) = 0;
    // slot 68 FUN_06d10322
    virtual GENRESULT DACOM_API VerifyState() = 0;
    // slot 69 FUN_06d1067f RET 0x18
    virtual GENRESULT DACOM_API DrawPrimitive(unsigned long Prim, unsigned int Fvf, const void* Verts,
                                              unsigned int Count, void* Unused) = 0;
    // slot 70 FUN_06d1097d RET 0x20 -- eight dwords, so this plus SEVEN arguments, the last
    // one unread by the callee just like slot 72's. Freelancer.exe 0x004217b6 and 0x00421973
    // both push eight: PUSH EBX (the trailing dword), index count, indices, vertex count,
    // vertices, PUSH 0x142 (an FVF: XYZ|DIFFUSE|TEX1), PUSH 2 or 4 (the primitive type), this.
    virtual GENRESULT DACOM_API DrawIndexedPrimitiveUp(unsigned long Prim, unsigned int Fvf,
                                                       const void* Verts, unsigned int VertCount,
                                                       const void* Indices,
                                                       unsigned int IndexCount,
                                                       unsigned long Unused) = 0;
    // slot 71 FUN_06d10c89 RET 0x18
    virtual GENRESULT DACOM_API DrawPrimitiveVb(unsigned long Prim, void* Vb, unsigned int StartVertex,
                                                unsigned int Count, void* Unused) = 0;
    // slot 72 FUN_06d10e4b RET 0x20 -- eight dwords, so this plus SEVEN arguments. Ghidra
    // lists only param_1..param_7 because the last one is never read, but the callee pops it
    // all the same and every retail caller pushes it: shading.dll 0x06eb328a, 0x06eb3c0f,
    // 0x06eb5915 and 0x06eba8ff each open with PUSH 0x0 and push eight dwords in total.
    // Declaring six here made ShadingInternal.h's PipeDrawIndexedVb underfill the stack.
    virtual GENRESULT DACOM_API DrawIndexedPrimitiveVb(unsigned long Prim, void* Vb,
                                                       unsigned int BaseVertex, unsigned int NumVerts,
                                                       const void* Indices,
                                                       unsigned int IndexCount,
                                                       unsigned long Unused) = 0;
    // slot 73 FUN_06d0ccb2
    virtual GENRESULT DACOM_API AddLight(void* Handle) = 0;
    // slot 74 FUN_06d0cd32
    virtual GENRESULT DACOM_API RemoveLight(void* Handle) = 0;
    // slot 75 FUN_06d0cddb
    // FUN_06d0cddb(int,uint) forwards its uint arg unchanged into FUN_06d2ca43,
    // which passes it straight through to FUN_06d2e9f0's tree lookup on
    // DAT_06d728cc -- the SAME tree that FUN_06d2c487/FUN_06d2c7cf (called from
    // AddLight/SetLightEnable/DestroyLight/RemoveLight) exclusively insert/erase
    // keyed by the raw Handle pointer value (rp8.dll.c:21709/21828/22012/22037-39;
    // confirmed by tracing FUN_06d2cb6f/FUN_06d2c487's own key locals, which copy
    // the incoming `int*` Handle param by value -- never dereference it -- before
    // taking its address as the generic comparison key). Since that tree is never
    // populated with plain D3D indices, a genuine index key here would make
    // UpdateLight permanently fail to find anything; Ghidra's `uint` typing of
    // this parameter is a decompiler artifact (no dereference inside FUN_06d2ca43
    // gave it a pointer cue), not evidence of a second key domain. Handle
    // confirmed correct 2026-09-10 (rp8.dll module, item 0x06d2ca43 notes).
    virtual GENRESULT DACOM_API UpdateLight(void* Handle) = 0;
    // slot 76 FUN_06d0af55
    virtual GENRESULT DACOM_API SetWorldN(int WorldIndex, const float* M12) = 0;
};

// The source-vertex descriptor IVertexBufferManager::CopyVertexData (slot 8) takes as its third
// argument: retail rp8.dll FUN_06d1f040's param_3. 0x70 bytes -- a flags word, five 0x14-byte
// streams and the source FVF with its vertex count. The client builders write every dword of it
// (freelancer.exe FUN_0051feb0 for the asteroid field, FUN_00406510 for the part cache, both
// with Flags 2 and Count in each stream); RP8's converters read Flags, SrcFvf and each stream's
// Base / Stride / IndexArray (the generic fallbacks also a texcoord stream's ByteSize), never a
// stream's Count or the trailing SrcCount.
struct FRpVertexStream
{
    const unsigned char* Base;       // +0x00
    unsigned int ByteSize;           // +0x04  bytes of the element (0xc position, 4 colour, ...)
    unsigned int Stride;             // +0x08  bytes between two vertices of this stream
    const unsigned long* IndexArray; // +0x0c  one DWORD per vertex when Flags & 4 (indexed)
    unsigned int Count;              // +0x10  vertices in the stream
};

struct FRpVertexSrcDesc
{
    unsigned int Flags;              // +0x00  2: interleaved source, 4: indexed (family B)
    FRpVertexStream Position;        // +0x04
    FRpVertexStream Normal;          // +0x18
    FRpVertexStream Diffuse;         // +0x2c
    FRpVertexStream Tex[2];          // +0x40 / +0x54  texcoord sets 0 and 1
    unsigned int SrcFvf;             // +0x68
    unsigned int SrcCount;           // +0x6c
};

static_assert(sizeof(FRpVertexStream) == 0x14, "FRpVertexStream is 0x14 bytes");
static_assert(sizeof(FRpVertexSrcDesc) == 0x70, "FRpVertexSrcDesc is 0x70 bytes");
static_assert(offsetof(FRpVertexSrcDesc, Normal) == 0x18, "FRpVertexSrcDesc +0x18 normal");
static_assert(offsetof(FRpVertexSrcDesc, Diffuse) == 0x2c, "FRpVertexSrcDesc +0x2c diffuse");
static_assert(offsetof(FRpVertexSrcDesc, Tex) == 0x40, "FRpVertexSrcDesc +0x40 texcoords");
static_assert(offsetof(FRpVertexSrcDesc, SrcFvf) == 0x68, "FRpVertexSrcDesc +0x68 source FVF");

// Borrowed vertex-buffer handle and draw offset.
struct FVbListing
{
    void* VertexBuffer;
    unsigned long Fvf;
    unsigned long BaseVertex;
};

// RP8 writes the listing and locked memory; materials borrow the listing prefix.
struct FVertexMemoryLock
{
    FVbListing Listing;
    void* Vertices;
};
static_assert(sizeof(FVbListing) == 0xC, "vertex listing three words");
static_assert(offsetof(FVertexMemoryLock, Vertices) == 0xC, "vertex memory after listing");
static_assert(sizeof(FVertexMemoryLock) == 0x10, "vertex memory lock four words");

struct IVertexBufferManager : IDAComponent
{
    // slot 3 FUN_06d11354 RET 0x14 -- four explicit dwords, and the body reads NONE of them:
    // it is `return 0`. Declared to the width anyway, because the width is the contract.
    virtual GENRESULT DACOM_API VbMgrInit(void* A, void* B, void* C, void* D) = 0;
    // slot 4 FUN_06d1135d
    virtual GENRESULT DACOM_API VbMgrReleaseAll() = 0;
    // slot 5 FUN_06d114ea RET 0x10
    virtual GENRESULT DACOM_API LockVertexMemory(unsigned int Fvf, unsigned int Count, FVertexMemoryLock* Out) = 0;
    // slot 6 FUN_06d11877 RET 0x8
    virtual GENRESULT DACOM_API UnlockVertexMemory(FVertexMemoryLock* Out) = 0;
    // slot 7 FUN_06d118bc return 0xFFFFFFF9
    virtual GENRESULT DACOM_API UnsupportedManagerOperation() = 0; // formerly VbMgrSlot7
    // slot 8 FUN_06d114c5 RET 0x18
    virtual GENRESULT DACOM_API CopyVertexData(void* Dest, unsigned int Fvf,
                                              const FRpVertexSrcDesc* Source,
                                              unsigned int StartVertex, unsigned int Count) = 0;
};

struct IRPDraw : IDAComponent
{
    // slot 3 FUN_06d111e1 RET 0x18
    virtual GENRESULT DACOM_API DrawIndexedPrimitive(unsigned long Prim, unsigned int MinIndex,
                                                     unsigned int NumVertices,
                                                     unsigned int StartIndex,
                                                     unsigned int Count) = 0;
};

struct IRPIndexBuffer : IDAComponent
{
    // slot 3 FUN_06d12d4e RET 0x10 -- THREE explicit dwords, not two. The body reads only
    // the first two; the trailing one mirrors CreateVb's Flags and is ignored on the index
    // path. Every caller in this tree used to push two and leave the callee popping four.
    virtual GENRESULT DACOM_API CreateIb(unsigned int Count, unsigned int** Out,
                                         unsigned int Flags) = 0;
    // slot 4 FUN_06d13002 RET 0x8 (the vtable entry is 06d13002; 06d13070 is an instruction
    // inside it, not the function start). The argument is a POINTER TO the caller's handle:
    // 06d13070 reads [Handle] and compares it against -1, 06d13087 writes 0 back through it.
    // Passing the handle by value makes the callee read the wrapper's first field as the
    // handle and then zero it.
    virtual GENRESULT DACOM_API DestroyIb(void** Handle) = 0;
    // slot 5 FUN_06d131b2
    virtual GENRESULT DACOM_API ResizeIb(void* Handle, unsigned int Count) = 0;
    // slot 6 FUN_06d13847 RET 0x14
    virtual GENRESULT DACOM_API CopyIndices(void* Handle, unsigned int* OutOffset, const unsigned short* SrcData, unsigned int Count) = 0;
    // slot 7 FUN_06d134ec — Handle==-1 resolves to ADDRESS this+0x140 (inline scratch
    // FRp8Ib at object+0x14C created in create_buffers), not a dereference of *(this+0x140).
    virtual GENRESULT DACOM_API LockIb(void* Handle, unsigned int* Offset, void** Out,
                                       unsigned int Count) = 0;
    // slot 8 FUN_06d13794 RET 0x8 (again, the vtable entry, not the inner address 06d136e0)
    virtual GENRESULT DACOM_API UnlockIb(void* Handle) = 0;
    // slot 9 FUN_06d13bce RET 0x14 -- FOUR explicit dwords. The FDUMP string in the body
    // names it select_ib(). It reads the first two: the handle, and a scalar it caches next to
    // the handle at this+0x12C (+8 = buffer, +0xC = this value, bit 1 of +0x10 = dirty). The
    // D3D8 shape of that pair is SetIndices(pIndexData, BaseVertexIndex), which is what the
    // port's own RP8::SelectIb writes -- it just had no argument to put there and hardcoded 0.
    // Arguments 3 and 4 are never read; Deformable2 0x065f6a55 pushes literal zeros for them.
    virtual GENRESULT DACOM_API SelectIb(void* Handle, unsigned int BaseVertexIndex,
                                         unsigned int Unused3, unsigned int Unused4) = 0;
    // slot 10 FUN_06d13d23 RET 0xC -- TWO explicit dwords, and the second is an out-param:
    // the body writes [buffer+0x18] through it and returns GR_OK, or 0x80004005 when it cannot.
    // The port declared one argument and returned the capacity AS the result code.
    virtual GENRESULT DACOM_API GetIbCapacity(void* Handle, unsigned int* OutCapacity) = 0;
    // slot 11 FUN_06d13b69
    virtual GENRESULT DACOM_API IsIbValid(void* Handle) = 0;
};

struct IRPVertexBuffer : IDAComponent
{
    // slot 3 FUN_06d118c8 RET 0x14
    virtual GENRESULT DACOM_API CreateVb(unsigned int Fvf, unsigned int Count, unsigned int** Out,
                                         unsigned int Flags) = 0;
    // slot 4 FUN_06d11db3 -- same shape as DestroyIb: a POINTER TO the caller's handle.
    // 06d11e24 reads [Handle], 06d11e38 writes 0 back through it.
    virtual GENRESULT DACOM_API DestroyVb(void** Handle) = 0;
    // slot 5 FUN_06d11f78
    virtual GENRESULT DACOM_API ResizeVb(void* Handle, unsigned int Fvf, unsigned int Count) = 0;
    // slot 6 FUN_06d1228c RET 0x18
    virtual GENRESULT DACOM_API CopyVertices(void* Handle, unsigned int* OutOffset, const FRpVertexSrcDesc* SrcData, unsigned int StartVertex, unsigned int Count) = 0;
    // slot 7 FUN_06d126bb RET 0x14
    virtual GENRESULT DACOM_API LockVb(void* Handle, unsigned int* Offset, void** Out,
                                       unsigned int Count) = 0;
    // slot 8 FUN_06d12a8b
    virtual GENRESULT DACOM_API UnlockVb(void* Handle) = 0;
    // slot 9 FUN_06d12b30 RET 0x8 -- one explicit dword, unread; the body is
    // `return 0xFFFFFFF9` ("unsupported"). Nothing in the tree calls it.
    virtual GENRESULT DACOM_API UnsupportedVertexBufferOperation(void* Handle) = 0; // formerly VbSlot9
    // slot 10 FUN_06d12b9d RET 0x8
    virtual GENRESULT DACOM_API SelectVb(void* Handle) = 0;
    // slot 11 FUN_06d12cdc RET 0x10
    virtual GENRESULT DACOM_API GetVbInfo(void* Handle, unsigned int* OutFvf, unsigned int* OutCount) = 0;
    // slot 12 FUN_06d12b3c
    virtual GENRESULT DACOM_API IsVbValid(void* Handle) = 0;
};

// Slot order from rp8.dll's IGammaControl vtables (.rdata:06d5e200 and 06d5e540, identical
// from slot 3 on): 06d153e6, 06d1560c, 06d15757, 06d1535d, 06d153a4. Confirmed by the only exe
// caller FUN_00424510: +0x18 (slot 6) with one byte argument, +0x0c (slot 3) with five
// (7, gamma, 0, 1.0f, 0). The earlier declaration order (enable/get-enable first) was wrong.
struct IGammaControl : IDAComponent
{
    // slot 3 FUN_06d153e6. Retail RET 0x18 pops 6 stack dwords (this + 5 explicit args): a
    // channel bitmask (bit0=red/bit1=green/bit2=blue) plus 4 floats consumed straight off the
    // caller's stack (Ghidra never recovered named parameters for them, only "Stack[...]"
    // slots) that drive a per-channel curve: value = clamp(Brightness + pow(Contrast*x + Bias,
    // 1/Gamma), 0, 1) for x = i/255, i in [0,256). The previous single `void* Channels`
    // placeholder didn't carry the 4 floats retail actually reads and has been replaced.
    virtual GENRESULT DACOM_API SetGammaFunction(unsigned int ChannelMask, float Gamma,
                                                 float Brightness, float Contrast,
                                                 float Bias) = 0;
    // slot 4 FUN_06d1560c. ChannelMask selects exactly one channel (1/2/4); RampData points at
    // that channel's 256 WORD source values.
    virtual GENRESULT DACOM_API SetGammaRamp(unsigned int ChannelMask,
                                             const unsigned short* RampData) = 0;
    // slot 5 FUN_06d15757. Channel selects exactly one channel (1/2/4); OutRamp receives that
    // channel's 256 WORD values.
    virtual GENRESULT DACOM_API GetGammaRamp(unsigned int Channel, unsigned short* OutRamp) = 0;
    // slot 6 FUN_06d1535d. Retail param_2 is a byte (only bit0 is read); RET pops one stack
    // dword beyond the implicit this, i.e. exactly one explicit argument -- no hidden out-param.
    virtual GENRESULT DACOM_API SetGammaEnable(unsigned char Enable) = 0;
    // slot 7 FUN_06d153a4. Retail takes ZERO explicit stack arguments (RET pops only the
    // implicit this) -- the enabled/disabled state is communicated purely via the return code
    // (GR_OK enabled, GR_GENERIC not-enabled, 0xFFFFFFFD unsupported), nothing is written
    // through an out-pointer. The previous `void* OutEnable` placeholder param didn't exist
    // in retail and has been removed.
    virtual GENRESULT DACOM_API GetGammaEnable() = 0;
};

struct IRPTexture : IDAComponent
{
    // slot 3 FUN_06d13d70 (RET 0xc: this + 2 args). ParentFileSystem != 0 -> FDUMP warning
    // "IFilesystem parent not yet supported" and GR_INVALID_PARMS (06d13de8); FileName == 0 ->
    // "filename not specified" and GR_INVALID_PARMS (06d13e47). FileName is a char path
    // (strrchr '.' at 06d13ebb). Freelancer.exe FUN_00425170 calls it as (0, path) (00425254).
    virtual GENRESULT DACOM_API PrintScreen(void* ParentFileSystem, const char* FileName) = 0;
    // slot 4 FUN_06d148d4 RET 0x10: opens FileName through FileSystem (+0x7C with a
    // "FileSystem" descriptor), maps it (+0x3C/+0x40) and decodes the view with
    // D3DXCreateTextureFromFileInMemory; *OutHandle receives the texture handle.
    virtual GENRESULT DACOM_API LoadTexture(IFileSystem* FileSystem, const char* FileName,
                                            void** OutHandle) = 0;
    // slot 5 FUN_06d1455e RET 0x14: Level is the mip level of a 2D handle or the cube face of a
    // cube handle (get_subsurface); SrcSize/SrcData are an encoded image in memory, loaded into
    // that subsurface with D3DXLoadSurfaceFromFileInMemory (06d147f6 pushes param_5 as pSrcData
    // and param_4 as SrcDataSize).
    virtual GENRESULT DACOM_API LoadSurfaceData(void* Handle, int Level, unsigned int SrcSize,
                                                const void* SrcData) = 0;
    // slot 6 FUN_06d152e4 RET 0x10: D3DXGetImageInfoFromFileInMemory(SrcData, SrcSize, Info).
    // Shading's load_dds_mips (06ebfe74) calls it and then compares Info.ImageFileFormat with 4
    // (D3DXIFF_DDS).
    virtual GENRESULT DACOM_API GetImageInfo(_D3DXIMAGE_INFO* Info, unsigned int SrcSize,
                                             const void* SrcData) = 0;
    // slot 7 FUN_06d14dd7 RET 0x10: load_texture for a cube map (TEXTURE_CUBEMAPS ability,
    // D3DXCreateCubeTextureFromFileInMemory). Shading 06ec0399 calls it as
    // (FileSystem, "CUBE", OutHandle).
    virtual GENRESULT DACOM_API LoadCubemap(IFileSystem* FileSystem, const char* FileName,
                                            void** OutHandle) = 0;
};
