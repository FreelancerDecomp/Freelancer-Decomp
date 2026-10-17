// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_messaging.h"

#include "menu_sound_arch.h"  // PlayClientSoundArchIfIdle (FUN_004285f0), SetClientSoundSourceType
#include "player_behavior.h"  // GetPlayerObjRW (FUN_0054baf0)
#include "space_letterbox.h"  // IsLetterboxVisible (FUN_0041a3e0)
#include "star_sys.h"         // StarSysHasVoiceMessage (FUN_00553660), StarSysPreloadVoiceMessage
#include "voice_profiles.h"   // LookupClientVoice (FUN_00430ab0), FVoiceProfile, FVoiceMsg

#include "common_id.h"        // CreateID
#include "dacom.h"            // FDUMP
#include "soundmanager.h"     // ISoundSource

#include <list>

namespace
{
    // One queue entry: the 12-byte payload of a list node (node +0x08..+0x13).
    struct FNnVoiceMessage
    {
        unsigned MsgId;         // +0x00
        unsigned char bKeyBit2; // +0x04
        ISoundSource* Source;   // +0x08

        FNnVoiceMessage(unsigned InMsgId, unsigned char bInKeyBit2)
            : MsgId(InMsgId), bKeyBit2(bInKeyBit2), Source(nullptr)
        {
        }

        // FUN_00489a50 / FUN_00489f70 (flag 0) / FUN_00489dc0: stop (vt+0x60) and release
        // (vt+8) the playing source.
        ~FNnVoiceMessage()
        {
            if (Source != nullptr)
            {
                Source->Stop();
                Source->Release();
                Source = nullptr;
            }
        }

        FNnVoiceMessage(FNnVoiceMessage const&) = delete;
        FNnVoiceMessage& operator=(FNnVoiceMessage const&) = delete;
    };

    // DAT_00671f50: constructed by the XC entry LAB_00489c70 (FUN_00489c90), destroyed at exit
    // through LAB_00489cc0 -> FUN_00489cd0. std::list keeps the same FIFO order and size count.
    std::list<FNnVoiceMessage> G_NnVoiceQueue;

    unsigned G_NnVoiceId = 0;                  // DAT_00671f5c
    FVoiceProfile* G_NnVoiceProfile = nullptr; // DAT_00671f70

    // NN_Messaging.cpp's overflow limit (CMP EBP,0x4 / JGE at 0x00489b70).
    int const kNnVoiceQueueCapacity = 4;

    // FUN_00489a80 (thiscall on the entry): start the entry's line through the NNVoice profile.
    void NnVoicePlay(FNnVoiceMessage* Entry)
    {
        if (IsLetterboxVisible()) // FUN_0041a3e0
        {
            return;
        }
        if (G_NnVoiceProfile == nullptr)
        {
            return;
        }
        // 0x00489aa0..0x00489acd: with a player ship, make sure the line is on the system's
        // preload list.
        if (GetPlayerObjRW() != nullptr)
        {
            if (!StarSysHasVoiceMessage(G_NnVoiceId, Entry->MsgId)) // FUN_00553660
            {
                StarSysPreloadVoiceMessage(G_NnVoiceId, Entry->MsgId); // FUN_00553590
            }
        }
        FVoiceMsg* const Msg = G_NnVoiceProfile->FindMessage(Entry->MsgId); // profile vt+0x14
        if (Msg == nullptr)
        {
            return;
        }
        // 0x00489ae2..0x00489b24: the play key. Dword 1's low byte keeps its stale bits 3..7 and
        // takes the entry's flag in bit 2 (bits 0/1 cleared); the upper three bytes are stale.
        // The port has 0 where retail has stack garbage. Dwords 0, 2..5 are 0.
        unsigned PlayKey[6] = {};
        PlayKey[1] = static_cast<unsigned>((Entry->bKeyBit2 << 2) & 4);
        void* Handle = nullptr;
        PlayClientSoundArchIfIdle(static_cast<Archetype::ISound*>(Msg), PlayKey, &Handle);
        // FUN_004285f0 writes the out pointer only when it starts a source.
        if (Handle != nullptr)
        {
            Entry->Source = static_cast<ISoundSource*>(Handle);
        }
        // 0x00489b31..0x00489b3a: the source's category = 5 (voice).
        if (Entry->Source != nullptr)
        {
            SetClientSoundSourceType(Entry->Source, 5);
        }
    }

    // FUN_00489b50 (thiscall on DAT_00671f50, RET 8).
    void NnVoiceQueuePush(unsigned MsgId, unsigned char bKeyBit2)
    {
        int const CountBefore = static_cast<int>(G_NnVoiceQueue.size());
        if (CountBefore >= kNnVoiceQueueCapacity)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
                  "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_Messaging.cpp", 0xa2,
                  "NN Message inbox overflow!");
            return;
        }
        // 0x00489b79..0x00489b93: a message already queued is not queued twice.
        for (std::list<FNnVoiceMessage>::iterator It = G_NnVoiceQueue.begin();
             It != G_NnVoiceQueue.end(); ++It)
        {
            if (It->MsgId == MsgId)
            {
                return;
            }
        }
        // 0x00489b95..0x00489bda: push_back of {MsgId, flag, null} (FUN_00489f40 + FUN_0059f7e0);
        // the stack temporary's destructor at 0x00489bdd has no source to release.
        G_NnVoiceQueue.emplace_back(MsgId, bKeyBit2);
        // 0x00489c04..0x00489c10: the queue was empty -- start the new head now.
        if (CountBefore == 0)
        {
            NnVoicePlay(&G_NnVoiceQueue.front());
        }
    }
}

void NnVoiceInit()
{
    // FUN_00489de0.
    G_NnVoiceId = CreateID("NNVoice");
    G_NnVoiceProfile = LookupClientVoice(G_NnVoiceId);
}

void NnVoiceTick(float Unused)
{
    // FUN_00489e00.
    (void)Unused;
    if (G_NnVoiceQueue.empty())
    {
        return;
    }
    // 0x00489e09..0x00489e29: a head that is still playing stays (vt+0x74 IsFinished; NEG/SBB/
    // INC turns "not finished" into the early return).
    ISoundSource* const HeadSource = G_NnVoiceQueue.front().Source;
    if (HeadSource != nullptr && HeadSource->IsFinished() == 0)
    {
        return;
    }
    // 0x00489e2b..0x00489e5c: unlink the head, FUN_00489f70(0) on it, free the node.
    G_NnVoiceQueue.pop_front();
    // 0x00489e61..0x00489e6e: tail jump into FUN_00489a80 on the new head.
    if (!G_NnVoiceQueue.empty())
    {
        NnVoicePlay(&G_NnVoiceQueue.front());
    }
}

void NnVoiceEnqueue(unsigned MsgId, unsigned char bKeyBit2)
{
    // FUN_00489e80: MOV ECX,DAT_00671f50; CALL FUN_00489b50.
    NnVoiceQueuePush(MsgId, bKeyBit2);
}

void NnVoiceEnqueueByName(char const* Name)
{
    // FUN_00489ea0.
    NnVoiceQueuePush(CreateID(Name), 0);
}
