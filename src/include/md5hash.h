// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Preserve per-module class exports for header-defined members; bodies live in DALib.
#if defined(DALIB_EXPORTS) || defined(COMMON_EXPORTS) || defined(SERVER_EXPORTS) || defined(FREELANCER_EXPORTS)
#define MD5HASH_API __declspec(dllexport)
#else
#define MD5HASH_API __declspec(dllimport)
#endif

class MD5HASH_API MD5Hash
{
public:
    MD5Hash();
    virtual ~MD5Hash();

    // Retail mangling ?AddData@MD5Hash@@QAE_NQAXK@Z = `void* const` (const pointer to mutable
    // data). The body only reads Data; callers holding const data cast it, as retail's did.
    bool AddData(void* const Data, unsigned long Length);
    bool CalcValue();
    const char* AsString();
    void FromString(const char* Text);
    bool Compare(MD5Hash& Other);

    // Server .text:06ce14e0: LEA EAX,[this + 0x4]; RET.
    unsigned char* GetStatePtr()
    {
        return (unsigned char*)State;
    }

protected:
    void Transform(unsigned char* Block);
    void Encode(unsigned char* Output, unsigned long* Input, unsigned long Length);
    void Decode(unsigned long* Output, unsigned char* Input, unsigned long Length);

    unsigned long State[4];         // +0x04
    unsigned long BitCountLow;      // +0x14
    unsigned long BitCountHigh;     // +0x18
    unsigned char Buffer[64];       // +0x1c
    char StringValue[128];          // +0x5c
};

// Server .text:06ce15da: PUSH 0xdc is the element size its vector deleting
// destructor hands the array helper.
static_assert(sizeof(MD5Hash) == 0xdc, "MD5Hash sizeof 0xdc");
