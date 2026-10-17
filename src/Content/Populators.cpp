// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU18 (06eb2670-06eb3ec0): the BasePop controller.
// Content.dll TU52 (06f5a2e0-06f5c760): the SpacePop controller, population flags and
// the exclusion spheres. StoryLauncher lives in StoryLauncher.cpp (CNT-MS-OBJ).

#include "Populators.h"

#include "BaseDB.h"                    // FContentBase, FBarCharacterList (TU17)
#include "ContentStaticIds.h"          // TU52 06f5a2e0-06f5a4c0 (and TU18 06eb2670-06eb2850)
#include "ContentInternal.h"
#include "ContentPlayerState.h"        // FContentPlayerState, FContentPlayerShip (TU46)
#include "DockQueues.h"                // CountNpcShipsNear (TU53 FUN_06f67440)
#include "GenericScripts.h"            // StartBarCharacterScript (TU34 FUN_06f00b60)
#include "NpcShip.h"                   // FContentNpcShip, FContentNpcShipParams (TU43)
#include "RandomMissionDB.h"           // ReleaseRmDestSystems (TU47 FUN_06f4d650)
#include "SpaceSupport.h"              // TestExclusionSpheres (TU53 FUN_06f616e0)
#include "SystemInfo.h"                // FSystemInfo, FSystemZone, FZonePopulation
#include "common_server.h"             // pub::SinglePlayer
#include "common_server_controller.h"
#include "x86math.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <intrin.h>
#include <map>
#include <memory>
#include <new>
#include <vector>

// Retail DAT_06fd0e54: an ordered vector of 16-byte records (static init LAB_06f5a4e0,
// atexit FUN_06f5a510).
std::vector<FSpacePopExclusionSphere> GSpacePopExclusionSpheres;

namespace
{
    // Retail DAT_06fce769, initialized to 01h in .data.
    unsigned char GSpacePopPopulationEnabled = 1;
    // Retail DAT_06fce768, initialized to 01h in .data.
    unsigned char GSpacePopEncounterEnabled = 1;
    // Retail DAT_06fce76a, initialized to 01h in .data (FUN_06f5a7a0 / FUN_06f5a7b0).
    unsigned char GSpacePopHostileClamp = 1;
    // Retail DAT_06fce76b, initialized to 01h in .data (FUN_06f5a7c0 / FUN_06f5a7e0).
    unsigned char GSpacePopSolarDetection = 1;

}

// Retail DAT_06fd0e64: SpacePop's controller ID, written by the ctor and compared by
// FUN_06f5a5d0 (06f5a697) and TU43 FUN_06f33e80.
unsigned int GSpacePopControllerId = 0;

// Retail DAT_06fd1c78 (.bss, 0): see Populators.h.
unsigned char GSpacePopAttackClamp = 0;

// FUN_06f63a80 (TU53, RET 0x50).
FSpawnRequest::FSpawnRequest(bool bInArrival, unsigned int InZoneList, unsigned int InControllerId,
                             FContentPlayerShip* InPlayerShip,
                             FSpaceObjPlacement const& InPlacement, Vector const& InVelocity,
                             std::vector<FSpacePopExclusionSphere> const* InExclusionSpheres)
    : bArrival(bInArrival)
    , ZoneList(InZoneList)
    , ControllerId(InControllerId)
    , PlayerShip(InPlayerShip)
    , Placement(InPlacement)
    , Velocity(InVelocity)
{
    // 06f63b0f-06f63ca9: the vector assignment (no self test needed: a fresh vector).
    ExclusionSpheres = *InExclusionSpheres;
}

namespace
{
    // DAT_06fd0e4c: static init LAB_06f5a5b0 = float(DAT_06fbbb34 -0.6f + double 0.01
    // DAT_06fb7a88) = -0.59f.
    float const GSpacePopAttitudeFloor =
        static_cast<float>(static_cast<double>(-0.6f) + 0.01);

    // DAT_06fd0ea8: this TU's math engine, created on first use (FUN_06fa8fa0).
    I3DMathEngine* GSpacePopMath = nullptr;

    unsigned int ZoneDensity(FSystemZone const* Zone)
    {
        // FUN_06ef9dc0 on zone +0x18: MOV EAX,[ECX]; the callers compare it unsigned (JBE)
        // and widen it with a zero high dword (FILD qword).
        return static_cast<unsigned int>(Zone->Population.Density);
    }

    // FUN_06f5bb10: FLD float DAT_06fbbb68 (2500.0f).
    float GetSpacePopAheadStep()
    {
        return 2500.0f;
    }

    // FUN_06f5c6d0 (thiscall on the placement, RET 8): the point transformed by the placement
    // (I3DMathEngine slot +0x28, TransformXform).
    Vector TransformByPlacement(FSpaceObjPlacement const* Placement, Vector const* Point)
    {
        if (GSpacePopMath == nullptr)
            GSpacePopMath = ContentAcquire3DMathEngine();
        FTransform Xform;
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Column = 0; Column < 3; ++Column)
                Xform.Rotation.D[Row][Column] = Placement->Orientation.E[Row][Column];
        }
        Xform.Translation.X = Placement->Position.x;
        Xform.Translation.Y = Placement->Position.y;
        Xform.Translation.Z = Placement->Position.z;
        FVector Source;
        Source.X = Point->x;
        Source.Y = Point->y;
        Source.Z = Point->z;
        FVector Result;
        GSpacePopMath->TransformXform(Result, Xform, Source);
        Vector Out;
        Out.x = Result.X;
        Out.y = Result.Y;
        Out.z = Result.Z;
        return Out;
    }

    // FUN_06f5be70 (thiscall on the result, RET 4): identity orientation, Position.
    void SetPlacementAt(FSpaceObjPlacement* Out, Vector const* Position)
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Column = 0; Column < 3; ++Column)
                Out->Orientation.E[Row][Column] = Row == Column ? 1.0f : 0.0f;
        }
        Out->Position = *Position;
    }

    // FUN_06f5bb40 (cdecl): each zone's share of the population. Weight = max density /
    // repop_time * the average repop_time (all float stores); the share is the weight over
    // the sum of weights (0 when the sum is 0). AtShip zones count with their density, Near
    // zones with ftol(density * 0.6).
    void ComputeZoneShares(std::vector<FSystemZone*> const* AtShip,
                           std::vector<FSystemZone*> const* Near,
                           std::vector<float>* OutAtShipShares,
                           std::vector<float>* OutNearShares)
    {
        float RepopSum = 0.0f;
        unsigned int MaxDensity = 0;
        for (std::vector<FSystemZone*>::const_iterator It = AtShip->begin(); It != AtShip->end(); ++It)
        {
            RepopSum = (*It)->Population.RepopTime + RepopSum;
            unsigned int const Density = ZoneDensity(*It);
            if (Density > MaxDensity)
                MaxDensity = Density;
        }
        for (std::vector<FSystemZone*>::const_iterator It = Near->begin(); It != Near->end(); ++It)
        {
            RepopSum = (*It)->Population.RepopTime + RepopSum;
            // 06f5bbba-06f5bbd0: FILD qword * 0.6f (DAT_06fbbb38), ftol.
            unsigned int const Density = static_cast<unsigned int>(static_cast<__int64>(
                static_cast<double>(ZoneDensity(*It)) * static_cast<double>(0.6f)));
            if (Density > MaxDensity)
                MaxDensity = Density;
        }

        // 06f5bbe5-06f5bc4d: the average repop time (0 without zones).
        unsigned int const Count = static_cast<unsigned int>(AtShip->size() + Near->size());
        float Average = 0.0f;
        if (Count != 0)
            Average = static_cast<float>(static_cast<double>(RepopSum) / static_cast<double>(Count));

        std::vector<float> Weights;
        Weights.reserve(Count);
        float WeightSum = 0.0f;
        float const MaxDensityF = static_cast<float>(static_cast<double>(MaxDensity));
        for (std::vector<FSystemZone*>::const_iterator It = AtShip->begin(); It != AtShip->end(); ++It)
        {
            float const Weight = static_cast<float>(static_cast<double>(MaxDensityF) /
                static_cast<double>((*It)->Population.RepopTime) * static_cast<double>(Average));
            Weights.push_back(Weight);                                      // FUN_06eb5770
            WeightSum = Weight + WeightSum;
        }
        for (std::vector<FSystemZone*>::const_iterator It = Near->begin(); It != Near->end(); ++It)
        {
            float const Weight = static_cast<float>(static_cast<double>(MaxDensityF) /
                static_cast<double>((*It)->Population.RepopTime) * static_cast<double>(Average));
            Weights.insert(Weights.end(), 1, Weight);                       // FUN_06f937c0
            WeightSum = Weight + WeightSum;
        }

        // 06f5bdc4-06f5be40: the shares go to OutAtShipShares until the AtShip zones run out.
        std::vector<float>* Out = OutAtShipShares;
        for (std::vector<float>::size_type Index = 0; Index < Weights.size(); ++Index)
        {
            if (Index == AtShip->size())
                Out = OutNearShares;
            float Share = 0.0f;
            if (WeightSum != 0.0f)
                Share = Weights[Index] / WeightSum;
            Out->insert(Out->end(), 1, Share);
        }
    }

    // CreateInstance("SpacePop") -> new 8 -> FUN_06f5a550; vtable 06fbbb4c
    // (slot 0 LAB_06f5c3f0, slot 1 FUN_06f5cd80 = IDLL::~IDLL + delete).
    class FSpacePop : public IDLL
    {
    public:
        explicit FSpacePop(unsigned int const* ControllerId);
        void HandleMessage(pub::Controller::EMessage MessageId, void const* Parms) override;

        // FUN_06f5ac10 (RET 0xc): the arrival population around Position.
        void PopulateAround(FSystemInfo* System, FContentPlayerShip* Ship, Vector const* Position);
        // FUN_06f5aee0 (RET 0x10): PopulateAround 1200 beyond the lane ring *RingIndex (away
        // from its neighbour ring).
        void PopulateAtLaneEnd(FSystemInfo* System, FTradeLane* Lane, int const* RingIndex,
                               FContentPlayerShip* Ship);
        // FUN_06f5b4e0: the heartbeat population around every player ship.
        void PopulatePlayers();
        // FUN_06f5b690 (RET 0x1c): one encounter at most, from the first zone list that
        // yields one.
        void PopulateZones(FSystemInfo* System, FContentPlayerShip* Ship,
                           FSpaceObjPlacement const* Placement, Vector const* Velocity,
                           std::vector<FSystemZone*> const* AtShip,
                           std::vector<FSystemZone*> const* Near,
                           std::vector<FSystemZone*> const* Ahead);
        // FUN_06f5beb0 (RET 0x14): fills the zones up to their share of the density budget.
        void PopulateArrival(FSystemInfo* System, FContentPlayerShip* Ship,
                             FSpaceObjPlacement const* Placement,
                             std::vector<FSystemZone*> const* AtShip,
                             std::vector<FSystemZone*> const* Near);

    private:
        // +4: the retail object is 8 bytes; neither the ctor nor the handler writes it.
        unsigned int Field_0x4;
    };

    // FUN_06f5a550 (06f5a550-06f5a5ab, RET 4).
    FSpacePop::FSpacePop(unsigned int const* ControllerId)
    {
        GSpacePopControllerId = *ControllerId;
        pub::Controller::SetHeartbeatInterval(GSpacePopControllerId, 3.0f);
    }

    // FUN_06f5ac10 (06f5ac10-06f5aea5).
    void FSpacePop::PopulateAround(FSystemInfo* System, FContentPlayerShip* Ship,
                                   Vector const* Position)
    {
        // 06f5ac34-06f5ac40: an empty vector local (local_4c, EH state 0, unwind 06faf850)
        // that nothing fills or reads; it outlives the early return and is destroyed last
        // (FUN_06eec8d0 at 06f5ae88). Its element type is not recoverable -- FUN_06eec8d0 is
        // the shared trivially-destructible vector dtor and no element is ever touched -- so
        // it takes the element type of the function's other vectors; it has no effect.
        std::vector<FSystemZone*> Unused;
        // 06f5ac4b-06f5accf: inside an exclusion sphere -> nothing.
        if (SpacePopPositionIsExcluded(&Position->x))
            return;

        std::vector<FSystemZone*> AtShip;
        std::vector<FSystemZone*> Near;
        std::vector<FSystemZone*> Ahead;
        // 06f5ad0d-06f5ad9f: identity orientation at the position.
        FSpaceObjPlacement Placement;
        SetPlacementAt(&Placement, Position);
        bool bBlocked;
        CollectSpacePopZones(System, &Placement, &AtShip, &Near, &Ahead, &bBlocked);
        Ahead.clear();                                                      // 06f5adac-06f5add4
        if (!bBlocked)
            PopulateArrival(System, Ship, &Placement, &AtShip, &Near);
    }

    // FUN_06f5aee0 (06f5aee0-06f5affb).
    void FSpacePop::PopulateAtLaneEnd(FSystemInfo* System, FTradeLane* Lane, int const* RingIndex,
                                      FContentPlayerShip* Ship)
    {
        Vector RingPosition;
        Matrix RingOrientation;
        pub::SpaceObj::GetLocation(Lane->Rings[*RingIndex].ObjectId, RingPosition, RingOrientation);
        // 06f5af15-06f5af45: the neighbour ring -- the second for ring 0, else the previous one.
        unsigned int const OtherId = *RingIndex == 0 ? Lane->Rings[1].ObjectId
                                                     : Lane->Rings[*RingIndex - 1].ObjectId;
        Vector OtherPosition;
        Matrix OtherOrientation;
        pub::SpaceObj::GetLocation(OtherId, OtherPosition, OtherOrientation);

        // 06f5af47-06f5afeb: RingPosition + (RingPosition - OtherPosition) / |.| * 1200
        // (DAT_06fbbb58). dz is stored as a float (FST) and squared as dz * float(dz); the
        // x and y components are stored after scaling, z is added unrounded.
        double const DeltaX = static_cast<double>(RingPosition.x) - static_cast<double>(OtherPosition.x);
        double const DeltaY = static_cast<double>(RingPosition.y) - static_cast<double>(OtherPosition.y);
        double const DeltaZ = static_cast<double>(RingPosition.z) - static_cast<double>(OtherPosition.z);
        float const DeltaZStored = static_cast<float>(DeltaZ);
        double const LengthSq = DeltaZ * static_cast<double>(DeltaZStored) + DeltaY * DeltaY +
                                DeltaX * DeltaX;
        float const InvLength = static_cast<float>(1.0 / std::sqrt(LengthSq));
        float const ScaledX = static_cast<float>(
            static_cast<double>(static_cast<float>(DeltaX * InvLength)) * 1200.0);
        float const ScaledY = static_cast<float>(DeltaY * InvLength * 1200.0);
        Vector Target;
        Target.x = static_cast<float>(static_cast<double>(ScaledX) + static_cast<double>(RingPosition.x));
        Target.y = static_cast<float>(static_cast<double>(ScaledY) + static_cast<double>(RingPosition.y));
        Target.z = static_cast<float>(static_cast<double>(DeltaZStored) * InvLength * 1200.0 +
                                      static_cast<double>(RingPosition.z));
        PopulateAround(System, Ship, &Target);
    }

    // FUN_06f5b4e0 (06f5b4e0-06f5b68e).
    void FSpacePop::PopulatePlayers()
    {
        // 06f5b4ff-06f5b671: the player states of DAT_06fd0bbc in key order.
        std::map<unsigned int, std::unique_ptr<FContentPlayerState>>& States = GetContentPlayerStates();
        for (std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator It = States.begin();
             It != States.end(); ++It)
        {
            FContentPlayerState* State = It->second.get();
            FContentPlayerShip* Ship = State->ShipObject;
            if (Ship == nullptr || Ship->HasField120())
                continue;
            FSystemInfo* System = State->GetSystemRecord();
            FSpaceObjPlacement Placement;
            Ship->GetPlacement(&Placement);                                 // FUN_06eb70a0
            Vector Linear;
            Vector Angular;
            Ship->GetMotion(Linear, Angular);                               // FUN_06eb7170
            std::vector<FSystemZone*> AtShip;
            std::vector<FSystemZone*> Near;
            std::vector<FSystemZone*> Ahead;
            bool bBlocked;
            CollectSpacePopZones(System, &Placement, &AtShip, &Near, &Ahead, &bBlocked);
            if (bBlocked)
                continue;
            // 06f5b5d8-06f5b5e6: in multiplayer the zones ahead are dropped (last = first).
            if (!pub::SinglePlayer())
                Ahead.clear();
            PopulateZones(System, Ship, &Placement, &Linear, &AtShip, &Near, &Ahead);
        }
    }

    // FUN_06f5b690 (06f5b690-06f5bb04).
    void FSpacePop::PopulateZones(FSystemInfo* System, FContentPlayerShip* Ship,
                                  FSpaceObjPlacement const* Placement, Vector const* Velocity,
                                  std::vector<FSystemZone*> const* AtShip,
                                  std::vector<FSystemZone*> const* Near,
                                  std::vector<FSystemZone*> const* Ahead)
    {
        // 06f5b6af-06f5b737: the largest density of the zones at the ship; a near zone takes
        // over with its own density when the maximum is below 0.6 of it (FILD qword compares).
        unsigned int MaxDensity = 0;
        for (std::vector<FSystemZone*>::const_iterator It = AtShip->begin(); It != AtShip->end(); ++It)
        {
            unsigned int const Density = ZoneDensity(*It);
            if (Density > MaxDensity)
                MaxDensity = Density;
        }
        for (std::vector<FSystemZone*>::const_iterator It = Near->begin(); It != Near->end(); ++It)
        {
            unsigned int const Density = ZoneDensity(*It);
            if (!(static_cast<double>(MaxDensity) >=
                  static_cast<double>(Density) * static_cast<double>(0.6f)))
                MaxDensity = Density;
        }
        float const Time = static_cast<float>(GetContentGameTime());          // FUN_06efec10

        // 06f5b742-06f5b771: the position must be outside every exclusion sphere.
        bool const bAll = true;
        bool const bInside = false;
        if (!TestExclusionSpheres(&Placement->Position.x, &GSpacePopExclusionSpheres, &bAll,
                                  &bInside))                                  // FUN_06f616e0
            return;

        int Spawned = 0;
        // 06f5b777-06f5b859: the zones at the ship.
        for (std::vector<FSystemZone*>::const_iterator It = AtShip->begin();
             It != AtShip->end() && Spawned == 0; ++It)
        {
            FSpawnRequest Request(false, 0, GSpacePopControllerId, Ship, *Placement, *Velocity,
                                  &GSpacePopExclusionSpheres);
            float const Scale = 1.0f;
            unsigned int const Limit = MaxDensity;
            Spawned = (*It)->Population.Populate(&Request, &Time, &Scale, &Limit);   // FUN_06ef95d0
        }
        // 06f5b85f-06f5b985: the near zones.
        for (std::vector<FSystemZone*>::const_iterator It = Near->begin();
             It != Near->end() && Spawned == 0; ++It)
        {
            FSpawnRequest Request(false, 1, GSpacePopControllerId, Ship, *Placement, *Velocity,
                                  &GSpacePopExclusionSpheres);
            float const Scale = 1.0f;
            unsigned int const Limit = MaxDensity;
            Spawned = (*It)->Population.Populate(&Request, &Time, &Scale, &Limit);
        }
        // 06f5b98b-06f5bae6: the zones ahead, with a request 5000 ahead of the ship (identity
        // orientation, no velocity) and a third of each zone's own density.
        for (std::vector<FSystemZone*>::const_iterator It = Ahead->begin();
             It != Ahead->end() && Spawned == 0; ++It)
        {
            Vector Offset;
            Offset.x = 0.0f;
            Offset.y = 0.0f;
            Offset.z = GetSpacePopAheadStep() * -2.0f;                         // DAT_06fbbb64
            Vector const AheadPoint = TransformByPlacement(Placement, &Offset);
            FSpaceObjPlacement AheadPlacement;
            SetPlacementAt(&AheadPlacement, &AheadPoint);
            Vector NoVelocity;
            NoVelocity.x = 0.0f;
            NoVelocity.y = 0.0f;
            NoVelocity.z = 0.0f;
            FSpawnRequest Request(false, 2, GSpacePopControllerId, Ship, AheadPlacement, NoVelocity,
                                  &GSpacePopExclusionSpheres);
            // 06f5ba66-06f5ba7d: FILD qword * 0.33f (DAT_06fbbb3c), ftol.
            unsigned int const Limit = static_cast<unsigned int>(static_cast<__int64>(
                static_cast<double>(ZoneDensity(*It)) * static_cast<double>(0.33f)));
            float const Scale = 1.0f;
            Spawned = (*It)->Population.Populate(&Request, &Time, &Scale, &Limit);
        }
    }

    // FUN_06f5beb0 (06f5beb0-06f5c3eb).
    void FSpacePop::PopulateArrival(FSystemInfo* System, FContentPlayerShip* Ship,
                                    FSpaceObjPlacement const* Placement,
                                    std::vector<FSystemZone*> const* AtShip,
                                    std::vector<FSystemZone*> const* Near)
    {
        // 06f5becd-06f5bf43: the density budget -- the largest density at the ship or
        // ftol(0.6 * density) of a near zone.
        unsigned int MaxDensity = 0;
        for (std::vector<FSystemZone*>::const_iterator It = AtShip->begin(); It != AtShip->end(); ++It)
        {
            unsigned int const Density = ZoneDensity(*It);
            if (Density > MaxDensity)
                MaxDensity = Density;
        }
        for (std::vector<FSystemZone*>::const_iterator It = Near->begin(); It != Near->end(); ++It)
        {
            unsigned int const Density = static_cast<unsigned int>(static_cast<__int64>(
                static_cast<double>(ZoneDensity(*It)) * static_cast<double>(0.6f)));
            if (Density > MaxDensity)
                MaxDensity = Density;
        }
        int Budget = static_cast<int>(MaxDensity);
        // 06f5bf49-06f5bf82: in multiplayer the NPC ships already within 9200 count against it.
        if (!pub::SinglePlayer())
        {
            float const NoCone = 0.0f;
            Budget -= CountNpcShipsNear(System, Placement, &GEncounterDensityRadiusMP, &NoCone,
                                        nullptr);                             // FUN_06f67440
        }

        std::vector<float> AtShipShares;
        std::vector<float> NearShares;
        ComputeZoneShares(AtShip, Near, &AtShipShares, &NearShares);         // FUN_06f5bb40
        float const Time = static_cast<float>(GetContentGameTime());          // FUN_06efec10

        // 06f5bfd7-06f5c009: the position must be outside every exclusion sphere.
        bool const bAll = true;
        bool const bInside = false;
        if (!TestExclusionSpheres(&Placement->Position.x, &GSpacePopExclusionSpheres, &bAll,
                                  &bInside))                                  // FUN_06f616e0
            return;

        Vector NoVelocity;
        NoVelocity.x = 0.0f;
        NoVelocity.y = 0.0f;
        NoVelocity.z = 0.0f;
        int Spawned = 0;
        for (int Pass = 0; Pass < 2; ++Pass)
        {
            // 06f5c00f-06f5c1eb (the zones at the ship), then 06f5c1f7-06f5c3a9 (the near
            // zones): each zone gets Budget * its share (-1 for a zero share), at least 1,
            // filled by FUN_06ef9a60 until a call creates nothing or the count reaches it.
            std::vector<FSystemZone*> const* Zones = Pass == 0 ? AtShip : Near;
            std::vector<float> const* Shares = Pass == 0 ? &AtShipShares : &NearShares;
            for (std::vector<FSystemZone*>::size_type Index = 0;
                 !Zones->empty() && Index < Zones->size() && Spawned < Budget; ++Index)
            {
                FSpawnRequest Request(true, 0, GSpacePopControllerId, Ship, *Placement, NoVelocity,
                                      &GSpacePopExclusionSpheres);
                float const Share = (*Shares)[Index];
                // 06f5c0c9-06f5c0fe: FILD Budget * (share, or -1.0 DAT_06fb5e84 when it is 0).
                float Target = static_cast<float>(static_cast<double>(Budget) *
                    (Share == 0.0f ? -1.0 : static_cast<double>(Share)));
                int Count = 0;
                if (!(Target > 0.0f))
                    continue;
                if (Target < 1.0f)
                    Target = 1.0f;
                unsigned int const Limit = static_cast<unsigned int>(static_cast<__int64>(Target));
                for (;;)
                {
                    unsigned int const MaxCount = Limit;
                    int const Created = (*Zones)[Index]->Population.PopulateUpTo(&Request, &Time,
                                                                                &MaxCount);   // FUN_06ef9a60
                    Count += Created;
                    if (Ship != nullptr)
                        Ship->UpdateHostileContacts(0.0f);                  // FUN_06f47590
                    if (Created == 0)
                        break;
                    if (!(static_cast<float>(Count) < Target))
                        break;
                }
                Spawned += Count;
            }
        }
    }

    // LAB_06f5c3f0 (06f5c3f0-06f5c679): byte table 06f5c694 over MessageId - 0xf.
    void FSpacePop::HandleMessage(pub::Controller::EMessage MessageId, void const* Parms)
    {
        using pub::Controller::EMessage;
        unsigned int const* Msg = static_cast<unsigned int const*>(Parms);
        switch (MessageId)
        {
        case EMessage::Heartbeat:
            // 06f5c41b: the heartbeat.
            if (GSpacePopPopulationEnabled != 0)
                PopulatePlayers();
            break;
        case EMessage::SystemEnter:
        {
            // 06f5c439: system enter {system, ?, player, arrival object, position}; the
            // player state is not null-tested.
            if (GSpacePopPopulationEnabled == 0)
                break;
            FContentPlayerState* State = FindContentPlayerState(Msg[2]);    // FUN_06f49b00
            FContentPlayerShip* Ship = State->ShipObject;
            if (Ship == nullptr || Msg[1] == 0)
                break;
            Vector Position = *reinterpret_cast<Vector const*>(Msg + 4);
            PopulateAround(State->GetSystemRecord(), Ship, &Position);
            break;
        }
        case EMessage::SpaceEnter:
        {
            // 06f5c4a2: player launch {system, player, dock target, terminal}; neither the
            // state nor its ship is null-tested.
            if (GSpacePopPopulationEnabled == 0)
                break;
            FContentPlayerState* State = FindContentPlayerState(Msg[1]);
            Vector Position;
            State->ShipObject->GetPosition(&Position);                      // FUN_06eb70f0
            PopulateAround(State->GetSystemRecord(), State->ShipObject, &Position);
            break;
        }
        case EMessage::TradelaneEnter:
        {
            // 06f5c4f0: a ship entered a trade lane {ship, entry ring, exit ring, system}.
            FSystemInfo* System = FindSystemInfo(&Msg[3]);                    // not null-tested
            if (System->FindShip(Msg[0]) == nullptr)                         // FUN_06f73930
                break;
            FTradeLane* Lane;
            System->FindTradeLaneByRing(&Msg[1], &Lane);                     // FUN_06f77e00
            // 06f5c528-06f5c596: the indices of both rings (the loop stops once both are
            // known); -1 when a ring is not on the lane.
            int EntryIndex = -1;
            int ExitIndex = -1;
            for (std::vector<FTradeLaneRing>::size_type Index = 0; Index < Lane->Rings.size(); ++Index)
            {
                unsigned int const RingId = Lane->Rings[Index].ObjectId;
                if (RingId == Msg[1])
                {
                    EntryIndex = static_cast<int>(Index);
                    if (ExitIndex != -1)
                        break;
                }
                else if (RingId == Msg[2])
                {
                    ExitIndex = static_cast<int>(Index);
                    if (EntryIndex != -1)
                        break;
                }
            }
            if (GSpacePopPopulationEnabled == 0)
                break;
            FContentPlayerState* State = FindContentPlayerStateByShip(&Msg[0]);   // FUN_06f49bc0
            if (State == nullptr)
                break;
            FContentPlayerShip* Ship = State->ShipObject;
            // 06f5c5ba: JNC -- the indices compare unsigned.
            if (static_cast<unsigned int>(EntryIndex) < static_cast<unsigned int>(ExitIndex))
            {
                if (GSpacePopEncounterEnabled != 0)
                {
                    int const Direction = 1;
                    System->SpawnTradeLaneAttack(Ship, &Direction, EntryIndex, &Lane);   // FUN_06f782e0
                }
                int const LastRing = static_cast<int>(Lane->Rings.size()) - 1;
                PopulateAtLaneEnd(System, Lane, &LastRing, Ship);
            }
            else
            {
                if (GSpacePopEncounterEnabled != 0)
                {
                    int const Direction = -1;
                    System->SpawnTradeLaneAttack(Ship, &Direction, EntryIndex, &Lane);   // FUN_06f782e0
                }
                int const FirstRing = 0;
                PopulateAtLaneEnd(System, Lane, &FirstRing, Ship);
            }
            break;
        }
        default:
            break;
        }
    }
}

// FUN_06f5a5d0 (06f5a5d0-06f5a795).
void __cdecl SpacePopClampBlockAttitudes(FContentPlayerShip* PlayerShip,
                                         FContentNpcShipParams const* Params)
{
    // 06f5a5d0-06f5a604: without the attack clamp only for a player whose flags have bit 2.
    if (GSpacePopAttackClamp == 0)
    {
        unsigned int const ShipId = PlayerShip->SpaceObjId;
        FContentPlayerState* State = FindContentPlayerStateByShip(&ShipId);   // not null-tested
        if ((State->GetFlags() & 4) == 0)
            return;
    }

    // 06f5a60a-06f5a650: every ship / solar-ish object of the system (type mask 0x7f0000,
    // radius -1 = no limit).
    unsigned int const SystemId = PlayerShip->GetSystem()->SystemId;
    unsigned int Objects[0x40];
    unsigned int ObjectCount = 0;
    Vector Origin;
    Origin.x = 0.0f;
    Origin.y = 0.0f;
    Origin.z = 0.0f;
    pub::System::ScanObjects(SystemId, Objects, 0x40, Origin, -1.0f, 0x7f0000, ObjectCount);
    int const BlockRep = Params->ReputationHandle->GetReputation();          // FUN_06f33f80

    for (unsigned int Index = 0; Index < ObjectCount; ++Index)
    {
        // 06f5a690-06f5a6e1: an NPC ship of the other side (SpacePop vs not), or -- for a
        // SpacePop block -- a player ship.
        bool bOtherSide = Params->ControllerId == GSpacePopControllerId;
        FContentSpaceObj* Object = PlayerShip->GetSystem()->FindSpaceObj(Objects[Index]);
        if (Object != nullptr)
        {
            bOtherSide = static_cast<FContentNpcShip*>(Object)->IsSpacePopShip() != bOtherSide;
        }
        else if (FindContentPlayerStateByShip(&Objects[Index]) == nullptr)
        {
            continue;
        }
        if (!bOtherSide)
            continue;

        // 06f5a6e7-06f5a770: both attitudes raised to at least GSpacePopAttitudeFloor.
        int ObjectRep;
        pub::SpaceObj::GetRep(Objects[Index], ObjectRep);
        float Attitude;
        pub::Reputation::GetAttitude(BlockRep, ObjectRep, Attitude);
        if (Attitude < GSpacePopAttitudeFloor)
            pub::Reputation::SetAttitude(BlockRep, ObjectRep, GSpacePopAttitudeFloor);
        pub::Reputation::GetAttitude(ObjectRep, BlockRep, Attitude);
        if (Attitude < GSpacePopAttitudeFloor)
            pub::Reputation::SetAttitude(ObjectRep, BlockRep, GSpacePopAttitudeFloor);
    }
}

// FUN_06f5b000 (06f5b000-06f5b4d1).
void __cdecl CollectSpacePopZones(FSystemInfo* System, FSpaceObjPlacement const* Placement,
                                  std::vector<FSystemZone*>* OutAtShip,
                                  std::vector<FSystemZone*>* OutNear,
                                  std::vector<FSystemZone*>* OutAhead, bool* OutBlocked)
{
    OutAtShip->clear();                                                     // FUN_06eb5910
    OutNear->clear();
    OutAhead->clear();
    Vector const* Position = &Placement->Position;

    // 06f5b04a-06f5b138: the zones within 10 of the position. Zones with a patrol path are
    // taken; of the others only the one with the smallest radius (0 for a trade lane zone)
    // survives -- the ids of the rest are zeroed so the later lists do not skip them.
    unsigned int AtShipIds[0x14];
    unsigned int AtShipCount = 0;
    pub::System::InZones(System->SystemId, *Position, 10.0f, AtShipIds, 0x14, AtShipCount);
    int Best = -1;
    float BestRadius = -1.0f;
    for (unsigned int Index = 0; Index < AtShipCount; ++Index)
    {
        FSystemZone* Zone = System->FindZone(AtShipIds[Index]);             // FUN_06f72cb0
        if (Zone == nullptr || Zone->bUnpopulated)
            continue;
        if (Zone->Path != nullptr)
        {
            OutAtShip->push_back(Zone);                                     // FUN_06eb5770
            continue;
        }
        float const Radius = Zone->bTradelane ? 0.0f : pub::Zone::GetRadius(Zone->ZoneId);
        if (BestRadius == -1.0f || BestRadius > Radius)
        {
            if (Best != -1)
                AtShipIds[Best] = 0;
            BestRadius = Radius;
            Best = static_cast<int>(Index);
        }
        else
        {
            AtShipIds[Index] = 0;
        }
    }
    // 06f5b13e-06f5b18f: the smallest zone goes to a random place of the list.
    if (Best != -1)
    {
        FSystemZone* BestZone = System->FindZone(AtShipIds[Best]);
        int const Count = static_cast<int>(OutAtShip->size());
        int const Slot = rand() * (Count + 1) / 0x8000;
        OutAtShip->insert(OutAtShip->begin() + Slot, 1, BestZone);          // FUN_06f937c0
    }

    // 06f5b194-06f5b1ba: a zone at the ship with density 0 blocks the population.
    *OutBlocked = false;
    for (std::vector<FSystemZone*>::iterator It = OutAtShip->begin(); It != OutAtShip->end(); ++It)
    {
        if ((*It)->Population.Density == 0)
        {
            *OutBlocked = true;
            return;
        }
    }

    // 06f5b1bc-06f5b256: the population_additive zones within 2000 that were not seen above.
    unsigned int NearIds[0x14];
    unsigned int NearCount = 0;
    pub::System::InZones(System->SystemId, *Position, 2000.0f, NearIds, 0x14, NearCount);
    for (unsigned int Index = 0; Index < NearCount; ++Index)
    {
        bool bSeen = false;
        for (int Other = 0; Other < static_cast<int>(AtShipCount); ++Other)
        {
            if (AtShipIds[Other] == NearIds[Index])
            {
                bSeen = true;
                break;
            }
        }
        if (bSeen)
            continue;
        FSystemZone* Zone = System->FindZone(NearIds[Index]);
        if (Zone != nullptr && !Zone->bUnpopulated && Zone->Population.bPopulationAdditive)
            OutNear->insert(OutNear->end(), 1, Zone);                       // FUN_06f937c0
    }

    // 06f5b258-06f5b4c1: the population_additive zones of the view frustum (70 x 55.4
    // degrees, 4000 deep) whose surface is within 4000, not seen in either list above.
    Transform View;
    View.Rotation = Placement->Orientation;
    View.Translation = Placement->Position;
    unsigned int AheadIds[0x14];
    unsigned int AheadCount = 0;
    float AngleX;
    float AngleY;
    unsigned int const AngleXBits = 0x3f9c61aau;                            // 1.2217305
    unsigned int const AngleYBits = 0x3f77954du;                            // 0.9670910
    std::memcpy(&AngleX, &AngleXBits, sizeof(AngleX));
    std::memcpy(&AngleY, &AngleYBits, sizeof(AngleY));
    pub::System::InZones(System->SystemId, View, AngleX, AngleY, 4000.0f, AheadIds, 0x14,
                         AheadCount);
    for (unsigned int Index = 0; Index < AheadCount; ++Index)
    {
        bool bSeen = false;
        for (int Other = 0; Other < static_cast<int>(AtShipCount); ++Other)
        {
            if (AtShipIds[Other] == AheadIds[Index])
            {
                bSeen = true;
                break;
            }
        }
        for (int Other = 0; !bSeen && Other < static_cast<int>(NearCount); ++Other)
        {
            if (NearIds[Other] == AheadIds[Index])
                bSeen = true;
        }
        if (bSeen)
            continue;
        FSystemZone* Zone = System->FindZone(AheadIds[Index]);
        if (Zone == nullptr)
            continue;
        // 06f5b2fe-06f5b31f: TEST AH,0x41 + JP -- skipped when farther than 4000 (or NaN).
        if (!(pub::Zone::GetDistance(AheadIds[Index], *Position) <= 4000.0f))
            continue;
        if (Zone->bUnpopulated || !Zone->Population.bPopulationAdditive)
            continue;
        OutAhead->push_back(Zone);                                          // 06f5b33f-06f5b4b3
    }
}

namespace
{
    // FUN_06eb2870: vtable 06fb454c (slot 0 FUN_06eb30d0, slot 1 FUN_06eb2940).
    class FBasePop;
    // Retail DAT_06fcef08: the BasePop instance is process-wide.
    FBasePop* GBasePopInstance = nullptr;

    class FBasePop : public IDLL
    {
    public:
        explicit FBasePop(unsigned int InControllerId);
        void HandleMessage(pub::Controller::EMessage MessageId, void const* Parms) override;

        unsigned int ControllerId;                                  // +0x04
        // +0x08 map<player id, the bar characters BasePop created for that player's room
        // visit> (node value +0x10 = vector of FBarCharacterList nodes, first +0x14 / last
        // +0x18). The dtor FUN_06eb2a30 only destroys this map.
        std::map<unsigned int, std::vector<FBarCharacterList::iterator>> PlayerNpcs;
    };

    // TU19 FUN_06eb4720 (cdecl; only caller BasePop 06eb3209): Out receives the index of every
    // character placement of the room whose name, split at '\' and '/' (strtok on a _strdup
    // copy, DAT_06fb4614), holds the tokens "zg", "npc", one starting with 'm' (tolower) and
    // "a" in this order (stricmp; other tokens are skipped); each placement is added once, at
    // the "a".
    void CollectBasePopPlacements(unsigned int RoomId, std::vector<int>* Out)
    {
        int const Count = pub::GF::GetNumCharacterPlacements(RoomId);
        for (int Index = 0; Index < Count; ++Index)
        {
            int State = 0;
            char* Name = _strdup(pub::GF::GetCharacterPlacementName(RoomId, Index));
            for (char* Token = std::strtok(Name, "\\/"); Token != nullptr;
                 Token = std::strtok(nullptr, "\\/"))
            {
                // 06eb4790: jump table over states 0..3, state 4 and up does nothing.
                switch (State)
                {
                case 0:
                    if (_stricmp(Token, "zg") == 0)                     // DAT_06fb4610
                        State = 1;
                    break;
                case 1:
                    if (_stricmp(Token, "npc") == 0)                    // DAT_06fb44d0
                        State = 2;
                    break;
                case 2:
                    if (std::tolower(*Token) == 'm')
                        State = 3;
                    break;
                case 3:
                    if (_stricmp(Token, "a") == 0)
                    {
                        State = 4;
                        Out->push_back(Index);                  // insert(end, 1, Index) inlined
                    }
                    break;
                default:
                    break;
                }
            }
            std::free(Name);
        }
    }

    // 06eb2f63-06eb2fb2 / 06eb2fc7-06eb3007: an element uniformly -- none: the end (no rand);
    // one: the first (no rand); otherwise (rand() * n) / 32768 (CDQ / AND 0x7fff / SAR 0xf).
    template <typename T>
    T* PickUniform(std::vector<T>* Values)
    {
        int const Count = static_cast<int>(Values->size());
        if (Count == 0)
            return Values->data() + Values->size();
        if (Count == 1)
            return Values->data();
        return Values->data() + (rand() * Count) / 32768;
    }

    // FUN_06eb2b10 (cdecl; caller 06eb3026): a bar character of Npc for Player on Placement
    // of RoomId -- the description from the GF_NPC (its nickname's string as the role, no
    // fidget), created at the front of the room (FUN_06ea9340), given the GF_NPC's offer for
    // the player (FUN_06ea8370 / FUN_06ea7ef0) and its script (FUN_06f00b60).
    FBarCharacterList::iterator CreateBasePopCharacter(FContentPlayerState* Player,
                                                       FContentBase* Base, unsigned int RoomId,
                                                       int Placement, FBaseNpc const* Npc)
    {
        FBarCharacterDesc Desc;                                             // FUN_06ea7bf0
        Desc.PlayerId = Player->PlayerId;
        Desc.Affiliation = Npc->Desc.Affiliation;
        Desc.IndividualName = Npc->Desc.IndividualName;
        Desc.Appearance.PartIds[0] = Npc->Desc.Appearance.PartIds[0];
        Desc.Appearance.PartIds[1] = Npc->Desc.Appearance.PartIds[1];
        Desc.Appearance.PartIds[2] = Npc->Desc.Appearance.PartIds[2];
        Desc.Appearance.PartIds[3] = Npc->Desc.Appearance.PartIds[3];
        Desc.Appearance.AccessoryCount = Npc->Desc.Appearance.AccessoryCount;
        for (int Index = 0; Index < static_cast<int>(Desc.Appearance.AccessoryCount); ++Index)
            Desc.Appearance.AccessoryIds[Index] = Npc->Desc.Appearance.AccessoryIds[Index];
        Desc.Voice = Npc->Desc.Voice;
        Desc.RoomId = RoomId;
        Desc.Placement = Placement;
        Desc.Nickname = Npc->Desc.Nickname;
        // 06eb2bcd-06eb2c13: the nickname's string, clamped to 31 characters (null -> empty).
        Desc.Role = Npc->Desc.Nickname.get_string();

        FBarCharacterList::iterator Character = Base->AddBarCharacter(&Desc);   // FUN_06ea9340
        FBarOffer Offer;                                                    // EH state 0
        Npc->ChooseOffer(&Offer, Player);                                   // FUN_06ea8370
        Character->SetOffer(&Offer);                                        // FUN_06ea7ef0
        StartBarCharacterScript(Character);                                 // FUN_06f00b60
        return Character;                                                   // Offer: FUN_06ea7c30
    }

    // FUN_06eb2ca0 (cdecl; caller 06eb32cd): fill the room up to its character_density (less
    // the player's characters already there) with GF_NPCs of the base's factions, each on a
    // placement of Placements (used placements are removed); the created characters go to Out.
    void SpawnBasePopCharacters(std::vector<int>* Placements, FContentPlayerState* Player,
                                FContentBase* Base, unsigned int RoomId,
                                std::vector<FBarCharacterList::iterator>* Out)
    {
        // 06eb2cbb-06eb2d0b.
        int Present = 0;
        FBaseRoomState* RoomState = Base->FindRoomState(&RoomId);           // FUN_06eac110
        if (RoomState != nullptr)
            Present = RoomState->CountCharacters(Player->PlayerId);         // FUN_06eac2a0
        int Wanted = Base->GetRoomDensity(RoomId) - Present;                // FUN_06eac2c0
        if (Wanted <= 0 || Placements->empty())
            return;
        Out->reserve(Wanted);                                               // 06eb2d16-06eb2d6c

        // 06eb2d6f-06eb2e95: a copy of the factions (FUN_06eb33f0) without the ones of weight
        // <= 0 (FCOMP 0, TEST AH,0x41 JNP; NaN stays) or with no GF_NPC; each kept faction's
        // GF_NPCs lose the ones whose room list is non-empty and lacks RoomId.
        std::vector<FBaseFaction> Factions = Base->Factions;
        std::vector<FBaseFaction>::iterator Faction = Factions.begin();
        while (Faction != Factions.end())
        {
            if (!(Faction->Weight <= 0.0f) && !Faction->Npcs.empty())
            {
                std::vector<unsigned int>::iterator Id = Faction->Npcs.begin();
                while (Id != Faction->Npcs.end())
                {
                    // FUN_06f7b0f0 (map::find), not tested.
                    std::vector<unsigned int> const& Rooms = Base->Npcs.find(*Id)->second.Rooms;
                    if (!Rooms.empty() && std::find(Rooms.begin(), Rooms.end(), RoomId) == Rooms.end())
                        Id = Faction->Npcs.erase(Id);                       // FUN_06f69a10
                    else
                        ++Id;
                }
                if (!Faction->Npcs.empty())
                {
                    ++Faction;
                    continue;
                }
            }
            Faction = Factions.erase(Faction);            // FUN_06eb3ae0 copy + FUN_06eb3580
        }

        // 06eb2ea1-06eb3093: a faction by weight, one of its GF_NPCs and a placement uniformly;
        // the character is created, the placement and the GF_NPC are used up (an emptied
        // faction is dropped, FUN_06eb3470).
        while (!Factions.empty() && Wanted > 0 && !Placements->empty())
        {
            FBaseFaction* Picked = PickByWeight(
                Factions.data(), Factions.data() + Factions.size(),
                [](FBaseFaction const& Entry) { return Entry.Weight; });
            std::vector<unsigned int>* Npcs = &Picked->Npcs;
            unsigned int* NpcId = PickUniform(Npcs);
            FBaseNpc const* Npc = &Base->Npcs.find(*NpcId)->second;          // FUN_06f7b0f0
            int* Placement = PickUniform(Placements);
            Out->push_back(CreateBasePopCharacter(Player, Base, RoomId, *Placement, Npc));   // FUN_06eb5770
            Placements->erase(Placements->begin() + (Placement - Placements->data()));   // FUN_06f6a640
            --Wanted;
            Npcs->erase(Npcs->begin() + (NpcId - Npcs->data()));             // FUN_06f69a10
            if (Npcs->empty())
                Factions.erase(Factions.begin() + (Picked - Factions.data()));
        }
        // 06eb3098: only after the draw (the early returns above skip it).
        ReleaseRmDestSystems();                                             // FUN_06f4d650
    }

    // FUN_06eb2870 (RET 4, ControllerId by value).
    FBasePop::FBasePop(unsigned int InControllerId)
        : ControllerId(InControllerId)
    {
        pub::Controller::SetHeartbeatInterval(ControllerId, -1.0f);
        if (GBasePopInstance != nullptr)
            __debugbreak();                                         // 06eb2925: INT3
        GBasePopInstance = this;
    }

    // FUN_06eb30d0 (06eb30d0-06eb3383, RET 8).
    void FBasePop::HandleMessage(pub::Controller::EMessage MessageId, void const* Parms)
    {
        unsigned int const* Msg = static_cast<unsigned int const*>(Parms);
        if (MessageId == pub::Controller::EMessage::LocationEnter)
        {
            // 06eb31b0: location enter {room, player}.
            FContentPlayerState* State = FindContentPlayerState(Msg[1]);    // FUN_06f49b00
            if (State == nullptr)
                return;
            FContentBase* Base = State->GetBaseRecord();                    // FUN_06f47980
            if (Base == nullptr)
                return;
            std::vector<int> Placements;
            CollectBasePopPlacements(Msg[0], &Placements);                  // FUN_06eb4720

            // 06eb320e-06eb329e: placements the player already has a character or a lock on
            // are dropped.
            unsigned int RoomId = Msg[0];
            FBaseRoomState* RoomState = Base->FindRoomState(&RoomId);       // FUN_06eac110
            if (RoomState != nullptr)
            {
                std::vector<int>::iterator Placement = Placements.begin();
                while (Placement != Placements.end())
                {
                    if (RoomState->HasCharacter(State->PlayerId, *Placement)     // FUN_06eac190
                        || RoomState->HasLock(State->PlayerId, *Placement))      // FUN_06eac160
                        Placement = Placements.erase(Placement);            // FUN_06f6a640
                    else
                        ++Placement;
                }
            }

            // 06eb32a2-06eb3350: the characters, kept per player when there are any (insert:
            // an existing entry stays as it is).
            std::vector<FBarCharacterList::iterator> Characters;
            SpawnBasePopCharacters(&Placements, State, Base, Msg[0], &Characters);   // FUN_06eb2ca0
            if (!Characters.empty())
                PlayerNpcs.insert(std::make_pair(Msg[1], Characters));      // FUN_06eb3620
        }
        else if (MessageId == pub::Controller::EMessage::LocationExit)
        {
            // 06eb3104: location exit {room, player}.
            std::map<unsigned int, std::vector<FBarCharacterList::iterator>>::iterator Found =
                PlayerNpcs.find(Msg[1]);                                    // FUN_06f7b0f0
            if (Found == PlayerNpcs.end())
                return;
            // Neither the state nor its base is tested.
            FContentBase* Base = FindContentPlayerState(Msg[1])->GetBaseRecord();
            for (std::vector<FBarCharacterList::iterator>::iterator It = Found->second.begin();
                 It != Found->second.end(); ++It)
            {
                (*It)->SetBehavior(0xffffffffu);                            // FUN_06ea7ce0(-1)
                FBarOffer NoOffer;                                          // EH state 0
                (*It)->SetOffer(&NoOffer);                                  // FUN_06ea7ef0
                Base->RemoveBarCharacter(-1, *It, nullptr);                 // FUN_06ea94c0
            }
            PlayerNpcs.erase(Found);                                        // FUN_06f30b60
        }
    }
}

// FUN_06eb3390 (06eb3390-06eb33e5): true when the BasePop instance exists and tracks
// room NPCs for this player.
bool __cdecl BasePopHasPlayer(unsigned int PlayerId)
{
    FBasePop* Instance = GBasePopInstance;
    if (Instance == nullptr)
        return false;
    return Instance->PlayerNpcs.find(PlayerId) != Instance->PlayerNpcs.end();
}

// FUN_06f5a7a0 (06f5a7a0-06f5a7a5).
unsigned char __cdecl SpacePopGetHostileClamp()
{
    return GSpacePopHostileClamp;
}

// FUN_06f5a7b0 (06f5a7b0-06f5a7b9): the byte as passed.
void __cdecl SpacePopSetHostileClamp(unsigned char EnabledByte)
{
    GSpacePopHostileClamp = EnabledByte;
}

// FUN_06f5a7c0 (06f5a7c0-06f5a7d3): stored only in single player.
void __cdecl SpacePopSetSolarDetection(unsigned char EnabledByte)
{
    if (pub::SinglePlayer())
        GSpacePopSolarDetection = EnabledByte;
}

// FUN_06f5a7e0 (06f5a7e0-06f5a7f1): the stored byte in single player, else 1.
unsigned char __cdecl SpacePopGetSolarDetection()
{
    if (!pub::SinglePlayer())
        return 1;
    return GSpacePopSolarDetection;
}

void __cdecl SpacePopSetPopulationEnabled(unsigned char EnabledByte)
{
    // FUN_06f5a800 stores the argument byte without normalizing it.
    GSpacePopPopulationEnabled = EnabledByte;
}

unsigned char __cdecl SpacePopGetPopulationEnabled()
{
    // FUN_06f5a810 returns the byte as stored.
    return GSpacePopPopulationEnabled;
}

void __cdecl SpacePopSetEncounterEnabled(unsigned char EnabledByte)
{
    // FUN_06f5a820 stores the argument byte without normalizing it.
    GSpacePopEncounterEnabled = EnabledByte;
}

unsigned char __cdecl SpacePopGetEncounterEnabled()
{
    // FUN_06f5a830 returns the byte as stored.
    return GSpacePopEncounterEnabled;
}

void __cdecl SpacePopSetAttackClamp(unsigned char EnabledByte)
{
    GSpacePopAttackClamp = EnabledByte;
}

// FUN_06eeaee0 (cdecl, 06eeaee0-06eeaf04; the COMDAT copy the linker kept is in TU25):
// each component is subtracted on the x87 stack and stored as a float, z first.
Vector __cdecl SubtractVectors(Vector const& A, Vector const& B)
{
    Vector Result;
    Result.z = A.z - B.z;
    Result.y = A.y - B.y;
    Result.x = A.x - B.x;
    return Result;
}

// FUN_06f5aeb0 (cdecl, 06f5aeb0-06f5aedf): y = bits 0x5f3759df - (bits(Value) SAR 1);
// FLD y, FMUL y, FMUL Value, FMUL 0.5f (DAT_06fb605c), FSUBR 1.5f (DAT_06fb6058), FMUL y --
// returned on the x87 stack, kept in double here.
double __cdecl FastReciprocalSqrt(float Value)
{
    int Bits;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    Bits = 0x5f3759df - (Bits >> 1);
    float Guess;
    std::memcpy(&Guess, &Bits, sizeof(Guess));
    double const Step = 1.5f - static_cast<double>(Guess) * Guess * Value * 0.5f;
    return Step * Guess;
}

bool __cdecl SpacePopPositionIsExcluded(float const* Position)
{
    // FUN_06f5ac10, 06f5ac4b-06f5accf: the sphere scan before any transform construction
    // or encounter selection; the position is copied first (06f5ac51-06f5ac70).
    Vector Point;
    Point.x = Position[0];
    Point.y = Position[1];
    Point.z = Position[2];
    for (size_t Index = 0; Index < GSpacePopExclusionSpheres.size(); ++Index)
    {
        FSpacePopExclusionSphere const* Sphere = &GSpacePopExclusionSpheres[Index];
        Vector Center;
        Center.x = Sphere->X;
        Center.y = Sphere->Y;
        Center.z = Sphere->Z;
        Vector const Delta = SubtractVectors(Center, Point);                    // 06f5ac81
        // 06f5ac86-06f5aca2: (z*z + y*y) + x*x, FSTP float.
        float const Square = static_cast<float>(
            (static_cast<double>(Delta.z) * Delta.z + static_cast<double>(Delta.y) * Delta.y) +
            static_cast<double>(Delta.x) * Delta.x);
        double const Length = FastReciprocalSqrt(Square) * Square;              // 06f5acab
        // 06f5acb7-06f5acbf: FCOMP Radius, TEST AH,5, JNP -- only C0 alone (less) takes the
        // excluded exit; equal and unordered go on to the next sphere.
        if (Length < static_cast<double>(Sphere->Radius))
            return true;
    }
    return false;
}

void __cdecl SpacePopAddExclusionSphere(float X, float Y, float Z, float Radius)
{
    // FUN_06f5a840: retain the first matching sphere; compare each coordinate
    // independently, then the absolute radius difference.
    for (size_t Index = 0; Index < GSpacePopExclusionSpheres.size(); ++Index)
    {
        FSpacePopExclusionSphere const& Sphere = GSpacePopExclusionSpheres[Index];
        double DeltaX = static_cast<double>(Sphere.X) - static_cast<double>(X);
        if (!(DeltaX * DeltaX <= static_cast<double>(0.0001f)))
            continue;
        double DeltaY = static_cast<double>(Sphere.Y) - static_cast<double>(Y);
        if (!(DeltaY * DeltaY <= static_cast<double>(0.0001f)))
            continue;
        double DeltaZ = static_cast<double>(Sphere.Z) - static_cast<double>(Z);
        if (!(DeltaZ * DeltaZ <= static_cast<double>(0.0001f)))
            continue;
        double DeltaRadius = static_cast<double>(Sphere.Radius) - static_cast<double>(Radius);
        if (DeltaRadius < 1.0 && DeltaRadius > -1.0)
            return;
    }

    FSpacePopExclusionSphere Sphere = { X, Y, Z, Radius };
    GSpacePopExclusionSpheres.push_back(Sphere);
}

void __cdecl SpacePopRemoveExclusionSphere(float X, float Y, float Z, float Radius)
{
    // FUN_06f5aaf0: erase only the first matching record, shifting later
    // records one position left while keeping the allocation.
    for (size_t Index = 0; Index < GSpacePopExclusionSpheres.size(); ++Index)
    {
        FSpacePopExclusionSphere const& Sphere = GSpacePopExclusionSpheres[Index];
        double DeltaX = static_cast<double>(Sphere.X) - static_cast<double>(X);
        if (!(DeltaX * DeltaX <= static_cast<double>(0.0001f)))
            continue;
        double DeltaY = static_cast<double>(Sphere.Y) - static_cast<double>(Y);
        if (!(DeltaY * DeltaY <= static_cast<double>(0.0001f)))
            continue;
        double DeltaZ = static_cast<double>(Sphere.Z) - static_cast<double>(Z);
        if (!(DeltaZ * DeltaZ <= static_cast<double>(0.0001f)))
            continue;
        double DeltaRadius = static_cast<double>(Sphere.Radius) - static_cast<double>(Radius);
        if (DeltaRadius < 1.0 && DeltaRadius > -1.0)
        {
            GSpacePopExclusionSpheres.erase(GSpacePopExclusionSpheres.begin() + Index);
            return;
        }
    }
}

void __cdecl SpacePopClearExclusionSpheres()
{
    // FUN_06f5abc0: end = begin; keep the backing allocation for reuse.
    GSpacePopExclusionSpheres.clear();
}

IDLL* CreateSpacePop(char const* /*Name*/, unsigned int ControllerId, void const* /*Parms*/)
{
    // FUN_06eeec50: operator new(8), FUN_06f5a550(&ControllerId).
    return new (std::nothrow) FSpacePop(&ControllerId);
}

IDLL* CreateBasePop(char const* /*Name*/, unsigned int ControllerId, void const* /*Parms*/)
{
    // FUN_06eeec50: operator new(0x1c), FUN_06eb2870(ControllerId).
    return new (std::nothrow) FBasePop(ControllerId);
}
