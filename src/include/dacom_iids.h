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
    static constexpr char Prefix[] = "1.11_";
    static constexpr unsigned int PrefixLength = 5;

    static constexpr char AggregateComponent[] = "1.11_IAggregateComponent";
    static constexpr char Alchemy[] = "1.11_IAlchemy";
    static constexpr char AnimatedSingle[] = "1.11_IAnimatedSingle";
    static constexpr char AnimatedSingleRamp[] = "1.11_IAnimatedSingleRamp";
    static constexpr char AnimatedTransform[] = "1.11_IAnimatedTransform";
    static constexpr char Animation[] = "1.11_IAnimation";
    static constexpr char Animation2[] = "1.11_IAnimation2";
    static constexpr char BatchedMaterial[] = "1.11_IBatchedMaterial";
    static constexpr char BigImage[] = "1.11_IBigImage";
    static constexpr char BigImage2[] = "1.11_IBigImage2";
    static constexpr char Camera[] = "1.11_ICamera";
    static constexpr char CameraControl[] = "1.11_ICameraControl";
    static constexpr char Channel[] = "1.11_IChannel";
    static constexpr char ChannelArchetype[] = "1.11_ChannelArchetype";
    static constexpr char COManager[] = "1.11_ICOManager";
    static constexpr char ComponentFactory[] = "1.11_IComponentFactory";
    static constexpr char CompoundControl[] = "1.11_ICompoundControl";
    static constexpr char CRSpline[] = "1.11_ICRSpline";
    static constexpr char D3DLightControl[] = "1.11_ID3DLightControl";
    static constexpr char DAComponent[] = "1.11_IDAComponent";
    static constexpr char DAConnectionPointContainer[] = "1.11_IDAConnectionPointContainer";
    static constexpr char DataBuffer[] = "1.11_IDataBuffer";
    static constexpr char Deformable[] = "1.11_IDeformable";
    static constexpr char Engine[] = "1.11_IEngine";
    static constexpr char Engine2[] = "1.11_IEngine2";
    static constexpr char EngineComponent[] = "1.11_IEngineComponent";
    static constexpr char FileSystem[] = "1.11_IFileSystem";
    static constexpr char FLAppearance[] = "1.11_IFLAppearance";
    static constexpr char FogControl[] = "1.11_IFogControl";
    static constexpr char FxAppearance[] = "1.11_IFxAppearance";
    static constexpr char FxEffect[] = "1.11_IFxEffect";
    static constexpr char FxEffectArchetype[] = "1.11_IFxEffectArchetype";
    static constexpr char FxEffectLibrary[] = "1.11_IFxEffectLibrary";
    static constexpr char FxEmitter[] = "1.11_IFxEmitter";
    static constexpr char FxField[] = "1.11_IFxField";
    static constexpr char FxMesh[] = "1.11_IFxMesh";               // freelancer.exe .rdata 005d9958
    static constexpr char FxMeshFactory[] = "1.11_IFxMeshFactory"; // freelancer.exe .rdata 005d9978
    static constexpr char FxNode[] = "1.11_IFxNode";
    static constexpr char FxNodeLibrary[] = "1.11_IFxNodeLibrary";
    static constexpr char FxNodePropertySet[] = "1.11_IFxNodePropertySet";
    static constexpr char FxRuntime[] = "1.11_IFxRuntime";
    static constexpr char GammaControl[] = "1.11_IGammaControl";
    static constexpr char GeoTransformControl[] = "1.11_IGeoTransformControl";
    static constexpr char Hardpoint[] = "1.11_IHardpoint";
    static constexpr char IKControl[] = "1.11_IIKControl";
    static constexpr char LightControl[] = "1.11_ILightControl";
    static constexpr char MatAnimLibrary[] = "1.11_IMatAnimLibrary";
    static constexpr char Material[] = "1.11_IMaterial";
    static constexpr char MaterialBatcher[] = "1.11_IMaterialBatcher";
    static constexpr char MaterialBatcher2[] = "1.11_IMaterialBatcher2";
    static constexpr char MaterialBatcherEvents[] = "1.11_IMaterialBatcherEvents"; // exe .rdata 0x005c924c
    static constexpr char MaterialLibrary[] = "1.11_IMaterialLibrary";
    static constexpr char MatHookUp[] = "1.11_IMatHookUp";
    static constexpr char Mesh[] = "1.11_IMesh";
    static constexpr char MotionPath[] = "1.11_IMotionPath";
    static constexpr char ParamCurve[] = "1.11_IParamCurve";
    static constexpr char PersistString[] = "1.11_IPersistString";
    static constexpr char ProfileParser[] = "1.11_IProfileParser";
    static constexpr char ProfileParser2[] = "1.11_IProfileParser2";
    static constexpr char PSysControl[] = "1.11_IPSysControl";
    static constexpr char RampColor[] = "1.11_IRampColor";
    static constexpr char RampColorRamp[] = "1.11_IRampColorRamp";
    static constexpr char RampSingle[] = "1.11_IRampSingle";
    static constexpr char RampSingleRamp[] = "1.11_IRampSingleRamp";
    static constexpr char RenderableMesh[] = "1.11_IRenderableMesh";
    static constexpr char RenderComponent[] = "1.11_IRenderComponent";
    static constexpr char Renderer[] = "1.11_IRenderer";
    static constexpr char Renderer2[] = "1.11_IRenderer2";
    static constexpr char RenderPipeline8B[] = "1.11_IRenderPipeline8B";
    static constexpr char RegExp[] = "1.11_IRegExp"; // dacom .data 065b8418, QI table 065b71dc
    static constexpr char ReverbControl[] = "1.11_IReverbControl";
    static constexpr char RootData[] = "1.11_IRootData";
    static constexpr char RPDraw[] = "1.11_IRPDraw";
    static constexpr char RPIndexBuffer[] = "1.11_IRPIndexBuffer";
    static constexpr char RPTexture[] = "1.11_IRPTexture";
    static constexpr char RPVertexBuffer[] = "1.11_IRPVertexBuffer";
    static constexpr char ScriptControl[] = "1.11_IScriptControl";
    static constexpr char ScriptEngine[] = "1.11_IScriptEngine";
    static constexpr char SearchPath[] = "1.11_ISearchPath";
    static constexpr char SoundArchetype[] = "1.11_ISoundArchetype";
    static constexpr char SoundControl[] = "1.11_ISoundControl";
    static constexpr char SoundManager[] = "1.11_ISoundManager";
    static constexpr char SoundManagerStatus[] = "1.11_ISoundManagerStatus";
    static constexpr char SphereMesh[] = "1.11_ISphereMesh";
    static constexpr char Streamer[] = "1.11_IStreamer";
    static constexpr char Streamer2[] = "1.11_IStreamer2";
    static constexpr char SubtitleControl[] = "1.11_ISubtitleControl";
    static constexpr char SystemComponent[] = "1.11_ISystemComponent";
    static constexpr char SystemContainer[] = "1.11_ISystemContainer";
    static constexpr char TextureLibrary2[] = "1.11_ITextureLibrary2";
    static constexpr char TextureLibraryA[] = "1.11_ITextureLibraryA";
    static constexpr char TriMeshAnim[] = "1.11_ITriMeshAnim";
    static constexpr char UserEventControl[] = "1.11_IUserEventControl";
    static constexpr char VertexBufferManager[] = "1.11_IVertexBufferManager";
    static constexpr char VMeshLibrary[] = "1.11_IVMeshLibrary";
    static constexpr char VMeshWire[] = "1.11_IVMeshWire";

    // PORT(rp9): ids no retail binary has: the Direct3D 9 extensions of RP9.dll
    // (render/IRenderPipelineEx.h). RP8.dll does not answer them -- a failed QueryInterface
    // means "Direct3D 8, use the retail path".
    static constexpr char RenderPipelineEx[] = "RP9_IRenderPipelineEx";
    static constexpr char RPDrawEx[] = "RP9_IRPDrawEx";
}
