// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// IScriptEngine scene: Lua foreach → C++ entities/events + playback clock.
// Layout: docs/re/thorn.md and thorn_thn_format.md.

#include "Thorn.h"
#include "thorn_controls.h"
#include <list>
#include <map>
#include <memory>
#include <string>
#include <optional>
#include <set>
#include <vector>

static const int kThornPlaybackLoaded = 2;
static const int kThornPlaybackReset = 3;
static const int kThornPlaybackDone = 4;
static const int kThornPlaybackPaused = 5;
static const int kThornPlaybackPlay = 6;
static const int kThornPlaybackLoop = 7;

// The entity / event type constants, FThornLightProps, FThornMotionPathDesc and
// FThornEntitySnap are public ABI now: src/include/thorn_iface.h (through Thorn.h).

static const int kThornTargetRoot = 0;
static const int kThornTargetHardpoint = 1;
static const int kThornTargetPart = 2;

static const int kThornFlagUseScriptDuration = 1;
static const int kThornFlagPosition = 2;
static const int kThornFlagOrientation = 4;
static const int kThornFlagLookAt = 8;
static const int kThornFlagQuatOrient = 0x10;
static const int kThornFlagEntityRelative = 0x20;
static const int kThornFlagParentChild = 0x40;
static const int kThornFlagOrientRelative = 0x80;

static const int kThornSpatialBitPos = 0x1;
static const int kThornSpatialBitOrient = 0x2;
static const int kThornSpatialBitAxisRot = 0x4;

// Type 5 load PTR_DAT_06f5e148: fovh/aspect/near/far → bits 1/2/4/8 (not GET-order names).
static const int kThornCamBitFovh = 0x1;
static const int kThornCamBitAspect = 0x2;
static const int kThornCamBitNear = 0x4;
static const int kThornCamBitFar = 0x8;

static const int kThornLightBitColor = 0x1;
static const int kThornLightBitDirection = 0x2;
static const int kThornLightBitRange = 0x4;
static const int kThornLightBitCutoff = 0x8;
static const int kThornLightBitOn = 0x10;
static const int kThornLightBitInfinite = 0x20;
static const int kThornLightBitAtten = 0x40;
static const int kThornLightBitTheta = 0x80;
static const int kThornLightBitType = 0x100;
static const int kThornLightBitSpecular = 0x200;
static const int kThornLightBitAmbient = 0x400;
static const int kThornLightBitDiffuse = 0x800;

// Retail Csys object: orient 3×3 @ +0, pos @ +0x24 (FUN_06f3b970 / event+0x4).
struct FThornCsys
{
    float Orient[9];
    float Pos[3];
};

struct FThornUserPropLess
{
    bool operator()(const std::string& Left, const std::string& Right) const;
};

typedef std::map<std::string, std::string, FThornUserPropLess> FThornUserProps;

// Native replacement for the retail START_IK constructor payload.  The retail
// object also carried VC6 string/vector state around this data; this descriptor
// keeps only the observed game fields and deliberately has no retail ABI.
struct FThornIkEventDesc
{
    int Desc[11];
    int CountToRoot;
    int LookAtUpAxis;
    int LookAtFrontAxis;
    float Damping;
    unsigned char bPointAt;
    unsigned char bMoveTo;
    unsigned char Pad[2];
    float TransitionDuration;
    const char* EndEffector;
    const char* TargetPart;
    int TargetType;
    int IkId;
};

class FThornEntity;

// Private typed circular owner. Node identity survives append/other-node erase;
// callbacks observe Next only after returning, as in 41490/414d0/41540.
struct FThornMonitorLinkNode
{
    FThornMonitorLinkNode* Next;
    FThornMonitorLinkNode* Previous;
    FThornEntity* Entity;
};

class FThornMonitorLinks
{
private:
    FThornMonitorLinkNode InactiveSentinel;
    std::unique_ptr<FThornMonitorLinkNode> OwnedSentinel;

public:
    FThornMonitorLinks();
    ~FThornMonitorLinks();
    void Initialize();
    void Reset();
    void Erase(FThornMonitorLinkNode* Node);
    void Clear();
    FThornMonitorLinks(const FThornMonitorLinks&) = delete;
    FThornMonitorLinks& operator=(const FThornMonitorLinks&) = delete;

    FThornMonitorLinkNode* Sentinel;
    unsigned int Count;
};

// C++ inner entity (not DACOM). Retail: operator_new per-type + vtable
// PTR_FUN_06f5b05c / 06f5b084 / … . Slot 3 +0x0C / slot 4 +0x10 = thiscall RET 4
// (LAB_06f49f00 / FUN_06f41490 / … → FUN_06f40900 / FUN_06f40ab0).
class FThornEntity
{
public:
    FThornEntity();
    FThornEntity(int Type, const FThornEntitySnap* Desc, FThornScene* OwnerScene);
    virtual ~FThornEntity() noexcept(false);
    void ReleaseCommonEntityState();
    char* AssignEntityName(const char* Source);
    char* AssignEntityTemplate(const char* Source);
    virtual void InnerUnused1();
    virtual void InnerApplyDesc(const FThornEntitySnap* SnapIn);
    float* InnerGetPos();
    float* InnerGetOrient();
    virtual void InnerSetPos(const float* Pos);
    virtual void InnerSetOrient(const float* Mat9);

    // Retail FUN_06f423d0: refresh the live audio payload, then copy all
    // eight words to the caller's output buffer.
    void CaptureAudioProps(float* Out8);

    // Retail FUN_06f42210 initializes SOUND ain/aout to 0x168 before the
    // descriptor payload is applied.  Keep that semantic default in the
    // native audio owner; it is not a retail object-layout contract.
    void InitializeSoundDefaults();

    // Retail FUN_06f41a20 releases the specialized light controls before
    // common entity cleanup.  Keep that ownership operation on the entity;
    // the scene chooses the remaining controls by bound entity type.
    void ReleaseLightControls();

    // Concrete derived controls precede common owned data and User/Geo teardown.
    void ReleaseDerivedControls();
    // Concrete entity virtual+0x24 is separate from deleting-destructor cleanup.
    void InnerUnbindControls();

    // Modern owner-side equivalent of retail FUN_06f433c0. Scene unbind
    // intentionally leaves ReverbControl held until final destruction,
    // matching the retail unbind body without reproducing its object layout.
    void ReleaseSceneBoundControls();

    // Retail FUN_06f437e0 is the motion-path three-control teardown.
    void ReleaseMotionPathControls();

    // Retail FUN_06f427d0: the MOTION_PATH constructor (FUN_06f426f0) calls
    // this once, right after ApplyDesc, with the entity's own
    // mutable Snap.MotionPath pair. It retains the descriptor's
    // path object a second time (+0xe8) and queries "1.11_IMotionPath" from
    // that copy into MotionPath (+0xd0).  UpdateEntityFromDesc never
    // re-enters this path; Attach later rebinds MotionPath from the engine
    // object.
    void BindMotionPathFromDescObject();

private:
    // String owners are distinct from the published descriptor pointers.
    // Construction's primitive-tail memset excludes all these RAII objects.
    std::optional<std::string> NameStorage;
    std::optional<std::string> TemplateStorage;
public:
    FThornMonitorLinks MonitorLinks;
    std::optional<FThornUserProps> UserProps;
    FThornEntitySnap Snap;
    // Retail entity +0x38 / +0x44 (FUN_06f40c40 / FUN_06f40c60): the pose
    // as loaded by the last ApplyDesc, already converted into the parent
    // Csys when one exists.  InnerSetPos/InnerSetOrient and the slot-5 live
    // refresh update Snap only; IScriptEngine slots 10/11 (LAB_06f38950 /
    // LAB_06f38a80) and therefore the retail serializer read these words.
    float LoadPos[3];
    float LoadOrient[9];
    void* EngineObj;
    FThornCsys* ParentCsys;
    int BindState;
    unsigned char bBound;
    // Type whose interface sequence is currently held by this aggregate.
    // Cleanup must use this value rather than a descriptor type that may
    // have been replaced after Attach and before Detach/Destroy.
    int BoundControlType;
    IGeoTransformControl* GeoControl;
    ICameraControl* CameraControl;
    ICompoundControl* CompoundControl;
    IIKControl* IkControl;
    ILightControl* LightControl;
    ID3DLightControl* D3dLightControl;
    ISoundControl* SoundControl;
    IUserEventControl* UserEventControl;
    ISubtitleControl* SubtitleControl;
    IFogControl* FogControl;
    IReverbControl* ReverbControl;
    IPSysControl* PsysControl;
    IMotionPath* MotionPath;
    // MOTION_PATH +e8 retains the construction-time path separately from
    // the mutable public owning descriptor reference (+cc); both release
    // in FUN_06f42cb0 before the sole PathType string and runtime +d0.
    // Parse-only temporary reference; actual MOTION_PATH owning +cc is
    // the mutable public Snap.MotionPath.MotionPath field itself.
    IPersistString* ParsedMotionPathDescObject;
    IPersistString* MotionPathBoundObject;
    // Typed construction identity differs from the Lua descriptor staging owner.
    bool bMotionPathDescriptorOwner;
    // Constructor unwind owns only the string, runtime interface and common base.
    bool bMotionPathConstructionComplete;
    IScriptControl* ScriptControl;


    // Concrete owner selected by the entity factory; descriptor Type can change on updates.
    int DescriptorOwnerType;
    // FUN_06f41540 / 06f460f0: runtime +0xec..+0xf8, separate from Snap +0xd4..+0xe0.
    float CameraRuntimeProps[4];

    // Type-specific dump/load (FUN_06f2aa10 / FUN_06f28a60). Not in Common snapshot 0x54.
    float FloorHeight;
    int LightMask;
    int LightOn;
    int LightInfinite;
    int LightType;
    float LightColor[3];
    float LightDiffuse[3];
    float LightSpecular[3];
    float LightAmbient[3];
    float LightDirection[3];
    float LightRange;
    float LightCutoff;
    float LightTheta;
    float LightAtten[3];
    // FUN_06f41960 clears the two light-entity runtime words at retail +0x158/+0x15c.
    int LightRuntimeState[2];
    // Retail initializes light payload in the constructor, not on every
    // descriptor update.
    unsigned char bLightStateInitialized;
    // FUN_06f41b00 copies the 0x24-dword descriptor to retail this+0x160.
    FThornLightProps LightEntityDescriptor;
    float Audio[8];
    int AudioMask;
    float PsysRuntimeSparam; // Retail +0xd0; retained input lives in Snap.PsysSparam.
    char* PathType;
    std::size_t PathTypeCapacity;
    const char* MotionPathBoundType; // Constructor cache corresponding to retail +e4.
    char* PathData;
    int SceneUpAxis;
    int SceneFrontAxis;
    int SceneAmbient[3];
    int SceneFogMask;
    // Entity+0x11c: 15 dword blob. Fog 8-dword lives at packed[7] (= +0x138).
    unsigned int SceneFogPacked[15];
    int SceneStartTime;
    float SceneReverb[4];
    int SceneReverbMask;
    // Retail FUN_06f431d0 mirrors packed words +0x130/+0x134 through these
    // optional caller-owned outputs.  Keep the pointers as native state;
    // their presence does not expose the retail object layout or ABI.
    unsigned int* SceneFogOutputA;
    unsigned int* SceneFogOutputB;
};

class FThornScene;

// Private CONNECT string ownership; published descriptor pointers are separate.
// This is a current-CRT owner, without a VC6 basic_string representation.
struct FThornConnectString
{
    std::unique_ptr<char[]> Data;
    size_t Length = 0;
    size_t Capacity = 0;
    FThornConnectString() = default;
    FThornConnectString(const FThornConnectString&) = delete;
    FThornConnectString& operator=(const FThornConnectString&) = delete;
    bool Assign(const char* Source);
    char* GetPublishedData();
    void Reset();
};

// C++ event vtable (not DACOM). Slot 2 = GetDesc → this+0x28 (LAB_06f4a470).
// Member order matches FUN_06f43940 + desc copy @ +0x28.
class FThornEvent
{
public:
    FThornEvent();
    explicit FThornEvent(int DescriptorStorageType); // storage selection only; no descriptor callbacks
    FThornEvent(int Type, const void* Desc);
    virtual ~FThornEvent() noexcept(false);
    virtual void ApplyDesc(const void* Desc);
    void ApplyCommonDesc(const void* Desc);
    void FinalizeDesc();
    void ApplySetCameraDesc(const void* Desc);
    void ApplyCameraPropAnimDesc(const void* Desc);
    void ApplyStartSoundDesc(const void* Desc);
    void ApplyFloorHeightDesc(const void* Desc);
    void ApplyStartPsysDesc(const void* Desc);
    void ApplyAudioPropAnimDesc(const void* Desc);
    void ApplyPsysPropAnimDesc(const void* Desc);
    void ApplyUserEventDesc(const void* Desc);
    virtual void* GetDesc();
    virtual unsigned char Start(FThornScene* Scene, int TimeMs);
    void DispatchSubtitleControl(FThornScene* Scene, bool bShow);
    virtual unsigned char Stop(FThornScene* Scene);
    virtual unsigned char StillRunning(int TimeMs, FThornScene* Scene);

    // Modern owner-side equivalent of the deleting-destructor branch used by
    // the retail event vtables.  Heap ownership is explicit at scene call
    // sites; no VC6 wrapper or cross-module delete ABI is exposed.
    static void DestroyNative(FThornEvent*& Event);

    // Retail FUN_06f49d00 is the typed descriptor path for subtitle events.
    // It deliberately remains separate from the common descriptor operation
    // so the mismatch behavior for dword 0 and strid is observable.
    void ApplySubtitleDesc(const void* Desc);

    // Retail LAB_06f49b90 is the event progress accessor used by the
    // type-18 vtable. Keep its arithmetic on the modern event owner; no
    // retail vtable or aggregate layout crosses this boundary.
    float EvaluateProgress(int TimeMs) const;

    // Retail LAB_06f493e0: REVERB never takes the period branch and
    // always rounds its ratio before either the plain curve call or return.
    float EvaluateProgressNoPeriodWrap(int TimeMs) const;
    void EvaluateProgressExtended(int TimeMs, FThornExtendedProgress* Result,
                                  bool bNoPeriodWrap) const;

    // Retail per-type event vtable slot 7 (base+0x1c): constant type-name
    // string getter ("MOTION", "IK", ... "SUBTITLE"). The port keeps one
    // Type-switched event class, so the 19 retail getters collapse here.
    const char* TypeName() const;

    // Retail LAB_06f44ec0: resolve both event endpoints, connect camera
    // monitor links, copy camera properties, then copy the source live pose.
    unsigned char StartSetCamera(FThornScene* Scene);
    unsigned char StartCameraPropAnim(FThornScene* Scene);
    unsigned char StartFogPropAnim(FThornScene* Scene);
    unsigned char StartPsysPropAnim(FThornScene* Scene);
    unsigned char StartReverbPropAnim(FThornScene* Scene);
    unsigned char TickReverbPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char StartAudioPropAnim(FThornScene* Scene);
    unsigned char TickAudioPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char TickFogPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char StartAttachEntity(FThornScene* Scene);
    unsigned char StartSpatialPropAnim(FThornScene* Scene);
    unsigned char TickCameraPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char TickPsysPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char TickLightPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char TickSpatialPropAnim(int TimeMs, FThornScene* Scene);
    unsigned char TickAttachEntity(int TimeMs, FThornScene* Scene);
    unsigned char TickPathAnim(int TimeMs, FThornScene* Scene);
    unsigned char StartFloorHeightAnim(FThornScene* Scene);
    unsigned char TickFloorHeightAnim(int TimeMs, FThornScene* Scene);

    void ApplyIkExtendedDesc(const FThornIkEventDesc* Desc);
    void ApplyStartIkDesc(const void* Desc);
    void SetIkEndEffector(const char* Name);
    void SetIkTargetPart(const char* Name);

    // Modern owner-side equivalent of FUN_06f484e0.  Type 12 has one
    // descriptor word beyond the common eleven-word event record; keep that
    // payload typed instead of reproducing the retail subclass layout.
    void ApplySubSceneDesc(const void* Desc, float StartTimeValue);

    // Modern owner-side equivalent of retail FUN_06f477e0: type-8 (attach)
    // carries six extra descriptor words (TargetType, PointAt xyz,
    // LookAtUpAxis, LookAtFrontAxis) plus one ref-counted target-part string
    // beyond the common eleven-word record.  A mismatched input type resets
    // the six words to their construction-time defaults instead of copying.
    void ApplyAttachDesc(const void* Desc);
    void ApplyConnectDesc(const void* Desc);
    // FUN_06f48050: two constructor setter calls, then descriptor event ID.
    void InitializeConnectDesc(const void* Desc);
    void SetConnectHardpoint(const char* Name);
    void SetConnectParentHardpoint(const char* Name);

    // Modern owner-side equivalent of retail FUN_06f450a0: type-6 (path
    // animation) carries eight extra descriptor words (StartPercent,
    // StopPercent, PercentDelta, PathOffset xyz, LookAtUpAxis,
    // LookAtFrontAxis) beyond the common eleven-word record.  A mismatched
    // input type resets them to their construction-time defaults instead of
    // copying.
    void ApplyPathAnimDesc(const void* Desc);

    // Native owner for the retail type-15 audio-property descriptor.  The
    // retail subclass carried the same semantic state behind a VC6 layout;
    // this adapter keeps the payload and mask in the current event owner.

    // Portable owner-side equivalent of the retail audio subclass cleanup.
    // Retail +0x4c is the optional descriptor curve, not a separate audio
    // ABI object; the current event owns it as ParamCurve.
    void ReleaseAudioOwnedState();

    // Portable owner-side equivalent of the camera subclass cleanup.  Retail
    // FUN_06f46010 releases only the optional curve before common cleanup.
    void ReleaseCameraOwnedState();

    // Portable owner-side equivalent of the spatial-property subclass
    // cleanup.  Retail FUN_06f46550 releases the optional curve and its
    // ref-counted target-part string before common cleanup.
    void ReleaseSpatialOwnedState();

    // Portable owner-side equivalent of the retail attach subclass cleanup.
    // TargetPart is the modern owned form of the retail ref-counted string
    // stored by the attach descriptor.
    void ReleaseAttachOwnedState();

    // Retail FUN_06f43ec0 releases the two owned IK endpoint strings.
    void ReleaseIkOwnedState();

    // Retail FUN_06f43a70 releases the two owned motion descriptor strings.
    void ReleaseMotionOwnedState();

    // Native path for the retail control-backed curve evaluator.  The
    // multiplier and additive term are the path event's stop/start percent
    // pair; the curve itself remains owned by the event.
    float EvaluateControlBackedCurve(float Scale, int TimeMs);

    // Retail FUN_06f45660: type-4 event descriptor.  The payload is kept in
    // the modern light-property structure, without reproducing the retail
    // aggregate's VC6 object layout.
    void ApplyLightEventDesc(const void* Desc);

    // Retail FUN_06f465f0: type-7 (SPATIAL PROP ANIM) descriptor.  Beyond
    // the common eleven-word record it carries the spatial mask, the target
    // orientation/position, an optional quaternion (used only with
    // kThornFlagQuatOrient, otherwise derived from the matrix), the
    // axis-rotation angle/axis pair and the target part/type.
    void ApplySpatialPropAnimDesc(const void* Desc);

    // Retail FUN_06f43af0: raw START_MOTION descriptor loading.
    void ApplyMotionDesc(const void* Desc);

    // Native payload owners for the specialized camera, PSYS and fog event
    // descriptors.  DescriptorType preserves the retail type-mismatch branch.
    void ApplyCameraPropAnimPayload(int DescriptorType, int Mask, const float* Target4);
    void ApplyPsysPropAnimPayload(int DescriptorType, int Mask, float Target);
    void ApplyFogPropAnimDesc(const void* DescIn);
    void ApplyReverbPropAnimDesc(const void* DescIn);
    void ApplyFogPropAnimPayload(int DescriptorType, int Mask, const FThornFog8* Target);

    // Modern owner-side equivalent of FUN_06f49150.  Retail type 17 carries
    // one mask dword and four payload words after the common 11-word record;
    // keep the payload in FThornEvent instead of exposing the retail
    // subclass/subobject or VC6 deleting-destructor ABI.
    void ApplyReverbPropAnimPayload(int DescriptorType, int Mask, const FThornReverb4* Target4);

    FThornCsys* ParentCsys;
    unsigned char bOwnsParentCsys;
    const unsigned int* SceneUpAxis;
    const unsigned int* SceneFrontAxis;
    int StartTimeMs;
    int UniqueId;
    int Type;
    int PrimaryIndex;
    int SecondaryIndex;
    int EndTimeMs;
    int Unused20;
    const bool bFloorEventOwner;
    // Concrete event identity survives descriptor callbacks and type mutation.
    const int NativeEventType;
    // Canonical typed storage; reference views are the exact same member types.
    FThornUserEventDesc UserEventDesc;
    FThornFogEventDesc FogDesc;
    FThornPsysPropEventDesc PsysPropDesc;
    FThornAudioPropEventDesc AudioPropDesc;
    FThornLightEventDesc LightDesc;
    FThornReverbEventDesc ReverbDesc;
    FThornCameraEventDesc CameraDesc;
    FThornEventDesc SetCameraDesc; // actual type-2 common-only owner (44e60).
    FThornFloorEventDesc FloorDesc; // actual type-18 live 56-byte owner (49570).
    FThornPathEventDesc PathDesc; // actual type-6 live 76-byte owner (450a0).
    IParamCurve*& ParamCurve;
    FThornEventDesc& Desc;
    int& PcurvePeriod;
    float TargetPos[3];
    float TargetOrient[9];
    float CapturePos[3];
    float CaptureOrient[9];
    FQuaternion TargetQuat;
    FQuaternion CaptureQuat;
    int SpatialMask;
    FThornSpatialEventDesc SpatialDesc;
    float AxisRotAngle;
    int AxisRotAxis;
    float AxisRotApplied;
    float SpatialWrapLast;
    bool bHasTargetPos;
    bool bHasTargetOrient;
    bool bCaptured;

    char* UserEventString;
    int& CamMask;
    // Load +0x58: fovh/aspect/near/far. Start capture +0x68 GET 8/11/12/10: fovh/near/far/aspect.
    float (&CamTarget)[4];
    float CamCapture[4];
    int& LightMask;
    FThornLightProps& LightTarget;
    FThornLightProps LightCapture;
    float& StartPercent;
    float& StopPercent;
    float& PercentDelta;
    float (&PathOffset)[3];
    char* Animation;
    char* LockedBone;
    float TransTime;
    float TimeScale;
    float Weight;
    float Heading;
    float TransScale;
    float MotionStartTime;
    unsigned char Field_0x40; // TODO: recover semantic name; descriptor +0x40.
    int Field_0x50; // TODO: recover semantic name; descriptor +0x50.
    float SubSceneStartTime;
    char* Hardpoint;
    char* ParentHardpoint;
    FThornConnectString ConnectHardpointStorage;
    FThornConnectString ConnectParentHardpointStorage;
    bool bConnectStorageActive = false;
    void AdoptConnectStringStorage();
    void ReleaseConnectStringStorage();
    char* EndEffector;
    const char* LegacyTargetPart;
    const char*& TargetPart;
    FThornConnectString IkEndEffectorStorage;
    FThornConnectString IkTargetPartStorage;
    bool bIkStorageActive = false;
    FThornConnectString MotionAnimationStorage;
    FThornConnectString MotionLockedBoneStorage;
    bool bMotionStorageActive = false;
    FThornConnectString AttachTargetPartStorage;
    bool bAttachStorageActive = false;
    FThornConnectString SpatialTargetPartStorage;
    bool bSpatialStorageActive = false;
    void AdoptSpatialStringStorage();
    void SetSpatialTargetPart(const char* Name);
    FThornConnectString FloorTargetPartStorage;
    bool bFloorStorageActive = false;
    void AdoptFloorStringStorage();
    void SetFloorTargetPart(const char* Name);
    void ReleaseFloorOwnedState();
    int LegacyTargetType;
    int& TargetType;
    int& LookAtUpAxis;
    int& LookAtFrontAxis;
    float PointAt[3];
    unsigned char bPointAt;
    unsigned char bMoveTo;
    float AttachXform[12];
    // Written first by the primary hardpoint callback; retained between IK ticks.
    float IkEffectorXform[12];
    int IkId;
    int CountToRoot;
    float Damping;
    float TransitionDuration;
    float& SparamTarget;
    float SparamCapture;
    int& PsysMask;
    int& AudioMask;
    FThornAudio8& AudioTarget;
    FThornAudio8 AudioCapture;
    int& FogMask;
    FThornFog8& FogTarget;
    int FogCaptureOn;
    float FogCaptureColor[3];
    float FogCaptureStart;
    float FogCaptureEnd;
    float FogCaptureDensity;
    FThornFog8 FogCapturePacked;
    int& ReverbMask;
    FThornReverb4& ReverbTarget;
    float ReverbCapture[4];
    float& FloorTarget;
    float FloorCapture;
    float FloorWrapLast;
    int Strid;
    FThornSubtitleEventDesc SubtitleDesc;
    FThornAttachEventDesc AttachDesc;
    void PackAttachDesc();
    bool bUserDescriptorOwner = false; // USER typed lifetime: 44be0/439a0 has no curve Release.
    int SoundStartTime;
    FThornSubSceneEventDesc SubSceneDesc;
    FThornSoundEventDesc SoundDesc;
    void PackSoundDesc();

    // START_MOTION's published descriptor (thorn_iface.h FThornMotionEventDesc): retail keeps
    // the common words and the motion fields contiguous at event +0x28 and hands that to
    // ICompoundControl slots 3 / 4 and IScriptEngine slots 15 / 16. Refreshed from Desc and
    // the named motion fields by PackMotionDesc before each of those uses.
    FThornMotionEventDesc MotionDesc;
    void PackMotionDesc();

    // START_IK's published descriptor (thorn_iface.h FThornStartIkDesc), the IK twin of
    // MotionDesc: retail FUN_06f43f40 lays the IK fields out after the common words at event
    // +0x28, with the event's own address at +0x2c, and IIKControl slots 3 / 4 / 5 and
    // IScriptEngine slots 15 / 16 receive that. Refreshed by PackIkDesc before each use.
    FThornStartIkDesc IkDesc;
    void PackIkDesc();

    // CONNECT_HARDPOINTS' published descriptor (thorn_iface.h FThornConnectEventDesc): retail
    // keeps the common words and the two hardpoint name pointers contiguous at event +0x28
    // (+0x54 / +0x58 = desc +0x2c / +0x30) and hands event +0x28 to ICompoundControl slot 7
    // (LAB_06f48340.., 0x06f48384) and slot 8 (LAB_06f483c0, 0x06f4840e). Refreshed by
    // PackConnectDesc before each use.
    FThornConnectEventDesc ConnectDesc;
    void PackConnectDesc();
};

class FThornScene
{
public:
    FThornScene();
    ~FThornScene();

    void Clear();
    FThornEntity* AddEntity();
    FThornEvent* AddEvent(int DescriptorStorageType = 0);
    void BindEntitySceneAxes(FThornEntity* Entity);
    void RegisterFrameEntity(FThornEntity* Entity);
    void BindEventSceneAxes(FThornEvent* Event);
    FThornCsys& ForwardCsys() { return SceneForwardCsys_; }
    void BuildInverseCsys(float* Out12) const;
    void MarkLoaded() { PlaybackState_ = kThornPlaybackLoaded; }
    void FinalizeLoad(int DurationMs);

    int FindEntityByName(const char* Name) const;
    FThornEntity** FindEntityEntry(int Index);
    FThornEntity* EntityAt(int Index);
    const FThornEntity* EntityAt(int Index) const;

    unsigned long EntityCount() const;
    unsigned long LiveEntityCount() const;
    void* EntityHandle(int Index);
    FThornEntitySnap* EntitySnap(int Index);
    FThornEntitySnap* EntitySnapInorder(int Index);
    unsigned long UserPropCount(int Index) const;
    const char* UserPropKey(int Index, int N) const;
    GENRESULT GetEntityPos(int Index, float* OutVec3);
    GENRESULT GetEntityOrient(int Index, float* OutMat9);
    GENRESULT Attach(int Index, void* EngineObj);
    GENRESULT Detach(int Index);
    void PushBoundGeoFromScene(const FTransform& OldInverse, bool bOrient);
    GENRESULT UserProp(void* Handle, const char* Key, char** Out);
    GENRESULT SetUserProp(int Index, const char* Key, const char* Value);
    GENRESULT AddEntityFromDesc(void* Desc, int* OutIndex);
    GENRESULT RemoveEntityAt(int Index);
    GENRESULT UpdateEntityFromDesc(int Index, void* Desc);
    GENRESULT AddEventFromDesc(const void* Desc, int* OutId);
    GENRESULT RemoveEventById(int Id);
    GENRESULT SaveToFileSystem(void* FileSystem, IThornComContext* ComContext,
                               unsigned int SavedDurationMs);

    unsigned long EventCount() const { return static_cast<unsigned long>(MasterCount_); }
    void* EventDescByIndex(int Index);
    void* EventDescById(int Id);
    bool HasEventType(int Type) const;

    int DurationMs() const { return DurationMs_; }
    void SetDuration(int Ms) { DurationMs_ = Ms; }
    int ClockMs() const { return ClockMs_; }
    int PlaybackState() const { return PlaybackState_; }
    unsigned char LoopFlag() const { return bLoop_; }
    // LAB_06f39430: MOV [ECX+0x30], AL — raw byte, not normalized 0/1.
    void SetLoop(unsigned char Value) { bLoop_ = Value; }

    GENRESULT StartPlayback(int Arg);
    GENRESULT StopPlayback();
    GENRESULT PausePlayback();
    GENRESULT Tick(int DtMs);

private:
    friend class FThornEntity;
    void GrowMaster();
    void GrowPending();
    void GrowActive();
    void RebuildPending();
    void StopAll();
    void DispatchWindow(int OldTime, int NewTime);
    struct FActiveGroup
    {
        std::vector<FThornEvent*> Events;
        bool bVisited = false;
    };
    void RebuildActiveGroups();
    void EmitActiveGroup(std::map<int, FActiveGroup>& Groups,
                         std::map<int, FActiveGroup>::iterator Group);
    void AppendActiveGroupEvent(FThornEvent* Event);
    void FrameUpdateEntities(int TimeMs);
    void UpdateActive(int TimeMs);
    void DestroyEntity(FThornEntity* Entity);
    void ApplyEntitySnap(FThornEntity* Entity, const FThornEntitySnap* Snap);
    bool EntityExists(int Index) const;
    void CompactPendingFrom(FThornEvent** SelectedBegin, FThornEvent** SelectedEnd);
    void DrainTimedStops(const int* TimeMs, bool bBeforeStartTime, std::set<int>* UpdatedTimes);
    FThornEvent** EraseMasterEventAt(FThornEvent** Slot);
    static int CompareEventStart(const void* Left, const void* Right);
    static bool EventAllowsMissingSecondary(int Type);

    std::map<int, FThornEntity*> Entities_;
    int EntityCount_;
    FThornEvent** Master_;
    int MasterCount_;
    int MasterCap_;

    FThornEvent** Pending_;
    int PendingCount_;
    int PendingCap_;
    int PendingBegin_;

    FThornEvent** Active_;
    int ActiveCount_;
    int ActiveCap_;

    std::optional<std::list<float>> StopTimes_;
    // Retail +f8/+fc: alias nodes; sentinel is destroyed before StopTimes_.
    std::optional<std::list<FThornEntity*>> FrameEntities_;

    int DurationMs_;
    int ClockMs_;
    int ClockAfterDt_;
    int PlaybackState_;
    unsigned char bLoop_;
    int LoopCarryMs_;
    unsigned int SceneUpAxis_;
    unsigned int SceneFrontAxis_;
    FThornCsys SceneForwardCsys_;
};

// FUN_06f41840 compact+D3D defaults; FUN_06f2eb20 convert when type not in mask.
void ThornInitLightEntityDefaults(FThornLightProps* Props);
void ThornConvertCompactLight(FThornLightProps* Blob);
void ThornCopyLightPropsToEntity(FThornEntity* Entity, const FThornLightProps* Props);
// Fill the published descriptor tail (Snap past +0x44) from the entity's native fields.
// Called once by the Lua entity loader, after every per-type field is read.
void ThornPublishEntityDescTail(FThornEntity* Entity);
