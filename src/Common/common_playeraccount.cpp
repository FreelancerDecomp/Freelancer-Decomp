// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll CPlayerAccount: the MD5-hashed player account kept in the registry, and its text
// key. Split out of common_net.cpp.
//
// MD5Hash is in DALib (src/DALib/md5hash.cpp) -- not duplicated here.

#pragma comment(lib, "winmm.lib")  // For timeGetTime() (also used by common_physys.cpp)
#pragma comment(lib, "rpcrt4.lib") // For UuidCreate() in SeedMachineHash

#include "common_net.h"
#include "md5hash.h" // MD5Hash for CPlayerAccount

#include <windows.h>
#include <mmsystem.h> // timeGetTime -- SeedMachineHash
#include <rpc.h>      // UUID / UuidCreate for SeedMachineHash's fallback
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

namespace
{
    const char kAccountKeyPath[] = "Software\\Microsoft\\Microsoft Games\\Freelancer\\1.0";

    // FUN_06306fe0: _vsnprintf into the caller's 0x2000-byte buffer, returning the buffer
    // (no terminator of its own). The text-key functions hand the result to
    // OutputDebugStringA.
    char* FormatDebugLine(char* Buffer, const char* Format, ...)
    {
        va_list Args;
        va_start(Args, Format);
        _vsnprintf(Buffer, 0x2000, Format, Args);
        va_end(Args);
        return Buffer;
    }

    // The tail the text-key checksum uses: Str + strlen(Str) - 4, the last four characters
    // of an MD5 text (inlined at 06306f6d, 063070a5, 063073cf).
    const char* LastFourChars(const char* Str)
    {
        return Str + strlen(Str) - 4;
    }

    // FUN_06306720: seeds Hash with the machine's DigitalProductId (Windows 9x key first,
    // then the NT key; a UuidCreate fallback when neither has one) and the user name
    // (timeGetTime as text when GetUserNameA fails). Always returns true.
    bool SeedMachineHash(MD5Hash& Hash)
    {
        HKEY Key9x = nullptr;
        HKEY KeyNT = nullptr;
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\",
                      0, KEY_QUERY_VALUE, &Key9x);
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows NT\\CurrentVersion\\",
                      0, KEY_QUERY_VALUE, &KeyNT);

        // One size variable for both queries: it is set to 0x400 once (06306776) and the NT
        // query reuses whatever the 9x query left in it.
        bool bGotProductId = false;
        unsigned char ProductId[0x400];
        DWORD ProductIdSize = sizeof(ProductId);
        if (Key9x != nullptr)
        {
            if (RegQueryValueExA(Key9x, "DigitalProductId", nullptr, nullptr, ProductId,
                                 &ProductIdSize) == ERROR_SUCCESS)
                bGotProductId = true;
            RegCloseKey(Key9x);
        }
        if (KeyNT != nullptr)
        {
            if (!bGotProductId &&
                RegQueryValueExA(KeyNT, "DigitalProductId", nullptr, nullptr, ProductId,
                                 &ProductIdSize) == ERROR_SUCCESS)
                bGotProductId = true;
            RegCloseKey(KeyNT);
        }
        if (!bGotProductId)
        {
            UuidCreate((UUID*)ProductId);
            ProductIdSize = 0x10;
        }
        Hash.AddData(ProductId, ProductIdSize);

        char UserName[0x104];
        DWORD UserNameSize = 0x101;
        if (!GetUserNameA(UserName, &UserNameSize))
            sprintf(UserName, "%u", timeGetTime());
        Hash.AddData(UserName, (unsigned long)strlen(UserName));
        return true;
    }

    // The widening tail shared by 0xa6940 / 0xa6ad0 / 0xa6c40: strlen+1 wide characters are
    // malloc'd, mbstowcs converts the whole string including its terminator, wcslen of the
    // result is what gets assigned, and the buffer is freed.
    void AssignWidenedHash(std::wstring& Out, const char* Source)
    {
        const size_t Count = strlen(Source) + 1;
        wchar_t* Wide = (wchar_t*)malloc(Count * sizeof(wchar_t));
        mbstowcs(Wide, Source, Count);
        Out.assign(Wide, wcslen(Wide));
        free(Wide);
    }
}

// ===========================================================================
// CPlayerAccount
// ===========================================================================

unsigned long CPlayerAccount::g_dwAccountIndex = 0;

CPlayerAccount::CPlayerAccount()
    : HashName(nullptr)
    , HashVerify(nullptr)
{
}

CPlayerAccount::CPlayerAccount(const CPlayerAccount& Other)
    : HashName(Other.HashName)
    , HashVerify(Other.HashVerify)
{
}

CPlayerAccount::~CPlayerAccount()
{
    TossHashes();
}

void CPlayerAccount::SetAcctIndex(unsigned long Index)
{
    // 0x062a8920: a single RET. Retail ignores Index and leaves g_dwAccountIndex untouched.
    (void)Index;
}

void CPlayerAccount::TossHashes()
{
    // 0x063065a0: each non-null hash goes through its scalar deleting destructor and the
    // pointer is cleared.
    if (HashName != nullptr)
    {
        delete HashName;
        HashName = nullptr;
    }
    if (HashVerify != nullptr)
    {
        delete HashVerify;
        HashVerify = nullptr;
    }
}

void CPlayerAccount::GenerateAccount(const char* Name)
{
    // 0x063065d0: fresh hashes; HashName = machine seed + Name (when non-null), and
    // HashVerify = MD5 of HashName's text. AsString is called twice for the text (once for
    // its length, once for the pointer), as retail does.
    TossHashes();
    HashName = new MD5Hash();
    HashVerify = new MD5Hash();

    if (!SeedMachineHash(*HashName))
    {
        TossHashes();
        return;
    }

    if (Name != nullptr)
        HashName->AddData((void*)Name, (unsigned long)strlen(Name));
    HashName->CalcValue();

    const unsigned long NameLength = (unsigned long)strlen(HashName->AsString());
    HashVerify->AddData((void*)HashName->AsString(), NameLength);
    HashVerify->CalcValue();
}

bool CPlayerAccount::LoadAccount()
{
    // 0x063063a0: fresh hashes, then MPAccountName / MPAccountNameSig from HKCU (0x400-byte
    // reads); true once the key opened, whether or not the values were there.
    TossHashes();
    HashName = new MD5Hash();
    HashVerify = new MD5Hash();

    HKEY Key = nullptr;
    if (RegOpenKeyA(HKEY_CURRENT_USER, kAccountKeyPath, &Key) != ERROR_SUCCESS)
        return false;

    char Buffer[0x400];
    DWORD BufSize = sizeof(Buffer);
    if (RegQueryValueExA(Key, "MPAccountName", nullptr, nullptr, (LPBYTE)Buffer, &BufSize) ==
        ERROR_SUCCESS)
        HashName->FromString(Buffer);

    BufSize = sizeof(Buffer);
    if (RegQueryValueExA(Key, "MPAccountNameSig", nullptr, nullptr, (LPBYTE)Buffer, &BufSize) ==
        ERROR_SUCCESS)
        HashVerify->FromString(Buffer);

    RegCloseKey(Key);
    return true;
}

bool CPlayerAccount::LoadAccountFromStrings(const char* NameHash, const char* VerifyHash)
{
    // 0x063062c0
    TossHashes();
    HashName = new MD5Hash();
    HashVerify = new MD5Hash();
    HashName->FromString(NameHash);
    HashVerify->FromString(VerifyHash);
    return true;
}

bool CPlayerAccount::VerifyAccount()
{
    // 0x06306870: with both hashes present, MD5 of HashName's text must equal HashVerify.
    if (HashName == nullptr || HashVerify == nullptr)
        return false;

    const char* NameStr = HashName->AsString();
    MD5Hash TempHash;
    TempHash.AddData((void*)NameStr, (unsigned long)strlen(NameStr));
    TempHash.CalcValue();
    return HashVerify->Compare(TempHash);
}

bool CPlayerAccount::GenerateTextKey(char* OutKey)
{
    // 0x06306df0: OutKey = MD5(machine seed [+ account index]) text + "-" + the last four
    // characters of the MD5 of that text. The three debug lines are retail's own.
    char DebugLine[0x2000];
    MD5Hash KeyHash;
    if (!SeedMachineHash(KeyHash))
        return false;

    if (g_dwAccountIndex != 0)
        KeyHash.AddData(&g_dwAccountIndex, 4);
    KeyHash.CalcValue();

    const char* RawKey = KeyHash.AsString();
    OutputDebugStringA(FormatDebugLine(DebugLine, "Raw key: %s\n", RawKey));

    MD5Hash VerifyHash;
    OutputDebugStringA(FormatDebugLine(DebugLine, "Raw key length: %d\n", (int)strlen(RawKey)));
    VerifyHash.AddData((void*)RawKey, (unsigned long)strlen(RawKey));
    VerifyHash.CalcValue();

    // 06306f2a: the '-' goes on OutKey's terminator (word store 2D 00 at the NUL).
    strcpy(OutKey, RawKey);
    strcat(OutKey, "-");

    const char* VerifyStr = VerifyHash.AsString();
    OutputDebugStringA(FormatDebugLine(DebugLine, "Verify hash: %s\n", VerifyStr));
    strcat(OutKey, LastFourChars(VerifyStr));
    return true;
}

bool CPlayerAccount::SetTextKey(char* Key)
{
    // 0x06307010. Key's first 0x23 bytes are MD5-hashed; the last four characters of that
    // text must equal the string at Key+0x24, or nothing is written. Then, under HKCU,
    // "InstallKey" = Key, "MPAccountName" = MD5(Key) text and "MPAccountNameSig" = MD5 of
    // that text -- the two values LoadAccount reads back.
    char DebugLine[0x2000];
    MD5Hash KeyHash;
    OutputDebugStringA(FormatDebugLine(DebugLine, "Key length: %d\n", 0x23));
    KeyHash.AddData(Key, 0x23);
    KeyHash.CalcValue();
    if (strcmp(LastFourChars(KeyHash.AsString()), Key + 0x24) != 0)
        return false;

    HKEY RegKey = nullptr;
    DWORD Disposition = 0;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, kAccountKeyPath, 0, nullptr, 0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &RegKey, &Disposition) !=
        ERROR_SUCCESS)
        return false;

    RegSetValueExA(RegKey, "InstallKey", 0, REG_SZ, (const BYTE*)Key, (DWORD)strlen(Key));

    MD5Hash NameHash;
    NameHash.AddData(Key, (unsigned long)strlen(Key));
    NameHash.CalcValue();
    const char* NameHashStr = NameHash.AsString();
    RegSetValueExA(RegKey, "MPAccountName", 0, REG_SZ, (const BYTE*)NameHashStr,
                   (DWORD)strlen(NameHashStr));

    MD5Hash SigHash;
    SigHash.AddData((void*)NameHashStr, (unsigned long)strlen(NameHashStr));
    SigHash.CalcValue();
    const char* SigHashStr = SigHash.AsString();
    RegSetValueExA(RegKey, "MPAccountNameSig", 0, REG_SZ, (const BYTE*)SigHashStr,
                   (DWORD)strlen(SigHashStr));

    RegCloseKey(RegKey);
    return true;
}

bool CPlayerAccount::GetTextKey(char* OutKey)
{
    // 0x06307310. Reads "InstallKey" (up to 0x29 bytes) and, when the same checksum as
    // SetTextKey holds, copies it into OutKey. RegCloseKey runs on every path, the failed
    // open included (with the handle still 0).
    HKEY Key = nullptr;
    bool bResult = false;
    // Retail's stack buffer is 0x2c bytes and uninitialised; the port zeroes it.
    char Buffer[0x2c] = {};
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kAccountKeyPath, 0, KEY_QUERY_VALUE, &Key) ==
        ERROR_SUCCESS)
    {
        DWORD BufSize = 0x29;
        if (RegQueryValueExA(Key, "InstallKey", nullptr, nullptr, (LPBYTE)Buffer, &BufSize) ==
            ERROR_SUCCESS)
        {
            MD5Hash KeyHash;
            KeyHash.AddData(Buffer, 0x23);
            KeyHash.CalcValue();
            if (strcmp(LastFourChars(KeyHash.AsString()), Buffer + 0x24) == 0)
            {
                strcpy(OutKey, Buffer);
                bResult = true;
            }
        }
    }
    RegCloseKey(Key);
    return bResult;
}

const char* CPlayerAccount::GetAccountNameChar()
{
    // 0x06306920
    if (HashName != nullptr)
        return HashName->AsString();
    return "";
}

const char* CPlayerAccount::GetAccountNameSigChar()
{
    // 0x06306ab0
    if (HashVerify != nullptr)
        return HashVerify->AsString();
    return "";
}

std::wstring CPlayerAccount::GetAccountName()
{
    // Common.dll 0xa6940: starts as L"", widened from HashName's text when there is one.
    std::wstring Result(L"");
    if (HashName != nullptr)
        AssignWidenedHash(Result, HashName->AsString());
    return Result;
}

std::wstring CPlayerAccount::GetAccountNameSig()
{
    // Common.dll 0xa6ad0: the same shape over HashVerify (this+0x08).
    std::wstring Result(L"");
    if (HashVerify != nullptr)
        AssignWidenedHash(Result, HashVerify->AsString());
    return Result;
}

std::wstring CPlayerAccount::GetServerSignature(const char* Text)
{
    // Common.dll 0xa6c40: a stack MD5Hash is loaded from Text (MD5Hash::FromString), and its
    // 16 digest bytes (+0x04..+0x13) are XORed with HashVerify's digest taken backwards --
    // byte i with HashVerify+0x13-i -- before the result's text is widened.
    std::wstring Result(L"");
    if (HashVerify != nullptr)
    {
        MD5Hash Signature;
        Signature.FromString(Text);
        unsigned char* SignatureBytes = Signature.GetStatePtr();
        unsigned char* VerifyBytes = HashVerify->GetStatePtr();
        for (int Index = 0; Index < 0x10; ++Index)
            SignatureBytes[Index] ^= VerifyBytes[0x0f - Index];
        AssignWidenedHash(Result, Signature.AsString());
    }
    return Result;
}
