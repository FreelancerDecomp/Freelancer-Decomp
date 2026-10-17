// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "chat_text_input.h"

#include "chat_init.h"             // FChatChannelTable, G_ChatChannelTable, chat ids, ChatSetInputOpen
#include "effect_vertex_batch.h"   // FEffectVertexBatch (FUN_0046ebc0 / FUN_0046eab0 / FUN_0046e9c0)
#include "hud_command_dispatch.h"  // FHudCmdRecord, HudPostCommand (FUN_005763b0)
#include "ids_rdl.h"               // LoadIdsTextIntoRdl, RdlAppendNode, RdlClear
#include "login_mgr.h"             // G_ChatServer (DAT_0067ecd4)
#include "menu_ui_widgets.h"
#include "win3d.h"                 // Win3DFindByName (FUN_0059da10)
#include "misc_util.h"             // MiscUtilLoadTextureLibrary / MiscUtilReleaseTextureLibrary
#include "nn_group_control.h"      // NnGroupControl::GroupMembers
#include "player_spawn.h"          // GetPlayerConnectionId (DAT_00673344)
#include "win3d_renderables.h"     // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "win_factory.h"

#include "DALib.h"                 // DALib::TextureLib
#include "common_server.h"         // IChatServer
#include "dacom.h"                 // FDUMP, DACOM_CRC::GetCRC32
#include "render/shading.h"        // ITextureLibrary2

#include <string.h>

// Retail DAT_0066d920 -- WINFACTORY<ChatTextInputDisplay> static (LAB_0045e270; Create
// FUN_0046be00: operator_new(0x5a8), FUN_0046be40, slot 9, delete on failure).
WINFACTORY<ChatTextInputDisplay> G_ChatTextInputDisplayFactory("ChatTextInputDisplay");

namespace
{
    // The target frame colours (RGBA bytes in memory order, read as one dword by FUN_00579870).
    unsigned char const kColorUniverse[4] = {0xff, 0xff, 0xff, 0xff};   // DAT_00611560
    unsigned char const kColorSystem[4] = {0x87, 0xc3, 0xe0, 0xff};     // DAT_00611564
    unsigned char const kColorLocal[4] = {0x3b, 0xbf, 0x1d, 0xff};      // DAT_00611568
    unsigned char const kColorPlayer[4] = {0x3b, 0xbf, 0x1d, 0xff};     // DAT_0061156c
    unsigned char const kColorGroup[4] = {0xff, 0x7e, 0xfd, 0xff};      // DAT_00611570
    unsigned char const kColorConsole[4] = {0x3b, 0xbf, 0x1d, 0xff};    // DAT_00611574

    wchar_t const kTargetSpace[] = L" ";         // DAT_005cef90
    wchar_t const kTargetUnknown[] = L" ????";   // u" ????" 0x005cef84
    wchar_t const kTargetArrow[] = L"->";        // DAT_005cef58 via PTR_DAT_0061155c

    // DAT_0066d4e0 -- the 0x400-byte send buffer BinaryRDLWriter fills before SubmitChat.
    char G_ChatSendBuffer[0x400];

    // The clone-into-list idiom (FUN_0046c800 with a fresh clone, first clone deleted).
    void AppendNodeClone(RenderDisplayList& List, RDLNode const& Node)
    {
        RDLNode* const Temp = Node.Clone();
        RdlAppendNode(List, Temp->Clone());
        delete Temp;
    }

    // DAT_0066da84 +0x350: the group set's element count.
    bool ChatGroupIsEmpty()
    {
        NnGroupControl* const Group = G_ChatGroupControl;
        return Group == nullptr || Group->GroupMembers.empty();
    }
}

// ---------------------------------------------------------------------------------------------
// FChatBackground

FChatBackground::FChatBackground()
    // FUN_0046be40 0x0046be45..0x0046be7d: the string, vftable 0x005cef80, +0x15 = 0,
    // +0x24 / +0x20 / +0x1c = 0, +0x18 = -1.
    : bLoaded(0)
    , LibraryHandle(-1)
    , TextureId(nullptr)
    , TileWidth(0.0f)
    , TileHeight(0.0f)
{
}

FChatBackground::~FChatBackground()
{
    // FUN_0045e3e0
    if (bLoaded != 0)
    {
        DALib::TextureLib->ReleaseTextureRecord(TextureId);        // slot 14 (+0x38)
        MiscUtilReleaseTextureLibrary(LibraryHandle);    // FUN_0041c830
        bLoaded = 0;
    }
}

namespace
{
    // FUN_0045e470's texture-stage tables, {type, value} pairs up to a zero type: stage 0
    // DAT_005cee48 (COLOROP MODULATE, COLORARG1 TEXTURE, COLORARG2 DIFFUSE, ALPHAOP MODULATE,
    // ALPHAARG1 TEXTURE, ALPHAARG2 DIFFUSE), stage 1 DAT_005cee80 (both ops DISABLE, args
    // TEXTURE / CURRENT).
    struct FChatBgStageState
    {
        int Type;
        int Value;
    };
    FChatBgStageState const kChatBgStage0[] = {{1, 4}, {2, 2}, {3, 0}, {4, 4}, {5, 2}, {6, 0}, {0, 0}};
    FChatBgStageState const kChatBgStage1[] = {{1, 1}, {2, 2}, {3, 1}, {4, 1}, {5, 2}, {6, 1}, {0, 0}};

    // DAT_00611578 -- the quad's vertex colour, copied byte by byte into the batch's +0x30..+0x33
    // (0x0045e67b..0x0045e69f): bytes 00 00 80 b0.
    unsigned char const kChatBgColor[4] = {0x00, 0x00, 0x80, 0xb0};

    // The vertex batch FUN_0045e470 builds on its stack (0x0045e61a..0x0045e69f): Type 1, empty
    // buffers, FVF 0x142, the current pipeline AddRef'd (vt+4), growth 0x80, strip state and UV
    // zero, colour DAT_00611578. Its destructor is FUN_0046e9c0 -- the unwind funclet at
    // 0x005ba383 and, inlined, the normal path 0x0045e798..0x0045e7c3.
    struct FChatBgBatch
    {
        FChatBgBatch()
        {
            Batch.Type = 1;
            Batch.Vertices = nullptr;
            Batch.VertCount = 0;
            Batch.Fvf = 0x142;
            Batch.Indices = nullptr;
            Batch.IndexCount = 0;
            Batch.Pipeline = DALib::Pipeline;
            if (Batch.Pipeline != nullptr)
                Batch.Pipeline->AddRef();
            Batch.Growth = 0x80;
            Batch.VertCapacity = 0;
            Batch.IndexCapacity = 0;
            Batch.Flag28 = 0;
            Batch.bStripMode = 0;
            Batch.Pad2A = 0;
            Batch.Pad2B = 0;
            Batch.StripCounter = 0;
            Batch.V = 0.0f;
            Batch.U = 0.0f;
            Batch.ColorR = kChatBgColor[0];
            Batch.ColorG = kChatBgColor[1];
            Batch.ColorB = kChatBgColor[2];
            Batch.ColorA = kChatBgColor[3];
        }
        ~FChatBgBatch() { EffectVertexBatchDestroy(&Batch); } // FUN_0046e9c0

        FEffectVertexBatch Batch;

    private:
        FChatBgBatch(FChatBgBatch const&) = delete;
        FChatBgBatch& operator=(FChatBgBatch const&) = delete;
    };
}

bool FChatBackground::Draw(float const* Orientation, float Width, float Height)
{
    // FUN_0045e470 (thiscall, RET 0xc). Both tile sizes are compared with 0.0f (DAT_005c7474,
    // FCOMP / TEST AH,0x44 / JNP: only an equal compare leaves, unordered goes on); nothing
    // without a loaded texture either. Returns 0 on those exits and 1 after drawing.
    if (TileWidth == 0.0f)
        return false;
    if (TileHeight == 0.0f)
        return false;
    if (bLoaded == 0)
        return false;

    // 0x0045e4c1..0x0045e554: render states (Pipeline slot 60, re-read from DALib::Pipeline for
    // every call): lighting off, z-write off, alpha blend on, 0x89 off, src/dest blend
    // SRCALPHA / INVSRCALPHA, 0x1b on.
    DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 0);
    DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
    DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, 1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
    DALib::Pipeline->SetRenderState(D3DRS_SRCBLEND, 5);
    DALib::Pipeline->SetRenderState(D3DRS_DESTBLEND, 6);
    DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);

    // 0x0045e555..0x0045e5a9: the two stage tables (slot 62), the first type read as the
    // constant 1, each next type from the table until a zero one.
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        FChatBgStageState const* Entry = kChatBgStage0;
        int Type = 1;
        for (;;)
        {
            Pipe->SetTextureStageState(0, Type, Entry->Value);
            Type = Entry[1].Type;
            ++Entry;
            if (Type == 0)
                break;
        }
    }
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        FChatBgStageState const* Entry = kChatBgStage1;
        int Type = 1;
        for (;;)
        {
            Pipe->SetTextureStageState(1, Type, Entry->Value);
            Type = Entry[1].Type;
            ++Entry;
            if (Type == 0)
                break;
        }
    }
    // 0x0045e5ab..0x0045e5d0: stage 0 ADDRESSU / ADDRESSV = WRAP.
    DALib::Pipeline->SetTextureStageState(0, 0xd, 1);
    DALib::Pipeline->SetTextureStageState(0, 0xe, 1);

    // 0x0045e5d6..0x0045e602: TextureLib slot 22 (texture, frame 0, 5-dword out); on success
    // the handle (out[0]) goes to stage 0 (Pipeline slot 66).
    unsigned long Frame[5];
    if (DALib::TextureLib->GetTextureFrame(TextureId, 0, Frame) == 0)
        DALib::Pipeline->SetTextureStageTexture(0, reinterpret_cast<void*>(Frame[0]));

    // 0x0045e608..0x0045e617: Pipeline slot 31 with the control's orientation.
    DALib::Pipeline->SetModelview(Orientation);

    // 0x0045e61a..0x0045e6a3: the stack batch, then FUN_0046ebc0.
    FChatBgBatch Local;
    FEffectVertexBatch* const Batch = &Local.Batch;
    EffectVertexBatchReset(Batch);

    // 0x0045e6a8..0x0045e749: a triangle list (type 4) in quad mode -- FUN_0046eab0 turns the
    // fourth vertex into two triangles. UV repeats the tile over the box: U = x / +0x20,
    // V = y / +0x24, the box running from (0, 0) to (Width, -Height).
    float const V1 = Height / TileHeight;
    Batch->Type = 4;
    Batch->Flag28 = 0;
    Batch->bStripMode = 1;
    Batch->U = 0.0f;
    Batch->V = V1;
    EffectVertexBatchAppend(Batch, 0.0f, 0.0f, 0.0f);

    Batch->U = Width / TileWidth;
    Batch->V = V1;
    EffectVertexBatchAppend(Batch, Width, 0.0f, 0.0f);

    Batch->V = 0.0f;
    Batch->U = Width / TileWidth;
    float const Bottom = -Height;
    EffectVertexBatchAppend(Batch, Width, Bottom, 0.0f);

    Batch->U = 0.0f;
    Batch->V = 0.0f;
    EffectVertexBatchAppend(Batch, 0.0f, Bottom, 0.0f);

    // 0x0045e74a..0x0045e78e: with vertices -- a closed batch (+0x28) repeats its first vertex
    // at the end; then Pipeline slot 69 (the batch's own AddRef'd pipeline, when it has one).
    int Count = Batch->VertCount;
    if (Count != 0)
    {
        if (Batch->Flag28 != 0)
        {
            Batch->Vertices[Count] = Batch->Vertices[0];
            ++Count;
            Batch->VertCount = Count;
        }
        if (Batch->Pipeline != nullptr)
        {
            Batch->Pipeline->DrawPrimitive(Batch->Type, Batch->Fvf, Batch->Vertices,
                                           static_cast<unsigned>(Count), nullptr);
        }
    }
    // 0x0045e798..0x0045e7c3: the batch's destructor (FUN_0046e9c0 inlined) -- ~FChatBgBatch.
    return true;
}

// ---------------------------------------------------------------------------------------------
// ChatTextInputDisplay

ChatTextInputDisplay::ChatTextInputDisplay()
    // FUN_0046be40: RichTextEditWin's ctor (FUN_0057b8d0), the background, the BinaryRDLWriter
    // (+0x564), Target = 0 (+0x568..+0x56b), the five prefix lists (FUN_0046ba60 / the Common
    // RenderDisplayList ctor), then the three vftables. Retail does NOT initialise +0x530..+0x539
    // (Create sets +0x538 / +0x539 and, with a table, +0x530); the port zeroes them -- see
    // Update for what that changes.
    : Table(nullptr)
    , TableGeneration(0)
    , bDrawBackground(0)
    , bCloseAfterSend(0)
    , Target(0u)
{
}

ChatTextInputDisplay::~ChatTextInputDisplay()
{
    // FUN_0046c640: the five prefix lists (+0x59c first), the background (FUN_0045e3e0 inline),
    // then RichTextEditWin's dtor (FUN_0057bbf0) -- member and base destructors.
}

bool ChatTextInputDisplay::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_0045eee0 (RET 8): RichTextWin's Create (FUN_0057b5a0) with (Parent, 0).
    bool const bResult = RichTextWin::Create(NewParent, nullptr);

    // 0x0045ef0e..0x0045ef52
    bDrawFrame = 1;                              // +0x402
    bDrawBackground = 1;                         // +0x538
    bCloseAfterSend = 0;                         // +0x539
    Background.TileWidth = 0.08f;                // +0x55c = 0x3da3d70a
    Background.TileHeight = 0.0533f;             // +0x560 = 0x3d5a740d
    Background.Name.assign("ChatBackground");    // +0x540 (s_ChatBackground_005cf030)

    // 0x0045ef58..0x0045ef76: DAT_005ceedc / DAT_005ceee0.
    NormWidth = 0.46f;                           // 0x3eeb851f
    NormHeight = 0.065f;                         // 0x3d851eb8

    // 0x0045ef5e..0x0045f423: the five target prefixes -- the IDS text with its last paragraph
    // stripped, or JustifyNode(1) + the English debug text.
    LoadIdsTextIntoRdl(0x6003b, PrefixSystem, L"DEBUG - system");       // +0x56c
    LoadIdsTextIntoRdl(0x6003a, PrefixLocal, L"DEBUG - local");         // +0x578
    LoadIdsTextIntoRdl(0x60039, PrefixGroup, L"DEBUG - group");         // +0x584
    LoadIdsTextIntoRdl(0x60038, PrefixConsole, L"DEBUG - console");     // +0x590
    LoadIdsTextIntoRdl(0x6003c, PrefixUniverse, L"DEBUG - universe");   // +0x59c

    // 0x0045f429..0x0045f44f: the channel table when it exists; fixed style; the system channel;
    // the display.
    if (G_ChatChannelTable != nullptr)
        Table = G_ChatChannelTable;
    SetFixedStyle(true);                 // FUN_0057bdd0(1)
    SetTarget(G_ChatIdChannel1);         // FUN_0045eaf0(&DAT_0066da40)
    RebuildDisplay();                    // FUN_0057d330

    // 0x0045f454..0x0045f4c4: the half extents (+0xb4) and hit offset (+0xc0) both
    // {W * 0.5, H * 0.5, 0} (DAT_005c75e0 = 0.5f), draw layer and sub-layer 7.
    float const HalfW = NormWidth * 0.5f;
    float const HalfH = NormHeight * 0.5f;
    HalfExtents[0] = HalfW;
    HalfExtents[1] = HalfH;
    HalfExtents[2] = 0.0f;
    HitOffset[0] = HalfW;
    HitOffset[1] = HalfH;
    HitOffset[2] = 0.0f;
    DrawLayer = 7;
    DrawSubLayer = 7;
    return bResult;
}

void ChatTextInputDisplay::SetTarget(CHAT_ID const& Id)
{
    // FUN_0045eaf0 (RET 4). The style, the prefix list and the frame colour by target; a player
    // target uses its name from the channel table ("????" text when there is no table).
    wchar_t const* Text = kTargetSpace;
    unsigned short Style = 0;
    RenderDisplayList const* Name = nullptr;
    unsigned char const* Color = nullptr;
    if (Id.Id == G_ChatIdChannel1.Id)
    {
        Style = 0xd801;
        Name = &PrefixSystem;
        Color = kColorSystem;
    }
    else if (Id.Id == G_ChatIdChannel2.Id)
    {
        Style = 0x1d9d;
        Name = &PrefixLocal;
        Color = kColorLocal;
    }
    else if (Id.Id == G_ChatIdChannel3.Id)
    {
        Style = 0xba28;
        Name = &PrefixGroup;
        Color = kColorGroup;
    }
    else if (Id.Id == G_ChatIdChannel0.Id)
    {
        Style = 0xd681;
        Name = &PrefixUniverse;
        Color = kColorUniverse;
    }
    else if (Id.Id == G_ChatIdNone.Id)
    {
        Style = 0x73f7;
        Name = &PrefixConsole;
        Color = kColorConsole;
    }
    else
    {
        // 0x0045ebb7..0x0045ebf2: FUN_0046cd50 find; the node's list or null.
        Style = 0x8283;
        if (Table != nullptr)
            Name = Table->FindName(Id);
        else
            Text = kTargetUnknown;
        Color = kColorPlayer;
    }

    // 0x0045ebf7..0x0045ec00: FUN_00579870(colour), +0x568 = *Id.
    unsigned long ColorWord = 0;
    memcpy(&ColorWord, Color, 4);
    SetFrameColor(&ColorWord);
    Target = Id;

    // 0x0045ec06..0x0045ecd6: the edit prefix list (+0x4dc) emptied (FUN_0046c860), then the
    // style and the leading text.
    RdlClear(Prefix);
    {
        StyleNode const Node(Style);
        AppendNodeClone(Prefix, Node);
    }
    {
        TextNode const Node(Text, -1);
        AppendNodeClone(Prefix, Node);
    }

    // 0x0045ecdc..0x0045ed4f: a clone of every node of the name list.
    if (Name != nullptr)
        ChatAppendRdlCopy(Prefix, *Name);

    // 0x0045ed51..0x0045edd2: TextNode(L"->", -1).
    {
        TextNode const Node(kTargetArrow, -1);
        AppendNodeClone(Prefix, Node);
    }

    // 0x0045edd8..0x0045ee92: the fixed-style list (+0x4fc) emptied node by node
    // (FUN_0046d680), then StyleNode(style).
    RdlClear(FixedStyle);
    {
        StyleNode const Node(Style);
        AppendNodeClone(FixedStyle, Node);
    }
}

void ChatTextInputDisplay::NextTarget()
{
    // FUN_0045e800. Own = {connection id word, 0, 0}; the result starts as {0, 0, 0}.
    CHAT_ID const Own(GetPlayerConnectionId() & 0xffffu);
    CHAT_ID Result(0u);

    if (Target.Id == G_ChatIdChannel1.Id)
    {
        // 0x0045e840..0x0045e88e: from the first entry, the first player (byte 2 clear) that is
        // not us; the system channel when there is none.
        Result = G_ChatIdChannel1;
        for (FChatChannelTable::FNameMap::iterator It = Table->Names.begin();
             It != Table->Names.end(); ++It)
        {
            unsigned const Key = It->first;
            if (((Key >> 16) & 0xffu) != 0)
                continue;
            if (Key == Own.Id)
                continue;
            Result = CHAT_ID(Key);
            break;
        }
    }
    else if (Target.Id == G_ChatIdChannel3.Id)
    {
        // 0x0045e8cf..0x0045e8db
        Result = G_ChatIdChannel1;
    }
    else
    {
        // 0x0045e8dd..0x0045e93a: from the current target onwards, the next entry that is not
        // us (a channel counts); past the last one, the group channel. An unknown target
        // leaves {0, 0, 0}.
        FChatChannelTable::FNameMap::iterator It = Table->Names.find(Target.Id);
        if (It != Table->Names.end())
        {
            for (;;)
            {
                ++It;
                if (It == Table->Names.end())
                {
                    Result = G_ChatIdChannel3;
                    break;
                }
                if (It->first == Own.Id)
                    continue;
                Result = CHAT_ID(It->first);
                break;
            }
        }
    }

    // 0x0045e890..0x0045e8bd: the group channel only with a non-empty group.
    if (Result.Id == G_ChatIdChannel3.Id && ChatGroupIsEmpty())
        Result = G_ChatIdChannel1;
    SetTarget(Result);
}

void ChatTextInputDisplay::PrevTarget()
{
    // FUN_0045e940.
    CHAT_ID const Own(GetPlayerConnectionId() & 0xffffu);
    CHAT_ID Result(0u);

    if (Target.Id == G_ChatIdChannel1.Id && !ChatGroupIsEmpty())
    {
        // 0x0045e983..0x0045e998
        Result = G_ChatIdChannel3;
    }
    else if (Target.Id == G_ChatIdChannel3.Id || Target.Id == G_ChatIdChannel1.Id)
    {
        // 0x0045ea2c..0x0045eaa1: from the last entry backwards; a channel stops the walk, the
        // first player that is not us wins; the system channel otherwise.
        Result = G_ChatIdChannel1;
        FChatChannelTable::FNameMap::iterator It = Table->Names.end();
        while (It != Table->Names.begin())
        {
            --It;
            unsigned const Key = It->first;
            if (((Key >> 16) & 0xffu) != 0)
                break;
            if (Key != Own.Id)
            {
                Result = CHAT_ID(Key);
                break;
            }
        }
    }
    else
    {
        // 0x0045e9b9..0x0045ea27: from the current target backwards, the same rules. Stepping
        // back from the first entry lands on the head (end) and stops.
        Result = G_ChatIdChannel1;
        FChatChannelTable::FNameMap::iterator It = Table->Names.find(Target.Id);
        if (It != Table->Names.end())
        {
            while (It != Table->Names.begin())
            {
                --It;
                unsigned const Key = It->first;
                if (((Key >> 16) & 0xffu) != 0)
                    break;
                if (Key != Own.Id)
                {
                    Result = CHAT_ID(Key);
                    break;
                }
            }
        }
    }

    // 0x0045eaa7..0x0045ead6
    if (Result.Id == G_ChatIdChannel3.Id && ChatGroupIsEmpty())
        Result = G_ChatIdChannel1;
    SetTarget(Result);
}

void ChatTextInputDisplay::Update()
{
    // FUN_0045f5e0: when the channel table changed since the last look and the current target
    // is no longer in it, fall back to the system channel -- on the "CTID" line as well as on
    // this one (they are the same object in practice) -- and rebuild.
    FChatChannelTable* const Current = Table;
    if (Current != nullptr && TableGeneration != Current->Generation)
    {
        // 0x0045f60f..0x0045f642: FUN_0046d920 lower_bound, key test (JA = not found).
        if (Current->Names.find(Target.Id) == Current->Names.end())
        {
            CHAT_ID const Fallback = G_ChatIdChannel1;
            G_ChatInputTarget = Fallback;    // DAT_0066d3fc
            Win3D* const Found = Win3DFindByName("CTID");   // FUN_0059da10, name 0x005cf04c
            if (Found != nullptr)
            {
                ChatTextInputDisplay* const Input = static_cast<ChatTextInputDisplay*>(Found);
                Input->SetTarget(Fallback);
                Input->RebuildDisplay();
            }
            RebuildDisplay();
        }
        TableGeneration = Table->Generation;
    }
    RichTextEditWin::Update();   // FUN_0057c360
}

void ChatTextInputDisplay::Draw()
{
    // FUN_0045f4f0
    Win3DFlushBeforeOverlayDraw();   // FUN_0045a9f0
    if (bDrawBackground != 0)
    {
        if (Background.bLoaded == 0)
        {
            // 0x0045f521..0x0045f5af: "<image root>chat.txm" loaded as a texture library
            // (FUN_0041c7e0) into +0x18; the texture named by the CRC of the background's
            // name looked up (TextureLib slot 12 with {name, crc}) into +0x1c; loaded on S_OK.
            std::string Path = G_ChatImageRoot;
            Path.append("chat.txm");
            Background.LibraryHandle = MiscUtilLoadTextureLibrary(Path.c_str());
            struct FTextureQuery
            {
                char const* Name;
                unsigned long Crc;
            } Query;
            Query.Name = Background.Name.c_str();
            Query.Crc = DACOM_CRC::GetCRC32(Query.Name);
            if (DALib::TextureLib->FindTextureByCrc(&Query, &Background.TextureId) == 0)
                Background.bLoaded = 1;
        }
        // 0x0045f5b3..0x0045f5c7: drawn whether or not the load succeeded.
        Background.Draw(Orientation, NormWidth, NormHeight);   // FUN_0045e470
    }
    RichTextEditWin::Draw();   // FUN_0057c440
}

bool ChatTextInputDisplay::OnKeyDown(FHudCmdRecord const* Record)
{
    // FUN_0045f700 (RET 4): key-downs of an active line only; Enter, Escape, Up and Down through
    // the byte table at 0x0045f83c (Key - 0xd); everything else to RichTextEditWin.
    if (Record->Byte18 != 0 && (Flags & 1) != 0)
    {
        switch (Record->Key)
        {
        case 0x0d:
        {
            // 0x0045f73a..0x0045f7d3: a non-empty line is serialised from the text list (+0x4e8)
            // and submitted from us to the target; the line is cleared either way, and a line
            // opened for one message closes.
            if (GetText(nullptr, 0) > 0)
            {
                unsigned OutSize = 0;
                if (Writer.write_buffer(TextDisplay, G_ChatSendBuffer, 0x400, OutSize))
                {
                    IChatServer* const ChatServer = static_cast<IChatServer*>(G_ChatServer);
                    if (ChatServer != nullptr)
                    {
                        ChatServer->SubmitChat(CHAT_ID(GetPlayerConnectionId() & 0xffffu), OutSize,
                                               G_ChatSendBuffer, Target, -1);
                    }
                }
            }
            ClearText();
            if (bCloseAfterSend != 0)
            {
                ChatSetInputOpen(false);
                bCloseAfterSend = 0;
            }
            return true;
        }
        case 0x1b:
            // 0x0045f7d6
            ClearText();
            ChatSetInputOpen(false);
            return true;
        case 0x26:
            // 0x0045f7ee
            PrevTarget();
            RebuildDisplay();
            return true;
        case 0x28:
            // 0x0045f803
            NextTarget();
            RebuildDisplay();
            return true;
        default:
            break;
        }
    }
    return RichTextEditWin::OnKeyDown(Record);   // 0x0045f818
}

void ChatTextInputDisplay::OnActivated()
{
    // LAB_0045f6a0: FUN_005760d0(0x14) on the +0x498 face.
    RegisterEvent(0x14);
}

void ChatTextInputDisplay::OnDeactivating()
{
    // LAB_0045f6b0: FUN_005760f0(0x14).
    UnregisterEvent(0x14);
}

bool ChatTextInputDisplay::OnEvent(unsigned MsgId)
{
    // FUN_0045f6c0 (RET 4, this = the face): 0x14 clears and closes the line and posts 0x14 on;
    // anything else goes to FUN_0057c930.
    if (MsgId != 0x14)
        return RichTextEditWin::OnEvent(MsgId);
    ClearText();
    ChatSetInputOpen(false);
    HudPostCommand(0x14, 0);
    return true;
}
