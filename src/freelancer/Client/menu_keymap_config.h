// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <list>
#include <string>
#include <vector>

// KeyMap.ini / accelerator groups — retail Client/Interface/KeyMap.cpp
// (FUN_00576bb0 / FUN_00576f40 / FUN_00576b60 / FUN_00577490 / FUN_00577850 walk).

enum EKeyCmdState : unsigned
{
    kKeyCmdStateKeyDown = 1,
    kKeyCmdStateKeyUp = 2,
    kKeyCmdStateRepeat = 4,
    kKeyCmdStateShiftIgnore = 8,
};

enum EKeyModifier : unsigned
{
    kKeyModNone = 0,
    kKeyModShift = 1,
    kKeyModControl = 4,
    kKeyModAlt = 0x10,
};

struct FKeyBinding
{
    int VirtualKey;
    unsigned Modifiers;
};

// One [KeyCmd]: the 0x20-byte payload of a DAT_0067c258 list node (node +8). New entries are
// copies of the lazily built template DAT_0067c230 (FUN_00576bb0 0x00576cb6..0x00576cf6),
// whose only non-zero field is StateMask = 1.
struct FKeyCmd
{
    unsigned NicknameId;               // +0x00 index into PTR_s_USER_NONE_00614dd8
    unsigned IdsName;                  // +0x04 ids_name
    unsigned IdsInfo;                  // +0x08 ids_info
    unsigned StateMask;                // +0x0c EKeyCmdState bits
    std::vector<FKeyBinding> Bindings; // +0x10 (allocator byte, then first / last / end)
};

// One [KeyMap] group: the 0x1c-byte payload of a DAT_0067c228 list node (node +8) -- a
// std::string nickname (+0x00) and a std::list of KeyCmd pointers (+0x10 allocator, +0x14
// head, +0x18 size).
struct FKeyMapGroup
{
    std::string Nickname;
    std::list<FKeyCmd*> Commands;
};

// FUN_00576bb0 — load interface/KeyMap.ini (GetFileSysPath). Returns group count (>0 = ok).
int LoadKeyMapIni(char const* RelPath, bool bFatal);

// FUN_00576f40 — load UserKeyMap.ini override (clears existing bindings, re-applies KeyCmd).
int LoadUserKeyMapIni(char const* FullPath);

// FUN_005772d0 — write live KeyCmd bindings to UserKeyMap.ini (options Controls OK/Apply).
int SaveUserKeyMapIni(char const* FullPath);

// FUN_00575d60 — returns the relative path string "interface/KeyMap.ini".
char const* GetKeyMapIniRelPath();

// FUN_00575d70 — ensure DAT_0067c2a0 holds "<user data>/UserKeyMap.ini"; return that buffer.
char const* EnsureUserKeyMapPath();

// FUN_00577ef0 — resolve USER_* nickname string → KeyCmd* (nullptr if unknown / unloaded).
FKeyCmd* FindKeyCmdByNickname(char const* Nickname);

// FUN_00578220 — update / erase / insert one binding on the KeyCmd with NicknameId.
// NewVirtualKey==0 erases the (OldVirtualKey, OldModifiers) pair; otherwise replaces or appends.
bool UpdateKeyCmdBinding(unsigned NicknameId, int NewVirtualKey, unsigned NewModifiers,
                         int OldVirtualKey, unsigned OldModifiers);

// FUN_00576b60 — resolve accelerator group nickname to group pointer (nullptr if missing).
FKeyMapGroup* FindKeyMapGroup(char const* GroupNickname);

// FUN_00577490 — first call loads KeyMap+UserKeyMap; always activates the named group.
bool RegisterAcceleratorGroup(char const* GroupNickname);

// FUN_00577590 — remove named group from the active list.
void UnregisterAcceleratorGroup(char const* GroupNickname);

// FUN_00577850 — route one pumped message: WM_KEYDOWN/KEYUP/SYSKEY*/CHAR fill the event record
// DAT_0067c278 (HudCommandRecord), go to the handler tree as kind 1 (key) or 2 (char), then
// through the held-key list DAT_0067c260 and the active accelerator groups (FUN_00576550 gate,
// FUN_00576410 per KeyCmd). True = consumed (skip TranslateMessage / DispatchMessage).
bool KeyMapRouteInputMessage(unsigned Message, unsigned Wparam, unsigned Lparam);

struct FHudCmdRecord;
// FUN_00577680 (cdecl) — route one queued mouse event (FUN_0041ef20's 0x1c-byte event head):
// copy it into DAT_0067c278, clear +0x0c/+0x10/+0x14, capture Shift/Ctrl/Alt, dispatch the
// event kind (key -1..-8 -> 6..0xd, -9 wheel -> 4, else 0) in mode 2, then the active
// accelerator groups' bindings on that key (mode 2 as well). True = consumed.
bool KeyMapRouteMouseEvent(FHudCmdRecord const* Event);

unsigned KeyMapGetActionNicknameId(char const* Nickname);
char const* KeyMapGetActionNickname(unsigned NicknameId);
unsigned KeyMapGetActiveGroupCount();

// Out-blob filled by FUN_00577dd0 / FUN_00577d60 (NicknameId + first binding).
struct FKeyBindingLookup
{
    unsigned NicknameId; // +0x00
    int VirtualKey;      // +0x04
    unsigned Modifiers;  // +0x08
};

// FUN_00577e10 (cdecl) -- over every KeyCmd (DAT_0067c258) with NicknameId: Out->NicknameId =
// NicknameId, and the first whose binding list has BindingIndex fills VK / modifiers (true).
bool KeyMapFindCommandBinding(unsigned NicknameId, FKeyBindingLookup* Out, unsigned BindingIndex);
// FUN_00577e80 / FUN_00577ea0 -- BindingIndex 0 / 1. Callers: NN_Preferences::AddKeyRow.
bool KeyMapFindPrimaryBinding(unsigned NicknameId, FKeyBindingLookup* Out);
bool KeyMapFindAltBinding(unsigned NicknameId, FKeyBindingLookup* Out);
// FUN_00577ec0 (cdecl) -- the first KeyCmd with NicknameId, or null.
struct FKeyCmd* FindKeyCmdById(unsigned NicknameId);
// FUN_00575dc0 (cdecl) -- GetKeyState: Shift 1 | Ctrl 4 | Alt 0x10.
unsigned KeyMapGetModifierState();

// FUN_00577dd0 — walk active accelerator groups (DAT_0067c21c); first binding of NicknameId.
bool KeyMapFindActiveBinding(unsigned NicknameId, FKeyBindingLookup* Out);

// FUN_005784a0 — is any binding of command NicknameId (list DAT_0067c258) held right now:
// its modifiers all down and its key down (GetKeyState), or for a mouse key -1/-2/-3 the
// matching bit of DAT_00616850.
bool KeyMapIsCommandHeld(unsigned NicknameId);

// FUN_00577f50 — format VirtualKey+Modifiers into Dest (GetKeyNameText / IDS fallbacks).
void KeyMapFormatBindingName(FKeyBindingLookup const* Binding, wchar_t* Dest, unsigned DestChars);
