// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "gf_thorn_marker.h"

#include "gf_thorn_light.h" // the reference forwarders shared with the light reference

#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------------------
// Thorn_Marker
// ---------------------------------------------------------------------------------------

// FUN_00453690. Base ctor, vptrs, then +0x6c..+0x74 = 0 (0x004536ed..0x00453718) and +0x78 the
// identity (0x0045371b..0x0045373a).
FThornMarker::FThornMarker(Csys const& ScriptFrame, FGfScript* InOwner,
                           FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc)
{
    World.Pos.x = 0.0f;
    World.Pos.y = 0.0f;
    World.Pos.z = 0.0f;
    MatrixSetIdentity(World.Orient);
}

// LAB_004537d0 (identity, RET 8). trans_from_frame of Transform+0x24 into +0x6c, then of the
// rotation into +0x78.
GENRESULT DACOM_API FThornMarker::SetTransform(float const* Transform12)
{
    World.Pos = Frame.trans_from_frame(*reinterpret_cast<Vector const*>(Transform12 + 9));
    World.Orient = Frame.trans_from_frame(*reinterpret_cast<Matrix const*>(Transform12));
    return GR_OK;
}

// LAB_004538b0 (identity, RET 8). trans_to_frame of +0x6c into Out+0x24, of +0x78 into Out+0.
GENRESULT DACOM_API FThornMarker::GetTransform(float* OutTransform12)
{
    Vector const LocalPos = Frame.trans_to_frame(World.Pos);
    OutTransform12[9] = LocalPos.x;
    OutTransform12[10] = LocalPos.y;
    OutTransform12[11] = LocalPos.z;
    Matrix const LocalOrient = Frame.trans_to_frame(World.Orient);
    memcpy(OutTransform12, &LocalOrient, sizeof(float) * 9);
    return GR_OK;
}

// LAB_00453750 (geo facet, RET 8). Facet +0x20 = the frame, +0x60 = +0x6c.
GENRESULT DACOM_API FThornMarker::SetPosition(float* Position)
{
    World.Pos = Frame.trans_from_frame(*reinterpret_cast<Vector const*>(Position));
    return GR_OK;
}

// LAB_00453790 (geo facet, RET 8). REP MOVSD 9 into facet +0x6c (= +0x78).
GENRESULT DACOM_API FThornMarker::SetOrientation(float* Orient9)
{
    World.Orient = Frame.trans_from_frame(*reinterpret_cast<Matrix const*>(Orient9));
    return GR_OK;
}

// LAB_00453830 (geo facet, RET 8).
GENRESULT DACOM_API FThornMarker::GetPosition(float* OutPosition)
{
    Vector const Local = Frame.trans_to_frame(World.Pos);
    OutPosition[0] = Local.x;
    OutPosition[1] = Local.y;
    OutPosition[2] = Local.z;
    return GR_OK;
}

// LAB_00453870 (geo facet, RET 8).
GENRESULT DACOM_API FThornMarker::GetOrientation(float* OutOrient9)
{
    Matrix const Local = Frame.trans_to_frame(World.Orient);
    memcpy(OutOrient9, &Local, sizeof(float) * 9);
    return GR_OK;
}

// FUN_00450870.
void* FThornMarker::GetEngineInstance()
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
}

// ---------------------------------------------------------------------------------------
// Thorn_MarkerReference
// ---------------------------------------------------------------------------------------

// FUN_00453910. Base ctor, vptrs, 0x00453967 +0x6c = 0.
FThornMarkerReference::FThornMarkerReference(Csys const& ScriptFrame, FGfScript* InOwner,
                                             FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Target(nullptr)
{
}

GENRESULT DACOM_API FThornMarkerReference::SetTransform(float const* Transform12)
{
    return ThornReferenceSetTransform(Target, Transform12);
}

GENRESULT DACOM_API FThornMarkerReference::GetTransform(float* OutTransform12)
{
    return ThornReferenceGetTransform(Target, OutTransform12);
}

GENRESULT DACOM_API FThornMarkerReference::SetPosition(float* Position)
{
    return ThornReferenceSetPosition(Target, Position);
}

GENRESULT DACOM_API FThornMarkerReference::SetOrientation(float* Orient9)
{
    return ThornReferenceSetOrientation(Target, Orient9);
}

GENRESULT DACOM_API FThornMarkerReference::GetPosition(float* OutPosition)
{
    return ThornReferenceGetPosition(Target, OutPosition);
}

GENRESULT DACOM_API FThornMarkerReference::GetOrientation(float* OutOrient9)
{
    return ThornReferenceGetOrientation(Target, OutOrient9);
}

// LAB_00450cd0.
void FThornMarkerReference::Stop()
{
    Target = nullptr;
}

// LAB_00453550.
void* FThornMarkerReference::GetEngineInstance()
{
    return ThornReferenceGetEngineInstance(Target);
}
