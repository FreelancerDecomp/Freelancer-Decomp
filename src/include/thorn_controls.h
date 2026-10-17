// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"

// Thorn entity control interfaces.
//
// A Thorn scene drives the world through small DACOM facets QI'd off whatever object the
// script is attached to: one for the transform, one for the camera, one for lights, and so on.
// thorn.dll QIs them by IID (Iids::GeoTransformControl and friends, all in dacom_iids.h) and
// then calls slots on them. freelancer.exe implements them, as several facets of one attach
// object -- Client/Gf/ScriptBase.cpp in retail; here Thorn_Component
// (src/freelancer/Client/gf_thorn_component.cpp) and its per-entity-type subclasses in
// gf_thorn_*.cpp. A second, much smaller implementation of IGeoTransformControl alone lives in
// space_camera_script.cpp.
//
// Until 2026-09-16 NONE of these were declared anywhere. Thorn reached them through generic
// dispatchers that took the slot number as a runtime argument --
// `CallCtrlFloat(Ctrl, 6, Value)` -- and freelancer answered with `static void* [16]` arrays
// filled out with SlotOk / SlotOk1 / SlotGeneric placeholder stubs. Neither side could be
// checked against the other, and they disagreed in sixteen places. The comment that used to
// sit on the padding stub says it best: short tables had already made a call run off the end
// of the array into adjacent .data, and the fix at the time was to pad with more stubs.
//
// WHERE THE SIGNATURES COME FROM. thorn.dll's side of this contract is ported and verified
// (see the thorn.dll marathon), so a Thorn call site is a trace of what retail pushes. The
// freelancer placeholders were not traces of anything. Where the two disagreed, the caller
// wins, and each such slot says so below. Slots nobody calls keep the shape the placeholder
// had, marked TODO -- their widths are assumptions, not recoveries, and they must not be
// called until someone reads the retail body.
//
// SLOT COUNTS (2026-09-26). They now follow retail's tables, not the placeholder arrays they
// replaced. Every Thorn object in freelancer.exe (RTTI Thorn_Component and its thirteen
// derived classes, Client\Gf\ScriptBase.cpp) is these nine facets in one fixed order, and the
// shared QueryInterface LAB_004500d0 answers them from the table at 0x005cca20:
//   +0x00 ICompoundControl      +0x04 ISoundControl        +0x08 IIKControl
//   +0x0c IGeoTransformControl  +0x10 ICameraControl       +0x14 ID3DLightControl
//   +0x18 IPSysControl          +0x1c IFogControl          +0x20 IReverbControl
// Thorn_Component's tables (0x005cc590 .. 0x005cc6d4, each followed by the next table's RTTI
// locator) fix the length of every facet and, through each default body's RET n, the width of
// every slot. The slots the placeholders appended past those lengths never existed; no caller
// in the tree used them. ILightControl, IScriptControl, ISubtitleControl and IUserEventControl
// are not in that table -- a Thorn object answers E_NOINTERFACE (-3) for them.

// "1.11_IGeoTransformControl" -- position and orientation of the attached entity.
// Retail: the only control every entity type answers to.
struct IGeoTransformControl : IDAComponent
{
    virtual GENRESULT DACOM_API SetPosition(float* Position) = 0;     // 3
    virtual GENRESULT DACOM_API SetOrientation(float* Orient9) = 0;   // 4
    virtual GENRESULT DACOM_API GetPosition(float* OutPosition) = 0;  // 5
    virtual GENRESULT DACOM_API GetOrientation(float* OutOrient9) = 0; // 6
    // The interface stops here. The two retail sinks in freelancer.exe's script camera
    // (.rdata tables behind 0x0051e830) are seven entries long and nothing in the tree reads
    // past slot 6, so the nine further slots the port used to pad this table with were
    // invented, not recovered.
};

// "1.11_ICameraControl" -- camera and monitor entities.
// Retail sink: the Thorn_Camera camera facet, .rdata 0x005ccb18, thirteen entries (the dword
// at 0x005ccb4c is the next table's RTTI locator). Slots 4, 5, 9 and 10 are all LAB_00454190
// (MOV EAX,0xfffffff9; RET 8): one dword argument, "not implemented". Thorn does call 5 and
// 10 (the aspect pair) and gets that result back. The seven slots the port used to declare
// past 12 do not exist.
struct ICameraControl : IDAComponent
{
    // Slots 4 and 9 carry the facet prefix (see ICompoundControl).
    // Modern typed pointer transport preserves the raw F32 word until the callee
    // consumes it. Retail forwards DWORDs without evaluating floating arguments.
    // Slot count/order is unchanged; every port caller and implementation agrees.
    virtual GENRESULT DACOM_API SetFov(const float* Fov) = 0;          // 3  LAB_00450b40
    virtual GENRESULT DACOM_API CameraSlot4(void* Arg) = 0;     // 4  LAB_00454190
    virtual GENRESULT DACOM_API SetAspect(const float* Aspect) = 0;    // 5  LAB_00454190
    virtual GENRESULT DACOM_API SetNear(const float* Near) = 0;        // 6  LAB_00450b80
    virtual GENRESULT DACOM_API SetFar(const float* Far) = 0;          // 7  LAB_00450bc0
    virtual GENRESULT DACOM_API GetFov(float* OutFov) = 0;      // 8  LAB_00450c00
    virtual GENRESULT DACOM_API CameraSlot9(void* Arg) = 0;     // 9  LAB_00454190
    virtual GENRESULT DACOM_API GetAspect(float* OutAspect) = 0; // 10 LAB_00454190
    virtual GENRESULT DACOM_API GetNear(float* OutNear) = 0;    // 11 LAB_00450c20
    virtual GENRESULT DACOM_API GetFar(float* OutFar) = 0;      // 12 LAB_00450c40
};

// "1.11_IScriptControl" -- a sub-scene entity's control: thorn.dll's own script engine
// answers it (FScriptEngine facet +8, offset 8 in the {IID, offset} table .rdata:06f5b010),
// and the parent scene drives the sub-scene through it (ThornScene.cpp). Seven slots: the
// engine's abstract-base table .rdata:06f5ae04 and its final table .rdata:06f5ad18 both end
// after slot 6 (the next dword is the following table). Slots 3..6 are adjustor thunks
// (`this -= 8; JMP [vtbl+n]`) into IScriptEngine slots 22 / 26 / 23 / 17, which name them.
// Until 2026-10-16 this declared slots 7..15 as well; they never existed.
struct IScriptControl : IDAComponent
{
    virtual GENRESULT DACOM_API Slot3Start(int Arg) = 0;      // 3  LAB_06f3c080 -> engine slot 22
    virtual GENRESULT DACOM_API Slot4Tick(int DtMs) = 0;      // 4  LAB_06f3c0a0 -> engine slot 26
    virtual GENRESULT DACOM_API Slot5Stop() = 0;              // 5  LAB_06f3c0c0 -> engine slot 23
    virtual int DACOM_API Slot6GetDuration() = 0;             // 6  LAB_06f3c0e0 -> engine slot 17
};

// "1.11_ICompoundControl" -- compound and deformable entities: motions, hardpoints, floor
// height. Ten slots (Thorn_Component 0x005cc6e0..0x005cc704); the identity table continues
// with the Thorn object's own thiscall virtuals (gf_thorn_component.h), not with this interface.
// Thorn_Compound 0x005ccf78.., Thorn_Deformable 0x005cd1f8.. implement it.
struct ICompoundControl : IDAComponent
{
    // NAMES. A Thorn object inherits all nine facets, so two facets' slots with one name and one
    // signature would be a single override in C++ -- Thorn_Compound's motion stop would answer
    // ISoundControl slot 4 and IIKControl slot 4 too. Every slot a sibling facet also declares
    // therefore has a name of its own: recovered from the retail bodies where they say what the
    // slot does, the facet's prefix plus the slot number where they do not.
    //
    // 3 / 4: START_MOTION / its stop, with the motion descriptor (thorn_iface.h
    // FThornMotionEventDesc). Compound FUN_004513b0 / FUN_00451450, Deformable LAB_00452240 /
    // LAB_004523a0.
    virtual GENRESULT DACOM_API StartMotion(void* Desc) = 0;  // 3
    virtual GENRESULT DACOM_API StopMotion(void* Desc) = 0;   // 4
    // 5: ThornScene sends a float -- the floor height (Deformable LAB_00452d00 stores it at
    // +0x74 and hands the character the height above the script frame).
    virtual GENRESULT DACOM_API SetFloorHeight(float Height) = 0;
    // 6: elapsed milliseconds (Deformable LAB_00452210 turns them into the character's update).
    virtual GENRESULT DACOM_API AdvanceTime(int DeltaMs) = 0;
    // 7 / 8: CONNECT_HARDPOINTS / its disconnect, with the event descriptor (+0x1c / +0x20 the
    // two entity indices, +0x2c / +0x30 the two hardpoint names).
    virtual GENRESULT DACOM_API ConnectHardpoints(void* Desc) = 0;
    virtual GENRESULT DACOM_API DisconnectHardpoints(void* Desc) = 0;
    // 9: ThornScene sends (int TargetType, const char* Name, float* OutXform12). The out is a
    // Common Transform: the 3x3 orientation, then the position at +0x24.
    virtual GENRESULT DACOM_API GetHardpointXform(int TargetType, char const* Name,
                                                  float* OutXform12) = 0;
    // 10: GET, not set. Retail's thorn at 0x06f497fa does `LEA EAX,[ESI+0x70]; PUSH EAX;
    // PUSH ECX; CALL [EDX+0x28]` -- slot 10, and the argument is an address.
    virtual GENRESULT DACOM_API GetFloorHeight(float* OutHeight) = 0;
    // 11 / 12: the whole frame as a Common Transform (orientation, then position at +0x24):
    // Compound LAB_00451660 / LAB_00451a00, Deformable LAB_00452b50 / LAB_00452c70. Both RET 8.
    virtual GENRESULT DACOM_API SetTransform(float const* Transform12) = 0;
    virtual GENRESULT DACOM_API GetTransform(float* OutTransform12) = 0;
};

// "1.11_IIKControl" -- inverse kinematics on deformable entities. Three slots
// (Thorn_Component 0x005cc690..0x005cc698; Thorn_Deformable 0x005cd1a8.. LAB_00452460 /
// FUN_00452a30 / LAB_00452680).
struct IIKControl : IDAComponent
{
    // 3 and 5: ThornScene sends (void* Desc, float* Position, float* Orientation) to both.
    // Deformable: 3 starts an aim on a free slot, 4 stops the slot whose event id matches, 5
    // moves a running aim's target.
    virtual GENRESULT DACOM_API StartIk(void* Desc, float* Position, float* Orientation) = 0;
    virtual GENRESULT DACOM_API StopIk(void* Desc) = 0;
    virtual GENRESULT DACOM_API UpdateIk(void* Desc, float* Position, float* Orientation) = 0;
};

// "1.11_ILightControl" -- light and scene entities.
//
// Slots 6 and 7 were transposed. ThornScene sends the cone cutoff to slot 6 and the on/off
// flag to slot 7; freelancer had a no-argument placeholder in 6 and the cutoff setter in 7,
// so the cutoff was dropped on the floor and the setter received a boolean instead.
struct ILightControl : IDAComponent
{
    virtual GENRESULT DACOM_API SetColor(int* Rgb) = 0;        // 3
    virtual GENRESULT DACOM_API SetDirection(float* Dir3) = 0; // 4
    virtual GENRESULT DACOM_API SetRange(float Range) = 0;     // 5
    virtual GENRESULT DACOM_API SetCutoff(float Radians) = 0;  // 6
    virtual GENRESULT DACOM_API SetOn(int On) = 0;             // 7
    // 8: placeholder took nothing; ThornScene sends `(Type == 3) ? 1 : 0`, i.e. a spot flag.
    virtual GENRESULT DACOM_API SetIsSpot(int IsSpot) = 0;
    virtual GENRESULT DACOM_API GetColor(float* OutRgb) = 0;      // 9
    virtual GENRESULT DACOM_API GetDirection(float* OutDir3) = 0; // 10
    virtual GENRESULT DACOM_API GetRange(float* OutRange) = 0;    // 11
    virtual GENRESULT DACOM_API GetCutoff(float* OutRadians) = 0; // 12
    // 13 and 14: placeholders took nothing; ThornScene reads both with a float*. Which
    // property each returns is not recovered -- the port's callers only copy them into a
    // light blob -- so they keep numeric names.
    virtual GENRESULT DACOM_API GetSlot13(float* Out) = 0;
    virtual GENRESULT DACOM_API GetSlot14(float* Out) = 0;
    virtual GENRESULT DACOM_API Slot15() = 0;                  // 15 TODO
    virtual GENRESULT DACOM_API Slot16(void* Arg) = 0;         // 16..19 TODO
    virtual GENRESULT DACOM_API Slot17(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot18(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot19(void* Arg) = 0;
};

// "1.11_ID3DLightControl" -- the Direct3D-facing half of a light entity.
//
// ThornScene sends (on-flag, &type, direction) to slots 3, 4 and 5. freelancer's table read
// those as (float, direction, colour), so the direction went into the colour setter and the
// light type into the direction setter.
//
// Slot semantics read off the one implementation, Thorn_Light (freelancer.exe table 0x005cd458,
// 2026-09-26): 3 LAB_00453310 writes the registry enable byte, 4 LAB_004533d0 hands its
// argument to the engine light's D3DLIGHT8 setter FUN_00411120, 5 LAB_00453330 stores a
// direction, 6 LAB_004533f0 reads the enable byte into an int, 7 LAB_00453450 copies 0x68 bytes
// (a D3DLIGHT8) out of the engine light, 8 LAB_00453420 reads the direction back. ThornScene
// passes the light props' +0x20 (On), +0x28 (the embedded D3DLIGHT8) and +0x0c (Direction) to
// exactly these. The D3DLIGHT8 travels as void*: freelancer keeps its own FD3dLight8 layout
// (engine_light.h) and Thorn only forwards the blob.
struct ID3DLightControl : IDAComponent
{
    virtual GENRESULT DACOM_API SetOn(int On) = 0;                   // 3
    virtual GENRESULT DACOM_API SetLight(void const* D3dLight8) = 0; // 4
    virtual GENRESULT DACOM_API SetDirection(float* Dir3) = 0;       // 5
    virtual GENRESULT DACOM_API GetOn(int* OutOn) = 0;               // 6
    virtual GENRESULT DACOM_API GetLight(void* OutD3dLight8) = 0;    // 7
    virtual GENRESULT DACOM_API GetDirection(float* OutDir3) = 0;    // 8
    // Six slots (Thorn_Component 0x005cc610..0x005cc624, all LAB_00454190: one argument).
};

// "1.11_IFogControl" -- scene entities. Shares the attach object's D3D-light facet field:
// a scene entity puts the fog table there instead of the D3D light table.
// Ten slots; the widths are Thorn_Component's defaults (0x005cc5b4..0x005cc5d8): 3..6 and
// 11..12 LAB_00454190 (one argument), 7..8 LAB_0044fcd0 (three), 9..10 LAB_0044fce0 (two).
// Thorn_Scene's table 0x005cdec0 is the one implementation (FThorn_Scene, gf_thorn_scene.cpp).
// Names and argument types read off that table (LAB_00454450..FUN_00454690): the colour is
// three ints whose low bytes are kept (LAB_00454500), and slots 11 / 12 exchange the 0x20-byte
// block {+0 on, +1 table, +4 int colour[3], +0x10 mode, +0x14 start, +0x18 end, +0x1c density}
// through the facet's own slots 3/5/7/9 and 4/6/8/10. thorn.dll FUN_06f43140 passes the colour
// ints straight from that block (0x06f431bb ADD ESI,0x13c; PUSH ESI).
struct IFogControl : IDAComponent
{
    virtual GENRESULT DACOM_API SetOn(unsigned char On) = 0;                          // 3
    virtual GENRESULT DACOM_API GetOn(unsigned char* OutOn) = 0;                      // 4
    virtual GENRESULT DACOM_API SetColor(int const* Rgb3) = 0;                        // 5
    virtual GENRESULT DACOM_API GetColor(int* OutRgb3) = 0;                           // 6
    virtual GENRESULT DACOM_API SetRange(float Start, float End, float Density) = 0;  // 7
    virtual GENRESULT DACOM_API GetRange(float* OutStart, float* OutEnd,
                                         float* OutDensity) = 0;                      // 8
    virtual GENRESULT DACOM_API SetTableMode(unsigned char Table, unsigned int Mode) = 0; // 9
    virtual GENRESULT DACOM_API GetTableMode(unsigned char* OutTable,
                                             unsigned int* OutMode) = 0;              // 10
    virtual GENRESULT DACOM_API SetPacked(unsigned char const* Packed32) = 0;         // 11
    virtual GENRESULT DACOM_API GetPacked(unsigned char* Out32) = 0;                  // 12
    // Modern raw-DWORD transport; original thirteen slots retain their indices.
    virtual GENRESULT DACOM_API SetRangeValue(const float* Start, const float* End,
                                              const float* Density) = 0;             // 13 +0x34
};

// "1.11_IReverbControl" -- scene entities.
struct IReverbControl : IDAComponent
{
    // 3 and 4 are the widest mismatch in this family. ThornScene sends four dwords to each;
    // freelancer had a ONE-argument setter in slot 3 and a NO-argument placeholder in slot 4,
    // so a reverb set leaked twelve bytes of stack and a reverb get leaked sixteen.
    virtual GENRESULT DACOM_API SetReverb(unsigned int Environment, float Volume, float Decay,
                                          float Damping) = 0;
    virtual GENRESULT DACOM_API GetReverb(float* OutEnvironment, float* OutVolume,
                                          float* OutDecay, float* OutDamping) = 0;
    // Two slots (Thorn_Component 0x005cc59c / 0x005cc5a0, LAB_0044fcf0: four arguments).
};

// "1.11_IPSysControl" -- particle-system entities. Five slots (Thorn_Component
// 0x005cc5ec..0x005cc5fc: 3 and 4 LAB_0044fcc0 with no argument, 5..7 LAB_00454190 with one).
// Slot 4 ends the effect: Thorn_Psys LAB_00453fd0 destroys it (FUN_005574a0), and ThornScene
// sends it when a START_PSYS event ends. It cannot be called Stop: the Thorn objects carry a
// thiscall Stop() of their own (identity slot 14) that it would collide with.
struct IPSysControl : IDAComponent
{
    virtual GENRESULT DACOM_API Start() = 0;                   // 3
    virtual GENRESULT DACOM_API StopEffect() = 0;              // 4
    virtual GENRESULT DACOM_API Tick(int DeltaMs) = 0;         // 5
    virtual GENRESULT DACOM_API SetSparam(float Value) = 0;    // 6
    virtual GENRESULT DACOM_API GetSparam(float* OutValue) = 0; // 7
};

// "1.11_ISoundControl" -- sound entities. Eight slots (Thorn_Component 0x005cc6ac..0x005cc6c8:
// 3, 9 and 10 LAB_0044fce0 with two arguments, 4..8 LAB_00454190 with one).
struct ISoundControl : IDAComponent
{
    virtual GENRESULT DACOM_API StartSound(void* Desc, int TimeMs) = 0; // 3
    // 4..8 read off Thorn_Sound's table 0x005ce234: slot 4 (LAB_004575e0) stops the playing
    // handle and ThornScene sends it when a START_SOUND event ends; 5 / 7 (LAB_004576c0 /
    // LAB_00457700) keep the attenuation at +0x88 that identity slot 21 adds to the fade before
    // the handle's SetAttenuation (ISoundSource slot 32); 6 / 8 (LAB_00457720 / LAB_00457740)
    // keep the pan at +0x8c that goes, times 100, to the handle's SetPan (slot 33).
    virtual GENRESULT DACOM_API StopSound(void* Desc) = 0;               // 4
    virtual GENRESULT DACOM_API SetAttenuation(float Attenuation) = 0;   // 5
    virtual GENRESULT DACOM_API SetPan(float Pan) = 0;                   // 6
    virtual GENRESULT DACOM_API GetAttenuation(float* OutAttenuation) = 0; // 7
    virtual GENRESULT DACOM_API GetPan(float* OutPan) = 0;               // 8
    // 9: ThornScene sends (float* Props8, int Mask).
    virtual GENRESULT DACOM_API SetAudioProps(float const* Props8, int Mask) = 0;
    // 10: the capture half of the same pair.
    virtual GENRESULT DACOM_API GetAudioProps(float* Props8, int Mask) = 0;
};

// Thorn binds SubtitleControl separately from UserEventControl. Subtitle calls
// pass a string ID; user-event calls pass a descriptor. A common foreign facet
// has not been established by retail producer/consumer evidence.
struct ISubtitleControl : IDAComponent
{
    virtual GENRESULT DACOM_API ShowSubtitle(int StringId) = 0;   // 3
    virtual GENRESULT DACOM_API HideSubtitle(int StringId) = 0;   // 4
    virtual GENRESULT DACOM_API Slot5() = 0;
    virtual GENRESULT DACOM_API Slot6() = 0;
    virtual GENRESULT DACOM_API Slot7(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot8(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot9(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot10(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot11(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot12(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot13(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot14(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot15(void* Arg) = 0;
};

// Thorn requests this IID into its own UserEventControl field. The retail
// client QI table does not establish a shared SubtitleControl/UserEventControl
// facet; foreign providers remain unresolved.
struct IUserEventControl : IDAComponent
{
    virtual GENRESULT DACOM_API Slot3(void* Desc) = 0;         // 3
    // 4: placeholder took nothing, ThornScene passes a descriptor.
    virtual GENRESULT DACOM_API Slot4(void* Desc) = 0;
    virtual GENRESULT DACOM_API Slot5() = 0;                   // 5, 6 TODO
    virtual GENRESULT DACOM_API Slot6() = 0;
    virtual GENRESULT DACOM_API Slot7(void* Arg) = 0;          // 7..15 TODO
    virtual GENRESULT DACOM_API Slot8(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot9(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot10(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot11(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot12(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot13(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot14(void* Arg) = 0;
    virtual GENRESULT DACOM_API Slot15(void* Arg) = 0;
};
