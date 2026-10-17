// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Public ABI of ReadFile.dll -- the DACOM FileSystem component (DOS / UTF / MEM)
// and SearchPath. Every module that touches game data calls through this; it is
// declared here and nowhere else (docs/re/cross_dll_protocol.md).

#include "dacom.h"

#include <cstddef>

// Digital Anvil FileSystem AGGDESC (size 0x30 or 0x34). DALib::OpenData / ReadFile.dll.c.
struct FFileSystemDesc
{
    unsigned long Size;              // +0x00  0x30 or 0x34
    const char* ComponentName;       // +0x04  "FileSystem"
    const char* Implementation;      // +0x08  "DOS" / "UTF" / "MEM"
    const char* Path;                // +0x0C
    unsigned long Access;            // +0x10  GENERIC_READ 0x80000000
    unsigned long ShareMode;         // +0x14  FILE_SHARE_READ
    void* Security;                  // +0x18
    unsigned long Creation;          // +0x1C  OPEN_EXISTING 3
    unsigned long Flags;             // +0x20  0x08000080
    void* TemplateFile;              // +0x24
    IDAComponent* Parent;            // +0x28
    unsigned long Extra;             // +0x2C
    unsigned long Field30;           // +0x30  0xFFFFFFFF when Size==0x34
    void* MemBuffer;                 // +0x34  MEM size 0x40 only (FUN_06b777f0)
    unsigned long MemSize;           // +0x38
    unsigned long MemFlags;          // +0x3C  bit0=wrap pointer, bit1=flag+1
};

// Callers fill `Size` (0x30 / 0x34 for DOS and UTF, 0x40 for MEM) and the callee reads
// only that much, so the DOS-sized prefix layout is the invariant that matters.
static_assert(offsetof(FFileSystemDesc, Field30) == 0x30, "DOS/UTF desc prefix is 0x34");
static_assert(sizeof(FFileSystemDesc) == 0x40, "MEM tail extends the desc to 0x40");

// File-system instances also expose their component factory.
struct IFileSystem : IComponentFactory
{
    virtual int DACOM_API CloseFile(unsigned long Handle) = 0;                          // +0x10
    virtual int DACOM_API Read(unsigned long Handle, void* Buffer, unsigned long Size,
                               unsigned long* BytesRead, unsigned long Extra) = 0;      // +0x14
    virtual int DACOM_API Write(unsigned long Handle, const void* Buffer, unsigned long Size,
                                 unsigned long* BytesWritten, const void* PositionOverride) = 0; // formerly Slot18
    virtual int DACOM_API RejectOperation1C(unsigned long A1, unsigned long A2, unsigned long A3,
                                 unsigned long A4) = 0; // formerly Slot1C; TODO: original operation contract.
    virtual int DACOM_API SeekFile(unsigned long Handle, unsigned long OffLow, unsigned long* OffHighOut,
                                  unsigned long Origin) = 0; // formerly Slot20
    virtual int DACOM_API SetEndOfFile(unsigned long A1) = 0; // formerly Slot24
    virtual unsigned long DACOM_API GetSize(unsigned long Handle, unsigned long* HighOut) = 0; // +0x28
    virtual int DACOM_API ReturnOne2C(unsigned long A1, unsigned long A2, unsigned long A3,
                                 unsigned long A4, unsigned long A5) = 0; // formerly Slot2C; TODO: original operation contract.
    virtual int DACOM_API ReturnOne30(unsigned long A1, unsigned long A2, unsigned long A3,
                                 unsigned long A4, unsigned long A5) = 0; // formerly Slot30; TODO: original operation contract.
    virtual int DACOM_API GetFileTimes(unsigned long Handle, FILETIME* CreationOut, FILETIME* AccessOut,
                                  FILETIME* WriteOut) = 0; // formerly Slot34
    virtual int DACOM_API RejectOperation38(unsigned long A1, unsigned long A2, unsigned long A3,
                                 unsigned long A4) = 0; // formerly Slot38; TODO: original operation contract.
    virtual int DACOM_API CreateMapping(unsigned long A1, unsigned long A2, unsigned long A3,
                                 unsigned long A4, unsigned long A5, unsigned long A6) = 0; // formerly Slot3C
    virtual int DACOM_API MapFileView(unsigned long A1, unsigned long A2, unsigned long A3,
                                 unsigned long A4, unsigned long A5) = 0; // formerly Slot40
    virtual int DACOM_API UnmapFileView(unsigned long A1) = 0; // formerly Slot44
    virtual int DACOM_API FindFirst(const char* Pattern, WIN32_FIND_DATAA* FindData) = 0; // formerly Slot48
    virtual int DACOM_API FindNext(unsigned long Handle, WIN32_FIND_DATAA* FindData) = 0; // formerly Slot4C
    virtual int DACOM_API FindClose(unsigned long Handle) = 0; // formerly Slot50
    virtual int DACOM_API RejectOperation54(const char* Name, unsigned long Extra) = 0; // formerly Slot54; TODO: original operation contract.
    virtual int DACOM_API RejectOperation58(unsigned long A1) = 0; // formerly Slot58; TODO: original operation contract.
    virtual int DACOM_API GetCurrentPath(unsigned long MaxChars, char* Out) = 0;        // +0x5C
    virtual int DACOM_API OpenNode(const char* Name) = 0;                               // +0x60
    virtual int DACOM_API RejectOperation64(unsigned long A1) = 0; // formerly Slot64; TODO: original operation contract.
    virtual int DACOM_API RejectOperation68(unsigned long A1, unsigned long A2, unsigned long A3) = 0; // formerly Slot68; TODO: original operation contract.
    virtual int DACOM_API RejectOperation6C(unsigned long A1, unsigned long A2) = 0; // formerly Slot6C; TODO: original operation contract.
    virtual int DACOM_API GetPathAttributes(const char* Path) = 0; // formerly Slot70
    virtual int DACOM_API RejectOperation74(unsigned long A1, unsigned long A2) = 0; // formerly Slot74; TODO: original operation contract.
    virtual int DACOM_API GetFileSystemError() = 0; // formerly Slot78
    virtual int DACOM_API OpenByDesc(FFileSystemDesc* Desc) = 0;                        // +0x7C
    virtual int DACOM_API GetFilePosition(unsigned long Handle, unsigned long* HighOut) = 0; // formerly Slot80
    virtual void DACOM_API GetFileSysPath(char* Out, unsigned long MaxChars) = 0;       // +0x84
    virtual int DACOM_API GetAccessFlags() = 0; // formerly Slot88
    virtual int DACOM_API GetParentFileSystem(IFileSystem** Out) = 0; // formerly Slot8C
    virtual int DACOM_API FailOperation90(unsigned long A1, unsigned long A2) = 0; // formerly Slot90; TODO: original operation contract.
    virtual int DACOM_API FailOperation94(unsigned long A1, unsigned long A2) = 0; // formerly Slot94; TODO: original operation contract.
    virtual int DACOM_API FailOperation98(unsigned long A1, unsigned long A2, unsigned long A3,
                                 unsigned long A4, unsigned long A5) = 0; // formerly Slot98; TODO: original operation contract.
    virtual int DACOM_API FailOperation9C(unsigned long A1, unsigned long A2, unsigned long A3,
                                 unsigned long A4, unsigned long A5) = 0; // formerly Slot9C; TODO: original operation contract.
    virtual int DACOM_API InvokeLockedCallback(unsigned long A1, unsigned long A2, unsigned long A3) = 0; // formerly SlotA0
    virtual int DACOM_API ResolvePath(char* Dest, const char* Src,
                                 unsigned long DestChars) = 0; // formerly SlotA4
};

// Access to a memory file system's underlying buffer.
struct IDataBuffer : IDAComponent
{
    virtual GENRESULT DACOM_API GetBuffer(void** OutPtr, unsigned long* OutSize) = 0; // +0x0C
};

struct ISearchPath : IComponentFactory
{
    virtual int DACOM_API SetPath(const char* PathList) = 0;                            // +0x10
    virtual int DACOM_API GetPath(char* Dest, unsigned long MaxChars) = 0; // formerly Slot14
};
