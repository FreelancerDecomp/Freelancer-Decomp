// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// ScriptCamera -- the in-flight camera a Thorn script drives (cutscenes, tradelane rides,
// jump sequences). Retail source is `E:\FL\Scratch\Source\Client\space\scriptcam.cpp`, which the
// binary still carries because the load-failure warning prints it.
//
// It is the last class in the camera hierarchy the port had not touched, and the vtable settles
// where it sits: 0x005dc164 is identical to FGameCamera's 0x005dbf5c in every slot except five,
// so ScriptCamera derives from FGameCamera directly -- NOT from FHudCamera, which the other four
// concrete cameras use. The five overrides are:
//
//   0x54  0x0051e070   the class name, "ScriptCamera"
//   0x60  0x0051e810   "is this camera finished?"
//   0x64  0x0051e4c0   activate: build the script and bind its entities
//   0x68  0x0051e660   deactivate: tear the script down again
//   0x6c  0x0051e740   per-frame update
//
// The camera owns two heap objects, and both are DACOM components: their shared slot 0 returns
// the IID "1.11_IGeoTransformControl" (read at 0x0051e830), which is how a Thorn script moves
// something. One drives the camera itself, the other drives the object the script calls
// `main_object`. So a script animates two transforms and the camera reads them back.

#include "space_camera_game.h"

#include "thorn_controls.h"

#include <stddef.h>

// The camera's own transform sink -- retail's 0x38-byte heap object with vtable 0x005dc140.
// Its ctor sets only the vtable and the reference count; the Csys is left as whatever the
// allocator returned, because the script writes it before anything reads it.
//
// The field ORDER is the sink's own, and it is not the FTransform order: the four accessor
// slots settle it outright. 0x0051e08d writes three dwords at `this + 8` and 0x0051e0aa writes
// nine at `this + 0x14`; 0x0051e0c8 and 0x0051e0ea read them back from exactly those two
// addresses. So the position comes first and the orientation second -- the reverse of the
// nine-then-three layout the camera itself and the math engine use.
// The DACOM plumbing the two sinks share: one QueryInterface (LAB_0051e830), one AddRef
// (LAB_0051e8e0) and one Release (LAB_0041a800), sitting in the first three slots of both
// retail vtables. Declaring it as a base is what lets the compiler emit those tables.
struct FScriptCamSink : IGeoTransformControl
{
    //                              // +0x00  the vtable pointer the port used to install by hand
    int            RefCount;        // +0x04  1

    FScriptCamSink() : RefCount(1) {}

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out);
    unsigned long DACOM_API AddRef();
    unsigned long DACOM_API Release();
};

struct FScriptCamTransform : FScriptCamSink
{
    float          Position[3];     // +0x08  slot 0x0c writes it, slot 0x14 reads it
    float          Orientation[9];  // +0x14  slot 0x10 writes it, slot 0x18 reads it

    GENRESULT DACOM_API SetPosition(float* Position3);
    GENRESULT DACOM_API SetOrientation(float* Orientation9);
    GENRESULT DACOM_API GetPosition(float* OutPosition3);
    GENRESULT DACOM_API GetOrientation(float* OutOrientation9);
};

// The transform sink for `main_object` -- retail's 0x70-byte heap object with vtable 0x005dc1f8.
// Unlike the camera's, this one is fully initialised by the ctor (two identity Csys) and carries
// a watcher on the object it drives, which is why the destructor has to unhook it.
struct FScriptCamObjectTransform : FScriptCamSink
{
    //                              // +0x00  vtable 0x005dc1f8; same slot 0 as the camera's sink
    BaseWatcher    Watched;         // +0x08  BaseWatcher on the driven object
    float          OrientationA[9]; // +0x10  identity
    float          PositionA[3];    // +0x34  zero
    float          OrientationB[9]; // +0x40  identity
    float          PositionB[3];    // +0x64  zero

    FScriptCamObjectTransform();

    // LAB_00451370, a bare `XOR EAX,EAX / RET 8` shared by twenty-one vtable slots across the
    // exe: this sink silently discards everything written to it.
    GENRESULT DACOM_API SetPosition(float* Position3);
    GENRESULT DACOM_API SetOrientation(float* Orientation9);
    GENRESULT DACOM_API GetPosition(float* OutPosition3);
    GENRESULT DACOM_API GetOrientation(float* OutOrientation9);
};

// The longest script name the camera will hold. Retail clamps the copy at 0x103 characters and
// then writes the terminator, so the buffer is 0x104 and the name is always terminated.
enum { kScriptCameraNameMax = 0x103, kScriptCameraNameBytes = 0x104 };

struct FScriptCamera : FGameCamera
{
    ~FScriptCamera();
    char const* GetName() override;
    bool IsFinished() override;
    bool Activate(int const* ViewRect4) override;
    void Deactivate() override;
    void Update(float DeltaSeconds) override;


    // +0x0f8  the live IScriptEngine, or null when no script is loaded. Released through its own
    // slot 8; the two entity handles below are handed back through its slot 0x34 first.
    void*                      Script;

    FScriptCamTransform*       CameraTransform; // +0x0fc  operator new(0x38)
    FScriptCamObjectTransform* ObjectTransform; // +0x100  operator new(0x70)

    int                        CameraEntity;    // +0x104  script entity handle, -1 when unbound
    int                        ObjectEntity;    // +0x108  script entity handle, -1 when unbound

    unsigned                   NameLength;      // +0x10c
    char                       Name[kScriptCameraNameBytes];   // +0x110 .. +0x214

    // +0x214  the transform the script starts from: a 3x3 orientation followed by a position,
    // copied in as one run of twelve floats.
    float                      Orientation[9];
    float                      Position[3];     // +0x238 .. +0x244
};

// Every offset below is a store in the ctor (0x0051e100), the dtor (0x0051dfc0) or the setup
// (0x0051e3b0). The three sizes are the three operator new / class sizes retail uses.
static_assert(sizeof(FScriptCamTransform) == 0x38, "the camera transform sink is 0x38");
static_assert(offsetof(FScriptCamTransform, RefCount) == 0x04, "SCT +0x04");
static_assert(offsetof(FScriptCamTransform, Position) == 0x08, "SCT +0x08");
static_assert(offsetof(FScriptCamTransform, Orientation) == 0x14, "SCT +0x14");

static_assert(sizeof(FScriptCamObjectTransform) == 0x70, "the object transform sink is 0x70");
static_assert(offsetof(FScriptCamObjectTransform, RefCount) == 0x04, "SCOT +0x04");
static_assert(offsetof(FScriptCamObjectTransform, Watched) == 0x08, "SCOT +0x08");
static_assert(offsetof(FScriptCamObjectTransform, OrientationA) == 0x10, "SCOT +0x10");
static_assert(offsetof(FScriptCamObjectTransform, PositionA) == 0x34, "SCOT +0x34");
static_assert(offsetof(FScriptCamObjectTransform, OrientationB) == 0x40, "SCOT +0x40");
static_assert(offsetof(FScriptCamObjectTransform, PositionB) == 0x64, "SCOT +0x64");

static_assert(offsetof(FScriptCamera, Script) == 0x0f8, "SC +0x0f8");
static_assert(offsetof(FScriptCamera, CameraTransform) == 0x0fc, "SC +0x0fc");
static_assert(offsetof(FScriptCamera, ObjectTransform) == 0x100, "SC +0x100");
static_assert(offsetof(FScriptCamera, CameraEntity) == 0x104, "SC +0x104");
static_assert(offsetof(FScriptCamera, ObjectEntity) == 0x108, "SC +0x108");
static_assert(offsetof(FScriptCamera, NameLength) == 0x10c, "SC +0x10c");
static_assert(offsetof(FScriptCamera, Name) == 0x110, "SC +0x110");

// The name buffer runs right up against the transform, which is the independent check that its
// 0x104 bytes are the whole of it: 0x110 + 0x104 == 0x214.
static_assert(offsetof(FScriptCamera, Orientation) == 0x214, "SC +0x214");
static_assert(offsetof(FScriptCamera, Position) == 0x238, "SC +0x238");
static_assert(sizeof(FScriptCamera) == 0x244, "ScriptCamera is 0x244");

// 0x0051e100. Runs the FGameCamera ctor, then allocates the two transform sinks. Either
// allocation may fail and retail simply stores the null -- so may this.
void ScriptCameraConstruct(FScriptCamera* Cam);

// 0x0051dfc0. Releases the two sinks and unhooks both watchers. Retail's destructor does NOT
// release the script engine -- slot 0x68 is expected to have done that -- and the port keeps that
// asymmetry rather than "fixing" it; see docs/re/deviations.md.
void ScriptCameraDestruct(FScriptCamera* Cam);

// 0x0051e3b0. Points the camera at an object, under a script name, from a starting transform.
// Every argument is optional:
//
//   * a script already loaded is torn down first (both entity handles returned, engine released,
//     name cleared, object watcher unhooked) -- so this doubles as the reset;
//   * a null Object unbinds the watcher;
//   * a null ScriptName clears the name;
//   * a null Transform12 leaves the stored transform alone.
//
// Transform12 is twelve floats: nine of orientation then three of position.
void ScriptCameraSetup(FScriptCamera* Cam, const char* ScriptName, void* Object,
                       const float* Transform12);

// Slot 0x64 (0x0051e4c0). Activate: create an IScriptEngine through DACOM, load the script whose
// name the setup stored, walk its entities and bind the two transform sinks to the ones that
// matter, start the script, hand the viewport to the FGameCamera layer and slide the cinematic
// bars in.
//
// ViewRect4 is the viewport, four inclusive edges, exactly as FGameCamera's own activate slot
// takes it. Returns false when the script failed to load -- and ONLY then; the base layer's
// return value is always true.
bool ScriptCameraActivate(FScriptCamera* Cam, const int* ViewRect4);

// Slot 0x68 (0x0051e660). Deactivate: hand both entities back, release the engine, clear the
// name, unhook the object watcher and slide the bars back out.
void ScriptCameraDeactivate(FScriptCamera* Cam);

// Slot 0x6c (0x0051e740). Per-frame: tick the script by the frame time in MILLISECONDS, read the
// camera transform the script has just written, compose it onto the stored starting transform and
// put the result in the FSpaceCamera's world Csys.
void ScriptCameraUpdate(FScriptCamera* Cam, float DeltaSeconds);

// Slot 0x54 (0x0051e070): the literal "ScriptCamera".
const char* ScriptCameraName();

// Slot 0x60 (0x0051e810): true when there is no script at all, otherwise true only once the
// script engine's own state (its slot 0x54) has reached 4. This is what lets the camera
// controller take the view back when a cutscene ends.
bool ScriptCameraIsFinished(const FScriptCamera* Cam);

// The object the `main_object` sink currently watches, or null. Retail spells this
// `watcher->Watched - 8` inline, the same idiom the other cameras use.
void* ScriptCameraWatchedObject(const FScriptCamera* Cam);

// Both sinks are the seven slots of DACOM IGeoTransformControl, and until 2026-09-16 the port
// installed them as two hand-written `void*[7]` arrays of free functions:
//
//   0x00  QueryInterface -- shared, LAB_0051e830; answers only "1.11_IGeoTransformControl"
//   0x04  AddRef         -- shared, LAB_0051e8e0
//   0x08  Release        -- shared, LAB_0041a800; frees the object when the count hits zero
//   0x0c  SetPosition
//   0x10  SetOrientation
//   0x14  GetPosition
//   0x18  GetOrientation
//
// The camera's sink implements all four accessors against its own fields. The OBJECT sink does
// not: its two setters are both LAB_00451370, a bare `XOR EAX,EAX / RET 8` shared by twenty-one
// vtable slots across the exe. So the object sink is READ-ONLY -- a script can ask where
// `main_object` is but cannot move it, which is the whole reason the camera hands the script two
// different sinks rather than two of the same.

inline FSpaceCamera* SpaceCameraOf(FScriptCamera* Cam) { return Cam; }
