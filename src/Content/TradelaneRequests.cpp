// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU55 (06f6fa90-06f6fd90): trade lane request handlers. See TradelaneRequests.h.

#include "TradelaneRequests.h"

#include "ContentStaticIds.h"     // TU55 static IDs 06f6fa90..06f6fc70 (DAT_06fd1d4c..DAT_06fd1d10)
#include "ContentPlayerState.h"   // FindContentPlayerStateByShip (FUN_06f49bc0)
#include "DockQueues.h"           // FGcsDockGrantedEvent (vtable 06fbbd70)
#include "Gcs.h"                  // GGcsConversations (DAT_06fd1c08)
#include "SystemInfo.h"           // FindSystemInfo (FUN_06f75f30)
#include "common_server.h"        // pub::SpaceObj::GetType / Activate / LaneResponse

#include <cstdint>

// FUN_06f6fcb0. The response starts at 5 (06f6fcd5) and stays 5 unless the object asked for
// is a trade lane ring (GetType's low byte has bit 7 set: TEST AL,AL / JNS 06f6fd65 jumps on
// the sign bit clear; GetType's result is not tested and the type starts as the stack slot of
// the argument). For a ring: a player ship (FUN_06f49bc0 non-null) asks TU46 FUN_06f47ee0
// (ring, end ring); 1 or 2 -> response 1 (no event). Any other answer, and every non-player
// ship, gets response 4: the GCS "granted" event (type 2; DockIndex -1, DockType 0,
// bWaitedInQueue 0 - the stores at 06f6fd39-06f6fd5a) is queued (FUN_06ee8d70, its float
// dropped). Then LaneResponse(ship, response), and on 4 Activate(ring, true, ship).
void HandleTradelaneRequest(FTradelaneRequest const* Request)
{
    // 06f6fcbd: the out slot is the argument's own stack slot, so a failed GetType (it writes
    // nothing on -4/-2) leaves the parameter pointer there and its low byte is tested.
    unsigned int RingType = static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(Request));
    pub::SpaceObj::GetType(Request->RingId, RingType);
    int Response = 5;
    if ((RingType & 0x80u) != 0)
    {
        bool bAnswered = false;
        FContentPlayerState* Player = FindContentPlayerStateByShip(&Request->ShipId);
        if (Player != nullptr)
        {
            int const Check = Player->CheckTradelaneRoute(Request->RingId, Request->EndRingId); // FUN_06f47ee0
            if (Check == 1)
            {
                Response = Check;
                bAnswered = true;
            }
            else if (Check == 2)
            {
                Response = 1;
                bAnswered = true;
            }
        }
        if (!bAnswered)
        {
            Response = 4;
            FGcsDockGrantedEvent Event;
            Event.System = FindSystemInfo(&Request->SystemId);
            Event.Speaker = Request->RingId;
            Event.Listener = Request->ShipId;
            Event.DockIndex = -1;
            Event.DockType = 0;
            Event.bWaitedInQueue = false;
            Event.Response = Response;
            GGcsConversations.QueueConversation(&Event);
        }
    }
    pub::SpaceObj::LaneResponse(Request->ShipId, Response);
    if (Response == 4)
        pub::SpaceObj::Activate(Request->RingId, true, static_cast<int>(Request->ShipId));
}

// FUN_06f6fc90: Activate(ring, false, ship).
void HandleTradelaneRelease(FTradelaneRequest const* Request)
{
    pub::SpaceObj::Activate(Request->RingId, false, static_cast<int>(Request->ShipId));
}
