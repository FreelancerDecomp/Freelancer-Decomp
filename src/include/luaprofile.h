// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"

// Lua values and the profile-parser interface shared with dacom's native INI parser.

// Tag 9 carries String; inactive union bytes are outside the source-level contract.
struct FLuaTaggedValue
{
    unsigned int Tag;
    unsigned int Unused;
    union
    {
        char* String;
        unsigned int UInt;
        int Int;
        float Float;
        double Number;
        __int64 LongLong;
    } Payload;
};

// ILua @ object+0. Slot 3 = FUN_06801690 (call Lua global). RET 0x14.
struct ILua : IDAComponent
{
    virtual GENRESULT DACOM_API InvokeGlobal(const char* Name, FLuaTaggedValue* OutValue,
                                             int Argc, FLuaTaggedValue* const* Argv) = 0;
};

typedef int(DACOM_API* FSectionEnumCallback)(void* Parser, const char* Name, void* Context);

// LoadFile and EnumerateSections apply to both INI sections and Lua global tables.
struct IProfileParser : IDAComponent
{
    virtual GENRESULT DACOM_API LoadFile(const char* Path, unsigned int Flags) = 0; // formerly UnnamedSlot3
    virtual int DACOM_API EnumerateSections(FSectionEnumCallback Callback, void* Context) = 0; // formerly UnnamedSlot4
    virtual void* DACOM_API OpenSection(const char* Name, unsigned int Flags) = 0;      // RET 0xC
    virtual int DACOM_API CloseSection(void* Handle) = 0;                               // RET 8
    virtual int DACOM_API ReadLine(void* Handle, int Index, char* Buffer,
                                   unsigned int BufferSize) = 0; // RET 0x14
    virtual int DACOM_API GetString(void* Handle, const char* Key, char* Buffer,
                                    unsigned int BufferSize) = 0; // RET 0x14
};

// The manager supplies its aggregated native INI parser.
inline IProfileParser* AcquireProfileParser()
{
    ICOManager* Dacom = DACOM_Acquire();
    if (Dacom == nullptr)
        return nullptr;
    IProfileParser* Parser = nullptr;
    if (Dacom->QueryInterface(Iids::ProfileParser, (void**)&Parser) != GR_OK || Parser == nullptr)
        return nullptr;
    return Parser;
}
