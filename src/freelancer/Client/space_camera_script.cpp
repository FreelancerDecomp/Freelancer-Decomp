// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// ScriptCamera (retail `E:\FL\Scratch\Source\Client\space\scriptcam.cpp`):
// ctor 0x0051e100, dtor 0x0051dfc0, setup 0x0051e3b0, and the five overridden vtable slots --
// 0x54 name (0x0051e070), 0x60 finished (0x0051e810), 0x64 activate (0x0051e4c0), 0x68
// deactivate (0x0051e660), 0x6c update (0x0051e740). The two transform sinks and their shared
// DACOM plumbing (0x0051e830 QI, 0x0051e8e0 AddRef, 0x0041a800 Release) are here too.

#include "space_camera_script.h"

#include "space_camera.h"
#include "space_letterbox.h"

#include "DALib.h"
#include "common_exports.h"
#include "common_csys.h"   // Acquire3DMathEngine (FUN_005b73e0, engine_init.cpp)
#include "common_iface.h"  // IObjInspect -- the watched object's thiscall getters
#include "common_misc.h"   // GetFullFilename
#include "dacom.h"
#include "thorn_iface.h"
#include "x86math.h"

#include <new>
#include <stdlib.h>
#include <string.h>

// ThornGetUserString comes from common_goods.h, which common_iface.h (needed for IObjInspect)
// already pulls in.

namespace
{
    // Same convention as the other cameras: a watcher holds the Watchable subobject, which sits
    // eight bytes into the IObjRW, which is where retail's ubiquitous `- 8` comes from.
    const int kWatchableOffset = 8;

    // The name literal at .rdata 0x005dc1e4, returned by slot 0x54.
    const char kScriptCameraClassName[] = "ScriptCamera";


    // 0x0051e4e3 / 0x0051e50f: the DACOM factory name and the interface the camera queries for.
    const char kScriptEngineFactory[] = "IScriptEngine";

    // 0x0051e5bc: the user property that marks the entity the camera should follow.
    const char kMainObjectProperty[] = "main_object";

    // 0x0051e565 / 0x0051e560: the source file and line retail names in its load warning.
    const char kScriptCamSource[] = "E:\\FL\\Scratch\\Source\\Client\\space\\scriptcam.cpp";
    const int kScriptCamLoadWarningLine = 0x9a;

    // The script engine's slot 21 reports its state; 4 is the one the camera treats as "done"
    // (0x0051e822: `CMP EAX,4` / `SETZ AL`).
    const int kScriptStateFinished = 4;

    // 0x0051e59f: the entity type the camera binds its own transform sink to.
    const int kScriptEntityTypeCamera = 4;

    // 0x0051e88f: the code a failed QueryInterface returns. dacom.h carries GR_OK and
    // GR_GENERIC but not this one; x86math.h spells it the same way.
    const GENRESULT kGrInterfaceUnsupported = static_cast<GENRESULT>(0xFFFFFFFD);

    // 0x0051e760: the frame time is multiplied by this double before ftol, so the script engine
    // is ticked in whole MILLISECONDS. The constant at .rdata 0x005d3f50 is 0x408f400000000000.
    const double kSecondsToMilliseconds = 1000.0;

    // 0x0051e601 / 0x0051e721: the bars slide over a third of a second either way.
    const float kLetterboxSpeed = 3.0f;

    // ------------------------------------------------------------------ IScriptEngine
    // These eight slots were reached by raw number until 2026-09-16 because the comment here
    // said the declaration "lives in Thorn.h, which is the DLL's own header and not on the
    // client's include path". That stopped being true in pass 6: IScriptEngine is published in
    // src/include/thorn_iface.h. The comment outlived the fact and kept the whole file on slot
    // numbers -- all eight arities happened to be right, but nothing was checking.

    IScriptEngine* ScriptOf(void* Script)
    {
        return static_cast<IScriptEngine*>(Script);
    }

    // ------------------------------------------------------------------ Csys arithmetic
    // DAT_006752c8 -- scriptcam.cpp's own lazily acquired 3DMathEngine. Each of the three call
    // sites (0x0051e28b, 0x0051e329, 0x0051e7b3) tests the global and, when it is null, stores
    // FUN_005b73e0's result before calling through it. No null test on the result.
    I3DMathEngine* G_ScriptCameraMathEngine = nullptr;

    I3DMathEngine* ScriptCameraMathEngine()
    {
        if (G_ScriptCameraMathEngine == nullptr)
            G_ScriptCameraMathEngine = Acquire3DMathEngine();
        return G_ScriptCameraMathEngine;
    }

    // A {Orientation[9], Position[3]} pair laid out back to back is exactly x86math's FTransform
    // (0x30 bytes); retail hands the engine the address of the orientation.
    FTransform const& AsTransform(const float* Orientation9)
    {
        return *reinterpret_cast<FTransform const*>(Orientation9);
    }

    void Transpose3x3(float* Dst, const float* Src)
    {
        Dst[0] = Src[0]; Dst[1] = Src[3]; Dst[2] = Src[6];
        Dst[3] = Src[1]; Dst[4] = Src[4]; Dst[5] = Src[7];
        Dst[6] = Src[2]; Dst[7] = Src[5]; Dst[8] = Src[8];
    }

    void SetIdentity(float* Orientation9, float* Position3)
    {
        Orientation9[0] = 1.0f; Orientation9[1] = 0.0f; Orientation9[2] = 0.0f;
        Orientation9[3] = 0.0f; Orientation9[4] = 1.0f; Orientation9[5] = 0.0f;
        Orientation9[6] = 0.0f; Orientation9[7] = 0.0f; Orientation9[8] = 1.0f;
        Position3[0] = 0.0f; Position3[1] = 0.0f; Position3[2] = 0.0f;
    }

    // 0x0051e258 / 0x0051e2e8: the sink reaches its driven object the same way every camera
    // does -- the stored Watchable pointer minus eight, with the null tested BOTH before and
    // after the subtraction, which is retail's inlined `Watchable::get()`.
    void* WatchedObjectOf(const BaseWatcher& Watcher)
    {
        if (Watcher.target() == nullptr)
            return nullptr;
        unsigned char* const Object =
            const_cast<unsigned char*>(
                reinterpret_cast<const unsigned char*>(Watcher.target())) -
            kWatchableOffset;
        return Object;
    }

    // ------------------------------------------------------------------ shared sink slots

    // ------------------------------------------------------------------ teardown

    // 0x0051e3b0 head and 0x0051e660 head share this: hand both entity handles back to the
    // script engine, release it, and forget the name and the driven object.
    void TearDownScript(FScriptCamera* Cam)
    {
        if (Cam->Script == nullptr)
            return;

        IScriptEngine* const Script = ScriptOf(Cam->Script);

        // 0x0051e3c6 / 0x0051e3dd: each handle is returned only if it was actually bound, and is
        // reset to -1 as it goes, so a second call cannot double-free.
        if (Cam->CameraEntity != -1)
        {
            Script->Slot13Detach(Cam->CameraEntity);
            Cam->CameraEntity = -1;
        }
        if (Cam->ObjectEntity != -1)
        {
            // 0x0051e694: retail re-reads Script from the object here rather than reusing the
            // register, which matters only if detaching could clear it. It cannot, but the port
            // reads it the same way for the same reason it exists: nothing is assumed.
            ScriptOf(Cam->Script)->Slot13Detach(Cam->ObjectEntity);
            Cam->ObjectEntity = -1;
        }

        // 0x0051e6b4: slot 2, the DACOM Release.
        reinterpret_cast<IDAComponent*>(Cam->Script)->Release();

        Cam->Script = nullptr;
        Cam->NameLength = 0;
        Cam->Name[0] = '\0';

        // 0x0051e41d / 0x0051e6d2: the driven object is let go too -- note retail tests the
        // watcher's stored pointer, not the object, so an already-empty watcher is left
        // completely alone. The sink pointer at +0x100 itself is NOT tested: retail dereferences
        // it unconditionally (MOV ECX,[+0x100] / MOV EAX,[ECX+8]).
        if (Cam->ObjectTransform->Watched.target() != nullptr)
            Cam->ObjectTransform->Watched.set(nullptr);
    }
}

// ---------------------------------------------------------------- the two transform sinks
//
// Until 2026-09-16 these seven slots each were free functions taking a `void* This`, installed
// into two hand-written `void*[7]` arrays. They are ordinary virtual overrides now; the tables
// are the compiler's, and the layout static_asserts in the header are what proves the move did
// not shift a field.

GENRESULT DACOM_API FScriptCamSink::QueryInterface(const char* Iid, void** Out)
{
    // LAB_0051e830. Retail walks a {name, offset} table at .rdata 0x005dc2a8 that has exactly
    // one live row -- the geo IID at offset zero -- and a null terminator. So the whole of it
    // is: this interface, or nothing. Neither Iid nor Out is null-checked (0x0051e845 reads
    // the IID bytes, 0x0051e889 / 0x0051e8ba write through Out).
    if (strcmp(Iid, Iids::GeoTransformControl) == 0)
    {
        // 0x0051e8b2: the reference is taken before the pointer is handed out, exactly as a
        // DACOM QueryInterface must.
        AddRef();
        *Out = static_cast<IGeoTransformControl*>(this);
        return GR_OK;
    }

    // 0x0051e889: the out pointer is cleared on failure, and the code is -3.
    *Out = nullptr;
    return kGrInterfaceUnsupported;
}

unsigned long DACOM_API FScriptCamSink::AddRef()
{
    // LAB_0051e8e0: pre-increment, and the NEW value is what comes back.
    ++RefCount;
    return static_cast<unsigned long>(RefCount);
}

unsigned long DACOM_API FScriptCamSink::Release()
{
    // LAB_0041a800, shared with FRoomBatcherSink::Release (room_control.cpp).
    // 0x0041a807: TEST EAX,EAX / JBE -- an UNSIGNED compare, so only an exact zero skips the
    // decrement (a count with the top bit set is still decremented).
    if (RefCount != 0)
        --RefCount;

    if (RefCount != 0)
        return static_cast<unsigned long>(RefCount);

    // 0x0041a817: the count is put back to 1 before the object is freed, so a re-entrant
    // release during destruction cannot free it twice. Then zero comes back, not the count.
    // The allocation came from malloc and neither sink has anything to destroy, so free() is
    // still what releases it -- see ScriptCameraConstruct.
    RefCount = 1;
    free(this);
    return 0;
}

// ------------------------------------------------------------------ camera sink slots

GENRESULT DACOM_API FScriptCamTransform::SetPosition(float* Position3)
{
    // LAB_0051e080: three dwords to this + 8.
    Position[0] = Position3[0];
    Position[1] = Position3[1];
    Position[2] = Position3[2];
    return GR_OK;
}

GENRESULT DACOM_API FScriptCamTransform::SetOrientation(float* Orientation9)
{
    // LAB_0051e0a0: nine dwords to this + 0x14.
    memcpy(Orientation, Orientation9, 9 * sizeof(float));
    return GR_OK;
}

GENRESULT DACOM_API FScriptCamTransform::GetPosition(float* OutPosition3)
{
    // LAB_0051e0c0.
    OutPosition3[0] = Position[0];
    OutPosition3[1] = Position[1];
    OutPosition3[2] = Position[2];
    return GR_OK;
}

GENRESULT DACOM_API FScriptCamTransform::GetOrientation(float* OutOrientation9)
{
    // LAB_0051e0e0.
    memcpy(OutOrientation9, Orientation, 9 * sizeof(float));
    return GR_OK;
}

// ------------------------------------------------------------------ object sink slots

FScriptCamObjectTransform::FScriptCamObjectTransform()
{
    // 0x0051e1b4 onwards: unlike the camera's sink, this one IS fully initialised.
    // BaseWatcher default-constructs to null Watched/Next (retail zeroes both dwords).
    SetIdentity(OrientationA, PositionA);
    SetIdentity(OrientationB, PositionB);
}

GENRESULT DACOM_API FScriptCamObjectTransform::SetPosition(float* /*Position3*/)
{
    // LAB_00451370: `XOR EAX,EAX / RET 8`. Both setters point at it, so the object sink
    // silently accepts and discards everything written to it -- see the header.
    return GR_OK;
}

GENRESULT DACOM_API FScriptCamObjectTransform::SetOrientation(float* /*Orientation9*/)
{
    // LAB_00451370 again, the same stub.
    return GR_OK;
}

GENRESULT DACOM_API FScriptCamObjectTransform::GetPosition(float* OutPosition3)
{
    // LAB_0051e250.
    void* const Object = WatchedObjectOf(Watched);
    if (Object != nullptr)
    {
        // 0x0051e276: `MOV ECX,obj / CALL [EAX]` -- IObjInspect::get_position (slot 0,
        // __thiscall, no stack argument), cached in the sink's SECOND position field.
        const Vector& World = static_cast<IObjInspect*>(Object)->get_position();
        PositionB[0] = World.x;
        PositionB[1] = World.y;
        PositionB[2] = World.z;
    }

    // 0x0051e2ae: and the answer is that position expressed in the FIRST transform's frame --
    // 3DMathEngine slot 0x3c (InverseTransform) with Dst = a stack vector, Xform = +0x10,
    // Source = +0x64, then the three floats are copied out. Note the cached value is used even
    // when there is no object -- a sink that has lost its object keeps reporting the last place
    // it saw it.
    FVector Local;
    ScriptCameraMathEngine()->InverseTransform(Local, AsTransform(OrientationA),
                                               *reinterpret_cast<FVector const*>(PositionB));
    OutPosition3[0] = Local.X;
    OutPosition3[1] = Local.Y;
    OutPosition3[2] = Local.Z;
    return GR_OK;
}

GENRESULT DACOM_API FScriptCamObjectTransform::GetOrientation(float* OutOrientation9)
{
    // LAB_0051e2e0, the orientation counterpart.
    void* const Object = WatchedObjectOf(Watched);
    if (Object != nullptr)
    {
        // 0x0051e308: `CALL [EAX+0xc]` -- IObjInspect slot 3 (get_transform, __thiscall; its
        // body is folded with get_orientation), the first nine floats cached in OrientationB.
        const Transform& World = static_cast<IObjInspect*>(Object)->get_transform();
        memcpy(OrientationB, &World, 9 * sizeof(float));
    }

    // 0x0051e325 .. 0x0051e370: the first orientation is transposed in place on the stack --
    // nine explicit FLD/FSTP pairs, not a loop -- and then multiplied into the cached one.
    // 0x0051e391: 3DMathEngine slot 0x20 (MulMatrix) into a stack matrix, then nine dwords out.
    float Transposed[9];
    Transpose3x3(Transposed, OrientationA);
    FMatrix Product;
    ScriptCameraMathEngine()->MulMatrix(Product, *reinterpret_cast<FMatrix const*>(Transposed),
                                        *reinterpret_cast<FMatrix const*>(OrientationB));
    memcpy(OutOrientation9, &Product, 9 * sizeof(float));
    return GR_OK;
}

// ---------------------------------------------------------------- the ScriptCamera vtable
//
// .rdata 0x005dc164, 32 slots. Diffed dword by dword against FGameCamera's 0x005dbf5c: exactly
// five differ, and they are the five this file implements --
//
//   0x54  0x0051e070  (GameCamera 0x0051b3b0)   name
//   0x60  0x0051e810  (GameCamera 0x0044fd00)   finished
//   0x64  0x0051e4c0  (GameCamera 0x0051b450)   activate
//   0x68  0x0051e660  (GameCamera 0x0051b480)   deactivate
//   0x6c  0x0051e740  (GameCamera 0x0051b650)   update
//
// Every other slot is inherited unchanged, so the table is FGameCamera's with those five
// overwritten -- the same way the RearView camera builds its own from HUDCamera's. The leaves
// follow the port's slot convention: `this` explicit, first on the stack.

void ScriptCameraConstruct(FScriptCamera* Cam)
{
    // 0x0051e11f: the FGameCamera ctor runs first, and it is the GameCamera one -- ScriptCamera
    // skips HUDCamera entirely, which is why its vtable matches 0x005dbf5c and not 0x005dc08c.
    // Retail has no null test on `this` (the one caller, 0x005493af, passes a static).
    GameCameraConstruct((FGameCamera*)Cam);

    // 0x0051e126 / 0x0051e12c.
    Cam->NameLength = 0;
    Cam->Name[0] = '\0';

    // 0x0051e132 .. 0x0051e179: the starting transform is an identity with a zero position.
    SetIdentity(Cam->Orientation, Cam->Position);

    // 0x0051e185: the ScriptCamera vptr replaces the GameCamera one the base ctor installed.

    // 0x0051e18b.
    Cam->Script = nullptr;

    // 0x0051e18f: operator new(0x38). Retail checks the result and stores the null on failure
    // rather than throwing, so the camera can legitimately exist without its sinks.
    // The storage still comes from malloc, because Release() still frees it with free(); the
    // placement new is only there to install the vtable pointer and the reference count, which
    // is exactly the pair of stores retail makes. Retail leaves the Csys uninitialised -- the
    // script writes it before the camera reads it -- and the port does not "helpfully" zero it,
    // because that would hide a missing write instead of exposing it.
    Cam->CameraTransform = static_cast<FScriptCamTransform*>(malloc(sizeof(FScriptCamTransform)));
    if (Cam->CameraTransform != nullptr)
        new (Cam->CameraTransform) FScriptCamTransform();

    // 0x0051e1b4: operator new(0x70), and this one IS fully initialised.
    Cam->ObjectTransform =
        static_cast<FScriptCamObjectTransform*>(malloc(sizeof(FScriptCamObjectTransform)));
    if (Cam->ObjectTransform != nullptr)
        new (Cam->ObjectTransform) FScriptCamObjectTransform();

    // 0x0051e21e / 0x0051e225: both entity handles start unbound.
    Cam->CameraEntity = -1;
    Cam->ObjectEntity = -1;
}

void ScriptCameraDestruct(FScriptCamera* Cam)
{
    Cam->~FScriptCamera();
}

FScriptCamera::~FScriptCamera()
{
    // No null test on `this` -- retail has none (callers 0x00548fd4 and the atexit thunks
    // 0x005c10f3 / 0x005c1217 all pass a static camera).

    // 0x0051dfdf: the destructor opens by restoring the ScriptCamera vptr, as every MSVC dtor
    // does, so any slot reached while tearing down is this class's own.

    // 0x0051dfe5 .. 0x0051e003: the camera's own sink. Its (inlined) dtor only rewrites its vptr
    // to 0x005dc140, which it already holds, then operator delete (FUN_005b7e1d). The port's
    // sinks are malloc'd (see ScriptCameraConstruct and FScriptCamSink::Release), so free().
    // Retail does not clear the member afterwards; neither does the port.
    if (this->CameraTransform != nullptr)
        free(this->CameraTransform);

    // 0x0051e006 .. 0x0051e02a: the object sink. Its inlined dtor is its BaseWatcher's: when the
    // watcher holds something ([EDI+8] != 0) it is unhooked through BaseWatcher::set_pointer(0),
    // THEN the object is deleted.
    if (this->ObjectTransform != nullptr)
    {
        if (this->ObjectTransform->Watched.target() != nullptr)
            this->ObjectTransform->Watched.set(nullptr);
        free(this->ObjectTransform);
    }

    // 0x0051e02d .. 0x0051e047: the inlined FGameCamera dtor -- only its BaseWatcher at +0xc0,
    // unhooked the same way. Retail does not rewrite the vptr to GameCamera's 0x005dbf5c here.

    // 0x0051e053 / 0x0051e059: the FSpaceCamera dtor -- vptr 0x005c8944, then a call to
    // 0x00401e90, which is a bare RET (Ghidra names the ICF-folded body
    // IDPMsgHandler::OnDisconnect). The vptr store is to an object whose lifetime is ending and
    // nothing reads it, so the port has nothing to do for it.

    // Retail's destructor never releases Script -- it relies on slot 0x68 having run. The port
    // keeps that asymmetry deliberately; see docs/re/deviations.md.
}

void ScriptCameraSetup(FScriptCamera* Cam, const char* ScriptName, void* Object,
                       const float* Transform12)
{
    // No null test on `this` and none on the +0x100 sink: retail has neither.

    // 0x0051e3ba: an already-loaded script is torn down before anything else, which is what makes
    // this method double as the reset.
    TearDownScript(Cam);

    // 0x0051e430 .. 0x0051e44d: point the object sink at the new object, skipping the write when
    // it already holds exactly that -- the same short-circuit GameCameraAttach has.
    Watchable const* const Target =
        (Object != nullptr)
            ? reinterpret_cast<Watchable const*>(static_cast<unsigned char*>(Object) +
                                                 kWatchableOffset)
            : nullptr;

    if (Target != Cam->ObjectTransform->Watched.target())
        Cam->ObjectTransform->Watched.set(Target);

    // 0x0051e45c: a null name clears the field rather than leaving the old one behind.
    if (ScriptName == nullptr)
    {
        Cam->NameLength = 0;
        Cam->Name[0] = '\0';
    }
    else
    {
        // 0x0051e46d: retail measures the string, clamps the COUNT to 0x103, copies that many
        // bytes and then writes its own terminator -- so an over-long name is truncated, never
        // rejected, and the stored length is the truncated one.
        size_t Length = strlen(ScriptName);
        if (Length > static_cast<size_t>(kScriptCameraNameMax))
            Length = static_cast<size_t>(kScriptCameraNameMax);

        memcpy(Cam->Name, ScriptName, Length);
        Cam->Name[Length] = '\0';
        Cam->NameLength = static_cast<unsigned>(Length);
    }

    // 0x0051e4a4: twelve floats in one run -- nine of orientation then three of position. A null
    // pointer leaves the stored transform untouched.
    if (Transform12 != nullptr)
    {
        memcpy(Cam->Orientation, Transform12, 9 * sizeof(float));
        memcpy(Cam->Position, Transform12 + 9, 3 * sizeof(float));
    }
}

const char* ScriptCameraName()
{
    return kScriptCameraClassName;
}

bool ScriptCameraIsFinished(const FScriptCamera* Cam)
{
    // 0x0051e816: no script at all counts as finished, and AL is already 1 at that point.
    if (Cam->Script == nullptr)
        return true;

    // 0x0051e81f: slot 21, then `== 4`. This is what lets the camera controller take the view
    // back when a cutscene ends.
    const GENRESULT State = ScriptOf(Cam->Script)->Slot21GetState();
    return State == kScriptStateFinished;
}

void* ScriptCameraWatchedObject(const FScriptCamera* Cam)
{
    if (Cam == nullptr || Cam->ObjectTransform == nullptr)
        return nullptr;
    return WatchedObjectOf(Cam->ObjectTransform->Watched);
}

bool ScriptCameraActivate(FScriptCamera* Cam, const int* ViewRect4)
{
    // 0x0051e4d9 .. 0x0051e515: build an IScriptEngine through DACOM and query the interface off
    // it. Retail's descriptor says 0x14 bytes even though dacom.h's plain AGGDESC is 0xC, which
    // is the same five-dword shape ThornScriptLoad uses -- declared locally at the retail size so
    // the callee cannot read past the end.
    struct FScriptAggDesc
    {
        unsigned long Size;
        const char* Name;
        void* Field08;
        void* Field0C;
        void* Field10;
    };
    static_assert(sizeof(FScriptAggDesc) == 0x14, "retail descriptor is 0x14 bytes");

    // 0x0051e4d9: DALib::Dacom is read and used with no null test.
    ICOManager* const Manager = DALib::Dacom;

    // 0x0051e4c9 .. 0x0051e4eb: the out slot and the three trailing descriptor dwords are zeroed.
    FScriptAggDesc Desc;
    Desc.Size = 0x14;
    Desc.Name = kScriptEngineFactory;
    Desc.Field08 = nullptr;
    Desc.Field0C = nullptr;
    Desc.Field10 = nullptr;

    // 0x0051e4ff: ICOManager slot 3 (CreateInstance); the result is not tested.
    void* Instance = nullptr;
    Manager->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &Instance);

    // 0x0051e515: slot 0 (QueryInterface) on the instance straight into +0xf8 -- again no test
    // of either the instance or the result.
    IDAComponent* const Component = reinterpret_cast<IDAComponent*>(Instance);
    Component->QueryInterface(Iids::ScriptEngine, &Cam->Script);

    // 0x0051e51e: the instance reference is dropped straight away -- the queried interface is
    // the only one the camera keeps.
    Component->Release();

    // 0x0051e521 .. 0x0051e53c: the stored starting transform is copied into the OBJECT sink's
    // first Csys before the script is even loaded, so the script sees the frame it should place
    // `main_object` relative to. Twelve dwords straight into sink + 0x10; the sink pointer is not
    // tested.
    memcpy(Cam->ObjectTransform->OrientationA, Cam->Orientation, 9 * sizeof(float));
    memcpy(Cam->ObjectTransform->PositionA, Cam->Position, 3 * sizeof(float));

    // 0x0051e53e: the stored name is a DATA-relative path, so it goes through GetFullFilename
    // before the engine sees it.
    const char* const FullPath = GetFullFilename(Cam->Name);

    // 0x0051e54f: slot 43 loads by path. NON-zero is the failure here, which is the opposite of
    // the usual GENRESULT reading and is why retail tests with JZ rather than JNZ.
    const GENRESULT LoadResult = ScriptOf(Cam->Script)->Slot43LoadPath(FullPath);
    if (LoadResult != GR_OK)
    {
        // 0x0051e575: CALL [FDUMP] with no test of the pointer.
        FDUMP(0x100002, "%s(%d) : *** WARNING: ScriptCamera load failed: %s\n",
              kScriptCamSource, kScriptCamLoadWarningLine, FullPath);
        // 0x0051e57d: XOR AL,AL -- and note the engine is NOT released and Script is NOT
        // cleared. A failed activate leaves the camera holding a loaded-nothing engine, which
        // only the deactivate slot will clean up. The port keeps that; see deviations.
        return false;
    }

    // 0x0051e58b .. 0x0051e5e9: walk every entity once and bind the two sinks.
    const unsigned long EntityCount = ScriptOf(Cam->Script)->Slot6EntityCount();

    for (int Index = 0; Index < static_cast<int>(EntityCount); ++Index)
    {
        // 0x0051e59c: slot 8 hands back a pointer whose FIRST dword is the entity type.
        const int* const Entity =
            static_cast<const int*>(ScriptOf(Cam->Script)->Slot8(Index));

        // 0x0051e59f: CMP dword [EAX],4 -- the pointer is dereferenced without a test.
        if (*Entity == kScriptEntityTypeCamera)
        {
            // 0x0051e5b1: type 4 is a camera entity, and it drives the camera's own sink.
            ScriptOf(Cam->Script)->Slot12Attach(Index, Cam->CameraTransform);
            Cam->CameraEntity = Index;
        }

        // 0x0051e5c3: and INDEPENDENTLY of the type, an entity carrying the `main_object` user
        // property drives the object sink. Retail does not `else` these -- one entity could in
        // principle be both, and the last matching index wins in each case.
        const char* const MainObject = ThornGetUserString(
            reinterpret_cast<IScriptEngine*>(Cam->Script), Index, kMainObjectProperty);
        if (MainObject != nullptr)
        {
            ScriptOf(Cam->Script)->Slot12Attach(Index, Cam->ObjectTransform);
            Cam->ObjectEntity = Index;
        }
    }

    // 0x0051e5f2: slot 22 starts the script at time zero.
    ScriptOf(Cam->Script)->Slot22Start(0);

    // 0x0051e5fc: CALL FUN_0051b450 -- the FGameCamera activate (its slot 0x64), which sets the
    // viewport AND, on success, runs the slot-0x5c viewport reset (FUN_0051b490) that tells the
    // letterbox to forget its camera. The port used to call only SpaceCameraSetViewport here and
    // so dropped that second half. Its result (BL) is what this slot returns.
    const bool bBaseResult = GameCameraActivate((FGameCamera*)Cam, ViewRect4);

    // 0x0051e601: and the bars slide in over a third of a second.
    SetLetterboxTarget(1.0f, kLetterboxSpeed);

    // 0x0051e615: and the ship and the server are told the view has gone cinematic.
    NotifyCinematicView(true);

    return bBaseResult;
}

void ScriptCameraDeactivate(FScriptCamera* Cam)
{
    // 0x0051e663: everything below the teardown runs whether or not there was a script, but the
    // teardown itself is skipped when there is none.
    TearDownScript(Cam);

    // 0x0051e6ec: retail calls the base class's deactivate, which is IDPMsgHandler::OnDisconnect
    // -- an empty function the linker folded the slot onto. Nothing to do, and saying so is the
    // point: this is not an omission.

    // 0x0051e6f1: the mirror of the activate notify.
    NotifyCinematicView(false);

    // 0x0051e721: the bars slide back out at the same speed.
    SetLetterboxTarget(0.0f, kLetterboxSpeed);
}

void ScriptCameraUpdate(FScriptCamera* Cam, float DeltaSeconds)
{
    // 0x0051e74a: no script means no update at all -- the camera keeps whatever world transform
    // it last had rather than falling back to the starting one.
    if (Cam->Script == nullptr)
        return;

    // 0x0051e758: the frame time reaches the engine as whole milliseconds, truncated by ftol.
    const int DeltaMs = static_cast<int>(static_cast<double>(DeltaSeconds) * kSecondsToMilliseconds);
    ScriptOf(Cam->Script)->Slot26Tick(DeltaMs);

    // 0x0051e772: the +0xfc sink is read with no null test.

    // 0x0051e772 / 0x0051e783: the script has just written the camera sink, so read it back --
    // and read it through the SLOTS, not off the fields, which is what makes the sink a real
    // interface rather than a struct the camera pokes at.
    float LocalPosition[3];
    float LocalOrientation[9];
    Cam->CameraTransform->GetPosition(LocalPosition);        // 0x14
    Cam->CameraTransform->GetOrientation(LocalOrientation);  // 0x18

    // 0x0051e7e2: the script's transform is composed ONTO the stored starting one, so a script
    // written around the origin lands wherever the setup put the camera.
    // 3DMathEngine slot 0x1c (MulTransform): Dst = stack, A = Cam+0x214, B = the stacked local
    // {orientation, position}.
    FTransform Local;
    memcpy(&Local.Rotation, LocalOrientation, 9 * sizeof(float));
    memcpy(&Local.Translation, LocalPosition, 3 * sizeof(float));
    FTransform World;
    ScriptCameraMathEngine()->MulTransform(World, AsTransform(Cam->Orientation), Local);

    // 0x0051e7e5: twelve dwords into the FSpaceCamera's own Csys at +0x04, and then the view
    // cache is invalidated at +0x34.
    FSpaceCamera* const Base = (FSpaceCamera*)Cam;
    memcpy(Base->World, &World.Rotation, 9 * sizeof(float));
    memcpy(Base->WorldPos, &World.Translation, 3 * sizeof(float));
    Base->bViewDirty = 1;
}

char const* FScriptCamera::GetName()
{
    // 0x0051e070: MOV EAX, "ScriptCamera" / RET -- `this` is never read.
    return ScriptCameraName();
}

bool FScriptCamera::IsFinished()
{
    return ScriptCameraIsFinished(this);
}

bool FScriptCamera::Activate(int const* ViewRect4)
{
    return ScriptCameraActivate(this, ViewRect4);
}

void FScriptCamera::Deactivate()
{
    ScriptCameraDeactivate(this);
}

void FScriptCamera::Update(float DeltaSeconds)
{
    ScriptCameraUpdate(this, DeltaSeconds);
}
