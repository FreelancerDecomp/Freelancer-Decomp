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
#include <cstdint>
#include <windows.h> // FILETIME, WIN32_FIND_DATAA

// FFileSystemDesc, the FileSystem AGGDESC, is in dacom.h with the other descriptors.

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
    // Returns the view's address. std::intptr_t is retail's int on Win32; on x64 the address
    // needs 64 bits (docs/x64.md). The other handle values here stay 32-bit on x64: ReadFile
    // hands out ids for them (handle_value.h).
    virtual std::intptr_t DACOM_API MapFileView(unsigned long A1, unsigned long A2, unsigned long A3,
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
