// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Gf\Script -- the two process-wide tables the Thorn script components share.
//
//   * The Thorn camera pool at .data 0x00668b00 (ScriptCamera.cpp unit, FUN_00450370 ..
//     FUN_00450680). Every non-reference Thorn camera and monitor takes a slot
//     (FUN_00450830); the slot with the highest priority among those allowed to be active
//     is the one whose position, orientation, fov and clip planes are pushed into its room
//     camera (FUN_00556cb0 .. FUN_00556f10).
//   * The script light registry at .data 0x0066d130 / 0x0066d230 (ScriptLight.cpp unit): 64
//     engine-light wrappers and their enable bytes, filled by the Thorn_Light ctor
//     (FUN_00452fa0), emptied by its release slot (LAB_00453100) and enumerated by the room's
//     per-frame light list (FUN_0044b450 through FUN_00452f70 / FUN_00452f80).

class FRoomCamera;
struct FGfLightWrapper;

// One 0x40-byte pool slot.
struct FGfCameraSlot
{
    FRoomCamera* Camera;     // +0x00  the room camera (RoomObject+0x500) this slot drives
    float Pos[3];            // +0x04
    float Orient[9];         // +0x10
    float HalfFovXDeg;       // +0x34  35.0
    float ZNear;             // +0x38  0.25
    float ZFar;              // +0x3c  1e6
};

// The whole pool object, 0x4604 bytes at 0x00668b00.
struct FGfCameraPool
{
    FGfCameraSlot Slots[0x100];        // +0x0000  DAT_00668b00
    int Priority[0x100];               // +0x4000  DAT_0066cb00, -1 when free
    unsigned char bCanBeActive[0x100]; // +0x4400  DAT_0066cf00
    unsigned char bOccupied[0x100];    // +0x4500  DAT_0066d000
    int ActiveIndex;                   // +0x4600  DAT_0066d100, -1 when none

    // LAB_00450370 (static init) and LAB_00450440 (its atexit twin): every slot back to the
    // defaults, every priority -1, both byte arrays 0, no active slot.
    FGfCameraPool();
    ~FGfCameraPool();
    void Reset();

    // FUN_00450580 (thiscall, RET 0xc). Takes the first free slot; returns -1 when all 256
    // are taken. The new slot becomes active when it may and outranks the active one. Nothing
    // is pushed into the camera here.
    int Allocate(int SlotPriority, FRoomCamera* Camera, unsigned char bSlotCanBeActive);

    // FUN_00450680 (thiscall, RET 4). Frees the slot; when it was the active one, the highest
    // priority occupied slot that may be active takes over and its whole state is pushed into
    // its camera.
    void Free(int Slot);
};

extern FGfCameraPool G_GfCameraPool;

// FUN_00452f70 -- the registry's capacity, 0x40.
int GfLightRegistryCount();

// FUN_00452f80 -- the wrapper in slot Index when its enable byte is exactly 1, else null.
// The wrapper is FUN_00556fd0's 8-byte { FEngineLight*, byte group }.
FGfLightWrapper* GfLightRegistryGet(int Index);

// FUN_00452fa0 tail (0x004530b4..0x004530d9): the first null slot takes the wrapper and the
// enable byte bOn != 0; returns that index, or -1 when all 64 are taken.
int GfLightRegistryAdd(FGfLightWrapper* Wrapper, bool bOn);

// The enable byte of slot Index, written by Thorn_Light's D3D-light slot 3 (LAB_00453310) and
// read by its slot 6 (LAB_004533f0). No range check, as retail.
void GfLightRegistrySetEnabled(int Index, bool bOn);
bool GfLightRegistryIsEnabled(int Index);

// LAB_00453100 head: clear the slot and its enable byte. The caller then resets its index
// to -1 and destroys the wrapper (FUN_00557130).
void GfLightRegistryRemove(int Index);

struct FEngineLight;
struct FD3dLight8;

// FUN_00556fd0's 8-byte result: the engine light and the light group byte. Both the script
// lights (FUN_00452fa0) and the room's own lights (FUN_004496b0, RoomControl+0xe4) are these.
struct FGfLightWrapper
{
    FEngineLight* Light;     // +0x00
    unsigned char Group;     // +0x04
    unsigned char Pad05[3];
};

// FUN_00556fd0(Frame, Source, Direction, Group): operator new(8) zeroed, then the engine light
// (EngineLightCreate: Frame12 = position + 3x3 orientation, Direction3 = the local direction
// the orientation turns), then the group byte. Returns the wrapper.
FGfLightWrapper* GfLightWrapperCreate(float const* Frame12, FD3dLight8 const* Source,
                                      float const* Direction3, unsigned char Group);

// FUN_00557130(FGfLightWrapper** Field): nothing when *Field is null; otherwise the light is
// released through its slot 0 when present, both wrapper fields cleared, the wrapper freed and
// *Field nulled.
void GfLightWrapperDestroy(FGfLightWrapper** Field);

// FUN_00556f20(r, g, b) (cdecl, three floats by value): the scene ambient. With the LIGHTING
// game option off it is full white; otherwise each channel is floor(c * 255 + 0.5) truncated to
// a byte. Either way the three bytes go to FUN_00410240 (D3DRS_AMBIENT). Callers are the
// RoomControl draw passes at 0x0044af11 and 0x0044b7c3 (room_control.cpp).
void GfSetSceneAmbient(float R, float G, float B);

// FUN_005586f0 (cdecl) -- how strongly a light reaches Pos3, for the nearest-light pick.
// Directional: 3.0, taken. Range 0: 0, not taken. Point: 2 - dist^2/range^2. Spot: the same,
// unless Pos3 lies outside the half cone (cos(Phi/2)), when the cosine of the angle replaces
// the 2. Radius is passed by retail and never read.
bool GfLightScore(FD3dLight8 const* D3d, float const* LightPos3, float const* Dir3,
                  float const* Pos3, float Radius, float* OutScore);

// FUN_00557170 (cdecl) -- every wrapper of List that holds a light of this Group and scores,
// inserted into OutLights / OutScores from the highest score down; *InOutCount comes in as the
// number of entries to clear and leaves as the count, cut to MaxCount at the end.
void GfLightsSelectNearest(FGfLightWrapper* const* List, int Count, float const* Pos3,
                           float Radius, unsigned char Group, int MaxCount,
                           FEngineLight** OutLights, float* OutScores, int* InOutCount);

// FUN_005572b0 (cdecl) -- the up to four best lights of Group for the engine instance's
// position, applied through FUN_004102c0 with the camera and no position.
struct ICamera;
void GfLightsApplyNearest(FGfLightWrapper* const* List, int Count, void* Instance,
                          unsigned char Group, ICamera* Camera);

// DAT_0066d2a8 -- the room fog state (ScriptFog unit). The Thorn fog facet (vtable
// 0x005cdec0, LAB_00454450 .. LAB_00454600) writes it, FUN_00454250 applies it once per room
// draw (FUN_00454760), FUN_00454700 tells the per-object draw whether fog is on, and the
// RoomControl ctor clears it (FUN_004546d0 at 0x00449324).
struct FGfFogState
{
    unsigned char bOn;       // +0x00 DAT_0066d2a8
    unsigned Color;          // +0x04 DAT_0066d2ac, 0x00RRGGBB
    float Start;             // +0x08 DAT_0066d2b0
    float End;               // +0x0c DAT_0066d2b4
    float Density;           // +0x10 DAT_0066d2b8
    unsigned TableMode;      // +0x14 DAT_0066d2bc
    unsigned VertexMode;     // +0x18 DAT_0066d2c0
};

extern FGfFogState G_GfFog;

void GfFogReset();                                           // FUN_004546d0 / LAB_00454220
void GfFogSetOn(unsigned char bOn);                          // LAB_00454450
bool GfFogIsActive();                                        // FUN_00454700 / LAB_00454480
void GfFogSetColor(int const* Rgb3);                         // LAB_00454500
void GfFogGetColor(int* Rgb3);                               // LAB_00454530
void GfFogSetRange(float Start, float End, float Density);
void GfFogSetRangeValue(const float* Start, const float* End, const float* Density);   // LAB_00454570
void GfFogGetRange(float* Start, float* End, float* Density); // LAB_004545a0
void GfFogSetMode(unsigned char bTable, unsigned Mode);      // LAB_004545d0
void GfFogGetMode(unsigned char* bTable, unsigned* Mode);    // LAB_00454600
void GfFogApply();                                           // FUN_00454760 -> FUN_00454250

// ---------------------------------------------------------------------------------------
// Thorn script scans the Gf script sequence runs over a loaded script (callers: the
// ScriptSequence setup FUN_00455290 at 0x00455322 / 0x00455332 / 0x0045533a, and 0x0045576c
// / 0x00455764 -- neither is ported yet).
// ---------------------------------------------------------------------------------------
struct IScriptEngine;

// FUN_0044fa90 (cdecl): every START_MOTION event's animation (thorn_iface.h
// FThornMotionEventDesc +0x3c) is queued in the CharacterInfo database (FUN_00435320) when
// bAcquire is set, released (FUN_00434eb0) otherwise. A null script does nothing.
void GfScriptCharacterAnimations(IScriptEngine* Script, bool bAcquire);

// FUN_0044faf0 (cdecl): out-bytes cleared, then among the SOUND entities a template
// "BackgroundAmbient" sets *OutAmbient and "BackgroundMusic" sets *OutMusic (stricmp; either
// out may be null).
void GfScriptFindBackgroundAudio(IScriptEngine* Script, unsigned char* OutAmbient,
                                 unsigned char* OutMusic);

// FUN_0044fb80 (cdecl): IScriptEngine slot 9 on every entity, the results dropped. Both callers
// pass a second byte -- 1 from the setup (0x0045531f..0x0045533a), 0 from the teardown
// (0x00455761..0x00455764), the same flag they give FUN_0044fa90 -- and the body never reads it
// (only [ESP+param_1+0xc] is loaded).
void GfScriptTouchEntities(IScriptEngine* Script, bool bAcquire);
