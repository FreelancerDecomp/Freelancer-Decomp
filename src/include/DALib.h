// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"

// Keep IEngine2 a distinct incomplete type so DALib::Engine2 mangles as PAUIEngine2
// (engbase.h would otherwise alias it to IEngine).
struct IEngine2;
#define FREELANCER_IENGINE2_DEFINED
#include "engine/engbase.h"
#include "render/IRenderPipeline.h"

// DALIB_API, FFileSystemAggDesc, FatalErrorFn / CriticalWarningFn and Startup / OpenData /
// CloseData / Shutdown (the part MFC translation units can include).
#include "dalib_core.h"

struct IRPDraw;
struct IRPIndexBuffer;
struct IRPVertexBuffer;
struct IRenderer2;
struct IFileSystem;
struct ISoundManager;
struct IStreamer2;
struct ITextureLibrary2;
struct IMaterialLibrary;
struct IMaterialBatcher2;
struct IVertexBufferManager;
struct IAlchemy;
struct IFxRuntime;
struct IFxEffectLibrary;
struct IMatAnimLibrary;
struct IVMeshLibrary;

namespace DALib
{
    // The DirectPlay8 network classes (CDPClient, CDPServer, CDPMsgList, CGunWrapper, ...) are
    // published in dalib_net.h; freelancer.exe calls CDPServer::CrashCleanup() and
    // CDPClient::SetSourcePort() directly, as retail does.

    DALIB_API extern ICOManager* Dacom;
    DALIB_API extern IEngine* Engine;
    DALIB_API extern IEngine2* Engine2;
    DALIB_API extern IRenderer2* Renderer;
    DALIB_API extern IAnimation2* Anim;
    DALIB_API extern IHardpoint* Hardpoint;
    DALIB_API extern IDeformable* Deform;
    DALIB_API extern IChannel* Channel;
    DALIB_API extern IRenderPipeline* Pipeline;
    DALIB_API extern IRPDraw* RPDraw;
    DALIB_API extern IRPIndexBuffer* RPIndexBuffer;
    DALIB_API extern IRPVertexBuffer* RPVertexBuffer;
    DALIB_API extern IFileSystem* Data;
    DALIB_API extern ISoundManager* Sound;
    DALIB_API extern IStreamer2* Streamer;
    DALIB_API extern ITextureLibrary2* TextureLib;
    DALIB_API extern IMaterialLibrary* MaterialLib;
    DALIB_API extern IMaterialBatcher2* MaterialBatcher;
    DALIB_API extern IVertexBufferManager* VertexBuffer;
    DALIB_API extern IAlchemy* Alchemy;
    DALIB_API extern IFxRuntime* FxRuntime;
    DALIB_API extern IFxEffectLibrary* FxEffectLibrary;
    DALIB_API extern IMatAnimLibrary* MatAnimLibrary;
    DALIB_API extern IVMeshLibrary* VMeshLibrary;
}
