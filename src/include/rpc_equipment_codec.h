// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "rpc_packet.h"
#include <list>

class FRpcWireStream;

// One equipment wire record, operator new(0x18) in every producer (RC 06b38210,
// 06b3a510, 06b4fb10). Member order is the record's, not the wire's.
struct FRpcServerEquipRecord {
    unsigned int ArchId;          // +0x00 EquipDesc +0x04
    int Count;                    // +0x04 EquipDesc +0x14
    float Health;                 // +0x08 EquipDesc +0x10
    unsigned short Id;            // +0x0c EquipDesc +0x02
    unsigned char bEquipped;      // +0x0e EquipDesc +0x0c, raw byte
    unsigned char Field_0x0f;     // +0x0f TODO: no builder writes it (zeroed); still on the wire
    unsigned short HardpointSize; // +0x10 strlen + 1; 0 for the internal cargo bay
    char* Hardpoint;              // +0x14 operator new(HardpointSize)
};

// The 4-byte object retail allocates with operator new(4): it owns a list of records
// that is created on the first push_back (RC 06b3a820). Retail's list is VC6
// std::list<record*>; the port keeps its own std::list of the same record pointers.
// The destructor is RC 06b3a450 / RS 06bdc790 (every EH funclet runs it on a stack
// handle): free each record's hardpoint, then the record, then the list.
class FRpcServerEquipHandle {
public:
    FRpcServerEquipHandle() : Records(nullptr) {}
    ~FRpcServerEquipHandle();
    FRpcServerEquipHandle(const FRpcServerEquipHandle&) = delete;
    FRpcServerEquipHandle& operator=(const FRpcServerEquipHandle&) = delete;
    // RC 06b3a820: create the list if absent (allocation failure drops the record),
    // then append.
    void PushBack(FRpcServerEquipRecord* Record);
    // RC 06b3a510 / RS 06bdc850: append a deep copy of every record of Source.
    void AppendCopy(const FRpcServerEquipHandle& Source);
    std::list<FRpcServerEquipRecord*>* Records;
};

// Common base of the equipment-list packets: vtable RC 06b6e0f4, dtor RC 06b4f860.
// +0x20 holds a pointer to the 4-byte handle, null until something is assigned.
class FRpcServerEquipListPacketBase : public FPacketBase {
public:
    explicit FRpcServerEquipListPacketBase(unsigned int PacketTypeId)
        : FPacketBase(PacketTypeId), Equipment(nullptr) {}
    ~FRpcServerEquipListPacketBase() override;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    // RC 06b3c510 (0x24) / 06b3c770 (0x25): allocate the handle if absent, then
    // append a deep copy of Source.
    void AssignEquipment(const FRpcServerEquipHandle& Source);
    // RS 06bdc770 (0x24) / 06bdce60 (0x25): deep-copy the handle, if any, into Dest.
    void ExtractEquipment(FRpcServerEquipHandle* Dest) const;
    FRpcServerEquipHandle* Equipment; // +0x20
protected:
    // RC 06b4f8d0 / RS 06becfb0 and RC 06b4fb10 / RS 06bed1f0 (thiscall on the
    // packet, running on its marshal scratch).
    unsigned int EncodeEquipment(FRpcWireStream& Stream);
    unsigned int DecodeEquipment(FRpcWireStream& Stream);
};
