// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// ChatTextInputDisplay -- the chat input line "CTID" (RTTI .?AVChatTextInputDisplay@@ :
// RichTextEditWin). Retail `NN_Chat.cpp`.
//
// Retail:
//   WINFACTORY<ChatTextInputDisplay> DAT_0066d920, static init LAB_0045e270; Create
//   FUN_0046be00: operator_new(0x5a8), ctor FUN_0046be40; vftable 0x005cfec4 (53 slots).
//   Overrides: 0 FUN_0046c620 (scalar deleting, dtor FUN_0046c640), 9 FUN_0045eee0 Create,
//   11 FUN_0045f5e0 Update, 13 FUN_0045f4f0 Draw, 49 FUN_0045f700 OnKeyDown,
//   51 LAB_0045f6a0 OnActivated (RegisterEvent(0x14)), 52 LAB_0045f6b0 OnDeactivating
//   (UnregisterEvent(0x14)); the Event::Handler face's OnEvent is FUN_0045f6c0.
//
// Layout past RichTextEditWin (retail offsets, kept for reference only -- the port's
// RichTextEditWin holds current-STL containers):
//   +0x530 FChatChannelTable* Table        +0x534 int TableGeneration
//   +0x538 bDrawBackground                 +0x539 bCloseAfterSend
//   +0x53c FChatBackground (vftable 0x005cef80; dtor FUN_0045e3c0 / scalar FUN_0045e3e0)
//   +0x564 BinaryRDLWriter                 +0x568 CHAT_ID Target
//   +0x56c / +0x578 / +0x584 / +0x590 / +0x59c the target prefix RDLs (IDS 0x6003b "system",
//          0x6003a "local", 0x60039 "group", 0x60038 "console", 0x6003c "universe").

#include "rich_text_win.h"

#include "common_server_types.h"
#include "common_text.h"

#include <string>

class FChatChannelTable;

// vftable 0x005cef80 (slot 0 FUN_0045e3c0, scalar deleting around the dtor FUN_0045e3e0) -- the
// textured background quad behind the input line. Only ever embedded, never deleted through the
// vtable here, so the port holds it without one.
struct FChatBackground
{
    FChatBackground();
    // FUN_0045e3e0: with a loaded texture, TextureLib slot 14 (+0x38)(TextureId) and
    // FUN_0041c830(LibraryHandle); bLoaded = 0; the name string destroyed.
    ~FChatBackground();

    // FUN_0045e470 (RET 0xc) -- draw the tiled quad at Orientation (Win3D +0x3c) with the
    // control's normalised width and height; false (no draw) with a zero tile size or no
    // texture. The one caller (0x0045f5c7) ignores the result.
    bool Draw(float const* Orientation, float Width, float Height);

    std::string Name;               // +0x04 (+0x540) -- "ChatBackground"
    unsigned char bLoaded;          // +0x15 (+0x551)
    int LibraryHandle;              // +0x18 (+0x554) -- -1 until loaded
    void* TextureId;                // +0x1c (+0x558) -- TextureLib slot 12 out (texture record)
    float TileWidth;                // +0x20 (+0x55c) -- 0.08f (0x3da3d70a)
    float TileHeight;               // +0x24 (+0x560) -- 0.0533f (0x3d5a740d)

private:
    FChatBackground(FChatBackground const&) = delete;
    FChatBackground& operator=(FChatBackground const&) = delete;
};

class ChatTextInputDisplay : public RichTextEditWin
{
public:
    // FUN_0046be40
    ChatTextInputDisplay();
    // FUN_0046c640
    ~ChatTextInputDisplay() override;

    // slot 9 FUN_0045eee0 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 FUN_0045f5e0
    void Update() override;
    // slot 13 FUN_0045f4f0
    void Draw() override;
    // slot 49 FUN_0045f700 (RET 4)
    bool OnKeyDown(FHudCmdRecord const* Record) override;
    // slot 51 LAB_0045f6a0 / slot 52 LAB_0045f6b0
    void OnActivated() override;
    void OnDeactivating() override;
    // Event::Handler face FUN_0045f6c0 (RET 4)
    bool OnEvent(unsigned MsgId) override;

    // FUN_0045eaf0 (RET 4) -- the prefix, fixed style and frame colour for Id.
    void SetTarget(CHAT_ID const& Id);
    // FUN_0045e800 -- Down: the next player / channel.
    void NextTarget();
    // FUN_0045e940 -- Up: the previous player / channel.
    void PrevTarget();

    FChatChannelTable* Table;           // +0x530
    int TableGeneration;                // +0x534
    unsigned char bDrawBackground;      // +0x538
    unsigned char bCloseAfterSend;      // +0x539
    FChatBackground Background;         // +0x53c
    BinaryRDLWriter Writer;             // +0x564
    CHAT_ID Target;                     // +0x568
    RenderDisplayList PrefixSystem;     // +0x56c -- G_ChatIdChannel1
    RenderDisplayList PrefixLocal;      // +0x578 -- G_ChatIdChannel2
    RenderDisplayList PrefixGroup;      // +0x584 -- G_ChatIdChannel3
    RenderDisplayList PrefixConsole;    // +0x590 -- G_ChatIdNone
    RenderDisplayList PrefixUniverse;   // +0x59c -- G_ChatIdChannel0

private:
    ChatTextInputDisplay(ChatTextInputDisplay const&) = delete;
    ChatTextInputDisplay& operator=(ChatTextInputDisplay const&) = delete;
};
