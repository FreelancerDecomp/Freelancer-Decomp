// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_types.h"

#include "alchemy_common.h"
#include "readfile.h"

#include <cstdio>
#include <cstring>

// alchemy.dll — DACOM particle FX scaffolding.
// Out-of-line method bodies for FAleChunk (the only alchemy_types.h struct
// with non-trivial logic — everything else in that header is pure data or
// abstract-interface declarations with no bodies to place here).
namespace AlchemyImpl
{
    int FAleChunk::OpenNode(const char* Name)
    {
        return Filesys->OpenNode(Name);
    }

    // OpenByDesc opens the same-named data leaf inside the entered directory
    // (UTF layout: \AlchemyNodeLibrary\AlchemyNodeLibrary). This helper supplies
    // the node loaders' current open-failure handling; their full failure and
    // reference-order contract remains pending in FUN_06228b90.
    bool FAleChunk::OpenNamedChunk(IFileSystem* Fs, const char* Name, unsigned char bWrite)
    {
        bEntered_ = 0;
        Filesys = Fs;
        if (Fs == nullptr || Name == nullptr)
            return false;
        const int Entered = OpenNode(Name);
        if (Entered == 0)
            return false;
        bEntered_ = 1;
        if (OpenChunk(Fs, Name, bWrite))
            return true;
        OpenNode(kAleLeaveNode);
        bEntered_ = 0;
        return false;
    }

    void FAleChunk::LeaveNamedNode()
    {
        if (bEntered_ != 0)
        {
            OpenNode(kAleLeaveNode);
            bEntered_ = 0;
        }
    }

    void FAleChunk::DeleteNode(const char* Name)
    {
        Filesys->RejectOperation54(Name, 0);
    }

    bool FAleChunk::OpenChunk(IFileSystem* Fs, const char* Name, unsigned char bWrite)
    {
        Filesys = Fs;
        Handle = 0xFFFFFFFF;
        FFileSystemDesc Desc;
        std::memset(&Desc, 0, sizeof(Desc));
        Desc.Size = kAleDescSize;
        Desc.ComponentName = kAleFileSystemName;
        Desc.Path = Name;
        Desc.Access = bWrite != 0 ? kAleAccessWrite : kAleAccessRead;
        Desc.ShareMode = bWrite != 0 ? 0 : kAleShareRead;
        Desc.Creation = bWrite != 0 ? kAleCreateAlways : kAleOpenExisting;
        Desc.Flags = kAleFileFlags;
        Desc.Field30 = 0xFFFFFFFF;
        const int Result = Filesys->OpenByDesc(&Desc);
        Handle = static_cast<unsigned long>(Result);
        if (Result == -1)
        {
            FDUMP(0x100001, "%s(%d) : ERROR:General:%s", "alchemy.cpp", 0x1E,
                  "Couldn't create write chunk\n");
            return false;
        }
        return true;
    }

    void FAleChunk::CloseChunk()
    {
        if (Handle == 0xFFFFFFFF)
            return;
        Filesys->CloseFile(Handle);
        Handle = 0xFFFFFFFF;
    }

    int FAleChunk::ReadBytes(void* Buffer, unsigned long Size)
    {
        unsigned long Bytes = 0xFFFFFFFF;
        const int Ok = Filesys->Read(Handle, Buffer, Size, &Bytes, 0);
        if (Ok == 0)
            return -1;
        return Bytes == Size ? 0 : -1;
    }

    int FAleChunk::WriteBytes(const void* Buffer, unsigned long Size)
    {
        unsigned long Bytes = 0xFFFFFFFF;
        const int Ok = Filesys->Write(
            Handle, Buffer, Size, &Bytes, nullptr);
        if (Ok == 0)
            return -1;
        return Bytes == Size ? 0 : -1;
    }

    // FUN_06223070: uint16 length + bytes; odd length gets 1 pad byte. RET <0 fail.
    int FAleChunk::ReadString(char* Buffer, unsigned long MaxChars)
    {
        unsigned short Length = 0;
        if (ReadBytes(&Length, 2) < 0)
            return -1;
        if (Length == 0)
        {
            Buffer[0] = '\0';
            return 0;
        }
        if (static_cast<unsigned long>(Length) > MaxChars)
            return -1;
        int Result = ReadBytes(Buffer, Length);
        if ((Length & 1) != 0)
        {
            Result = ReadBytes(&MaxChars, 1);
        }
        return Result;
    }

    // FUN_06222f80: uint16 (strlen+1 or 0) + bytes + odd pad.
    int FAleChunk::WriteString(const char* Text)
    {
        unsigned short Length = 0;
        if (Text != nullptr)
            Length = static_cast<unsigned short>(std::strlen(Text) + 1);
        WriteBytes(&Length, 2);
        if (Length == 0)
            return 0;
        int Result = WriteBytes(Text, Length);
        if ((Length & 1) != 0)
        {
            const unsigned long Pad = 0;
            Result = WriteBytes(&Pad, 1);
        }
        return Result;
    }
} // namespace AlchemyImpl
