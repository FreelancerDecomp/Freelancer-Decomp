// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Every DACOM interface id used anywhere in the port, in one place.
// The strings are evidence from the retail binaries (QueryInterface / CreateInstance
// call sites and .rdata), never invented -- adding one means you read it out of a
// listing first. Before 2026-09-16 these lived in 203 scattered declarations under
// three naming schemes plus 201 inline literals, so a typo in one copy was a silent
// QI failure at runtime (docs/re/cross_dll_protocol.md).

namespace Iids
{
    // The DACOM version stamp every id carries; dacom.dll strips it before it
    // hashes a name (FCOManager::StripVersionPrefix).
    static const char Prefix[] = "1.11_";
    static const unsigned int PrefixLength = 5;

    static const char AggregateComponent[] = "1.11_IAggregateComponent";
    static const char Alchemy[] = "1.11_IAlchemy";
    static const char AnimatedSingle[] = "1.11_IAnimatedSingle";
    static const char AnimatedSingleRamp[] = "1.11_IAnimatedSingleRamp";
    static const char AnimatedTransform[] = "1.11_IAnimatedTransform";
    static const char Animation[] = "1.11_IAnimation";
    static const char Animation2[] = "1.11_IAnimation2";
    static const char BatchedMaterial[] = "1.11_IBatchedMaterial";
    static const char BigImage[] = "1.11_IBigImage";
    static const char BigImage2[] = "1.11_IBigImage2";
    static const char Camera[] = "1.11_ICamera";
    static const char CameraControl[] = "1.11_ICameraControl";
    static const char Channel[] = "1.11_IChannel";
    static const char ChannelArchetype[] = "1.11_ChannelArchetype";
    static const char COManager[] = "1.11_ICOManager";
    static const char ComponentFactory[] = "1.11_IComponentFactory";
    static const char CompoundControl[] = "1.11_ICompoundControl";
    static const char CRSpline[] = "1.11_ICRSpline";
    static const char D3DLightControl[] = "1.11_ID3DLightControl";
    static const char DAComponent[] = "1.11_IDAComponent";
    static const char DAConnectionPointContainer[] = "1.11_IDAConnectionPointContainer";
    static const char DataBuffer[] = "1.11_IDataBuffer";
    static const char Deformable[] = "1.11_IDeformable";
    static const char Engine[] = "1.11_IEngine";
    static const char Engine2[] = "1.11_IEngine2";
    static const char EngineComponent[] = "1.11_IEngineComponent";
    static const char FileSystem[] = "1.11_IFileSystem";
    static const char FLAppearance[] = "1.11_IFLAppearance";
    static const char FogControl[] = "1.11_IFogControl";
    static const char FxAppearance[] = "1.11_IFxAppearance";
    static const char FxEffect[] = "1.11_IFxEffect";
    static const char FxEffectArchetype[] = "1.11_IFxEffectArchetype";
    static const char FxEffectLibrary[] = "1.11_IFxEffectLibrary";
    static const char FxEmitter[] = "1.11_IFxEmitter";
    static const char FxField[] = "1.11_IFxField";
    static const char FxMesh[] = "1.11_IFxMesh";               // freelancer.exe .rdata 005d9958
    static const char FxMeshFactory[] = "1.11_IFxMeshFactory"; // freelancer.exe .rdata 005d9978
    static const char FxNode[] = "1.11_IFxNode";
    static const char FxNodeLibrary[] = "1.11_IFxNodeLibrary";
    static const char FxNodePropertySet[] = "1.11_IFxNodePropertySet";
    static const char FxRuntime[] = "1.11_IFxRuntime";
    static const char GammaControl[] = "1.11_IGammaControl";
    static const char GeoTransformControl[] = "1.11_IGeoTransformControl";
    static const char Hardpoint[] = "1.11_IHardpoint";
    static const char IKControl[] = "1.11_IIKControl";
    static const char LightControl[] = "1.11_ILightControl";
    static const char MatAnimLibrary[] = "1.11_IMatAnimLibrary";
    static const char Material[] = "1.11_IMaterial";
    static const char MaterialBatcher[] = "1.11_IMaterialBatcher";
    static const char MaterialBatcher2[] = "1.11_IMaterialBatcher2";
    static const char MaterialBatcherEvents[] = "1.11_IMaterialBatcherEvents"; // exe .rdata 0x005c924c
    static const char MaterialLibrary[] = "1.11_IMaterialLibrary";
    static const char MatHookUp[] = "1.11_IMatHookUp";
    static const char Mesh[] = "1.11_IMesh";
    static const char MotionPath[] = "1.11_IMotionPath";
    static const char ParamCurve[] = "1.11_IParamCurve";
    static const char PersistString[] = "1.11_IPersistString";
    static const char ProfileParser[] = "1.11_IProfileParser";
    static const char ProfileParser2[] = "1.11_IProfileParser2";
    static const char PSysControl[] = "1.11_IPSysControl";
    static const char RampColor[] = "1.11_IRampColor";
    static const char RampColorRamp[] = "1.11_IRampColorRamp";
    static const char RampSingle[] = "1.11_IRampSingle";
    static const char RampSingleRamp[] = "1.11_IRampSingleRamp";
    static const char RenderableMesh[] = "1.11_IRenderableMesh";
    static const char RenderComponent[] = "1.11_IRenderComponent";
    static const char Renderer[] = "1.11_IRenderer";
    static const char Renderer2[] = "1.11_IRenderer2";
    static const char RenderPipeline8B[] = "1.11_IRenderPipeline8B";
    static const char RegExp[] = "1.11_IRegExp"; // dacom .data 065b8418, QI table 065b71dc
    static const char ReverbControl[] = "1.11_IReverbControl";
    static const char RootData[] = "1.11_IRootData";
    static const char RPDraw[] = "1.11_IRPDraw";
    static const char RPIndexBuffer[] = "1.11_IRPIndexBuffer";
    static const char RPTexture[] = "1.11_IRPTexture";
    static const char RPVertexBuffer[] = "1.11_IRPVertexBuffer";
    static const char ScriptControl[] = "1.11_IScriptControl";
    static const char ScriptEngine[] = "1.11_IScriptEngine";
    static const char SearchPath[] = "1.11_ISearchPath";
    static const char SoundArchetype[] = "1.11_ISoundArchetype";
    static const char SoundControl[] = "1.11_ISoundControl";
    static const char SoundManager[] = "1.11_ISoundManager";
    static const char SoundManagerStatus[] = "1.11_ISoundManagerStatus";
    static const char SphereMesh[] = "1.11_ISphereMesh";
    static const char Streamer[] = "1.11_IStreamer";
    static const char Streamer2[] = "1.11_IStreamer2";
    static const char SubtitleControl[] = "1.11_ISubtitleControl";
    static const char SystemComponent[] = "1.11_ISystemComponent";
    static const char SystemContainer[] = "1.11_ISystemContainer";
    static const char TextureLibrary2[] = "1.11_ITextureLibrary2";
    static const char TextureLibraryA[] = "1.11_ITextureLibraryA";
    static const char TriMeshAnim[] = "1.11_ITriMeshAnim";
    static const char UserEventControl[] = "1.11_IUserEventControl";
    static const char VertexBufferManager[] = "1.11_IVertexBufferManager";
    static const char VMeshLibrary[] = "1.11_IVMeshLibrary";
    static const char VMeshWire[] = "1.11_IVMeshWire";

    // PORT-ONLY ids (no retail binary has them): the Direct3D 9 extensions of RP9.dll
    // (render/IRenderPipelineEx.h). RP8.dll does not answer them -- a failed QueryInterface
    // means "Direct3D 8, use the retail path".
    static const char RenderPipelineEx[] = "RP9_IRenderPipelineEx";
    static const char RPDrawEx[] = "RP9_IRPDrawEx";
}
