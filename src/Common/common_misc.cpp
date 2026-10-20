// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_misc.h"
#include "x86math.h" // I3DMathEngine
#include "common_reputation.h" // Reputation::Vibe::FreeDynamic (CleanUpDatabases)
#include "readfile.h"      // IFileSystem, FFileSystemDesc (DAEngine_create_filesystem)
#include "DALib.h"         // DALib::Data for filesystem ops
#include "dacom.h"         // FDUMP

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <direct.h>
#include <io.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>          // SHGetFolderPathA (GetUserLocalDataPath / GetUserDataPath / GetScreenShotPath)
#include "common_csys.h"
#include "common_universe.h"
#include "common_id.h"       // ID_String -- the four globals below are really ID_String
#include "common_net.h"      // StateMachineMessage::GenerateUniqueMessageType (SMM_* ids)
#include "common_text.h"     // ViewRect (Left/Top/Right/Bottom) for get_projected_bounding_box

// ============================================================================
// Math utilities
// ============================================================================

int CallDacomStricmp(const char* Left, const char* Right);  // common_dacom_stricmp.cpp

// .text:06303840..063038a0. strtok(String, ",;/ ") (DAT_063a2710) only cuts the first token in
// place; every compare then uses String itself through the DACOM.DLL stricmp import
// (06303877, IAT 0639902c). DefaultName is never read.
COMMON_API unsigned long __cdecl select_option_from_string(char* String, option_spec const* const Options, int Count, char const* DefaultName)
{
    char* Token = strtok(String, ",;/ ");
    if (Token != nullptr && Count > 0)
    {
        for (int i = 0; i < Count; ++i)
        {
            if (CallDacomStricmp(String, Options[i].Name) == 0)
            {
                return Options[i].Value;
            }
        }
    }
    return 0xffffffff;
}

// ============================================================================
// CRT-heap string helpers (malloc_strcpy family; StringAlloc is the StringCache in common_stringcache.cpp)
// ============================================================================

// CRT-heap string helpers. Unlike StringAlloc (StringCache pool, never freed) these strings
// are owned by the caller and released with malloc_strfree / malloc_wstrfree.

char const* __cdecl malloc_strcpy(char** Dest, char const* Src)
{
    // Common.dll.txt:06334400. Src != 0 -> *Dest = _strdup(Src) (the old *Dest is not freed);
    // Src == 0 -> *Dest = 0. Returns *Dest. Dest is dereferenced unchecked.
    if (Src != nullptr)
    {
        *Dest = _strdup(Src);
        return *Dest;
    }
    *Dest = nullptr;
    return *Dest;
}

void __cdecl malloc_strfree(char** Str)
{
    // Common.dll.txt:06334430 (also exported as malloc_wstrfree, one body): if *Str, free it and
    // zero the slot. Str is dereferenced unchecked.
    if (*Str != nullptr)
    {
        free(*Str);
        *Str = nullptr;
    }
}

wchar_t const* __cdecl malloc_wstrcpy(wchar_t** Dest, wchar_t const* Src)
{
    // Common.dll.txt:06334450: the wide twin of malloc_strcpy, _wcsdup.
    if (Src != nullptr)
    {
        *Dest = _wcsdup(Src);
        return *Dest;
    }
    *Dest = nullptr;
    return *Dest;
}

void __cdecl malloc_wstrfree(wchar_t** Str)
{
    // Common.dll.txt:06334430 -- the same body as malloc_strfree (both exports name that VA).
    if (*Str != nullptr)
    {
        free(*Str);
        *Str = nullptr;
    }
}

// ============================================================================
// Client/server run-permission handshake
// ============================================================================

namespace
{
    // Both events come from CRT static initialisers, client first (LAB_0631d970 via the init
    // table slot 063ea544, LAB_0631d990 via 063ea548): CreateEventA(NULL, TRUE, TRUE, name) --
    // manual reset, initially signalled. Nothing closes them (no atexit registration).
    HANDLE GRunPermissionClientEvent =                       // DAT_06401810
        CreateEventA(nullptr, TRUE, TRUE, "MicrosoftFreelancerClientCanRunEvent");
    HANDLE GRunPermissionServerEvent =                       // DAT_0640180c
        CreateEventA(nullptr, TRUE, TRUE, "MicrosoftFreelancerServerCanRunEvent");

    unsigned long const kClientRunTimeoutMs = 0x1388;   // 5000, 0631d9c2
    unsigned long const kServerRunTimeoutMs = 0x1f4;    // 500,  0631da02
}

void __cdecl WaitForClientRunPermission()
{
    // 0631d9b0: ResetEvent(server); WaitForSingleObject(client, 5000) == WAIT_TIMEOUT (0x102)
    // -> SetEvent(client).
    ResetEvent(GRunPermissionServerEvent);
    if (WaitForSingleObject(GRunPermissionClientEvent, kClientRunTimeoutMs) == WAIT_TIMEOUT)
        SetEvent(GRunPermissionClientEvent);
}

void __cdecl WaitForServerRunPermission()
{
    // 0631d9f0: the mirror image, with the shorter budget.
    ResetEvent(GRunPermissionClientEvent);
    if (WaitForSingleObject(GRunPermissionServerEvent, kServerRunTimeoutMs) == WAIT_TIMEOUT)
        SetEvent(GRunPermissionServerEvent);
}

void __cdecl GiveClientRunPermission()
{
    // 0631da30: SetEvent(client).
    SetEvent(GRunPermissionClientEvent);
}

void __cdecl GiveServerRunPermission()
{
    // 0631da40: SetEvent(server).
    SetEvent(GRunPermissionServerEvent);
}

unsigned long __cdecl SafeWaitForSingleObject(HANDLE Handle, unsigned long Timeout)
{
    // 062fd4c0. Wait on one object but keep the message queue moving, so a modal wait does not
    // freeze the window. WM_QUIT stops the wait and reports a timeout rather than being
    // re-posted -- retail removes it from the queue (PM_REMOVE) and does not put it back.
    for (;;)
    {
        MSG Msg;
        while (PeekMessageA(&Msg, nullptr, 0, 0, PM_REMOVE) != 0)
        {
            if (Msg.message == WM_QUIT)
                return WAIT_TIMEOUT;
            DispatchMessageA(&Msg);
        }

        // Retail passes 0xff literally (062fd514). That was QS_ALLINPUT when this shipped;
        // on Vista and later QS_ALLINPUT gained QS_RAWINPUT and became 0x4ff, so the constant
        // is spelled out here to keep the wake mask retail's.
        unsigned long const kRetailWakeMask = 0xff;
        DWORD const Result =
            MsgWaitForMultipleObjects(1, &Handle, FALSE, Timeout, kRetailWakeMask);
        if (Result != 1)
            return Result;
    }
}

// ============================================================================
// Hex <-> wide-string and GUID text parsing
// ============================================================================

namespace
{
    // Retail inlines this test three times inside ParseGUID (0631ba00, 0631ba40, 0631ba77):
    // '0'..'9', 'a'..'f', 'A'..'F', in that order, on the raw signed char.
    bool IsGuidHexDigit(char Ch)
    {
        if (Ch >= '0' && Ch <= '9')
            return true;
        if (Ch >= 'a' && Ch <= 'f')
            return true;
        return Ch >= 'A' && Ch <= 'F';
    }

    // Advance past a run of hex digits, stopping at the separator or the terminator.
    char const* SkipGuidField(char const* Cursor)
    {
        while (*Cursor != '\0' && IsGuidHexDigit(*Cursor))
            Cursor += 1;
        return Cursor;
    }

    // The two-character group ParseGUID builds on the stack at 0631baaf and friends.
    unsigned long ReadGuidByte(char const* Pair)
    {
        char Buf[3];
        Buf[0] = Pair[0];
        Buf[1] = Pair[1];
        Buf[2] = '\0';
        return strtoul(Buf, nullptr, 16);
    }

    char const kNullGuidText[] = "{00000000-0000-0000-0000-000000000000}";
}

namespace
{
    // DAT_063fcc78 -- one shared static that every filetime_to_wstring call overwrites.
    wchar_t GFileTimeText[0x100];
}

wchar_t const* __cdecl filetime_to_wstring(_FILETIME const& Time)
{
    // Common.dll.txt:06303360.
    if (Time.dwHighDateTime == 0 && Time.dwLowDateTime == 0)
    {
        // 06303376: a zero FILETIME is the empty string, not "01/01/1601".
        wcscpy(GFileTimeText, L"");
        return GFileTimeText;
    }

    SYSTEMTIME Local;
    FileTimeToSystemTime(&Time, &Local);

    // 063033b7 / 063033d2: 0x400 is LOCALE_SYSTEM_DEFAULT, 0xe is
    // TIME_NOSECONDS|TIME_NOTIMEMARKER|TIME_FORCE24HOURFORMAT, 1 is DATE_SHORTDATE.
    char TimeText[0x40];
    GetTimeFormatA(0x400, 0xe, &Local, nullptr, TimeText, 0x40);
    char DateText[0x40];
    GetDateFormatA(0x400, 1, &Local, nullptr, DateText, 0x40);

    wchar_t WideTime[0x40];
    mbstowcs(WideTime, TimeText, 0x40);
    wchar_t WideDate[0x40];
    mbstowcs(WideDate, DateText, 0x40);

    // 06303400 onwards: date, one space, time.
    wcscpy(GFileTimeText, WideDate);
    wcscat(GFileTimeText, L" ");
    wcscat(GFileTimeText, WideTime);
    return GFileTimeText;
}

void __cdecl HexToWide(char const* Src, wchar_t* Dst)
{
    // Common.dll.txt:063032b0. The scan at 063032c2 accepts the EMPTY string into the hex path
    // as well (063032c0 jumps straight past the loop), where the first sscanf simply fails and
    // only the terminator is written.
    bool bAllHex = true;
    for (char const* Scan = Src; *Scan != '\0'; ++Scan)
    {
        if (isxdigit((unsigned char)*Scan) == 0)
        {
            bAllHex = false;
            break;
        }
    }

    if (!bAllHex)
    {
        // 06303338: measure, then convert including the terminator.
        size_t const Needed = mbstowcs(nullptr, Src, 0);
        mbstowcs(Dst, Src, Needed + 1);
        return;
    }

    char const* Cursor = Src;
    wchar_t* Out = Dst;
    unsigned int Value = 0;
    int Read = sscanf(Cursor, "%04x", &Value);
    while (Read != 0 && Read != -1)
    {
        *Out = (wchar_t)Value;
        Cursor += 4;
        Out += 1;
        Value = 0;
        Read = sscanf(Cursor, "%04x", &Value);
    }
    *Out = 0;
}

void __cdecl WideToHex(wchar_t const* Src, char* Dst)
{
    // Common.dll.txt:06303260. Four characters per code unit, no terminator until the end.
    wchar_t const* Cursor = Src;
    char* Out = Dst;
    while (*Cursor != 0)
    {
        sprintf(Out, "%04x", (unsigned int)*Cursor);
        Cursor += 1;
        Out += 4;
    }
    *Out = '\0';
}

_GUID* __cdecl ParseGUID(char const* Text, _GUID* Out)
{
    // Common.dll.txt:0631b980.
    // Note the asymmetry retail actually has: the brace search below runs over Text, but the
    // null-GUID shortcut at 0631b9b6 compares the ORIGINAL Text, not the position it found.
    char const* Cursor = Text;
    while (*Cursor != '\0' && *Cursor != '{')
        Cursor += 1;

    if (strncmp(Text, kNullGuidText, strlen(kNullGuidText)) == 0)
    {
        // 0631b9c3: zero the whole GUID and hand back NULL.
        Out->Data1 = 0;
        Out->Data2 = 0;
        Out->Data3 = 0;
        memset(Out->Data4, 0, sizeof(Out->Data4));
        return nullptr;
    }

    // 0631b9de: step over the brace. Retail also strcpy's the tail into a stack buffer here
    // (0631b9e7) and never reads it back; the copy is omitted, see docs/re/deviations.md.
    Cursor += 1;

    char const* Field = Cursor;
    Cursor = SkipGuidField(Cursor);
    unsigned long const Data1 = strtoul(Field, nullptr, 16);
    Cursor += 1;                                  // past the '-'

    Field = Cursor;
    Cursor = SkipGuidField(Cursor);
    unsigned long const Data2 = strtoul(Field, nullptr, 16);
    Cursor += 1;

    Field = Cursor;
    Cursor = SkipGuidField(Cursor);
    unsigned long const Data3 = strtoul(Field, nullptr, 16);
    Cursor += 1;

    // The fourth group is two bytes, then the '-' (0631bae4 folds the skip into ADD ESI,2).
    unsigned char Data4[8];
    Data4[0] = (unsigned char)ReadGuidByte(Cursor);
    Cursor += 2;
    Data4[1] = (unsigned char)ReadGuidByte(Cursor);
    Cursor += 3;

    int Index = 0;
    for (; Index < 6; ++Index)
    {
        Data4[2 + Index] = (unsigned char)ReadGuidByte(Cursor);
        Cursor += 2;
    }

    Out->Data1 = Data1;
    Out->Data2 = (unsigned short)Data2;
    Out->Data3 = (unsigned short)Data3;
    memcpy(Out->Data4, Data4, sizeof(Data4));

    // 0631bb4d: retail compares the 16 bytes against a zero-filled static in the BSS tail of
    // .data that nothing ever writes -- i.e. GUID_NULL -- and returns NULL on a match.
    static _GUID const kNullGuid = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
    if (memcmp(Out, &kNullGuid, sizeof(_GUID)) == 0)
        return nullptr;
    return Out;
}

// FrustumCull / SphereCull and their helpers live in common_zone.cpp (Systems.cpp in retail).

// ============================================================================
// Global constants (initialized from retail .data values)
// ============================================================================

float CHATTER_MAX_DIST = 2000.0f;          // DAT_063ed1a4
float CHATTER_MAX_DIST_ATTEN = -20.0f;     // DAT_063ed1ac
float CHATTER_START_ATTEN = -5.0f;         // DAT_063ed1a8
float COLLISION_DAMAGE_FACTOR = 0.25f;     // DAT_063ed184
int COMM_CONFLICT_PRIORITY_CUTOFF = -2;    // DAT_063ed1b0 (retail default; ini can override)
float COMM_PLAYER_FAR_DIST = 2000.0f;      // DAT_063ed19c
float COMM_PLAYER_FAR_DIST_ATTEN = -15.0f; // DAT_063ed1a0
double DaysPerSecond = 1.0 / 60.0;         // DAT_063edb78 (retail value is 1/60, not 1/86400 - this game's "day" is a 60-real-second compressed cycle)
float FIRE_FAILED_DELAY = 1.0f;            // DAT_063ed1c8
float JETTISONED_CARGO_VELOCITY = 10.0f;   // DAT_063ed194
float LOOT_OWNER_SAFE_TIME = 1.0f;         // DAT_063ed188
float LOOT_UNSEEN_LIFE_TIME = 60.0f;       // DAT_063ed18c
float LOOT_UNSEEN_RADIUS = 2500.0f;        // DAT_063ed190
int MAX_PLAYER_AMMO = 50;                  // DAT_063ed1d4
float MUSIC_CROSS_FADE_DELAY = 30.0f;      // DAT_063ed198
float MUZZLE_CONE_ANGLE = 90.0f;           // DAT_063ed1c4
float PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE = 1.0f; // DAT_063ed1d0
float PLAYER_COLLISION_GROUP_HIT_PTS_SCALE = 1.0f; // DAT_063ed1cc
float WALLA_MAX_DIST = 2500.0f;            // DAT_063ed1b4
float WALLA_MAX_DIST_ATTEN = -26.0f;       // DAT_063ed1b8
int WALLA_PRIORITY_CUTOFF = -4;            // DAT_063ed1c0
float WALLA_START_ATTEN = -8.0f;           // DAT_063ed1bc

bool PlayerTrailAvailable = false;         // DAT_063fce0d

// These four are ?<name>@@3UID_String@@A in retail, so they must be the real ID_String and
// not a local stand-in -- a private struct here would silently give them a different type.
//
// The first three get their default (pre-ReadConstants-override) value from a dedicated
// static-initializer thunk in retail's .data init-array (Common.dll 0x62c1220-region
// neighbors 0x6304930/0x6304950/0x6304970), each just `Global.Id = CreateID("<literal>")`:
//   LAB_06304930: SND_CARGO_JETTISONED.Id = CreateID("cargo_jettison")
//   LAB_06304950: FIRE_FAILED_SOUND.Id    = CreateID("fire_dry")
//   LAB_06304970: DEATH_COMM_FUSE.Id      = CreateID("death_comm")
// CreateID() only touches CFLIDMaker's lazily-constructed singleton (CFLIDMaker::GetInstance,
// common_id.cpp), whose backing pointer is zero-initialized before any dynamic initializer
// runs, so calling it here from another global's own dynamic initializer is safe regardless
// of translation-unit init order. ReadConstants() (common_constants.cpp) can still override these
// later from an ini file via ApplyUintCreateId(), same as retail.
ID_String DEATH_COMM_FUSE = { CreateID("death_comm") };       // DAT_0644ce10
ID_String FIRE_FAILED_SOUND = { CreateID("fire_dry") };       // DAT_0644ce14
ID_String SND_CARGO_JETTISONED = { CreateID("cargo_jettison") }; // DAT_0644ce18
ID_String SpaceflightLocationID = { CreateID("Spaceflight") };   // DAT_063f270c -- retail sets this via a static-initializer table entry (LAB_0632e6d0, called before DllMain like the other CreateID(...) globals above), not literally at runtime

// Retail values verified directly against Common.dll.txt:568132-568158 (the real
// .rdata:063a4aa4 PDB-demangled symbol block: ?SUBOBJ_ID_NONE@@3GB=0, ?SUBOBJ_ID_ROOT@@3GB=1,
// ?SUBOBJ_ID_POWER@@3GB=2, each read by 25+ .text sites, all as a plain CMP/JZ equality test --
// see the call sites across Common's TUs, e.g. common_aifirecontrol.cpp, common_behavior.cpp, common_cequip.cpp). The previous values here
// (NONE=0xFFFF, ROOT=0, POWER=1, address comments DAT_063eca4/6/aa) were wrong on both counts:
// DAT_063eca4 is unrelated raw data inside the hardpoint-name table (PTR_s_hp_gun_063eca40,
// Common.dll.txt:635888-635894), not this symbol at all, and the values were off by one slot.
unsigned short const SUBOBJ_ID_NONE = 0;         // .rdata:063a4aa4 (?SUBOBJ_ID_NONE@@3GB)
unsigned short const SUBOBJ_ID_ROOT = 1;         // .rdata:063a4aa6 (?SUBOBJ_ID_ROOT@@3GB)
unsigned short const SUBOBJ_ID_POWER = 2;        // .rdata:063a4aa8 (?SUBOBJ_ID_POWER@@3GB)

// SMM_CHANGE_STATE (.data 0x0640a768, ?SMM_CHANGE_STATE@@3IB, ordinal 1948) and
// SMM_TIMEOUT (.data 0x0640a764, ?SMM_TIMEOUT@@3IB, ordinal 1949). These are NOT literals.
// Retail gives each one its own CRT static initialiser -- LAB_063331a0 at CRT table slot
// 0x063ea5c0 for SMM_CHANGE_STATE, LAB_063331c0 at slot 0x063ea5c4 for SMM_TIMEOUT -- and both
// are byte-for-byte an inlined copy of StateMachineMessage::GenerateUniqueMessageType()
// (0x06332f70: `MOV EAX,[s_unique_message_type_generator]; INC EAX; MOV [gen],EAX; RET`),
// storing the post-increment result. So they are message-type ids handed out by the same
// allocator every other message type uses, not tuning numbers.
//
// The whole listing was grepped for the generator at 0x0640a778: its only references are
// GenerateUniqueMessageType itself and these two inlined initialisers, so no other static
// initialiser draws an id first. CRT table order therefore fixes the values exactly:
// SMM_CHANGE_STATE == 1, SMM_TIMEOUT == 2, and the first runtime call to
// GenerateUniqueMessageType() returns 3. DECLARATION ORDER BELOW IS LOAD-BEARING -- it is what
// reproduces the CRT table order, so SMM_CHANGE_STATE must stay first.
//
// This corrects a real bug: SMM_TIMEOUT was previously hardcoded to 5000 with the comment
// "(5 seconds)", i.e. read as a millisecond timeout. It is a message-type id, and 5000 would
// have collided with nothing but also matched nothing any SMM_TIMEOUT producer actually sends.
// The old address comments (DAT_063f2764/063f2768) were wrong too.
unsigned const SMM_CHANGE_STATE = StateMachineMessage::GenerateUniqueMessageType();  // == 1
unsigned const SMM_TIMEOUT = StateMachineMessage::GenerateUniqueMessageType();       // == 2

// ============================================================================
// Utility functions
// ============================================================================

// Constants shared by the random_* family (values read from the retail .rdata).
// Values that stay on the x87 stack in retail are held as double here.
namespace
{
float const GRandUnitScale = 3.0517578125e-05f;      // DAT_063997d0 (float, 1/32768)
double const GRandTwoPi = 6.2831854820251465;        // DAT_0639c9e0 (double; 2*pi rounded through float)
double const GRandOneThird = 0.3333333333333333;     // DAT_0639e0e8 (double)
float const GRandZero = 0.0f;                        // DAT_06399408 (float)
float const GRandHalf = 0.5f;                        // DAT_0639c13c (float)
float const GRandOne = 1.0f;                         // DAT_0639a1d0 (float)
double const GRandAlignedDot = 0.998;                // DAT_063a3c50 (double)

// FILD rand() / FMUL float [DAT_063997d0]: the product stays on the FPU stack.
double RandUnit()
{
    return (double)(rand()) * (double)(GRandUnitScale);
}
} // namespace

Vector __cdecl RandomVector(float Radius)
{
    // Common.dll.txt 06303450-063034e7. Three rand() - 0x3fff draws (x and y stored as float,
    // z kept exact on the FPU stack), no rejection loop: the result is (x, y, z) scaled by
    // Radius / |v| (sum taken z^2 + y^2 + x^2). All three draws equal to 0x3fff divide by zero,
    // as in retail.
    float const X = (float)(rand() - 0x3fff);
    float const Y = (float)(rand() - 0x3fff);
    double const Z = (double)(rand() - 0x3fff);
    double const Scale = (double)(Radius) /
        sqrt(Z * Z + (double)(Y) * Y + (double)(X) * X);

    Vector Result;
    Result.x = (float)((double)(X) * Scale);
    Result.y = (float)((double)(Y) * Scale);
    Result.z = (float)(Scale * Z);
    return Result;
}

Vector __cdecl random_on_unit_sphere()
{
    // Common.dll.txt 0631da50-0631db11. Two draws stored as float; cos(theta) = 2u - 1 clamped
    // to [-1, 1] (the lower test runs on the unrounded value: TEST AH,0x05 / JP skips the
    // clamp unless ST0 < -1.0; the upper test on the stored float: TEST AH,0x41 / JNZ skips it
    // unless the value is greater than 1.0). theta = acos, phi = u2 * 2pi.
    float const U1 = (float)(RandUnit());
    float const U2 = (float)(RandUnit());

    double const CosRaw = (double)(U1) + (double)(U1) - 1.0;
    float CosTheta = (float)(CosRaw);
    if (CosRaw < -1.0)
        CosTheta = -1.0f;
    if (CosTheta > 1.0f)
        CosTheta = 1.0f;

    double const Theta = acos((double)(CosTheta));
    double const Phi = (double)(U2) * GRandTwoPi;
    double const SinTheta = std::sin(static_cast<double>(Theta));

    Vector Result;
    Result.x = (float)(std::sin(static_cast<double>(Phi)) * SinTheta);
    Result.y = static_cast<float>(std::cos(static_cast<double>(Theta)));
    Result.z = (float)(std::cos(static_cast<double>(Phi)) * SinTheta);
    return Result;
}

Vector __cdecl random_inside_sphere(float Radius)
{
    // Common.dll.txt 0631db20-0631dbce. Radius' = u1^(1/3) * Radius (float);
    // cos(theta) = 2u2 - 1 (stored float, acos of the unrounded value); S = sin(theta) * Radius'
    // (float); phi = u3 * 2pi. Result (sin(phi) * S, cos(theta) * Radius', cos(phi) * S).
    float const Scaled = (float)(pow(RandUnit(), GRandOneThird) * Radius);

    double const CosRaw = RandUnit() * 2.0 - 1.0;
    float const CosTheta = (float)(CosRaw);
    float const SinScaled = (float)(std::sin(static_cast<double>(acos(CosRaw))) * Scaled);

    double const Phi = RandUnit() * GRandTwoPi;

    Vector Result;
    Result.x = (float)(std::sin(static_cast<double>(Phi)) * SinScaled);
    Result.y = (float)((double)(CosTheta) * Scaled);
    Result.z = (float)(std::cos(static_cast<double>(Phi)) * SinScaled);
    return Result;
}

Vector __cdecl random_inside_box(float X, float Y, float Z)
{
    // Common.dll.txt 0631dd30-0631dda0: (rand() * extent) * (1/32768) per axis -- the box is
    // [0, extent), not centred.
    float const ResultX = (float)((double)(rand()) * X * GRandUnitScale);
    float const ResultY = (float)((double)(rand()) * Y * GRandUnitScale);
    float const ResultZ = (float)((double)(rand()) * Z * GRandUnitScale);

    Vector Result;
    Result.x = ResultX;
    Result.y = ResultY;
    Result.z = ResultZ;
    return Result;
}

Vector __cdecl random_inside_cylinder(float Radius, float HalfHeight)
{
    // Common.dll.txt 0631dbd0-0631dc70. The cylinder axis is y.
    // R = sqrt(u1) * Radius (float); -HalfHeight stored as float; u2 stored as double;
    // phi = u3 * 2pi. Result (sin(phi) * R, (H - (-H)) * u2 + (-H), cos(phi) * R).
    float const Scaled = (float)(sqrt(RandUnit()) * Radius);
    float const NegHalfHeight = -HalfHeight;
    double const T = RandUnit();
    double const Phi = RandUnit() * GRandTwoPi;

    Vector Result;
    Result.x = (float)(std::sin(static_cast<double>(Phi)) * Scaled);
    Result.y = (float)(((double)(HalfHeight) - NegHalfHeight) * T + NegHalfHeight);
    Result.z = (float)(std::cos(static_cast<double>(Phi)) * Scaled);
    return Result;
}

Vector __cdecl random_inside_ellipsoid(float A, float B, float C)
{
    // Common.dll.txt 0631ddb0-0631deac. Ra/Rb/Rc = u^(1/3) * A/B/C (three draws, float);
    // cos(theta) = 2u4 - 1 (float), sin(theta) = sin(acos(unrounded)) (float); phi = u5 * 2pi.
    // Result (sin(phi) * (sin(theta) * Ra), cos(theta) * Rb, cos(phi) * (sin(theta) * Rc)).
    float const ScaledA = (float)(pow(RandUnit(), GRandOneThird) * A);
    float const ScaledB = (float)(pow(RandUnit(), GRandOneThird) * B);
    float const ScaledC = (float)(pow(RandUnit(), GRandOneThird) * C);

    double const CosRaw = RandUnit() * 2.0 - 1.0;
    float const CosTheta = (float)(CosRaw);
    float const SinTheta = static_cast<float>(std::sin(static_cast<double>(acos(CosRaw))));

    double const Phi = RandUnit() * GRandTwoPi;

    Vector Result;
    Result.x = (float)(std::sin(static_cast<double>(Phi)) * ((double)(SinTheta) * ScaledA));
    Result.y = (float)((double)(CosTheta) * ScaledB);
    Result.z = (float)(std::cos(static_cast<double>(Phi)) * ((double)(SinTheta) * ScaledC));
    return Result;
}

Vector __cdecl random_inside_ring(float Outer, float Inner, float HalfHeight)
{
    // Common.dll.txt 0631dc80-0631dd2a. Same shape as random_inside_cylinder (axis y) with
    // R = sqrt(u1) * (Outer - Inner) + Inner (float).
    float const Scaled = (float)(sqrt(RandUnit()) * ((double)(Outer) - Inner) + Inner);
    float const NegHalfHeight = -HalfHeight;
    double const T = RandUnit();
    double const Phi = RandUnit() * GRandTwoPi;

    Vector Result;
    Result.x = (float)(std::sin(static_cast<double>(Phi)) * Scaled);
    Result.y = (float)(((double)(HalfHeight) - NegHalfHeight) * T + NegHalfHeight);
    Result.z = (float)(std::cos(static_cast<double>(Phi)) * Scaled);
    return Result;
}

Vector __cdecl random_sphere_constrained(Vector const& Dir, float Angle)
{
    // Common.dll.txt 0631deb0-0631e04b. A random direction inside a cone of half-angle Angle
    // around +z, rotated so that +z maps onto Dir:
    //  - Axis = Dir x (0,0,1) written out with the 0.0f products retail keeps
    //    (y - z*0, z*0 - x, x*0 - y*0), dot = y*0 + x*0 + z;
    //  - half = acos(dot) * 0.5f; Q = (cos(half), Axis * (sin(half) / |Axis|)) via
    //    FUN_06288830 (Vector * float). No guard: Dir == +z gives 0/0, as in retail;
    //  - v = u1 * (1 - (-sin Angle)) + (-sin Angle) (float); phi = u2 * 2pi (float);
    //    P = (cos(phi) * cos(asin v), sin(phi) * cos(asin v), v);
    //  - 3DMathEngine (DAT_06401814 lazy pointer -> Acquire3DMathEngine): slot +0x54
    //    QuaternionToMatrix(M, Q), slot +0x38 TransposeTransform(Out, M, P).
    double const ZeroZ = (double)(Dir.z) * GRandZero;
    float const AxisX = (float)((double)(Dir.y) - ZeroZ);
    float const AxisY = (float)(ZeroZ - Dir.x);
    float const ZeroX = (float)((double)(Dir.x) * GRandZero);
    float const ZeroY = (float)((double)(Dir.y) * GRandZero);
    float const AxisZ = (float)((double)(ZeroX) - ZeroY);

    double const HalfAngle = acos((double)(ZeroY) + ZeroX + Dir.z) * GRandHalf;
    FQuaternion Rotation;
    Rotation.W = static_cast<float>(std::cos(static_cast<double>(HalfAngle)));
    float const AxisScale = (float)(std::sin(static_cast<double>(HalfAngle)) /
        sqrt((double)(AxisZ) * AxisZ + (double)(AxisY) * AxisY +
             (double)(AxisX) * AxisX));
    // FUN_06288830 (Vector operator*(Vector const&, float)): each product rounded to float.
    Rotation.X = AxisScale * AxisX;
    Rotation.Y = AxisScale * AxisY;
    Rotation.Z = AxisScale * AxisZ;

    // FSIN; FCHS; FSTP dword: one rounding (negation is exact).
    float const NegSinAngle = -static_cast<float>(std::sin(static_cast<double>(Angle)));
    float const Height = (float)(RandUnit() * ((double)(GRandOne) - NegSinAngle) + NegSinAngle);
    float const Phi = (float)(RandUnit() * GRandTwoPi);
    double const Ring = std::cos(asin((double)(Height)));

    FVector Point;
    Point.X = (float)(std::cos(static_cast<double>(Phi)) * Ring);
    Point.Y = (float)(std::sin(static_cast<double>(Phi)) * Ring);
    Point.Z = Height;

    FMatrix Frame;
    Acquire3DMathEngine()->QuaternionToMatrix(Frame, Rotation);
    FVector Rotated;
    Acquire3DMathEngine()->TransposeTransform(Rotated, Frame, Point);

    Vector Result;
    Result.x = Rotated.X;
    Result.y = Rotated.Y;
    Result.z = Rotated.Z;
    return Result;
}

Vector __cdecl random_sphere_constrained_2(Vector const& Dir, float MinAngle, float MaxAngle)
{
    // Common.dll.txt 0631e050-0631e294. A random direction at an angle in [MinAngle, MaxAngle)
    // from +z, rotated so that +z maps onto Dir:
    //  - a = u1 * (MaxAngle - MinAngle) + MinAngle; V = (sin a, 0, cos a);
    //  - phi = u2 * 2pi; cos is taken of the unrounded phi, sin of phi stored as float;
    //    Mz = rows (cos, -sin, 0), (sin, cos, 0), (0, 0, 1); slot +0x2C TransformMatrix(V', Mz, V);
    //  - dot = x*0 + y*0 + z. Unless dot < 0.998 (TEST AH,0x05 / JP to 0631e278 when not
    //    less, NaN included) V' is returned as is;
    //  - otherwise Axis = Dir x (0,0,1) as in random_sphere_constrained, half = acos(dot as
    //    float) * 0.5f, Q = (cos(half), Axis * (sin(half) / |Axis|)) through the Vector
    //    ctor FUN_0628b030, slot +0x54 QuaternionToMatrix(M, Q), slot +0x38
    //    TransposeTransform(Out, M, V').
    double const AngleValue = RandUnit() * ((double)(MaxAngle) - MinAngle) + MinAngle;
    FVector Cone;
    Cone.X = static_cast<float>(std::sin(static_cast<double>(AngleValue)));
    Cone.Y = 0.0f;
    Cone.Z = static_cast<float>(std::cos(static_cast<double>(AngleValue)));

    // 0631e0e1-0631e0fb: FCOS / FSIN results go to the float matrix straight from the
    // register (FLD ST1 / FCHS / FSTP dword), one rounding each.
    double const PhiRaw = RandUnit() * GRandTwoPi;
    float const PhiStored = (float)(PhiRaw);
    float const CosPhi = static_cast<float>(std::cos(static_cast<double>(PhiRaw)));
    float const SinPhi = static_cast<float>(std::sin(static_cast<double>(PhiStored)));
    FMatrix Spin;
    Spin.D[0][0] = CosPhi;
    Spin.D[0][1] = -SinPhi;
    Spin.D[0][2] = 0.0f;
    Spin.D[1][0] = SinPhi;
    Spin.D[1][1] = CosPhi;
    Spin.D[1][2] = 0.0f;
    Spin.D[2][0] = 0.0f;
    Spin.D[2][1] = 0.0f;
    Spin.D[2][2] = 1.0f;

    FVector Spun;
    Acquire3DMathEngine()->TransformMatrix(Spun, Spin, Cone);

    double const ZeroY = (double)(Dir.y) * GRandZero;
    double const ZeroX = (double)(Dir.x) * GRandZero;
    double const Dot = ZeroX + ZeroY + Dir.z;
    float const DotStored = (float)(Dot);

    Vector Result;
    if (!(Dot < GRandAlignedDot))
    {
        Result.x = Spun.X;
        Result.y = Spun.Y;
        Result.z = Spun.Z;
        return Result;
    }

    double const ZeroZ = (double)(Dir.z) * GRandZero;
    float const AxisX = (float)((double)(Dir.y) - ZeroZ);
    float const AxisY = (float)(ZeroZ - Dir.x);
    float const AxisZ = (float)(ZeroX - ZeroY);

    double const HalfAngle = acos((double)(DotStored)) * GRandHalf;
    double const AxisScale = std::sin(static_cast<double>(HalfAngle)) /
        sqrt((double)(AxisZ) * AxisZ + (double)(AxisY) * AxisY +
             (double)(AxisX) * AxisX);

    FQuaternion Rotation;
    Rotation.W = static_cast<float>(std::cos(static_cast<double>(HalfAngle)));
    Rotation.X = (float)((double)(AxisX) * AxisScale);
    Rotation.Y = (float)((double)(AxisY) * AxisScale);
    Rotation.Z = (float)((double)(AxisZ) * AxisScale);

    FMatrix Frame;
    Acquire3DMathEngine()->QuaternionToMatrix(Frame, Rotation);
    FVector Rotated;
    Acquire3DMathEngine()->TransposeTransform(Rotated, Frame, Spun);

    Result.x = Rotated.X;
    Result.y = Rotated.Y;
    Result.z = Rotated.Z;
    return Result;
}

char* __cdecl safe_strcpy(char* Dest, int Size, char const* Src)
{
    // Common.dll.txt 063047d0-063047f1: strncpy with the full Size (zero padding included),
    // then the last byte forced to NUL. No argument checks; the result is strncpy's (Dest).
    char* const Result = strncpy(Dest, Src, (size_t)Size);
    Dest[Size - 1] = '\0';
    return Result;
}

char* __cdecl safe_strcat(char* Dest, int Size, char const* Src)
{
    // Common.dll.txt 06304800-06304838: signed compare of strlen(Dest) against Size; when the
    // destination already fills Size the function returns null (EAX was zeroed for the
    // SCASB). Otherwise strncat with Size - Length (strncat's terminator may land on
    // Dest[Size], as in retail) and the last byte forced to NUL.
    int const Length = (int)strlen(Dest);
    if (!(Length < Size))
        return nullptr;

    char* const Result = strncat(Dest, Src, (size_t)(Size - Length));
    Dest[Size - 1] = '\0';
    return Result;
}

wchar_t* __cdecl safe_wcscpy(wchar_t* Dest, int Size, wchar_t const* Src)
{
    // Common.dll.txt 06304840-06304865: Size is a byte count, halved with an unsigned shift;
    // wcsncpy (zero padding included), then element Count - 1 forced to NUL. No checks.
    unsigned int const Count = (unsigned int)Size >> 1;
    wchar_t* const Result = wcsncpy(Dest, Src, Count);
    Dest[(int)Count - 1] = 0;
    return Result;
}

bool __cdecl strequal(char const* A, char const* B)
{
    // Common.dll RVA 0xd4480 (decomp-tracker item 14186). BUG FIX: this was
    // previously ported as a case-INsensitive compare (_stricmp), but the raw
    // disassembly (Common.dll.txt:379962-380015) is a plain byte-for-byte loop
    // with no case-folding (no OR 0x20 / toupper anywhere) -- retail's
    // strequal is case-SENSITIVE, equivalent to strcmp(A,B)==0. Null handling
    // matches retail exactly: both null -> true, exactly one null -> false.
    if (A == nullptr || B == nullptr)
        return A == B;
    return strcmp(A, B) == 0;
}

void __cdecl BuildDirectory(char const* Path)
{
    // Common.dll.txt 06304410-063044cc. No null check on Path (strncpy dereferences it).
    // Every '/' becomes '\\' (each strchr restarts at the buffer start), then each prefix
    // ending at a '\\' -- and the whole path -- is created with _mkdir when _access says it
    // does not exist. A trailing '\\' ends the walk.
    char Buffer[0x104];
    strncpy(Buffer, Path, 0x104);
    Buffer[0x103] = '\0';

    char* Slash = strchr(Buffer, '/');
    while (Slash != nullptr)
    {
        *Slash = '\\';
        Slash = strchr(Buffer, '/');
    }

    if (Buffer[0] == '\0')
        return;

    char* Cursor = Buffer;
    for (;;)
    {
        char* const Separator = strchr(Cursor, '\\');
        if (Separator != nullptr)
            *Separator = '\0';
        if (Buffer[0] != '\0' && _access(Buffer, 0) != 0)
            _mkdir(Buffer);
        if (Separator == nullptr)
            return;
        Cursor = Separator + 1;
        *Separator = '\\';
        if (*Cursor == '\0')
            return;
    }
}

void __cdecl DebugPrint(char const* Format, ...)
{
    // Common.dll.txt 06302dc0-06302deb: Format != null -> vsprintf into a 0x100-byte stack
    // buffer, and nothing else - the retail build sends the text nowhere (the old port's
    // OutputDebugStringA was a port-only addition). Retail's vsprintf (06302ddc) is
    // unbounded, so a text of 0x100 chars or more overruns the frame there as here.
    if (Format == nullptr)
        return;

    char Buffer[0x100];
    va_list Args;
    va_start(Args, Format);
    vsprintf(Buffer, Format, Args);
    va_end(Args);
}

// ============================================================================
// CmnMisc.cpp file-system paths, the single-player / MP-server flags,
// CleanUpDatabases and DllMain (retail 0x06302df0..0x06304880).
// ============================================================================

namespace
{
    const char kCmnMiscSource[] = "E:\\FL\\Scratch\\Source\\Common\\CmnMisc.cpp";
    const unsigned long kPathChars = 0x104;

    // DAT_063fccf8 — static MAX_PATH returned by GetFullFilename.
    char GFullFilename[kPathChars];

    // DAT_063fce0c — bool, IsMPServer flag
    bool GbIsMPServer = false;

    // Desc layout is asserted once, in readfile.h (DOS/UTF prefix 0x34, MEM tail to 0x40).

    void CallIfsGetRootPath(IFileSystem* Filesys, char* Out, unsigned long MaxChars)
    {
        Filesys->GetFileSysPath(Out, MaxChars); // +0x84
    }

    bool AppendAndEnsureDir(char* Dest, char const* Subdir)
    {
        char Folder[kPathChars];
        strncpy(Folder, Dest, kPathChars);
        Folder[kPathChars - 1] = '\0';
        _makepath(Dest, nullptr, Folder, Subdir, nullptr);
        if (_access(Dest, 0) == 0)
            return true;
        if (_mkdir(Dest) == 0)
            return true;
        *Dest = '\0';
        return false;
    }

    // SHGetFolderPathA(0, CsIdl, 0, 0, Dest); failure -> Dest[0] = 0, false (06302f2a /
    // 06302fbd / 063030ca and their JNZ to the "Dest[0] = 0; return false" tail).
    bool FillSpecialFolder(char* const Dest, int CsIdl)
    {
        if (SHGetFolderPathA(nullptr, CsIdl, nullptr, 0, Dest) != 0)
        {
            *Dest = '\0';
            return false;
        }
        return true;
    }
}

// DAT_063ed17c — byte in Common.dll .data, init 01h. True = single-player.
// Definition for the extern declared in common_misc.h.
bool GbSinglePlayer = true;

bool __cdecl SinglePlayer()
{
    // Common.dll.c SinglePlayer @ 0xa3220 — return DAT_063ed17c
    return GbSinglePlayer;
}

void __cdecl SetMultiPlayer(bool bMultiPlayer)
{
    // Common.dll.c SetMultiPlayer @ 0xa3210 — DAT_063ed17c = !param
    GbSinglePlayer = !bMultiPlayer;
}

bool __cdecl IsMPServer()
{
    // Common.dll.c IsMPServer @ 0xa3250
    return GbIsMPServer;
}

void __cdecl SetIsMPServer(bool bIsMPServer)
{
    // Common.dll.c SetIsMPServer @ 0xa3230
    GbIsMPServer = bIsMPServer;
    if (bIsMPServer) {
        GbSinglePlayer = false;
    }
}

void __cdecl CleanUpDatabases()
{
    // 0xa4870 JMP Vibe::FreeDynamic. Dynamic vibe map only — not ArchDB / Groups / class lists.
    Reputation::Vibe::FreeDynamic();
}

void __cdecl GetFileSysPath(char* Dest, char const* RelOrAbs, IFileSystem* Filesys)
{
    // Common.dll.txt 06302df0-06302e9f. Null RelOrAbs -> Dest[0] = 0. Absolute ('\\' first,
    // or a non-empty name with ':' second) -> byte copy into Dest. Otherwise the file
    // system (Filesys, else DALib::Data; both null -> Dest[0] = 0) gives its root through
    // vt+0x84 into a 0x104 buffer and Dest = _makepath(0, root, RelOrAbs, 0).
    if (RelOrAbs == nullptr)
    {
        *Dest = '\0';
        return;
    }

    const bool bAbsSlash = RelOrAbs[0] == '\\';
    const bool bAbsDrive = RelOrAbs[0] != '\0' && RelOrAbs[1] == ':';
    if (bAbsSlash || bAbsDrive)
    {
        char const* Source = RelOrAbs;
        char* Out = Dest;
        char Copied;
        do
        {
            Copied = *Source;
            *Out = Copied;
            ++Source;
            ++Out;
        } while (Copied != '\0');
        return;
    }

    IFileSystem* Resolved = Filesys;
    if (Resolved == nullptr)
        Resolved = DALib::Data;
    if (Resolved == nullptr)
    {
        *Dest = '\0';
        return;
    }

    char RootDir[kPathChars];
    CallIfsGetRootPath(Resolved, RootDir, kPathChars);
    _makepath(Dest, nullptr, RootDir, RelOrAbs, nullptr);
}

char const* __cdecl GetFullFilename(char const* RelOrAbs)
{
    // Common.dll.txt 06302ea0-06302f02: DALib::Data vt+0x84 (root into 0x104), _makepath
    // into a second 0x104 buffer, _fullpath into the static DAT_063fccf8, return it. No
    // absolute-path short-circuit and no null check of DALib::Data.
    IFileSystem* Filesys = DALib::Data;
    char RootDir[kPathChars];
    char Joined[kPathChars];
    CallIfsGetRootPath(Filesys, RootDir, kPathChars);
    _makepath(Joined, nullptr, RootDir, RelOrAbs, nullptr);
    _fullpath(GFullFilename, Joined, kPathChars);
    return GFullFilename;
}

bool __cdecl GetUserLocalDataPath(char* const Dest)
{
    // listing 06302f10 — CSIDL 0x801c, subdir "Freelancer"
    if (!FillSpecialFolder(Dest, CSIDL_FLAG_CREATE | CSIDL_LOCAL_APPDATA))
        return false;
    return AppendAndEnsureDir(Dest, "Freelancer");
}

bool __cdecl GetUserDataPath(char* const Dest)
{
    // listing 06302fa0 — CSIDL 0x8005, "My Games" then "Freelancer"
    if (!FillSpecialFolder(Dest, CSIDL_FLAG_CREATE | CSIDL_PERSONAL))
        return false;
    if (!AppendAndEnsureDir(Dest, "My Games"))
        return false;
    return AppendAndEnsureDir(Dest, "Freelancer");
}

bool __cdecl GetScreenShotPath(char* const Dest)
{
    // listing 063030b0 — CSIDL 0x8027, subdir "FreelancerShots"
    if (!FillSpecialFolder(Dest, CSIDL_FLAG_CREATE | CSIDL_MYPICTURES))
        return false;
    return AppendAndEnsureDir(Dest, "FreelancerShots");
}

void __cdecl DAEngine_create_filesystem(char const* Path, IFileSystem** Out,
                                        char const* Implementation)
{
    // Common.dll.txt 06303140-0630320a. *Out = 0; a null or empty Path returns (with *Out
    // = 0 again). Otherwise a 0x34-byte desc {0x34, "FileSystem", Implementation, Path,
    // 0x80000000, 1, 0, 3, 0x8000080, 0, 0, 0, -1} goes to DALib::Data vt+0x0c
    // (CreateInstance) with Out; *Out still null -> FDUMP "Could not load file" (line 0xd8).
    // No null checks of Out or DALib::Data, and no retry: a "FileSystem" path that is a
    // UTF file is resolved by the DOS file system itself (ReadFile re-enters ICOManager
    // with Parent = the DOS object), not here.
    *Out = nullptr;
    if (Path == nullptr || Path[0] == '\0')
    {
        *Out = nullptr;
        return;
    }

    IFileSystem* Filesys = DALib::Data;
    FFileSystemDesc Desc;
    memset(&Desc, 0, sizeof(Desc));
    Desc.Size = 0x34;
    Desc.ComponentName = "FileSystem";
    Desc.Implementation = Implementation;
    Desc.Path = Path;
    Desc.Access = 0x80000000;
    Desc.ShareMode = 1;
    Desc.Creation = 3;
    Desc.Flags = 0x8000080;
    Desc.Field30 = 0xFFFFFFFF;

    // +0x0C IComponentFactory::CreateInstance -- the FileSystem desc stands in for AGGDESC.
    Filesys->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), reinterpret_cast<void**>(Out));

    if (*Out == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Could not load file %s\n", kCmnMiscSource, 0xd8,
              Path);
    }
}

void __cdecl DAEngine_release_filesystem(IFileSystem** Filesys)
{
    // Common.dll.txt 0627ba10-0627ba28 (ICF-shared with ThornScriptDestroy): *Filesys != 0 ->
    // Release (+0x08), *Filesys = 0. No null check of Filesys itself.
    IFileSystem* Instance = *Filesys;
    if (Instance == nullptr)
        return;

    Instance->Release(); // +0x08
    *Filesys = nullptr;
}

// Common.dll.c 0x6304880 / Common.dll.txt:293107 (FUN_06304880). This is the
// actual user DllMain body: its one caller (0x63920ca) is the CRT's
// _DllMainCRTStartup thunk. Retail: if fdwReason == DLL_PROCESS_ATTACH (1),
// call DisableThreadLibraryCalls(hinstDLL); always return TRUE. lpvReserved
// (the 3rd param) is never referenced by retail, matching Ghidra's 2-param
// recovery. No prior DllMain existed anywhere in src/Common -- MSVC's default
// CRT-supplied DllMain would have covered "always return TRUE" but silently
// skipped the DisableThreadLibraryCalls optimization (harmless but not
// faithful), so this was a real, if minor, functional gap.

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID /*lpvReserved*/)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}


// Common.dll.txt 06304320..06304408. Dist is the first argument (the centre distance), the
// two radii follow. Values the listing keeps on the x87 stack are double here; the result is
// returned in ST0 and narrowed by the float return type.
float __cdecl GetSphereIntersectVol(float Dist, float RadiusA, float RadiusB)
{
    // 06304320..06304335: FCOMP, `TEST AH,0x5; JP` -- the smaller radius is RadiusA only when
    // RadiusA < RadiusB; greater, equal and unordered take RadiusB.
    const double Small = (RadiusA < RadiusB) ? RadiusA : RadiusB;
    // 06304339..0630435a: `TEST AH,0x41; JNZ` -- the larger radius is RadiusA only when
    // RadiusA > RadiusB; less, equal and unordered take RadiusB.
    const double Large = (RadiusA > RadiusB) ? RadiusA : RadiusB;

    // 0630435a..06304377: Large - Small FCOMP Dist, `TEST AH,0x41; JNZ`. Strictly greater: the
    // small sphere lies inside the large one -> its volume, (Small*Small)*Small * 4/3 pi
    // (DAT_063a2718 = 4.1887903f).
    if (Large - Small > Dist)
        return (float)(Small * (Small * Small) * 4.1887903f);

    // 06304378..06304391: Large + Small FCOMP Dist, `TEST AH,0x5; JP`. Strictly less: the spheres
    // are apart -> 0.0f (DAT_06399408). Greater, equal and unordered fall through.
    if ((Large + Small) < Dist)
        return 0.0f;

    // 06304392..06304408: the lens. H1 = ((Dist^2 + Small^2) - Large^2) / (Dist + Dist),
    // H2 = Dist - H1; (Large^3 + Small^3) * 2/3 pi (DAT_063a271c) minus
    // (((H2*Large*Large + H1*Small*Small) - H1^3/3) - H2^3/3) * pi (DAT_0639f520 = 0.33333334f,
    // DAT_0639d208 = 3.1415927f), all on the stack.
    const double D = Dist;
    const double SmallSq = Small * Small;
    const double LargeSq = Large * Large;
    const double H1 = ((D * D + SmallSq) - LargeSq) / (D + D);
    const double H2 = D - H1;
    const double Caps = (LargeSq * Large + SmallSq * Small) * 2.0943952f;
    const double Lens = (((H2 * Large * Large + H1 * Small * Small) - H1 * H1 * H1 * 0.33333334f) -
                         H2 * H2 * H2 * 0.33333334f) * 3.1415927f;
    return (float)(Caps - Lens);
}

// Common.dll.txt 292611-292803 (0x063044d0..0x063046ce). The x87 intermediates are carried in
// double (project /arch:IA32 convention); every FSTP float is a float here.
//   Out1 / Out2: the sphere centre on screen; Out3: its projected radius; Out4: its depth.
//   Returns 1 when the projected disc reaches into the viewport, 0 otherwise.
long __cdecl get_projected_bounding_sphere(ICamera* Camera, Vector const& Center, float Radius, float& Out1, float& Out2, float& Out3, float& Out4)
{
    // 0x063044d8..0x06304502: ICamera slot 4 (+0x10) -- the camera's world transform (the 3x3 at
    // camera +0x04 followed by the position at +0x28, 12 floats) -- and the 3DMathEngine
    // (DAT_063fce08, FUN_06391cf0 on first use) slot 15 InverseTransform: the centre in view space.
    float const* const CameraXform = Camera->GetOrientation();
    FVector View;
    Acquire3DMathEngine()->InverseTransform(View, *reinterpret_cast<FTransform const*>(CameraXform),
                                           reinterpret_cast<FVector const&>(Center));

    // 0x06304505..0x06304516: -ZNear FCOMP z, TEST AH,1 / JNZ -- -near < z (behind the near
    // plane) or unordered -> 0.
    if (!(-(double)(Camera->GetZNear()) >= (double)(View.Z)))
        return 0;
    // 0x0630451c..0x06304527: depth = -z.
    Out4 = -View.Z;

    // 0x06304529..0x06304581: the viewport (slot 5) centre, W * 0.5f + left / H * 0.5f + top
    // (DAT_0639c13c), each FSTP float; InvZ = -1.0 (DAT_063a03f0) / z, FSTP float.
    int const* const Viewport = Camera->GetViewport();
    float const CenterX = (float)(
        (double)(Viewport[2] - Viewport[0]) * (double)(0.5f) +
        (double)(Viewport[0]));
    float const CenterY = (float)(
        (double)(Viewport[3] - Viewport[1]) * (double)(0.5f) +
        (double)(Viewport[1]));
    float const InvZ = (float)(-1.0 / (double)(View.Z));
    // 0x06304585..0x0630458d: x * InvZ, FSTP float.
    float const XOverZ = (float)((double)(View.X) * (double)(InvZ));

    // 0x06304591..0x063045af: slot 11 (FSTP float), then slot 8: near * xscale * (x / z) + CX.
    float const XScale = (float)(Camera->GetXScaleOverNear());
    Out1 = (float)((double)(Camera->GetZNear()) * (double)(XScale) *
                                  (double)(XOverZ) +
                              (double)(CenterX));

    // 0x063045b1..0x063045d8: slot 12 (FSTP float), slot 8: near * yscale * (y * InvZ, kept on the
    // stack) + CY; stored, and the unrounded value stays on the stack for DY below.
    float const YScale = (float)(Camera->GetYScaleOverNear());
    double const ScreenY = (double)(Camera->GetZNear()) * (double)(YScale) *
                               ((double)(View.Y) * (double)(InvZ)) +
                           (double)(CenterY);
    Out2 = (float)(ScreenY);

    // 0x063045da..0x063045f8: the distance, sqrt(x*x + z*z + y*y) in that order, FSTP float.
    double const X = (double)(View.X);
    double const Y = (double)(View.Y);
    double const Z = (double)(View.Z);
    float const Dist = (float)(std::sqrt((X * X + Z * Z) + Y * Y));
    // 0x063045fc..0x0630460f: a radius beyond the distance is cut to it (TEST AH,0x41 / JNZ keeps
    // the radius when it is <= or unordered).
    if (Radius > Dist)
        Radius = Dist;

    // 0x06304613..0x06304625: |Out1 - CX| (the stored float) and |ScreenY - CY| (the stack value),
    // each FSTP float.
    float const DeltaX = (float)(
        std::fabs((double)(Out1) - (double)(CenterX)));
    float const DeltaY = (float)(std::fabs(ScreenY - (double)(CenterY)));

    // 0x06304629..0x06304659: A = atan(|x / z|) (FLD1 / FPATAN), S = asin(Radius / Dist)
    // (_CIasin), tan(A - S) (FPTAN, the 1.0 popped); Delta = |x / z| - tan(A - S), FSTP float.
    float const AbsXOverZ = (float)(std::fabs((double)(XOverZ)));
    double const Angle = std::atan(static_cast<double>(AbsXOverZ));
    double const HalfAngle =
        std::asin((double)(Radius) / (double)(Dist));
    float const Delta =
        (float)((double)(AbsXOverZ) - std::tan(static_cast<double>(Angle - HalfAngle)));

    // 0x0630465d..0x06304679: slot 11 (unrounded) * Delta, FSTP float; slot 8 times that: the
    // projected radius, stored, and kept on the stack.
    float const ScaledDelta = (float)(Camera->GetXScaleOverNear() * (double)(Delta));
    double const ScreenRadius = (double)(Camera->GetZNear()) * (double)(ScaledDelta);
    Out3 = (float)(ScreenRadius);

    // 0x0630467b..0x06304696: (right - left + 1) >> 1 (SAR) + radius, FCOMP DeltaX, TEST AH,0x41 /
    // JNZ -- not beyond DeltaX (or unordered) -> 0.
    int const HalfWidth = (Viewport[2] - Viewport[0] + 1) >> 1;
    if (!((double)(HalfWidth) + ScreenRadius > (double)(DeltaX)))
        return 0;
    // 0x06304698..0x063046b8: DeltaY FCOMPP (bottom - top + 1) >> 1 + radius, TEST AH,5 / JP --
    // only DeltaY strictly below it goes on.
    int const HalfHeight = (Viewport[3] - Viewport[1] + 1) >> 1;
    if (!((double)(DeltaY) < (double)(HalfHeight) + ScreenRadius))
        return 0;
    return 1;
}

// Common.dll.txt 292805-292857 (0x63046d0, raw disasm only, no Ghidra decomp). Pure forwarder:
// calls get_projected_bounding_sphere for the screen-space center (Out1/Out2), half-extent
// (Out3) and depth (Out4, written directly into the caller's OutZ), then turns center+extent
// into edges (ftol of the unrounded x87 sum/difference).
long __cdecl get_projected_bounding_box(ICamera* Camera, Vector const& Center, float Radius, ViewRect* Rect, float& OutZ)
{
    float ScreenX, ScreenY, HalfExtent;
    if (!get_projected_bounding_sphere(Camera, Center, Radius, ScreenX, ScreenY, HalfExtent, OutZ))
    {
        return 0;
    }
    // 0x063046ff..0x06304740: FLD / FSUB (or FADD) float, then ftol -- the unrounded x87 sum is
    // truncated, so the sums are formed in double here.
    Rect->Left   = (long)((double)ScreenX - (double)HalfExtent);
    Rect->Top    = (long)((double)ScreenY - (double)HalfExtent);
    Rect->Right  = (long)((double)ScreenX + (double)HalfExtent);
    Rect->Bottom = (long)((double)ScreenY + (double)HalfExtent);
    return 1;
}

// Common.dll.c 0x4a680, ?ComputeExplosiveImpulses@@YAXJAAVVector@@00@Z. Traced from the raw
// x86 listing (062aa680-062aa977) rather than the decompiled C, which garbles this function
// badly via x87/stack-slot reuse across the two branches.
//
// IEngine (DALib::Engine) slots used: +0xd4 GetInstanceParent, +0xc4 GetInstanceRelative (a
// block {Count:int, Items:Vector[]}; Items[0] at +0x04, Items[1] at +0x10), +0x90
// GetInstancePosition, +0x98 GetInstanceOrientation. I3DMathEngine (DAT_063fc2e4, lazily
// acquired right before each use) +0x2c TransformMatrix. Neither pointer is tested.
namespace
{
    // 062aa790..062aa7d9 (and the two copies at 062aa823 / 062aa8ec): FLD z, y, x;
    // (x*x + y*y) + z*z and FSQRT on the stack; FCOM 0.0f, TEST AH,0x44, JNP skips only an
    // ordered-equal length (NaN normalises); FDIVR 1.0f keeps the reciprocal on the stack and
    // each component is FLD inv / FMUL float / FSTP float.
    void NormalizeExplosionVector(Vector& V)
    {
        double const X = V.x;
        double const Y = V.y;
        double const Z = V.z;
        double const Len = sqrt((X * X + Y * Y) + Z * Z);
        if (Len != 0.0)
        {
            double const Inv = 1.0 / Len;
            V.x = (float)(Inv * V.x);
            V.y = (float)(Inv * V.y);
            V.z = (float)(Inv * V.z);
        }
    }
}

void __cdecl ComputeExplosiveImpulses(long EngineHandle, Vector& OutLinear, Vector& OutAngular1, Vector& OutAngular2)
{
    void* const Instance = (void*)EngineHandle;
    // 062aa680..062aa6b6: parent first (EBP), then the relative block (EBX).
    void* const Parent = DALib::Engine->GetInstanceParent(Instance);
    unsigned long* const Relative = DALib::Engine->GetInstanceRelative(Instance);
    int const Count = (int)Relative[0];
    FVector const* const Items = (FVector const*)(Relative + 1);

    // 062aa6b4..062aa6bf: TEST/JLE and CMP 4/JG -- signed, 1..4 takes the first branch.
    if (Count > 0 && Count <= 4)
    {
        // 062aa6c5..062aa713: Items[1] by the instance's orientation into OutAngular2.
        FMatrix const* const Orient = (FMatrix const*)DALib::Engine->GetInstanceOrientation(Instance);
        FVector Local;
        Acquire3DMathEngine()->TransformMatrix(Local, *Orient, Items[1]);
        OutAngular2.x = Local.X;
        OutAngular2.y = Local.Y;
        OutAngular2.z = Local.Z;
        // 062aa716..062aa73e: + the instance position (FLD pos / FADD out / FSTP float).
        float const* Pos = DALib::Engine->GetInstancePosition(Instance);
        OutAngular2.x = Pos[0] + OutAngular2.x;
        OutAngular2.y = Pos[1] + OutAngular2.y;
        OutAngular2.z = Pos[2] + OutAngular2.z;

        // 062aa72c / 062aa741..062aa78d: Items[0] by the PARENT's orientation (PUSH EBP).
        FMatrix const* const ParentOrient = (FMatrix const*)DALib::Engine->GetInstanceOrientation(Parent);
        Acquire3DMathEngine()->TransformMatrix(Local, *ParentOrient, Items[0]);
        OutAngular1.x = Local.X;
        OutAngular1.y = Local.Y;
        OutAngular1.z = Local.Z;
        NormalizeExplosionVector(OutAngular1);

        // 062aa7db..062aa86f: OutLinear = OutAngular2 - position, normalised.
        Pos = DALib::Engine->GetInstancePosition(Instance);
        OutLinear.x = OutAngular2.x - Pos[0];
        OutLinear.y = OutAngular2.y - Pos[1];
        OutLinear.z = OutAngular2.z - Pos[2];
        NormalizeExplosionVector(OutLinear);
        return;
    }

    // 062aa876..062aa898: OutAngular2 = the instance position.
    float const* const Pos = DALib::Engine->GetInstancePosition(Instance);
    OutAngular2.x = Pos[0];
    OutAngular2.y = Pos[1];
    OutAngular2.z = Pos[2];

    // 062aa89b..062aa930: Items[1] by the parent's orientation into OutAngular1, normalised.
    FMatrix const* const ParentOrient = (FMatrix const*)DALib::Engine->GetInstanceOrientation(Parent);
    FVector Local;
    Acquire3DMathEngine()->TransformMatrix(Local, *ParentOrient, Items[1]);
    OutAngular1.x = Local.X;
    OutAngular1.y = Local.Y;
    OutAngular1.z = Local.Z;
    NormalizeExplosionVector(OutAngular1);

    // 062aa937..062aa969: OutLinear = -OutAngular1 (FCHS, FSTP float).
    OutLinear.x = -OutAngular1.x;
    OutLinear.y = -OutAngular1.y;
    OutLinear.z = -OutAngular1.z;
}
