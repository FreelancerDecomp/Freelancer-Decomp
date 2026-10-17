// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ui_ship_view_control.h"

#include "ui_page.h"      // UiWidgetMathEngine (DAT_0067dbc0)
#include "ui_page_desc.h" // FUiPayloadBounds
#include "win_factory.h"

#include "x86math.h"

namespace
{
    // .data:006156f4 / .data:00615704 -- the RGBA the Create of UIShipViewControl hands to
    // FUN_00586a70 (rect colour) and FUN_00586af0 (wire colour); both tables hold the same four
    // floats (checked in PE bytes) and nothing writes them.
    float const kUiShipViewRectColor[4] = {0.529411972f, 0.764706016f, 0.874509990f, 0.486274987f};
    float const kUiShipViewWireColor[4] = {0.529411972f, 0.764706016f, 0.874509990f, 0.486274987f};
}

UiShipViewControl::UiShipViewControl()
{
    // FUN_0059ccf0 0x0059cd2b..0x0059cd36: the ShipViewControl ctor, then the two vftables.
    // +0x570..+0x578 are not written until Create (retail leaves the heap bytes); the port
    // starts them at 0.
    HalfExtent[0] = 0.0f;
    HalfExtent[1] = 0.0f;
    HalfExtent[2] = 0.0f;
}

UiShipViewControl::~UiShipViewControl()
{
    // FUN_0059bff0: the vftables back to UIShipViewControl's, then ShipViewControl's
    // destructor (FUN_00583c30) -- nothing of its own.
}

bool UiShipViewControl::Create(Win3D* NewParent, void* Payload)
{
    // FUN_0059c010 (RET 8): ShipViewControl's slot 9 (FUN_00583e30) with no INI reader.
    bool const bCreated = ShipViewControl::Create(NewParent, nullptr);
    if (!bCreated)
        return bCreated;

    // 0x0059c02d..0x0059c056: with a payload, the half extent {Width * 0.5 (DAT_005c75e0),
    // Height * -0.5 (DAT_005cef60), 0}.
    if (Payload != nullptr)
    {
        FUiPayloadBounds const* const Bounds = static_cast<FUiPayloadBounds const*>(Payload);
        HalfExtent[2] = 0.0f;
        HalfExtent[0] = Bounds->Right * 0.5f;
        HalfExtent[1] = Bounds->Bottom * -0.5f;
    }

    // 0x0059c05c..0x0059c0ed: the view set up through ShipViewControl's setters.
    float Vector[3];
    Vector[0] = 0.0f;
    Vector[1] = 0.0f;
    Vector[2] = 30.0f; // 0x41f00000
    SetRotSpeed(Vector); // FUN_005868e0
    Vector[0] = 0.0f;
    Vector[1] = 270.0f; // 0x43870000
    Vector[2] = 0.0f;
    SetObjectYpr(Vector); // FUN_00586900
    Vector[0] = 0.0f;
    Vector[1] = 0.0f;
    Vector[2] = 0.0f;
    SetAttachPosition(Vector);               // FUN_00586a10
    SetShowRect(false);                      // FUN_00586a30(0)
    SetShowWireframe(true);                  // FUN_00586a50(1)
    SetRectColor(kUiShipViewRectColor);      // FUN_00586a70(&DAT_006156f4)
    SetWireColor(kUiShipViewWireColor);      // FUN_00586af0(&DAT_00615704)
    return bCreated;
}

void UiShipViewControl::SetTransform(float const* Transform)
{
    // LAB_0059c100 (RET 4). The 12-float block (rotation, then the translation at +0x24) is
    // copied to a local; the half extent turned by the rotation (DAT_0067dbc0 slot 11 (+0x2c),
    // the engine made on first use by FUN_005b73e0) is the view size, and the box's corner --
    // that turned extent plus the translation, each sum rounded to float (FADD / FSTP) -- the
    // position.
    float Block[12];
    for (int Index = 0; Index < 12; ++Index)
        Block[Index] = Transform[Index];
    float const Translation[3] = {Transform[9], Transform[10], Transform[11]};

    FVector Turned;
    FVector Source;
    Source.X = HalfExtent[0];
    Source.Y = HalfExtent[1];
    Source.Z = HalfExtent[2];
    UiWidgetMathEngine()->TransformMatrix(Turned, *reinterpret_cast<FMatrix const*>(Transform),
                                          Source);

    float Corner[3];
    Corner[0] = Turned.X + Translation[0];
    Corner[1] = Turned.Y + Translation[1];
    Corner[2] = Turned.Z + Translation[2];
    float const ViewExtent[3] = {Turned.X, Turned.Y, Turned.Z};

    SetOrientation(Block); // slot 7 (+0x1c) with the local copy
    SetPosition(Corner);   // slot 5 (+0x14)
    SetViewSize(ViewExtent); // FUN_005868c0
}

// Retail DAT_0067da3c -- WINFACTORY<UIShipViewControl> (static init LAB_0059c330 -> FUN_0059cc80
// ("UIShipViewControl"), atexit LAB_0059c350 -> FUN_00579380; Create FUN_0059ccf0).
WINFACTORY<UiShipViewControl> G_UIShipViewControlFactory("UIShipViewControl");
