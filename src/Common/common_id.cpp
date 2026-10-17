// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_id.h"
#include "common_server_types.h"

#include "dacom.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp, the import the CHARACTER_ID comparisons call.
int CallDacomStricmp(const char* Left, const char* Right);

// Retail FLIDMaker CRC state is 0x41c bytes (Common.dll.c:144062–144124).
struct FFlidCrcState
{
    unsigned int Table[256];
    unsigned int RegisterWidth;
    unsigned int CrcWidth;
    unsigned int Polynomial;
    unsigned int XorIn;
    unsigned int XorOut;
    unsigned char bReflectIn;
    unsigned char bUseTablePath;
    unsigned char Pad[2];
    unsigned int RunningCrc;
};

static_assert(sizeof(FFlidCrcState) == 0x41c, "CFLIDMaker CRC state must be 0x41c");

CFLIDMaker* CFLIDMaker::m_pTheMaker = nullptr;

namespace
{
    const unsigned int NicknameCopyBytes = 256;
    const unsigned int MakeIdCopyBytes = 128;
    const unsigned int IdStringHexSlots = 4;
    const unsigned int IdStringHexSlotBytes = 0x20;
    const char FlidMakerSource[] = "E:\\FL\\Scratch\\Source\\Common\\FLIDMaker.cpp";

    char GIdStringHexScratch[IdStringHexSlots * IdStringHexSlotBytes];
    unsigned int GIdStringHexSlot = 0;

    unsigned short GMakeIdTable[256];
    bool bMakeIdTableReady = false;

    void InitMakeIdTable()
    {
        // DAT_063ed230 — CRC-16-CCITT poly 0x1021, MSB-first table (Common.dll.c:145676).
        if (bMakeIdTableReady)
            return;

        unsigned int ByteIndex = 0;
        do
        {
            unsigned int Work = ByteIndex << 8;
            int Bit = 8;
            do
            {
                if ((Work & 0x8000u) == 0)
                    Work = Work << 1;
                else
                    Work = (Work << 1) ^ 0x1021u;
                Work &= 0xFFFFu;
                Bit -= 1;
            } while (Bit != 0);
            GMakeIdTable[ByteIndex] = (unsigned short)Work;
            ByteIndex += 1;
        } while (ByteIndex < 256);

        bMakeIdTableReady = true;
    }

    unsigned int ReflectBits(unsigned int Value, int BitCount)
    {
        // Common.dll.c:144078–144091 / 144108–144119 — overlay each dest bit from LSB of Source.
        unsigned int Result = Value;
        unsigned int Source = Value;
        int BitIndex = 0;
        while (BitIndex < BitCount)
        {
            unsigned int Mask = 1u << ((BitCount - BitIndex) - 1);
            if ((Source & 1u) == 0)
                Result = Result & ~Mask;
            else
                Result = Result | Mask;
            Source >>= 1;
            BitIndex += 1;
        }
        return Result;
    }

    void AsciiToLower(char* Text, unsigned int Length)
    {
        unsigned int Index = 0;
        while (Index < Length)
        {
            unsigned char Char = (unsigned char)Text[Index];
            if (Char >= 'A' && Char <= 'Z')
                Text[Index] = (char)(Char + 32);
            Index += 1;
        }
    }

    void UpdateCrc(FFlidCrcState* State, unsigned char Byte)
    {
        // FUN_0630d330 — thiscall on CRC state, byte on the stack (Common.dll.c:144216).
        if (State->bReflectIn != 0)
        {
            if (State->bUseTablePath != 0)
            {
                unsigned int Crc = State->RunningCrc;
                State->RunningCrc = State->Table[(Crc & 0xFFu) ^ Byte] ^ (Crc >> 8);
                return;
            }
            unsigned int Shift = State->CrcWidth - 8u;
            State->RunningCrc =
                ((unsigned int)Byte << Shift) ^
                State->Table[State->RunningCrc & 0xFFu] ^
                (State->RunningCrc >> 8);
            return;
        }

        unsigned int Crc = State->RunningCrc;
        unsigned int Shift = State->CrcWidth - 8u;
        if (State->bUseTablePath != 0)
        {
            State->RunningCrc = State->Table[((Crc >> Shift) & 0xFFu) ^ Byte] ^ (Crc << 8);
            return;
        }
        State->RunningCrc = State->Table[(Crc >> Shift) & 0xFFu] ^ (Crc << 8) ^ Byte;
    }

    unsigned int FinalizeStaticId(FFlidCrcState* State)
    {
        // Common.dll.c:144185–144209
        unsigned int Width32 = State->RegisterWidth;
        unsigned int CrcWidth = State->CrcWidth;
        unsigned int Msb = 1u << (CrcWidth - 1u);
        unsigned int Mask = (Msb * 2u - 1u) | 1u;
        unsigned int Xored = Mask & (State->RunningCrc ^ State->XorOut);

        unsigned int Folded = 0;
        if (State->bReflectIn == 0)
        {
            Folded = Xored << (Width32 - CrcWidth);
        }
        else
        {
            unsigned int ShiftPos = 8;
            unsigned int Remain = Xored;
            if (Width32 > 7)
            {
                do
                {
                    unsigned int Byte = Remain & 0xFFu;
                    Folded = Folded | (Byte << (Width32 - ShiftPos));
                    ShiftPos += 8;
                    Remain >>= 8;
                } while (ShiftPos <= Width32);
            }
        }

        return (Folded >> (Width32 - CrcWidth)) | 0x80000000u;
    }
}

CFLIDMaker::CFLIDMaker()
    : CrcState(nullptr)
    , NextDynamicId(1)
{
}

CFLIDMaker::~CFLIDMaker()
{
    // Common.dll.txt 0630d030-0630d03d: operator delete of the CRC state when it is set.
    if (CrcState != nullptr)
        ::operator delete(CrcState);
}

CFLIDMaker* __cdecl CFLIDMaker::GetInstance()
{
    // Common.dll.c:143990 — VC6 operator_new returns null; use nothrow.
    if (m_pTheMaker == nullptr)
    {
        m_pTheMaker = new (std::nothrow) CFLIDMaker();
        if (m_pTheMaker == nullptr)
            return nullptr;
    }
    return m_pTheMaker;
}

void __cdecl CFLIDMaker::Shutdown()
{
    // Common.dll.c:144024 — delete table, then the singleton.
    if (m_pTheMaker != nullptr)
    {
        delete m_pTheMaker;
        m_pTheMaker = nullptr;
    }
}

int CFLIDMaker::Initialize()
{
    // Common.dll.c:144045
    if (CrcState != nullptr)
        return 1;

    FFlidCrcState* State = (FFlidCrcState*)::operator new(sizeof(FFlidCrcState), std::nothrow);
    if (State == nullptr)
    {
        CrcState = nullptr;
        return 0;
    }

    State->RegisterWidth = 0x20;
    State->CrcWidth = 0x1e;
    State->Polynomial = 0x8005;
    State->XorIn = 0;
    State->XorOut = 0;
    State->bReflectIn = 1;
    State->bUseTablePath = 1;
    State->RunningCrc = 0; // 0630d0e5; the two pad bytes +0x416/+0x417 are not written

    unsigned int ByteIndex = 0;
    do
    {
        unsigned int Work = ByteIndex;
        if (State->bReflectIn != 0)
            Work = ReflectBits(ByteIndex, 8);

        unsigned int Msb = 1u << (State->CrcWidth - 1u);
        Work = Work << (State->CrcWidth - 8u);
        int Bit = 8;
        do
        {
            if ((Msb & Work) == 0)
                Work = Work << 1;
            else
                Work = (Work * 2u) ^ State->Polynomial;
            Bit -= 1;
        } while (Bit != 0);

        if (State->bReflectIn != 0 && (int)State->CrcWidth > 0)
            Work = ReflectBits(Work, (int)State->CrcWidth);

        unsigned int Mask = (Msb * 2u - 1u) | 1u;
        State->Table[ByteIndex] = Mask & Work;
        ByteIndex += 1;
    } while (ByteIndex < 0x100);

    CrcState = State;
    return 1;
}

unsigned int CFLIDMaker::CreateStaticID(char const* Name)
{
    // Common.dll.txt 0630d1f0-0630d320. No table yet -> Initialize (its result is not
    // checked: a failed allocation leaves CrcState null and the next read faults, as in
    // retail). Null Name -> 0. Length = strlen(Name); strlen+1 bytes are copied into a
    // 0x100-byte stack buffer (unbounded in retail - a name of 0x100+ bytes is undefined
    // behaviour; the port copies at most 0xff bytes and hashes what it copied), _strlwr,
    // RunningCrc = XorIn, FUN_0630d330 per byte for Length bytes, then the fold below.
    if (CrcState == nullptr)
        Initialize();

    if (Name == nullptr)
        return 0;

    unsigned int Length = (unsigned int)strlen(Name);
    if (Length > NicknameCopyBytes - 1u)
        Length = NicknameCopyBytes - 1u;

    char Local[NicknameCopyBytes];
    memcpy(Local, Name, Length);
    Local[Length] = '\0';
    AsciiToLower(Local, Length);

    CrcState->RunningCrc = CrcState->XorIn;
    unsigned int Remain = Length;
    char const* Ptr = Local;
    while (Remain > 0)
    {
        UpdateCrc(CrcState, (unsigned char)*Ptr);
        Ptr += 1;
        Remain -= 1;
    }

    return FinalizeStaticId(CrcState);
}

unsigned int CFLIDMaker::AllocateObjectID()
{
    // Common.dll.c:64598
    unsigned int Id = NextDynamicId;
    NextDynamicId = Id + 1;
    return Id & 0x3FFFFFFFu;
}

unsigned int __cdecl CreateID(char const* Name)
{
    // Common.dll.c:144253
    CFLIDMaker* Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", FlidMakerSource, 0x88,
              "Failed to get pointer to global ID maker instance.\n");
        return 0;
    }
    return Maker->CreateStaticID(Name);
}

void ID_String::clear()
{
    // Common.dll.c:180414 / ?clear@ID_String@@QAEXXZ
    Id = 0;
}

bool ID_String::IsEmpty() const
{
    // Common.dll.c:180426 / ?IsEmpty@ID_String@@QBE_NXZ
    return Id == 0;
}

char const* ID_String::get_string() const
{
    // Common.dll.c:180437 / ?get_string@ID_String@@QBEPBDXZ
    // Four 0x20-byte slots at DAT_0640a780; counter DAT_0658a818. Formats Id as "0x"+_itoa(hex).
    unsigned int SlotOffset = (GIdStringHexSlot & 3u) * IdStringHexSlotBytes;
    GIdStringHexSlot += 1;
    char* Slot = GIdStringHexScratch + SlotOffset;
    Slot[0] = '0';
    Slot[1] = 'x';
    _itoa((int)Id, Slot + 2, 16);
    return Slot;
}

// ---------------------------------------------------------------------------
// CHARACTER_ID — the seven symbols retail Common.dll exports. See the export
// table transcribed in common_server_types.h; all seven are COMMON_API there,
// so all seven must have exactly one out-of-line definition, here.
// ---------------------------------------------------------------------------

CHARACTER_ID::CHARACTER_ID()
{
    // Common.dll.txt 0x1030:  MOV EAX,this / MOV byte ptr [EAX],0x0 / RET
    // Retail only writes Buffer[0]; the port zeroes the whole buffer so that a
    // slot copied wholesale never carries uninitialized bytes. No retail code
    // reads past the terminator, so this is a harmless deviation.
    memset(Buffer, 0, sizeof(Buffer));
}

bool CHARACTER_ID::is_valid() const
{
    // Common.dll.txt 0x1040:  MOV DL,[this] / XOR EAX,EAX / TEST DL,DL / SETNZ AL
    return Buffer[0] != '\0';
}

void CHARACTER_ID::invalidate()
{
    // Common.dll.txt 0x1050: writes ONE byte to 0 (empty string), not the whole buffer.
    Buffer[0] = '\0';
}

CHARACTER_ID const& CHARACTER_ID::operator=(CHARACTER_ID const& other)
{
    // Common.dll.c:181542 (0xce720): byte-copy loop that stops after copying the
    // terminating 0 byte — equivalent to strcpy, not a full-buffer memcpy.
    strcpy(Buffer, other.Buffer);
    return *this;
}

bool operator<(CHARACTER_ID const& lhs, CHARACTER_ID const& rhs)
{
    // Common.dll.txt 0632e740-0632e75c: DACOM.DLL stricmp(lhs, rhs), SETL.
    return CallDacomStricmp(lhs.Buffer, rhs.Buffer) < 0;
}

bool operator==(CHARACTER_ID const& lhs, CHARACTER_ID const& rhs)
{
    // Common.dll.txt 0632e760-0632e778: DACOM.DLL stricmp(lhs, rhs), NEG/SBB/INC = (result == 0).
    return CallDacomStricmp(lhs.Buffer, rhs.Buffer) == 0;
}

bool operator!=(CHARACTER_ID const& lhs, CHARACTER_ID const& rhs)
{
    // Common.dll.txt 0632e780-0632e799: DACOM.DLL stricmp(lhs, rhs), NEG/SBB/NEG = (result != 0).
    return CallDacomStricmp(lhs.Buffer, rhs.Buffer) != 0;
}

unsigned int __cdecl MakeLocationID(unsigned int BaseId, char const* Name)
{
    // Common.dll.txt 062662d0-06266300: sprintf(local[0x100], "%X_%s", BaseId, Name), then
    // CreateID(local). No null or length checks (a null Name prints "(null)" in both CRTs).
    // Retail's sprintf is unbounded - a text of 0x100+ characters is undefined behaviour;
    // the port truncates at the buffer.
    char Local[0x100];
    _snprintf(Local, sizeof(Local), "%X_%s", BaseId, Name);
    Local[sizeof(Local) - 1] = '\0';
    return CreateID(Local);
}

unsigned short __cdecl MakeId(char const* Name)
{
    // Common.dll.txt 0630e510-0630e57c: strcpy Name into a 0x80-byte stack buffer, _strlwr it,
    // then CRC = 0xFFFF; per byte until the NUL: CRC = DAT_063ed230[(CRC & 0xff) ^ c] ^
    // (CRC >> 8). No null or length checks. Retail's copy is unbounded - a name of 0x80+
    // bytes is undefined behaviour; the port copies at most 0x7f bytes. DAT_063ed230 is
    // initialised data in retail (0x0000, 0x1021, 0x2042, ...); the port computes the same
    // table on first use.
    InitMakeIdTable();

    char Local[MakeIdCopyBytes];
    unsigned int Length = 0;
    while (Length < MakeIdCopyBytes - 1 && Name[Length] != '\0')
    {
        Local[Length] = Name[Length];
        Length += 1;
    }
    Local[Length] = '\0';
    AsciiToLower(Local, Length);

    unsigned short Crc = 0xFFFF;
    unsigned int Remain = Length;
    char const* Ptr = Local;
    while (Remain > 0)
    {
        unsigned int Index = (Crc & 0xFFu) ^ (unsigned char)*Ptr;
        Crc = (unsigned short)(GMakeIdTable[Index] ^ (Crc >> 8));
        Ptr += 1;
        Remain -= 1;
    }
    return Crc;
}
