// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "hud_target.h"

#include "character_select_equip.h" // GetPlayerBaseIdRetail (DAT_00673358)
#include "client_gameobj.h"         // FClientGameobj : IObjInspectImpl
#include "client_objdb.h"           // ClientObjDb_FindObject (FUN_005416c0)
#include "font_string.h"
#include "globals.h"                // IsClientServerTickPaused (FUN_0042d730)
#include "ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "input_manager.h"          // GetClientMouseCursorDrawFlag, GetClientSoftwareCursor
#include "menu_universe_base.h"     // IsClientLocationInTransition (DAT_00668761)
#include "misc_util.h"              // FmtStrSprintf (FUN_0041c970)
#include "nn_modal.h"               // G_bNnScreenUp (DAT_0067dcc8)
#include "nn_text_scratch.h"        // G_NnTextScratchA/B (DAT_0066dc60 / DAT_0066fc60)
#include "nn_ui_pulse.h"            // G_HudChromeColorRgba (DAT_00679bb4)
#include "engine_light.h"           // ScaleVec3 (FUN_00411d60)
#include "player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "player_spawn.h"           // IsPlayerShipId (FUN_00548eb0)
#include "space_camera.h"           // AsCamera
#include "star_sphere.h"            // Win3DIdentityRotation3x3 (FUN_004215b0)
#include "win3d_immediate.h"        // Win3DSetViewport (FUN_00422610), FUN_00422910
#include "space_letterbox.h"        // IsLetterboxVisible (FUN_0041a3e0)
#include "tooltip_hover.h"          // FToolTipHoverInfo, NnToolTipFeedHover (FUN_004be370)
#include "win3d.h"                  // Win3DGetCamera (DAT_0067dbf8)

#include "common_cequip.h"
#include "common_cobject.h"
#include "common_csys.h"            // Acquire3DMathEngine (FUN_005b73e0)
#include "common_iface.h"           // IObjRW, IObjInspectImpl, ObjRwFromWatchable
#include "common_misc.h"            // SUBOBJ_ID_NONE, ICamera
#include "common_reputation.h"      // Reputation::Vibe::GetAffiliation / GetName, get_short_name
#include "common_universe.h"        // Universe::get_system

#include "DALib.h"
#include "engine/engbase.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace
{
    // DAT_00674b1c -- the 3DMathEngine HUD_Target_Base.cpp acquires lazily (FUN_005b73e0) in
    // FUN_004e65d0 / FUN_004e6880 / FUN_004e68d0 / FUN_004e69c0 / FUN_004e79f0.
    I3DMathEngine* G_HudTargetMath = nullptr;

    // DAT_006649c0 -- FUN_00422c70's own lazily acquired 3DMathEngine.
    I3DMathEngine* G_HudPickMath = nullptr;

    I3DMathEngine* HudTargetMath()
    {
        if (G_HudTargetMath == nullptr)
        {
            G_HudTargetMath = Acquire3DMathEngine();
        }
        return G_HudTargetMath;
    }

    // Engine instance handles travel as void*; a GetRootIndex / get_index long converts 1:1.
    void* InstanceFromIndex(long Index)
    {
        return reinterpret_cast<void*>(static_cast<intptr_t>(Index));
    }

    void* NoInstance()
    {
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }

    FTransform const* InstanceTransform(void* Instance)
    {
        // IEngine slot 40 (+0xa0) returns the instance's 12-float transform.
        return reinterpret_cast<FTransform const*>(DALib::Engine->GetInstanceTransform(Instance));
    }

    // DAT_006127b8 -- the slack PickSubTarget hands FHudSpherePick::Test (0x3ecccccd).
    float const kSubTargetPickSlack = 0.4f;

    // .rdata 0x3f333333 -- the chrome dimming factor of SetViewObject's wireframe colour.
    float const kViewWireDim = 0.7f;

    // DAT_00611400 -- FUN_0045a7c0's 0x15 {type, ids_name} pairs.
    struct FCollisionTypeIds
    {
        unsigned Type;
        unsigned Ids;
    };

    FCollisionTypeIds const kCollisionTypeIds[0x15] = {
        {0x00000000u, 0x708u}, {0x00000001u, 0x708u}, {0x00000002u, 0x709u},
        {0x00000004u, 0x70au}, {0x00000008u, 0x70bu}, {0x00000010u, 0x70cu},
        {0x00000020u, 0x70du}, {0x00000040u, 0x70eu}, {0x00000080u, 0x70fu},
        {0x00000100u, 0x710u}, {0x00000200u, 0x711u}, {0x00000400u, 0x712u},
        {0x00000800u, 0x713u}, {0x00001000u, 0x714u}, {0x00002000u, 0x715u},
        {0x00004000u, 0x716u}, {0x00008000u, 0x717u}, {0x00010000u, 0x718u},
        {0x00020000u, 0x719u}, {0x00040000u, 0x71au}, {0x00080000u, 0x71bu},
    };

    // The inlined chain retail spells out at every use (0x004e6166.., 0x004e7a20.., 0x004e7e2e..):
    // watched -> -8 (IObjRW) -> +0xc (IObjInspectImpl's CBase) -> +4 (CBase::Object), null
    // anywhere along it -> null.
    CObject* CObjectOfWatcher(BaseWatcher const& Watcher)
    {
        Watchable const* const Watched = Watcher.target();
        if (Watched == nullptr)
        {
            return nullptr;
        }
        IObjRW* const Rw = ObjRwFromWatchable(const_cast<Watchable*>(Watched));
        if (Rw == nullptr)
        {
            return nullptr;
        }
        IObjInspectImpl* const Impl = static_cast<IObjInspectImpl*>(Rw);
        CBase const* const Base = Impl;
        return Base->cobject();
    }

    // FUN_004104c0 (cdecl) -- clamp to 0..255.
    int ClampByte(int Value)
    {
        if (Value < 0)
        {
            return 0;
        }
        if (Value > 0xff)
        {
            return 0xff;
        }
        return Value;
    }

    // FILD byte / FMUL 0.7f / ftol: the product of an 8-bit integer and a float is exact in
    // double, and ftol truncates.
    int DimChromeByte(unsigned char Byte)
    {
        return static_cast<int>(static_cast<double>(Byte) * static_cast<double>(kViewWireDim));
    }
}

// =============================================================================================
// FHudInstanceBounds
// =============================================================================================

FVector* HudInverseTransformPoint(FTransform const* Xform, FVector* Out, FVector const* In)
{
    // FUN_004e6880: InverseTransform into a local, then copied to Out.
    FVector Local;
    HudTargetMath()->InverseTransform(Local, *Xform, *In);
    *Out = Local;
    return Out;
}

FHudInstanceBounds::FHudInstanceBounds(void* Instance, FTransform const* Xform,
                                       bool bIncludeShieldParts)
{
    // FUN_004e65d0: the root part's sphere (extent flag 0, radius pre-zeroed).
    float Radius = 0.0f;
    FVector Center;
    DALib::Engine->GetInstanceExtent(Instance, 0, &Radius, &Center.X);
    FTransform const* const Transform = InstanceTransform(Instance);
    FVector World;
    HudTargetMath()->TransformXform(World, *Transform, Center);
    Center = World;

    // Two separate InverseTransform calls, one for Min and one for Max (0x004e6676 / 0x004e66d5).
    FVector Local;
    HudTargetMath()->InverseTransform(Local, *Xform, Center);
    Min.X = Local.X - Radius;
    Min.Y = Local.Y - Radius;
    Min.Z = Local.Z - Radius;

    HudTargetMath()->InverseTransform(Local, *Xform, Center);
    Max.X = Local.X + Radius;
    Max.Y = Local.Y + Radius;
    Max.Z = Local.Z + Radius;

    // 0x004e6717..0x004e686d: the child loop -- the same body as FUN_004e69c0.
    ExpandChildren(Instance, Xform, bIncludeShieldParts);
}

FVector* FHudInstanceBounds::GetCenter(FVector* Out) const
{
    // FUN_004e6580 (DAT_005c75e0 = 0.5f).
    Out->X = (Min.X + Max.X) * 0.5f;
    Out->Y = (Min.Y + Max.Y) * 0.5f;
    Out->Z = (Min.Z + Max.Z) * 0.5f;
    return Out;
}

void FHudInstanceBounds::ExpandChildren(void* Instance, FTransform const* Xform,
                                        bool bIncludeShieldParts)
{
    // FUN_004e69c0: GetRelatedInstance(Instance, 1, cursor) from -1 until it returns -1.
    void* Child = DALib::Engine->GetRelatedInstance(Instance, 1, NoInstance());
    while (Child != NoInstance())
    {
        if (bIncludeShieldParts || !IsShieldPart(Child))
        {
            FVector Center;
            float const Radius = GetPartSphere(Child, &Center);

            // Min: v - (r, r, r), component-wise minimum (FCOMP / TEST AH,5 / JP: taken only
            // when strictly below).
            FVector Local;
            HudInverseTransformPoint(Xform, &Local, &Center);
            FVector Low;
            Low.X = Local.X - Radius;
            Low.Y = Local.Y - Radius;
            Low.Z = Local.Z - Radius;
            if (Low.X < Min.X)
            {
                Min.X = Low.X;
            }
            if (Low.Y < Min.Y)
            {
                Min.Y = Low.Y;
            }
            if (Low.Z < Min.Z)
            {
                Min.Z = Low.Z;
            }

            // Max: the transform again, v + (r, r, r), component-wise maximum (TEST AH,0x41 /
            // JNZ: taken only when strictly above).
            HudInverseTransformPoint(Xform, &Local, &Center);
            FVector High;
            High.X = Local.X + Radius;
            High.Y = Local.Y + Radius;
            High.Z = Local.Z + Radius;
            if (High.X > Max.X)
            {
                Max.X = High.X;
            }
            if (High.Y > Max.Y)
            {
                Max.Y = High.Y;
            }
            if (High.Z > Max.Z)
            {
                Max.Z = High.Z;
            }

            ExpandChildren(Child, Xform, bIncludeShieldParts);
        }
        Child = DALib::Engine->GetRelatedInstance(Instance, 1, Child);
    }
}

float FHudInstanceBounds::GetPartSphere(void* Instance, FVector* OutCenter) const
{
    // FUN_004e68d0: radius pre-zeroed, extent flag 0 straight into OutCenter, then OutCenter is
    // moved through the instance transform.
    float Radius = 0.0f;
    DALib::Engine->GetInstanceExtent(Instance, 0, &Radius, &OutCenter->X);
    FTransform const* const Transform = InstanceTransform(Instance);
    FVector World;
    HudTargetMath()->TransformXform(World, *Transform, *OutCenter);
    *OutCenter = World;
    return Radius;
}

bool FHudInstanceBounds::IsShieldPart(void* Instance) const
{
    // FUN_004e6960: the instance's user data is its CObject; retail inlines inst_to_part and
    // is_shield_part over the +0x68..+0x6c part map (no null test on the user data).
    CObject const* const Object = static_cast<CObject const*>(
        DALib::Engine->GetInstanceUserData(Instance));
    long const EngineInst = static_cast<long>(reinterpret_cast<intptr_t>(Instance));
    return Object->is_shield_part(Object->inst_to_part(EngineInst));
}

// =============================================================================================
// FHudSpherePick
// =============================================================================================

FHudSpherePick::FHudSpherePick(unsigned char Flag)
    : HitCount(0)
{
    // FUN_00422ba0.
    BestKey = 0.0f;
    Field50 = -1;
    Field4c = Flag;
}

void FHudSpherePick::Setup(FVector const* From, FVector const* To)
{
    // FUN_00422bc0: FDIVR 1.0 by the FSQRT of the squared length, then each component scaled.
    Origin = *From;
    Dir.X = To->X - Origin.X;
    Dir.Y = To->Y - Origin.Y;
    Dir.Z = To->Z - Origin.Z;
    double const Inverse = 1.0 / sqrt(static_cast<double>(Dir.X) * Dir.X +
                                      static_cast<double>(Dir.Y) * Dir.Y +
                                      static_cast<double>(Dir.Z) * Dir.Z);
    Dir.X = static_cast<float>(Inverse * Dir.X);
    Dir.Y = static_cast<float>(Inverse * Dir.Y);
    Dir.Z = static_cast<float>(Inverse * Dir.Z);
    Field18 = -1.0f;
    HitCount = 0;
    Field50 = -1;
    BestKey = 0.0f;
}

float HudRaySphereClosest(FVector const* Origin, FVector const* Dir, FVector const* Center,
                          float Radius, FVector* OutRay, FVector* OutSurface,
                          FTransform const* Xform)
{
    // FUN_00422c70.
    if (G_HudPickMath == nullptr)
    {
        G_HudPickMath = Acquire3DMathEngine();
    }
    FVector World;
    G_HudPickMath->TransformXform(World, *Xform, *Center);

    FVector ToCenter;
    ToCenter.X = World.X - Origin->X;
    ToCenter.Y = World.Y - Origin->Y;
    ToCenter.Z = World.Z - Origin->Z;

    // t = V.D summed x, z, y; the branch is taken on t < 0 (FCOM 0.0 / TEST AH,5 / JP).
    float const Along = ToCenter.X * Dir->X + ToCenter.Z * Dir->Z + ToCenter.Y * Dir->Y;
    float Cosine = 1.0f;
    if (Along < 0.0f)
    {
        *OutRay = *Origin;
    }
    else
    {
        OutRay->X = Along * Dir->X + Origin->X;
        OutRay->Y = Along * Dir->Y + Origin->Y;
        OutRay->Z = Along * Dir->Z + Origin->Z;
        Cosine = Along / sqrtf(ToCenter.Z * ToCenter.Z + ToCenter.Y * ToCenter.Y +
                               ToCenter.X * ToCenter.X);
    }

    FVector Gap;
    Gap.X = OutRay->X - World.X;
    Gap.Y = OutRay->Y - World.Y;
    Gap.Z = OutRay->Z - World.Z;
    float const Distance = sqrtf(Gap.Z * Gap.Z + Gap.Y * Gap.Y + Gap.X * Gap.X);
    float const Scale = Radius / Distance;
    OutSurface->X = World.X + Gap.X * Scale;
    OutSurface->Y = World.Y + Gap.Y * Scale;
    OutSurface->Z = World.Z + Gap.Z * Scale;

    // FCOMP Radius / TEST AH,5 / JP: the ray point snaps to the surface only when inside.
    if (Distance < Radius)
    {
        *OutRay = *OutSurface;
    }
    return Cosine;
}

bool FHudSpherePick::Test(void* Instance, float Slack)
{
    // FUN_00422e30: every child first.
    bool bHit = false;
    void* Child = DALib::Engine->GetRelatedInstance(Instance, 1, NoInstance());
    while (Child != NoInstance())
    {
        if (Test(Child, Slack))
        {
            bHit = true;
        }
        Child = DALib::Engine->GetRelatedInstance(Instance, 1, Child);
    }

    // Then this instance's extent-flag-1 sphere.
    float Radius;
    FVector Center;
    DALib::Engine->GetInstanceExtent(Instance, 1, &Radius, &Center.X);
    FTransform const* const Transform = InstanceTransform(Instance);
    FVector RayPoint;
    FVector SurfacePoint;
    float const Cosine = HudRaySphereClosest(&Origin, &Dir, &Center, Radius, &RayPoint,
                                             &SurfacePoint, Transform);

    // FCOM double 0.5 / TEST AH,0x41 / JNZ: only a cosine strictly above 0.5 goes on.
    if (!(Cosine > 0.5))
    {
        return bHit;
    }
    float const Key = -Cosine;
    float const Dx = RayPoint.X - SurfacePoint.X;
    float const Dy = RayPoint.Y - SurfacePoint.Y;
    float const Dz = RayPoint.Z - SurfacePoint.Z;
    float const Gap = sqrtf(Dx * Dx + Dz * Dz + Dy * Dy);
    // FCOMP Slack / TEST AH,0x41 / JP: on when the gap is below or equal to the slack.
    if (!(Gap <= Slack))
    {
        return bHit;
    }
    // FCOMP BestKey / TEST AH,5 / JP: a later hit must be strictly better.
    if (HitCount != 0 && !(Key < BestKey))
    {
        return bHit;
    }
    BestKey = Key;
    HitPoint = RayPoint;
    HitSurface = SurfacePoint;
    ++HitCount;
    return true;
}

// =============================================================================================
// Free helpers
// =============================================================================================

bool HudIsMousePickBlocked()
{
    // FUN_0045a740.
    if (G_bNnScreenUp)
    {
        return true;
    }
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        return true;
    }
    if (IsClientServerTickPaused())
    {
        return true;
    }
    if (IsClientLocationInTransition()) // DAT_00668761 (0x0045a75b)
    {
        return true;
    }
    // FUN_004c4810 -- docked: XOR AL,AL / RET.
    if ((GetPlayerBaseIdRetail() & 0x3fffffffu) != 0)
    {
        return false;
    }
    // NEG / SBB / INC over FUN_0054baf0.
    return GetPlayerObjRW() == nullptr;
}

unsigned HudCollisionGroupTypeIds(unsigned CollisionGroupType)
{
    // FUN_0045a7c0.
    for (int Index = 0; Index < 0x15; ++Index)
    {
        if (kCollisionTypeIds[Index].Type == CollisionGroupType)
        {
            return kCollisionTypeIds[Index].Ids;
        }
    }
    return 0;
}

unsigned long* HudDamageColorArgb(unsigned long* OutArgb, float Damage)
{
    // FUN_0045a1a0. The first two bands: FCOMP / TEST AH,5 / JP leaves a band on "not below"
    // (unordered included), so each is taken only for Damage strictly below its bound.
    if (Damage < 0.25f)
    {
        memcpy(OutArgb, G_HudChromeColorRgba, sizeof(G_HudChromeColorRgba)); // DAT_00679bb4
        return OutArgb;
    }
    if (Damage < 0.5f)
    {
        *OutArgb = 0xff4080ffu; // bytes ff, 80, 40, ff
        return OutArgb;
    }
    // TEST AH,0x41 / JNZ: "below, equal or unordered" takes the third band.
    if (!(Damage > 0.75f))
    {
        *OutArgb = 0xff4040ffu; // bytes ff, 40, 40, ff
        return OutArgb;
    }

    // 0x0045a1f7: the pulse phase stored as a float over the argument, flipped by the
    // direction byte; FLD 255.0 / FMUL ST1 / ftol, then FILD of that byte / FMUL ST1 / ftol.
    float const Phase = NnUiPulsePhaseNorm();
    double const Level = (NnUiPulseDirection() != 0) ? 1.0 - static_cast<double>(Phase)
                                                      : static_cast<double>(Phase);
    unsigned char const Red = static_cast<unsigned char>(static_cast<int>(255.0 * Level));
    unsigned char const Green =
        static_cast<unsigned char>(static_cast<int>(static_cast<double>(Red) * Level));
    *OutArgb = 0xff000000u | (static_cast<unsigned long>(Green) << 16) |
               (static_cast<unsigned long>(Green) << 8) | static_cast<unsigned long>(Red);
    return OutArgb;
}

int* HudProjectScreenRect(int* OutRect4, FVector const* A, FVector const* B)
{
    // FUN_004c71b0. Retail's Project outputs x / y / z and the rectangle's fallback values share
    // the four stack dwords [ESP+0xc..0x18]. Project writes nothing when it fails, and the pair
    // of a failed projection takes the raw bits left there: left / top from the x / y slots,
    // right / bottom from the z slot and a fourth dword nothing writes. The port keeps the same
    // overlay; what it holds on a failure is as undefined as in retail.
    union
    {
        float F[4];
        int I[4];
    } Scratch;
    ICamera* const Camera = AsCamera(Win3DGetCamera());

    int Left = 0;
    int Top = 0;
    if (static_cast<unsigned char>(Camera->Project(&Scratch.F[0], &Scratch.F[1], &Scratch.F[2],
                                                   &A->X)) != 0)
    {
        Left = static_cast<int>(Scratch.F[0]);
        Top = static_cast<int>(Scratch.F[1]);
    }
    else
    {
        Left = Scratch.I[0];
        Top = Scratch.I[1];
    }

    int Right = 0;
    int Bottom = 0;
    if (static_cast<unsigned char>(Camera->Project(&Scratch.F[0], &Scratch.F[1], &Scratch.F[2],
                                                   &B->X)) != 0)
    {
        Right = static_cast<int>(Scratch.F[0]);
        Bottom = static_cast<int>(Scratch.F[1]);
    }
    else
    {
        Right = Scratch.I[2];
        Bottom = Scratch.I[3];
    }

    OutRect4[0] = Left;
    OutRect4[1] = Top;
    OutRect4[2] = Right;
    OutRect4[3] = Bottom;
    return OutRect4;
}

FVector* HudTransformPoint(FTransform const* Xform, FVector* Out, FVector const* In)
{
    // FUN_004e7090: TransformXform into a stack vector through DAT_00674b1c, copied to Out.
    FVector Result;
    HudTargetMath()->TransformXform(Result, *Xform, *In);
    *Out = Result;
    return Out;
}

CShip* HudShipOfObjRw(IObjRW* ObjRw)
{
    // FUN_00543320: ECX null -> 0; LEA ECX+0xc (the IObjInspectImpl CBase) null -> 0; its +4
    // Object null -> 0; (Object+0x4c & 0x503) == 0x503 -> Object, else 0.
    if (ObjRw == nullptr)
    {
        return nullptr;
    }
    IObjInspectImpl* const Impl = static_cast<IObjInspectImpl*>(ObjRw);
    CBase* const Base = Impl;
    if (Base == nullptr)
    {
        return nullptr;
    }
    CObject* const Object = Base->cobject();
    if (Object == nullptr)
    {
        return nullptr;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & 0x503u) != 0x503u)
    {
        return nullptr;
    }
    return static_cast<CShip*>(Object);
}

namespace
{
    // The inlined `IObjRW + 0xc (CBase) -> +4 (Object)` chain of FUN_0045a4e0, null anywhere
    // -> null.
    CObject* HudCObjectOf(IObjInspectImpl* Impl)
    {
        if (Impl == nullptr)
        {
            return nullptr;
        }
        CBase* const Base = Impl;
        if (Base == nullptr)
        {
            return nullptr;
        }
        return Base->cobject();
    }

    // CShip +0x1b4 -- the multiplayer group id (common_basefollowerbehavior.cpp .text:062cb7a2 compares the
    // same field between two player ships) -- CShip::GroupId, read by offset from a CObject.
    unsigned HudShipGroupId(CObject const* Ship)
    {
        unsigned Value = 0;
        memcpy(&Value, reinterpret_cast<unsigned char const*>(Ship) + 0x1b4, sizeof(Value));
        return Value;
    }
}

bool HudIsUnvisitedSolar(CSimple const* Object)
{
    // FUN_004d4c70.
    // 0x004d4c75..0x004d4cae: a watch-listed client object of a matching type is known.
    FClientGameobj* const Gameobj = ClientObjDb_FindObject(Object->Id, 0); // FUN_005416c0
    if (Gameobj != nullptr)
    {
        unsigned Type = 0;
        Gameobj->get_type(Type); // vt+0x88; the return value is not looked at
        if ((Type & 0x107ffdffu) != 0 && Gameobj->bWatchListed != 0)
            return false;
    }
    // 0x004d4cb0..0x004d4cbf: only a solar can be unvisited.
    if ((static_cast<unsigned>(Object->ObjectClass) & 0x303u) != 0x303u)
        return false;
    // 0x004d4cc4..0x004d4cf3: the solar's own visit byte OR the visit-map flags (zero when the
    // id has no entry: FUN_004c4da0 leaves the pre-cleared byte alone); bit 0 means visited.
    unsigned char const OwnVisit = static_cast<CSolar const*>(Object)->GetVisitValue();
    unsigned char MapFlags = 0;
    GetPlayerVisitFlagsRetail(Object->Id, &MapFlags, 0, 0); // 0x004d4ce1
    return ((MapFlags | OwnVisit) & 1u) == 0;
}

unsigned char HudObjectVisitFlags(CObject const* Object)
{
    // FUN_004d4d00.
    if (Object == nullptr)
        return 1;
    if ((static_cast<unsigned>(Object->ObjectClass) & 0x303u) != 0x303u)
        return 1;
    CSolar const* const Solar = static_cast<CSolar const*>(Object);
    unsigned char const OwnVisit = Solar->GetVisitValue();
    unsigned char MapFlags = 0;
    GetPlayerVisitFlagsRetail(Solar->Id, &MapFlags, 0, 0); // 0x004d4d38
    return static_cast<unsigned char>(MapFlags | OwnVisit);
}

unsigned HudObjectTypeIds(CSimple const* Object)
{
    // FUN_004d4d50.
    unsigned const Bits = Object->TypeBits;
    if ((Bits & 0x1u) != 0)
        return 0x4a0;
    if ((Bits & 0x2u) != 0)
        return 0x4a1;
    if ((Bits & 0x4u) != 0)
        return 0x4a2;
    if ((Bits & 0x8010u) != 0)
        return 0x4a3;
    if ((Bits & 0x40u) != 0)
        return 0x4a4;
    if ((Bits & 0x80u) != 0)
        return 0x4a5;
    if ((Bits & 0x100u) != 0)
        return 0x4a6;
    return 0x4a7;
}

I3DMathEngine* G_HudObjectsMath = nullptr;

namespace
{
    // DAT_00674b80 / DAT_00674b84 (.data, zero at load; only FUN_004e8100 touches them): the
    // last docking-ring owner base FUN_004e8100 resolved and the ids_name it found for it.
    unsigned G_CaptionOwnerBaseId = 0;
    unsigned G_CaptionOwnerIdsName = 0;

    // The copy every FUN_004e8100 / FUN_005472d0 exit ends with: reserve Length + 1, wcsncpy
    // Length chars, terminate. The reserve is FUN_004142a0's body, which retail inlines at
    // 0x004e84db, 0x004e8657, 0x004e88be and 0x005472e5 -- WideTextReserve (font_string.cpp).
    void HudWideTextAssign(FHudWideText* Out, wchar_t const* Source, size_t Length)
    {
        WideTextReserve(Out, static_cast<unsigned>(Length + 1));
        wcsncpy(Out->Text, Source, Length);
        Out->Text[Length] = 0;
    }

    // FUN_004e8100's exits: the caption built in G_NnTextScratchB goes to Out, true.
    bool HudCaptionFinish(FHudWideText* Out)
    {
        HudWideTextAssign(Out, G_NnTextScratchB, wcslen(G_NnTextScratchB));
        return true;
    }

    // 0x004e8c3e..0x004e8cce (and 0x004e8916..0x004e89a1): the system's strid_name (ISystem
    // slot 3) and ids 0x561 when the player's system resolves, then the nav-map sector of the
    // object. Retail passes the capacity to FUN_0048dc10 as a third argument it never reads.
    void HudCaptionAppendLocation(CSimple const* Object)
    {
        Universe::ISystem const* const System =
            Universe::get_system(GetPlayerStarSystemIdRetail()); // FUN_004c4830
        if (System != nullptr)
        {
            GetIdsStringWWithRawFallback((unsigned)System->GetStridName(),
                                         G_NnTextScratchA, G_NnTextScratchCapacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
            GetIdsStringWWithRawFallback(0x561, G_NnTextScratchA, G_NnTextScratchCapacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
        }
        NavMapSectorName(&Object->WorldPosition, G_NnTextScratchA); // FUN_0048dc10
        wcscat(G_NnTextScratchB, G_NnTextScratchA);
    }

    // 0x004e823f..0x004e8284 (also 0x004e87d1.., 0x004e8b92..): " (%d)" (u" (%d)" 0x005cfb88)
    // for a loot object ((class & 0x803) == 0x803) of more than one unit (CLoot +0xf0).
    void HudCaptionAppendLootCount(CSimple const* Object)
    {
        if ((static_cast<unsigned>(Object->ObjectClass) & 0x803u) != 0x803u)
        {
            return;
        }
        unsigned const Units = static_cast<CLoot const*>(Object)->Units;
        if (Units <= 1)
        {
            return;
        }
        wchar_t Count[0x80];
        swprintf(Count, 0x80, L" (%d)", static_cast<int>(Units));
        wcscat(G_NnTextScratchB, Count);
    }

    // 0x004e8c19..0x004e8c3e: Mode 2 puts "\n" (0x005d0254) and the location after the name
    // line, Mode 1 the location alone, any other mode nothing; then the exit copy.
    bool HudCaptionLocationTail(CSimple const* Object, FHudWideText* Out, int Mode)
    {
        if (Mode == 2)
        {
            wcscat(G_NnTextScratchB, L"\n");
            HudCaptionAppendLocation(Object);
        }
        else if (Mode == 1)
        {
            HudCaptionAppendLocation(Object);
        }
        return HudCaptionFinish(Out);
    }

    // 0x004e81f6..0x004e8284 for an object that is not a CEqObj, and its copy 0x004e8b4a..
    // 0x004e8be1 for a CEqObj without a faction: the ids_name as the name line; a loot-type
    // object (TypeBits == 0x10000000) answers only in Mode 0 / 2 and takes the unit count
    // instead of a location.
    bool HudCaptionPlain(CSimple const* Object, unsigned Name, unsigned TypeBits,
                         FHudWideText* Out, int Mode)
    {
        if (Mode == 0 || Mode == 2)
        {
            GetIdsStringWWithRawFallback(Name, G_NnTextScratchB, G_NnTextScratchCapacity);
        }
        if (TypeBits != 0x10000000u)
        {
            return HudCaptionLocationTail(Object, Out, Mode);
        }
        if (Mode != 0 && Mode != 2)
        {
            return false;
        }
        HudCaptionAppendLootCount(Object);
        return HudCaptionFinish(Out);
    }
}

unsigned GetObjectIdsName(CObject const* Object)
{
    // FUN_005472a0 (cdecl).
    if (Object == nullptr)
    {
        return 0;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & 0x503u) == 0x503u)
    {
        // 0x005472b9: Archetype (+0x88) -> ids_name (+0x14), no null test.
        return static_cast<unsigned>(Object->Archetype->IdsName);
    }
    // 0x005472c3: JMP [vtable + 0x88] -- CSimple::get_name.
    return static_cast<CSimple const*>(Object)->get_name();
}

unsigned GetShipPilotName(CObject const* Object, FHudWideText* Out)
{
    // FUN_005472d0 (cdecl).
    // 0x005472d4..0x00547336: Out = "" (DAT_005c8ce0).
    wchar_t const* const Empty = L"";
    HudWideTextAssign(Out, Empty, wcslen(Empty));
    if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & 0x503u) == 0x503u)
    {
        wchar_t const* const Pilot = static_cast<CShip const*>(Object)->get_pilot_name();
        if (Pilot != nullptr)
        {
            HudWideTextAssign(Out, Pilot, wcslen(Pilot));
        }
        else
        {
            // 0x005473c1..0x005473cd
            if (Out->Text == nullptr)
            {
                return 0;
            }
            if (Out->Text[0] != 0)
            {
                Out->Text[0] = 0;
            }
        }
    }
    // 0x005473d2..0x005473ed
    if (Out->Text == nullptr)
    {
        return 0;
    }
    return static_cast<unsigned>(wcslen(Out->Text));
}

void NavMapSectorName(Vector const* Pos, wchar_t* Out)
{
    // FUN_0048dc10 (cdecl).
    // 0x0048dc16..0x0048dc3a: the system's NavMapScale (slot 20, +0x50), 1.0 without a system.
    float Scale = 1.0f;
    Universe::ISystem const* const System =
        Universe::get_system(GetPlayerStarSystemIdRetail()); // FUN_004c4830
    if (System != nullptr)
    {
        Scale = System->GetNavMapScale();
    }
    // 0x0048dc45..0x0048dc69: the row coordinate z * scale + 2700 (DAT_005d2ef4), the column
    // coordinate x * scale, and the format ids 0x54d into a 0x40-char buffer.
    float const RowCoord = Scale * Pos->z + 2700.0f;
    float const ColumnCoord = Scale * Pos->x;
    wchar_t Format[0x40];
    GetIdsStringWWithRawFallback(0x54d, Format, 0x40);

    // 0x0048dc72..0x0048dcbb: the first band (from -99999, DAT_005d2ef0, step 33333,
    // DAT_005d2eec) the coordinate lies below, capped at 8. TEST AH,5 / JNP leaves on "below"
    // only; equal and unordered step on.
    int Row = 0;
    float Bound = -99999.0f;
    for (Row = 0; Row < 8; ++Row)
    {
        if (RowCoord < Bound)
        {
            break;
        }
        Bound += 33333.0f;
    }
    int Column = 0;
    Bound = -99999.0f;
    for (Column = 0; Column < 8; ++Column)
    {
        if (ColumnCoord < Bound)
        {
            break;
        }
        Bound += 33333.0f;
    }

    // 0x0048dcbd..0x0048dd09: the row and column captions (0x10 chars each), then the legacy
    // two-argument swprintf of msvcrt (no bound on Out) -- Format(RowText, ColumnText).
    wchar_t RowText[0x10];
    wchar_t ColumnText[0x10];
    GetIdsStringWWithRawFallback(0x515 + Row, RowText, 0x10);
    GetIdsStringWWithRawFallback(0x506 + Column, ColumnText, 0x10);
    _swprintf(Out, Format, RowText, ColumnText);
}

unsigned HudObjectKindIds(unsigned TypeBits)
{
    // FUN_004e8060 (cdecl).
    if ((TypeBits & 0x1u) != 0)
        return 0x563;
    if ((TypeBits & 0x2u) != 0)
        return 0x44b;
    if ((TypeBits & 0x4u) != 0)
        return 0x564;
    if ((TypeBits & 0x8u) != 0)
        return 0x565;
    if ((TypeBits & 0x8010u) != 0)
        return 0x566;
    if ((TypeBits & 0x20u) != 0)
        return 0x567;
    if ((TypeBits & 0x40u) != 0)
        return 0x568;
    if ((TypeBits & 0x80u) != 0)
        return 0x4fa;
    if ((TypeBits & 0x100u) != 0)
        return 0x44c;
    if ((TypeBits & 0x200u) != 0)
        return 0x442;
    if ((TypeBits & 0x400u) != 0)
        return 0x56a;
    if ((TypeBits & 0x800u) != 0)
        return 0x56b;
    if ((TypeBits & 0x1000u) != 0)
        return 0x56c;
    // 0x004e80ee: AND 0x2000 / NEG / SBB / AND 0x56d.
    return (TypeBits & 0x2000u) != 0 ? 0x56du : 0u;
}

bool HudTradelaneCaption(CSimple const* Object, wchar_t* Out, int Capacity)
{
    // FUN_004e8d30 (cdecl).
    CSolar const* Ring = nullptr;
    if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & 0x303u) == 0x303u)
    {
        Ring = static_cast<CSolar const*>(Object);
    }
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player == nullptr || Ring == nullptr)
    {
        return false;
    }

    // 0x004e8d7b..0x004e8e3d: the ring's -z axis (its orientation, +0x08, copied with REP
    // MOVSD, times (0, 0, -1)), normalised, and the plane offset -(n . ring position).
    FMatrix const Orientation = reinterpret_cast<FMatrix const&>(Object->Orientation);
    FVector const Back = {0.0f, 0.0f, -1.0f};
    if (G_HudObjectsMath == nullptr)
    {
        G_HudObjectsMath = Acquire3DMathEngine(); // FUN_005b73e0
    }
    FVector Axis;
    G_HudObjectsMath->TransformMatrix(Axis, Orientation, Back); // vt+0x2c
    // 0x004e8dc9..0x004e8e04: (x*x + y*y) + z*z, FSQRT, FDIVR 1.0f -- inline, not FUN_00410190;
    // the reciprocal stays on the FPU (double here). X and Y are stored as float; Z is FST'd
    // to float but the unrounded product feeds the plane offset straight away (0x004e8e22).
    double const Inverse = 1.0 / sqrt(static_cast<double>(Axis.X) * Axis.X +
                                      static_cast<double>(Axis.Y) * Axis.Y +
                                      static_cast<double>(Axis.Z) * Axis.Z);
    float const NormalX = static_cast<float>(Axis.X * Inverse);
    float const NormalY = static_cast<float>(Axis.Y * Inverse);
    double const NormalZExact = Inverse * Axis.Z;
    float const NormalZ = static_cast<float>(NormalZExact);
    Vector const& RingPos = Object->WorldPosition;
    float const PlaneOffset = static_cast<float>(
        -(NormalZExact * RingPos.z + static_cast<double>(NormalY) * RingPos.y +
          static_cast<double>(NormalX) * RingPos.x));

    // 0x004e8e41..0x004e8e86: the player's side of that plane (IObjInspect slot 0,
    // get_position); TEST AH,0x41 takes only "greater than 0".
    Vector const PlayerPos = Player->get_position();
    bool const bSwap =
        (PlayerPos.z * NormalZ + PlayerPos.y * NormalY + PlayerPos.x * NormalX) + PlaneOffset >
        0.0f;

    // 0x004e8e88..0x004e8ec6: follow PrevRing (+0x1d8) to the first ring of the lane.
    CSolar const* Head = Ring;
    while (Head->PrevRing != 0)
    {
        CObject* const Next = HudCObjectOf(ClientObjDb_FindObject(Head->PrevRing, 0));
        if (Next == nullptr || (static_cast<unsigned>(Next->ObjectClass) & 0x303u) != 0x303u)
        {
            return false;
        }
        Head = static_cast<CSolar const*>(Next);
    }
    unsigned First = Head->get_tradelane_space_name();

    // 0x004e8ee1..0x004e8f1b: and NextRing (+0x1dc) to the last.
    CSolar const* Tail = Ring;
    while (Tail->NextRing != 0)
    {
        CObject* const Next = HudCObjectOf(ClientObjDb_FindObject(Tail->NextRing, 0));
        if (Next == nullptr || (static_cast<unsigned>(Next->ObjectClass) & 0x303u) != 0x303u)
        {
            return false;
        }
        Tail = static_cast<CSolar const*>(Next);
    }
    unsigned Second = Tail->get_tradelane_space_name();
    if (First == 0 || Second == 0)
    {
        return false;
    }
    if (bSwap)
    {
        unsigned const Swap = First;
        First = Second;
        Second = Swap;
    }

    // 0x004e8f35..0x004e8fc7: First "->" Second, each through a 0x100-char buffer (0xff read),
    // refused when the running length (+2 for the arrow) reaches Capacity (signed JGE).
    wchar_t Text[0x100];
    GetIdsStringWWithRawFallback(First, Text, 0xff);
    int const FirstLength = static_cast<int>(wcslen(Text)) + 2;
    if (FirstLength >= Capacity)
    {
        return false;
    }
    wcscpy(Out, Text);
    wcscat(Out, L"->"); // DAT_005cef58
    GetIdsStringWWithRawFallback(Second, Text, 0xff);
    if (static_cast<int>(wcslen(Text)) + FirstLength >= Capacity)
    {
        return false;
    }
    wcscat(Out, Text);
    return true;
}

bool HudBuildObjectCaption(CSimple const* Object, FHudWideText* Out, int Mode,
                           bool bFactionShortName)
{
    // FUN_004e8100 (cdecl).
    wchar_t* const Line = G_NnTextScratchB;    // DAT_0066fc60
    wchar_t* const Scratch = G_NnTextScratchA; // DAT_0066dc60

    // 0x004e811f..0x004e8165: the type bits, the ids_name, an empty line, and the watch-list
    // byte (+0xa8) of the object's client gameobj when TypeBits & 0xfdff.
    unsigned const TypeBits = Object->TypeBits;
    unsigned const Name = GetObjectIdsName(Object); // FUN_005472a0
    Line[0] = 0;
    unsigned char bWatched = 0;
    FClientGameobj* const Gameobj = ClientObjDb_FindObject(Object->Id, 0); // FUN_005416c0
    if (Gameobj != nullptr && (TypeBits & 0xfdffu) != 0)
    {
        bWatched = Gameobj->bWatchListed;
    }

    // 0x004e816b..0x004e81d1: TypeBits bit 15 or a watched object, with a name: the name line
    // (Mode 0 / 2) and the location tail.
    if (((TypeBits & 0x8000u) != 0 || bWatched != 0) && Name != 0)
    {
        if (Mode == 0 || Mode == 2)
        {
            GetIdsStringWWithRawFallback(Name, Line, G_NnTextScratchCapacity);
        }
        return HudCaptionLocationTail(Object, Out, Mode);
    }

    // 0x004e81d6..0x004e81f1: anything but a CEqObj.
    if ((static_cast<unsigned>(Object->ObjectClass) & 0x103u) != 0x103u)
    {
        if (Name == 0)
        {
            return false;
        }
        return HudCaptionPlain(Object, Name, TypeBits, Out, Mode);
    }

    // 0x004e8289..0x004e82b6: the faction of the object's reputation instance (+0x104).
    CEqObj const* const EqObj = static_cast<CEqObj const*>(Object);
    int const VibeId = static_cast<int>(EqObj->VibeId); // local_118
    unsigned Affiliation = 0;                          // local_114, written by GetAffiliation
    if (Reputation::Vibe::GetAffiliation(VibeId, Affiliation, false) != 0 ||
        Affiliation == 0xffffffffu)
    {
        // 0x004e89a9..0x004e8b45
        if (Name == 0)
        {
            return false;
        }
        if ((TypeBits & 0x7f0000u) == 0)
        {
            return HudCaptionPlain(Object, Name, TypeBits, Out, Mode);
        }
        if (Mode != 1 && Mode != 2)
        {
            return false;
        }
        // A player's display name, when the instance has one, is the whole caption. Retail
        // points GetName's out argument at the Name slot (+0x4c); GetName always writes it on
        // success, so the initial value is never read.
        FmtStr Name1;
        FmtStr Name2;
        wchar_t const* DisplayName = nullptr;
        if (Reputation::Vibe::GetName(VibeId, Name1, Name2, DisplayName) == 0 &&
            EqObj->OwnerPlayer != 0 && DisplayName != nullptr)
        {
            HudWideTextAssign(Out, DisplayName, wcslen(DisplayName));
            return true;
        }
        return false;
    }

    // 0x004e82bc..0x004e82d4
    unsigned const ShortName = Reputation::get_short_name(Affiliation); // [ESP+0x14]
    if ((TypeBits & 0x7f0000u) != 0)
    {
        // 0x004e82da..0x004e8558: the instance's two FmtStr names (ctors inlined, trylevel 2/3).
        FmtStr Name1;
        FmtStr Name2;
        wchar_t const* DisplayName = nullptr; // [ESP+0x48]; GetName writes it on success
        if (Reputation::Vibe::GetName(VibeId, Name1, Name2, DisplayName) == 0)
        {
            bool const bNoName1 = Name1.MadLibId == 0;
            if (Mode == 0 || Mode == 2)
            {
                if (!bNoName1)
                {
                    FmtStrSprintf(&Name1, Line, G_NnTextScratchCapacity); // FUN_0041c970
                }
                if (Mode == 2 && !bNoName1)
                {
                    wcscat(Line, L"\n");
                }
            }
            if (Mode == 1 || Mode == 2)
            {
                // 0x004e8417..0x004e84c8
                if (EqObj->OwnerPlayer != 0 && DisplayName != nullptr)
                {
                    wcscat(Line, DisplayName);
                }
                else
                {
                    if (bNoName1 && ShortName != 0 && ShortName != 0xffffffffu &&
                        bFactionShortName)
                    {
                        wchar_t Short[0x80];
                        if (GetIdsStringWWithRawFallback(ShortName, Short, 0x80) == 0)
                        {
                            Short[0] = 0;
                        }
                        // JA on length > 1, else a first char other than ' '.
                        if (wcslen(Short) > 1 || Short[0] != L' ')
                        {
                            wcscat(Line, Short);
                            wcscat(Line, L" - "); // DAT_005d01f4
                        }
                    }
                    FmtStrSprintf(&Name2, Scratch, G_NnTextScratchCapacity); // FUN_0041c970
                    wcscat(Line, Scratch);
                }
            }
            // 0x004e84cb..0x004e8558: the exit copy, then both FmtStr dtors (Name2 first).
            return HudCaptionFinish(Out);
        }
        // 0x004e855d..0x004e8582: both dtors, then on to the base captions.
    }

    // 0x004e8584..0x004e863d: docked at a base (+0x160) with a faction short name: the short
    // name + ids 0x562 as the name line, the ids_name as the location line.
    if (EqObj->DockingBaseId != 0 && ShortName != 0)
    {
        if (Mode == 0 || Mode == 2)
        {
            GetIdsStringWWithRawFallback(ShortName, Line, G_NnTextScratchCapacity);
            GetIdsStringWWithRawFallback(0x562, Scratch, G_NnTextScratchCapacity);
            wcscat(Line, Scratch);
            if (Mode == 2)
            {
                wcscat(Line, L"\n");
            }
        }
        if (Mode == 1 || Mode == 2)
        {
            GetIdsStringWWithRawFallback(Name, Scratch, G_NnTextScratchCapacity);
            wcscat(Line, Scratch);
        }
        return HudCaptionFinish(Out);
    }

    // 0x004e8684..0x004e8699: the object-kind ids, else the ids_name, as the second word.
    unsigned const KindIds = HudObjectKindIds(TypeBits); // FUN_004e8060
    unsigned const SecondIds = KindIds != 0 ? KindIds : Name;
    if (Mode == 0 || Mode == 2)
    {
        // 0x004e86b3..0x004e8758: the first word is the faction short name, or for TypeBits
        // 0x20 the ids_name of the solar whose client gameobj owns the base at +0x164 (bucket 2
        // walked through get_base, slot 35; the owner through cobject, slot 84), cached.
        unsigned FirstIds = ShortName;
        if ((TypeBits & 0x20u) != 0)
        {
            unsigned const OwnerBase = EqObj->DockingPointBaseId;
            if (OwnerBase != 0)
            {
                if (OwnerBase == G_CaptionOwnerBaseId)
                {
                    FirstIds = G_CaptionOwnerIdsName;
                }
                else
                {
                    FClientGameobj* Scan = nullptr;
                    for (;;)
                    {
                        int Kind = 2; // [ESP+0x58], re-stored every pass
                        Scan = Scan != nullptr ? Scan->ListNext : ClientObjDb_GetKindHead(&Kind);
                        if (Scan == nullptr)
                        {
                            break;
                        }
                        // Retail uses the short-name slot [ESP+0x14] for this; the short
                        // name is already in EBP and the slot is not read again.
                        unsigned Base = 0;
                        Scan->get_base(Base);
                        if (Base != OwnerBase)
                        {
                            continue;
                        }
                        CObject const* const Owner = Scan->cobject();
                        if (Owner != nullptr &&
                            (static_cast<unsigned>(Owner->ObjectClass) & 3u) == 3u)
                        {
                            FirstIds = GetObjectIdsName(Owner); // FUN_005472a0
                            G_CaptionOwnerBaseId = OwnerBase;
                            G_CaptionOwnerIdsName = FirstIds;
                        }
                        break;
                    }
                }
            }
        }
        // 0x004e875c..0x004e87a6
        GetIdsStringWWithRawFallback(FirstIds, Line, G_NnTextScratchCapacity);
        GetIdsStringWWithRawFallback(SecondIds, Scratch, G_NnTextScratchCapacity);
        wcscat(Line, L" "); // DAT_005cef90
        wcscat(Line, Scratch);
    }

    // 0x004e87b2..0x004e8815: a loot-type object answers only in Mode 0 / 2, with its count.
    if (TypeBits == 0x10000000u)
    {
        if (Mode != 0 && Mode != 2)
        {
            return false;
        }
        HudCaptionAppendLootCount(Object);
        return HudCaptionFinish(Out);
    }

    // 0x004e881a..0x004e8839
    if (Mode == 2)
    {
        wcscat(Line, L"\n");
    }
    else if (Mode != 1)
    {
        return HudCaptionFinish(Out);
    }

    // 0x004e8839..0x004e8870: a trade-lane ring (TypeBits bit 7) with an "A->B" caption.
    if ((TypeBits & 0x80u) != 0 &&
        HudTradelaneCaption(Object, Scratch, G_NnTextScratchCapacity)) // FUN_004e8d30
    {
        wcscat(Line, Scratch);
        return HudCaptionFinish(Out);
    }
    // 0x004e8875..0x004e8911: TypeBits & 0x8c0 -- the ids_name.
    if ((TypeBits & 0x8c0u) != 0)
    {
        GetIdsStringWWithRawFallback(Name, Scratch, G_NnTextScratchCapacity);
        wcscat(Line, Scratch);
        return HudCaptionFinish(Out);
    }
    // 0x004e8916..0x004e89a4: system name and sector.
    HudCaptionAppendLocation(Object);
    return HudCaptionFinish(Out);
}

void HudReputationColor(unsigned long* OutArgb, IObjRW* Player, IObjRW* Other)
{
    // FUN_0045a4e0 (cdecl).
    if (Other == nullptr || Player == nullptr)
    {
        *OutArgb = G_NnPaletteArgb_679b84;
        return;
    }

    FClientGameobj* const Gameobj = ClientObjDb_FindObject(Other->get_id(), 1);
    if (Gameobj != nullptr)
    {
        // Ship gameobj byte +0x18c bit 2 (0x0045a517; bucket 1 holds ships only). No instruction
        // in freelancer.exe sets bit 2 (writers: ctor 0x005434a5 zeroes the dword, 0x00543b71
        // sets bit 3, 0x00545f07 bits 0/1, 0x005447c6 / 0x0051db60 / 0x0051db8d clear 0/1).
        if ((static_cast<FClientShipGameobj*>(Gameobj)->Field_0x18c & 4) != 0)
        {
            *OutArgb = G_NnPaletteArgb_679b9c;
            return;
        }
        // IObjInspect slot 47 (+0xbc) is_player, then a ship with a non-zero group id that the
        // player's ship shares (0x0045a530..0x0045a59b).
        if (Gameobj->is_player())
        {
            CObject* const Object = HudCObjectOf(Gameobj);
            if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & 0x503u) == 0x503u &&
                HudShipGroupId(Object) != 0)
            {
                IObjRW* const PlayerRw = static_cast<IObjRW*>(GetPlayerObjRW());
                if (PlayerRw != nullptr)
                {
                    // 0x0045a56e..0x0045a58f: a player object that is not a ship leaves EAX = 0
                    // and retail reads [0 + 0x1b4] -- the player in space is always a ship, and
                    // the port keeps that read unguarded, as retail does.
                    CObject* PlayerShip = HudCObjectOf(static_cast<IObjInspectImpl*>(PlayerRw));
                    if (PlayerShip != nullptr &&
                        (static_cast<unsigned>(PlayerShip->ObjectClass) & 0x503u) != 0x503u)
                    {
                        PlayerShip = nullptr;
                    }
                    if (HudShipGroupId(PlayerShip) == HudShipGroupId(Object))
                    {
                        *OutArgb = G_NnPaletteArgb_679b88;
                        return;
                    }
                }
            }
        }
    }

    // Other's attitude towards the player (vt+0x68); a failed query is white.
    float Attitude = 0.0f;
    if (Other->get_attitude_towards(Attitude, Player) != 0)
    {
        *OutArgb = G_NnPaletteArgb_679b84;
        return;
    }
    // FCOMP -0.6f / TEST AH,0x41 / JP: hostile when at or below -0.6.
    if (Attitude <= -0.6f)
    {
        *OutArgb = G_NnPaletteArgb_679ba8;
        return;
    }
    // FCOMP -0.2f (DAT_005ce7f0 = 0xbe4ccccd) / TEST AH,0x41 / JNP: white when at or below
    // -0.2; then FCOMP 0.6f / TEST AH,1 / JNZ: white below 0.6 (or unordered), green at or
    // above it. Both white exits are kept as retail spells them.
    if (Attitude <= -0.2f)
    {
        *OutArgb = G_NnPaletteArgb_679b84;
        return;
    }
    if (!(Attitude >= 0.6f))
    {
        *OutArgb = G_NnPaletteArgb_679b84;
        return;
    }
    *OutArgb = G_NnPaletteArgb_679bac;
}

int HudReputationBand(float Attitude)
{
    // FUN_0045a6e0. FCOMP DAT_005ce7ec (-0.6f), TEST AH,0x41 + JP: below or equal returns 0,
    // greater (and unordered) goes on.
    if (Attitude <= -0.6f)
        return 0;
    // 0x0045a6f4..0x0045a70a: the same against DAT_005ce7f0 (-0.2f) -- 1.
    if (Attitude <= -0.2f)
        return 1;
    // 0x0045a70b..0x0045a721: FCOMP DAT_005ce7f8 (0.6f), TEST AH,0x01 + JNZ jumps when less (or
    // unordered); not less returns 4.
    if (Attitude >= 0.6f)
        return 4;
    // 0x0045a722..0x0045a73d: FCOMP DAT_005ce7f4 (0.2f), TEST AH,0x41, EAX = 3 and JZ keeps it
    // when greater; less, equal or unordered falls to 2.
    if (Attitude > 0.2f)
        return 3;
    return 2;
}

// =============================================================================================
// HudTargetBase
// =============================================================================================

HudTargetBase::HudTargetBase()
    : ViewObject()
    , PartCache()
    , bSubTargetPickEnabled(0)
    , ViewPanel(nullptr)
    , bExpanded(0)
{
    // FUN_004e6020: bases (HUDItem inlined with an empty name, WinControl FUN_0059f860,
    // ButtonMover FUN_0055dec0, the handler vptr), +0x398 zeroed, FUN_00405890 (the part cache
    // zeroed), then the GameCamera ctor FUN_0051b2f0 at +0x3b8.
    GameCameraConstruct(&ViewCamera);
    SubTargetId = SUBOBJ_ID_NONE;
}

HudTargetBase::~HudTargetBase()
{
    // FUN_004da630, in retail's order: the camera's watcher (+0x3b8 + 0xc0) cleared (the
    // camera vptr put back and its empty base dtor run), PartCacheClear FUN_004058e0 on +0x3a0;
    // the member dtors then clear the +0x398 watcher, and the base dtors run Event::Handler
    // FUN_00576010, free the mover's list, ~WinControl FUN_0059fa50 and HUDItem's watcher.
    //
    // 0x004da662: `if (watched != 0) BaseWatcher::set_pointer(0)`. set_pointer is protected in
    // Common (?set_pointer@BaseWatcher@@IAE...), so retail's inlined test-and-call comes from a
    // header-only BaseWatcher subclass that left no symbol, vtable or RTTI in either PE; its body
    // is exactly Common's public BaseWatcher::set (`if (Target != Watched) set_pointer(Target)`,
    // .text import 0x005c645c), which the port calls.
    ViewCamera.Watched.set(nullptr);
    PartCacheClear(&PartCache);
}

void HudTargetBase::SetViewObject(IObjRW* Target)
{
    // LAB_004e6270. The client object (FUN_005416c0, kind 0) of Target's id, kept only when its
    // CObject has class bits 3 (& 3 == 3).
    IObjRW* Found = nullptr;
    if (Target != nullptr)
    {
        FClientGameobj* const Gameobj = ClientObjDb_FindObject(Target->get_id(), 0);
        Found = Gameobj;
        if (Gameobj != nullptr)
        {
            CObject const* const Object = Gameobj->cobject();
            if (Object == nullptr || (static_cast<unsigned>(Object->ObjectClass) & 3u) != 3u)
            {
                Found = nullptr;
            }
        }
    }

    IObjRW* const Current = ObjRwFromWatchable(const_cast<Watchable*>(ViewObject.target()));
    if (Current == Found)
    {
        return;
    }
    // 0x004e62ef: `if (Found+8 != watched) set_pointer(Found+8)` -- BaseWatcher::set's body (see
    // the destructor for why the port calls set).
    Watchable const* const NewWatched = (Found != nullptr) ? static_cast<Watchable*>(Found) : nullptr;
    ViewObject.set(NewWatched);
    Slot16();

    IObjRW* const Viewed = ObjRwFromWatchable(const_cast<Watchable*>(ViewObject.target()));
    if (Viewed == nullptr)
    {
        // 0x004e655f
        PartCacheClear(&PartCache);
        return;
    }

    // 0x004e633b: slot 4 (+0x10) get_orientation, whose retail body is ICF-folded with slot 3
    // get_transform -- the pointer is the object's whole Transform, and FUN_004e65d0 uses it as
    // one. The port asks for the transform by its own name.
    FTransform const* const Xform = reinterpret_cast<FTransform const*>(&Viewed->get_transform());
    void* const Instance = InstanceFromIndex(Viewed->get_index());
    FHudInstanceBounds Bounds(Instance, Xform, false);

    FVector Center;
    ViewCenter = *Bounds.GetCenter(&Center);

    // 0x004e6390..0x004e63a1: FUN_00423b60 (Max - Center into a stack vector), FUN_00410190 on
    // it (the length left on the FPU), FSTP float to +0x388 -- stored, then overwritten below.
    float Reach[3];
    Reach[0] = Bounds.Max.X - ViewCenter.X;
    Reach[1] = Bounds.Max.Y - ViewCenter.Y;
    Reach[2] = Bounds.Max.Z - ViewCenter.Z;
    ViewRadius = static_cast<float>(Vec3Length(Reach));

    // FCOM / TEST AH,0x41 / JZ: the X span is kept only when strictly larger than the Z span,
    // then * 0.5 (DAT_005c75e8, double).
    float const SpanX = Bounds.Max.X - Bounds.Min.X;
    float const SpanZ = Bounds.Max.Z - Bounds.Min.Z;
    float const Span = (SpanX > SpanZ) ? SpanX : SpanZ;
    ViewRadius = static_cast<float>(static_cast<double>(Span) * 0.5);

    // 0x004e63d8: the chrome colour DAT_00679bb4 dimmed to 70 % per byte (byte 0 clamped inline,
    // bytes 1..3 through FUN_004104c0), packed as byte3 << 24 | byte0 << 16 | byte1 << 8 | byte2.
    unsigned char const* const Chrome = G_HudChromeColorRgba;
    unsigned const Byte0 = static_cast<unsigned>(ClampByte(DimChromeByte(Chrome[0])));
    unsigned const Byte1 = static_cast<unsigned>(ClampByte(DimChromeByte(Chrome[1])));
    unsigned const Byte2 = static_cast<unsigned>(ClampByte(DimChromeByte(Chrome[2])));
    unsigned const Byte3 = static_cast<unsigned>(ClampByte(DimChromeByte(Chrome[3])));
    unsigned const Tag = (Byte3 << 24) | (Byte0 << 16) | (Byte1 << 8) | Byte2;

    // 0x004e6492: get_index again; the renderer LODs pinned to 3 while the cache is built, then
    // put back (FUN_00402580 / FUN_00405b10 / FUN_00402720), then the parts hidden.
    void* const BuildInstance = InstanceFromIndex(Viewed->get_index());
    FRendererLodMap SavedLods;
    BindRendererLods(BuildInstance, 3, &SavedLods);
    PartCacheBuild(&PartCache, BuildInstance, Tag);
    ApplyRendererDetailQueue(BuildInstance, &SavedLods);
    HideGroupAndEquipParts();
}

void HudTargetBase::Slot12()
{
    // FUN_00401e90 -- RET.
}

void HudTargetBase::Slot13(bool /*bHide*/)
{
    // FUN_0053e4a0 -- RET 4.
}

void HudTargetBase::Slot14()
{
    // FUN_00401e90 -- RET.
}

void HudTargetBase::Slot15()
{
    // FUN_00401e90 -- RET.
}

void HudTargetBase::Slot16()
{
    // FUN_00401e90 -- RET.
}

CObject* HudTargetBase::GetViewCObject() const
{
    return CObjectOfWatcher(ViewObject);
}

void HudTargetBase::HideGroupAndEquipParts()
{
    // FUN_004e6160: only for a CEqObj (class & 0x103 == 0x103).
    CObject* const Object = GetViewCObject();
    if (Object == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & 0x103u) != 0x103u)
    {
        return;
    }
    CEqObj* const EqObj = static_cast<CEqObj*>(Object);

    // Every collision group root, without recursion; the root index is not tested against -1.
    CArchGrpTraverser GroupWalk;
    CArchGroup* Group = EqObj->ArchGroups.Traverse(GroupWalk);
    while (Group != nullptr)
    {
        PartCacheMarkHidden(&PartCache, InstanceFromIndex(Group->GetRootIndex()), false);
        Group = EqObj->ArchGroups.Traverse(GroupWalk);
    }

    // Every attached equipment (mask 0x1fe0) root other than -1, with recursion.
    CEquipTraverser EquipWalk(0x1fe0);
    CEquip* Equip = EqObj->EquipManager.Traverse(EquipWalk);
    while (Equip != nullptr)
    {
        // vt+0x84 CAttachedEquip::GetRootIndex, called on every traversed item.
        long const Root = static_cast<CAttachedEquip*>(Equip)->GetRootIndex();
        if (Root != -1)
        {
            PartCacheMarkHidden(&PartCache, InstanceFromIndex(Root), true);
        }
        Equip = EqObj->EquipManager.Traverse(EquipWalk);
    }
}

namespace
{
    // DAT_006127b4 -- the view camera's half fov x (0x420c0000, read-only in the PE).
    float const kViewHalfFovX = 35.0f;

    // The part tag both colouring passes build from a colour's four memory bytes (0x004e6e60,
    // 0x004e701a): byte3 << 24 | byte0 << 16 | byte1 << 8 | byte2.
    unsigned PackPartTag(unsigned long Colour)
    {
        unsigned const Byte0 = static_cast<unsigned>(Colour & 0xffu);
        unsigned const Byte1 = static_cast<unsigned>((Colour >> 8) & 0xffu);
        unsigned const Byte2 = static_cast<unsigned>((Colour >> 16) & 0xffu);
        unsigned const Byte3 = static_cast<unsigned>((Colour >> 24) & 0xffu);
        return (Byte3 << 24) | (Byte0 << 16) | (Byte1 << 8) | Byte2;
    }

    unsigned long ChromeColourArgb()
    {
        unsigned long Colour = 0;
        memcpy(&Colour, G_HudChromeColorRgba, sizeof(G_HudChromeColorRgba)); // DAT_00679bb4
        return Colour;
    }
}

void HudTargetBase::SetupViewCamera(bool bFixedOrbit, float ScreenShift)
{
    // FUN_004e70e0. The hit box's top-left and bottom-right corners through the Win3D camera.
    FVector Corner0;
    Corner0.X = HitCenter.X - HitHalfWidth;
    Corner0.Y = HitCenter.Y + HitHalfHeight;
    Corner0.Z = HitCenter.Z;
    FVector Corner1;
    Corner1.X = HitCenter.X + HitHalfWidth;
    Corner1.Y = HitCenter.Y - HitHalfHeight;
    Corner1.Z = HitCenter.Z;
    int Box[4];
    int const* const Projected = HudProjectScreenRect(Box, &Corner0, &Corner1);
    int const Left = Projected[0];
    int const Top = Projected[1];
    int const Right = Projected[2];
    int const Bottom = Projected[3];

    // 0x004e717a: moved ScreenShift Win3D-camera widths sideways (DAT_0067dca4 / DAT_0067dcac,
    // the left and right of that camera's viewport), then inset by an eighth of the box: its
    // width on the left and the right, its height at the top -- and its WIDTH again at the
    // bottom, because 0x004e7228 subtracts the width term still on the FPU stack. The products
    // and sums are exact in double, as they are on the x87 stack.
    int const* const Screen = Win3DGetCamera()->Viewport;
    double const ScreenWidth = static_cast<double>(Screen[2] - Screen[0] + 1);
    double const WidthInset = static_cast<double>(Right - Left) * 0.125;
    double const HeightInset = static_cast<double>(Bottom - Top) * 0.125;
    int const ShiftedLeft =
        static_cast<int>(ScreenWidth * static_cast<double>(ScreenShift) + static_cast<double>(Left));
    int const ShiftedRight =
        static_cast<int>(ScreenWidth * static_cast<double>(ScreenShift) + static_cast<double>(Right));
    int Viewport[4];
    Viewport[0] = static_cast<int>(static_cast<double>(ShiftedLeft) + WidthInset);
    Viewport[1] = static_cast<int>(HeightInset + static_cast<double>(Top));
    Viewport[2] = static_cast<int>(static_cast<double>(ShiftedRight) - WidthInset);
    Viewport[3] = static_cast<int>(static_cast<double>(Bottom) - WidthInset);
    SpaceCameraSetViewportEx(&ViewCamera, Viewport, 1);

    // DAT_00674b08 -- 1 / tan(35 degrees), computed once (bit 0 of DAT_00674b0c): FLD 35.0f,
    // FMUL 0.0174533f (DAT_005c77f4), FPTAN, FDIVR 1.0.
    static float const InvTanHalfFov = static_cast<float>(
        1.0 / tan(static_cast<double>(kViewHalfFovX) * static_cast<double>(0.0174533f)));
    SpaceCameraSetHalfFovX(&ViewCamera, kViewHalfFovX);

    IObjRW* const Viewed = ObjRwFromWatchable(const_cast<Watchable*>(ViewObject.target()));
    FTransform Xform;
    if (!bFixedOrbit)
    {
        // 0x004e729a: ViewRadius * 1.1 (DAT_005d8468) / tan(35) away from the object's centre.
        float const Distance = static_cast<float>(static_cast<double>(InvTanHalfFov) *
                                                  static_cast<double>(ViewRadius) *
                                                  static_cast<double>(1.1f));
        Win3DIdentityRotation3x3(&Xform.Rotation.D[0][0]);
        Xform.Translation.X = 0.0f;
        Xform.Translation.Y = 0.0f;
        Xform.Translation.Z = 0.0f;

        // The HUDItem target (+0x28) when it is a ship, else null -- which retail does not test:
        // GetDirectionMatrix is handed null + 8 (EngineObject::get_transform inlined).
        CObject* Ship = CObjectOfWatcher(HUDItem::Target);
        if (Ship != nullptr && (static_cast<unsigned>(Ship->ObjectClass) & 0x503u) != 0x503u)
        {
            Ship = nullptr;
        }
        Matrix const Facing = GetDirectionMatrix(Ship->get_transform(), Viewed->get_position());
        memcpy(&Xform.Rotation, &Facing, sizeof(FMatrix));

        // Slot 4 (+0x10), ICF-folded with get_transform -- see SetViewObject.
        FTransform const* const ViewXform =
            reinterpret_cast<FTransform const*>(&Viewed->get_transform());
        FVector Center;
        HudTransformPoint(ViewXform, &Center, &ViewCenter);
        float const Axis[3] = {Xform.Rotation.D[0][2], Xform.Rotation.D[1][2],
                               Xform.Rotation.D[2][2]};
        float Offset[3];
        ScaleVec3(Offset, Axis, Distance);
        // Loot (class & 0x803) stands four times as far (DAT_005cfc9c).
        CObject* const ViewedObject = GetViewCObject();
        if (ViewedObject != nullptr &&
            (static_cast<unsigned>(ViewedObject->ObjectClass) & 0x803u) == 0x803u)
        {
            Offset[0] = Offset[0] * 4.0f;
            Offset[1] = Offset[1] * 4.0f;
            Offset[2] = Offset[2] * 4.0f;
        }
        // FUN_00423b30.
        Xform.Translation.X = Center.X + Offset[0];
        Xform.Translation.Y = Center.Y + Offset[1];
        Xform.Translation.Z = Center.Z + Offset[2];
    }
    else
    {
        // 0x004e7420: the object's own orientation (slot 3, +0xc) with its y / z columns turned
        // by -90 degrees (the double DAT_005d8ec0 = (double)-1.57079637f): per row,
        // m1' = m1 cos + sin m2 and m2' = cos m2 - m1 sin.
        FTransform const* const ObjectXform =
            reinterpret_cast<FTransform const*>(&Viewed->get_transform());
        Xform.Rotation = ObjectXform->Rotation;
        Xform.Translation.X = 0.0f;
        Xform.Translation.Y = 0.0f;
        Xform.Translation.Z = 0.0f;
        double const Angle = static_cast<double>(-1.57079637f);
        double const Cos = cos(Angle);
        double const Sin = sin(Angle);
        for (int Row = 0; Row < 3; ++Row)
        {
            double const M1 = static_cast<double>(Xform.Rotation.D[Row][1]);
            double const M2 = static_cast<double>(Xform.Rotation.D[Row][2]);
            Xform.Rotation.D[Row][1] = static_cast<float>(M1 * Cos + Sin * M2);
            Xform.Rotation.D[Row][2] = static_cast<float>(Cos * M2 - M1 * Sin);
        }

        float const Distance = static_cast<float>(static_cast<double>(InvTanHalfFov) *
                                                  static_cast<double>(ViewRadius) *
                                                  static_cast<double>(1.1f));
        // Slot 4 (+0x10) through DAT_00674b1c directly (FUN_004e7090's body, inlined).
        FTransform const* const ViewXform =
            reinterpret_cast<FTransform const*>(&Viewed->get_transform());
        FVector Center;
        HudTargetMath()->TransformXform(Center, *ViewXform, ViewCenter);
        float const OffsetX = Xform.Rotation.D[0][2] * Distance;
        float const OffsetY = Xform.Rotation.D[1][2] * Distance;
        float const OffsetZ = Xform.Rotation.D[2][2] * Distance;
        Xform.Translation.X = Center.X + OffsetX;
        Xform.Translation.Y = Center.Y + OffsetY;
        Xform.Translation.Z = Center.Z + OffsetZ;
    }

    // 0x004e7567: the twelve floats into the camera's world orientation and position, the view
    // marked dirty, then the viewport and the perspective of this camera.
    ViewCamera.bViewDirty = 1;
    memcpy(ViewCamera.World, &Xform.Rotation, sizeof(ViewCamera.World));
    memcpy(ViewCamera.WorldPos, &Xform.Translation, sizeof(ViewCamera.WorldPos));
    Win3DSetViewport(AsCamera(&ViewCamera),
                     "E:\\FL\\Scratch\\Source\\Client\\Space\\HUD_Target_Base.cpp", 0x16b);
    Win3DApplyCameraPerspective(AsCamera(&ViewCamera));
    bSubTargetPickEnabled = 1;
}

void HudTargetBase::ColourViewParts()
{
    // FUN_004e7070.
    PartCacheClearHiddenActive(&PartCache);
    ColourGroupParts();
    ColourEquipParts();
}

void HudTargetBase::ColourGroupParts()
{
    // FUN_004e6cb0: only for a CEqObj (class & 0x103 == 0x103).
    CObject* const Object = GetViewCObject();
    if (Object == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & 0x103u) != 0x103u)
    {
        return;
    }
    CEqObj* const EqObj = static_cast<CEqObj*>(Object);

    CArchGrpTraverser GroupWalk;
    CArchGroup* Group = EqObj->ArchGroups.Traverse(GroupWalk);
    while (Group != nullptr)
    {
        // The chrome colour, or the damage colour while 1 - relative health > 0 (TEST AH,0x41).
        unsigned long Colour = ChromeColourArgb();
        float const Damage = 1.0f - Group->GetRelativeHealth();
        if (Damage > 0.0f)
        {
            unsigned long Damaged = 0;
            Colour = *HudDamageColorArgb(&Damaged, Damage);
        }

        // 0x004e6d66: each byte * 0.7f, ftol, clamped to 0..255 inline.
        unsigned long const Dim0 = static_cast<unsigned long>(
            ClampByte(DimChromeByte(static_cast<unsigned char>(Colour))));
        unsigned long const Dim1 = static_cast<unsigned long>(
            ClampByte(DimChromeByte(static_cast<unsigned char>(Colour >> 8))));
        unsigned long const Dim2 = static_cast<unsigned long>(
            ClampByte(DimChromeByte(static_cast<unsigned char>(Colour >> 16))));
        unsigned long const Dim3 = static_cast<unsigned long>(
            ClampByte(DimChromeByte(static_cast<unsigned char>(Colour >> 24))));
        Colour = Dim0 | (Dim1 << 8) | (Dim2 << 16) | (Dim3 << 24);

        // The sub-target pulses (FUN_00561f20 round the dimmed colour) unless the mouse may not
        // pick (FUN_0045a740).
        if (SubTargetId == Group->GetID() && !HudIsMousePickBlocked())
        {
            unsigned long Pulsed = 0;
            NnUiPulseBlendFromArgb(&Pulsed, Colour);
            Colour = Pulsed;
        }

        // Re-activated without recursion; the root index is not tested against -1.
        PartCacheActivatePart(&PartCache, InstanceFromIndex(Group->GetRootIndex()), false,
                              PackPartTag(Colour));
        Group = EqObj->ArchGroups.Traverse(GroupWalk);
    }
}

void HudTargetBase::ColourEquipParts()
{
    // FUN_004e6ec0: only for a CEqObj (class & 0x103 == 0x103).
    CObject* const Object = GetViewCObject();
    if (Object == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & 0x103u) != 0x103u)
    {
        return;
    }
    CEqObj* const EqObj = static_cast<CEqObj*>(Object);

    // 0x004e6f15: FUN_00548eb0 on the object's id; its result is not used.
    IsPlayerShipId(EqObj->Id);

    CEquipTraverser EquipWalk(0x1fe0);
    CEquip* Equip = EqObj->EquipManager.Traverse(EquipWalk);
    while (Equip != nullptr)
    {
        unsigned long Colour = G_NnPaletteArgb_679b98;
        float const Damage = 1.0f - Equip->GetRelativeHealth();
        if (Damage > 0.0f)
        {
            unsigned long Damaged = 0;
            Colour = *HudDamageColorArgb(&Damaged, Damage);
        }

        // A launcher that is a gun without auto_turret (Gun +0xba): amber while active, else the
        // chrome colour -- in place of the damage colour.
        if (CELauncher::cast(Equip) != nullptr)
        {
            CEGun* const Gun = CEGun::cast(Equip);
            if (Gun != nullptr)
            {
                Archetype::Gun const* const Arch = Gun->GunArch();
                if (Arch != nullptr && !Arch->bAutoTurret)
                {
                    Colour = Equip->IsActive() ? G_NnPaletteArgb_679ba4 : ChromeColourArgb();
                }
            }
        }

        if (SubTargetId == Equip->Id && !HudIsMousePickBlocked())
        {
            unsigned long Pulsed = 0;
            NnUiPulseBlendFromArgb(&Pulsed, Colour);
            Colour = Pulsed;
        }

        // vt+0x84 CAttachedEquip::GetRootIndex; re-activated with recursion.
        long const Root = static_cast<CAttachedEquip*>(Equip)->GetRootIndex();
        if (Root != -1)
        {
            PartCacheActivatePart(&PartCache, InstanceFromIndex(Root), true, PackPartTag(Colour));
        }
        Equip = EqObj->EquipManager.Traverse(EquipWalk);
    }
}

void HudTargetBase::UpdateSubTargetToolTip()
{
    // FUN_004e75b0.
    if (HudIsMousePickBlocked())
    {
        SubTargetId = SUBOBJ_ID_NONE;
        return;
    }

    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ); // DAT_00616840 / DAT_00616844
    unsigned short PickedSub = 0;
    int PickedCount = 0;
    unsigned const Ids = PickSubTarget(CursorX, CursorY, &PickedSub, &PickedCount);
    if (Ids == 0)
    {
        SubTargetId = SUBOBJ_ID_NONE;
        return;
    }
    SubTargetId = PickedSub;

    if (PickedCount > 1)
    {
        // "(%d) %s" with the part's name (FUN_004347e0, 0x40 chars; empty when it fails).
        wchar_t Name[0x40];
        if (GetIdsStringWWithRawFallback(Ids, Name, 0x40) == 0)
        {
            Name[0] = 0;
        }
        wchar_t Text[0x80];
        swprintf(Text, 0x80, L"(%d) %s", PickedCount, Name);

        // The hover record: key = SubTargetId, no tooltip id, caption = Text (FUN_004142a0
        // grows the buffer to len + 1, wcsncpy, terminator). Retail leaves +0x48..+0x5f as
        // stack garbage; the port zeroes them (same as WinControl::ShowToolTip). The caption
        // zeroing and FUN_004144b0 are the record's constructor.
        FToolTipHoverInfo Info;
        Info.IdsAlt = 0;
        Info.Ids = -1;
        Info.AuxField = 0;
        // FUN_00414280 -- clear the (still null) caption.
        if (Info.CaptionW != nullptr && Info.CaptionW[0] != 0)
        {
            Info.CaptionW[0] = 0;
        }
        Info.bLive = 0;
        Info.Ids = static_cast<int>(SubTargetId);

        size_t const Length = wcslen(Text);
        unsigned const Need = static_cast<unsigned>(Length + 1);
        if (Need > Info.CaptionCapacity)
        {
            // FUN_004142a0
            if (Info.CaptionW != nullptr)
            {
                Info.CaptionW = static_cast<wchar_t*>(realloc(Info.CaptionW, Need * sizeof(wchar_t)));
            }
            else
            {
                Info.CaptionW = static_cast<wchar_t*>(malloc(Need * sizeof(wchar_t)));
                Info.CaptionW[0] = 0;
            }
            Info.CaptionCapacity = Need;
        }
        wcsncpy(Info.CaptionW, Text, Length);
        Info.CaptionW[Length] = 0;
        NnToolTipFeedHover(&Info);

        // Teardown (~FToolTipHoverInfo): FUN_004144f0, the children (FUN_00445d70 /
        // FUN_00444e20), the caption.
        return;
    }

    // One part: key = SubTargetId, tooltip id = the part's ids_name, no caption
    // (FUN_00413df0(0, -1) on the null caption does nothing).
    FToolTipHoverInfo Info;
    Info.IdsAlt = 0;
    Info.Ids = -1;
    Info.AuxField = 0;
    Info.bLive = 0;
    Info.Ids = static_cast<int>(SubTargetId);
    Info.IdsAlt = static_cast<int>(Ids);
    NnToolTipFeedHover(&Info);
    // Teardown: ~FToolTipHoverInfo.
}

unsigned HudTargetBase::PickSubTarget(int X, int Y, unsigned short* OutSubId, int* OutCount)
{
    // FUN_004e79f0.
    *OutSubId = 0;
    *OutCount = 0;
    if (bSubTargetPickEnabled == 0)
    {
        return 0;
    }
    CObject* const Object = GetViewCObject();
    if (Object == nullptr)
    {
        return 0;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & 0x103u) != 0x103u)
    {
        return 0;
    }
    CEqObj* const EqObj = static_cast<CEqObj*>(Object);

    // The view camera's unprojection of (X, Y) (slot 0x38), offset by its position (slot 0x0c).
    float const ScreenX = static_cast<float>(X);
    float const ScreenY = static_cast<float>(Y);
    ICamera* const Camera = AsCamera(&ViewCamera);
    FVector Unprojected;
    Camera->Unproject(&Unprojected.X, ScreenX, ScreenY);
    float const* const CameraPos = Camera->GetPosition();
    FVector RayEnd;
    RayEnd.X = Unprojected.X + CameraPos[0];
    RayEnd.Y = Unprojected.Y + CameraPos[1];
    RayEnd.Z = Unprojected.Z + CameraPos[2];

    // The same screen point on the Win3D camera's near plane (DAT_0067dbf8: +0x68 / +0x6c
    // centre, +0x78 / +0x7c scales, +0xa4 near), rotated by its orientation (+0x04) -- the
    // direction the WinControl face's HitTest (slot 4) takes.
    FSpaceCamera const* const UiCamera = Win3DGetCamera();
    FVector Near;
    Near.X = static_cast<float>((static_cast<double>(ScreenX) - UiCamera->ScreenCenterX) *
                                (static_cast<double>(UiCamera->ZNear) / UiCamera->XScale));
    Near.Y = static_cast<float>((static_cast<double>(ScreenY) - UiCamera->ScreenCenterY) *
                                (static_cast<double>(UiCamera->ZNear) / UiCamera->YScale));
    Near.Z = -UiCamera->ZNear;
    FMatrix Orientation;
    memcpy(&Orientation, UiCamera->World, sizeof(Orientation));
    FVector Direction;
    HudTargetMath()->TransformMatrix(Direction, Orientation, Near);
    if (!HitTest(&Direction.X))
    {
        return 0;
    }

    FHudSpherePick Pick(1);
    Pick.Setup(reinterpret_cast<FVector const*>(Camera->GetPosition()), &RayEnd);

    unsigned Result = 0;

    // Attached equipment (mask 0x1ee0): IsConnected (vt+0x58), then the root (vt+0x84).
    CEquipTraverser EquipWalk(0x1ee0);
    CEquip* Equip = EqObj->EquipManager.Traverse(EquipWalk);
    while (Equip != nullptr)
    {
        CAttachedEquip* const Attached = static_cast<CAttachedEquip*>(Equip);
        if (Attached->IsConnected() &&
            Pick.Test(InstanceFromIndex(Attached->GetRootIndex()), kSubTargetPickSlack))
        {
            *OutSubId = Equip->Id;
            Result = static_cast<unsigned>(Equip->Arch->IdsName);
            CELauncher* const Launcher = CELauncher::cast(Equip);
            *OutCount = (Launcher != nullptr) ? Launcher->GetAmmoCount() : 1;
        }
        Equip = EqObj->EquipManager.Traverse(EquipWalk);
    }

    // Collision groups: root != -1 (GetRootIndex asked twice), then the group's type name.
    CArchGrpTraverser GroupWalk;
    CArchGroup* Group = EqObj->ArchGroups.Traverse(GroupWalk);
    while (Group != nullptr)
    {
        if (Group->GetRootIndex() != -1 &&
            Pick.Test(InstanceFromIndex(Group->GetRootIndex()), kSubTargetPickSlack))
        {
            *OutSubId = Group->GetID();
            Result = HudCollisionGroupTypeIds(Group->GetCollisionGroupType());
            *OutCount = 1;
        }
        Group = EqObj->ArchGroups.Traverse(GroupWalk);
    }
    return Result;
}

bool HudTargetBase::HitTest(float const* Ray)
{
    // LAB_004e7d20.
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        return false;
    }
    if (GetPlayerObjRW() == nullptr)
    {
        return false;
    }
    if (IsLetterboxVisible())
    {
        return false;
    }

    if (bExpanded != 0)
    {
        // The ray scaled to the hit plane's depth, tested against the box around HitCenter.
        float const Scale = HitCenter.Z / Ray[2];
        float const PlaneX = Scale * Ray[0];
        float const PlaneY = Scale * Ray[1];
        float const Dx = PlaneX - (HitCenter.X - HitHalfWidth);
        float const Dy = (HitCenter.Y + HitHalfHeight) - PlaneY;
        // DAT_005c7474 = 0.0f. Each test falls through to the base on false or unordered.
        if (Dx > 0.0f && Dx < HitHalfWidth + HitHalfWidth && Dy > 0.0f &&
            HitHalfHeight + HitHalfHeight > Dy)
        {
            return true;
        }
    }
    // FUN_005a0340
    return WinControl::HitTest(Ray);
}

void HudTargetBase::Update()
{
    // FUN_004e7980: Win3D::Update FUN_005a17b0, the slide FUN_0055e170, then (expanded) the
    // sub-target tool tip (tail jump to FUN_004e75b0).
    Win3D::Update();
    ButtonMover::Update();
    if (bExpanded != 0)
    {
        UpdateSubTargetToolTip();
    }
}

void HudTargetBase::Draw()
{
    // FUN_004e7890.
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        return;
    }
    if (GetPlayerObjRW() == nullptr)
    {
        return;
    }
    if (IsLetterboxVisible())
    {
        return;
    }

    if (bExpanded != 0 && !ButtonMover::IsActive())
    {
        // Visible children (flag bit 1): AsWinControl (slot 2), Draw (slot 13); right after the
        // view panel, HUDItem slot 14.
        for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        {
            if ((Child->Flags & 2) == 0)
            {
                continue;
            }
            WinControl* const AsControl = Child->AsWinControl();
            Child->Draw();
            if (AsControl == ViewPanel)
            {
                Slot14();
            }
        }
    }
    else
    {
        for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        {
            if ((Child->Flags & 2) == 0)
            {
                continue;
            }
            Child->AsWinControl();
            Child->Draw();
        }
    }
    // Tail jump through the WinControl face's slot 39 (+0x9c).
    Slot39();
}

intptr_t HudTargetBase::SetProperty(int Msg, uintptr_t A, uintptr_t /*B*/)
{
    // FUN_004e7950: message 0x26 from the toggle button -> HUDItem slot 12; always 0, the base
    // is not called.
    if (Msg == 0x26 && A == reinterpret_cast<uintptr_t>(ToggleButton))
    {
        Slot12();
    }
    return 0;
}

void HudTargetBase::OnMoveFinished()
{
    // FUN_004e79b0: still collapsed (the opening slide just ended) -> HUDItem slot 13(0), i.e.
    // un-hide the panel controls; then the flag re-read and inverted.
    if (bExpanded == 0)
    {
        Slot13(false);
    }
    bExpanded = (bExpanded == 0) ? 1 : 0;
}

CEquip* HudTargetBase::FindTargetEquip(unsigned short SubId)
{
    // FUN_004e7e20 -- on the HUDItem target (+0x28), not the view object.
    if (SubId == SUBOBJ_ID_NONE)
    {
        return nullptr;
    }
    CObject* const Object = CObjectOfWatcher(HUDItem::Target);
    if (Object == nullptr)
    {
        return nullptr;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & 0x503u) != 0x503u)
    {
        return nullptr;
    }
    return static_cast<CEqObj*>(Object)->EquipManager.FindByID(SubId);
}
