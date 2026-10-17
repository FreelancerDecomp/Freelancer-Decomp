// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail NN_Scanner (RTTI .?AVNN_Scanner@@ : NN_Inventory, Event::Handler) -- the cargo scan
// window the target panel opens (HUD_Target FUN_004e54c0 -> FUN_004b6b60): an NN_Inventory in
// commodity mode that lists the scanned ship's cargo instead of the player's, with the player
// cash, jettison button, ship-view toggle, cargo label and full cargo bar hidden.
//
// Layout (size 0x984, WINFACTORY Create FUN_004b7270 = operator_new(0x984)):
//   +0x000 NN_Inventory (0x974)
//   +0x974 Event::Handler face (vftable 0x005d5604)
//   +0x978 EquipDescList -- the scanned cargo (VC6 list: allocator byte, +0x97c head,
//          +0x980 size)
//
// vftable 0x005d561c (NN_Inventory's 47 slots) overrides:
//    0 FUN_004b6c60 scalar deleting dtor (dtor FUN_004b6c80)
//    9 FUN_004b6d40 Create      11 LAB_004b6e30 Update      23 LAB_004b7180 Slot23
//   42 LAB_004b7050 SetProperty 46 FUN_004b6c50 Slot46 (`MOV AL,1 / RET`)
// Event::Handler vftable 0x005d5604: slot 0 FUN_004b71c0 OnEvent.
//
// WINFACTORY<NN_Scanner> "NN_Scanner": DAT_00672294, static init LAB_004b6b30 (ctor
// FUN_004b7200, atexit LAB_004b6b50 -> FUN_00579380), vftable 0x005d56dc (Create FUN_004b7270:
// the NN_Inventory ctor FUN_00477cf0 inlined, the handler vptr, the empty list, then slot 9).

#include "event_handler.h"
#include "nn_inventory.h"

#include "common_goods.h"

class NnScanner : public NnInventory, public EventHandler
{
public:
    // Inlined in FUN_004b7270 (0x004b72af..0x004b7326): FUN_00477cf0, the handler vptr, an
    // empty +0x978 list (the inlined clear walks no node), the NN_Scanner vptrs.
    NnScanner();
    // FUN_004b6c80 -- the +0x978 list, the handler (FUN_00576010), then FUN_00477da0.
    ~NnScanner() override;

    // slot 9 FUN_004b6d40 (RET 8) -- NN_Inventory's Create, then (on success) bind the
    // controls, the title ids 0xbcb, hide the controls a scan does not use and switch to the
    // commodity filter; RegisterEvent(0xf) either way.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_004b6e30 -- close when the scan can no longer stand, otherwise the child walk
    // and the rows of the scanned cargo.
    void Update() override;
    // slot 23 LAB_004b7180 (RET 0xc) -- 0xef02: UI sound 0xe and FUN_0047ad20; else NN_Inventory.
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 LAB_004b7050 (RET 0xc) -- 0x26 on the close button, 0x49 (A = the cargo list,
    // B = the scanned ship's archetype), 0x4a (show); then NN_Inventory's SetProperty.
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;
    // slot 46 FUN_004b6c50 -- true (read by NN_Inventory at 0x0047f6bf).
    bool Slot46() override;

    // Event::Handler slot 0 FUN_004b71c0 (RET 4) -- command 0xf closes the window.
    bool OnEvent(unsigned MsgId) override;

    EquipDescList ScannedCargo;  // +0x978

private:
    NnScanner(NnScanner const&) = delete;
    NnScanner& operator=(NnScanner const&) = delete;
};

// FUN_004b6b60 (cdecl) -- open the NN_Scanner window on Cargo, the cargo of the ship whose
// archetype is ShipArchId. Caller: HUD_Target FUN_004e54c0 (0x004e55b1).
void OpenNnScanner(EquipDescList const* Cargo, unsigned ShipArchId);
