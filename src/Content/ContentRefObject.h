// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The ref-counted base shared by Content.dll's reference-held objects (vtable 06fb43f8:
// FUN_06eacd40 AddRef, FUN_06eacd50 Release, LAB_06eacd20 scalar deleting dtor). Its dtor
// body FUN_06ea8cc0 only resets the vptr to 06fb43f8; the shared scalar deleting dtor
// FUN_06f06eb0 (base dtor, then free when bit 0 is set) is slot 2 of about twenty derived
// vtables (FBaseFixture 06fb4498, FContentPlayerStateBase 06fb4640, the bar offers, the
// guide descriptions, the ObjList directives, ...). Content.dll carries no RTTI (the dword
// before each vtable is the previous vtable's last slot), so the retail class name is
// unknown; the port name is ours.
class FContentRefObject
{
public:
    FContentRefObject() = default;
    // Slot 0, FUN_06eacd40 (06eacd40-06eacd47): ++count, returns it.
    virtual unsigned int AddRef();
    // Slot 1, FUN_06eacd50 (06eacd50-06eacd66): --count; at 0 vtbl[+8](1) (delete this);
    // returns the count.
    virtual unsigned int Release();
    // Slot 2 (FUN_06ea8cc0 body; LAB_06eacd20 / FUN_06f06eb0 deleting forms).
    virtual ~FContentRefObject() = default;

    unsigned int ReferenceCount = 1;        // +0x04 1 after new
};
