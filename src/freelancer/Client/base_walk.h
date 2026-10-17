// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

struct FRoomRuntime;
struct FMouseEvent;
struct FCharacterObject;

// Optional gameplay feature in WalkModeTest. Movement/camera hooks are inert in classic mode.
void BaseWalkTick(float DeltaSeconds);
void BaseWalkAfterWorldUpdate();
void BaseWalkLeaveBase();
void BaseWalkRoomDestroyed(FRoomRuntime* Room);
void BaseWalkCharacterChanged(FCharacterObject* Character, bool bDestroying);
void BaseWalkCharacterScriptStarting(FCharacterObject* Character);
bool BaseWalkIsLooking();
void BaseWalkBeforeInteraction();
bool BaseWalkHandleKey(unsigned Message, unsigned Key);
bool BaseWalkConsumeMouseEvent(FMouseEvent const& Event);
bool BaseWalkMouseFrame(bool bRead, int DeltaX, int DeltaY, int CursorX, int CursorY);
void BaseWalkFocusLost();

// Keep base chrome visible while it fades, without making the hidden cursor/UI pickable.
bool BaseWalkShouldDrawInterface();
float BaseWalkGetInterfaceDrawOpacity();
unsigned long BaseWalkFadeInterfaceColor(unsigned long Argb, bool bAdditive = false);

// Limits opacity changes to Win3D's interface pass; world rendering and the cursor are excluded.
struct FBaseWalkInterfaceDraw
{
    float SavedOpacity;
    FBaseWalkInterfaceDraw();
    ~FBaseWalkInterfaceDraw();
    FBaseWalkInterfaceDraw(FBaseWalkInterfaceDraw const&) = delete;
    FBaseWalkInterfaceDraw& operator=(FBaseWalkInterfaceDraw const&) = delete;
};

struct FBaseWalkInterfaceAlpha
{
    float& Alpha;
    float SavedAlpha;
    explicit FBaseWalkInterfaceAlpha(float& InAlpha) : Alpha(InAlpha), SavedAlpha(InAlpha)
    {
        Alpha *= BaseWalkGetInterfaceDrawOpacity();
    }
    ~FBaseWalkInterfaceAlpha() { Alpha = SavedAlpha; }
    FBaseWalkInterfaceAlpha(FBaseWalkInterfaceAlpha const&) = delete;
    FBaseWalkInterfaceAlpha& operator=(FBaseWalkInterfaceAlpha const&) = delete;
};
