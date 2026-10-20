// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "portable_rpc_quantization.h"
#include "portable_double_load.h"
#include "portable_vector_load.h"
#include "rpc_server_channel_codec.h"
#include "common_goods.h" // RS-P2: EquipDesc, EquipDescVector, StringAlloc (0x24/0x25)

// ===== shared SERVER helpers =====
#include "common_text.h"
#include <new>

// RS-P2: the 127-scale attitude quantizer and its inverse (CREATEGUIDED 0x37: RC
// 06b5c4bf..06b5c504, 06b5c862..06b5c8ae; RS 06bef98f.., 06befd32..). The two
// doubles are RC 06b6c1e0 / 06b6c1e8, RS 06c0df40 / 06c0df48.
static const double GRpcServerScale127 = 127.0;
static const double GRpcServerInverse127 = 1.0 / 127.0;

unsigned char RpcServerQuantize127(const float* Value) {
    return static_cast<unsigned char>(PortableRpcQuantization::QuantizeByte(Value, GRpcServerScale127));
}

void RpcServerExpand127(unsigned char Encoded, float* Value) {
    const int Quantized = static_cast<signed char>(Encoded);
    PortableRpcQuantization::ExpandInteger(Quantized, GRpcServerInverse127, Value);
}

// --- create-object block: 0x04 CREATESHIP and 0x42 CREATESOLAR (RS-P10) ---
// Addresses are RS unless marked; the RC encoder 06b50e50 is instruction-identical to
// RS 06c08640, the RC decoder 06b51750 differs only where the #if branches say.
#include "server_createinfo.h" // CEqObjCreateInfo (fill / read)
#include <map>
#include <string>

// The two doubles behind the 255-scale health bytes: RS 06c0e270 (255.0) and
// 06c0e660 (1/255).
static const double GRpcServerScale255 = 255.0;
static const double GRpcServerInverse255 = 1.0 / 255.0;

// FLD float, FMUL double 255.0, MSVCRT _ftol (truncating 64-bit), low byte
// (06c08bb8..06c08bc1 equipment health, 06c08e01..06c08e0a collision health).
static unsigned char RpcServerQuantize255(const float* Value) {
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Quantized;
    __asm {
        mov ecx, Value
        fld dword ptr [ecx]
        fmul qword ptr GRpcServerScale255
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Quantized
        fldcw OldControl
    }
    return static_cast<unsigned char>(Quantized);
}

// The byte ZERO-extended, FILD dword, FMUL double 1/255, FSTP float (06c09446..06c09460,
// 06c09724..06c09743).
static void RpcServerExpand255(unsigned char Encoded, float* Value) {
    const int Widened = static_cast<int>(Encoded);
    PortableRpcQuantization::ExpandInteger(Widened, GRpcServerInverse255, Value);
}

// 06c089bc..06c089e6, the hull byte: FILD the int hit points, FSTP to a float local
// (rounded before the divide), GetArchMaxHitPts, FILD it, FDIVR by the float, FMUL
// 255.0, _ftol, low byte. A zero maximum divides to an infinity whose _ftol result
// (0x8000000000000000) has a low byte of 0, as in retail.
static unsigned char RpcServerQuantizeHull(int HitPts, unsigned int ArchetypeId) {
    float Points;
    __asm {
        fild dword ptr HitPts
        fstp dword ptr Points
    }
    const int MaxHitPts = Archetype::GetArchMaxHitPts(ArchetypeId);
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Quantized;
    __asm {
        fild dword ptr MaxHitPts
        fdivr dword ptr Points
        fmul qword ptr GRpcServerScale255
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Quantized
        fldcw OldControl
    }
    return static_cast<unsigned char>(Quantized);
}

// 06c0928a..06c092b4, the inverse: GetArchMaxHitPts, FILD it, FILD the zero-extended
// byte, FMUL 1/255, FMULP, _ftol; the low dword is stored.
static int RpcServerExpandHull(unsigned char Encoded, unsigned int ArchetypeId) {
    const int MaxHitPts = Archetype::GetArchMaxHitPts(ArchetypeId);
    const int Widened = static_cast<int>(Encoded);
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Result;
    __asm {
        fild dword ptr MaxHitPts
        fild dword ptr Widened
        fmul qword ptr GRpcServerInverse255
        fmulp st(1), st
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Result
        fldcw OldControl
    }
    return static_cast<int>(Result);
}

// The hardpoint dictionary. Names: RC .data:06b73708, RS .data:06c13700 (42 entries,
// null-terminated, identical in both DLLs). The map is RC/RS .data's CRT-initialised
// std::map<std::string,int> (RS 06c13bbc), the count RC 06b73f48 / RS 06c13bd0.
static const char* const GRpcServerHardpointNames[] = {
    "HpCM01", "HpCargo01", "HpCargo02", "HpContrail01", "HpContrail02",
    "HpDockLight01", "HpDockLight02", "HpDockLight03", "HpHeadlight", "HpMine01",
    "HpRunningLight01", "HpRunningLight02", "HpRunningLight03", "HpRunningLight04",
    "HpRunningLight05", "HpRunningLight06", "HpRunningLight07", "HpRunningLight10",
    "HpRunningLight11", "HpRunningLight12", "HpRunningLight13", "HpShield01",
    "HpThruster01", "HpTurret01", "HpTurret02", "HpTurret03", "HpTurret04", "HpTurret05",
    "HpTurret06", "HpTurret07", "HpTurret08", "HpTurret09", "HpTurret_U1_01",
    "HpTurret_U1_02", "HpTurret_U1_03", "HpTurret_U1_04", "HpTurret_U1_05",
    "HpTurret_U3_01", "HpWeapon01", "HpWeapon02", "HpWeapon03", "HpWeapon04",
    nullptr
};
static std::map<std::string, int> GRpcServerHardpointMap;
static unsigned int GRpcServerHardpointCount = 0;

// RC 06b4fee0 / RS 06bed590: count = 0; if the first name is non-null, per name
// map[std::string(name)] = code (06bed651 insert-or-find, 06bed66e store), code and
// count incremented together, until the next name is null. Codes start at 1.
void RpcServerBuildHardpointDictionary() {
    GRpcServerHardpointCount = 0;
    int Code = 1;
    for (const char* const* Entry = &GRpcServerHardpointNames[0]; *Entry != nullptr; ++Entry) {
        GRpcServerHardpointMap[std::string(*Entry)] = Code;
        ++Code;
        ++GRpcServerHardpointCount;
    }
}

// RC 06b50040 / RS 06bed6f0: find(std::string(Name)); the mapped code, or 0 on a miss.
// The caller keeps the low byte (06c08b0b).
static int RpcServerHardpointNameToCode(const char* Name) {
    std::map<std::string, int>::const_iterator It =
        GRpcServerHardpointMap.find(std::string(Name));
    if (It == GRpcServerHardpointMap.end()) return 0;
    return It->second;
}

// RC 06b50140 / RS 06bed7f0: 0 -> null, above the count -> null, else names[code - 1].
static const char* RpcServerHardpointCodeToName(unsigned int Code) {
    if (Code == 0) return nullptr;
    if (Code > GRpcServerHardpointCount) return nullptr;
    return GRpcServerHardpointNames[Code - 1u];
}

FRpcServerCreateObjectFields::FRpcServerCreateObjectFields()
    : ObjId(0), ArchetypeId(0), DockingBaseId(0), VibeGroupId(0), VoiceId(0), HitPts(0),
      Field_0x6c(0) {
    Position.x = 0.0f;                                   // 06be539e..06be53a4
    Position.y = 0.0f;
    Position.z = 0.0f;
    Orientation.w = 1.0f;                                // 06bde8d0
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
}

// The per-record flag byte, built at 06c08ac6..06c08b2d and taken apart at
// 06c09399..06c0961c.
static const unsigned char kRpcServerEquipEquipped = 0x01;
static const unsigned char kRpcServerEquipField0f = 0x02;
static const unsigned char kRpcServerEquipWideCount = 0x04;
static const unsigned char kRpcServerEquipWideId = 0x08;
static const unsigned char kRpcServerEquipHardpointText = 0x10;
static const unsigned char kRpcServerEquipNoHardpoint = 0x20;
static const unsigned char kRpcServerEquipFullHealth = 0x40;
static const unsigned char kRpcServerEquipUnitCount = 0x80;

// 06c08ac0..06c08d3e, one record; the node's record is used untested (06c08ac0).
static bool RpcServerEncodeCreateObjectEquip(FRpcWireStream& Stream,
    const FRpcServerEquipRecord* Record) {
    unsigned char Flags = 0;
    if (Record->bEquipped != 0) Flags = kRpcServerEquipEquipped;              // 06c08ac8
    if (Record->Field_0x0f != 0) Flags |= kRpcServerEquipField0f;            // 06c08ad5
    if (Record->Count > 0xff) Flags |= kRpcServerEquipWideCount;             // 06c08ae4 signed
    if (Record->Id > 0xffu) Flags |= kRpcServerEquipWideId;                  // 06c08aeb unsigned
    unsigned char Code = 0;                                                  // 06c08aca
    if (Record->Hardpoint == nullptr) {
        Flags |= kRpcServerEquipNoHardpoint;                                 // 06c08afb
    } else {
        Code = static_cast<unsigned char>(RpcServerHardpointNameToCode(Record->Hardpoint));
        if (Code == 0) Flags |= kRpcServerEquipHardpointText;                // 06c08b11
    }
    if (Record->Count == 1) Flags |= kRpcServerEquipUnitCount;               // 06c08b14
    // 06c08b1d FLD float / FCOMP double 1.0 (06c0bab0) / TEST AH,0x44 / JP: only an
    // ordered equal sets the bit.
    if (static_cast<double>(Record->Health) == 1.0) Flags |= kRpcServerEquipFullHealth;
    if (!Stream.Put(&Flags, 1u)) return false;                               // 06c08b56
    if ((Flags & kRpcServerEquipUnitCount) == 0) {                           // 06c08b5e JS
        if ((Flags & kRpcServerEquipWideCount) != 0) {
            if (!Stream.Put(&Record->Count, 4u)) return false;               // 06c08b89
        } else {
            const unsigned char Narrow = static_cast<unsigned char>(
                static_cast<unsigned int>(Record->Count) & 0xffu);           // 06c08b90
            if (!Stream.Put(&Narrow, 1u)) return false;
        }
    }
    if ((Flags & kRpcServerEquipFullHealth) == 0) {                          // 06c08bb3
        const unsigned char Health = RpcServerQuantize255(&Record->Health);   // before the check
        if (!Stream.Put(&Health, 1u)) return false;                          // 06c08bef
    }
    const unsigned short SmallArch = Archetype::LargeIDToSmallID(Record->ArchId); // 06c08bf8
    if (!Stream.Put(&SmallArch, 2u)) return false;                           // 06c08c27
    if ((Flags & kRpcServerEquipWideId) != 0) {
        if (!Stream.Put(&Record->Id, 2u)) return false;                      // 06c08c5a
    } else {
        const unsigned char Narrow = static_cast<unsigned char>(Record->Id & 0xffu);
        if (!Stream.Put(&Narrow, 1u)) return false;                          // 06c08c7f
    }
    if ((Flags & kRpcServerEquipNoHardpoint) != 0) return true;              // 06c08c88
    if ((Flags & kRpcServerEquipHardpointText) != 0) {
        // 06c08c99 sends the LOW BYTE of the size, 06c08cc1 then copies the whole word.
        const unsigned char Length = static_cast<unsigned char>(Record->HardpointSize & 0xffu);
        if (!Stream.Put(&Length, 1u)) return false;
        // 06c08ce8..06c08d0a: REP MOVSD/MOVSB into the cached destination.
        return Stream.PutBlock(Record->Hardpoint, Record->HardpointSize);    // 06c08cfb
    }
    return Stream.Put(&Code, 1u);                                            // 06c08d38
}

// RC 06b50e50 / RS 06c08640.
unsigned int RpcServerCreateObjectEncode(FPacketBase& Packet, FRpcWireStream& Stream,
    const FRpcServerCreateObjectFields& Fields) {
    (void)Packet;
    if (!Stream.Put(&Fields.ObjId, 4u)) return 0x16u;                         // 06c08680
    // 06c0868c: the archetype is narrowed before its two bytes are checked.
    const unsigned short SmallArch = Archetype::LargeIDToSmallID(Fields.ArchetypeId);
    if (!Stream.Put(&SmallArch, 2u)) return 0x16u;                            // 06c086c0
    if (!Stream.Put(&Fields.DockingBaseId, 4u)) return 0x16u;                 // 06c086f6
    if (!Stream.Put(&Fields.VibeGroupId, 4u)) return 0x16u;                   // 06c0872b
    // +0x34 goes out before +0x30; +0x38 / +0x3c never travel.
    if (!Stream.Put(&Fields.Costume.Body, 4u)) return 0x16u;                  // 06c08760
    if (!Stream.Put(&Fields.Costume.Head, 4u)) return 0x16u;                  // 06c08795
    const unsigned char IdCount = static_cast<unsigned char>(
        static_cast<unsigned int>(Fields.Costume.Count) & 0xffu);            // 06c08797
    if (!Stream.Put(&IdCount, 1u)) return 0x16u;                              // 06c087c9
    // 06c087d8: Count dwords as Count << 2 bytes, one check, then the REP MOVSD/MOVSB
    // copy into the cached destination (06c087fc..06c0881c).
    if (!Stream.PutBlock(Fields.Costume.Ids,
            static_cast<unsigned int>(Fields.Costume.Count) << 2))
        return 0x16u;
    if (!Stream.Put(&Fields.VoiceId, 4u)) return 0x16u;                       // 06c08854
    // 06c0885c..06c088aa: one 12-byte check, then an integer copy of the three dwords.
    if (!Stream.Put(&Fields.Position, 12u)) return 0x16u;
    // 06c088af..06c088f2: all four bytes are computed first, x, y, z, w (memory order
    // is w first), and only then checked and written one by one.
    const unsigned char QuatX = RpcServerQuantize127(&Fields.Orientation.x);
    const unsigned char QuatY = RpcServerQuantize127(&Fields.Orientation.y);
    const unsigned char QuatZ = RpcServerQuantize127(&Fields.Orientation.z);
    const unsigned char QuatW = RpcServerQuantize127(&Fields.Orientation.w);
    if (!Stream.Put(&QuatX, 1u)) return 0x16u;                                // 06c08925
    if (!Stream.Put(&QuatY, 1u)) return 0x16u;                                // 06c08958
    if (!Stream.Put(&QuatZ, 1u)) return 0x16u;                                // 06c0898b
    if (!Stream.Put(&QuatW, 1u)) return 0x16u;                                // 06c089ba
    const unsigned char Hull = RpcServerQuantizeHull(Fields.HitPts, Fields.ArchetypeId);
    if (!Stream.Put(&Hull, 1u)) return 0x16u;                                 // 06c08a14
    if (SinglePlayer()) {                                                    // 06c08a1a
        if (!Stream.Put(&Fields.Field_0x6c, 4u)) return 0x16u;                // 06c08a52
    }
    // 06c08a5d (06bdc830): the handle's list pointer. A null list ships a zero word.
    const std::list<FRpcServerEquipRecord*>* Records = Fields.Equipment.Records;
    if (Records == nullptr) {
        const unsigned short None = 0;
        if (!Stream.Put(&None, 2u)) return 0x16u;                             // 06c08d8a
    } else {
        // 06c08a71: the low word of the list size.
        const unsigned short Count = static_cast<unsigned short>(Records->size() & 0xffffu);
        if (!Stream.Put(&Count, 2u)) return 0x16u;                            // 06c08aa1
        for (std::list<FRpcServerEquipRecord*>::const_iterator It = Records->begin();
             It != Records->end(); ++It) {
            if (!RpcServerEncodeCreateObjectEquip(Stream, *It)) return 0x16u;
        }
    }
    // 06c08d9b: the low byte of the collision list size, then per node the low byte of
    // the Id word and the health as a 255-scale byte.
    const unsigned char GroupCount =
        static_cast<unsigned char>(Fields.CollisionGroups.size() & 0xffu);
    if (!Stream.Put(&GroupCount, 1u)) return 0x16u;                           // 06c08dc8
    for (std::list<CollisionGroupDesc>::const_iterator Group = Fields.CollisionGroups.begin();
         Group != Fields.CollisionGroups.end(); ++Group) {
        const unsigned char GroupId = static_cast<unsigned char>(Group->Id & 0xffu);
        if (!Stream.Put(&GroupId, 1u)) return 0x16u;                          // 06c08dfb
        const unsigned char Health = RpcServerQuantize255(&Group->Health);
        if (!Stream.Put(&Health, 1u)) return 0x16u;                           // 06c08e30
    }
    if (!SinglePlayer()) return 0;                                           // 06c08e3c
    // 06c08e7e: the damage list's size as a dword, then per entry the SubObjId word,
    // the Fate dword and the HitPts float through FLD/FSTP (06c08f0e).
    const unsigned int DamageCount = static_cast<unsigned int>(Fields.Damage.Entries.size());
    if (!Stream.Put(&DamageCount, 4u)) return 0x16u;
    for (std::list<DamageEntry>::const_iterator Entry = Fields.Damage.Entries.begin();
         Entry != Fields.Damage.Entries.end(); ++Entry) {
        if (!Stream.Put(&Entry->SubObjId, 2u)) return 0x16u;                  // 06c08ec1
        if (!Stream.Put(&Entry->Fate, 4u)) return 0x16u;                      // 06c08ee9
        if (!Stream.PutFloat(Entry->HitPts)) return 0x16u;                    // 06c08f14
    }
    return 0;
}

// 06c09341..06c09627, one record into the zeroed 0x18 block. Returns 0, the stream
// error, or (RC) 5 when the hardpoint text allocation fails.
static unsigned int RpcServerDecodeCreateObjectEquip(FPacketBase& Packet,
    FRpcWireStream& Stream, FRpcServerEquipRecord* Record) {
    unsigned char Flags = 0;
    if (!Stream.Get(&Flags, 1u)) return Packet.MarshalError;                 // 06c09393
    Record->Field_0x0f = static_cast<unsigned char>((Flags >> 1) & 1u);      // 06c093aa
    Record->bEquipped = static_cast<unsigned char>(Flags & 1u);              // 06c093ad
    if ((Flags & kRpcServerEquipUnitCount) != 0) {                           // 06c093b0 JNS
        Record->Count = 1;
    } else if ((Flags & kRpcServerEquipWideCount) != 0) {
        if (!Stream.Get(&Record->Count, 4u)) return Packet.MarshalError;     // 06c093e8
    } else {
        unsigned char Narrow = 0;
        if (!Stream.Get(&Narrow, 1u)) return Packet.MarshalError;            // 06c0940b
        Record->Count = static_cast<int>(Narrow);
    }
    if ((Flags & kRpcServerEquipFullHealth) != 0) {
        Record->Health = 1.0f;                                               // 06c0941d 0x3f800000
    } else {
        unsigned char Health = 0;
        if (!Stream.Get(&Health, 1u)) return Packet.MarshalError;            // 06c09449
        RpcServerExpand255(Health, &Record->Health);
    }
    unsigned short SmallArch = 0;
    if (!Stream.Get(&SmallArch, 2u)) return Packet.MarshalError;             // 06c09489
    Record->ArchId = Archetype::SmallIDToLargeID(SmallArch);                 // 06c09493
    if ((Flags & kRpcServerEquipWideId) != 0) {
        if (!Stream.Get(&Record->Id, 2u)) return Packet.MarshalError;        // 06c094cf
    } else {
        unsigned char Narrow = 0;
        if (!Stream.Get(&Narrow, 1u)) return Packet.MarshalError;            // 06c094f8
        Record->Id = static_cast<unsigned short>(Narrow);
    }
    if ((Flags & kRpcServerEquipNoHardpoint) != 0) {                         // 06c0961c
        Record->HardpointSize = 0;
        Record->Hardpoint = nullptr;
        return 0;
    }
    if ((Flags & kRpcServerEquipHardpointText) != 0) {
        unsigned char Length = 0;
        if (!Stream.Get(&Length, 1u)) return Packet.MarshalError;            // 06c0953c
        Record->HardpointSize = static_cast<unsigned short>(Length);
        if (Record->HardpointSize == 0) return 0;                            // 06c0954e
#if defined(REMOTECLIENT_EXPORTS)
        // RC 06b51d95..06b51dca: the run is checked first (not committed); a failed
        // allocation returns 5.
        if (Packet.MarshalCursor + Record->HardpointSize > Packet.MarshalLimit)
            Packet.MarshalError = 0x17u;
        if (Packet.MarshalError != 0) return Packet.MarshalError;
        Record->Hardpoint = static_cast<char*>(
            ::operator new(Record->HardpointSize, std::nothrow));
        if (Record->Hardpoint == nullptr) return 5u;
#else
        // 06c0955a: allocated and stored before the check, no null test.
        Record->Hardpoint = static_cast<char*>(
            ::operator new(Record->HardpointSize, std::nothrow));
#endif
        if (!Stream.Get(Record->Hardpoint, Record->HardpointSize)) return Packet.MarshalError;
        return 0;
    }
    unsigned char Code = 0;
    if (!Stream.Get(&Code, 1u)) return Packet.MarshalError;                  // 06c095d2
    // 06c095dd: an unknown code leaves the record without a hardpoint.
    const char* Name = RpcServerHardpointCodeToName(static_cast<unsigned int>(Code));
    if (Name == nullptr) return 0;
    // 06c095eb..06c09618: strlen + 1 as a word, a bare operator new of that word, and a
    // copy up to and including the terminator.
    Record->HardpointSize = static_cast<unsigned short>(std::strlen(Name) + 1u);
    Record->Hardpoint = static_cast<char*>(
        ::operator new(static_cast<unsigned int>(Record->HardpointSize), std::nothrow));
    const char* Text = Name;
    char* Out = Record->Hardpoint;
    char Value;
    do {
        Value = *Text++;
        *Out++ = Value;
    } while (Value != '\0');
    return 0;
}

// RC 06b51750 / RS 06c08f40. A failed read returns at once; a record being decoded is
// not linked yet and is leaked, as in retail (reproduced on purpose).
unsigned int RpcServerCreateObjectDecode(FPacketBase& Packet, FRpcWireStream& Stream,
    FRpcServerCreateObjectFields& Fields) {
    if (!Stream.Get(&Fields.ObjId, 4u)) return Packet.MarshalError;          // 06c08f7f
    unsigned short SmallArch = 0;
    if (!Stream.Get(&SmallArch, 2u)) return Packet.MarshalError;             // 06c08fa7
    Fields.ArchetypeId = Archetype::SmallIDToLargeID(SmallArch);             // 06c08fb7
    if (!Stream.Get(&Fields.DockingBaseId, 4u)) return Packet.MarshalError;  // 06c08fe4
    if (!Stream.Get(&Fields.VibeGroupId, 4u)) return Packet.MarshalError;    // 06c09014
    if (!Stream.Get(&Fields.Costume.Body, 4u)) return Packet.MarshalError;   // 06c0903e
    if (!Stream.Get(&Fields.Costume.Head, 4u)) return Packet.MarshalError;   // 06c0906e
    Fields.Costume.LeftHand = 0;                                             // 06c09071
    Fields.Costume.RightHand = 0;                                            // 06c09074
    unsigned char IdCount = 0;
    if (!Stream.Get(&IdCount, 1u)) return Packet.MarshalError;               // 06c09098
    Fields.Costume.Count = static_cast<int>(IdCount);                        // 06c090a4
    if (Fields.Costume.Count != 0) {
        const unsigned int Bytes = static_cast<unsigned int>(Fields.Costume.Count) << 2;
#if defined(REMOTECLIENT_EXPORTS)
        // RC 06b518bd..06b518ec: the run is checked first (not committed); a failed
        // allocation returns 5.
        if (Packet.MarshalCursor + Bytes > Packet.MarshalLimit) Packet.MarshalError = 0x17u;
        if (Packet.MarshalError != 0) return Packet.MarshalError;
        Fields.Costume.Ids = static_cast<unsigned int*>(::operator new(Bytes, std::nothrow));
        if (Fields.Costume.Ids == nullptr) return 5u;
#else
        // 06c090b1: allocated and stored before the check, no null test.
        Fields.Costume.Ids = static_cast<unsigned int*>(::operator new(Bytes, std::nothrow));
#endif
        if (!Stream.Get(Fields.Costume.Ids, Bytes)) return Packet.MarshalError;
    }
    if (!Stream.Get(&Fields.VoiceId, 4u)) return Packet.MarshalError;        // 06c09138
    // 06c0913b..06c0916a: one 12-byte check, then an integer copy (06bda4c0).
    if (!Stream.Get(&Fields.Position, 12u)) return Packet.MarshalError;
    // 06c0916d..06c0920a: the four bytes are read first, then widened with MOVSX and
    // stored x, y, z, w.
    unsigned char QuatX = 0;
    unsigned char QuatY = 0;
    unsigned char QuatZ = 0;
    unsigned char QuatW = 0;
    if (!Stream.Get(&QuatX, 1u)) return Packet.MarshalError;
    if (!Stream.Get(&QuatY, 1u)) return Packet.MarshalError;
    if (!Stream.Get(&QuatZ, 1u)) return Packet.MarshalError;
    if (!Stream.Get(&QuatW, 1u)) return Packet.MarshalError;
    RpcServerExpand127(QuatX, &Fields.Orientation.x);                       // 06c0922d
    RpcServerExpand127(QuatY, &Fields.Orientation.y);                       // 06c0923e
    RpcServerExpand127(QuatZ, &Fields.Orientation.z);                       // 06c0924f
    RpcServerExpand127(QuatW, &Fields.Orientation.w);                       // 06c0925c
    unsigned char Hull = 0;
    if (!Stream.Get(&Hull, 1u)) return Packet.MarshalError;                  // 06c09280
    Fields.HitPts = RpcServerExpandHull(Hull, Fields.ArchetypeId);           // 06c092b4
    if (SinglePlayer()) {                                                    // 06c092b7
        if (!Stream.Get(&Fields.Field_0x6c, 4u)) return Packet.MarshalError; // 06c092ed
    }
    unsigned short EquipCount = 0;
    if (!Stream.Get(&EquipCount, 2u)) return Packet.MarshalError;            // 06c0931c
    // The counter lives in the stack slot the collision loop below copies from.
    unsigned int EquipIndex = 0;                                             // 06c09328
    for (; static_cast<unsigned short>(EquipIndex) < EquipCount; ++EquipIndex) {
        // 06c09343 operator new(0x18); a null record skips every read (06c0934f).
        FRpcServerEquipRecord* Record = static_cast<FRpcServerEquipRecord*>(
            ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
        if (Record == nullptr) continue;
        Record->ArchId = 0;
        Record->Count = 0;
        Record->Health = 0.0f;
        Record->Id = 0;
        Record->bEquipped = 0;
        Record->Field_0x0f = 0;
        Record->HardpointSize = 0;
        Record->Hardpoint = nullptr;
        const unsigned int Error = RpcServerDecodeCreateObjectEquip(Packet, Stream, Record);
        if (Error != 0) return Error;
        Fields.Equipment.PushBack(Record);                                   // 06c09627
    }
    unsigned char GroupCount = 0;
    if (!Stream.Get(&GroupCount, 1u)) return Packet.MarshalError;            // 06c0966c
    for (unsigned int Group = 0; Group < static_cast<unsigned int>(GroupCount); ++Group) {
        // 06c09684..06c096bc: the node is linked and the size bumped first; its value
        // is the stack pair {equipment counter, residue} (06bd59e0). The Id word is
        // overwritten below and the high word of the counter is 0; the residue dword is
        // overwritten by the health read or the packet fails, so 0 here is exact.
        CollisionGroupDesc Value;
        Value.Id = static_cast<unsigned short>(EquipIndex & 0xffffu);
        Value.Pad02 = static_cast<unsigned short>(EquipIndex >> 16);
        Value.Health = 0.0f;
        Fields.CollisionGroups.push_back(Value);
        CollisionGroupDesc& Linked = Fields.CollisionGroups.back();
        unsigned char GroupId = 0;
        if (!Stream.Get(&GroupId, 1u)) return Packet.MarshalError;           // 06c096e4
        Linked.Id = static_cast<unsigned short>(GroupId);                    // 06c096f5
        unsigned char Health = 0;
        if (!Stream.Get(&Health, 1u)) return Packet.MarshalError;            // 06c0971e
        RpcServerExpand255(Health, &Linked.Health);                          // 06c09743
    }
    if (!SinglePlayer()) return 0;                                           // 06c0974c
    unsigned int DamageCount = 0;
    if (!Stream.Get(&DamageCount, 4u)) return Packet.MarshalError;           // 06c09783
    for (unsigned int Index = 0; Index < DamageCount; ++Index) {
        // 06c09797..06c097f1: the entry is seeded {SUBOBJ_ID_NONE, -1.0f, 0}, linked,
        // and the list size bumped before the reads fill it in place.
        DamageEntry Value;
        Value.SubObjId = SUBOBJ_ID_NONE;
        Value.HitPts = -1.0f;
        Value.Fate = DamageEntry::SubObjFate::None;
        Fields.Damage.Entries.push_back(Value);
        ++Fields.Damage.Count;
        DamageEntry& Linked = Fields.Damage.Entries.back();
        if (!Stream.Get(&Linked.SubObjId, 2u)) return Packet.MarshalError;   // 06c0982b
        if (!Stream.Get(&Linked.Fate, 4u)) return Packet.MarshalError;       // 06c09863
        if (!Stream.Get(&Linked.HitPts, 4u)) return Packet.MarshalError;     // 06c09898 MOV
    }
    return 0;
}

#if defined(REMOTECLIENT_EXPORTS)
// RC 06b382f0 (__cdecl). Every store is an integer move except the equipment
// records' health (FLD/FSTP inside the record builder).
void RpcServerCreateObjectFill(const CEqObjCreateInfo* Info,
    FRpcServerCreateObjectFields* Fields) {
    Fields->ObjId = Info->ObjId;                                             // 06b38300
    Fields->ArchetypeId = Info->ArchetypeNickname;                           // 06b38306
    Fields->DockingBaseId = Info->DockingBaseId;                             // 06b3830c
    Fields->VibeGroupId = Info->VibeGroupId;                                 // 06b38312
    Fields->VoiceId = Info->VoiceId;                                         // 06b38318
    Fields->HitPts = Info->HitPts;                                           // 06b3831e
    Fields->Field_0x6c = Info->Field_0x6c;                                   // 06b38324
    Fields->Costume.Head = Info->CostumeHead;                                // 06b3832a
    Fields->Costume.Body = Info->CostumeBody;
    Fields->Costume.LeftHand = Info->CostumeLeftHand;
    Fields->Costume.RightHand = Info->CostumeRightHand;                      // 06b3833c
    Fields->Costume.Count = Info->CostumeAccessoryCount;                     // 06b38342
    // 06b38348: a bare operator new of Count << 2, whatever the count, stored untested;
    // the copy (signed, count > 0) is not bounded by the eight slots: reproduced on
    // purpose.
    Fields->Costume.Ids = static_cast<unsigned int*>(::operator new(
        static_cast<unsigned int>(Info->CostumeAccessoryCount) << 2, std::nothrow));
    const unsigned int* Accessories = &Info->CostumeAccessory[0];
    for (int Index = 0; Index < Info->CostumeAccessoryCount; ++Index) {
        Fields->Costume.Ids[Index] = Accessories[Index];                     // 06b38365
    }
    std::memcpy(&Fields->Position, &Info->Position, 12u);                    // 06b38378..
    std::memcpy(&Fields->Orientation, &Info->Orientation, 16u);              // ..06b3839f
    // 06b383a2..06b3845d: one record per EquipDesc, no filter -- the record builder
    // RC 06b38210 inlined with bAll true.
    RpcServerEquipHandleFromVector(&Info->Equipment, &Fields->Equipment, true);
    // 06b38461..06b384ea: Common's list<CollisionGroupDesc>::operator= inlined.
    Fields->CollisionGroups = Info->CollisionGroups;
    // 06b384ec..06b38585: DamageList::operator= inlined (the list, then the byte and
    // three dwords of its tail).
    Fields->Damage = Info->Damage;
}
#endif

#if defined(REMOTESERVER_EXPORTS)
// Common's VC6 vector<EquipDesc>::insert(end, 1, Value) as RS 06bd5398 calls it:
// without room, a block of size + max(size, 1) elements, the old elements and the
// new one copy-constructed into it, the old block freed; otherwise one copy at end.
static void RpcServerEquipDescVectorInsertEnd(EquipDescVector* Vector, const EquipDesc& Value) {
    if (static_cast<unsigned int>(Vector->Cap - Vector->End) < 1u) {
        const unsigned int Size = (Vector->Begin == nullptr)
            ? 0u : static_cast<unsigned int>(Vector->End - Vector->Begin);
        const unsigned int Capacity = Size + ((1u < Size) ? Size : 1u);
        EquipDesc* Block = static_cast<EquipDesc*>(
            ::operator new(sizeof(EquipDesc) * Capacity, std::nothrow));
        EquipDesc* Out = Block;
        for (EquipDesc* In = Vector->Begin; In != Vector->End; ++In, ++Out) {
            new (Out) EquipDesc(*In);
        }
        new (Out) EquipDesc(Value);
        ::operator delete(Vector->Begin);
        Vector->Cap = Block + Capacity;
        Vector->End = Block + Size + 1u;
        Vector->Begin = Block;
        return;
    }
    new (Vector->End) EquipDesc(Value);
    ++Vector->End;
}

// RS 06bd52a0 (__cdecl).
void RpcServerCreateObjectRead(const FRpcServerCreateObjectFields* Fields,
    CEqObjCreateInfo* Info) {
    Info->ObjId = Fields->ObjId;                                             // 06bd52b0
    Info->ArchetypeNickname = Fields->ArchetypeId;                           // 06bd52b6
    Info->DockingBaseId = Fields->DockingBaseId;                             // 06bd52bc
    Info->CostumeBody = Fields->Costume.Body;                                // 06bd52c2
    Info->CostumeHead = Fields->Costume.Head;                                // 06bd52c8
    Info->CostumeRightHand = Fields->Costume.RightHand;                      // 06bd52ce
    Info->CostumeLeftHand = Fields->Costume.LeftHand;                        // 06bd52d5
    Info->CostumeAccessoryCount = Fields->Costume.Count;                     // 06bd52e0
    // 06bd52e3..06bd5302: a signed count > 0, then an unsigned loop that re-reads the
    // packet's count; not bounded by the eight slots: reproduced on purpose.
    if (Fields->Costume.Count > 0) {
        unsigned int* Accessories = &Info->CostumeAccessory[0];
        for (unsigned int Index = 0; Index < static_cast<unsigned int>(Fields->Costume.Count);
             ++Index) {
            Accessories[Index] = Fields->Costume.Ids[Index];
        }
    }
    Info->VibeGroupId = Fields->VibeGroupId;                                 // 06bd5307
    Info->VoiceId = Fields->VoiceId;                                         // 06bd530d
    Info->HitPts = Fields->HitPts;                                           // 06bd5313
    Info->Field_0x6c = Fields->Field_0x6c;                                   // 06bd5319
    // 06bd531c..06bd532b: FLD z, y, x, then FSTP x, y, z.
    const float* PositionIn = &Fields->Position.x;
    float* PositionOut = &Info->Position.x;
    PortableVectorLoad::Copy(PositionOut, PositionIn);
    // 06bd532e..06bd533b: an identity quaternion is built in place, then 06bd533e..
    // 06bd5353 overwrite it with integer moves.
    Info->Orientation.w = 1.0f;
    Info->Orientation.z = 0.0f;
    Info->Orientation.y = 0.0f;
    Info->Orientation.x = 0.0f;
    std::memcpy(&Info->Orientation, &Fields->Orientation, 16u);
    // 06bd5356..06bd5426 (06bdc840 = the handle's list pointer): a null or empty list
    // adds nothing; otherwise per record (used untested) a default EquipDesc appended
    // and filled through Common's setters in this order.
    const std::list<FRpcServerEquipRecord*>* Records = Fields->Equipment.Records;
    if (Records != nullptr) {
        for (std::list<FRpcServerEquipRecord*>::const_iterator It = Records->begin();
             It != Records->end(); ++It) {
            const FRpcServerEquipRecord* Record = *It;
            const EquipDesc Blank;                                           // 06bd5388
            RpcServerEquipDescVectorInsertEnd(&Info->Equipment, Blank);      // 06bd5398
            EquipDesc* Slot = Info->Equipment.End - 1;                       // 06bd53a3
            Slot->set_arch_id(Record->ArchId);                               // 06bd53a9
            Slot->set_count(Record->Count);                                  // 06bd53b5
            Slot->set_id(Record->Id);                                        // 06bd53c4
            Slot->set_equipped(Record->bEquipped);                           // 06bd53d2 raw byte
            Slot->set_status(Record->Health);                                // 06bd53de
            if (Record->HardpointSize > 0) {                                 // 06bd53e4
                CacheString Hardpoint;
                Hardpoint.Value = StringAlloc(Record->Hardpoint, false);     // 06bd53f1
                Slot->set_hardpoint(Hardpoint);                              // 06bd5405
            } else {
                Slot->make_internal();                                       // 06bd540f
            }
        }
    }
    // 06bd542c..06bd54f2: Common's list<CollisionGroupDesc>::operator= inlined.
    Info->CollisionGroups = Fields->CollisionGroups;
    // 06bd54f4..06bd5591: DamageList::operator= inlined.
    Info->Damage = Fields->Damage;
}
#endif
// ===== end shared SERVER helpers =====

#if defined(REMOTECLIENT_EXPORTS)

int RpcRegisterRemoteClientServerPackets(FPacketSerializer* Serializer) {
    // 06b49675/06b49d77: a NULL serializer returns 4 before any registration.
    if (Serializer == nullptr) {
        return 4;
    }
    // Native order of 06b4967d..06b49d6e; every RegisterType result is ignored.
    Serializer->RegisterType(0x20001, "FLPACKET_SERVER_CONNECTRESPONSE", &FRpcConnectResponsePacket::Factory);
    Serializer->RegisterType(0x20002, "FLPACKET_SERVER_LOGINRESPONSE", &FRpcLoginResponsePacket::Factory);
    Serializer->RegisterType(0x20005u, "FLPACKET_SERVER_DAMAGEOBJECT",
        &FRpcServerDamageObjectPacket::Factory);
    Serializer->RegisterType(0x20006u, "FLPACKET_SERVER_DESTROYOBJECT",
        &FRpcServerDestroyObjectPacket::Factory);
    Serializer->RegisterType(0x20004u, "FLPACKET_SERVER_CREATESHIP", &FRpcServerCreateShipPacket::Factory);
    Serializer->RegisterType(0x20044u, "FLPACKET_SERVER_REQUEST_RETURNED",
        &FRpcServerRequestReturnedPacket::Factory);
    Serializer->RegisterType(0x20003u, "FLPACKET_SERVER_CHARACTERINFO",
        &FRpcServerCharacterInfoPacket::Factory);
    Serializer->RegisterType(0x20007u, "FLPACKET_SERVER_LAUNCH", &FRpcServerLaunchPacket::Factory);
    Serializer->RegisterType(0x20008u, "FLPACKET_SERVER_CHARSELECTVERIFIED",
        &FRpcServerCharSelectVerifiedPacket::Factory);
    Serializer->RegisterType(0x2000au, "FLPACKET_SERVER_ACTIVATEOBJECT",
        &FRpcServerActivateObjectPacket::Factory);
    Serializer->RegisterType(0x2000bu, "FLPACKET_SERVER_LAND", &FRpcServerLandPacket::Factory);
    Serializer->RegisterType(0x2000eu, "FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST",
        &FRpcServerGfCompleteNewsBroadcastListPacket::Factory);
    Serializer->RegisterType(0x20010u, "FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST",
        &FRpcServerGfCompleteMissionComputerListPacket::Factory);
    Serializer->RegisterType(0x2000fu, "FLPACKET_SERVER_GFCOMPLETECHARLIST",
        &FRpcServerGfCompleteCharListPacket::Factory);
    Serializer->RegisterType(0x20011u, "FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST",
        &FRpcServerGfCompleteScriptBehaviorListPacket::Factory);
    Serializer->RegisterType(0x20013u, "FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST",
        &FRpcServerGfCompleteAmbientScriptListPacket::Factory);
    Serializer->RegisterType(0x20021u, "FLPACKET_SERVER_SYSTEM_SWITCH_OUT",
        &FRpcServerSystemSwitchOutPacket::Factory);
    Serializer->RegisterType(0x20022u, "FLPACKET_SERVER_SYSTEM_SWITCH_IN",
        &FRpcServerSystemSwitchInPacket::Factory);
    Serializer->RegisterType(0x20023u, "FLPACKET_SERVER_SETSHIPARCH", &FRpcSetShipArchPacket::Factory);
    Serializer->RegisterType(0x20024u, "FLPACKET_SERVER_SETEQUIPMENT",
        &FRpcServerSetEquipmentPacket::Factory);
    Serializer->RegisterType(0x20025u, "FLPACKET_SERVER_SETCARGO", &FRpcServerSetCargoPacket::Factory);
    Serializer->RegisterType(0x20027u, "FLPACKET_SERVER_REQUESTCREATESHIPRESP",
        &FRpcServerRequestCreateShipRespPacket::Factory);
    Serializer->RegisterType(0x2000du, "FLPACKET_SERVER_SETSTARTROOM",
        &FRpcServerSetStartRoomPacket::Factory);
    Serializer->RegisterType(0x20015u, "FLPACKET_SERVER_GFDESTROYCHARACTER",
        &FRpcServerGfDestroyCharacterPacket::Factory);
    Serializer->RegisterType(0x20014u, "FLPACKET_SERVER_GFDESTROYNEWSBROADCAST",
        &FRpcServerGfDestroyNewsBroadcastPacket::Factory);
    Serializer->RegisterType(0x20016u, "FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER",
        &FRpcServerGfDestroyMissionComputerPacket::Factory);
    Serializer->RegisterType(0x20017u, "FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR",
        &FRpcServerGfDestroyScriptBehaviorPacket::Factory);
    Serializer->RegisterType(0x20019u, "FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT",
        &FRpcServerGfDestroyAmbientScriptPacket::Factory);
    Serializer->RegisterType(0x2001au, "FLPACKET_SERVER_GFSCRIPTBEHAVIOR",
        &FRpcServerGfScriptBehaviorPacket::Factory);
    Serializer->RegisterType(0x2001fu, "FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT",
        &FRpcServerGfUpdateAmbientScriptPacket::Factory);
    Serializer->RegisterType(0x2005bu, "FLPACKET_SERVER_MISSIONSAVEA",
        &FRpcServerMissionSaveAPacket::Factory);
    Serializer->RegisterType(0x2001du, "FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER",
        &FRpcServerGfUpdateMissionComputerPacket::Factory);
    Serializer->RegisterType(0x2001eu, "FLPACKET_SERVER_GFUPDATENEWSBROADCAST",
        &FRpcServerGfUpdateNewsBroadcastPacket::Factory);
    Serializer->RegisterType(0x20020u, "FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE",
        &FRpcServerGfMissionVendorAcceptancePacket::Factory);
    Serializer->RegisterType(0x2005au, "FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY",
        &FRpcServerGfMissionVendorWhyEmptyPacket::Factory);
    Serializer->RegisterType(0x20026u, "FLPACKET_SERVER_GFUPDATECHAR",
        &FRpcServerGfUpdateCharPacket::Factory);
    Serializer->RegisterType(0x20028u, "FLPACKET_SERVER_CREATELOOT", &FRpcServerCreateLootPacket::Factory);
    Serializer->RegisterType(0x20029u, "FLPACKET_SERVER_SETREPUTATION",
        &FRpcServerSetReputationPacket::Factory);
    Serializer->RegisterType(0x2002au, "FLPACKET_SERVER_ADJUSTATTITUDE",
        &FRpcServerAdjustAttitudePacket::Factory);
    Serializer->RegisterType(0x2002bu, "FLPACKET_SERVER_SETGROUPFEELINGS",
        &FRpcServerSetGroupFeelingsPacket::Factory);
    Serializer->RegisterType(0x2002cu, "FLPACKET_SERVER_CREATEMINE", &FRpcServerCreateMinePacket::Factory);
    Serializer->RegisterType(0x2002du, "FLPACKET_SERVER_CREATECOUNTER",
        &FRpcServerCreateCounterPacket::Factory);
    Serializer->RegisterType(0x20031u, "FLPACKET_SERVER_EXPLODEASTEROIDMINE",
        &FRpcServerExplodeAsteroidMinePacket::Factory);
    Serializer->RegisterType(0x2004du, "FLPACKET_SERVER_LOADHINT", &FRpcServerLoadHintPacket::Factory);
    Serializer->RegisterType(0x2004eu, "FLPACKET_SERVER_SETDIRECTIVE",
        &FRpcServerSetDirectivePacket::Factory);
    Serializer->RegisterType(0x20037u, "FLPACKET_SERVER_CREATEGUIDED",
        &FRpcServerCreateGuidedPacket::Factory);
    Serializer->RegisterType(0x20038u, "FLPACKET_SERVER_ITEMTRACTORED",
        &FRpcServerItemTractoredPacket::Factory);
    Serializer->RegisterType(0x2002fu, "FLPACKET_SERVER_SETREMOVEITEM",
        &FRpcServerSetRemoveItemPacket::Factory);
    Serializer->RegisterType(0x2002eu, "FLPACKET_SERVER_SETADDITEM", &FRpcServerSetAddItemPacket::Factory);
    Serializer->RegisterType(0x20030u, "FLPACKET_SERVER_SETCASH", &FRpcServerSetCashPacket::Factory);
    Serializer->RegisterType(0x20032u, "FLPACKET_SERVER_REQUESTSPACESCRIPT",
        &FRpcServerRequestSpaceScriptPacket::Factory);
    Serializer->RegisterType(0x20033u, "FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE",
        &FRpcServerSetMissionObjectiveStatePacket::Factory);
    Serializer->RegisterType(0x20047u, "FLPACKET_SERVER_CFGINTERFACENOTIFICATION",
        &FRpcServerCfgInterfaceNotificationPacket::Factory);
    Serializer->RegisterType(0x20046u, "FLPACKET_SERVER_MARKOBJ", &FRpcServerMarkObjPacket::Factory);
    Serializer->RegisterType(0x20035u, "FLPACKET_SERVER_SETMISSIONOBJECTIVES",
        &FRpcServerSetMissionObjectivesPacket::Factory);
    Serializer->RegisterType(0x20034u, "FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE",
        &FRpcServerReplaceMissionObjectivePacket::Factory);
    Serializer->RegisterType(0x20043u, "FLPACKET_SERVER_SET_STORY_CUE",
        &FRpcServerSetStoryCuePacket::Factory);
    Serializer->RegisterType(0x2004bu, "FLPACKET_SERVER_SET_CAMERA", &FRpcServerSetCameraPacket::Factory);
    Serializer->RegisterType(0x2004cu, "FLPACKET_SERVER_REVERT_CAMERA",
        &FRpcServerRevertCameraPacket::Factory);
    Serializer->RegisterType(0x20045u, "FLPACKET_SERVER_SET_MISSION_MESSAGE",
        &FRpcServerSetMissionMessagePacket::Factory);
    Serializer->RegisterType(0x2003cu, "FLPACKET_SERVER_REPAIROBJECT",
        &FRpcServerRepairObjectPacket::Factory);
    Serializer->RegisterType(0x2003eu, "FLPACKET_SERVER_SETNUMKILLS", &FRpcServerSetNumKillsPacket::Factory);
    Serializer->RegisterType(0x2003fu, "FLPACKET_SERVER_SETMISSIONSUCCESSES",
        &FRpcServerSetMissionSuccessesPacket::Factory);
    Serializer->RegisterType(0x20040u, "FLPACKET_SERVER_SETMISSIONFAILURES",
        &FRpcServerSetMissionFailuresPacket::Factory);
    Serializer->RegisterType(0x20041u, "FLPACKET_SERVER_BURNFUSE", &FRpcServerBurnFusePacket::Factory);
    Serializer->RegisterType(0x20042u, "FLPACKET_SERVER_CREATESOLAR", &FRpcServerCreateSolarPacket::Factory);
    Serializer->RegisterType(0x20048u, "FLPACKET_SERVER_SETCOLLISIONGROUPS",
        &FRpcServerSetCollisionGroupsPacket::Factory);
    Serializer->RegisterType(0x20049u, "FLPACKET_SERVER_SETHULLSTATUS", &FRpcSetHullStatusPacket::Factory);
    Serializer->RegisterType(0x2004au, "FLPACKET_SERVER_SETGUIDEDTARGET",
        &FRpcServerSetGuidedTargetPacket::Factory);
    Serializer->RegisterType(0x2004fu, "FLPACKET_SERVER_SENDCOMM", &FRpcServerSendCommPacket::Factory);
    Serializer->RegisterType(0x20051u, "FLPACKET_SERVER_USE_ITEM", &FRpcServerUseItemPacket::Factory);
    Serializer->RegisterType(0x20039u, "FLPACKET_SERVER_SCANNOTIFY", &FRpcServerScanNotifyPacket::Factory);
    Serializer->RegisterType(0x20052u, "FLPACKET_SERVER_PLAYERLIST", &FRpcServerPlayerListPacket::Factory);
    Serializer->RegisterType(0x2003du, "FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE",
        &FRpcServerRemoteObjectCargoUpdatePacket::Factory);
    Serializer->RegisterType(0x20054u, "FLPACKET_SERVER_MISCOBJUPDATE",
        &FRpcServerMiscObjUpdatePacket::Factory);
    Serializer->RegisterType(0x20053u, "FLPACKET_SERVER_FORMATION_UPDATE",
        &FRpcServerFormationUpdatePacket::Factory);
    Serializer->RegisterType(0x20055u, "FLPACKET_SERVER_OBJECTCARGOUPDATE",
        &FRpcServerObjectCargoUpdatePacket::Factory);
    Serializer->RegisterType(0x20056u, "FLPACKET_SERVER_SENDNNMESSAGE",
        &FRpcServerSendNNMessagePacket::Factory);
    Serializer->RegisterType(0x20057u, "FLPACKET_SERVER_SET_MUSIC", &FRpcServerSetMusicPacket::Factory);
    Serializer->RegisterType(0x20058u, "FLPACKET_SERVER_CANCEL_MUSIC", &FRpcServerCancelMusicPacket::Factory);
    Serializer->RegisterType(0x20059u, "FLPACKET_SERVER_PLAY_SOUND_EFFECT",
        &FRpcServerPlaySoundEffectPacket::Factory);
    return 0;
}


#elif defined(REMOTESERVER_EXPORTS)

int RpcRegisterRemoteServerServerPackets(FPacketSerializer* Serializer) {
    // 06be4135/06be4837: a NULL serializer returns 4 before any registration.
    if (Serializer == nullptr) {
        return 4;
    }
    // Native order of 06be413d..06be482e; every RegisterType result is ignored.
    Serializer->RegisterType(0x20001, "FLPACKET_SERVER_CONNECTRESPONSE", &FRpcConnectResponsePacket::Factory);
    Serializer->RegisterType(0x20002, "FLPACKET_SERVER_LOGINRESPONSE", &FRpcLoginResponsePacket::Factory);
    Serializer->RegisterType(0x20005u, "FLPACKET_SERVER_DAMAGEOBJECT",
        &FRpcServerDamageObjectPacket::Factory);
    Serializer->RegisterType(0x20006u, "FLPACKET_SERVER_DESTROYOBJECT",
        &FRpcServerDestroyObjectPacket::Factory);
    Serializer->RegisterType(0x20004u, "FLPACKET_SERVER_CREATESHIP", &FRpcServerCreateShipPacket::Factory);
    Serializer->RegisterType(0x20044u, "FLPACKET_SERVER_REQUEST_RETURNED",
        &FRpcServerRequestReturnedPacket::Factory);
    Serializer->RegisterType(0x20003u, "FLPACKET_SERVER_CHARACTERINFO",
        &FRpcServerCharacterInfoPacket::Factory);
    Serializer->RegisterType(0x20007u, "FLPACKET_SERVER_LAUNCH", &FRpcServerLaunchPacket::Factory);
    Serializer->RegisterType(0x20008u, "FLPACKET_SERVER_CHARSELECTVERIFIED",
        &FRpcServerCharSelectVerifiedPacket::Factory);
    Serializer->RegisterType(0x2000au, "FLPACKET_SERVER_ACTIVATEOBJECT",
        &FRpcServerActivateObjectPacket::Factory);
    Serializer->RegisterType(0x2000bu, "FLPACKET_SERVER_LAND", &FRpcServerLandPacket::Factory);
    Serializer->RegisterType(0x2000eu, "FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST",
        &FRpcServerGfCompleteNewsBroadcastListPacket::Factory);
    Serializer->RegisterType(0x20010u, "FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST",
        &FRpcServerGfCompleteMissionComputerListPacket::Factory);
    Serializer->RegisterType(0x2000fu, "FLPACKET_SERVER_GFCOMPLETECHARLIST",
        &FRpcServerGfCompleteCharListPacket::Factory);
    Serializer->RegisterType(0x20011u, "FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST",
        &FRpcServerGfCompleteScriptBehaviorListPacket::Factory);
    Serializer->RegisterType(0x20013u, "FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST",
        &FRpcServerGfCompleteAmbientScriptListPacket::Factory);
    Serializer->RegisterType(0x20021u, "FLPACKET_SERVER_SYSTEM_SWITCH_OUT",
        &FRpcServerSystemSwitchOutPacket::Factory);
    Serializer->RegisterType(0x20022u, "FLPACKET_SERVER_SYSTEM_SWITCH_IN",
        &FRpcServerSystemSwitchInPacket::Factory);
    Serializer->RegisterType(0x20023u, "FLPACKET_SERVER_SETSHIPARCH", &FRpcSetShipArchPacket::Factory);
    Serializer->RegisterType(0x20024u, "FLPACKET_SERVER_SETEQUIPMENT",
        &FRpcServerSetEquipmentPacket::Factory);
    Serializer->RegisterType(0x20025u, "FLPACKET_SERVER_SETCARGO", &FRpcServerSetCargoPacket::Factory);
    Serializer->RegisterType(0x20027u, "FLPACKET_SERVER_REQUESTCREATESHIPRESP",
        &FRpcServerRequestCreateShipRespPacket::Factory);
    Serializer->RegisterType(0x2000du, "FLPACKET_SERVER_SETSTARTROOM",
        &FRpcServerSetStartRoomPacket::Factory);
    Serializer->RegisterType(0x20015u, "FLPACKET_SERVER_GFDESTROYCHARACTER",
        &FRpcServerGfDestroyCharacterPacket::Factory);
    Serializer->RegisterType(0x20014u, "FLPACKET_SERVER_GFDESTROYNEWSBROADCAST",
        &FRpcServerGfDestroyNewsBroadcastPacket::Factory);
    Serializer->RegisterType(0x20016u, "FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER",
        &FRpcServerGfDestroyMissionComputerPacket::Factory);
    Serializer->RegisterType(0x20017u, "FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR",
        &FRpcServerGfDestroyScriptBehaviorPacket::Factory);
    Serializer->RegisterType(0x20019u, "FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT",
        &FRpcServerGfDestroyAmbientScriptPacket::Factory);
    Serializer->RegisterType(0x2001au, "FLPACKET_SERVER_GFSCRIPTBEHAVIOR",
        &FRpcServerGfScriptBehaviorPacket::Factory);
    Serializer->RegisterType(0x2001fu, "FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT",
        &FRpcServerGfUpdateAmbientScriptPacket::Factory);
    Serializer->RegisterType(0x2005bu, "FLPACKET_SERVER_MISSIONSAVEA",
        &FRpcServerMissionSaveAPacket::Factory);
    Serializer->RegisterType(0x2001du, "FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER",
        &FRpcServerGfUpdateMissionComputerPacket::Factory);
    Serializer->RegisterType(0x2001eu, "FLPACKET_SERVER_GFUPDATENEWSBROADCAST",
        &FRpcServerGfUpdateNewsBroadcastPacket::Factory);
    Serializer->RegisterType(0x20020u, "FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE",
        &FRpcServerGfMissionVendorAcceptancePacket::Factory);
    Serializer->RegisterType(0x2005au, "FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY",
        &FRpcServerGfMissionVendorWhyEmptyPacket::Factory);
    Serializer->RegisterType(0x20026u, "FLPACKET_SERVER_GFUPDATECHAR",
        &FRpcServerGfUpdateCharPacket::Factory);
    Serializer->RegisterType(0x20028u, "FLPACKET_SERVER_CREATELOOT", &FRpcServerCreateLootPacket::Factory);
    Serializer->RegisterType(0x20029u, "FLPACKET_SERVER_SETREPUTATION",
        &FRpcServerSetReputationPacket::Factory);
    Serializer->RegisterType(0x2002au, "FLPACKET_SERVER_ADJUSTATTITUDE",
        &FRpcServerAdjustAttitudePacket::Factory);
    Serializer->RegisterType(0x2002bu, "FLPACKET_SERVER_SETGROUPFEELINGS",
        &FRpcServerSetGroupFeelingsPacket::Factory);
    Serializer->RegisterType(0x2002cu, "FLPACKET_SERVER_CREATEMINE", &FRpcServerCreateMinePacket::Factory);
    Serializer->RegisterType(0x2002du, "FLPACKET_SERVER_CREATECOUNTER",
        &FRpcServerCreateCounterPacket::Factory);
    Serializer->RegisterType(0x20031u, "FLPACKET_SERVER_EXPLODEASTEROIDMINE",
        &FRpcServerExplodeAsteroidMinePacket::Factory);
    Serializer->RegisterType(0x2004du, "FLPACKET_SERVER_LOADHINT", &FRpcServerLoadHintPacket::Factory);
    Serializer->RegisterType(0x2004eu, "FLPACKET_SERVER_SETDIRECTIVE",
        &FRpcServerSetDirectivePacket::Factory);
    Serializer->RegisterType(0x20037u, "FLPACKET_SERVER_CREATEGUIDED",
        &FRpcServerCreateGuidedPacket::Factory);
    Serializer->RegisterType(0x20038u, "FLPACKET_SERVER_ITEMTRACTORED",
        &FRpcServerItemTractoredPacket::Factory);
    Serializer->RegisterType(0x2002fu, "FLPACKET_SERVER_SETREMOVEITEM",
        &FRpcServerSetRemoveItemPacket::Factory);
    Serializer->RegisterType(0x2002eu, "FLPACKET_SERVER_SETADDITEM", &FRpcServerSetAddItemPacket::Factory);
    Serializer->RegisterType(0x20030u, "FLPACKET_SERVER_SETCASH", &FRpcServerSetCashPacket::Factory);
    Serializer->RegisterType(0x20032u, "FLPACKET_SERVER_REQUESTSPACESCRIPT",
        &FRpcServerRequestSpaceScriptPacket::Factory);
    Serializer->RegisterType(0x20033u, "FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE",
        &FRpcServerSetMissionObjectiveStatePacket::Factory);
    Serializer->RegisterType(0x20047u, "FLPACKET_SERVER_CFGINTERFACENOTIFICATION",
        &FRpcServerCfgInterfaceNotificationPacket::Factory);
    Serializer->RegisterType(0x20046u, "FLPACKET_SERVER_MARKOBJ", &FRpcServerMarkObjPacket::Factory);
    Serializer->RegisterType(0x20035u, "FLPACKET_SERVER_SETMISSIONOBJECTIVES",
        &FRpcServerSetMissionObjectivesPacket::Factory);
    Serializer->RegisterType(0x20034u, "FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE",
        &FRpcServerReplaceMissionObjectivePacket::Factory);
    Serializer->RegisterType(0x20043u, "FLPACKET_SERVER_SET_STORY_CUE",
        &FRpcServerSetStoryCuePacket::Factory);
    Serializer->RegisterType(0x2004bu, "FLPACKET_SERVER_SET_CAMERA", &FRpcServerSetCameraPacket::Factory);
    Serializer->RegisterType(0x2004cu, "FLPACKET_SERVER_REVERT_CAMERA",
        &FRpcServerRevertCameraPacket::Factory);
    Serializer->RegisterType(0x20045u, "FLPACKET_SERVER_SET_MISSION_MESSAGE",
        &FRpcServerSetMissionMessagePacket::Factory);
    Serializer->RegisterType(0x2003cu, "FLPACKET_SERVER_REPAIROBJECT",
        &FRpcServerRepairObjectPacket::Factory);
    Serializer->RegisterType(0x2003eu, "FLPACKET_SERVER_SETNUMKILLS", &FRpcServerSetNumKillsPacket::Factory);
    Serializer->RegisterType(0x2003fu, "FLPACKET_SERVER_SETMISSIONSUCCESSES",
        &FRpcServerSetMissionSuccessesPacket::Factory);
    Serializer->RegisterType(0x20040u, "FLPACKET_SERVER_SETMISSIONFAILURES",
        &FRpcServerSetMissionFailuresPacket::Factory);
    Serializer->RegisterType(0x20041u, "FLPACKET_SERVER_BURNFUSE", &FRpcServerBurnFusePacket::Factory);
    Serializer->RegisterType(0x20042u, "FLPACKET_SERVER_CREATESOLAR", &FRpcServerCreateSolarPacket::Factory);
    Serializer->RegisterType(0x20048u, "FLPACKET_SERVER_SETCOLLISIONGROUPS",
        &FRpcServerSetCollisionGroupsPacket::Factory);
    Serializer->RegisterType(0x20049u, "FLPACKET_SERVER_SETHULLSTATUS", &FRpcSetHullStatusPacket::Factory);
    Serializer->RegisterType(0x2004au, "FLPACKET_SERVER_SETGUIDEDTARGET",
        &FRpcServerSetGuidedTargetPacket::Factory);
    Serializer->RegisterType(0x2004fu, "FLPACKET_SERVER_SENDCOMM", &FRpcServerSendCommPacket::Factory);
    Serializer->RegisterType(0x20051u, "FLPACKET_SERVER_USE_ITEM", &FRpcServerUseItemPacket::Factory);
    Serializer->RegisterType(0x20039u, "FLPACKET_SERVER_SCANNOTIFY", &FRpcServerScanNotifyPacket::Factory);
    Serializer->RegisterType(0x20052u, "FLPACKET_SERVER_PLAYERLIST", &FRpcServerPlayerListPacket::Factory);
    Serializer->RegisterType(0x2003du, "FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE",
        &FRpcServerRemoteObjectCargoUpdatePacket::Factory);
    Serializer->RegisterType(0x20054u, "FLPACKET_SERVER_MISCOBJUPDATE",
        &FRpcServerMiscObjUpdatePacket::Factory);
    Serializer->RegisterType(0x20053u, "FLPACKET_SERVER_FORMATION_UPDATE",
        &FRpcServerFormationUpdatePacket::Factory);
    Serializer->RegisterType(0x20055u, "FLPACKET_SERVER_OBJECTCARGOUPDATE",
        &FRpcServerObjectCargoUpdatePacket::Factory);
    Serializer->RegisterType(0x20056u, "FLPACKET_SERVER_SENDNNMESSAGE",
        &FRpcServerSendNNMessagePacket::Factory);
    Serializer->RegisterType(0x20057u, "FLPACKET_SERVER_SET_MUSIC", &FRpcServerSetMusicPacket::Factory);
    Serializer->RegisterType(0x20058u, "FLPACKET_SERVER_CANCEL_MUSIC", &FRpcServerCancelMusicPacket::Factory);
    Serializer->RegisterType(0x20059u, "FLPACKET_SERVER_PLAY_SOUND_EFFECT",
        &FRpcServerPlaySoundEffectPacket::Factory);
    return 0;
}


#endif

// ===== SERVER 0x01..0x1f (RS-P1) =====
// Bodies compiled into both DLLs: each RC/RS pair below was compared instruction by
// instruction (RS-S1 contracts) and differs only in addresses.

// RC 06b3e780 / RS 06bdb450 (= rpclocal 06d90480). The count is the low word of the
// list's size (RS 06bdb551). Per entry retail loads all three fields first (FLD
// float +0xc, word +0x8, byte +0x10) and writes word, byte, then the float with FSTP;
// the error exit pops the unused float (06bdb633 FSTP ST0). PutFloat loads after the
// check instead: the bytes written and every error path are the same. Every failure
// returns 0x16 (EBP=0x16 from 06bdb4a7, literal 0x16 at the loop exit 06bdb637).
unsigned int FRpcServerDamageObjectPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;                      // 06bdb4c3
    if (!Stream.Put(&Damage.bDestroyed, 1u)) return 0x16u;             // 06bdb4f0 raw byte
    if (!Stream.Put(&Damage.Cause, 4u)) return 0x16u;                  // 06bdb51d
    if (!Stream.Put(&Damage.InflictorId, 4u)) return 0x16u;            // 06bdb54c
    const unsigned short Count = static_cast<unsigned short>(Damage.Entries.size());
    if (!Stream.Put(&Count, 2u)) return 0x16u;                         // 06bdb57d
    for (std::list<DamageEntry>::const_iterator It = Damage.Entries.begin();
         It != Damage.Entries.end(); ++It) {                           // 06bdb588 / 06bdb60e
        if (!Stream.Put(&It->SubObjId, 2u)) return 0x16u;              // 06bdb5b8
        const unsigned char Fate = static_cast<unsigned char>(
            static_cast<unsigned int>(It->Fate) & 0xffu);
        if (!Stream.Put(&Fate, 1u)) return 0x16u;                      // 06bdb5dc
        if (!Stream.PutFloat(It->HitPts)) return 0x16u;                // 06bdb602 FSTP
    }
    return Stream.FinishEncodeSuccess(OutLength);                      // 06bdb61d trailer
}

// RC 06b3e9c0 / RS 06bdb690 (= rpclocal 06d906c0). The decoder APPENDS to the list the
// ctor built and bumps its size per element (06bdb8d7); the count read is only the
// loop bound (unsigned, 06bdb7ae / 06bdb8e2). Each entry is read into a temporary
// (word, byte zero-extended into the dword Fate, float through FLD/FSTP) and only then
// linked in. The byte at +0x30 is stored raw (06bdb728), reproduced on purpose.
unsigned int FRpcServerDamageObjectPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;               // 06bdb704
    if (!Stream.Get(&Damage.bDestroyed, 1u)) return MarshalError;      // 06bdb728
    if (!Stream.Get(&Damage.Cause, 4u)) return MarshalError;           // 06bdb750
    if (!Stream.Get(&Damage.InflictorId, 4u)) return MarshalError;     // 06bdb778
    unsigned short Count = 0;
    if (!Stream.Get(&Count, 2u)) return MarshalError;                  // 06bdb799
    for (unsigned int Index = 0; Index < static_cast<unsigned int>(Count); ++Index) {
        // 06bdb7e0..06bdb801: retail seeds the temporary with SUBOBJ_ID_NONE, -1.0f, 0;
        // every field is overwritten below before the node is linked in.
        DamageEntry Entry;
        if (!Stream.Get(&Entry.SubObjId, 2u)) return MarshalError;     // 06bdb81d
        unsigned char Fate = 0;
        if (!Stream.Get(&Fate, 1u)) return MarshalError;               // 06bdb847
        if (!Stream.Reserve(4u, 0x17u)) return MarshalError;           // 06bdb84c..06bdb866
        RpcServerX87CopyFloat(&Entry.HitPts,
            reinterpret_cast<const float*>(MarshalPtr));               // 06bdb86e / 06bdb877
        Stream.CommitReadPointer(4u);                                  // 06bdb880
        Entry.Fate = static_cast<DamageEntry::SubObjFate>(static_cast<unsigned int>(Fate));
        Damage.Entries.push_back(Entry);                               // 06bdb892..06bdb8c9
        ++Damage.Count;                                                // 06bdb8d7 list size
    }
    return Stream.Finish(OutLength, false);
}

// RC 06b5cf10 / RS 06bf03e0: every failure returns literal 0x16; trailer 06b5cfc5.
unsigned int FRpcServerDestroyObjectPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;                      // 06b5cf85
    if (!Stream.Put(&Field_0x04, 1u)) return 0x16u;                    // 06b5cfb8
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5d030 / RS 06bf0500.
unsigned int FRpcServerDestroyObjectPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x04, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// RC 06b4b2e0 / RS 06be5e00.
FRpcServerLaunchPacket::FRpcServerLaunchPacket() : FPacketBase(0x20007u) {
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
}

// RC 06b5e770 / RS 06bf2fc0. Position and Orientation are each one cursor
// reservation followed by an integer dword copy (06b3a160 / 06b39ec0) into the cached
// destination (06b5e886 / 06b5e8d5), then Ptr=cached+W. Every failure returns 0x16
// (ECX=0x16 from 06b5e7cd, literal at 06b5e8c9).
unsigned int FRpcServerLaunchPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x04, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x08, 4u)) return 0x16u;
    if (!Stream.Put(&Position, 12u)) return 0x16u;
    if (!Stream.Put(&Orientation, 16u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b5e90e
}

// RC 06b5e970 / RS 06bf31c0.
unsigned int FRpcServerLaunchPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x04, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x08, 4u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    if (!Stream.Get(&Orientation, 16u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// RC 06b5eb10 / RS 06bf3360: prints Position only.
void FRpcServerLaunchPacket::Dump(std::ostream& Out) const {
    Out << "   X: " << Position.x << "\n";
    Out << "   Y: " << Position.y << "\n";
    Out << "   Z: " << Position.z << "\n";
}

// RC 06b5fed0 / RS 06bf4aa0. The double crosses the x87 stack (FLD/FSTP qword at
// 06b5ff77/7a) after its 8-byte reservation; signaling NaNs are quieted as in retail.
// The destination is cached before the FSTP and Ptr=cached+8 is committed after it
// (06b5ff75 / 06b5ff87). Every failure returns literal 0x16.
unsigned int FRpcServerCharSelectVerifiedPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&RepInstance, 4u)) return 0x16u;                   // 06b5ff45
    if (!Stream.Reserve(8u, 0x16u)) return 0x16u;                      // 06b5ff4c..06b5ff6c
    const double* Source = &Time;
    const unsigned int Cached = MarshalPtr;                            // 06b5ff75
    double* Destination = reinterpret_cast<double*>(Cached);
    PortableDoubleLoad::Copy(Destination, Source);
    MarshalPtr = Cached + 8u;                                          // 06b5ff87
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b5ff89
}

// RC 06b5fff0 / RS 06bf4bc0: the double is read back as two integer dwords.
unsigned int FRpcServerCharSelectVerifiedPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&RepInstance, 4u)) return MarshalError;
    if (!Stream.Get(&Time, 8u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// RC 06b60100 / RS 06bf4cd0.
void FRpcServerCharSelectVerifiedPacket::Dump(std::ostream& Out) const {
    Out << "   RepInstance: " << RepInstance << " time: " << Time << "\n";
}

// RC 06b5cc60 / RS 06bf0130: every failure returns literal 0x16; trailer 06b5cd4a.
unsigned int FRpcServerActivateObjectPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;                      // 06b5ccd5
    if (!Stream.Put(&bActivate, 1u)) return 0x16u;                     // 06b5cd08
    if (!Stream.Put(&Field_0x08, 4u)) return 0x16u;                    // 06b5cd3b
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5cdb0 / RS 06bf0280.
unsigned int FRpcServerActivateObjectPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&bActivate, 1u)) return MarshalError;
    if (!Stream.Get(&Field_0x08, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// Blob family 1a/1d/1e/1f encode, RC 06b3d170/06b3d9f0/06b3e240/06b3d4d0 and RS
// 06bf68a0/06bf6bc0/06bf7390/06bf76b0: header; dword Size; one Size-byte
// reservation, then a REP MOVSD/MOVSB copy of Data into the cached destination and
// Ptr=cached+Size (RC 06b3d21b..06b3d245). Size 0 still runs every check. Every
// failure returns literal 0x16; trailer RC 06b3d24a.
unsigned int RpcServerBlobEncode(FPacketBase& Packet, const unsigned int& Size,
    const unsigned char* Data, unsigned char** BufferHolder, unsigned int Capacity,
    unsigned int* OutLength) {
    FRpcWireStream Stream(Packet, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Size, 4u)) return 0x16u;
    if (!Stream.PutBlock(Data, Size)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

// Blob family decode. The two DLLs differ:
// - RC 06b3d2b0/06b3db30/06b3e380/06b3d610: read Size; check cursor+Size against the
//   limit WITHOUT committing it (0x17); operator new(Size), null -> return 5; then
//   commit/check the body again and copy it.
// - RS 06bf69e0/06bf6d00/06bf74d0/06bf77f0: read Size; operator new(Size) with no
//   prior check and no null test; then commit/check the body and copy it.
// A block already held is overwritten, not freed (native).
unsigned int RpcServerBlobDecode(FPacketBase& Packet, unsigned int& Size,
    unsigned char*& Data, const unsigned char* Buffer, unsigned int InSize,
    unsigned int* OutLength) {
    FRpcWireStream Stream(Packet, const_cast<unsigned char*>(Buffer), InSize);
    if (!Stream.Get(&Size, 4u)) return Packet.MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    if (Packet.MarshalCursor + Size > Packet.MarshalLimit) Packet.MarshalError = 0x17u;
    if (Packet.MarshalError != 0) return Packet.MarshalError;
    Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
    if (Data == nullptr) return 5u;
#else
    // No null test, reproduced on purpose: a failed allocation faults in the copy.
    Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
#endif
    if (!Stream.Get(Data, Size)) return Packet.MarshalError;
    return Stream.Finish(OutLength, false);
}

// 0x04 CREATESHIP. RS addresses; the RC bodies are instruction-identical except the
// decoder's #if branch.

// RS 06be5340 (the block's members are built by FRpcServerCreateObjectFields).
FRpcServerCreateShipPacket::FRpcServerCreateShipPacket()
    : FPacketBase(0x20004u), bUsingTradelane(0), Throttle(0.0f), OwnerPlayer(0),
      Field_0xc0(0), ReputationRank(0), Field_0xc4(0), NameASize(0), NameA(nullptr),
      NameBSize(0), NameB(nullptr), DisplayNameLength(0), DisplayName(nullptr),
      Affiliation(0), Attitude(0.0f), bMarkedBy(0) {
    Velocity.x = 0.0f;                                   // 06be53e3..06be53eb
    Velocity.y = 0.0f;
    Velocity.z = 0.0f;
    AngularVelocity.x = 0.0f;                            // 06be53f5..06be5401
    AngularVelocity.y = 0.0f;
    AngularVelocity.z = 0.0f;
}

// RS 06bf0c10.
FRpcServerCreateShipPacket::~FRpcServerCreateShipPacket() {
    if (DisplayName != nullptr) {                        // 06bf0c2b
        ::operator delete(DisplayName);
        DisplayName = nullptr;                           // 06bf0c4b
    }
    if (NameB != nullptr) ::operator delete(NameB);      // 06bf0c51
    if (NameA != nullptr) ::operator delete(NameA);      // 06bf0c64
}

// RS 06bf0da0 / RC 06b3f9b0. Every failure returns 0x16 (literal, or EDI=0x16 from
// 06bf0eeb); the three variable blocks are REP copies into the cached destination.
unsigned int FRpcServerCreateShipPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();                                                      // 06bf0dd5
    // 06bf0dfd: the return is discarded; the sticky error stops the next field.
    RpcServerCreateObjectEncode(*this, Stream, Object);
    unsigned char Flags = 0;
    if (bUsingTradelane != 0) Flags = 1;                                     // 06bf0e02
    if (OwnerPlayer != 0) Flags |= 4;                                        // 06bf0e16 dword
    if (bMarkedBy != 0) Flags |= 2;                                          // 06bf0e25
    if (!Stream.Put(&Flags, 1u)) return 0x16u;                               // 06bf0e67
    // 06bf0e6c..06bf0ed6: one 12-byte check, then an integer copy (06bda4c0).
    if (!Stream.Put(&Velocity, 12u)) return 0x16u;
    const unsigned char ThrottleByte = RpcServerQuantize127(&Throttle);     // 06bf0ec7
    if (!Stream.Put(&ThrottleByte, 1u)) return 0x16u;                       // 06bf0f0a
    if (!Stream.Put(&Field_0xc0, 2u)) return 0x16u;                         // 06bf0f3e
    if (!Stream.Put(&ReputationRank, 1u)) return 0x16u;                     // 06bf0f72
    if ((Flags & 4) != 0) {
        const unsigned char Owner = static_cast<unsigned char>(OwnerPlayer & 0xffu);
        if (!Stream.Put(&Owner, 1u)) return 0x16u;                          // 06bf0faf
        if (!Stream.Put(&Field_0xc4, 2u)) return 0x16u;                     // 06bf0fe3
        if (!SinglePlayer()) {                                              // 06bf0feb
            // 06bf0fff: the low byte of the length, then 2 * length bytes.
            const unsigned char Length =
                static_cast<unsigned char>(static_cast<unsigned int>(DisplayNameLength) & 0xffu);
            if (!Stream.Put(&Length, 1u)) return 0x16u;                     // 06bf102e
            if (!Stream.PutBlock(DisplayName,
                    static_cast<unsigned int>(DisplayNameLength) << 1))
                return 0x16u;                                               // 06bf1069 -> 06bf11b7
        }
    } else if (!SinglePlayer()) {                                           // 06bf107e
        if (!Stream.Put(&NameASize, 4u)) return 0x16u;                      // 06bf10c3
        if (!Stream.PutBlock(NameA, NameASize)) return 0x16u;               // 06bf1117
        if (!Stream.Put(&NameBSize, 4u)) return 0x16u;                      // 06bf1161
        if (!Stream.PutBlock(NameB, NameBSize)) return 0x16u;               // 06bf11b7
    }
    if (!SinglePlayer()) {                                                  // 06bf11c9
        if (!Stream.Put(&Affiliation, 4u)) return 0x16u;                    // 06bf120f
        const unsigned char AttitudeByte = RpcServerQuantize127(&Attitude); // 06bf1211
        if (!Stream.Put(&AttitudeByte, 1u)) return 0x16u;                   // 06bf1258
    }
    return Stream.FinishEncodeSuccess(OutLength);                            // 06bf1269
}

// RS 06bf12d0 / RC 06b3fee0. Returns the stream error, or (RC) 5 when the display name
// allocation fails.
unsigned int FRpcServerCreateShipPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // 06bf1322: the return is discarded; the sticky error stops the next field.
    RpcServerCreateObjectDecode(*this, Stream, Object);
    unsigned char Flags = 0;
    if (!Stream.Get(&Flags, 1u)) return MarshalError;                        // 06bf134b
    // 06bf1350..06bf1365: the bits only ever SET the bytes.
    if ((Flags & 1) != 0) bUsingTradelane = 1;
    if ((Flags & 2) != 0) bMarkedBy = 1;
    // 06bf136b..06bf13a0: one 12-byte check, then an integer copy (06bda4c0).
    if (!Stream.Get(&Velocity, 12u)) return MarshalError;
    unsigned char ThrottleByte = 0;
    if (!Stream.Get(&ThrottleByte, 1u)) return MarshalError;                 // 06bf13c7
    RpcServerExpand127(ThrottleByte, &Throttle);                             // 06bf13dd
    if (!Stream.Get(&Field_0xc0, 2u)) return MarshalError;                   // 06bf1405
    if (!Stream.Get(&ReputationRank, 1u)) return MarshalError;               // 06bf1434
    if ((Flags & 4) != 0) {
        unsigned char Owner = 0;
        if (!Stream.Get(&Owner, 1u)) return MarshalError;                    // 06bf146a
        OwnerPlayer = static_cast<unsigned int>(Owner);                      // 06bf1473
        if (!Stream.Get(&Field_0xc4, 2u)) return MarshalError;               // 06bf149b
        if (!SinglePlayer()) {                                               // 06bf14aa
            unsigned char Length = 0;
            if (!Stream.Get(&Length, 1u)) return MarshalError;               // 06bf14dc
            // 06bf14e1 MOVSX: a length byte of 0x80 or more is negative.
            DisplayNameLength = static_cast<int>(static_cast<signed char>(Length));
            const unsigned int Bytes = static_cast<unsigned int>(DisplayNameLength) << 1;
#if defined(REMOTECLIENT_EXPORTS)
            // RC 06b400fb..06b40129: the run is checked first (not committed); a failed
            // allocation returns 5.
            if (MarshalCursor + Bytes > MarshalLimit) MarshalError = 0x17u;
            if (MarshalError != 0) return MarshalError;
            DisplayName = static_cast<wchar_t*>(::operator new(Bytes + 2u, std::nothrow));
            if (DisplayName == nullptr) return 5u;
#else
            // 06bf14ef: allocated and stored before the check, no null test.
            DisplayName = static_cast<wchar_t*>(::operator new(Bytes + 2u, std::nothrow));
#endif
            if (!Stream.Get(DisplayName, Bytes)) return MarshalError;        // 06bf1543
            DisplayName[DisplayNameLength] = L'\0';                          // 06bf1561
        }
    } else if (!SinglePlayer()) {                                            // 06bf1607
        // 06bf161e / 06bf162c (RS 06bec240, RC 06b4eb30): both returns discarded.
        RpcWireDecodeOwnedBlock(*this, Stream, NameASize, NameA);
        RpcWireDecodeOwnedBlock(*this, Stream, NameBSize, NameB);
    }
    if (!SinglePlayer()) {                                                   // 06bf156c
        if (!Stream.Get(&Affiliation, 4u)) return MarshalError;              // 06bf15a1
        unsigned char AttitudeByte = 0;
        if (!Stream.Get(&AttitudeByte, 1u)) return MarshalError;             // 06bf15c8
        RpcServerExpand127(AttitudeByte, &Attitude);                         // 06bf15df
    }
    return Stream.Finish(OutLength, false);                                  // 06bf15f1..
}

// ===== end SERVER 0x01..0x1f (RS-P1) =====

// ===== SERVER 0x20..0x3f (RS-P2) =====
// Bodies compiled into both DLLs. Each RC/RS pair named below has the same
// instructions (RS-S1 / RS-P2 dossiers) and differs only in addresses.

// --- equipment list (0x24 SETEQUIPMENT, 0x25 SETCARGO) ---

// RC 06b3a450 / RS 06bdc790: every record's hardpoint, then the record, then the
// list (nodes, dummy node, header). The handle pointer itself is left as it is.
FRpcServerEquipHandle::~FRpcServerEquipHandle() {
    if (Records == nullptr) return;
    for (std::list<FRpcServerEquipRecord*>::iterator It = Records->begin();
        It != Records->end(); ++It) {
        FRpcServerEquipRecord* Record = *It;
        if (Record != nullptr) {
            if (Record->Hardpoint != nullptr) ::operator delete(Record->Hardpoint);
            ::operator delete(Record);
        }
    }
    delete Records;
}

// RC 06b3a820: the list is created on the first push_back; when that allocation
// fails (06b3a88a) the record is dropped.
void FRpcServerEquipHandle::PushBack(FRpcServerEquipRecord* Record) {
    if (Records == nullptr) {
        Records = new (std::nothrow) std::list<FRpcServerEquipRecord*>();
        if (Records == nullptr) return;
    }
    Records->push_back(Record);
}

// RC 06b3a510 / RS 06bdc850. A null source list leaves this handle untouched; a
// record whose allocation fails is skipped (06b3a5b9). Fields are integer copies
// (Health included, 06b3a5e6); the hardpoint is re-allocated with the record's
// size and copied up to its terminator (06b3a623 loop), not by size.
void FRpcServerEquipHandle::AppendCopy(const FRpcServerEquipHandle& Source) {
    if (Source.Records == nullptr) return;
    if (Records == nullptr) {
        Records = new (std::nothrow) std::list<FRpcServerEquipRecord*>();
        if (Records == nullptr) return;
    }
    for (std::list<FRpcServerEquipRecord*>::const_iterator It = Source.Records->begin();
        It != Source.Records->end(); ++It) {
        const FRpcServerEquipRecord* From = *It;
        FRpcServerEquipRecord* Record = static_cast<FRpcServerEquipRecord*>(
            ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
        if (Record == nullptr) continue;
        Record->Hardpoint = nullptr;
        Record->HardpointSize = From->HardpointSize;
        Record->ArchId = From->ArchId;
        std::memcpy(&Record->Health, &From->Health, 4u);
        Record->Count = From->Count;
        Record->Id = From->Id;
        Record->bEquipped = From->bEquipped;
        Record->Field_0x0f = From->Field_0x0f;
        if (Record->HardpointSize > 0) {
            Record->Hardpoint = static_cast<char*>(
                ::operator new(Record->HardpointSize, std::nothrow));
            const char* Text = From->Hardpoint;
            char* Out = Record->Hardpoint;
            char Value;
            do {
                Value = *Text++;
                *Out++ = Value;
            } while (Value != '\0');
        }
        Records->push_back(Record);
    }
}

// RC 06b38210. End is read once (06b38218); bAll is a byte test (06b38230); a
// record allocation is not checked (06b3824b..06b3826a), so a failure writes
// through null, reproduced on purpose. Health moves through the x87 stack
// (06b38277/06b3827a). The hardpoint size is strlen + 1 truncated to 16 bits for
// both the stored size and the allocation, while the copy runs to the terminator.
void RpcServerEquipHandleFromVector(const EquipDescVector* Source,
    FRpcServerEquipHandle* Handle, bool bAll) {
    const EquipDesc* End = Source->End;
    for (const EquipDesc* Desc = Source->Begin; Desc != End; ++Desc) {
        if (!bAll && Desc->bEquipped != 0) continue;
        FRpcServerEquipRecord* Record = static_cast<FRpcServerEquipRecord*>(
            ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
        if (Record != nullptr) {
            Record->ArchId = 0;
            Record->Count = 0;
            std::memset(&Record->Health, 0, 4u);
            Record->Id = 0;
            Record->bEquipped = 0;
            Record->Field_0x0f = 0;
            Record->HardpointSize = 0;
            Record->Hardpoint = nullptr;
        }
        Record->ArchId = Desc->ArchId;
        Record->Count = Desc->Count;
        RpcServerX87CopyFloat(&Record->Health, &Desc->Health);
        Record->Id = Desc->Id;
        Record->bEquipped = Desc->bEquipped;
        if (!Desc->is_internal()) {
            const unsigned int Length = static_cast<unsigned int>(std::strlen(Desc->Hardpoint)) + 1u;
            Record->HardpointSize = static_cast<unsigned short>(Length);
            Record->Hardpoint = static_cast<char*>(
                ::operator new(static_cast<unsigned short>(Length), std::nothrow));
            const char* Text = Desc->Hardpoint;
            char* Out = Record->Hardpoint;
            char Value;
            do {
                Value = *Text++;
                *Out++ = Value;
            } while (Value != '\0');
        }
        Handle->PushBack(Record);
    }
}

// RS 06bd5130. Reserve(list size) first: a capacity below the list size moves the
// existing elements into an exact new block (Common's vector<EquipDesc>::_Ucopy,
// _Destroy, operator delete). Then one element per non-null record, appended as a
// default EquipDesc and filled through Common's setters in retail order. Every
// caller passes a fresh vector, so the reserved block always has room for the
// append and the native insert never grows it here.
void RpcServerEquipVectorFromHandle(const FRpcServerEquipHandle* Handle,
    EquipDescVector* Dest) {
    const std::list<FRpcServerEquipRecord*>* List = Handle->Records;
    if (List == nullptr) return;
    const unsigned int Wanted = static_cast<unsigned int>(List->size());
    const unsigned int Capacity = (Dest->Begin == nullptr)
        ? 0u : static_cast<unsigned int>(Dest->Cap - Dest->Begin);
    if (Capacity < Wanted) {
        EquipDesc* Block = static_cast<EquipDesc*>(::operator new(sizeof(EquipDesc) * Wanted));
        EquipDesc* Out = Block;
        for (EquipDesc* In = Dest->Begin; In != Dest->End; ++In, ++Out) {
            new (Out) EquipDesc(*In);
        }
        const unsigned int OldSize = static_cast<unsigned int>(Dest->End - Dest->Begin);
        ::operator delete(Dest->Begin);
        Dest->Cap = Block + Wanted;
        Dest->End = Block + OldSize;
        Dest->Begin = Block;
    }
    for (std::list<FRpcServerEquipRecord*>::const_iterator It = List->begin();
        It != List->end(); ++It) {
        const FRpcServerEquipRecord* Record = *It;
        if (Record == nullptr) continue;
        EquipDesc* Slot = new (Dest->End) EquipDesc();
        ++Dest->End;
        Slot->set_arch_id(Record->ArchId);
        Slot->set_count(Record->Count);
        Slot->set_status(Record->Health);
        Slot->set_id(Record->Id);
        Slot->set_equipped(Record->bEquipped);
        if (Record->HardpointSize > 0) {
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
            Slot->set_hardpoint(Hardpoint);
        } else {
            Slot->make_internal();
        }
    }
}

// RC 06b4f860 (shared by 0x24/0x25 through vtable 06b6e0f4): the handle's contents,
// then the 4-byte handle itself.
FRpcServerEquipListPacketBase::~FRpcServerEquipListPacketBase() {
    delete Equipment;
}

// RC 06b3c510 / 06b3c770 (identical bodies). A failed handle allocation leaves the
// packet without one.
void FRpcServerEquipListPacketBase::AssignEquipment(const FRpcServerEquipHandle& Source) {
    if (Equipment == nullptr) {
        Equipment = new (std::nothrow) FRpcServerEquipHandle();
    }
    if (Equipment != nullptr) {
        Equipment->AppendCopy(Source);
    }
}

// RS 06bdc770 / 06bdce60 (identical bodies).
void FRpcServerEquipListPacketBase::ExtractEquipment(FRpcServerEquipHandle* Dest) const {
    if (Equipment != nullptr) {
        Dest->AppendCopy(*Equipment);
    }
}

// RC 06b3c370 / 06b3c5d0, RS 06bdc590 / 06bdcc80.
unsigned int FRpcServerEquipListPacketBase::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // Native Encode entry/cached header; a nonzero helper result (literal 0x16) is
    // returned unchanged without touching Error, success takes the trailer tail.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    const unsigned int Result = EncodeEquipment(Stream);
    if (Result != 0) return Result;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b3c440 / 06b3c6a0, RS 06bdc660 / 06bdcd50.
unsigned int FRpcServerEquipListPacketBase::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    const unsigned int Result = DecodeEquipment(Stream);
    if (Result != 0) return Result;
    return Stream.Finish(OutLength, false);
}

// RC 06b4f8d0 / RS 06becfb0. Wire: u16 count (list size truncated), then per record
// in list order: Count, Health (FLD/FSTP), ArchId, u16 Id, byte bEquipped, byte
// Field_0x0f, u16 HardpointSize, HardpointSize raw bytes. The walk covers the whole
// list, not the truncated count. Every failed reservation exits through EDI=0x16
// (06b4f905/06b4f962 -> 06b4f916) without rewriting the sticky Error.
unsigned int FRpcServerEquipListPacketBase::EncodeEquipment(FRpcWireStream& Stream) {
    const std::list<FRpcServerEquipRecord*>* CountList =
        (Equipment != nullptr) ? Equipment->Records : nullptr;
    const unsigned short Count =
        (CountList != nullptr) ? static_cast<unsigned short>(CountList->size()) : static_cast<unsigned short>(0);
    if (!Stream.Put(&Count, 2u)) return 0x16u;
    if (Count == 0) return 0;
    // RC06b4f937..06b4f956: the handle's list is fetched again after the count
    // write; a null holder ends the encode successfully.
    const std::list<FRpcServerEquipRecord*>* List =
        (Equipment != nullptr) ? Equipment->Records : nullptr;
    if (List == nullptr) return 0;
    for (std::list<FRpcServerEquipRecord*>::const_iterator It = List->begin();
        It != List->end(); ++It) {
        const FRpcServerEquipRecord* Record = *It;
        if (!Stream.Put(&Record->Count, 4u)) return 0x16u;
        if (!Stream.PutFloat(Record->Health)) return 0x16u;
        if (!Stream.Put(&Record->ArchId, 4u)) return 0x16u;
        if (!Stream.Put(&Record->Id, 2u)) return 0x16u;
        if (!Stream.Put(&Record->bEquipped, 1u)) return 0x16u;
        if (!Stream.Put(&Record->Field_0x0f, 1u)) return 0x16u;
        if (!Stream.Put(&Record->HardpointSize, 2u)) return 0x16u;
        // RC06b4facb..06b4faf1: cached REP MOVSD/MOVSB body, Ptr=cached+size.
        if (!Stream.PutBlock(Record->Hardpoint, Record->HardpointSize)) return 0x16u;
    }
    return 0;
}

// RC 06b4fb10 / RS 06bed1f0. The decoded records are appended to whatever handle
// the packet already has. A record whose allocation fails is skipped; a field
// failure returns without freeing the half-read record (06b4fd91, reproduced on
// purpose). The hardpoint body differs between the DLLs: RC bounds-checks it before
// the cursor moves and returns 5 on a failed allocation; RS (06bed3c7..06bed3f4)
// allocates first, with no prior check and no null test, then reserves and checks.
unsigned int FRpcServerEquipListPacketBase::DecodeEquipment(FRpcWireStream& Stream) {
    unsigned short Count;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    if (Count == 0) return 0;
    if (Equipment == nullptr) {
        Equipment = new (std::nothrow) FRpcServerEquipHandle();
    }
    for (unsigned short Index = 0; Index < Count; ++Index) {
        FRpcServerEquipRecord* Record = static_cast<FRpcServerEquipRecord*>(
            ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
        if (Record == nullptr) continue;
        Record->ArchId = 0;
        Record->Count = 0;
        std::memset(&Record->Health, 0, 4u);
        Record->Id = 0;
        Record->bEquipped = 0;
        Record->Field_0x0f = 0;
        Record->HardpointSize = 0;
        Record->Hardpoint = nullptr;
        if (!Stream.Get(&Record->Count, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Health, 4u)) return MarshalError;
        if (!Stream.Get(&Record->ArchId, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Id, 2u)) return MarshalError;
        if (!Stream.Get(&Record->bEquipped, 1u)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x0f, 1u)) return MarshalError;
        if (!Stream.Get(&Record->HardpointSize, 2u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + Record->HardpointSize > MarshalLimit) MarshalError = 0x17u;
        if (MarshalError != 0) return MarshalError;
        Record->Hardpoint = static_cast<char*>(::operator new(Record->HardpointSize, std::nothrow));
        if (Record->Hardpoint == nullptr) return 5u;
#else
        Record->Hardpoint = static_cast<char*>(::operator new(Record->HardpointSize, std::nothrow));
#endif
        if (!Stream.Get(Record->Hardpoint, Record->HardpointSize)) return MarshalError;
        Equipment->PushBack(Record);
    }
    return 0;
}

// --- 0x27 REQUESTCREATESHIPRESP: RC 06b5ba90 / 06b5bbb0, RS 06beef60 / 06bef080 ---
// Encode: every failure returns literal 0x16; trailer RC 06b5bb45.
unsigned int FRpcServerRequestCreateShipRespPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 1u)) return 0x16u;                    // 06b5bb03
    if (!Stream.Put(&Field_0x04, 4u)) return 0x16u;                    // 06b5bb36
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcServerRequestCreateShipRespPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 1u)) return MarshalError;
    if (!Stream.Get(&Field_0x04, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x2a ADJUSTATTITUDE: RC 06b5e230 / 06b5e350, RS 06bf21b0 / 06bf22d0 ---
// The float goes through FLD/FSTP on encode (06b5e2d7) and an integer MOV on decode.
// Encode: every failure returns literal 0x16; trailer RC 06b5e2e9.
unsigned int FRpcServerAdjustAttitudePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;                    // 06b5e2a5
    if (!Stream.PutFloat(Field_0x04)) return 0x16u;                    // 06b5e2da FSTP
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcServerAdjustAttitudePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x04, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x2d CREATECOUNTER ---
static_assert(sizeof(FRpcServerCreateCounterPacket) == 0x68, "CREATECOUNTER is operator new(0x68)");

// RC 06b4aab0 / RS 06be55d0.
FRpcServerCreateCounterPacket::FRpcServerCreateCounterPacket() : FPacketBase(0x2002du) {
    ArchetypeNickname = 0;
    ObjId = 0;
    Field_0x08 = 0;
    OwnerId = 0;
    SubObjId = SUBOBJ_ID_NONE;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Velocity.x = 0.0f;
    Velocity.y = 0.0f;
    Velocity.z = 0.0f;
    Field_0x3c.x = 0.0f;
    Field_0x3c.y = 0.0f;
    Field_0x3c.z = 0.0f;
}

// RC 06b5dcc0 / RS 06bf1c40. Every failure returns 0x16 (EAX=0x16 from 06b5dd1d into
// the shared epilogue 06b5df98, then literals); the quaternion and the three vectors
// are integer copies into the cached destination (06b39ec0 / 06b3a160).
unsigned int FRpcServerCreateCounterPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ArchetypeNickname, 4u)) return 0x16u;
    if (!Stream.Put(&ObjId, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x08, 4u)) return 0x16u;
    if (!Stream.Put(&OwnerId, 4u)) return 0x16u;
    if (!Stream.Put(&SubObjId, 2u)) return 0x16u;
    if (!Stream.Put(&Orientation, 16u)) return 0x16u;
    if (!Stream.Put(&Position, 12u)) return 0x16u;
    if (!Stream.Put(&Velocity, 12u)) return 0x16u;
    if (!Stream.Put(&Field_0x3c, 12u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b5df3e
}

// RC 06b5dfb0 / RS 06bf1f30.
unsigned int FRpcServerCreateCounterPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ArchetypeNickname, 4u)) return MarshalError;
    if (!Stream.Get(&ObjId, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x08, 4u)) return MarshalError;
    if (!Stream.Get(&OwnerId, 4u)) return MarshalError;
    if (!Stream.Get(&SubObjId, 2u)) return MarshalError;
    if (!Stream.Get(&Orientation, 16u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    if (!Stream.Get(&Velocity, 12u)) return MarshalError;
    if (!Stream.Get(&Field_0x3c, 12u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x37 CREATEGUIDED ---
static_assert(sizeof(FRpcServerCreateGuidedPacket) == 0x58, "CREATEGUIDED is operator new(0x58)");

// RC 06b4a460 / RS 06be4f60: only Position and Orientation are written.
FRpcServerCreateGuidedPacket::FRpcServerCreateGuidedPacket() : FPacketBase(0x20037u) {
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
}

// RC 06b5c310 / RS 06bef7e0. The small id is computed before its reservation
// (06b5c363); all four attitude bytes are quantized, x, y, z, w, after the Position
// pointer commit and before the first of their four one-byte reservations. Every
// failure returns 0x16 (EAX=0x16 from 06b5c37c into the epilogue 06b5c627, then
// EBX=0x16 from 06b5c515).
unsigned int FRpcServerCreateGuidedPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    const unsigned short SmallId = Archetype::LargeIDToSmallID(ArchetypeNickname);
    if (!Stream.Put(&SmallId, 2u)) return 0x16u;
    if (!Stream.Put(&OwnerId, 4u)) return 0x16u;
    if (!Stream.Put(&SubObjId, 2u)) return 0x16u;
    if (!Stream.Put(&TargetId, 4u)) return 0x16u;
    if (!Stream.Put(&TargetSubObjId, 2u)) return 0x16u;
    if (!Stream.Put(&ObjId, 4u)) return 0x16u;
    if (!Stream.Put(&Position, 12u)) return 0x16u;
    const unsigned char X = RpcServerQuantize127(&Orientation.x);
    const unsigned char Y = RpcServerQuantize127(&Orientation.y);
    const unsigned char Z = RpcServerQuantize127(&Orientation.z);
    const unsigned char W = RpcServerQuantize127(&Orientation.w);
    if (!Stream.Put(&X, 1u)) return 0x16u;
    if (!Stream.Put(&Y, 1u)) return 0x16u;
    if (!Stream.Put(&Z, 1u)) return 0x16u;
    if (!Stream.Put(&W, 1u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b5c5d0
}

// RC 06b5c640 / RS 06befb10. Field_0x00 is zeroed before the first read (06b5c680).
// The four bytes are all read before any is expanded into x, y, z, w.
unsigned int FRpcServerCreateGuidedPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    Field_0x00 = 0;
    unsigned short SmallId;
    if (!Stream.Get(&SmallId, 2u)) return MarshalError;
    ArchetypeNickname = Archetype::SmallIDToLargeID(SmallId);
    if (!Stream.Get(&OwnerId, 4u)) return MarshalError;
    if (!Stream.Get(&SubObjId, 2u)) return MarshalError;
    if (!Stream.Get(&TargetId, 4u)) return MarshalError;
    if (!Stream.Get(&TargetSubObjId, 2u)) return MarshalError;
    if (!Stream.Get(&ObjId, 4u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    unsigned char X;
    unsigned char Y;
    unsigned char Z;
    unsigned char W;
    if (!Stream.Get(&X, 1u)) return MarshalError;
    if (!Stream.Get(&Y, 1u)) return MarshalError;
    if (!Stream.Get(&Z, 1u)) return MarshalError;
    if (!Stream.Get(&W, 1u)) return MarshalError;
    RpcServerExpand127(X, &Orientation.x);
    RpcServerExpand127(Y, &Orientation.y);
    RpcServerExpand127(Z, &Orientation.z);
    RpcServerExpand127(W, &Orientation.w);
    return Stream.Finish(OutLength, false);
}

// --- 0x22 SYSTEM_SWITCH_IN ---
static_assert(sizeof(FRpcServerSystemSwitchInPacket) == 0x44, "SYSTEM_SWITCH_IN is operator new(0x44)");

// RC 06b4a790 / RS 06be5290.
FRpcServerSystemSwitchInPacket::FRpcServerSystemSwitchInPacket() : FPacketBase(0x20022u) {
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
}

// RC 06b5d3d0 / RS 06bf08a0. Every failure returns 0x16 (ECX=0x16 from 06b5d42d,
// literal at 06b5d4f7); vector/quaternion are integer copies into the cached
// destination (06b3a160 / 06b39ec0).
unsigned int FRpcServerSystemSwitchInPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x04, 4u)) return 0x16u;
    if (!Stream.Put(&Position, 12u)) return 0x16u;
    if (!Stream.Put(&Orientation, 16u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b5d53c
}

// RC 06b5d5a0 / RS 06bf0a70.
unsigned int FRpcServerSystemSwitchInPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x04, 4u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    if (!Stream.Get(&Orientation, 16u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x31 EXPLODEASTEROIDMINE ---
static_assert(sizeof(FRpcServerExplodeAsteroidMinePacket) == 0x30,
    "EXPLODEASTEROIDMINE is operator new(0x30)");

// RC 06b4ab90 / RS 06be56b0: +0x20..+0x2c = 0 as dwords.
FRpcServerExplodeAsteroidMinePacket::FRpcServerExplodeAsteroidMinePacket()
    : FPacketBase(0x20031u) {
    Field_0x00 = 0;
    std::memset(&Position, 0, sizeof(Position));
}

// RC 06b42c90 / RS 06bdb9a0: every failure returns literal 0x16; Position is an
// integer copy into the cached destination (06b3a160).
unsigned int FRpcServerExplodeAsteroidMinePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.Put(&Position, 12u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b42d75
}

// RC 06b42de0 / RS 06bdbaf0.
unsigned int FRpcServerExplodeAsteroidMinePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x38 ITEMTRACTORED ---
static_assert(sizeof(FRpcServerItemTractoredPacket) == 0x2c, "ITEMTRACTORED is operator new(0x2c)");

// RC 06b4a510 / RS 06be5010.
FRpcServerItemTractoredPacket::FRpcServerItemTractoredPacket() : FPacketBase(0x20038u) {
    Field_0x00 = 0;
    Field_0x04 = 0;
    Field_0x08 = 0;
    Field_0x0a = 0;
}

// RC 06b5c980 / RS 06befe50. Every failure returns 0x16 (EBP=0x16 from 06b5c9d7).
unsigned int FRpcServerItemTractoredPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x04, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x08, 2u)) return 0x16u;
    if (!Stream.Put(&Field_0x0a, 2u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b5ca96
}

// RC 06b5cb00 / RS 06beffd0.
unsigned int FRpcServerItemTractoredPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x04, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x08, 2u)) return MarshalError;
    if (!Stream.Get(&Field_0x0a, 2u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x2e SETADDITEM ---
static_assert(sizeof(FRpcServerSetAddItemPacket) == 0x40, "SETADDITEM is operator new(0x40)");

// RC 06b49e60 / RS 06be4920.
FRpcServerSetAddItemPacket::FRpcServerSetAddItemPacket() : FPacketBase(0x2002eu) {
    HardpointSize = 0;
    bMission = 0;
    Hardpoint = nullptr;
}

// RC 06b3c800 / RS 06bee710.
FRpcServerSetAddItemPacket::~FRpcServerSetAddItemPacket() {
    if (Hardpoint != nullptr) ::operator delete(Hardpoint);
}

// RC 06b3cd50.
void FRpcServerSetAddItemPacket::SetHardpoint(const char* Source, unsigned int Length) {
    if (Length == 0 || Source == nullptr) return;
    const unsigned int Size = Length + 1u;
    Hardpoint = static_cast<char*>(::operator new(Size, std::nothrow));
    std::memcpy(Hardpoint, Source, Size);
    HardpointSize = Size;
}

// RC 06b3c850 / RS 06bee740. Health goes through FLD/FSTP (06b3c950). Every failure
// returns 0x16 (EDI=0x16 from 06b3c8a7, literal at the header); the hardpoint run is
// a REP MOVSD/MOVSB into the cached destination (06b3ca38..06b3ca5a).
unsigned int FRpcServerSetAddItemPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ArchId, 4u)) return 0x16u;
    if (!Stream.Put(&Id, 4u)) return 0x16u;
    if (!Stream.Put(&Count, 4u)) return 0x16u;
    if (!Stream.PutFloat(Health)) return 0x16u;
    if (!Stream.Put(&bEquipped, 1u)) return 0x16u;
    if (!Stream.Put(&bMission, 1u)) return 0x16u;
    if (!Stream.Put(&Field_0x0c, 4u)) return 0x16u;
    if (!Stream.Put(&HardpointSize, 4u)) return 0x16u;
    if (HardpointSize != 0) {                                          // 06b3ca12
        if (!Stream.PutBlock(Hardpoint, HardpointSize)) return 0x16u;
    }
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b3ca6d
}

// RC 06b3cad0 / RS 06bee9c0. Health is an integer MOV here. The hardpoint block:
// - RC: checks cursor + size against the limit WITHOUT committing it (0x17),
//   operator new(size), null -> return 5, then commits, checks and copies;
// - RS: operator new(size) with no prior check and no null test, then commits, checks
//   and copies.
unsigned int FRpcServerSetAddItemPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ArchId, 4u)) return MarshalError;
    if (!Stream.Get(&Id, 4u)) return MarshalError;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    if (!Stream.Get(&Health, 4u)) return MarshalError;
    if (!Stream.Get(&bEquipped, 1u)) return MarshalError;
    if (!Stream.Get(&bMission, 1u)) return MarshalError;
    if (!Stream.Get(&Field_0x0c, 4u)) return MarshalError;
    if (!Stream.Get(&HardpointSize, 4u)) return MarshalError;
    if (HardpointSize != 0) {
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + HardpointSize > MarshalLimit) MarshalError = 0x17u;
        if (MarshalError != 0) return MarshalError;
        Hardpoint = static_cast<char*>(::operator new(HardpointSize, std::nothrow));
        if (Hardpoint == nullptr) return 5u;
#else
        // No null test, reproduced on purpose: a failed allocation faults in the copy.
        Hardpoint = static_cast<char*>(::operator new(HardpointSize, std::nothrow));
#endif
        if (!Stream.Get(Hardpoint, HardpointSize)) return MarshalError;
    }
    return Stream.Finish(OutLength, false);
}

// --- 0x32 REQUESTSPACESCRIPT ---
static_assert(sizeof(FRpcServerRequestSpaceScriptPacket) == 0x3c,
    "REQUESTSPACESCRIPT is operator new(0x3c)");

// RC 06b4b990 / RS 06be64b0.
FRpcServerRequestSpaceScriptPacket::FRpcServerRequestSpaceScriptPacket()
    : FPacketBase(0x20032u) {
    Field_0x00 = 0;
    Position[0] = 0;
    Position[1] = 0;
    Position[2] = 0;
    Mode = 1;
    Name = nullptr;
}

// RC 06b43ed0.
FRpcServerRequestSpaceScriptPacket::~FRpcServerRequestSpaceScriptPacket() {
    if (Name != nullptr) ::operator delete(Name);
}

// RC 06b44240.
void FRpcServerRequestSpaceScriptPacket::SetName(const char* Source, unsigned int Length) {
    if (Length == 0 || Source == nullptr) return;
    if (Name != nullptr) ::operator delete(Name);
    const unsigned int Size = Length + 1u;
    Name = static_cast<char*>(::operator new(Size, std::nothrow));
    std::memcpy(Name, Source, Size);
    NameSize = Size;
}

// RC 06b43f20 / RS 06bf4780. Every failure returns literal 0x16; the name is a REP
// MOVSD/MOVSB into the cached destination (06b44000..06b44023).
unsigned int FRpcServerRequestSpaceScriptPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.Put(&NameSize, 4u)) return 0x16u;
    if (NameSize != 0) {                                               // 06b43fd4
        if (!Stream.PutBlock(Name, NameSize)) return 0x16u;
    }
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b44032
}

// RC 06b44090: uncommitted check of the name block (0x17), free the old Name, operator
// new(NameSize), null -> return 5, then commit, check and copy.
// RS 06bf48f0: commit, check and copy into Name with no allocation at all.
unsigned int FRpcServerRequestSpaceScriptPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!Stream.Get(&NameSize, 4u)) return MarshalError;
    if (NameSize != 0) {
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + NameSize > MarshalLimit) MarshalError = 0x17u;
        if (MarshalError != 0) return MarshalError;
        if (Name != nullptr) ::operator delete(Name);
        Name = static_cast<char*>(::operator new(NameSize, std::nothrow));
        if (Name == nullptr) return 5u;
#endif
        // RS: Name is still null here; the copy writes through it, reproduced on purpose.
        if (!Stream.Get(Name, NameSize)) return MarshalError;
    }
    return Stream.Finish(OutLength, false);
}

// --- 0x33 SETMISSIONOBJECTIVESTATE ---
static_assert(sizeof(FRpcServerSetMissionObjectiveStatePacket) == 0x2c,
    "SETMISSIONOBJECTIVESTATE is operator new(0x2c)");

// RC 06b5ebc0 / RS 06bf3410: every failure returns literal 0x16; trailer 06b5ecb0.
unsigned int FRpcServerSetMissionObjectiveStatePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x04, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x08, 2u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5ed10 / RS 06bf3560.
unsigned int FRpcServerSetMissionObjectiveStatePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x04, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x08, 2u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x34 REPLACEMISSIONOBJECTIVE ---
static_assert(sizeof(FRpcServerReplaceMissionObjectivePacket) == 0x30,
    "REPLACEMISSIONOBJECTIVE is operator new(0x30)");

// RC 06b4b5c0 / RS 06be60e0.
FRpcServerReplaceMissionObjectivePacket::~FRpcServerReplaceMissionObjectivePacket() {
    if (TextData != nullptr) ::operator delete(TextData);
}

// RC 06b5f320 / RS 06bf3b70: the block goes out even when TextSize is 0, as a REP
// MOVSD/MOVSB into the cached destination (06b5f40c..06b5f436). Every failure returns
// 0x16 (EAX=0x16 from 06b5f377 into the epilogue 06b5f490, literal at the header).
unsigned int FRpcServerReplaceMissionObjectivePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.Put(&State, 4u)) return 0x16u;
    if (!Stream.Put(&TextSize, 4u)) return 0x16u;
    if (!Stream.PutBlock(TextData, TextSize)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b5f43b
}

// RC 06b5f4a0 / RS 06bf3cf0. The block helper's status is discarded (06b5f540) and the
// end helper's 0 is stored over MarshalError: an overrunning block still decodes as
// success. Reproduced on purpose.
unsigned int FRpcServerReplaceMissionObjectivePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!Stream.Get(&State, 4u)) return MarshalError;
    RpcWireDecodeOwnedBlock(*this, Stream, TextSize, TextData);
    MarshalError = 0;
    return Stream.Finish(OutLength, false);
}

// --- 0x35 SETMISSIONOBJECTIVES ---

// RC 06b4b8c0 / RS 06be63e0: base, TextA then TextB zeroed by their ctors (EH states 1
// and 2), Count, then the list sentinel from the std::list ctor.
FRpcServerSetMissionObjectivesPacket::FRpcServerSetMissionObjectivesPacket()
    : FPacketBase(0x20035u) {
    Count = 0;
}

// RC 06b44d20 / RS 06bdaad0: the records here, then the members: the list, TextB and
// TextA (FRpcServerFlatTextBlock dtors, RS 06bdabb0 inline).
FRpcServerSetMissionObjectivesPacket::~FRpcServerSetMissionObjectivesPacket() {
    for (std::list<FRpcServerMissionObjectiveRecord*>::iterator It = Objectives.begin();
         It != Objectives.end(); ++It) {
        FRpcServerMissionObjectiveRecord* Record = *It;
        if (Record != nullptr) {
            if (Record->Data != nullptr) ::operator delete(Record->Data);
            ::operator delete(Record);
        }
    }
    Objectives.clear();
}

// RC 06b44e20.
void FRpcServerSetMissionObjectivesPacket::PushObjective(FRpcServerMissionObjectiveRecord* Record) {
    if (Record == nullptr) return;
    Objectives.push_back(Record);
    ++Count;
}

// RS 06bdac30.
FRpcServerMissionObjectiveRecord* FRpcServerSetMissionObjectivesPacket::ObjectiveAt(
    unsigned int Index) const {
    unsigned int At = 0;
    for (std::list<FRpcServerMissionObjectiveRecord*>::const_iterator It = Objectives.begin();
         It != Objectives.end(); ++It, ++At) {
        if (At == Index) return *It;
    }
    return nullptr;
}

// RC 06b44ea0 / RS 06bdac60. The records go out only when Count is non-zero; the loop
// then runs to the end of the list, not to Count, and reads each record without a
// null test.
unsigned int FRpcServerSetMissionObjectivesPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC 06b44ea0: every failure returns 0x16 (EAX=0x16 from 06b44f01 into the epilogue
    // 06b451c7, literals after the TextA copy and in the record loop 06b45179); the
    // three kinds of text block are REP MOVSD/MOVSB into the cached destination
    // (06b44fc8, 06b4505c, 06b45124).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return 0x16u;
    if (!Stream.Put(&Count, 4u)) return 0x16u;
    if (!Stream.Put(&State, 4u)) return 0x16u;
    if (!Stream.Put(&TextA.Size, 4u)) return 0x16u;
    if (!Stream.PutBlock(TextA.Data, TextA.Size)) return 0x16u;
    if (!Stream.Put(&TextB.Size, 4u)) return 0x16u;
    if (!Stream.PutBlock(TextB.Data, TextB.Size)) return 0x16u;
    if (Count != 0) {                                                  // 06b45087
        for (std::list<FRpcServerMissionObjectiveRecord*>::const_iterator It = Objectives.begin();
             It != Objectives.end(); ++It) {
            const FRpcServerMissionObjectiveRecord* Record = *It;
            if (!Stream.Put(&Record->State, 1u)) return 0x16u;
            if (!Stream.Put(&Record->Size, 4u)) return 0x16u;
            if (!Stream.PutBlock(Record->Data, Record->Size)) return 0x16u;
        }
    }
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b45169
}

// RC 06b451e0 / RS 06bdafa0. RC returns 0x17 when either text block helper fails; RS
// ignores both results. Each record: operator new(0xc) (null -> the byte is written
// through null, as retail), Size/Data zeroed, one byte State, then the record block
// (a failure returns 0x17 and leaks the record, as retail), then the list append; Count
// is not touched. The end helper's 0 is stored over MarshalError (06b453a8 / 06bdb148).
unsigned int FRpcServerSetMissionObjectivesPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    if (!Stream.Get(&State, 4u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    if (RpcWireDecodeOwnedBlock(*this, Stream, TextA.Size, TextA.Data) != 0) return 0x17u;
    if (RpcWireDecodeOwnedBlock(*this, Stream, TextB.Size, TextB.Data) != 0) return 0x17u;
#else
    RpcWireDecodeOwnedBlock(*this, Stream, TextA.Size, TextA.Data);
    RpcWireDecodeOwnedBlock(*this, Stream, TextB.Size, TextB.Data);
#endif
    for (unsigned int Index = 0; Index < Count; ++Index) {
        FRpcServerMissionObjectiveRecord* Record = static_cast<FRpcServerMissionObjectiveRecord*>(
            ::operator new(sizeof(FRpcServerMissionObjectiveRecord), std::nothrow));
        if (Record != nullptr) {
            Record->Size = 0;
            Record->Data = nullptr;
        }
        if (!Stream.Get(&Record->State, 1u)) return MarshalError;
        if (RpcWireDecodeOwnedBlock(*this, Stream, Record->Size, Record->Data) != 0) return 0x17u;
        Objectives.push_back(Record);
    }
    MarshalError = 0;
    return Stream.Finish(OutLength, false);
}

// --- 0x3c REPAIROBJECT ---
static_assert(sizeof(FRpcServerRepairObjectPacket) == 0x2c, "REPAIROBJECT is operator new(0x2c)");

// RC 06b4c4d0 / RS 06be70d0.
FRpcServerRepairObjectPacket::FRpcServerRepairObjectPacket() : FPacketBase(0x2003cu) {
    ObjectId = 0;
    EquipId = 0;
    Points = 0;
}

// RC 06b61970 / RS 06bf7b50: every failure returns literal 0x16.
unsigned int FRpcServerRepairObjectPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&EquipId, 4u)) return 0x16u;
    if (!Stream.Put(&Points, 4u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b61ac0 / RS 06bf7ca0.
unsigned int FRpcServerRepairObjectPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&EquipId, 4u)) return MarshalError;
    if (!Stream.Get(&Points, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// RC 06b61bf0 / RS 06bf7dd0: operator<<(unsigned) for the two ids, operator<<(int) for
// Points, each line ended by the "\n" string (06b6bf74).
void FRpcServerRepairObjectPacket::Dump(std::ostream& Out) const {
    Out << "   Object ID  : " << ObjectId << "\n";
    Out << "   Equip ID   : " << EquipId << "\n";
    Out << "   Points     : " << Points << "\n";
}

// --- 0x28 CREATELOOT ---

// RC 06b44b40: node operator new(0x50), the record copied in (06b44c60), size + 1.
void FRpcServerCreateLootPacket::AddLoot(const FRpcServerLootRecord& Record) {
    Loot.push_back(Record);
}

// RC 06b442a0 / RS 06bddfd0. The list size goes out as a byte; when that byte is zero
// no record is written, otherwise the walk covers the whole list. Each small id is
// computed before its reservation. Velocity and Orientation are written only when
// Common's SinglePlayer() is true (06b4455f). Every failure returns 0x16 (literals,
// and EDX=0x16 from 06b443fd / 06b44492 at 06b446a7); both floats are FSTP into the
// cached destination (06b4443c..06b44447, 06b444d3..06b444dd).
unsigned int FRpcServerCreateLootPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&OwnerId, 4u)) return 0x16u;
    if (!Stream.Put(&Kind, 1u)) return 0x16u;
    const unsigned char Count = static_cast<unsigned char>(Loot.size());
    if (!Stream.Put(&Count, 1u)) return 0x16u;
    if (Count != 0) {
        for (std::list<FRpcServerLootRecord>::const_iterator It = Loot.begin();
            It != Loot.end(); ++It) {
            const FRpcServerLootRecord& Record = *It;
            if (!Stream.Put(&Record.LootId, 4u)) return 0x16u;
            const unsigned short ContentsId = Archetype::LargeIDToSmallID(Record.ContentsNickname);
            if (!Stream.Put(&ContentsId, 2u)) return 0x16u;
            if (!Stream.PutFloat(Record.ContentsHitPts)) return 0x16u;
            if (!Stream.Put(&Record.Units, 2u)) return 0x16u;
            const unsigned short ContainerId = Archetype::LargeIDToSmallID(Record.ContainerNickname);
            if (!Stream.Put(&ContainerId, 2u)) return 0x16u;
            if (!Stream.PutFloat(Record.HitPts)) return 0x16u;
            if (!Stream.Put(&Record.NameOverride, 4u)) return 0x16u;
            if (!Stream.Put(&Record.Position, 12u)) return 0x16u;
            if (SinglePlayer()) {
                if (!Stream.Put(&Record.Velocity, 12u)) return 0x16u;
                if (!Stream.Put(&Record.Orientation, 16u)) return 0x16u;
            }
            if (!Stream.Put(&Record.bMarkedBy, 1u)) return 0x16u;
            if (!Stream.Put(&Record.bTemporary, 1u)) return 0x16u;
        }
    }
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b44689
}

// RC 06b44700 / RS 06bde430. Per record (byte counter): a zeroed stack record with
// Orientation {w=1,0,0,0} (06b44ba0) is appended first (06b44bd0), then the fields are
// read straight into the appended node, so a field failure leaves it in the list.
// Floats are integer copies; the small ids are widened by SmallIDToLargeID.
unsigned int FRpcServerCreateLootPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&OwnerId, 4u)) return MarshalError;
    if (!Stream.Get(&Kind, 1u)) return MarshalError;
    unsigned char Count;
    if (!Stream.Get(&Count, 1u)) return MarshalError;
    for (unsigned char Index = 0; Index < Count; ++Index) {
        FRpcServerLootRecord Fresh;
        Fresh.LootId = 0;
        Fresh.ContentsNickname = 0;
        std::memset(&Fresh.ContentsHitPts, 0, 4u);
        Fresh.Units = 0;
        Fresh.Pad0e = 0;
        Fresh.ContainerNickname = 0;
        std::memset(&Fresh.HitPts, 0, 4u);
        Fresh.NameOverride = 0;
        std::memset(&Fresh.Position, 0, sizeof(Fresh.Position));
        std::memset(&Fresh.Velocity, 0, sizeof(Fresh.Velocity));
        Fresh.Orientation.w = 1.0f;
        Fresh.Orientation.x = 0.0f;
        Fresh.Orientation.y = 0.0f;
        Fresh.Orientation.z = 0.0f;
        Fresh.bMarkedBy = 0;
        Fresh.bTemporary = 0;
        Fresh.Pad46[0] = 0;
        Fresh.Pad46[1] = 0;
        Loot.push_back(Fresh);
        FRpcServerLootRecord& Record = Loot.back();
        if (!Stream.Get(&Record.LootId, 4u)) return MarshalError;
        unsigned short ContentsId;
        if (!Stream.Get(&ContentsId, 2u)) return MarshalError;
        Record.ContentsNickname = Archetype::SmallIDToLargeID(ContentsId);
        if (!Stream.Get(&Record.ContentsHitPts, 4u)) return MarshalError;
        if (!Stream.Get(&Record.Units, 2u)) return MarshalError;
        unsigned short ContainerId;
        if (!Stream.Get(&ContainerId, 2u)) return MarshalError;
        Record.ContainerNickname = Archetype::SmallIDToLargeID(ContainerId);
        if (!Stream.Get(&Record.HitPts, 4u)) return MarshalError;
        if (!Stream.Get(&Record.NameOverride, 4u)) return MarshalError;
        if (!Stream.Get(&Record.Position, 12u)) return MarshalError;
        if (SinglePlayer()) {
            if (!Stream.Get(&Record.Velocity, 12u)) return MarshalError;
            if (!Stream.Get(&Record.Orientation, 16u)) return MarshalError;
        }
        if (!Stream.Get(&Record.bMarkedBy, 1u)) return MarshalError;
        if (!Stream.Get(&Record.bTemporary, 1u)) return MarshalError;
    }
    return Stream.Finish(OutLength, false);
}

// --- relation lists (0x29, 0x2b) ---

// RC 06b3f6c0 / 06b43da0, RS 06bddef0 / 06bdd430.
void RpcServerRelationListAppendCopy(std::list< ::Reputation::Relation*>& Dest,
    const std::list< ::Reputation::Relation*>& Source) {
    for (std::list< ::Reputation::Relation*>::const_iterator It = Source.begin();
        It != Source.end(); ++It) {
        const ::Reputation::Relation* From = *It;
        if (From == nullptr) continue;
        ::Reputation::Relation* Copy = static_cast< ::Reputation::Relation*>(
            ::operator new(sizeof(::Reputation::Relation), std::nothrow));
        std::memcpy(&Copy->Feeling, &From->Feeling, 4u);
        Copy->GroupId = From->GroupId;
        Dest.push_back(Copy);
    }
}

// --- 0x29 SETREPUTATION ---
#include "common_net.h" // IsMPServer (0x29 Encode)

// RC 06b4af30 / RS 06be5a50: base, NameA then NameB zeroed by their ctors (EH states 1
// and 2), then the scalars and the list.
FRpcServerSetReputationPacket::FRpcServerSetReputationPacket() : FPacketBase(0x20029u) {
    Attitude = 0.0f;
    MaskAffiliation = 1u;
    MaskNames = 2u;
    MaskDisplayName = 4u;
    MaskRelationsByte = 8u;
    MaskRelationsWord = 0x10u;
    Flags = 0;
    Target = 0;
    Affiliation = 0xffffffffu;
    DisplayNameLength = 0;
    DisplayName = nullptr;
}

// RC 06b3ed00 / RS 06bdd4e0: every relation, the display name, the list nodes, then the
// members NameB and NameA (FRpcServerFlatTextBlock dtors, RS 06bdabb0 inline).
FRpcServerSetReputationPacket::~FRpcServerSetReputationPacket() {
    for (std::list< ::Reputation::Relation*>::iterator It = Relations.begin();
        It != Relations.end(); ++It) {
        if (*It != nullptr) ::operator delete(*It);
    }
    if (DisplayName != nullptr) ::operator delete(DisplayName);
    Relations.clear();
}

// RC 06b3f670.
void FRpcServerSetReputationPacket::SetDisplayName(const wchar_t* Source, unsigned int Length) {
    if (Source == nullptr || Length == 0) return;
    const unsigned int Count = Length + 1u;
    DisplayName = static_cast<wchar_t*>(::operator new(Count * 4u, std::nothrow));
    std::memcpy(DisplayName, Source, Count * 2u);
    DisplayNameLength = static_cast<int>(Count);
}

// RC 06b3ee10 / RS 06bdd5e0. Flags is rebuilt on every encode: Affiliation != -1 sets
// the low byte of MaskAffiliation; in single player a non-zero DisplayNameLength sets
// MaskDisplayName, a non-zero first dword of either flattened block sets MaskNames (both
// blocks are read without a null test, reproduced on purpose), and a non-zero list size
// sets MaskRelationsWord when it is above 0xff (signed compare), MaskRelationsByte
// otherwise. The relation count is the list size only in single player (0 otherwise).
// The presence tests use the low byte of each mask. Every failure returns 0x16 (EDI=0x16
// from 06b3eed7, re-set after each REP block at 06b3efec / 06b3f10b, and literals);
// the display name and both name blocks are REP MOVSD/MOVSB into the cached destination
// (06b3efc5, 06b3f057, 06b3f0e5).
unsigned int FRpcServerSetReputationPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    unsigned int RelationCount = 0;
    Flags = 0;
    if (Affiliation != 0xffffffffu) {
        Flags = static_cast<unsigned char>(MaskAffiliation);
    }
    if (SinglePlayer()) {
        if (DisplayNameLength != 0) {
            Flags = static_cast<unsigned char>(Flags | static_cast<unsigned char>(MaskDisplayName));
        }
        unsigned int FirstA;
        unsigned int FirstB;
        std::memcpy(&FirstA, NameA.Data, 4u);
        bool bNames = FirstA != 0;
        if (!bNames) {
            std::memcpy(&FirstB, NameB.Data, 4u);
            bNames = FirstB != 0;
        }
        if (bNames) {
            Flags = static_cast<unsigned char>(Flags | static_cast<unsigned char>(MaskNames));
        }
        RelationCount = static_cast<unsigned int>(Relations.size());
        if (RelationCount != 0) {
            if (static_cast<int>(RelationCount) > 0xff) {
                Flags = static_cast<unsigned char>(Flags | static_cast<unsigned char>(MaskRelationsWord));
            } else {
                Flags = static_cast<unsigned char>(Flags | static_cast<unsigned char>(MaskRelationsByte));
            }
        }
    }
    if (!Stream.Put(&Flags, 1u)) return 0x16u;
    if (!Stream.Put(&Target, 4u)) return 0x16u;
    if ((static_cast<unsigned char>(MaskAffiliation) & Flags) != 0) {
        if (!Stream.Put(&Affiliation, 4u)) return 0x16u;
    }
    if ((static_cast<unsigned char>(MaskDisplayName) & Flags) != 0) {
        const unsigned char Length = static_cast<unsigned char>(DisplayNameLength);
        if (!Stream.Put(&Length, 1u)) return 0x16u;
        if (!Stream.PutBlock(DisplayName, static_cast<unsigned int>(DisplayNameLength) << 1)) {
            return 0x16u;
        }
    }
    if ((static_cast<unsigned char>(MaskNames) & Flags) != 0) {
        if (!Stream.Put(&NameA.Size, 4u)) return 0x16u;
        if (!Stream.PutBlock(NameA.Data, NameA.Size)) return 0x16u;
        if (!Stream.Put(&NameB.Size, 4u)) return 0x16u;
        if (!Stream.PutBlock(NameB.Data, NameB.Size)) return 0x16u;
    }
    if (IsMPServer()) {
        if (!Stream.PutFloat(Attitude)) return 0x16u;
    } else if (RelationCount != 0) {
        if ((static_cast<unsigned char>(MaskRelationsWord) & Flags) != 0) {
            const unsigned short Count = static_cast<unsigned short>(RelationCount);
            if (!Stream.Put(&Count, 2u)) return 0x16u;
        } else {
            const unsigned char Count = static_cast<unsigned char>(RelationCount);
            if (!Stream.Put(&Count, 1u)) return 0x16u;
        }
        for (std::list< ::Reputation::Relation*>::const_iterator It = Relations.begin();
            It != Relations.end(); ++It) {
            const ::Reputation::Relation* Relation = *It;
            if (!Stream.PutFloat(Relation->Feeling)) return 0x16u;
            if (!Stream.Put(&Relation->GroupId, 4u)) return 0x16u;
        }
    }
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b3f22c
}

// RC 06b3f290 / RS 06bdda60. Affiliation is -1 when its bit is clear. In single player:
// the display name (signed byte length; RC checks the room before allocating, RS
// 06bddb7b allocates first; operator new(length * 4 + 4) with no null test, length * 2
// bytes, a terminator at [length]) or DisplayNameLength = 0, and the two FmtStr blocks,
// whose results are discarded (06b3f44f / 06b3f45a); then the relations (count byte or
// word; per relation operator new(8) with no null test, Feeling, GroupId, append). Not
// in single player: Attitude as an integer copy. The end helper's 0 is stored over
// MarshalError, so a failed block still decodes as success. Reproduced on purpose.
unsigned int FRpcServerSetReputationPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Flags, 1u)) return MarshalError;
    if (!Stream.Get(&Target, 4u)) return MarshalError;
    if ((static_cast<unsigned char>(MaskAffiliation) & Flags) != 0) {
        if (!Stream.Get(&Affiliation, 4u)) return MarshalError;
    } else {
        Affiliation = 0xffffffffu;
    }
    if (SinglePlayer()) {
        if ((static_cast<unsigned char>(MaskDisplayName) & Flags) != 0) {
            signed char Length;
            if (!Stream.Get(&Length, 1u)) return MarshalError;
            DisplayNameLength = static_cast<int>(Length);
#if defined(REMOTECLIENT_EXPORTS)
            if (MarshalCursor + (static_cast<unsigned int>(DisplayNameLength) << 1) > MarshalLimit) {
                MarshalError = 0x17u;
            }
            if (MarshalError != 0) return MarshalError;
#endif
            DisplayName = static_cast<wchar_t*>(::operator new(
                static_cast<unsigned int>(DisplayNameLength) * 4u + 4u, std::nothrow));
            if (!Stream.Get(DisplayName, static_cast<unsigned int>(DisplayNameLength) << 1)) {
                return MarshalError;
            }
            DisplayName[DisplayNameLength] = 0;
        } else {
            DisplayNameLength = 0;
        }
        if ((static_cast<unsigned char>(MaskNames) & Flags) != 0) {
            RpcWireDecodeOwnedBlock(*this, Stream, NameA.Size, NameA.Data);
            RpcWireDecodeOwnedBlock(*this, Stream, NameB.Size, NameB.Data);
        }
    }
    if (SinglePlayer()) {
        const unsigned int Present = static_cast<unsigned int>(Flags);
        if ((Present & (MaskRelationsByte | MaskRelationsWord)) != 0) {
            unsigned int Count;
            if ((Present & MaskRelationsWord) != 0) {
                unsigned short Word;
                if (!Stream.Get(&Word, 2u)) return MarshalError;
                Count = Word;
            } else {
                unsigned char Byte;
                if (!Stream.Get(&Byte, 1u)) return MarshalError;
                Count = Byte;
            }
            for (unsigned int Index = 0; Index < Count; ++Index) {
                ::Reputation::Relation* Relation = static_cast< ::Reputation::Relation*>(
                    ::operator new(sizeof(::Reputation::Relation), std::nothrow));
                if (!Stream.Get(&Relation->Feeling, 4u)) return MarshalError;
                if (!Stream.Get(&Relation->GroupId, 4u)) return MarshalError;
                Relations.push_back(Relation);
            }
        }
    } else {
        if (!Stream.Get(&Attitude, 4u)) return MarshalError;
    }
    MarshalError = 0;
    return Stream.Finish(OutLength, false);
}

// RC 06b3f7c0 / RS 06bddf70: both values through operator<<(unsigned), each line ended
// by the "\n" string (06b6bf74).
void FRpcServerSetReputationPacket::Dump(std::ostream& Out) const {
    Out << "   Target     : " << Target << "\n";
    Out << "   Affiliation: " << Affiliation << "\n";
}

// --- 0x2b SETGROUPFEELINGS ---

// RC 06b43870 / RS (06bdcf40's body): every relation, the name, then the list nodes.
FRpcServerSetGroupFeelingsPacket::~FRpcServerSetGroupFeelingsPacket() {
    for (std::list< ::Reputation::Relation*>::iterator It = Relations.begin();
        It != Relations.end(); ++It) {
        if (*It != nullptr) ::operator delete(*It);
    }
    if (Name != nullptr) ::operator delete(Name);
    Relations.clear();
}

// RC 06b43920.
void FRpcServerSetGroupFeelingsPacket::SetName(const char* Source, unsigned int Size) {
    if (Name != nullptr) ::operator delete(Name);
    Name = static_cast<char*>(::operator new(Size, std::nothrow));
    std::strncpy(Name, Source, Size);
    NameSize = Size;
}

// RC 06b43960 / RS 06bdcfa0. No count is written (see the class comment); the relation
// walk runs only when the low word of the list size is non-zero and skips null entries.
// Every failure returns 0x16 (EDI=0x16 from 06b439b7, re-set after the name copy at
// 06b43a2b); the name is a REP MOVSD/MOVSB into the cached destination (06b43a06).
unsigned int FRpcServerSetGroupFeelingsPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&NameSize, 4u)) return 0x16u;
    if (NameSize > 0) {                                                // 06b439e2 unsigned
        if (!Stream.PutBlock(Name, NameSize)) return 0x16u;
    }
    if (static_cast<unsigned short>(Relations.size()) > 0) {
        for (std::list< ::Reputation::Relation*>::const_iterator It = Relations.begin();
            It != Relations.end(); ++It) {
            const ::Reputation::Relation* Relation = *It;
            if (Relation == nullptr) continue;
            if (!Stream.PutFloat(Relation->Feeling)) return 0x16u;
            if (!Stream.Put(&Relation->GroupId, 4u)) return 0x16u;
        }
    }
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b43aae
}

// RC 06b43b10 / RS 06bdd150. The name: RC checks the room first and returns 5 on a
// failed allocation; RS (06bdd1cf) allocates first with no null test. Then a u16 count
// the encoder never writes (reproduced on purpose) and per relation operator new(8)
// with no null test, Feeling, GroupId (a failure returns and leaks the relation), append.
unsigned int FRpcServerSetGroupFeelingsPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&NameSize, 4u)) return MarshalError;
    if (NameSize > 0) {
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + NameSize > MarshalLimit) MarshalError = 0x17u;
        if (MarshalError != 0) return MarshalError;
        Name = static_cast<char*>(::operator new(NameSize, std::nothrow));
        if (Name == nullptr) return 5u;
#else
        Name = static_cast<char*>(::operator new(NameSize, std::nothrow));
#endif
        if (!Stream.Get(Name, NameSize)) return MarshalError;
    }
    unsigned short Count;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    for (unsigned short Index = 0; Index < Count; ++Index) {
        ::Reputation::Relation* Relation = static_cast< ::Reputation::Relation*>(
            ::operator new(sizeof(::Reputation::Relation), std::nothrow));
        if (!Stream.Get(&Relation->Feeling, 4u)) return MarshalError;
        if (!Stream.Get(&Relation->GroupId, 4u)) return MarshalError;
        Relations.push_back(Relation);
    }
    return Stream.Finish(OutLength, false);
}

// --- 0x2c CREATEMINE ---
static_assert(sizeof(FRpcServerCreateMinePacket) == 0x68, "CREATEMINE is operator new(0x68)");

// RC 06b4a9e0 / RS 06be5500.
FRpcServerCreateMinePacket::FRpcServerCreateMinePacket() : FPacketBase(0x2002cu) {
    ObjId = 0;
    ArchetypeNickname = 0;
    OwnerId = 0;
    SubObjId = 0;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Velocity.x = 0.0f;
    Velocity.y = 0.0f;
    Velocity.z = 0.0f;
    Field_0x38.x = 0.0f;
    Field_0x38.y = 0.0f;
    Field_0x38.z = 0.0f;
    Field_0x44 = 0;
}

// RC 06b5d740 / RS 06bf16c0. The small id is computed before its reservation; the
// last three blocks go out only when SinglePlayer() is true (06b5d8b5). Every failure
// returns 0x16 (EBP=0x16 from 06b5d79d, literals in the single-player blocks); the
// blocks are integer copies into the cached destination.
unsigned int FRpcServerCreateMinePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjId, 4u)) return 0x16u;
    const unsigned short SmallId = Archetype::LargeIDToSmallID(ArchetypeNickname);
    if (!Stream.Put(&SmallId, 2u)) return 0x16u;
    if (!Stream.Put(&OwnerId, 4u)) return 0x16u;
    if (!Stream.Put(&SubObjId, 2u)) return 0x16u;
    if (!Stream.Put(&Position, 12u)) return 0x16u;
    if (SinglePlayer()) {
        if (!Stream.Put(&Orientation, 16u)) return 0x16u;
        if (!Stream.Put(&Velocity, 12u)) return 0x16u;
        if (!Stream.Put(&Field_0x38, 12u)) return 0x16u;
    }
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b5d9d3
}

// RC 06b5da40 / RS 06bf19c0. Field_0x44 is zeroed right before the end helper
// (06b5dc1b), on both single-player paths.
unsigned int FRpcServerCreateMinePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjId, 4u)) return MarshalError;
    unsigned short SmallId;
    if (!Stream.Get(&SmallId, 2u)) return MarshalError;
    ArchetypeNickname = Archetype::SmallIDToLargeID(SmallId);
    if (!Stream.Get(&OwnerId, 4u)) return MarshalError;
    if (!Stream.Get(&SubObjId, 2u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    if (SinglePlayer()) {
        if (!Stream.Get(&Orientation, 16u)) return MarshalError;
        if (!Stream.Get(&Velocity, 12u)) return MarshalError;
        if (!Stream.Get(&Field_0x38, 12u)) return MarshalError;
    }
    Field_0x44 = 0;
    return Stream.Finish(OutLength, false);
}

// --- 0x3d REMOTEOBJECTCARGOUPDATE ---

// RC 06b454c0 / RS 06bda7f0. The equipment helper's non-zero result is returned as is.
unsigned int FRpcServerRemoteObjectCargoUpdatePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // Failed own reservations return literal 0x16 (RC06b454c0 body); the shared
    // helper's literal 0x16 is passed through; success takes the trailer tail.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&Flag, 1u)) return 0x16u;
    const unsigned int Result = EncodeEquipment(Stream);
    if (Result != 0) return Result;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b455f0 / RS 06bda920.
unsigned int FRpcServerRemoteObjectCargoUpdatePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&Flag, 1u)) return MarshalError;
    const unsigned int Result = DecodeEquipment(Stream);
    if (Result != 0) return Result;
    return Stream.Finish(OutLength, false);
}

// ===== end SERVER 0x20..0x3f (RS-P2) =====

// ===== SERVER 0x40..0x5b (RS-P3) =====
// Bodies compiled into both DLLs. Every encoder: header 06b4e740 / 06bebe50, one
// reservation + check per field (error 0x16), end helper, OutLength, stats 06b4ecd0 /
// 06bec3b0. Every decoder: the same with 0x17 and the decode stats.

// --- 0x41 BURNFUSE ---
// RC ctor 06b4c750 / RS 06be7350.
FRpcServerBurnFusePacket::FRpcServerBurnFusePacket()
    : FPacketBase(0x20041u), SourceId(0), ObjectId(0), FuseId(0), SubObjId(SUBOBJ_ID_NONE),
      AtT(0.0f), Lifetime(-1.0f), bLit(0) {}

// RC 06b62240 / RS 06bf8420. Every failure returns 0x16 (EBP=0x16 from 06b62297); both
// floats are FSTP into the cached destination (06b62374, 06b623a6).
unsigned int FRpcServerBurnFusePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&SourceId, 4u)) return 0x16u;
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&FuseId, 4u)) return 0x16u;
    if (!Stream.Put(&SubObjId, 2u)) return 0x16u;
    if (!Stream.PutFloat(AtT)) return 0x16u;
    if (!Stream.PutFloat(Lifetime)) return 0x16u;
    if (!Stream.Put(&bLit, 1u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b623de
}

// RC 06b62440 / RS 06bf8620: the floats are integer moves here.
unsigned int FRpcServerBurnFusePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&SourceId, 4u)) return MarshalError;
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&FuseId, 4u)) return MarshalError;
    if (!Stream.Get(&SubObjId, 2u)) return MarshalError;
    if (!Stream.Get(&AtT, 4u)) return MarshalError;
    if (!Stream.Get(&Lifetime, 4u)) return MarshalError;
    if (!Stream.Get(&bLit, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x44 REQUEST_RETURNED ---
// RC 06b5a540 / RS 06bf2a20: 7 body bytes, no padding byte. Every failure returns 0x16
// (EBP=0x16 from 06b5a597).
unsigned int FRpcServerRequestReturnedPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&RequestType, 1u)) return 0x16u;
    if (!Stream.Put(&Response, 1u)) return 0x16u;
    if (!Stream.Put(&Field_0x26, 1u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b5a646
}

// RC 06b5a6b0 / RS 06bf2b90.
unsigned int FRpcServerRequestReturnedPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&RequestType, 1u)) return MarshalError;
    if (!Stream.Get(&Response, 1u)) return MarshalError;
    if (!Stream.Get(&Field_0x26, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x45 SET_MISSION_MESSAGE ---
// RC 06b4b860 / RS 06be6380 (called by the scalar deleting dtors 06b4b840 / 06be6360):
// operator delete of the block when set.
FRpcServerSetMissionMessagePacket::~FRpcServerSetMissionMessagePacket() {
    if (TextData != nullptr) ::operator delete(TextData);
}

// RC 06b5fc90 / RS 06bf44e0: Type first, then the block's size and bytes (a REP
// MOVSD/MOVSB into the cached destination, 06b5fd4c..06b5fd7c, even when TextSize is
// 0). Every failure returns 0x16 (EAX=0x16 from 06b5fce7 into the epilogue, literals).
unsigned int FRpcServerSetMissionMessagePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Type, 4u)) return 0x16u;
    if (!Stream.Put(&TextSize, 4u)) return 0x16u;
    if (!Stream.PutBlock(TextData, TextSize)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b5fd81
}

// RC 06b5fde0 / RS 06bf4630.
unsigned int FRpcServerSetMissionMessagePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Type, 4u)) return MarshalError;
    // The block helper's status is discarded (RC 06b5fe52) and the end helper's 0 is
    // stored over MarshalError (06b5fe63): an overrunning block still decodes as
    // success. Reproduced on purpose.
    RpcWireDecodeOwnedBlock(*this, Stream, TextSize, TextData);
    MarshalError = 0;
    return Stream.Finish(OutLength, false);
}

// --- 0x46 MARKOBJ ---
// RC 06b5ee40 / RS 06bf3690: every failure returns literal 0x16; trailer 06b5eef5.
unsigned int FRpcServerMarkObjPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&MarkType, 1u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5ef60 / RS 06bf37b0.
unsigned int FRpcServerMarkObjPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&MarkType, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x47 CFGINTERFACENOTIFICATION ---
// RC 06b5f070 / RS 06bf38c0: the byte travels between the two dwords. Every failure
// returns literal 0x16; trailer 06b5f15a.
unsigned int FRpcServerCfgInterfaceNotificationPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&NotifType, 4u)) return 0x16u;
    if (!Stream.Put(&bEnable, 1u)) return 0x16u;
    if (!Stream.Put(&Param, 4u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5f1c0 / RS 06bf3a10.
unsigned int FRpcServerCfgInterfaceNotificationPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&NotifType, 4u)) return MarshalError;
    if (!Stream.Get(&bEnable, 1u)) return MarshalError;
    if (!Stream.Get(&Param, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x4a SETGUIDEDTARGET ---
// RC 06b62ee0 / RS 06bf90c0: every failure returns literal 0x16; trailer 06b62fd0.
unsigned int FRpcServerSetGuidedTargetPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x20, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x24, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x28, 2u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b63030 / RS 06bf9210.
unsigned int FRpcServerSetGuidedTargetPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x20, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x24, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x28, 2u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x4b SET_CAMERA ---
// RC ctor 06b4b6c0 / RS 06be61e0: +0x20..+0x28 = 0, +0x2c = 1.0f, +0x30..+0x38 = 0.
FRpcServerSetCameraPacket::FRpcServerSetCameraPacket() : FPacketBase(0x2004bu) {
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
}

// RC 06b5f7b0 / RS 06bf4000: Position and Orientation through the raw 3- and 4-dword
// copiers 06b3a160 / 06b39ec0, the two floats through FLD/FSTP, all into the cached
// destination. Every failure returns literal 0x16; trailer 06b5f92c.
unsigned int FRpcServerSetCameraPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Position, 12u)) return 0x16u;
    if (!Stream.Put(&Orientation, 16u)) return 0x16u;
    if (!Stream.PutFloat(Fov)) return 0x16u;
    if (!Stream.PutFloat(Aspect)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b5f990 / RS 06bf41e0: all integer copies.
unsigned int FRpcServerSetCameraPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    if (!Stream.Get(&Orientation, 16u)) return MarshalError;
    if (!Stream.Get(&Fov, 4u)) return MarshalError;
    if (!Stream.Get(&Aspect, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x51 USE_ITEM ---
// RC 06b63850 / RS 06bf9a00: every failure returns literal 0x16; trailer 06b63942.
unsigned int FRpcServerUseItemPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&EquipId, 2u)) return 0x16u;
    if (!Stream.Put(&Count, 2u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b639a0 / RS 06bf9b50.
unsigned int FRpcServerUseItemPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&EquipId, 2u)) return MarshalError;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x53 FORMATION_UPDATE ---
// RC ctor 06b4d010 / RS 06be7c30: all four dwords zero.
FRpcServerFormationUpdatePacket::FRpcServerFormationUpdatePacket()
    : FPacketBase(0x20053u), LeaderId(0) {
    Offset.x = 0.0f;
    Offset.y = 0.0f;
    Offset.z = 0.0f;
}

// RC 06b63d60 / RS 06bfa330: Offset through the raw copier 06b3a160 into the cached
// destination. Every failure returns literal 0x16; trailer 06b63e45.
unsigned int FRpcServerFormationUpdatePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&LeaderId, 4u)) return 0x16u;
    if (!Stream.Put(&Offset, 12u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b63eb0 / RS 06bfa480.
unsigned int FRpcServerFormationUpdatePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&LeaderId, 4u)) return MarshalError;
    if (!Stream.Get(&Offset, 12u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x55 OBJECTCARGOUPDATE ---
// RC ctor 06b4d160 / RS 06be7da0.
FRpcServerObjectCargoUpdatePacket::FRpcServerObjectCargoUpdatePacket()
    : FPacketBase(0x20055u), ObjectId(0), ArchId(0), EquipId(0), Count(1), Status(1.0f),
      bMission(0) {}

// RC 06b64040 / RS 06bfb4c0. Every failure returns 0x16 (EBP=0x16 from 06b64097);
// Status is FSTP into the cached destination (06b64179).
unsigned int FRpcServerObjectCargoUpdatePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return 0x16u;
    if (!Stream.Put(&ArchId, 4u)) return 0x16u;
    if (!Stream.Put(&EquipId, 2u)) return 0x16u;
    if (!Stream.Put(&Count, 2u)) return 0x16u;
    if (!Stream.PutFloat(Status)) return 0x16u;
    if (!Stream.Put(&bMission, 1u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b641b1
}

// RC 06b64210 / RS 06bfb690.
unsigned int FRpcServerObjectCargoUpdatePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&ArchId, 4u)) return MarshalError;
    if (!Stream.Get(&EquipId, 2u)) return MarshalError;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    if (!Stream.Get(&Status, 4u)) return MarshalError;
    if (!Stream.Get(&bMission, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x57 SET_MUSIC ---
// RC 06b645b0 / RS 06bfba30. Every failure returns 0x16 (EBP=0x16 from 06b64607); the
// float is FSTP into the cached destination (06b6470d).
unsigned int FRpcServerSetMusicPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x20, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x24, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x28, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x2c, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x30, 1u)) return 0x16u;
    if (!Stream.PutFloat(Field_0x34)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b6471c
}

// RC 06b64780 / RS 06bfbc00.
unsigned int FRpcServerSetMusicPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x20, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x24, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x28, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x2c, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x30, 1u)) return MarshalError;
    if (!Stream.Get(&Field_0x34, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x4d LOADHINT (RS-P4) ---
// RC 06b42f00 / RS 06bdbc10: free a non-null block and clear the pointer.
FRpcServerLoadHintPacket::~FRpcServerLoadHintPacket() {
    if (Data != nullptr) {
        ::operator delete(Data);
        Data = nullptr;
    }
}

// RC 06b42f40 (SendServerLoadHint's setter): Data = operator new(InSize) with no null test, the
// copy, then Size. A block already held is not released.
void FRpcServerLoadHintPacket::SetData(const unsigned char* InData, unsigned int InSize) {
    Data = static_cast<unsigned char*>(::operator new(InSize, std::nothrow));
    std::memcpy(Data, InData, InSize);
    Size = InSize;
}

// RC 06b42f90 / RS 06bdbca0: Size, Kind, then the Size bytes of the block (REP
// MOVSD/MOVSB into the cached destination, 06b43065..06b4307c). Every failure returns
// 0x16 (EAX=0x16 from 06b42fe7 into the epilogue, literals).
unsigned int FRpcServerLoadHintPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Size, 4u)) return 0x16u;
    if (!Stream.Put(&Kind, 4u)) return 0x16u;
    if (!Stream.PutBlock(Data, Size)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b43081
}

// The two DLLs differ after Size and Kind are read:
// - RC 06b430e0: the body is checked against the limit without committing it (0x17),
//   then operator new(Size); a null result returns 5 (Size kept, error untouched);
// - RS 06bdbdf0: operator new(Size) with no prior check and no null test (reproduced on
//   purpose: a failed allocation faults in the copy).
// Then both commit and check the body and copy it. A block already held is overwritten.
unsigned int FRpcServerLoadHintPacket::Decode(const unsigned char* Buffer,
    unsigned int InSize, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), InSize);
    if (!Stream.Get(&Size, 4u)) return MarshalError;
    if (!Stream.Get(&Kind, 4u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    if (MarshalCursor + Size > MarshalLimit) MarshalError = 0x17u;
    if (MarshalError != 0) return MarshalError;
    Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
    if (Data == nullptr) return 5u;
#else
    Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
#endif
    if (!Stream.Get(Data, Size)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// --- 0x4e SETDIRECTIVE (RS-P4) ---

// The field-wise copy of FPayloadServerSetDirective that retail repeats in four places:
// its operator= (RC 06b43710 / RS 06bdc400), the sender 06b35f40, and the decoders
// 06b434d0 / 06bdc1c0, all inlined the same way. Every field from +0x00 to +0x90 by
// integer moves (the three-dword blocks through 06b3a160 / 06bda4c0); the padding bytes
// +0x09..+0x0b and +0x91..+0x93 are never written.
static void RpcServerCopyDirectiveFields(void* Destination, const void* Source) {
    unsigned char* Dst = static_cast<unsigned char*>(Destination);
    const unsigned char* Src = static_cast<const unsigned char*>(Source);
    std::memcpy(Dst, Src, 0x09u);
    std::memcpy(Dst + 0x0cu, Src + 0x0cu, 0x85u);
}

// RC 06b4aca0 / RS 06be57c0: +0x00 and the padding stay as operator new left them.
FRpcServerSetDirectivePacket::FRpcServerSetDirectivePacket() : FPacketBase(0x2004eu) {
    Directive.Field_0x04 = 0;
    Directive.Field_0x08 = 0;
    Directive.Field_0x0c = 0.0f;
    Directive.Field_0x10 = 0;
    Directive.Field_0x14 = 0;
    Directive.Field_0x18 = -1;
    Directive.Field_0x1c = 0;
    Directive.Field_0x20 = 0.0f;
    Directive.Field_0x24 = 0.0f;
    Directive.Field_0x28 = 0;
    Directive.Field_0x2c = 0.0f;
    Directive.Field_0x30.x = 0.0f;
    Directive.Field_0x30.y = 0.0f;
    Directive.Field_0x30.z = 0.0f;
    Directive.Field_0x3c = 0.0f;
    Directive.Field_0x40 = 3;
    Directive.Field_0x44.x = 0.0f;
    Directive.Field_0x44.y = 0.0f;
    Directive.Field_0x44.z = 0.0f;
    Directive.Field_0x50 = 0;
    Directive.Block_0x54 = FRpcDirectiveBlock{0u, 0u, 0u};
    Directive.Block_0x60 = FRpcDirectiveBlock{0u, 0u, 0u};
    Directive.Block_0x6c = FRpcDirectiveBlock{0u, 0u, 0u};
    Directive.Block_0x78 = FRpcDirectiveBlock{0u, 0u, 0u};
    Directive.Field_0x84 = 0.0f;
    Directive.Field_0x88 = 0.0f;
    Directive.Field_0x8c = 0;
    Directive.Field_0x8d = 0;
    Directive.Field_0x8e = 0;
    Directive.Field_0x8f = 0;
    Directive.Field_0x90 = 0;
}

// The copy inlined in the sender RC 06b35f40 (Source -> packet +0x20).
void FRpcServerSetDirectivePacket::SetDirective(const FPayloadServerSetDirective& Source) {
    RpcServerCopyDirectiveFields(&Directive, &Source);
}

// RC 06b43270 / RS 06bdbf60 (they differ only in the copier addresses 06b436c0 / 06bdc3b0
// and 06b43710 / 06bdc400): one 0x94-byte reservation; retail then copies the record to a
// stack temporary field by field and assigns that temporary to the buffer with operator=,
// which writes the same bytes as the direct field copy here.
unsigned int FRpcServerSetDirectivePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // Every failure returns literal 0x16 (06b432b7 / 06b432e9). The destination is read
    // once (06b433f7) and Ptr=cached+0x94 is committed after the copy (06b4345c).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Reserve(0x94u, 0x16u)) return 0x16u;
    const unsigned int Cached = MarshalPtr;
    RpcServerCopyDirectiveFields(reinterpret_cast<void*>(Cached), &Directive);
    MarshalPtr = Cached + 0x94u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b4345e
}

// RC 06b434d0 / RS 06bdc1c0 (identical): one 0x94-byte reservation, the field copy from
// the buffer, then the pointer moves past the record.
unsigned int FRpcServerSetDirectivePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Reserve(0x94u, 0x17u)) return MarshalError;
    RpcServerCopyDirectiveFields(&Directive, reinterpret_cast<const void*>(MarshalPtr));
    Stream.CommitReadPointer(0x94u);
    return Stream.Finish(OutLength, false);
}

// --- 0x52 PLAYERLIST (RS-P4) ---
#include <cstdlib>
#include <cwchar>

// RC 06b466a0 / RS 06bf9f40: PlayerId is cleared first, then a held Name is freed and
// the pointer cleared.
FRpcServerPlayerListPacket::~FRpcServerPlayerListPacket() {
    PlayerId = 0;
    if (Name != nullptr) {
        std::free(Name);
        Name = nullptr;
    }
}

// RC 06b466f0 (the setter all three senders call): PlayerId first; a held Name is freed
// and cleared; a non-null InName is duplicated with _wcsdup (no null test of the result).
void FRpcServerPlayerListPacket::SetEntry(const wchar_t* InName, unsigned int InPlayerId) {
    PlayerId = InPlayerId;
    if (Name != nullptr) {
        std::free(Name);
        Name = nullptr;
    }
    if (InName != nullptr) {
        Name = _wcsdup(InName);
    }
}

// RC 06b46730 / RS 06bf9fb0 (identical): Kind, PlayerId, the byte +0x2c, then the
// character count as one byte -- the low byte of wcslen + 1 (it wraps for 255+ characters),
// 0 for a null Name -- and, when it is not 0, count * 2 bytes from Name.
unsigned int FRpcServerPlayerListPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // Every failure returns 0x16 (EDI=0x16 from 06b46787, re-set 06b4688f); the name is
    // a REP MOVSD/MOVSB into the cached destination (06b4686d..06b4688c).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Kind, 4u)) return 0x16u;
    if (!Stream.Put(&PlayerId, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x2c, 1u)) return 0x16u;
    unsigned char Count = 0;
    if (Name != nullptr) {
        Count = static_cast<unsigned char>(std::wcslen(Name) + 1u);
    }
    if (!Stream.Put(&Count, 1u)) return 0x16u;
    if (Count != 0) {
        if (!Stream.PutBlock(Name, static_cast<unsigned int>(Count) * 2u)) return 0x16u;
    }
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b4689f
}

// RC 06b46900 / RS 06bfa180. Kind, PlayerId, the byte, then the count byte; a zero count
// leaves Name as it was. Otherwise the two DLLs differ:
// - RC checks the body against the limit without committing it (0x17), then malloc;
// - RS calls malloc with no prior check.
// Both then commit and check the body (0x17) and copy it; no null test of malloc's
// result, and a Name already held is overwritten, not freed.
unsigned int FRpcServerPlayerListPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Kind, 4u)) return MarshalError;
    if (!Stream.Get(&PlayerId, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x2c, 1u)) return MarshalError;
    unsigned char Count = 0;
    if (!Stream.Get(&Count, 1u)) return MarshalError;
    if (Count != 0) {
        const unsigned int Bytes = static_cast<unsigned int>(Count) * 2u;
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + Bytes > MarshalLimit) MarshalError = 0x17u;
        if (MarshalError != 0) return MarshalError;
#endif
        Name = static_cast<wchar_t*>(std::malloc(Bytes));
        if (!Stream.Get(Name, Bytes)) return MarshalError;
    }
    return Stream.Finish(OutLength, false);
}

// --- 0x48 SETCOLLISIONGROUPS (RS-P4) ---

// RC 06b3a360 (SendServerSetCollisionGroups's setter): the list's operator= -- a list assigned to itself is
// left alone, existing nodes are overwritten, surplus ones erased, the rest appended.
void FRpcServerSetCollisionGroupsPacket::SetGroups(const std::list<CollisionGroupDesc>& Source) {
    Groups = Source;
}

// RC 06b62b20 / RS 06bf8d00 (they differ only in the helper address): header, the payload
// helper RC 06b3a180 / RS 06bda4e0 (= RpcWireEncodeCollisionGroups: the low word of the
// list size; when it is not 0 every node, u16 Id then Health through FLD/FSTP), whose
// nonzero return is returned as is, then end helper and stats.
unsigned int FRpcServerSetCollisionGroupsPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC 06b62b76..06b62b7d: the helper's nonzero result goes straight to the epilogue
    // 06b62bdc; header failure returns literal 0x16.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    const unsigned int Result = RpcWireEncodeCollisionGroups(Stream, Groups);
    if (Result != 0) return Result;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b62b87
}

// RC 06b62bf0 / RS 06bf8dd0: the payload helper RC 06b3a230 / RS 06bda590 (=
// RpcWireDecodeCollisionGroups: the list emptied first, the u16 count, each node appended
// before its fields are read), whose nonzero return is returned as is, then end helper and
// stats.
unsigned int FRpcServerSetCollisionGroupsPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    const unsigned int Result = RpcWireDecodeCollisionGroups(*this, Stream, Groups);
    if (Result != 0) return Result;
    return Stream.Finish(OutLength, false);
}

// --- 0x4f SENDCOMM (RS-P9) ---

// The one-byte quantizer of +0x70: FLD float, FMUL float 0.1f (RC 06b6e944 / RS 06c0e278),
// FMUL double 255.0 (RC 06b6e390 / RS 06c0e270), MSVCRT _ftol (truncating 64-bit FISTP),
// low byte. Its inverse: the byte zero-extended, FILD dword, FMUL double 2/51 (RC 06b6e948 /
// RS 06c0e280, 3fa41414'14141414), FSTP float.
static const float GRpcServerSendCommTenth = 0.1f;
static const double GRpcServerSendCommScale255 = 255.0;
static const double GRpcServerSendCommExpand = 2.0 / 51.0;

static unsigned char RpcServerSendCommQuantize(const float* Value) {
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Quantized;
    __asm {
        mov ecx, Value
        fld dword ptr [ecx]
        fmul dword ptr GRpcServerSendCommTenth
        fmul qword ptr GRpcServerSendCommScale255
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Quantized
        fldcw OldControl
    }
    return static_cast<unsigned char>(Quantized);
}

static void RpcServerSendCommExpand(unsigned char Encoded, float* Value) {
    const int Widened = static_cast<int>(Encoded);
    PortableRpcQuantization::ExpandInteger(Widened, GRpcServerSendCommExpand, Value);
}

// RC 06b4cd10 / RS 06be7910.
FRpcServerSendCommPacket::FRpcServerSendCommPacket()
    : FPacketBase(0x2004fu), Field_0x64(nullptr), Field_0x74(1) {
    Body.PartIds[0] = 0;
    Body.PartIds[1] = 0;
    Body.PartIds[2] = 0;
    Body.PartIds[3] = 0;
    Body.AccessoryCount = 0;
}

// RC 06b4cd70 / RS 06be7970: operator delete of a held array, then the pointer cleared.
FRpcServerSendCommPacket::~FRpcServerSendCommPacket() {
    if (Field_0x64 != nullptr) ::operator delete(Field_0x64);
    Field_0x64 = nullptr;
}

// RC 06b63190 / RS 06bf9370 (they differ only in the constant addresses). Part ids 2 and 3
// are not sent. The accessory loop is bounded by the signed count alone, re-read each pass:
// counts above 8 read on past AccessoryIds into the fields that follow and, from 15 on, past
// the packet (as retail does). The extra-array loop is bounded by the signed byte. The
// quantized byte is computed before its reservation. Every failure returns 0x16 (EAX=0x16
// from 06b631cb, re-set 06b6333f after each accessory store, into the epilogue
// 06b6349f; literals at 06b63432 / 06b63459).
unsigned int FRpcServerSendCommPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x20, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x24, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x28, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x74, 1u)) return 0x16u;
    if (Field_0x74 != 0) {
        if (!Stream.Put(&Body.PartIds[0], 4u)) return 0x16u;
        if (!Stream.Put(&Body.PartIds[1], 4u)) return 0x16u;
        const unsigned char CountByte = static_cast<unsigned char>(Body.AccessoryCount);
        if (!Stream.Put(&CountByte, 1u)) return 0x16u;
        const unsigned int* Ids = &Body.AccessoryIds[0];
        for (int Index = 0; Index < static_cast<int>(Body.AccessoryCount); ++Index) {
            if (!Stream.Put(&Ids[Index], 4u)) return 0x16u;
        }
    }
    if (!Stream.Put(&Field_0x60, 4u)) return 0x16u;
    if (!Stream.Put(&Field_0x68, 1u)) return 0x16u;
    for (int Index = 0; Index < static_cast<int>(Field_0x68); ++Index) {
        if (!Stream.Put(&Field_0x64[Index], 4u)) return 0x16u;
    }
    const unsigned short LowWord = static_cast<unsigned short>(Field_0x6c);
    if (!Stream.Put(&LowWord, 2u)) return 0x16u;
    const unsigned char Quantized = RpcServerSendCommQuantize(&Field_0x70);
    if (!Stream.Put(&Quantized, 1u)) return 0x16u;
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b6344a
}

// RC 06b634b0 / RS 06bf9690. With the costume byte set: part ids 0 and 1, ids 2 and 3
// zeroed, the count byte zero-extended into AccessoryCount, and the accessory loop bounded by
// AccessoryCount re-read each pass -- counts 9..255 write on past AccessoryIds (the ninth id
// lands on AccessoryCount itself and becomes the new bound), reproduced on purpose. Without
// it the four part ids and the count are zeroed. A held extra array is deleted and replaced
// by operator new((signed) count * 4). The two DLLs differ there:
// - RC checks (signed) count * 4 against the limit without committing it (0x17) and returns
//   5 when operator new fails;
// - RS does neither (a failed allocation with a positive count faults in the loop).
// The last byte is expanded through the x87 into +0x70.
unsigned int FRpcServerSendCommPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x20, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x24, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x28, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x74, 1u)) return MarshalError;
    if (Field_0x74 != 0) {
        if (!Stream.Get(&Body.PartIds[0], 4u)) return MarshalError;
        if (!Stream.Get(&Body.PartIds[1], 4u)) return MarshalError;
        Body.PartIds[2] = 0;
        Body.PartIds[3] = 0;
        unsigned char CountByte = 0;
        if (!Stream.Get(&CountByte, 1u)) return MarshalError;
        Body.AccessoryCount = CountByte;
        unsigned int* Ids = &Body.AccessoryIds[0];
        for (int Index = 0; Index < static_cast<int>(Body.AccessoryCount); ++Index) {
            if (!Stream.Get(&Ids[Index], 4u)) return MarshalError;
        }
    } else {
        Body.PartIds[0] = 0;
        Body.PartIds[1] = 0;
        Body.PartIds[2] = 0;
        Body.PartIds[3] = 0;
        Body.AccessoryCount = 0;
    }
    if (!Stream.Get(&Field_0x60, 4u)) return MarshalError;
    if (!Stream.Get(&Field_0x68, 1u)) return MarshalError;
    const unsigned int ExtraBytes = static_cast<unsigned int>(static_cast<int>(Field_0x68)) * 4u;
#if defined(REMOTECLIENT_EXPORTS)
    if (MarshalCursor + ExtraBytes > MarshalLimit) MarshalError = 0x17u;
    if (MarshalError != 0) return MarshalError;
#endif
    if (Field_0x64 != nullptr) ::operator delete(Field_0x64);
    Field_0x64 = static_cast<unsigned int*>(::operator new(ExtraBytes, std::nothrow));
#if defined(REMOTECLIENT_EXPORTS)
    if (Field_0x64 == nullptr) return 5u;
#endif
    for (int Index = 0; Index < static_cast<int>(Field_0x68); ++Index) {
        if (!Stream.Get(&Field_0x64[Index], 4u)) return MarshalError;
    }
    unsigned short LowWord = 0;
    if (!Stream.Get(&LowWord, 2u)) return MarshalError;
    Field_0x6c = LowWord;
    unsigned char Quantized = 0;
    if (!Stream.Get(&Quantized, 1u)) return MarshalError;
    RpcServerSendCommExpand(Quantized, &Field_0x70);
    return Stream.Finish(OutLength, false);
}

// --- 0x54 MISCOBJUPDATE (RS-P9) ---

// RC 06b4d0b0 / RS 06be7cd0.
FRpcServerMiscObjUpdatePacket::FRpcServerMiscObjUpdatePacket()
    : FPacketBase(0x20054u), Flags(0), Field_0x24(0), Field_0x28(0), Field_0x2c(0),
      Field_0x30(0), Field_0x34(0), Field_0x38(0), Field_0x3c(0), Field_0x50(0),
      Field_0x54(0xffffffffu), IdCount(0) {}

// RC 06b45830.
void FRpcServerMiscObjUpdatePacket::SetField0x28(unsigned int Value) {
    Flags |= 0x08u;
    Field_0x28 = Value;
}

// RC 06b45840.
void FRpcServerMiscObjUpdatePacket::SetField0x24(unsigned int Value) {
    Flags |= 0x04u;
    Field_0x24 = Value;
}

// RC 06b45850.
void FRpcServerMiscObjUpdatePacket::SetField0x54(unsigned int Value) {
    Flags |= 0x20u;
    Field_0x54 = Value;
}

// RC 06b45860.
void FRpcServerMiscObjUpdatePacket::SetField0x34(unsigned int Value) {
    Flags |= 0x01u;
    Field_0x34 = Value;
}

// RC 06b45870.
void FRpcServerMiscObjUpdatePacket::SetFields0x38(unsigned int Value0x38, unsigned int Value0x3c) {
    Flags |= 0x02u;
    Field_0x38 = Value0x38;
    Field_0x3c = Value0x3c;
}

// RC 06b45890: the flag, then VC6 basic_string<unsigned short>::assign(const&) (the whole
// source; self-assignment keeps the string).
void FRpcServerMiscObjUpdatePacket::SetText(const std::wstring& Source) {
    Flags |= 0x10u;
    Text.assign(Source);
}

// RC 06b45a40.
void FRpcServerMiscObjUpdatePacket::SetField0x30(unsigned int Value) {
    Flags |= 0x80u;
    Field_0x30 = Value;
}

// RC 06b45a50.
void FRpcServerMiscObjUpdatePacket::SetField0x2c(unsigned short Value) {
    Flags |= 0x40u;
    Field_0x2c = Value;
}

// RC 06b45a60: byte +0x21 |= 1, insert(end(), 1, Id), then the low byte of the size.
void FRpcServerMiscObjUpdatePacket::AddId(unsigned int Id) {
    Flags |= 0x100u;
    Ids.push_back(Id);
    IdCount = static_cast<unsigned char>(Ids.size());
}

// RC 06b45aa0 / RS 06bfa8e0 (identical). The text length is the low word of the string's
// length, and that many characters are copied (the reservation is made for 0 as well); the
// id loop walks the vector, not IdCount. Every failure returns 0x16 (ESI=0x16 from
// 06b45af7, re-set 06b45cb0 after the text copy; literal at the header); the text is a
// REP MOVSD/MOVSB into the cached destination (06b45c91..06b45cad).
unsigned int FRpcServerMiscObjUpdatePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Flags, 2u)) return 0x16u;
    if ((Flags & 0x08u) != 0) {
        if (!Stream.Put(&Field_0x28, 4u)) return 0x16u;
    }
    if ((Flags & 0x04u) != 0) {
        if (!Stream.Put(&Field_0x24, 4u)) return 0x16u;
    }
    if ((Flags & 0x01u) != 0) {
        if (!Stream.Put(&Field_0x34, 4u)) return 0x16u;
    }
    if ((Flags & 0x02u) != 0) {
        if (!Stream.Put(&Field_0x38, 4u)) return 0x16u;
        if (!Stream.Put(&Field_0x3c, 4u)) return 0x16u;
    }
    if ((Flags & 0x10u) != 0) {
        const unsigned short Length = static_cast<unsigned short>(Text.size());
        if (!Stream.Put(&Length, 2u)) return 0x16u;
        if (!Stream.PutBlock(Text.c_str(), static_cast<unsigned int>(Length) * 2u)) return 0x16u;
    }
    if ((Flags & 0x20u) != 0) {
        if (!Stream.Put(&Field_0x54, 4u)) return 0x16u;
    }
    if ((Flags & 0x40u) != 0) {
        if (!Stream.Put(&Field_0x2c, 2u)) return 0x16u;
    }
    if ((Flags & 0x80u) != 0) {
        if (!Stream.Put(&Field_0x30, 4u)) return 0x16u;
    }
    if ((Flags & 0x100u) != 0) {
        if (!Stream.Put(&IdCount, 1u)) return 0x16u;
        for (std::vector<unsigned int>::const_iterator It = Ids.begin(); It != Ids.end(); ++It) {
            if (!Stream.Put(&*It, 4u)) return 0x16u;
        }
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

// RC 06b45e40 / RS 06bfac80, the same order as Encode. The text: the u16 length, then a CRT
// buffer of length + 1 characters, the characters, a terminator, assign up to the first
// null (wcslen), free. The two DLLs differ there:
// - RC checks the length (characters counted as bytes, not * 2) against the limit without
//   committing it (0x17), and returns 5 when malloc fails;
// - RS does neither (a failed malloc faults on the terminator).
// When the characters overrun the buffer the block leaks (reproduced on purpose). The ids
// are appended to the vector as they are read (it is not cleared first); IdCount, re-read
// each pass, bounds the loop.
unsigned int FRpcServerMiscObjUpdatePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Flags, 2u)) return MarshalError;
    if ((Flags & 0x08u) != 0) {
        if (!Stream.Get(&Field_0x28, 4u)) return MarshalError;
    }
    if ((Flags & 0x04u) != 0) {
        if (!Stream.Get(&Field_0x24, 4u)) return MarshalError;
    }
    if ((Flags & 0x01u) != 0) {
        if (!Stream.Get(&Field_0x34, 4u)) return MarshalError;
    }
    if ((Flags & 0x02u) != 0) {
        if (!Stream.Get(&Field_0x38, 4u)) return MarshalError;
        if (!Stream.Get(&Field_0x3c, 4u)) return MarshalError;
    }
    if ((Flags & 0x10u) != 0) {
        unsigned short Length = 0;
        if (!Stream.Get(&Length, 2u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + Length > MarshalLimit) MarshalError = 0x17u;
        if (MarshalError != 0) return MarshalError;
        wchar_t* Chars = static_cast<wchar_t*>(std::malloc(static_cast<unsigned int>(Length) * 2u + 2u));
        if (Chars == nullptr) return 5u;
#else
        wchar_t* Chars = static_cast<wchar_t*>(std::malloc(static_cast<unsigned int>(Length) * 2u + 2u));
#endif
        if (!Stream.Get(Chars, static_cast<unsigned int>(Length) * 2u)) return MarshalError;
        Chars[Length] = 0;
        Text.assign(Chars, std::wcslen(Chars));
        std::free(Chars);
    }
    if ((Flags & 0x20u) != 0) {
        if (!Stream.Get(&Field_0x54, 4u)) return MarshalError;
    }
    if ((Flags & 0x40u) != 0) {
        if (!Stream.Get(&Field_0x2c, 2u)) return MarshalError;
    }
    if ((Flags & 0x80u) != 0) {
        if (!Stream.Get(&Field_0x30, 4u)) return MarshalError;
    }
    if ((Flags & 0x100u) != 0) {
        if (!Stream.Get(&IdCount, 1u)) return MarshalError;
        for (unsigned int Index = 0; Index < static_cast<unsigned int>(IdCount); ++Index) {
            unsigned int Id = 0;
            if (!Stream.Get(&Id, 4u)) return MarshalError;
            Ids.push_back(Id);
        }
    }
    return Stream.Finish(OutLength, false);
}

// --- 0x42 CREATESOLAR (RS-P9) ---

// RC 06b4c810 / RS 06be7410: the block (base vtable, +0x20..+0x9b, EH states 1..3), then
// +0xa0..+0xac = 0 and the two bytes 0; +0xb0 and +0xb4 are left.
FRpcServerCreateSolarPacket::FRpcServerCreateSolarPacket()
    : FPacketBase(0x20042u), Field_0x9c(0), bMarkedBy(0), NameASize(0), NameA(nullptr),
      NameBSize(0), NameB(nullptr) {}

// RC 06b4c920 / RS 06be7520: operator delete of a non-null NameB (+0xac), then of a
// non-null NameA (+0xa4), neither cleared; the block's members follow in reverse order.
FRpcServerCreateSolarPacket::~FRpcServerCreateSolarPacket() {
    if (NameB != nullptr) ::operator delete(NameB);
    if (NameA != nullptr) ::operator delete(NameA);
}

// RC 06b62640 / RS 06bf8820 (they differ only in the block helper and the 127.0 constant).
// The block helper's return is discarded; its sticky error stops the first byte (retail
// returns the literal 0x16 there, the only error the block can leave). The attitude byte is
// quantized after Affiliation is written and before its own reservation.
unsigned int FRpcServerCreateSolarPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // Every failure returns 0x16 (literals, and EAX=0x16 from 06b62769 into the epilogue
    // for the attitude/NameA checks); both name blocks are REP MOVSD/MOVSB into the
    // cached destination (06b627ee, 06b62884).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    RpcServerCreateObjectEncode(*this, Stream, Fields);
    if (!Stream.Put(&Field_0x9c, 1u)) return 0x16u;
    if (!Stream.Put(&bMarkedBy, 1u)) return 0x16u;
    if (!SinglePlayer()) {
        if (!Stream.Put(&Affiliation, 4u)) return 0x16u;
        const unsigned char Quantized = RpcServerQuantize127(&Attitude);
        if (!Stream.Put(&Quantized, 1u)) return 0x16u;
        if (!Stream.Put(&NameASize, 4u)) return 0x16u;
        if (!Stream.PutBlock(NameA, NameASize)) return 0x16u;
        if (!Stream.Put(&NameBSize, 4u)) return 0x16u;
        if (!Stream.PutBlock(NameB, NameBSize)) return 0x16u;
    }
    return Stream.FinishEncodeSuccess(OutLength);                      // 06b628a1
}

// RC 06b62900 / RS 06bf8ae0 (they differ only in the block helper and the 1/127 constant).
// The block helper's return is discarded. Outside single player: Affiliation, the attitude
// byte expanded (MOVSX, FILD, x 1/127, FSTP) after its pointer commit, then the two name
// blocks (RC 06b4eb30 / RS 06bec240) with their status discarded -- and the end helper's 0
// is stored over MarshalError (06b62a54), so an overrunning name block still decodes as
// success. Reproduced on purpose.
unsigned int FRpcServerCreateSolarPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    RpcServerCreateObjectDecode(*this, Stream, Fields);
    if (!Stream.Get(&Field_0x9c, 1u)) return MarshalError;
    if (!Stream.Get(&bMarkedBy, 1u)) return MarshalError;
    if (!SinglePlayer()) {
        if (!Stream.Get(&Affiliation, 4u)) return MarshalError;
        unsigned char Quantized = 0;
        if (!Stream.Get(&Quantized, 1u)) return MarshalError;
        RpcServerExpand127(Quantized, &Attitude);
        RpcWireDecodeOwnedBlock(*this, Stream, NameASize, NameA);
        RpcWireDecodeOwnedBlock(*this, Stream, NameBSize, NameB);
    }
    MarshalError = 0;
    return Stream.Finish(OutLength, false);
}

// ===== end SERVER 0x40..0x5b (RS-P3) =====

// ===== SERVER 0x01..0x1f (RS-P1) part 2 =====
// 0x03 CHARACTERINFO. RC and RS bodies are instruction-identical except the RC
// decoder (#if branches below). Addresses are RS unless marked.

// RS 06bd4ed0 / packet dtor 06bdeac1..06bdeace (06be1270 frees +0x14).
void RpcServerCharacterIdBlockFree(FRpcServerCharacterIdBlock* Block) {
    if (Block->Ids != nullptr) ::operator delete(Block->Ids);
    ::operator delete(Block);
}

FRpcServerCharacterRecord::FRpcServerCharacterRecord()
    : Field_0x200(0), Field_0x201(0), Field_0x224(0), Field_0x228(0), Field_0x22c(0),
      Field_0x230(0), Field_0x234(0), Field_0x238(0), Field_0x23c(0), Field_0x240(0),
      Field_0x244(0), Field_0x248(0), Field_0x24c(0), Field_0x250(0), Field_0x254(0),
      Field_0x258(0), Field_0x25c(0), Field_0x260(0), BlockA(nullptr), BlockB(nullptr) {
    Name.Buffer[0] = '\0';
}

// 06bdeab2..06bdeae8: +0x274, then +0x278; the members follow in reverse declaration
// order (collision list 06bdeb14, handle 06bdeb37, +0x214 06bdeb49, +0x204 06bdeb5c).
FRpcServerCharacterRecord::~FRpcServerCharacterRecord() {
    if (BlockA != nullptr) RpcServerCharacterIdBlockFree(BlockA);
    if (BlockB != nullptr) RpcServerCharacterIdBlockFree(BlockB);
}

// 06bdea60: pass 1 deletes every non-null record (06bdeaa6), pass 2 is the list's
// own node teardown.
FRpcServerCharacterInfoPacket::~FRpcServerCharacterInfoPacket() {
    for (std::list<FRpcServerCharacterRecord*>::iterator It = Records.begin();
         It != Records.end(); ++It) {
        if (*It != nullptr) delete *It;
    }
}

// RS 06be0281..06be0384 (TextA) / 06be02fb.. (TextB): the length word in characters is
// checked; the 2*length run is counted (the overflow error is recorded) but NOT tested
// before the copy, so an oversized string is written past the buffer and the next field
// fails. Reproduced on purpose. The copy is a REP MOVSD/MOVSB into the cached
// destination with Ptr=cached+2*length committed after it (06be02d9..06be02f8).
static bool RpcServerPutWideText(FRpcWireStream& Stream, const std::wstring& Text) {
    const unsigned short Length = static_cast<unsigned short>(Text.size() & 0xffffu);
    if (!Stream.Put(&Length, 2u)) return false;
    const unsigned int Bytes = static_cast<unsigned int>(Length) * 2u;
    Stream.Reserve(Bytes, 0x16u);
    Stream.PutReserved(Text.c_str(), Bytes);
    return true;
}

// RS 06be0636..06be07d3 (+0x274) and 06be07d6..06be0978 (+0x278).
static bool RpcServerPutIdBlock(FRpcWireStream& Stream, const FRpcServerCharacterIdBlock* Block) {
    const unsigned char Present = (Block != nullptr) ? 1u : 0u;
    if (!Stream.Put(&Present, 1u)) return false;
    if (Block == nullptr) return true;
    if (!Stream.Put(&Block->Field_0x04, 4u)) return false;               // 06be0690
    if (!Stream.Put(&Block->Field_0x00, 4u)) return false;               // 06be06bf
    if (!Stream.Put(&Block->Field_0x08, 4u)) return false;               // 06be06ef
    if (!Stream.Put(&Block->Field_0x0c, 4u)) return false;               // 06be071f
    if (!Stream.Put(&Block->Count, 4u)) return false;                    // 06be074f
    // Count dwords as Count*4 bytes (06be0762), checked before the copy (06be0773), then
    // a REP MOVSD/MOVSB into the cached destination (06be079d / 06be093b).
    return Stream.PutBlock(Block->Ids, static_cast<unsigned int>(Block->Count) << 2);
}

// RS 06be00e0 / RC 06b41aa0. Every failure returns literal 0x16 (06be0130, 06be015e,
// 06be0c53, 06be0c84); the name, both wide texts, both id blocks and each hardpoint run
// are REP MOVSD/MOVSB into the cached destination.
unsigned int FRpcServerCharacterInfoPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    // 06be0138: the low byte of the list size.
    const unsigned char Count = static_cast<unsigned char>(Records.size() & 0xffu);
    if (!Stream.Put(&Count, 1u)) return 0x16u;
    // 06be016c: a zero count skips the walk; otherwise the whole ring is walked.
    if (Count != 0) {
        for (std::list<FRpcServerCharacterRecord*>::const_iterator It = Records.begin();
             It != Records.end(); ++It) {
            const FRpcServerCharacterRecord* Record = *It;
            if (Record == nullptr) continue;                                 // 06be0190
            // 06be019d REPNE SCASB: an unbounded strlen, truncated to a word.
            const unsigned short NameLength =
                static_cast<unsigned short>(std::strlen(Record->Name.Buffer));
            if (!Stream.Put(&NameLength, 2u)) return 0x16u;                 // 06be01cb
            if (!Stream.PutBlock(Record->Name.Buffer, NameLength)) return 0x16u; // 06be020e
            if (!Stream.Put(&Record->Field_0x200, 1u)) return 0x16u;         // 06be0252
            if (!Stream.Put(&Record->Field_0x201, 1u)) return 0x16u;         // 06be027b
            if (!RpcServerPutWideText(Stream, Record->TextA)) return 0x16u;
            if (!RpcServerPutWideText(Stream, Record->TextB)) return 0x16u;
            if (!Stream.Put(&Record->Field_0x224, 4u)) return 0x16u;         // 06be03b7
            if (!Stream.Put(&Record->Field_0x22c, 4u)) return 0x16u;         // 06be03e4
            if (!Stream.Put(&Record->Field_0x228, 4u)) return 0x16u;         // 06be0411
            if (!Stream.Put(&Record->Field_0x230, 4u)) return 0x16u;
            if (!Stream.Put(&Record->Field_0x234, 4u)) return 0x16u;
            if (!Stream.Put(&Record->Field_0x238, 4u)) return 0x16u;
            if (!Stream.Put(&Record->Field_0x23c, 4u)) return 0x16u;
            if (!Stream.Put(&Record->Field_0x240, 4u)) return 0x16u;
            if (!Stream.Put(&Record->Field_0x244, 4u)) return 0x16u;
            if (!Stream.Put(&Record->Field_0x248, 4u)) return 0x16u;
            if (!Stream.Put(&Record->Field_0x24c, 4u)) return 0x16u;
            // 06be059d FLD / 06be05a9 FSTP: the hull status crosses the x87 stack.
            if (!Stream.PutFloat(*reinterpret_cast<const float*>(&Record->Field_0x250)))
                return 0x16u;
            if (!Stream.Put(&Record->Field_0x254, 4u)) return 0x16u;
            if (!Stream.Put(&Record->Field_0x258, 4u)) return 0x16u;
            if (!Stream.Put(&Record->Field_0x25c, 4u)) return 0x16u;         // 06be062e
            if (!RpcServerPutIdBlock(Stream, Record->BlockA)) return 0x16u;
            if (!RpcServerPutIdBlock(Stream, Record->BlockB)) return 0x16u;
            // 06be099e: the stored count byte, then the whole list (06be09b0 header
            // test only).
            if (!Stream.Put(&Record->Field_0x260, 1u)) return 0x16u;
            if (Record->Equipment.Records != nullptr) {
                for (std::list<FRpcServerEquipRecord*>::const_iterator Item =
                         Record->Equipment.Records->begin();
                     Item != Record->Equipment.Records->end(); ++Item) {
                    const FRpcServerEquipRecord* E = *Item;
                    if (!Stream.Put(&E->Count, 4u)) return 0x16u;            // 06be0a06
                    if (!Stream.PutFloat(E->Health)) return 0x16u;           // 06be0a2a FLD/FSTP
                    if (!Stream.Put(&E->ArchId, 4u)) return 0x16u;           // 06be0a5a
                    if (!Stream.Put(&E->Id, 2u)) return 0x16u;               // 06be0a85
                    if (!Stream.Put(&E->bEquipped, 1u)) return 0x16u;        // 06be0aae
                    if (!Stream.Put(&E->Field_0x0f, 1u)) return 0x16u;       // 06be0ad4
                    if (!Stream.Put(&E->HardpointSize, 2u)) return 0x16u;
                    if (!Stream.PutBlock(E->Hardpoint, E->HardpointSize))
                        return 0x16u;                                        // 06be0b3a
                }
            }
            // 06be0b6c: the collision list's size, then per node the Id word and the
            // Health float through FLD/FSTP (06be0bfc).
            const unsigned int GroupCount = static_cast<unsigned int>(Record->CollisionGroups.size());
            if (!Stream.Put(&GroupCount, 4u)) return 0x16u;
            for (std::list<CollisionGroupDesc>::const_iterator Group =
                     Record->CollisionGroups.begin();
                 Group != Record->CollisionGroups.end(); ++Group) {
                if (!Stream.Put(&Group->Id, 2u)) return 0x16u;               // 06be0bd7
                if (!Stream.PutFloat(Group->Health)) return 0x16u;           // 06be0c02
            }
        }
    }
    if (!Stream.Put(&SaveFailureCount, 4u)) return 0x16u;                    // 06be0c5c
    return Stream.FinishEncodeSuccess(OutLength);                             // 06be0c6c
}

// RS 06bdee6f..06bdef2e (and the TextB twin): the character count, append(count, ' ')
// (_Grow + the 0x20 fill 06bdfb30), then the 2*count run counted but NOT tested before
// it is copied over the string; the next field fails. Reproduced on purpose.
static bool RpcServerGetWideText(FRpcWireStream& Stream, std::wstring& Text) {
    unsigned short Length = 0;
    if (!Stream.Get(&Length, 2u)) return false;
    Text.append(static_cast<std::wstring::size_type>(Length), L' ');
    const unsigned int Bytes = static_cast<unsigned int>(Length) * 2u;
    Stream.Reserve(Bytes, 0x17u);
    Stream.GetUncounted(&Text[0], Bytes);
    return true;
}

// RS 06bdf2e6..06bdf4c1 (+0x274; +0x278 at 06bdf4c1..06bdf6c0). Returns 0 or the code
// the decoder returns. The new-expression test only skips the zero stores; a null
// block is written through, reproduced on purpose. RC 06b40d6f..06b40db9: the array
// run is checked (not committed) before the allocation, and a null array returns 5.
static unsigned int RpcServerGetIdBlock(FRpcWireStream& Stream, FPacketBase& Packet,
    FRpcServerCharacterIdBlock*& Out) {
    unsigned char Present = 0;
    if (!Stream.Get(&Present, 1u)) return Packet.MarshalError;
    if (Present == 0) return 0;                                              // 06bdf2ef
    FRpcServerCharacterIdBlock* Block = static_cast<FRpcServerCharacterIdBlock*>(
        ::operator new(sizeof(FRpcServerCharacterIdBlock), std::nothrow));   // 06bdf2f7
    if (Block != nullptr) {
        Block->Ids = nullptr;
        Block->Count = 0;
        Block->Field_0x0c = 0;
        Block->Field_0x08 = 0;
        Block->Field_0x04 = 0;
        Block->Field_0x00 = 0;
    }
    Out = Block;                                                             // 06bdf31e
    if (!Stream.Get(&Block->Field_0x04, 4u)) return Packet.MarshalError;
    if (!Stream.Get(&Block->Field_0x00, 4u)) return Packet.MarshalError;    // 06bdf38a
    if (!Stream.Get(&Block->Field_0x08, 4u)) return Packet.MarshalError;
    if (!Stream.Get(&Block->Field_0x0c, 4u)) return Packet.MarshalError;    // 06bdf3f5
    if (!Stream.Get(&Block->Count, 4u)) return Packet.MarshalError;         // 06bdf42e
    if (static_cast<unsigned int>(Block->Count) != 0) {                      // 06bdf43e JBE
        const unsigned int Bytes = static_cast<unsigned int>(Block->Count) << 2;
#if defined(REMOTECLIENT_EXPORTS)
        if (Packet.MarshalCursor + Bytes > Packet.MarshalLimit) Packet.MarshalError = 0x17u;
        if (Packet.MarshalError != 0) return Packet.MarshalError;
        Block->Ids = static_cast<unsigned int*>(::operator new(Bytes, std::nothrow));
        if (Block->Ids == nullptr) return 5u;
#else
        // 06bdf44c: allocated before the run is checked, never null-tested.
        Block->Ids = static_cast<unsigned int*>(::operator new(Bytes, std::nothrow));
#endif
        if (!Stream.Get(Block->Ids, Bytes)) return Packet.MarshalError;
    }
    return 0;
}

// RS 06bdebe0 / RC 06b40500. A failed read returns at once; the record being decoded
// is not linked yet and is leaked, as in retail (reproduced on purpose), and so is an
// equipment record that was not yet pushed.
unsigned int FRpcServerCharacterInfoPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    unsigned char Count = 0;
    if (!Stream.Get(&Count, 1u)) return MarshalError;                        // 06bdec64
    // 06bdec70..06bdfa64: a word counter against the byte count.
    for (unsigned short Index = 0; Index < static_cast<unsigned short>(Count); ++Index) {
        // 06bdec95 operator new(0x27c); null skips the whole record (06bded6e).
        FRpcServerCharacterRecord* Record = new (std::nothrow) FRpcServerCharacterRecord();
        if (Record == nullptr) continue;
        unsigned short NameLength = 0;
        if (!Stream.Get(&NameLength, 2u)) return MarshalError;               // 06bded9a
#if defined(REMOTECLIENT_EXPORTS)
        // RC 06b406c0 / 06b406c7: the length word is consumed, then bounded.
        if (NameLength >= 0x200u) return 0x4au;
#endif
        // RS: copied with the full wire length and terminated at [length], with no
        // bound (06bdedf0): reproduced on purpose.
        if (!Stream.Get(Record->Name.Buffer, NameLength)) return MarshalError;
        Record->Name.Buffer[NameLength] = '\0';
        if (!Stream.Get(&Record->Field_0x200, 1u)) return MarshalError;     // 06bdee20
        if (!Stream.Get(&Record->Field_0x201, 1u)) return MarshalError;     // 06bdee49
        if (!RpcServerGetWideText(Stream, Record->TextA)) return MarshalError;
        if (!RpcServerGetWideText(Stream, Record->TextB)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x224, 4u)) return MarshalError;     // 06bdf04a
        if (!Stream.Get(&Record->Field_0x22c, 4u)) return MarshalError;     // 06bdf077
        if (!Stream.Get(&Record->Field_0x228, 4u)) return MarshalError;     // 06bdf0a4
        if (!Stream.Get(&Record->Field_0x230, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x234, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x238, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x23c, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x240, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x244, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x248, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x24c, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x250, 4u)) return MarshalError;     // 06bdf239 MOV
        if (!Stream.Get(&Record->Field_0x254, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x258, 4u)) return MarshalError;
        if (!Stream.Get(&Record->Field_0x25c, 4u)) return MarshalError;     // 06bdf2bd
        unsigned int Error = RpcServerGetIdBlock(Stream, *this, Record->BlockA);
        if (Error != 0) return Error;
        Error = RpcServerGetIdBlock(Stream, *this, Record->BlockB);
        if (Error != 0) return Error;
        if (!Stream.Get(&Record->Field_0x260, 1u)) return MarshalError;     // 06bdf6df
        // 06bdf6e7..06bdf8f2: word counter against the stored byte, re-read each round.
        for (unsigned short Item = 0; Item < static_cast<unsigned short>(Record->Field_0x260);
             ++Item) {
            // 06bdf702 operator new(0x18); null skips this element's reads (06bdf712).
            FRpcServerEquipRecord* E = static_cast<FRpcServerEquipRecord*>(
                ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
            if (E == nullptr) continue;
            E->ArchId = 0;
            E->Count = 0;
            E->Health = 0.0f;
            E->Id = 0;
            E->bEquipped = 0;
            E->Field_0x0f = 0;
            E->HardpointSize = 0;
            E->Hardpoint = nullptr;
            if (!Stream.Get(&E->Count, 4u)) return MarshalError;             // 06bdf760
            if (!Stream.Get(&E->Health, 4u)) return MarshalError;            // 06bdf78c MOV
            if (!Stream.Get(&E->ArchId, 4u)) return MarshalError;            // 06bdf7b8
            if (!Stream.Get(&E->Id, 2u)) return MarshalError;                // 06bdf7e4
            if (!Stream.Get(&E->bEquipped, 1u)) return MarshalError;         // 06bdf80d
            if (!Stream.Get(&E->Field_0x0f, 1u)) return MarshalError;        // 06bdf835
            if (!Stream.Get(&E->HardpointSize, 2u)) return MarshalError;     // 06bdf85f
#if defined(REMOTECLIENT_EXPORTS)
            // RC 06b411fc..06b41226: the run is checked first, a null block returns 5.
            if (MarshalCursor + E->HardpointSize > MarshalLimit) MarshalError = 0x17u;
            if (MarshalError != 0) return MarshalError;
            E->Hardpoint = static_cast<char*>(::operator new(E->HardpointSize, std::nothrow));
            if (E->Hardpoint == nullptr) return 5u;
#else
            // 06bdf86d: allocated (zero bytes included) before the check, no null test.
            E->Hardpoint = static_cast<char*>(::operator new(E->HardpointSize, std::nothrow));
#endif
            if (!Stream.Get(E->Hardpoint, E->HardpointSize)) return MarshalError;
            Record->Equipment.PushBack(E);                                   // 06bdf8d5
        }
        unsigned int GroupCount = 0;
        if (!Stream.Get(&GroupCount, 4u)) return MarshalError;               // 06bdf922
        for (unsigned int Group = 0; Group < GroupCount; ++Group) {
            // 06bdf940..06bdf982: the node is linked first; its value is the stack pair
            // {count just read, residue}. The low word is then overwritten, so the pad
            // keeps the count's high half. The residue dword is always overwritten by the
            // Health read or the record is leaked on error, so 0 here is exact.
            CollisionGroupDesc Value;
            std::memcpy(&Value, &GroupCount, 4u);
            Value.Health = 0.0f;
            Record->CollisionGroups.push_back(Value);
            CollisionGroupDesc& Linked = Record->CollisionGroups.back();
            if (!Stream.Get(&Linked.Id, 2u)) return MarshalError;            // 06bdf9be
            if (!Stream.Get(&Linked.Health, 4u)) return MarshalError;        // 06bdf9fb MOV
        }
        Records.push_back(Record);                                           // 06bdfa25..06bdfa51
    }
    if (!Stream.Get(&SaveFailureCount, 4u)) return MarshalError;             // 06bdfa9e
    return Stream.Finish(OutLength, false);
}

// RS 06bdea20 / RC 06b40340.
void FRpcServerCharacterInfoPacket::Dump(std::ostream& Out) const {
    if (Records.size() != 0) {
        Out << "   Num Characters : " << static_cast<unsigned int>(Records.size()) << "\n";
    }
}

// ===== end SERVER 0x01..0x1f (RS-P1) part 2 =====
