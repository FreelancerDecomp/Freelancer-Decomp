// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "win3d.h"
#ifdef FREELANCER_WALK_MODE
#include "base_walk.h"
#endif

#include "client_cliobj.h"   // ResolveMaterialEntryFromFs (FUN_0041ade0) / ReleaseMaterialEntrySlot
#include "client_mesh_load.h"
#include "engine_light.h"
#include "hud_command_dispatch.h"
#include "hud_widescreen.h"    // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "event_handler.h"
#include "input_manager.h"
#include "nn_rollover.h"
#include "nn_tooltip.h"
#include "nn_ui_pulse.h"
#include "space_camera.h"
#include "space_scene_obj_draw.h" // SpaceCameraSetNearFar (FUN_0040fb60)
#include "win3d_engine_submit.h"
#include "win3d_immediate.h"
#include "win3d_render.h"
#include "win3d_renderables.h"
#include "wincontrol_base.h"

#include "common_csys.h"
#include "DALib.h"
#include "readfile.h"             // IFileSystem::CreateInstance (the "FileSystem" instance)
#include "render/IRenderPipeline.h"
#include "render/shading.h"
#include "x86math.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------
// Manager state (Win3D.cpp .data / .bss)
// ---------------------------------------------------------------------------------------

namespace
{
    // FUN_0059e810's list: a count and a doubly linked chain of 0xc-byte nodes
    // { prev +0, next +4, payload +8 }.
    struct FWin3DListNode
    {
        FWin3DListNode* Prev;
        FWin3DListNode* Next;
        void* Payload;
    };

    struct FWin3DList
    {
        int Count;            // +0
        FWin3DListNode* Head; // +4
        FWin3DListNode* Tail; // +8

        // The atexit bodies LAB_0059d480 (registry) and LAB_0059d4f0 (pending list), one code
        // shape: walk from Head freeing each node (FUN_005b7e1d, the next link read first),
        // then Count = 0, Tail = 0, Head = 0 in that order. The payloads are not touched.
        ~FWin3DList()
        {
            FWin3DListNode* Node = Head;
            while (Node != nullptr)
            {
                FWin3DListNode* const Next = Node->Next;
                operator delete(Node);
                Node = Next;
            }
            Count = 0;
            Tail = nullptr;
            Head = nullptr;
        }
    };

    // DAT_0067dbe4 / dbe8 / dbec -- the registry, payload = Win3D*. Static init LAB_0059d460
    // zeroes it; atexit LAB_0059d480 frees the nodes (~FWin3DList).
    FWin3DList G_Registry = {0, nullptr, nullptr};
    // DAT_0067dbd8 / dbdc / dbe0 -- nodes queued for removal, payload = registry node.
    // Static init LAB_0059d4d0, atexit LAB_0059d4f0 (~FWin3DList).
    FWin3DList G_PendingRemove = {0, nullptr, nullptr};

    // DAT_0067dcdc keyboard focus, DAT_0067dce0 capture stack head (link at +0x38),
    // DAT_0067dcd8 object under the mouse.
    Win3D* G_KeyFocus = nullptr;
    Win3D* G_CaptureHead = nullptr;
    Win3D* G_Hover = nullptr;

    // DAT_0067dbcc / d0 / d4 -- the mouse ray. LAB_0059d540 seeds (0, 0, -1); the producer is
    // the Win3D input handler FUN_0059d140 case 2.
    float G_MouseRay[3] = {0.0f, 0.0f, -1.0f};

    // DAT_0067dcd0 -- how many registry nodes the last FUN_0059e080 walked.
    int G_UpdateNodeCount = 0;
    // DAT_0067dcf0 -- read by FUN_0059e300 (JG skips the whole draw). No writer exists in
    // the binary (XREF[1]: 0059e30a R), so it stays zero.
    int G_DrawSuppressCount = 0;

    // FUN_0059e810 (thiscall on the list, RET 4): operator_new(0xc); a null allocation still
    // runs the tail with node 0, as retail does.
    bool ListPushBack(FWin3DList* List, void* Payload)
    {
        FWin3DListNode* Node = static_cast<FWin3DListNode*>(operator new(sizeof(FWin3DListNode)));
        if (Node != nullptr)
        {
            Node->Next = nullptr;
            Node->Prev = nullptr;
            Node->Payload = Payload;
        }
        if (List->Tail != nullptr)
            List->Tail->Next = Node;
        else
            List->Head = Node;
        Node->Prev = List->Tail;
        List->Tail = Node;
        Node->Next = nullptr;
        List->Count = List->Count + 1;
        return true;
    }

    // FUN_0059e7d0 (thiscall on a LinkTree's child-head field, RET 4): unlink Child from the
    // sibling chain starting at *ChildHeadField. Silently returns when it is not there.
    void LinkTreeRemoveChild(Win3D** ChildHeadField, Win3D* Child)
    {
        Win3D* Walk = *ChildHeadField;
        Win3D* Prev = nullptr;
        if (Walk == nullptr)
            return;
        while (Walk != Child)
        {
            Prev = Walk;
            Walk = Walk->NextSibling;
            if (Walk == nullptr)
                return;
        }
        if (Prev != nullptr)
            Prev->NextSibling = Walk->NextSibling;
        else
            *ChildHeadField = Walk->NextSibling;
    }

    // The unlink-and-free of one registry node, repeated inline four times in retail
    // (0059d6f0.., 0059d808.., 0059e1f0.., 0059e280..).
    void RegistryEraseNode(FWin3DListNode* Node)
    {
        FWin3DListNode* const Prev = Node->Prev;
        FWin3DListNode* const Next = Node->Next;
        Node->Payload = nullptr;
        operator delete(Node);
        if (Prev != nullptr)
            Prev->Next = Next;
        else
            G_Registry.Head = Next;
        if (Next != nullptr)
            Next->Prev = Prev;
        else
            G_Registry.Tail = Prev;
        G_Registry.Count = G_Registry.Count - 1;
    }

    // 0059e1e1..0059e257 / 0059e272..0059e2ef: erase every registry node the pending list
    // names, free the pending nodes, then clear the pending list header.
    void DrainPendingRemove()
    {
        for (FWin3DListNode* Pending = G_PendingRemove.Head; Pending != nullptr;
             Pending = Pending->Next)
        {
            FWin3DListNode* const Node = static_cast<FWin3DListNode*>(Pending->Payload);
            if (Node != nullptr)
                RegistryEraseNode(Node);
        }
        FWin3DListNode* Pending = G_PendingRemove.Head;
        while (Pending != nullptr)
        {
            FWin3DListNode* const Next = Pending->Next;
            operator delete(Pending);
            Pending = Next;
        }
        G_PendingRemove.Count = 0;
        G_PendingRemove.Tail = nullptr;
        G_PendingRemove.Head = nullptr;
    }

    // The sorted draw used by FUN_0059e300 (roots) and FUN_0059e5c0 / FUN_0059e6e0 (children):
    // zero layer draws at once, the rest go into a 0x400-entry stack array ordered by the
    // unsigned layer, then by +0x68 with an ordered-less compare (TEST AH,5 / JNP), equal keys
    // keeping insertion order. Retail checks the capacity only on the append path
    // (0059e656 / 0059e479 / 0059e781); the insertion path writes past the array when it is
    // full, and so does the port (the extra bound it used to have was removed 2026-09-30).
    void InsertSorted(Win3D** Sorted, unsigned* Count, Win3D* Object)
    {
        unsigned const Layer = Object->DrawLayer;
        unsigned const Size = *Count;
        if (Size == 0 || Layer > Sorted[Size - 1]->DrawLayer)
        {
            if (Size >= 0x400)
                return;
            Sorted[Size] = Object;
            *Count = Size + 1;
            return;
        }

        unsigned InsertAt = 0;
        while (InsertAt < Size)
        {
            Win3D* const Other = Sorted[InsertAt];
            if (Layer < Other->DrawLayer)
                break;
            if (Layer == Other->DrawLayer && Object->Position[2] < Other->Position[2])
                break;
            InsertAt = InsertAt + 1;
        }
        // 0x0059e635..0x0059e662: no capacity check on the insertion path (retail bounds only
        // the append at 0x0059e656); more than 0x400 visible layered children of one parent
        // would overrun the stack array in retail too.
        if (Size - InsertAt != 0)
            memmove(&Sorted[InsertAt + 1], &Sorted[InsertAt], (Size - InsertAt) * sizeof(Win3D*));
        Sorted[InsertAt] = Object;
        *Count = Size + 1;
    }

    // Body shared by FUN_0059e5c0 and FUN_0059e6e0.
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only (hud_widescreen.h): every draw, update, hit test and mouse call on an object
    // runs with the Win3D camera shifted for that object's HUD corner window (no-op for
    // everything else, and on a 4:3 surface).
    void DrawWidescreen(Win3D* Object)
    {
        FWidescreenHudShiftScope const Scope(WidescreenHudShiftPixelsFor(Object), true);
        Object->Draw();
    }

    void UpdateWidescreen(Win3D* Object)
    {
        FWidescreenHudShiftScope const Scope(WidescreenHudShiftPixelsFor(Object), false);
        if ((Object->Flags & kWin3DActive) != 0)
            Object->Update();
        else
            Object->UpdateInactive();
    }

    // Ray is the frame's mouse ray (DAT_0067dbcc); a shifted object is tested against the
    // cursor's ray through the shifted camera instead.
    bool HitTestWidescreen(Win3D* Object, float const* Ray)
    {
        float const Shift = WidescreenHudShiftPixelsFor(Object);
        if (Shift == WidescreenHudCurrentShift())
            return Object->HitTest(Ray);
        FWidescreenHudShiftScope const Scope(Shift, false);
        float Shifted[3];
        return Object->HitTest(Win3DGetMouseRay(Shifted));
    }

    enum class EMouseCall
    {
        Enter,
        Over,
        Leave
    };

    void MouseCallWidescreen(Win3D* Object, EMouseCall Call)
    {
        FWidescreenHudShiftScope const Scope(WidescreenHudShiftPixelsFor(Object), false);
        float Ray[3];
        Win3DGetMouseRay(Ray);
        if (Call == EMouseCall::Enter)
            Object->MouseEnter(Ray);
        else if (Call == EMouseCall::Over)
            Object->MouseOver(Ray);
        else
            Object->MouseLeave(Ray);
    }

    bool MouseEventWidescreen(Win3D* Object, FWin3DMouseEvent const* Event)
    {
        FWidescreenHudShiftScope const Scope(WidescreenHudShiftPixelsFor(Object), false);
        return Object->MouseEvent(Event);
    }

    bool WheelWidescreen(Win3D* Object, int Wheel)
    {
        FWidescreenHudShiftScope const Scope(WidescreenHudShiftPixelsFor(Object), false);
        return Object->Slot18(Wheel);
    }
#endif

    void DrawChildrenSorted(Win3D* Parent)
    {
        Win3D* Sorted[0x400];
        unsigned Count = 0;
        for (Win3D* Child = Parent->ChildHead; Child != nullptr; Child = Child->NextSibling)
        {
            if ((Child->Flags & kWin3DVisible) == 0)
                continue;
            if (Child->DrawLayer == 0)
            {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
                DrawWidescreen(Child);
#else
                Child->Draw();
#endif
                continue;
            }
            InsertSorted(Sorted, &Count, Child);
        }
        for (unsigned Index = 0; Index < Count; ++Index)
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            DrawWidescreen(Sorted[Index]);
#else
            Sorted[Index]->Draw();
#endif
    }

    char const* const kWin3DSourceFile = "E:\\FL\\Scratch\\Source\\Client\\Interface\\Win3D.cpp";
}

int G_Win3DLiveCount = 0;

// DAT_0067dccc -- nesting count raised by the modal NN dialogs' ctors (004746db, 00474956,
// 0047481e) and lowered by their dtors (004721ea, 0047256a); while it is non-zero
// FUN_0059e080 updates only the WinControl holding the mouse.
int G_ModalCaptureCount = 0;

// ---------------------------------------------------------------------------------------
// Win3D
// ---------------------------------------------------------------------------------------

Win3D::Win3D()
{
    // FUN_004b6790. Name[1..31], Pad6d and the upper flag bits are left as allocated.
    ChildHead = nullptr;
    NextSibling = nullptr;
    Parent = nullptr;
    NameLength = 0;
    Name[0] = '\0';
    Orientation[7] = 0.0f;
    Orientation[6] = 0.0f;
    Orientation[5] = 0.0f;
    Orientation[3] = 0.0f;
    Orientation[2] = 0.0f;
    Orientation[1] = 0.0f;
    Orientation[8] = 1.0f;
    Orientation[4] = 1.0f;
    Orientation[0] = 1.0f;
    Position[2] = 0.0f;
    Position[1] = 0.0f;
    Position[0] = 0.0f;
    ClassName = nullptr;
    DrawLayer = 0;
    CaptureNext = nullptr;
    HitPriority = 0;
    DrawSubLayer = 0;
    Flags = static_cast<unsigned char>((Flags & 0xf8) | kWin3DPickable);
    G_Win3DLiveCount = G_Win3DLiveCount + 1;
}

Win3D::~Win3D()
{
    // FUN_004b4fd0: --DAT_0067dcd4, flags &= 0xfc, slot 10 on every child (children are not
    // deleted here), FUN_0059dc00(this).
    G_Win3DLiveCount = G_Win3DLiveCount - 1;
    Flags = static_cast<unsigned char>(Flags & 0xfc);
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->Destroy();
    Win3DUnregister(this);
}

bool Win3D::Slot1(unsigned /*A*/, unsigned /*B*/)
{
    // LAB_0043d8a0: XOR AL,AL / RET 8.
    return false;
}

WinControl* Win3D::AsWinControl()
{
    // FUN_004b5010: XOR EAX,EAX / RET.
    return nullptr;
}

bool Win3D::IsDescendantOf(Win3D* Ancestor)
{
    // LAB_0043d8b0: parent == Ancestor → true; parent → tail-jump to parent's slot 3.
    Win3D* const Up = Parent;
    if (Up == Ancestor)
        return true;
    if (Up != nullptr)
        return Up->IsDescendantOf(Ancestor);
    return false;
}

bool Win3D::HitTest(float const* /*Ray*/)
{
    // FUN_0053f330: XOR AL,AL / RET 4.
    return false;
}

void Win3D::SetPosition(float const* NewPosition)
{
    // LAB_004b5020: three dword copies to +0x60.
    Position[0] = NewPosition[0];
    Position[1] = NewPosition[1];
    Position[2] = NewPosition[2];
}

bool Win3D::Slot6(unsigned /*A*/)
{
    // LAB_0043d8e0: MOV AL,1 / RET 4.
    return true;
}

void Win3D::SetOrientation(float const* NewOrientation)
{
    // FUN_004b5040: REP MOVSD, nine dwords to +0x3c.
    memmove(Orientation, NewOrientation, sizeof(Orientation));
}

void Win3D::SetTransform(float const* Transform)
{
    // LAB_00595f70: virtual slot 7 (Transform), then virtual slot 5 (Transform + 9 floats).
    SetOrientation(Transform);
    SetPosition(Transform + 9);
}

bool Win3D::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_004b5060 (RET 8): FUN_0059dd00(this, NewParent), FUN_0059dbf0(this),
    // flags = (flags & 0xfb) | 3, return 1.
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & 0xfb) | kWin3DActive | kWin3DVisible);
    return true;
}

void Win3D::Destroy()
{
    // FUN_004b5090.
    Flags = static_cast<unsigned char>(Flags & 0xfc);
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->Destroy();
    Win3DUnregister(this);
}

void Win3D::Update()
{
    // FUN_005a17b0: the next sibling is read before each call.
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        UpdateWidescreen(Child);
#else
        if ((Child->Flags & kWin3DActive) != 0)
            Child->Update();
        else
            Child->UpdateInactive();
#endif
        Child = Next;
    }
}

void Win3D::UpdateInactive()
{
    // LAB_004682f0: the next sibling is read after each call.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
    {
        if ((Child->Flags & kWin3DActive) == 0)
        {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            UpdateWidescreen(Child);
#else
            Child->UpdateInactive();
#endif
        }
    }
}

void Win3D::Draw()
{
    // FUN_0059e5c0.
    Win3DDrawChildren(this);
}

void Win3D::MouseOver(float const* /*Ray*/)
{
    // FUN_0053e4a0: RET 4.
}

void Win3D::MouseEnter(float const* /*Ray*/)
{
    // FUN_0053e4a0: RET 4.
}

void Win3D::MouseLeave(float const* /*Ray*/)
{
    // FUN_0053e4a0: RET 4.
}

void Win3D::Slot17()
{
    // FUN_00401e90: RET.
}

bool Win3D::Slot18(int /*Value*/)
{
    // FUN_0053f330: XOR AL,AL / RET 4.
    return false;
}

bool Win3D::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_004b50c0: parent → tail-jump to its slot 19 with the same argument; else false.
    Win3D* const Up = Parent;
    if (Up != nullptr)
        return Up->MouseEvent(Event);
    return false;
}

bool Win3D::KeyEvent(unsigned /*Key*/, unsigned char* /*Modifiers*/)
{
    // LAB_0043d8a0: XOR AL,AL / RET 8.
    return false;
}

bool Win3D::Slot21(unsigned /*A*/, unsigned /*B*/, unsigned /*C*/, unsigned /*D*/)
{
    // LAB_0043d8f0: XOR AL,AL / RET 0x10.
    return false;
}

bool Win3D::Slot22(unsigned /*A*/, unsigned /*B*/)
{
    // LAB_0043d8a0: XOR AL,AL / RET 8.
    return false;
}

bool Win3D::Slot23(unsigned /*Msg*/, unsigned /*A*/, unsigned /*B*/)
{
    // LAB_004b50e0: XOR AL,AL / RET 0xc.
    return false;
}

void Win3D::Slot24(unsigned A)
{
    // LAB_0043d900: slot 24(A) on every child.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->Slot24(A);
}

float Win3D::Slot25()
{
    // LAB_0043d930: FLD DAT_005c75dc (1.0f).
    return 1.0f;
}

float Win3D::Slot26()
{
    // LAB_0043d930 (the same body).
    return 1.0f;
}

int Win3D::GetHitPriority()
{
    // FUN_0043d940.
    return HitPriority;
}

void Win3D::SetName(char const* NewName)
{
    // 0059dba3..0059dbe1.
    if (NewName == nullptr)
    {
        NameLength = 0;
        Name[0] = '\0';
        return;
    }
    unsigned Length = static_cast<unsigned>(strlen(NewName));
    if (Length >= 0x20)
        Length = 0x1f;
    memcpy(Name, NewName, Length);
    Name[Length] = '\0';
    NameLength = Length;
}

// ---------------------------------------------------------------------------------------
// Manager
// ---------------------------------------------------------------------------------------

void Win3DSetKeyFocus(Win3D* Focus)
{
    // FUN_0059d8c0.
    G_KeyFocus = Focus;
}

void Win3DPushCapture(Win3D* Object)
{
    // FUN_0059d8d0. A non-zero +0x38 means the object may already be on the stack: find it
    // (keeping the previous entry) and move it to the head. A zero +0x38 means it is either
    // the tail or absent -- either way it is pushed without a search, as retail does.
    Win3D* const OldNext = Object->CaptureNext;
    if (OldNext == nullptr)
    {
        Object->CaptureNext = G_CaptureHead;
        G_CaptureHead = Object;
        return;
    }

    Win3D* const Head = G_CaptureHead;
    Win3D* Walk = Head;
    Win3D* Prev = nullptr;
    while (Walk != nullptr)
    {
        if (Walk == Object)
        {
            if (Prev != nullptr)
            {
                Prev->CaptureNext = OldNext;
                Object->CaptureNext = nullptr;
                Object->CaptureNext = G_CaptureHead;
                G_CaptureHead = Object;
                return;
            }
            G_CaptureHead = OldNext;
            Object->CaptureNext = nullptr;
            Object->CaptureNext = G_CaptureHead;
            G_CaptureHead = Object;
            return;
        }
        Prev = Walk;
        Walk = Walk->CaptureNext;
    }
    Object->CaptureNext = Head;
    G_CaptureHead = Object;
}

void Win3DRemoveCapture(Win3D* Object)
{
    // FUN_0059d960.
    Win3D* Walk = G_CaptureHead;
    Win3D* Prev = nullptr;
    while (Walk != nullptr)
    {
        if (Walk == Object)
        {
            if (Prev != nullptr)
            {
                Prev->CaptureNext = Object->CaptureNext;
                Object->CaptureNext = nullptr;
                return;
            }
            G_CaptureHead = Object->CaptureNext;
            Object->CaptureNext = nullptr;
            return;
        }
        Prev = Walk;
        Walk = Walk->CaptureNext;
    }
}

Win3D* Win3DGetCapture()
{
    // FUN_0059d9b0.
    return G_CaptureHead;
}

Win3D* Win3DFindByClassName(char const* ClassName)
{
    // FUN_0059d9c0: nodes with a null payload or a null +0x10 are skipped.
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr || Object->ClassName == nullptr)
            continue;
        if (_stricmp(Object->ClassName, ClassName) == 0)   // DACOM.DLL::stricmp
            return Object;
    }
    return nullptr;
}

Win3D* Win3DFindByName(char const* Name)
{
    // FUN_0059da10.
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr)
            continue;
        if (_stricmp(Object->Name, Name) == 0)             // DACOM.DLL::stricmp
            return Object;
    }
    return nullptr;
}

Win3D* Win3DGetFirstRegistered()
{
    // FUN_0059da50.
    if (G_Registry.Head != nullptr)
        return static_cast<Win3D*>(G_Registry.Head->Payload);
    return nullptr;
}

float* Win3DGetMouseRay(float* Out)
{
    // FUN_0059da70.
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only: inside a shifted HUD window the ray comes through the shifted camera.
    if (WidescreenHudCurrentShift() != 0.0f)
    {
        WidescreenHudCursorRay(Out);
        return Out;
    }
#endif
    Out[0] = G_MouseRay[0];
    Out[1] = G_MouseRay[1];
    Out[2] = G_MouseRay[2];
    return Out;
}

float* Win3DMouseRayStorage()
{
    return G_MouseRay;
}

FSpaceCamera* Win3DGetCamera()
{
    // FUN_0059da60: MOV EAX, &DAT_0067dbf8. The port's DAT_0067dbf8 is G_Win3DSceneCamera, built
    // at static init as LAB_0059d020 builds it (win3d_engine_submit.cpp).
    return GetWin3DSceneCamera();
}

void Win3DSetViewportSize(int Width, int Height)
{
    // FUN_0059dcc0 (cdecl w, h): {0, 0, w - 1, h - 1} into FUN_0040fb80 on DAT_0067dbf8 with a
    // second argument of 1 (dead in the callee).
    int ViewRect[4];
    ViewRect[0] = 0;
    ViewRect[1] = 0;
    ViewRect[2] = Width - 1;
    ViewRect[3] = Height - 1;
    SpaceCameraSetViewportEx(Win3DGetCamera(), ViewRect, 1);
}

Win3D* Win3DGetHover()
{
    // FUN_0059daa0.
    return G_Hover;
}

void Win3DRegister(Win3D* Object)
{
    // FUN_0059dbf0.
    ListPushBack(&G_Registry, Object);
}

bool Win3DUnregister(Win3D* Object)
{
    // FUN_0059dc00.
    if ((Object->Flags & kWin3DUnregistered) != 0)
        return true;
    Object->Flags = static_cast<unsigned char>(Object->Flags | kWin3DUnregistered);
    if (G_KeyFocus == Object)
        G_KeyFocus = nullptr;
    if (G_CaptureHead == Object)
        G_CaptureHead = nullptr;
    if (G_Hover == Object)
        G_Hover = nullptr;

    FWin3DListNode* Node = G_Registry.Head;
    while (Node != nullptr && Node->Payload != Object)
        Node = Node->Next;
    if (Node == nullptr || Node->Payload == nullptr)
        return false;

    // 0059dc65..0059dc96: FUN_0059e7d0 inlined on the parent's child list, then +4 = 0.
    Win3D* const Up = Object->Parent;
    if (Up != nullptr)
    {
        LinkTreeRemoveChild(&Up->ChildHead, Object);
        Object->Parent = nullptr;
    }
    ListPushBack(&G_PendingRemove, Node);
    Node->Payload = nullptr;
    return true;
}

void Win3DReparent(Win3D* Child, Win3D* NewParent)
{
    // FUN_0059dd00 (cdecl).
    if (Child == nullptr)
        return;
    Win3D* const OldParent = Child->Parent;
    if (OldParent == NewParent)
        return;
    if (OldParent != nullptr)
        LinkTreeRemoveChild(&OldParent->ChildHead, Child);
    Child->Parent = NewParent;
    if (NewParent == nullptr)
        return;
    Win3D* Tail = NewParent->ChildHead;
    if (Tail == nullptr)
    {
        Child->NextSibling = nullptr;
        NewParent->ChildHead = Child;
        return;
    }
    while (Tail->NextSibling != nullptr)
        Tail = Tail->NextSibling;
    Tail->NextSibling = Child;
    Child->NextSibling = nullptr;
}

bool Win3DDispatchKey(FHudCmdRecord const* Record)
{
    // FUN_0059dfd0 (cdecl, one argument). The modifier byte is built in the argument's own stack
    // slot, so bits 5..7 are whatever the low byte of the Record pointer holds
    // (0059dfd3 MOV AL,[ESP+param_1] / AND AL,0xe0); only bits 0..4 are written:
    //   bit0 = (+0x18 == 0), bit1 = +0x18 bit0, bit2 = (+0x08 & 3) != 0,
    //   bit3 = (+0x08 & 0xc) != 0, bit4 = (+0x08 & 0x30) != 0.
    unsigned char Modifiers =
        static_cast<unsigned char>(reinterpret_cast<uintptr_t>(Record) & 0xe0);
    unsigned char const Byte18 = Record->Byte18;
    Modifiers = static_cast<unsigned char>(Modifiers | ((Byte18 & 1) << 1));
    if (Byte18 == 0)
        Modifiers = static_cast<unsigned char>(Modifiers | 0x01);
    unsigned const Shift = Record->Modifiers;
    if ((Shift & 0x03) != 0)
        Modifiers = static_cast<unsigned char>(Modifiers | 0x04);
    if ((Shift & 0x0c) != 0)
        Modifiers = static_cast<unsigned char>(Modifiers | 0x08);
    if ((Shift & 0x30) != 0)
        Modifiers = static_cast<unsigned char>(Modifiers | 0x10);
    unsigned const Key = Record->Key;

    // 0059e02f: the focus object alone gets the key, and its answer is returned.
    if (G_KeyFocus != nullptr)
        return G_KeyFocus->KeyEvent(Key, &Modifiers);

    // 0059e04c..0059e071: otherwise every registered object in order until one takes it.
    bool bHandled = false;
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr)
            continue;
        bHandled = Object->KeyEvent(Key, &Modifiers) || bHandled;
        if (bHandled)
            break;
    }
    return bHandled;
}

Win3D* Win3DPick(float const* Ray)
{
    // FUN_0059dd80.
    Win3D* Best = nullptr;
    int BestDepth = -1;
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr)
            continue;
        unsigned char const ObjectFlags = Object->Flags;
        if ((ObjectFlags & kWin3DActive) == 0 || (ObjectFlags & kWin3DVisible) == 0 ||
            (ObjectFlags & kWin3DPickable) == 0)
            continue;

        // Every ancestor (and the object itself) must be visible.
        bool bVisibleChain = true;
        for (Win3D* Walk = Object; Walk != nullptr; Walk = Walk->Parent)
        {
            if ((Walk->Flags & kWin3DVisible) == 0)
            {
                bVisibleChain = false;
                break;
            }
        }
        if (!bVisibleChain)
            continue;

        int Depth = Object->GetHitPriority();
        if (Depth == 0)
        {
            for (Win3D* Walk = Object; Walk != nullptr; Walk = Walk->Parent)
                Depth = Depth + 1;
        }
        if (Depth > BestDepth)
        {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            if (HitTestWidescreen(Object, Ray))
#else
            if (Object->HitTest(Ray))
#endif
            {
                Best = Object;
                BestDepth = Depth;
            }
            continue;
        }
        if (Depth != BestDepth)
            continue;
        // 0059ddfc: FCOMP / TEST AH,0x41 / JNZ -- only a strictly greater +0x68 competes.
        if (!(Object->Position[2] > Best->Position[2]))
            continue;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        if (HitTestWidescreen(Object, Ray))
#else
        if (Object->HitTest(Ray))
#endif
        {
            Best = Object;
            BestDepth = Depth;
        }
    }
    return Best;
}

Win3D* Win3DPickWithin(Win3D* Capture, float const* Ray)
{
    // FUN_0059de50 (cdecl Capture, Ray). Unlike FUN_0059dd80 the ancestor-visibility walk runs
    // only after the depth comparison, and only on the "deeper" branch.
    Win3D* Best = nullptr;
    int BestDepth = -1;
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr)
            continue;
        unsigned char const ObjectFlags = Object->Flags;
        if ((ObjectFlags & kWin3DActive) == 0 || (ObjectFlags & kWin3DVisible) == 0 ||
            (ObjectFlags & kWin3DPickable) == 0)
            continue;
        if (!Object->IsDescendantOf(Capture) && Object != Capture)
            continue;

        int Depth = Object->GetHitPriority();
        if (Depth == 0)
        {
            for (Win3D* Walk = Object; Walk != nullptr; Walk = Walk->Parent)
                Depth = Depth + 1;
        }
        if (Depth > BestDepth)
        {
            bool bVisibleChain = true;
            for (Win3D* Walk = Object; Walk != nullptr; Walk = Walk->Parent)
            {
                if ((Walk->Flags & kWin3DVisible) == 0)
                {
                    bVisibleChain = false;
                    break;
                }
            }
            if (!bVisibleChain)
                continue;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            if (HitTestWidescreen(Object, Ray))
#else
            if (Object->HitTest(Ray))
#endif
            {
                Best = Object;
                BestDepth = Depth;
            }
            continue;
        }
        if (Depth != BestDepth)
            continue;
        if (!(Object->Position[2] > Best->Position[2]))
            continue;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        if (HitTestWidescreen(Object, Ray))
#else
        if (Object->HitTest(Ray))
#endif
        {
            Best = Object;
            BestDepth = Depth;
        }
    }
    return Best;
}

bool Win3DBroadcast(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_0059df40.
    bool bHandled = false;
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr)
            continue;
        bHandled = bHandled || Object->Slot23(Msg, A, B);
        if (bHandled)
            break;
    }
    return bHandled;
}

void Win3DSetHover(Win3D* Object)
{
    // FUN_0059df80. Leave/enter only on a change; slot 14 runs on every call with a
    // non-null Object.
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    if (G_Hover != Object)
    {
        if (G_Hover != nullptr)
            MouseCallWidescreen(G_Hover, EMouseCall::Leave);
        G_Hover = Object;
        if (Object != nullptr)
            MouseCallWidescreen(Object, EMouseCall::Enter);
    }
    if (Object != nullptr)
        MouseCallWidescreen(Object, EMouseCall::Over);
#else
    if (G_Hover != Object)
    {
        if (G_Hover != nullptr)
            G_Hover->MouseLeave(G_MouseRay);
        G_Hover = Object;
        if (Object != nullptr)
            Object->MouseEnter(G_MouseRay);
    }
    if (Object != nullptr)
        Object->MouseOver(G_MouseRay);
#endif
}

void Win3DUpdateAll(double DeltaSeconds)
{
    // FUN_0059e080 (cdecl, double dt).
    NnUiPulseAdvance(DeltaSeconds);                           // 0059e080..0059e0c9
    NnToolTipTickGlobal(DeltaSeconds);                        // FUN_004be2b0
    NnRolloverTickGlobal(DeltaSeconds);                       // FUN_004b6290

    // 0059e0eb: FUN_0041dd90 -- the software cursor is drawn.
    Win3D* Picked = nullptr;
    if (GetClientMouseCursorDrawFlag() != 0)
    {
        if (G_CaptureHead == nullptr)
        {
            Picked = Win3DPick(G_MouseRay);
        }
        else
        {
            Picked = Win3DPickWithin(G_CaptureHead, G_MouseRay);
            if (Picked == nullptr)
                Picked = G_CaptureHead;
        }
    }

    // 0059e123..0059e15a: FUN_0059df80's body inlined, but slot 14 runs only on a change.
    if (G_Hover != Picked)
    {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        if (G_Hover != nullptr)
            MouseCallWidescreen(G_Hover, EMouseCall::Leave);
        G_Hover = Picked;
        if (Picked != nullptr)
        {
            MouseCallWidescreen(Picked, EMouseCall::Enter);
            MouseCallWidescreen(Picked, EMouseCall::Over);
        }
#else
        if (G_Hover != nullptr)
            G_Hover->MouseLeave(G_MouseRay);
        G_Hover = Picked;
        if (Picked != nullptr)
        {
            Picked->MouseEnter(G_MouseRay);
            Picked->MouseOver(G_MouseRay);
        }
#endif
    }

    G_UpdateNodeCount = 0;
    if (G_ModalCaptureCount != 0)
    {
        // 0059e16b..0059e194: the first registered WinControl holding the mouse gets the only
        // update of the frame, then the pending list is drained.
        for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
        {
            Win3D* const Object = static_cast<Win3D*>(Node->Payload);
            if (Object == nullptr)
                continue;
            WinControl* const Control = Object->AsWinControl();
            if (Control == nullptr || Control->bCaptured == 0)
                continue;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            UpdateWidescreen(Object);
#else
            if ((Object->Flags & kWin3DActive) != 0)
                Object->Update();
            else
                Object->UpdateInactive();
#endif
            DrainPendingRemove();
            return;
        }
    }

    // 0059e196..0059e26c: every registered root (+4 == 0), counting every node walked.
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object != nullptr && Object->Parent == nullptr)
        {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            UpdateWidescreen(Object);
#else
            if ((Object->Flags & kWin3DActive) != 0)
                Object->Update();
            else
                Object->UpdateInactive();
#endif
        }
        G_UpdateNodeCount = G_UpdateNodeCount + 1;
    }
    DrainPendingRemove();
}

void Win3DBeginDraw()
{
    // FUN_0059e500.
    Win3DDisable(kWin3DLightingCap);   // 0xb50
    Win3DDisable(kWin3DTexture2D);     // 0xde1
    Win3DDisable(kWin3DCullFace);      // 0xb44
    Win3DDisable(kWin3DDepthTest);     // 0xb71
    Win3DDisable(kWin3DFog);           // 0xb60
    ICamera* const Camera = AsCamera(Win3DGetCamera());
    Win3DSetViewport(Camera, kWin3DSourceFile, 0x487);   // FUN_00422610
    Win3DApplyCamera(Camera);                            // FUN_00422950(&DAT_0067dbf8)
    DALib::MaterialBatcher->SetBinOrder(1, -1);                // +0x20
    EngineLightClearRegisteredList();                    // FUN_004101e0
    IRenderPipeline* const Pipe = DALib::Pipeline;
    Pipe->SetRenderState(D3DRS_AMBIENT, -1);                      // +0xf0
    Pipe->SetRenderState(D3DRS_DITHERENABLE, 0);
}

void Win3DEndDraw()
{
    // FUN_0059e5a0.
    DALib::MaterialBatcher->SetBinOrder(1, 1);
}

void Win3DDrawAll()
{
    // FUN_0059e300.
    if (G_DrawSuppressCount > 0)
        return;

    {
#ifdef FREELANCER_WALK_MODE
        FBaseWalkInterfaceDraw InterfaceDraw;
#endif
        Win3DBeginDraw();          // 0059e319..0059e3a9, the same sequence as FUN_0059e500
        Win3DImmedBatchReset();    // FUN_005a29b0
        ButtonMontageEnsure();     // FUN_00559c10

        Win3D* Sorted[0x400];
        unsigned Count = 0;
        for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
        {
            Win3D* const Object = static_cast<Win3D*>(Node->Payload);
            if (Object == nullptr || Object->Parent != nullptr)
                continue;
            if ((Object->Flags & kWin3DVisible) == 0)
                continue;
            if (Object->DrawLayer == 0)
            {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
                DrawWidescreen(Object);
#else
                Object->Draw();
#endif
                continue;
            }
            InsertSorted(Sorted, &Count, Object);
        }

        for (int SubLayer = 0; SubLayer < 8; ++SubLayer)
        {
            for (unsigned Index = 0; Index < Count; ++Index)
            {
                if (Sorted[Index]->DrawSubLayer == SubLayer)
#ifdef FREELANCER_WIDESCREEN_SUPPORT
                    DrawWidescreen(Sorted[Index]);
#else
                    Sorted[Index]->Draw();
#endif
            }
        }

        ButtonMontageFlushBatch(); // FUN_00559c20
        Win3DImmedBatchFlushAll(); // FUN_005a2a20
        UiOverlayPrepPipeline();   // FUN_00417f10
        ImeFocusDrawPass();        // FUN_00563af0
    }
    MouseFrameUpdate();        // FUN_0041f150
    DALib::MaterialBatcher->SetBinOrder(1, 1);
}

void Win3DDrawChildren(Win3D* Parent)
{
    // FUN_0059e5c0.
    DrawChildrenSorted(Parent);
}

void Win3DDrawChildrenOf(Win3D* Parent)
{
    // FUN_0059e6e0.
    DrawChildrenSorted(Parent);
}

void Win3DDeleteTree(Win3D* Root)
{
    // FUN_0043d2a0 (cdecl). 0043d2a5 TEST EDI,EDI / JZ: a null root does nothing. Post-order:
    // the next sibling is read (0043d2b1) before the child's subtree goes, then slot 10
    // Destroy (0043d2c8) and the scalar deleting dtor with 1 (0043d2d1).
    if (Root == nullptr)
        return;
    Win3D* Child = Root->ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Root->Destroy();
    delete Root;
}

bool Win3DAnyCaptured()
{
    // FUN_0059e6a0.
    for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
    {
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object == nullptr)
            continue;
        WinControl* const Control = Object->AsWinControl();
        if (Control != nullptr && Control->bCaptured != 0)
            return true;
    }
    return false;
}

// DAT_0067dbc8 -- the material-cache entry of interface\interface.generic.vms (FUN_0059d5a0
// fills it through FUN_0041ade0, FUN_0059d6d0 gives it back through FUN_0041b030).
Archetype::FCliMaterialEntry* G_InterfaceGenericMaterial = nullptr;

void Win3DStartupManager(int Width, int Height)
{
    // FUN_0059d5a0 (.text:0059d5a0-0059d6c1), cdecl(width, height); sole caller InitializeEngine
    // 0x005b1d53 with the options object's size (DAT_00679bc8 / DAT_00679bcc).
    FSpaceCamera* const WinCamera = Win3DGetCamera(); // &DAT_0067dbf8

    // 0x0059d5b8..0x0059d5e0: FUN_0040fd60 with the stack rect {0, 0, width - 1, height - 1}.
    int ViewRect[4];
    ViewRect[0] = 0;
    ViewRect[1] = 0;
    ViewRect[2] = Width - 1;
    ViewRect[3] = Height - 1;
    SpaceCameraSetViewport(WinCamera, ViewRect);
    // 0x0059d5e5..0x0059d5f7: FUN_0040fb60(0.1f, 1000.0f) -- DAT_005c891c / DAT_005c8920.
    SpaceCameraSetNearFar(WinCamera, 0.1f, 1000.0f);
    // 0x0059d5fc..0x0059d606: FUN_0040f8c0 with the float bits 0x41d9ba5e.
    {
        unsigned int const kHalfFovBits = 0x41d9ba5eu;
        float HalfFovX;
        memcpy(&HalfFovX, &kHalfFovBits, sizeof(HalfFovX));
        SpaceCameraSetHalfFovX(WinCamera, HalfFovX);
    }
    // 0x0059d60b: FUN_0041e5c0 -- mouse.ini shapes and cursors.
    MouseInitCursors();
    // 0x0059d610: FUN_005a2970 -- both line batchers.
    Win3DImmedBatchCreateAll();

    // 0x0059d615..0x0059d67e: a "FileSystem" for interface\interface.generic.vms under
    // DALib::Data (slot 3), the 0x34-byte description zeroed by REP STOSD and filled like every
    // other client call site.
    FFileSystemAggDesc Desc;
    memset(&Desc, 0, sizeof(Desc));
    Desc.Size = 0x34;
    Desc.ComponentName = "FileSystem";
    Desc.DataPath = "interface\\interface.generic.vms";
    Desc.Field10 = 0x80000000u;
    Desc.Field14 = 1;
    Desc.Field1C = 3;
    Desc.Field20 = 0x8000080u;
    Desc.Field30 = 0xffffffffu;
    void* FsOut = nullptr;
    if (static_cast<IFileSystem*>(DALib::Data)->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc),
                                                               &FsOut) >= 0)
    {
        // 0x0059d685..0x0059d69a: FUN_0041ade0(fs, path, 1, -1) -> DAT_0067dbc8.
        G_InterfaceGenericMaterial = Archetype::ResolveMaterialEntryFromFs(
            static_cast<IFileSystem*>(FsOut), Desc.DataPath, true, 0xffffffffu);
    }
    // 0x0059d69f..0x0059d6b0: the file system is released (slot 2) whenever the out pointer is
    // non-null, whatever the create returned.
    if (FsOut != nullptr)
        static_cast<IFileSystem*>(FsOut)->Release();
}

void Win3DShutdownManager()
{
    // FUN_0059d6d0, 0059d6d0..0059d8ae.
    G_KeyFocus = nullptr;
    G_CaptureHead = nullptr;

    // 0059d6ec..0059d730: erase whatever is already pending.
    for (FWin3DListNode* Pending = G_PendingRemove.Head; Pending != nullptr;
         Pending = Pending->Next)
    {
        FWin3DListNode* const Node = static_cast<FWin3DListNode*>(Pending->Payload);
        if (Node != nullptr)
            RegistryEraseNode(Node);
    }
    FWin3DListNode* Pending = G_PendingRemove.Head;
    while (Pending != nullptr)
    {
        FWin3DListNode* const Next = Pending->Next;
        operator delete(Pending);
        Pending = Next;
    }
    G_PendingRemove.Count = 0;
    G_PendingRemove.Tail = nullptr;
    G_PendingRemove.Head = nullptr;

    // 0059d752..0059d7f8: FUN_0059dc00 inlined on every registered object (the next node is
    // read before the object is processed).
    FWin3DListNode* Node = G_Registry.Head;
    while (Node != nullptr)
    {
        FWin3DListNode* const Next = Node->Next;
        Win3D* const Object = static_cast<Win3D*>(Node->Payload);
        if (Object != nullptr && (Object->Flags & kWin3DUnregistered) == 0)
        {
            Object->Flags = static_cast<unsigned char>(Object->Flags | kWin3DUnregistered);
            if (G_KeyFocus == Object)
                G_KeyFocus = nullptr;
            if (G_CaptureHead == Object)
                G_CaptureHead = nullptr;
            if (G_Hover == Object)
                G_Hover = nullptr;
            FWin3DListNode* Found = G_Registry.Head;
            while (Found != nullptr && Found->Payload != Object)
                Found = Found->Next;
            if (Found != nullptr && Found->Payload != nullptr)
            {
                Win3D* const Up = Object->Parent;
                if (Up != nullptr)
                {
                    LinkTreeRemoveChild(&Up->ChildHead, Object);
                    Object->Parent = nullptr;
                }
                ListPushBack(&G_PendingRemove, Found);
                Found->Payload = nullptr;
            }
        }
        Node = Next;
    }

    // 0059d7fe..0059d84f: erase the pending registry nodes, then FUN_0059e860 frees the
    // pending list itself.
    for (FWin3DListNode* Drain = G_PendingRemove.Head; Drain != nullptr; Drain = Drain->Next)
    {
        FWin3DListNode* const Erase = static_cast<FWin3DListNode*>(Drain->Payload);
        if (Erase != nullptr)
            RegistryEraseNode(Erase);
    }
    FWin3DListNode* Free = G_PendingRemove.Head;
    while (Free != nullptr)
    {
        FWin3DListNode* const Next = Free->Next;
        operator delete(Free);
        Free = Next;
    }
    G_PendingRemove.Count = 0;
    G_PendingRemove.Tail = nullptr;
    G_PendingRemove.Head = nullptr;

    // 0059d854..0059d883: free whatever registry nodes remain and clear the header.
    FWin3DListNode* Rest = G_Registry.Head;
    while (Rest != nullptr)
    {
        FWin3DListNode* const Next = Rest->Next;
        operator delete(Rest);
        Rest = Next;
    }
    G_Registry.Count = 0;
    G_Registry.Tail = nullptr;
    G_Registry.Head = nullptr;

    // 0x0059d872..0x0059d889: FUN_0059e8d0 on the model cache DAT_0067dcf4.
    Win3DModelCacheClear();

    // 0x0059d88e..0x0059d8a0: FUN_0041b030(&DAT_0067dbc8) gives back the interface material
    // entry, FUN_0041e610 releases the mouse device and the cursors, FUN_005a2990 destroys both
    // line batchers. The tail jump to the camera's LAB_00401e90 (a bare RET) does nothing.
    Archetype::ReleaseMaterialEntrySlot(&G_InterfaceGenericMaterial);
    MouseShutdown();
    Win3DImmedBatchDestroyAll();
}

// ---------------------------------------------------------------------------------------
// Input router DAT_0067dbf0
// ---------------------------------------------------------------------------------------

namespace
{
    // DAT_006679d8 -- FUN_00423b90's lazily acquired 3DMathEngine (FUN_005b73e0 on first use).
    I3DMathEngine* G_TransformMathEngine = nullptr;

    // FUN_00423b90 (cdecl Out, Matrix, Source): I3DMathEngine slot 0x2c into a local, copied out.
    void TransformByMatrix(FVector& Out, FMatrix const& Matrix, FVector const& Source)
    {
        if (G_TransformMathEngine == nullptr)
            G_TransformMathEngine = Acquire3DMathEngine();
        FVector Result;
        G_TransformMathEngine->TransformMatrix(Result, Matrix, Source);
        Out = Result;
    }

    // FUN_004bdd40 (thiscall on the camera; Out, ScreenX, ScreenY): the camera-space point on
    // the near plane under a screen pixel. Each product is rounded to float on its FSTP.
    void ScreenToNearPlane(FSpaceCamera const* Cam, FVector& Out, float ScreenX, float ScreenY)
    {
        double const ZNear = static_cast<double>(Cam->ZNear);
        Out.X = static_cast<float>((static_cast<double>(ScreenX) - Cam->ScreenCenterX) *
                                   (ZNear / Cam->XScale));
        Out.Y = static_cast<float>((static_cast<double>(ScreenY) - Cam->ScreenCenterY) *
                                   (ZNear / Cam->YScale));
        Out.Z = static_cast<float>(-ZNear);
    }

    // FUN_0041a390 (thiscall on the vector): scale by 1 / |v|. There is no zero test: a zero
    // vector divides by zero exactly as retail does.
    void NormalizeInPlace(FVector& V)
    {
        double const X = V.X;
        double const Y = V.Y;
        double const Z = V.Z;
        double const Inv = 1.0 / sqrt(X * X + Y * Y + Z * Z);
        V.X = static_cast<float>(Inv * V.X);
        V.Y = static_cast<float>(Inv * V.Y);
        V.Z = static_cast<float>(Inv * V.Z);
    }

    // The handler object at DAT_0067dbf0 (vftable 0x005e5dcc, one slot). Its only virtual is
    // slot 0, so its vptr is exactly the FHudCmdHandler shape FUN_00576410 calls through.
    class FWin3DInputHandler : public EventHandler
    {
    public:
        FWin3DInputHandler();
        ~FWin3DInputHandler();

        // FUN_0059d140 (thiscall, RET 4).
        bool OnEvent(unsigned MsgId) override;

    };

    FWin3DInputHandler::FWin3DInputHandler()
    {
        // FUN_0059d080, run from the static initializer LAB_0059d060: the twelve ids are laid
        // out on the stack in this order and registered one by one through FUN_005760d0.
        static unsigned const kIds[12] = {5, 4, 6, 7, 8, 9, 10, 11, 12, 13, 1, 2};
        for (unsigned Index = 0; Index < 12; ++Index)
            RegisterEvent(kIds[Index]);
    }

    FWin3DInputHandler::~FWin3DInputHandler()
    {
        // FUN_0059d410, reached from the atexit thunk LAB_0059d400: FUN_00576190 erases every
        // registration of this handler; the Event::Handler base dtor FUN_00576010 then repeats
        // the same erase (after FUN_00577c30 on the handler), which finds nothing left.
        UnregisterAllEvents();
        // FUN_00576010 follows through the EventHandler base destructor.
    }

    bool FWin3DInputHandler::OnEvent(unsigned MsgId)
    {
        // 0059d148: LEA EAX,[ECX-1] / CMP EAX,0xc / JA -> the false arm; the byte table at
        // 0x0059d3ec sends 1, 2, 4, 5 to their own arms, 3 to the false arm and 6..13 to the
        // mouse-button arm.
        if (MsgId - 1 > 12 || MsgId == 3)
            return false;

        if (MsgId == 1)
        {
            // 0059d34d: the key goes to FUN_0059dfd0 with the event record.
            return Win3DDispatchKey(HudCommandRecord());
        }

        if (MsgId == 2)
        {
            // 0059d364: slot 22 (record +0x10, record +0x14) to the focus object and its answer
            // returned; without a focus, to every registered object until one returns true.
            unsigned const B = HudCommandRecord()->Arg14;
            unsigned const A = HudCommandRecord()->Arg10;
            if (G_KeyFocus != nullptr)
                return G_KeyFocus->Slot22(A, B);
            bool bHandled = false;
            for (FWin3DListNode* Node = G_Registry.Head; Node != nullptr; Node = Node->Next)
            {
                Win3D* const Object = static_cast<Win3D*>(Node->Payload);
                if (Object == nullptr)
                    continue;
                bHandled = Object->Slot22(A, B) || bHandled;
                if (bHandled)
                    break;
            }
            return bHandled;
        }

        if (MsgId == 4)
        {
            // 0059d2fe: the wheel. The capture head alone gets slot 18 with DAT_0061684c;
            // otherwise the picked object does.
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            if (G_CaptureHead != nullptr)
                return WheelWidescreen(G_CaptureHead, GetClientMouseWheel());
            Win3D* const Picked = Win3DPick(G_MouseRay);
            if (Picked == nullptr)
                return false;
            return WheelWidescreen(Picked, GetClientMouseWheel());
#else
            if (G_CaptureHead != nullptr)
                return G_CaptureHead->Slot18(GetClientMouseWheel());
            Win3D* const Picked = Win3DPick(G_MouseRay);
            if (Picked == nullptr)
                return false;
            return Picked->Slot18(GetClientMouseWheel());
#endif
        }

        if (MsgId == 5)
        {
            // 0059d22c: mouse move. The cursor pixel (DAT_00616840 / DAT_00616844) goes to the
            // near plane of DAT_0067dbf8 (FUN_004bdd40), into world space through a copy of
            // the camera's orientation DAT_0067dbfc (FUN_00423b90), and is normalised
            // (FUN_0041a390) into the stored ray DAT_0067dbcc.
            int CursorX = 0;
            int CursorY = 0;
            GetClientSoftwareCursor(&CursorX, &CursorY, nullptr);
            FSpaceCamera* const Cam = Win3DGetCamera();
            FVector Local;
            ScreenToNearPlane(Cam, Local, static_cast<float>(CursorX),
                              static_cast<float>(CursorY));
            FMatrix Orientation;
            memcpy(&Orientation, Cam->World, sizeof(Orientation));
            FVector World;
            TransformByMatrix(World, Orientation, Local);
            NormalizeInPlace(World);
            G_MouseRay[0] = World.X;
            G_MouseRay[1] = World.Y;
            G_MouseRay[2] = World.Z;

            // 0059d2aa: with a capture, the pick inside it, falling back to the capture head
            // itself; without one, the global pick. The result becomes the hover object and
            // the event counts as consumed when there is one.
            Win3D* Picked = nullptr;
            if (G_CaptureHead == nullptr)
            {
                Picked = Win3DPick(G_MouseRay);
            }
            else
            {
                Picked = Win3DPickWithin(G_CaptureHead, G_MouseRay);
                if (Picked == nullptr)
                    Picked = G_CaptureHead;
            }
            Win3DSetHover(Picked);
            return Picked != nullptr;
        }

        // 0059d164: mouse buttons 6..13. Field0 is -1 - (id - 6); the listing's "else 0" arm
        // (0059d177) is unreachable from these ids.
        FWin3DMouseEvent Event;
        unsigned const ButtonIndex = MsgId - 6;
        if (ButtonIndex < 8)
            Event.Field0 = -1 - static_cast<int>(ButtonIndex);
        else
            Event.Field0 = 0;
        unsigned char const Byte1A = HudCommandRecord()->Byte1A;
        if (HudCommandRecord()->Byte18 != 0)
            Event.Field1 = 2 + (Byte1A != 0 ? 1 : 0);
        else
            Event.Field1 = 1;

        // 0059d1ab..0059d1fb: walk the capture chain. For each capture object the pick inside it
        // gets the event first; the capture object itself gets it when the pick missed or
        // declined, unless the pick WAS the capture object. The walk stops once handled.
        bool bHandled = false;
        Win3D* Capture = G_CaptureHead;
        if (Capture != nullptr)
        {
            do
            {
                if (bHandled)
                    return bHandled;
                Win3D* const Next = Capture->CaptureNext;
                Win3D* const Hit = Win3DPickWithin(Capture, G_MouseRay);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
                if (Hit != nullptr)
                    bHandled = MouseEventWidescreen(Hit, &Event);
                if (!bHandled && Hit != Capture)
                    bHandled = MouseEventWidescreen(Capture, &Event);
#else
                if (Hit != nullptr)
                    bHandled = Hit->MouseEvent(&Event);
                if (!bHandled && Hit != Capture)
                    bHandled = Capture->MouseEvent(&Event);
#endif
                Capture = Next;
            } while (Capture != nullptr);
            if (bHandled)
                return bHandled;
        }

        // 0059d201: nothing in the capture chain took it; the global pick gets it.
        Win3D* const Picked = Win3DPick(G_MouseRay);
        if (Picked == nullptr)
            return bHandled;
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        return MouseEventWidescreen(Picked, &Event);
#else
        return Picked->MouseEvent(&Event);
#endif
    }

    // DAT_0067dbf0. Constructed by the static initializer LAB_0059d060, destroyed through the
    // atexit thunk LAB_0059d400.
    FWin3DInputHandler G_Win3DInputHandler;
}

void Win3DTransformByMatrix(FVector& Out, FMatrix const& Matrix, FVector const& Source)
{
    // FUN_00423b90 -- the same body the input router uses; one engine cache (DAT_006679d8).
    TransformByMatrix(Out, Matrix, Source);
}

I3DMathEngine* Win3DTransformMathEngine()
{
    // DAT_006679d8: `MOV EAX,[DAT_006679d8]; TEST EAX,EAX; JNZ; CALL FUN_005b73e0; MOV [..],EAX`.
    if (G_TransformMathEngine == nullptr)
        G_TransformMathEngine = Acquire3DMathEngine();
    return G_TransformMathEngine;
}

void Win3DScreenToNearPlane(FSpaceCamera const* Cam, FVector& Out, float ScreenX, float ScreenY)
{
    // FUN_004bdd40.
    ScreenToNearPlane(Cam, Out, ScreenX, ScreenY);
}
