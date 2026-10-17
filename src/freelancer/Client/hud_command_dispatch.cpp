// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "hud_command_dispatch.h"

#include "event_handler.h"

#include "client_gameobj.h"
#include "space_camera.h"
#include "space_camera_game.h"

#include <algorithm>
#include <list>
#include <string.h>
#include <vector>
#include <windows.h>

namespace
{
    // DAT_0067c278 — the seven-dword event record. DAT_0067c280 is its +0x08 (Modifiers) and
    // DAT_0067c290 its +0x18 byte; they are fields of this one record, not separate globals.
    FHudCmdRecord G_HudCmdRecord = {};

    // DAT_0067c3ac — optional filter when Mode==1 (only matching handler ptr). Set by
    // FUN_00577b40 (HudPushInputFocus), restored by FUN_00577c30 (HudPopInputFocus).
    FHudCmdHandler* G_HudCmdMode1Filter = nullptr;

    // DAT_0067c3a8 — the filters pushed aside by FUN_00577b40: a 0xc-byte std::list object
    // (allocator byte, head, size) allocated on the first push and freed by FUN_00577c30 as soon
    // as it runs empty.
    std::list<FHudCmdHandler*>* G_HudCmdFocusStack = nullptr;

    // DAT_0067c26c (allocator byte) / DAT_0067c270 (sentinel) / DAT_0067c274 (count) — the
    // cancel stack: handlers that FUN_00576360 pushed to receive USER_CANCEL (0xf) in mode 1.
    // Retail builds the list in the static initializer LAB_00576250 and frees it in the atexit
    // LAB_00576290, which also zeroes the count, so a handler destroyed later at exit finds
    // FUN_005762f0's count test at 0 and touches nothing. The port keeps that shape: the node
    // list lives behind a pointer and FHudCmdCancelStackLifetime plays both thunks.
    std::list<FHudCmdHandler*>* G_HudCmdCancelNodes = nullptr;
    int G_HudCmdCancelCount = 0;

    struct FHudCmdCancelStackLifetime
    {
        FHudCmdCancelStackLifetime()
        {
            // LAB_00576250: empty sentinel into DAT_0067c270, DAT_0067c274 = 0.
            G_HudCmdCancelNodes = new std::list<FHudCmdHandler*>();
            G_HudCmdCancelCount = 0;
        }
        ~FHudCmdCancelStackLifetime()
        {
            // LAB_00576290: free every node and the sentinel, zero both globals.
            delete G_HudCmdCancelNodes;
            G_HudCmdCancelNodes = nullptr;
            G_HudCmdCancelCount = 0;
        }
    };
    FHudCmdCancelStackLifetime G_HudCmdCancelStackLifetime;

    // DAT_0067c3a4 — multimap MsgId → Handler* (retail VC6 _Tree with the _Multi byte set at
    // +0xc by FUN_00575e00; port: vector kept sorted by MsgId, equal ids in insertion order).
    struct FHudCmdRegEntry
    {
        unsigned MsgId;
        FHudCmdHandler* Handler;
    };

    // Retail's DAT_0067c3a4 is a pointer to a tree allocated on the first insert (FUN_00575e00),
    // so handlers can register from static constructors in any translation unit (the Win3D input
    // handler does, from LAB_0059d060). The port keeps that shape: a pointer, zero before any
    // dynamic initializer runs, allocated on first use, and freed again (pointer zeroed) when the
    // last registration goes (FUN_00576190 / FUN_00576010 tail).
    std::vector<FHudCmdRegEntry>* G_HudCmdRegistryPtr = nullptr;

    void EnsureHudCmdRegistry()
    {
        // FUN_00575e00 00575e19..00575e9f: lazy header alloc.
        if (G_HudCmdRegistryPtr == nullptr)
            G_HudCmdRegistryPtr = new std::vector<FHudCmdRegEntry>();
    }

    // FUN_00576190 body, also inlined as the middle of FUN_00576010: erase every node whose
    // +0x10 is Handler, then free the tree when it is empty.
    void EraseAllRegistrations(FHudCmdHandler* Handler)
    {
        // 00576194..0057619c: no tree, nothing to do.
        if (G_HudCmdRegistryPtr == nullptr)
            return;
        std::vector<FHudCmdRegEntry>& Registry = *G_HudCmdRegistryPtr;
        std::vector<FHudCmdRegEntry>::iterator It = Registry.begin();
        while (It != Registry.end())
        {
            if (It->Handler == Handler)
                It = Registry.erase(It);
            else
                ++It;
        }
        // 0057620d..00576228: size (+0x10) zero -> FUN_00575f50 + free, DAT_0067c3a4 = 0.
        if (Registry.empty())
        {
            delete G_HudCmdRegistryPtr;
            G_HudCmdRegistryPtr = nullptr;
        }
    }
}

void HudRegisterCommandHandler(FHudCmdHandler* Handler, unsigned MsgId)
{
    // FUN_005760d0 (thiscall on the handler, RET 4) → FUN_00575e00(handler, MsgId): multimap
    // insert. The descent at 00575ec7 goes left only when MsgId < node key (SETL), so an equal
    // key goes right -- the new entry lands after every existing entry with the same id. The
    // _Multi byte (+0xc, set to 1 at 00575e4e) skips the duplicate check: the same handler may
    // be registered twice for one id.
    EnsureHudCmdRegistry();

    FHudCmdRegEntry Entry;
    Entry.MsgId = MsgId;
    Entry.Handler = Handler;

    std::vector<FHudCmdRegEntry>& Registry = *G_HudCmdRegistryPtr;
    std::vector<FHudCmdRegEntry>::iterator It = Registry.begin();
    while (It != Registry.end() && static_cast<int>(It->MsgId) <= static_cast<int>(MsgId))
        ++It;
    Registry.insert(It, Entry);
}

void HudUnregisterCommand(FHudCmdHandler* Handler, unsigned MsgId)
{
    // FUN_005760f0 (thiscall on the handler, RET 4) — listing 005760f0..00576189: walk
    // equal_range(MsgId) (FUN_00578950 / FUN_00578920) and erase (FUN_0046c8c0) the nodes whose
    // +0x10 is this handler. The tree is not freed here even when it empties.
    if (G_HudCmdRegistryPtr == nullptr)
        return;
    std::vector<FHudCmdRegEntry>& Registry = *G_HudCmdRegistryPtr;
    std::vector<FHudCmdRegEntry>::iterator It = Registry.begin();
    while (It != Registry.end())
    {
        if (It->MsgId == MsgId && It->Handler == Handler)
            It = Registry.erase(It);
        else
            ++It;
    }
}

void HudUnregisterCommandHandler(FHudCmdHandler* Handler)
{
    // FUN_00576190 (thiscall on the handler) — listing 00576190..00576234.
    EraseAllRegistrations(Handler);
}

void HudRemoveCancelHandler(FHudCmdHandler* Handler, int Skip)
{
    // FUN_005762f0 (thiscall on the handler, RET 4) — listing 005762f0..00576350.
    // 005762f0..00576306: empty count or empty list -> nothing.
    if (G_HudCmdCancelCount == 0)
        return;
    if (G_HudCmdCancelNodes->empty())
        return;
    // 00576310..0057634b: skip the first Skip nodes holding Handler, unlink every later one.
    std::list<FHudCmdHandler*>::iterator It = G_HudCmdCancelNodes->begin();
    while (It != G_HudCmdCancelNodes->end())
    {
        if (*It == Handler)
        {
            if (Skip != 0)
            {
                Skip = Skip - 1;
                ++It;
                continue;
            }
            It = G_HudCmdCancelNodes->erase(It);
            G_HudCmdCancelCount = G_HudCmdCancelCount - 1;
            continue;
        }
        ++It;
    }
}

void HudPushCancelHandler(FHudCmdHandler* Handler, int Skip)
{
    // FUN_00576360 (thiscall on the handler, RET 4) — listing 00576360..005763ac:
    // FUN_005762f0(Skip), then insert Handler before the sentinel (push_back) and count it.
    HudRemoveCancelHandler(Handler, Skip);
    G_HudCmdCancelNodes->push_back(Handler);
    G_HudCmdCancelCount = G_HudCmdCancelCount + 1;
}

void HudDestroyEventHandler(FHudCmdHandler* Handler)
{
    // FUN_00576010 — the Event::Handler base destructor (vptr reset to 005cbed8 at 00576016):
    // FUN_00577c30(this); the FUN_00576190 erase + free inlined (00576021..005760b8);
    // FUN_005762f0(this, 0).
    HudPopInputFocus(Handler);
    EraseAllRegistrations(Handler);
    HudRemoveCancelHandler(Handler, 0);
}

int HudDispatchCommand(unsigned MsgId, int Mode)
{
    // FUN_00576410 — listing 00576410..0057653c.
    // 00576418..00576420: no tree -> false, before the record is touched.
    if (G_HudCmdRegistryPtr == nullptr)
        return 0;
    std::vector<FHudCmdRegEntry>& Registry = *G_HudCmdRegistryPtr;

    G_HudCmdRecord.MsgId = MsgId;

    // 0057643d..005764b7: equal_range on MsgId (FUN_00578950 / FUN_00578920), Mode==1 filter
    // via DAT_0067c3ac. The handlers go into the 0x80-byte frame buffer (SUB ESP,0x80 -> 32
    // slots) with no bound check (00576476 `MOV [ESP+EDX*4+0xc],ECX; INC EDX`): retail never
    // registers more than 32 handlers for one id, so the port writes unchecked as well.
    FHudCmdHandler* Collected[32];
    int Count = 0;
    for (size_t Index = 0; Index < Registry.size(); ++Index)
    {
        FHudCmdRegEntry const& Entry = Registry[Index];
        if (Entry.MsgId != MsgId)
            continue;
        if (Mode == 1 && G_HudCmdMode1Filter != nullptr &&
            Entry.Handler != G_HudCmdMode1Filter)
            continue;
        Collected[Count] = Entry.Handler;
        Count = Count + 1;
    }

    // 005764b9..005764d6: call in reverse order — handler vt[0](MsgId); non-zero consumes.
    for (int Index = Count - 1; Index >= 0; --Index)
    {
        FHudCmdHandler* Handler = Collected[Index];
        bool const Handled = Handler->OnEvent(MsgId);
        if (Handled != 0)
            return 1;
    }

    // 005764d8..005764f7: mode 1, USER_CANCEL (0xf), a key-down event and a non-empty cancel
    // stack (DAT_0067c274 > 0).
    if (Mode != 1 || MsgId != 0xfu)
        return 0;
    if (G_HudCmdRecord.Byte18 == 0)
        return 0;
    if (G_HudCmdCancelCount <= 0)
        return 0;

    // 005764f9..0057652f: unlink the last pushed handler (sentinel->prev), count it down and
    // call its slot 0 with 0x10; the answer is ignored and the event counts as consumed.
    FHudCmdHandler* const Cancelled = G_HudCmdCancelNodes->back();
    G_HudCmdCancelNodes->pop_back();
    G_HudCmdCancelCount = G_HudCmdCancelCount - 1;
    Cancelled->OnEvent(0x10u);
    return 1;
}

int HudPostCommand(unsigned MsgId, int bSkipMods)
{
    // FUN_005763b0 — listing 005763b0..0057640a → JMP FUN_00576410(msgId, 0).
    if (bSkipMods == 0)
    {
        memset(&G_HudCmdRecord, 0, sizeof(G_HudCmdRecord));

        unsigned Mods = 0;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
            Mods = Mods | 1u;
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
            Mods = Mods | 4u;
        if ((GetKeyState(VK_MENU) & 0x8000) != 0)
            Mods = Mods | 0x10u;

        G_HudCmdRecord.Modifiers = Mods;
        G_HudCmdRecord.Byte18 = 1;
    }

    // 0x0057640a: JMP FUN_00576410 -- its EAX is this function's result.
    return HudDispatchCommand(MsgId, 0);
}

void HudPushInputFocus(FHudCmdHandler* Handler)
{
    // FUN_00577b40 (cdecl, 1 arg) — listing 00577b40..00577c27.
    if (G_HudCmdMode1Filter != nullptr)
    {
        // 00577b63..00577bb8: allocate the stack object on first use.
        if (G_HudCmdFocusStack == nullptr)
            G_HudCmdFocusStack = new std::list<FHudCmdHandler*>();
        // 00577bbd..00577bf1: insert the current filter before the sentinel (push_back).
        G_HudCmdFocusStack->push_back(G_HudCmdMode1Filter);
    }
    // 00577bfa / 00577c13.
    G_HudCmdMode1Filter = Handler;
}

void HudPopInputFocus(FHudCmdHandler* Handler)
{
    // FUN_00577c30 (cdecl, 1 arg) — listing 00577c30..00577d4b.
    if (G_HudCmdMode1Filter == Handler)
    {
        // 00577c3f..00577c52: nothing stacked -> filter cleared.
        if (G_HudCmdFocusStack == nullptr)
        {
            G_HudCmdMode1Filter = nullptr;
            return;
        }
        // 00577c53..00577c6d: the last pushed filter comes back and its node is erased
        // (FUN_00516f70).
        G_HudCmdMode1Filter = G_HudCmdFocusStack->back();
        G_HudCmdFocusStack->pop_back();
    }
    else
    {
        // 00577c74..00577cb9: not the active filter -- unlink the first stacked node holding
        // it, if any.
        if (G_HudCmdFocusStack == nullptr)
            return;
        std::list<FHudCmdHandler*>::iterator It = G_HudCmdFocusStack->begin();
        for (; It != G_HudCmdFocusStack->end(); ++It)
        {
            if (*It == Handler)
            {
                G_HudCmdFocusStack->erase(It);
                break;
            }
        }
    }

    // 00577cbc..00577d3e: an empty stack is freed (nodes, sentinel, object) and the pointer
    // cleared.
    if (G_HudCmdFocusStack != nullptr && G_HudCmdFocusStack->empty())
    {
        delete G_HudCmdFocusStack;
        G_HudCmdFocusStack = nullptr;
    }
}

FHudCmdHandler* HudGetInputFocus()
{
    // FUN_00577d50: MOV EAX, [DAT_0067c3ac].
    return G_HudCmdMode1Filter;
}

FHudCmdRecord* HudCommandRecord()
{
    // FUN_00576240: MOV EAX, &DAT_0067c278.
    return &G_HudCmdRecord;
}

bool SpaceSendCommand(FSpaceCommand* Command)
{
    // FUN_0054bcb0.
    // 0x0054bcb0: DAT_00678ba0, the "controller exists" byte.
    if (!IsSpaceCameraControllerReady())
        return false;

    // Slots 0x70 / 0x74 of the camera table. Retail calls them __thiscall with one stack
    // argument; the port's camera tables are explicit-this throughout (space_camera_game.h,
    // GameCamSlotRefuseCommand), so they are called that way here.

    // DAT_00679738 is re-read before every use, as retail does.
    FSpaceCamera* Camera = GetActiveSpaceCamera();
    if (Camera != nullptr)
    {
        // 0x0054bcc8: slot 0x70; the result is ignored, but a record whose id it cleared to 0 is
        // consumed.
        ((FGameCamera*)Camera)->HandleCommand70(Command);
        if (Command->Id == 0)
            return true;

        // 0x0054bcd7: slot 0x74; non-zero consumes the record.
        Camera = GetActiveSpaceCamera();
        if (((FGameCamera*)Camera)->HandleCommand74(Command) != 0)
            return true;

        Camera = GetActiveSpaceCamera();
    }

    // 0x0054bcf1: ids up to 0xe stop here, unconsumed.
    if (Command->Id <= 0xe)
        return false;

    // 0x0054bcf6 .. 0x0054bd14: the camera's watched object (GameCamera +0xc0, a Watchable
    // eight bytes into the client object); one whose +0x1c ManagerKind is 1 gets the record
    // through its slot 115 (+0x1cc; the ship's override is FUN_00544d70 over EquippedObj's
    // FUN_00534460), result ignored. The record is the same 0x28-byte command both headers
    // describe (FSpaceCommand here, FCliEquipFirePacket on the object side).
    if (Camera != nullptr)
    {
        FGameCamera* const GameCamera = reinterpret_cast<FGameCamera*>(Camera);
        unsigned char* const Held = const_cast<unsigned char*>(
            reinterpret_cast<unsigned char const*>(GameCamera->Watched.target()));
        if (Held != nullptr)
        {
            FClientGameobj* const Object = reinterpret_cast<FClientGameobj*>(Held - 8);
            // ManagerKind 1 is the ship bucket: the object is an FClientShipGameobj, whose
            // slot 115 is FClientEquippedObj::Slot1cc (overridden by the ship).
            if (Object != nullptr && Object->ManagerKind == 1)
                static_cast<FClientShipGameobj*>(Object)->Slot1cc(
                    reinterpret_cast<FCliEquipFirePacket const*>(Command));
        }
    }

    // 0x0054bd1a: every path past the id test answers true.
    return true;
}
