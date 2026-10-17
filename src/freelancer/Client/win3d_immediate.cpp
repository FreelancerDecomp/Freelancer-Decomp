// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The OpenGL immediate-mode emulator (retail `E:\FL\Scratch\Source\Client\cmn\OpenGL.cpp`):
// FUN_00421670 glBegin, FUN_00421cc0 glVertex3f, FUN_00421bd0 glColor4f, FUN_00421a70 glColor3ub,
// FUN_00421690 glEnd, FUN_00421290 the texture-stage apply, and the state helpers FUN_004220c0,
// FUN_00422190, FUN_00422610 and FUN_004225e0; FUN_00421ad0 glColor3f; and at the end of the
// file the fuzzy-line drawers FUN_00423be0 / FUN_004231f0 with their vector helpers.

#include "win3d_immediate.h"
#ifdef FREELANCER_WALK_MODE
#include "base_walk.h"
#endif

#include "game_options.h"
#include "space_camera.h"
#include "star_sphere.h"      // Win3DBindModelview (FUN_00422690)
#include "engine_light.h"     // ScaleVec3 (FUN_00411d60)
#include "win3d.h"            // Win3DTransformByMatrix (FUN_00423b90), DAT_006679d8

#include "common_misc.h"      // RandomVector
#include "DALib.h"
#include "dacom.h"
#include "globals.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"   // ITextureLibrary2::GetTextureFrame
#include "x86math.h"

#include <math.h>
#include <string.h>

namespace
{
    constexpr D3DRENDERSTATETYPE kRsZEnable = D3DRS_ZENABLE;
    constexpr D3DRENDERSTATETYPE kRsZWriteEnable = D3DRS_ZWRITEENABLE;
    constexpr D3DRENDERSTATETYPE kRsSrcBlend = D3DRS_SRCBLEND;
    constexpr D3DRENDERSTATETYPE kRsDestBlend = D3DRS_DESTBLEND;
    constexpr D3DRENDERSTATETYPE kRsZFunc = D3DRS_ZFUNC;
    constexpr D3DRENDERSTATETYPE kRsCullMode = D3DRS_CULLMODE;
    constexpr D3DRENDERSTATETYPE kRsAlphaBlendEnable = D3DRS_ALPHABLENDENABLE;
    constexpr D3DRENDERSTATETYPE kRsClipping = D3DRS_CLIPPING;
    constexpr D3DRENDERSTATETYPE kRsLighting = D3DRS_LIGHTING;
    constexpr D3DRENDERSTATETYPE kRsColorVertex = D3DRS_COLORVERTEX;
    constexpr D3DRENDERSTATETYPE kRsDiffuseMaterialSource = D3DRS_DIFFUSEMATERIALSOURCE;

    // D3DTSS_*, the six the two tables below drive.
    enum EWin3DStageState
    {
        kTssColorOp   = 1,
        kTssColorArg1 = 2,
        kTssColorArg2 = 3,
        kTssAlphaOp   = 4,
        kTssAlphaArg1 = 5,
        kTssAlphaArg2 = 6
    };

    // .rdata 0x005c96c0 -- what a textured stage is set to: MODULATE of the texture against the
    // vertex diffuse, on both colour and alpha.
    const int kStageTextured[6] = {4, 2, 0, 4, 2, 0};

    // .rdata 0x005c96f8 -- and what an untextured stage is set to: DISABLE.
    const int kStageDisabled[6] = {1, 2, 1, 1, 2, 1};

    // 0x00421741: the FVF the pipeline is handed. XYZ | DIFFUSE | TEX1.
    const unsigned kWin3DFvf = 0x142;

    // DAT_006168a4 / DAT_006168b0: both buffers are 0x3000 entries. Retail keeps them as globals,
    // but their only writers (the static initializer 0x004211ef / 0x00421200 and FUN_004213d0)
    // store 0x3000 and their only readers are glVertex's bounds tests (0x00421cc6 / 0x00421cd3),
    // so a constant is the same value.
    const int kMaxVertices = 0x3000;
    const int kMaxIndices = 0x3000;

    // 0x00421904: the quad path caps the index count it will triangulate.
    const unsigned kMaxQuadIndices = 0xaaa;

    // The accumulator. Retail's vertex buffer is .data 0x006168b8 with a 0x18 stride, and the
    // index buffer is .data 0x0065e8b8, sixteen bits per entry.
    struct FWin3DVertex
    {
        float         X;
        float         Y;
        float         Z;
        unsigned long Diffuse;
        float         U;
        float         V;
    };
    static_assert(sizeof(FWin3DVertex) == 0x18, "the FVF 0x142 vertex is 24 bytes");

    FWin3DVertex   G_Vertices[kMaxVertices];
    unsigned short G_Indices[kMaxIndices];

    // 0x00616888 / 0x006168a8 / 0x006168b4 / 0x006168ac.
    int           G_PrimitiveMode = -1;
    int           G_VertexCount = 0;
    int           G_IndexCount = 0;
    unsigned char G_bBufferLocked = 0;

    // 0x00616898 / 0x0061689c / 0x006168a0 -- the current vertex attributes. DAT_00616898 is 0 in
    // .data and neither the static initializer (LAB_00421160) nor FUN_004213d0 writes it; only
    // the colour setters do.
    unsigned long G_CurrentColour = 0;
    float         G_CurrentU = 0.0f;
    float         G_CurrentV = 0.0f;

    // 0x006648d0 / 0x006648d4 -- whether a texture is bound, and which.
    unsigned char G_bTextureBound = 0;
    void*         G_BoundTexture = nullptr;

    // 0x006648d8 -- the lighting flag glEnd installs while it draws.
    unsigned char G_bLighting = 0;

    // 0x006648b8 / 0x006648bc / 0x006648c0 / 0x006648c4 / 0x006648c8 / 0x006648cc -- the cached
    // pieces of render state the two setters below re-apply alongside the one they change.
    // Initial values from the static initializer LAB_00421160 (0x00421210..0x0042124b), the same
    // ones Win3DResetState (FUN_004213d0) restores.
    int           G_AlphaBlendEnable = 0;
    int           G_SrcBlend = 2;
    int           G_DestBlend = 2;
    unsigned char G_bDepthTest = 1;
    int           G_DepthWrite = 1;
    int           G_DepthFunc = 4;

    // 0x0042227b: the pipeline-state id the projection setter asks about. Non-zero means the
    // pipeline wants one combined modelview rather than a separate world and view.
    const int kPipelineStateViewspace = 7;

    // 0x004222f0: the value the projection kind is compared against and set to. Only one kind is
    // ever stored here by this function, so it doubles as "an ortho projection is installed".
    const int kProjectionKindOrtho = 2;

    const int kProjectionParamCount = 6;

    // 0x00610844 -- and it is 1 in .data, so the cache is on out of the box.
    unsigned char G_bProjectionCacheEnabled = 1;

    // 0x0066490c / 0x00664910 / 0x00664990 / 0x0066498c.
    int   G_ProjectionKind = 0;
    float G_ProjectionParams[kProjectionParamCount] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    int   G_ProjectionSetCount = 0;
    int   G_ProjectionSkipCount = 0;

    // 0x00422851: the perspective setter's own cache kind, sharing DAT_0066490c/DAT_00664910
    // with the ortho setter but only ever comparing/storing the first four slots.
    const int kProjectionKindPerspective = 1;

    // 0x00664928 / 0x0066492c / 0x00664948 -- Win3DPushProjection's shadow copy. DAT_00664928 is
    // the saved cache kind (0 means "nothing saved" / "matrix path"), the six floats from
    // DAT_0066492c are the saved cache params, and the 16 floats at DAT_00664948 are the raw
    // projection matrix saved when the cache is off.
    int   G_SavedProjectionKind = 0;
    float G_SavedProjectionParams[kProjectionParamCount] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float G_SavedProjectionMatrix[16] = {};

    // 0x00664994 / 0x00664998 / 0x0066499c / 0x006649a0 -- the last viewport Win3DSetViewport
    // installed, kept only so Win3DGetViewport can answer a GL_VIEWPORT query. The stored Y is
    // the flipped origin Win3DSetViewport's own comment describes, not Top itself -- that is what
    // retail stored and what retail hands back.
    int G_ViewportLeft = 0;
    int G_ViewportFlippedTop = 0;
    int G_ViewportWidth = 0;
    int G_ViewportHeight = 0;

    // 0x00422390: the only GL_VIEWPORT-style query id retail's glGetIntegerv understood.
    const int kWin3DQueryViewport = 0xba2;

    // DALib::Pipeline, never tested: no function in OpenGL.cpp (0x00421160..0x00423000) checks
    // the pipeline for null before calling through it -- a raw scan of that range finds no
    // TEST/CMP on a value loaded from DALib::Pipeline. The guards this module used to carry at
    // every call were port-only and are gone (exe-w4p3, 2026-09-30).
    IRenderPipeline* GetPipeline()
    {
        return DALib::Pipeline;
    }

    void ApplyStageTable(IRenderPipeline* Pipe, int Stage, const int* Table)
    {
        // Both retail loops walk a {state, value} table whose terminator is a zero STATE, and
        // both start from state 1 -- so the six entries are states 1..6 in order.
        for (int I = 0; I < 6; ++I)
            Pipe->SetTextureStageState(Stage, kTssColorOp + I, Table[I]);
    }

    // FUN_00421290. Decides what the two texture stages should be doing, given whether a texture
    // is bound and whether the pipeline still recognises it.
    void ApplyTextureStages()
    {
        IRenderPipeline* const Pipe = GetPipeline();

        if (G_bTextureBound == 0)
        {
            // 0x004212a4: nothing bound -- clear stage 0's texture and disable it.
            Pipe->SetTextureStageTexture(0, nullptr);
            ApplyStageTable(Pipe, 0, kStageDisabled);
            return;
        }

        // 0x004212cd: the handle is checked against the pipeline before it is used. A handle it
        // no longer knows leaves the stage disabled, which is what stops a freed texture from
        // being drawn with.
        const GENRESULT Known = Pipe->IsTexture(G_BoundTexture);
        if (Known != GR_OK)
        {
            Pipe->SetTextureStageTexture(0, nullptr);
            ApplyStageTable(Pipe, 0, kStageDisabled);
            // 0x0042138b: and the stale handle is dropped -- DAT_006648d4 takes EAX, which the
            // table loop left at its zero terminator. The bound flag (DAT_006648d0) stays set.
            G_BoundTexture = nullptr;
            return;
        }

        Pipe->SetTextureStageTexture(0, G_BoundTexture);

        // 0x004212f4: and stage 0's ARG2 is forced to zero before the table runs -- retail
        // writes state 0xb (D3DTSS_TEXTURETRANSFORMFLAGS) with 0 first.
        Pipe->SetTextureStageState(0, 0xb, 0);
        ApplyStageTable(Pipe, 0, kStageTextured);

        // 0x00421320: stage ONE is always disabled, textured or not.
        ApplyStageTable(Pipe, 1, kStageDisabled);
    }

    // DAT_005c75d8.
    const float kColourByteScale = 255.0f;

    int ScaleClampToByte(float Component)
    {
        // 0x00421bd0 (and 0x00421ad0 / 0x004214d0): FLD float component, FMUL float 255.0f --
        // the product stays on the FPU -- then ftol, which truncates toward zero, and the clamp
        // afterwards (TEST/JGE to 0, CMP 0xff/JLE), so the clamp catches what the conversion
        // produced, not what it was given.
        const double Scaled = static_cast<double>(Component) * static_cast<double>(kColourByteScale);
        int Value = static_cast<int>(Scaled);
        if (Value < 0)
            Value = 0;
        else if (Value > 0xff)
            Value = 0xff;
        return Value;
    }
}

float GWin3DModelview[12] = {1.0f, 0.0f, 0.0f,
                             0.0f, 1.0f, 0.0f,
                             0.0f, 0.0f, 1.0f,
                             0.0f, 0.0f, 0.0f};

void Win3DResetState()
{
    // FUN_004213d0 (cdecl; RenderMgrCreateBuffers pushes the surface width and height at
    // 0x00424bdd, neither is read). Listing 0x004213d0..0x00421491, the same stores as the static
    // initializer LAB_00421160. The two buffer capacities (0x3000, DAT_006168a4 / DAT_006168b0)
    // are constants here -- see kMaxVertices.
    G_SrcBlend = 2;                  // DAT_006648bc
    G_DestBlend = 2;                 // DAT_006648c0
    G_PrimitiveMode = -1;            // DAT_00616888
    G_VertexCount = 0;               // DAT_006168a8
    G_bBufferLocked = 0;             // DAT_006168ac
    G_IndexCount = 0;                // DAT_006168b4
    // DAT_006648dc: the 4x3 modelview -- diagonal 1.0, everything else 0.
    GWin3DModelview[0] = 1.0f; GWin3DModelview[4] = 1.0f; GWin3DModelview[8] = 1.0f;
    GWin3DModelview[7] = 0.0f; GWin3DModelview[6] = 0.0f; GWin3DModelview[5] = 0.0f;
    GWin3DModelview[3] = 0.0f; GWin3DModelview[2] = 0.0f; GWin3DModelview[1] = 0.0f;
    GWin3DModelview[11] = 0.0f; GWin3DModelview[10] = 0.0f; GWin3DModelview[9] = 0.0f;
    G_AlphaBlendEnable = 0;          // DAT_006648b8
    G_bDepthTest = 1;                // DAT_006648c4
    G_DepthWrite = 1;                // DAT_006648c8
    G_DepthFunc = 4;                 // DAT_006648cc
    G_bTextureBound = 0;             // DAT_006648d0
    G_BoundTexture = nullptr;        // DAT_006648d4
    G_bLighting = 0;                 // DAT_006648d8
    G_ProjectionKind = 0;            // DAT_0066490c
    G_SavedProjectionKind = 0;       // DAT_00664928
}

void Win3DBegin(int PrimitiveMode)
{
    // FUN_00421670.
    G_PrimitiveMode = PrimitiveMode;
    G_IndexCount = 0;

    // 0x00421681: the vertex counter survives a begin while the buffer is locked. That is what
    // lets a caller build one vertex pool and issue several index runs against it.
    if (G_bBufferLocked == 0)
        G_VertexCount = 0;
}

void Win3DVertex3f(float X, float Y, float Z)
{
    // FUN_00421cc0. Both counters are checked (CMP / JNC against the 0x3000 caps), so the last
    // usable slot is the cap minus one.
    if (G_IndexCount >= kMaxIndices || G_VertexCount >= kMaxVertices)
    {
        // 0x00421d35..0x00421db0: the vertex is dropped, and a function-static rate limiter
        // (guard bit DAT_00616880, last time DAT_00616878 = 0.0f, interval DAT_0061687c = 1.0f,
        // its empty destructor registered through atexit) warns at most once per interval of
        // client time (FUN_0042d660): FSUBR now - last, FCOMP interval, TEST AH,0x41 / JNZ --
        // only a strictly greater gap warns (less, equal and unordered skip). The warning
        // stores now into the last time first.
        static float LastWarnTime = 0.0f;
        static float WarnInterval = 1.0f;
        const double Now = GetClientElapsedTime();
        if (Now - static_cast<double>(LastWarnTime) > static_cast<double>(WarnInterval))
        {
            LastWarnTime = static_cast<float>(Now);
            FDUMP(0x100002, "%s(%d) : *** WARNING: GL: %s\n",
                  "E:\\FL\\Scratch\\Source\\Client\\cmn\\OpenGL.cpp", 0x59, "glVertex overflow");
        }
        return;
    }

    // 0x00421ce2: the index is written first and points at the vertex about to be appended.
    G_Indices[G_IndexCount] = static_cast<unsigned short>(G_VertexCount);
    ++G_IndexCount;

    FWin3DVertex& Vertex = G_Vertices[G_VertexCount];
    ++G_VertexCount;

    Vertex.X = X;
    Vertex.Y = Y;
    Vertex.Z = Z;
#ifdef FREELANCER_WALK_MODE
    Vertex.Diffuse = BaseWalkFadeInterfaceColor(G_CurrentColour);
#else
    Vertex.Diffuse = G_CurrentColour;
#endif
    Vertex.U = G_CurrentU;
    Vertex.V = G_CurrentV;
}

void Win3DColor4f(float R, float G, float B, float A)
{
    // FUN_00421bd0. Retail scales by 255 and packs ARGB.
    const int Red = ScaleClampToByte(R);
    const int Green = ScaleClampToByte(G);
    const int Blue = ScaleClampToByte(B);
    const int Alpha = ScaleClampToByte(A);

    G_CurrentColour = (static_cast<unsigned long>(Alpha) << 24) |
                      (static_cast<unsigned long>(Red) << 16) |
                      (static_cast<unsigned long>(Green) << 8) |
                      static_cast<unsigned long>(Blue);
}

void Win3DColor3f(float R, float G, float B)
{
    // FUN_00421ad0: each component times 255.0f (DAT_005c75d8), ftol, then the clamp -- red into
    // the argument slot, green into EBX, blue into EAX -- and `((R | 0xffffff00) << 8 | G) << 8 | B`
    // into DAT_00616898, so alpha is 0xff.
    const int Red = ScaleClampToByte(R);
    const int Green = ScaleClampToByte(G);
    const int Blue = ScaleClampToByte(B);

    G_CurrentColour = 0xff000000ul |
                      (static_cast<unsigned long>(Red) << 16) |
                      (static_cast<unsigned long>(Green) << 8) |
                      static_cast<unsigned long>(Blue);
}

void Win3DSetPipelineClearColor(float R, float G, float B, float A)
{
    // FUN_004214d0: same scale-clamp-pack as Win3DColor4f (0x00421bd0), but the packed value goes
    // straight to the pipeline's SetPipelineState(8, ...) instead of the immediate-mode colour.
    IRenderPipeline* const Pipe = GetPipeline();

    const int Red = ScaleClampToByte(R);
    const int Green = ScaleClampToByte(G);
    const int Blue = ScaleClampToByte(B);
    const int Alpha = ScaleClampToByte(A);

    const unsigned long Packed = (static_cast<unsigned long>(Alpha) << 24) |
                                  (static_cast<unsigned long>(Red) << 16) |
                                  (static_cast<unsigned long>(Green) << 8) |
                                  static_cast<unsigned long>(Blue);

    const int kPipelineStateClearColor = 8;
    Pipe->SetPipelineState(kPipelineStateClearColor, static_cast<int>(Packed));
}

void Win3DColor3ub(unsigned char R, unsigned char G, unsigned char B)
{
    // FUN_00421a70: `((R | 0xffffff00) << 8 | G) << 8 | B`, which is the alpha byte left at 0xff
    // with the three components shifted in under it.
    G_CurrentColour = ((static_cast<unsigned long>(R) | 0xffffff00u) << 8 |
                       static_cast<unsigned long>(G)) << 8 |
                      static_cast<unsigned long>(B);
}

void Win3DColor3ubv(const unsigned char* Rgb)
{
    // FUN_00421aa0 (.text:00421aa0-00421ac5): MOVZX the three bytes, `((R | 0xffffff00) << 8 |
    // G) << 8 | B` into DAT_00616898.
    G_CurrentColour = ((static_cast<unsigned long>(Rgb[0]) | 0xffffff00u) << 8 |
                       static_cast<unsigned long>(Rgb[1])) << 8 |
                      static_cast<unsigned long>(Rgb[2]);
}

void Win3DColor4ub(unsigned char R, unsigned char G, unsigned char B, unsigned char A)
{
    // FUN_00421b70. The same ARGB pack as Win3DColor4f, but the four components arrive as bytes
    // already -- no scale by 255, no clamp.
    G_CurrentColour = (static_cast<unsigned long>(A) << 24) |
                      (static_cast<unsigned long>(R) << 16) |
                      (static_cast<unsigned long>(G) << 8) |
                      static_cast<unsigned long>(B);
}

void Win3DColor4ubv(const unsigned char* Rgba)
{
    // FUN_00421ba0. The pointer form of Win3DColor4ub: Rgba[0]=R, Rgba[1]=G, Rgba[2]=B, Rgba[3]=A,
    // packed the same way.
    G_CurrentColour = (static_cast<unsigned long>(Rgba[3]) << 24) |
                      (static_cast<unsigned long>(Rgba[0]) << 16) |
                      (static_cast<unsigned long>(Rgba[1]) << 8) |
                      static_cast<unsigned long>(Rgba[2]);
}

void Win3DTexCoord2f(float U, float V)
{
    // FUN_00421ca0.
    G_CurrentU = U;
    G_CurrentV = V;
}

void Win3DBindTexture(void* TextureHandle)
{
    // FUN_004220a0.
    G_BoundTexture = TextureHandle;
    G_bTextureBound = 1;
}

void Win3DEnable(int Capability)
{
    // FUN_00421dd0.
    IRenderPipeline* const Pipe = GetPipeline();

    switch (Capability)
    {
    case kWin3DDepthTest:
        // 0x00421ddf: the flag is raised and then all three depth states are written, the
        // other two from the cache. Retail even branches on the flag it has just set and takes
        // the same path either way -- a leftover the port simply does not reproduce.
        G_bDepthTest = 1;
        Pipe->SetRenderState(kRsZFunc, G_DepthFunc);
        Pipe->SetRenderState(kRsZEnable, 1);
        Pipe->SetRenderState(kRsZWriteEnable, G_DepthWrite);
        return;

    case kWin3DCullFace:
        // 0x00421e2a: D3DCULL_CCW. glDisable writes 1 (NONE) into the same state.
        Pipe->SetRenderState(kRsCullMode, 2);
        return;

    case kWin3DLightingCap:
        // 0x00421e42: no pipeline call at all -- the flag is only read by glEnd.
        G_bLighting = 1;
        return;

    case kWin3DBlend:
        G_AlphaBlendEnable = 1;
        Pipe->SetRenderState(kRsAlphaBlendEnable, 1);
        return;

    case kWin3DTexture2D:
        // 0x00421e7c: likewise a flag only; the stage apply in glEnd is what acts on it.
        G_bTextureBound = 1;
        return;

    default:
        // 0x00421e9b: CALL [FDUMP] through the import, no test of the pointer.
        FDUMP(0x100002, "%s(%d) : *** WARNING: GL: %s\n",
              "E:\\FL\\Scratch\\Source\\Client\\cmn\\OpenGL.cpp", 0x287, "glEnable - invalid");
        return;
    }
}

void Win3DDisable(int Capability)
{
    // FUN_00421ed0, the mirror image.
    IRenderPipeline* const Pipe = GetPipeline();

    switch (Capability)
    {
    case kWin3DDepthTest:
        G_bDepthTest = 0;
        // The ZFUNC and ZWRITE writes still happen, from the cache -- disabling the test
        // does not disable the write.
        Pipe->SetRenderState(kRsZFunc, G_DepthFunc);
        Pipe->SetRenderState(kRsZEnable, 0);
        Pipe->SetRenderState(kRsZWriteEnable, G_DepthWrite);
        return;

    case kWin3DCullFace:
        Pipe->SetRenderState(kRsCullMode, 1);   // D3DCULL_NONE
        return;

    case kWin3DLightingCap:
        G_bLighting = 0;
        return;

    case kWin3DFog:
        // 0x00421f02..0x00421f97: glDisable(GL_FOG) zeroes seven render states in this order --
        // 0x1c (FOGENABLE), 0x23, 0x8c, 0x22, 0x24, 0x25, 0x26. glEnable has no fog case
        // (FUN_00421dd0 sends 0xb60 to its warning).
        Pipe->SetRenderState(D3DRS_FOGENABLE, 0);
        Pipe->SetRenderState(D3DRS_FOGTABLEMODE, 0);
        Pipe->SetRenderState(D3DRS_FOGVERTEXMODE, 0);
        Pipe->SetRenderState(D3DRS_FOGCOLOR, 0);
        Pipe->SetRenderState(D3DRS_FOGSTART, 0);
        Pipe->SetRenderState(D3DRS_FOGEND, 0);
        Pipe->SetRenderState(D3DRS_FOGDENSITY, 0);
        return;

    case kWin3DBlend:
        G_AlphaBlendEnable = 0;
        Pipe->SetRenderState(kRsAlphaBlendEnable, 0);
        return;

    case kWin3DTexture2D:
        G_bTextureBound = 0;
        Pipe->SetTextureStageTexture(0, nullptr);
        return;

    default:
        // 0x00422030..: CALL [FDUMP] through the import, no test of the pointer.
        FDUMP(0x100002, "%s(%d) : *** WARNING: GL: %s\n",
              "E:\\FL\\Scratch\\Source\\Client\\cmn\\OpenGL.cpp", 0x2bf, "glDisable");
        return;
    }
}

void* Win3DSetTextureFilterLinear(void* PassThrough)
{
    // FUN_004223d0. Stage 0's MAG/MIN filter, both to D3DTEXF_LINEAR (3).
    IRenderPipeline* const Pipe = GetPipeline();
    const int kTssMagFilter = 0xd;
    const int kTssMinFilter = 0xe;
    const int kTexfLinear = 3;
    Pipe->SetTextureStageState(0, kTssMagFilter, kTexfLinear);
    Pipe->SetTextureStageState(0, kTssMinFilter, kTexfLinear);
    return PassThrough;
}

void Win3DSetTextureFilterPoint()
{
    // FUN_00422410. The mirror image, D3DTEXF_POINT (1).
    IRenderPipeline* const Pipe = GetPipeline();
    const int kTssMagFilter = 0xd;
    const int kTssMinFilter = 0xe;
    const int kTexfPoint = 1;
    Pipe->SetTextureStageState(0, kTssMagFilter, kTexfPoint);
    Pipe->SetTextureStageState(0, kTssMinFilter, kTexfPoint);
}

void Win3DSetDepthFunc(int CompareFunc)
{
    // FUN_00422120: the same three-write shape as everything else in this group.
    G_DepthFunc = CompareFunc;

    IRenderPipeline* const Pipe = GetPipeline();

    Pipe->SetRenderState(kRsZFunc, CompareFunc);
    Pipe->SetRenderState(kRsZEnable, G_bDepthTest != 0 ? 1 : 0);
    Pipe->SetRenderState(kRsZWriteEnable, G_DepthWrite);
}
void Win3DSetBlendFunc(int SourceFactor, int DestFactor)
{
    // FUN_004220c0. Note it re-applies the alpha-blend enable from the cache as well, so a
    // blend-func change cannot leave blending in a state the cache disagrees with.
    G_SrcBlend = SourceFactor;
    G_DestBlend = DestFactor;

    IRenderPipeline* const Pipe = GetPipeline();

    Pipe->SetRenderState(kRsSrcBlend, SourceFactor);
    Pipe->SetRenderState(kRsDestBlend, G_DestBlend);
    Pipe->SetRenderState(kRsAlphaBlendEnable, G_AlphaBlendEnable);
}

void Win3DSetDepthWrite(int bEnabled)
{
    // FUN_00422190, the same shape: the one field it owns, then two re-applies.
    G_DepthWrite = bEnabled;

    IRenderPipeline* const Pipe = GetPipeline();

    Pipe->SetRenderState(kRsZFunc, G_DepthFunc);
    Pipe->SetRenderState(kRsZEnable, G_bDepthTest != 0 ? 1 : 0);
    Pipe->SetRenderState(kRsZWriteEnable, G_DepthWrite);
}

void Win3DSetViewport(ICamera* Cam, const char* SourceFile, int SourceLine)
{
    // FUN_00422610. The file and line are retail's diagnostic pair; nothing in the ported path
    // consumes them, and they are kept in the signature because every call site passes them.
    (void)SourceFile;
    (void)SourceLine;

    // 0x00422653: DALib::Pipeline is read and called with no null test.
    IRenderPipeline* const Pipe = GetPipeline();

    // 0x0042263b .. 0x0042264b: no camera -> the whole surface, DAT_00610850 / DAT_00610854
    // (the size ApplyGameWindowLayout recorded, not the requested display mode).
    int Left = 0;
    int Top = 0;
    int Width = static_cast<int>(G_RenderSurfaceWidth);
    int Height = static_cast<int>(G_RenderSurfaceHeight);

    if (Cam != nullptr)
    {
        // 0x0042261f: the camera's ViewRect comes out of slot 0x14, and the extents are the
        // inclusive-edge difference plus one -- the same convention SpaceCameraSetViewport uses.
        const int* const Rect = Cam->GetViewport();
        Left = Rect[0];
        Top = Rect[1];
        Width = Rect[2] - Rect[0] + 1;
        Height = Rect[3] - Rect[1] + 1;
    }

    // 0x00422655. Retail stores a flipped origin -- `SurfaceHeight - Height - Top` -- and then
    // hands the pipeline `SurfaceHeight - that - Height`, which cancels exactly back to Top.
    // The value that reaches the viewport does not need the round trip, so the port passes Top
    // directly; the flipped value itself DOES have another reader (Win3DGetViewport, retail's
    // GL_VIEWPORT query), so it is still computed and stored below.
    G_ViewportLeft = Left;
    G_ViewportWidth = Width;
    G_ViewportHeight = Height;
    G_ViewportFlippedTop = static_cast<int>(G_RenderSurfaceHeight) - Height - Top;

    Pipe->SetViewportRect(Left, Top, Width, Height);
}

void Win3DGetViewport(int Query, int* Out4)
{
    // FUN_00422390. Retail only ever compares against one query id; anything else falls through
    // to nothing, matching the disassembly (there is no else branch). The out array is written
    // untested (0x004223a0).
    if (Query == kWin3DQueryViewport)
    {
        Out4[0] = G_ViewportLeft;
        Out4[1] = G_ViewportFlippedTop;
        Out4[2] = G_ViewportWidth;
        Out4[3] = G_ViewportHeight;
    }
}

void Win3DSetViewportRect(int X, int Y, int Width, int Height, const char* SourceFile,
                          int SourceLine)
{
    // FUN_00421620. The stores to 0x00664994..0x006649a0 come first, then pipeline slot 21
    // (+0x54) with the flipped Y (DAT_00610854 is the surface height). Retail has no pipeline
    // null test; GetPipeline() is this module's accessor for DALib::Pipeline.
    (void)SourceFile;
    (void)SourceLine;
    G_ViewportLeft = X;
    G_ViewportFlippedTop = Y;
    G_ViewportWidth = Width;
    G_ViewportHeight = Height;
    GetPipeline()->SetViewportRect(X, static_cast<int>(G_RenderSurfaceHeight) - Y - Height, Width,
                                   Height);
}

void Win3DSetOrtho2DForCamera(FSpaceCamera* Cam)
{
    // FUN_004227d0. Win3DSetOrtho2D's shape, but sized from Cam's own viewport rect (the same
    // Vtable[5] accessor Win3DSetViewport uses) instead of the display.
    const int* const Rect = AsCamera(Cam)->GetViewport();
    const float Width = static_cast<float>(Rect[2] - Rect[0] + 1);
    const float Height = static_cast<float>(Rect[3] - Rect[1] + 1);
    Win3DSetProjection(0.0f, Width, Height, 0.0f, -1.0f, 1.0f);
}

void Win3DSetProjection(float Left, float Right, float Bottom, float Top, float NearPlane,
                        float FarPlane)
{
    // FUN_004221f0.
    //
    // 0x004221fa .. 0x0042226d: an identity is built on the stack and copied into the modelview.
    // This happens FIRST, before any cache test -- so every call resets it, even the ones that
    // decide the projection has not changed.
    GWin3DModelview[0] = 1.0f; GWin3DModelview[1] = 0.0f; GWin3DModelview[2] = 0.0f;
    GWin3DModelview[3] = 0.0f; GWin3DModelview[4] = 1.0f; GWin3DModelview[5] = 0.0f;
    GWin3DModelview[6] = 0.0f; GWin3DModelview[7] = 0.0f; GWin3DModelview[8] = 1.0f;
    GWin3DModelview[9] = 0.0f; GWin3DModelview[10] = 0.0f; GWin3DModelview[11] = 0.0f;

    IRenderPipeline* const Pipe = GetPipeline();
    // 0x0042226f: the out-parameter is seeded with 1 before the query, so a pipeline that
    // does not answer leaves it non-zero and takes the combined-modelview path below.
    int PipelineState = 1;
    Pipe->GetPipelineState(kPipelineStateViewspace, &PipelineState);

    if (PipelineState != 0)
    {
        // 0x00422298: the pipeline is in view space, so one combined matrix is enough.
        Pipe->SetModelview(GWin3DModelview);
    }
    else
    {
        // 0x0042229d: otherwise world and view are set separately, both to the identity.
        Pipe->SetWorld(GWin3DModelview);
        Pipe->SetView(GWin3DModelview);
    }

    // 0x004222b2: the whole cache is behind one flag, and that flag is 1 in .data -- so the
    // cache is on unless something turns it off.
    if (G_bProjectionCacheEnabled != 0)
    {
        const float Args[kProjectionParamCount] = {Left, Right, Bottom, Top, NearPlane, FarPlane};

        // 0x004222f5: a different projection KIND always counts as a change, whatever the
        // parameters say.
        bool bChanged = (G_ProjectionKind != kProjectionKindOrtho);

        // 0x00422300: the compare and the store share one loop, and the STORE IS
        // UNCONDITIONAL -- it happens on the equal path too, where it writes back what was
        // already there. Once a difference is found the compare is skipped for the rest.
        for (int I = 0; I < kProjectionParamCount; ++I)
        {
            if (!bChanged && G_ProjectionParams[I] != Args[I])
                bChanged = true;
            G_ProjectionParams[I] = Args[I];
        }

        if (!bChanged)
        {
            // 0x00422371: nothing to do but count it.
            ++G_ProjectionSkipCount;
            return;
        }

        ++G_ProjectionSetCount;
        G_ProjectionKind = kProjectionKindOrtho;
    }

    // 0x00422365: and the six parameters go to the pipeline in glOrtho's own order.
    Pipe->SetOrtho(Left, Right, Bottom, Top, NearPlane, FarPlane);
}

void Win3DSetOrtho2D()
{
    // FUN_004225e0: glOrtho(0, Width, Height, 0, -1, 1) -- x to the right, y DOWN, which is why
    // the top and bottom arguments are the way round they are. 0x004225e0 / 0x004225f9: FILD
    // DAT_00610854 / DAT_00610850, the recorded surface size.
    Win3DSetProjection(0.0f, static_cast<float>(static_cast<int>(G_RenderSurfaceWidth)),
                       static_cast<float>(static_cast<int>(G_RenderSurfaceHeight)), 0.0f, -1.0f,
                       1.0f);
}

void Win3DSetPerspective(float FovDeg, float Aspect, float NearPlane, float FarPlane)
{
    // FUN_00422820. Perspective's sibling to Win3DSetProjection's ortho path: same change-cache,
    // same backing store (only the first four of its six slots), cache "kind" 1 instead of 2.
    // Unlike the ortho setter, no modelview reset happens here -- retail's disassembly never
    // touches it.
    IRenderPipeline* const Pipe = GetPipeline();

    if (G_bProjectionCacheEnabled != 0)
    {
        const float Args[4] = {FovDeg, Aspect, NearPlane, FarPlane};

        bool bChanged = (G_ProjectionKind != kProjectionKindPerspective);
        for (int I = 0; I < 4; ++I)
        {
            if (!bChanged && G_ProjectionParams[I] != Args[I])
                bChanged = true;
            G_ProjectionParams[I] = Args[I];
        }

        if (!bChanged)
        {
            ++G_ProjectionSkipCount;
            return;
        }

        ++G_ProjectionSetCount;
        G_ProjectionKind = kProjectionKindPerspective;
    }

    Pipe->SetPerspective(FovDeg, Aspect, NearPlane, FarPlane);
}

void Win3DApplyCameraPerspective(ICamera* Camera)
{
    // FUN_00422910: the four getters in retail's call order (0x00422918 far, 0x00422922 near,
    // 0x0042292c aspect, 0x00422936 fov), then FUN_00422820.
    const float FarPlane = Camera->GetZFar();
    const float NearPlane = Camera->GetZNear();
    const float Aspect = Camera->GetAspect();
    const float FovDeg = Camera->GetFovYDeg();
    Win3DSetPerspective(FovDeg, Aspect, NearPlane, FarPlane);
}

void Win3DPushProjection()
{
    // FUN_00422a70. With the cache on, the save is just the cache's own kind and params -- cheap,
    // and exactly what Win3DPopProjection needs to replay through Win3DSetPerspective or
    // Win3DSetProjection. With the cache off there is no cached shape to save, so retail reads the
    // pipeline's actual 4x4 projection matrix back out instead.
    if (G_bProjectionCacheEnabled != 0)
    {
        G_SavedProjectionKind = G_ProjectionKind;
        for (int I = 0; I < kProjectionParamCount; ++I)
            G_SavedProjectionParams[I] = G_ProjectionParams[I];
        return;
    }

    GetPipeline()->GetProjection(G_SavedProjectionMatrix);
}

void Win3DPopProjection()
{
    // FUN_00422ab0, the mirror image.
    if (G_bProjectionCacheEnabled == 0)
    {
        GetPipeline()->SetProjection(G_SavedProjectionMatrix);
        return;
    }

    if (G_SavedProjectionKind == kProjectionKindPerspective)
    {
        Win3DSetPerspective(G_SavedProjectionParams[0], G_SavedProjectionParams[1],
                            G_SavedProjectionParams[2], G_SavedProjectionParams[3]);
    }
    else if (G_SavedProjectionKind == kProjectionKindOrtho)
    {
        Win3DSetProjection(G_SavedProjectionParams[0], G_SavedProjectionParams[1],
                           G_SavedProjectionParams[2], G_SavedProjectionParams[3],
                           G_SavedProjectionParams[4], G_SavedProjectionParams[5]);
    }

    G_SavedProjectionKind = 0;
}

int Win3DGetProjectionSetCount()
{
    return G_ProjectionSetCount;
}

int Win3DGetProjectionSkipCount()
{
    return G_ProjectionSkipCount;
}

// FUN_004214a0 (listing 004214a0-004214c1).
void Win3DOnPresentFrame()
{
    G_ProjectionSetCount = 0;
    G_ProjectionSkipCount = 0;
    // NEG/SBB/INC: AL = 1 when option is 0, else 0.
    G_bProjectionCacheEnabled =
        static_cast<unsigned char>(GetGameOption(kGameOptLikePetal) == 0 ? 1 : 0);
}

void Win3DEnd()
{
    IRenderPipeline* const Pipe = GetPipeline();

    // 0x004216ac: clipping goes on before anything else, and stays on -- nothing turns it back
    // off at the end.
    Pipe->SetRenderState(kRsClipping, 1);

    ApplyTextureStages();

    if (G_VertexCount > 0)
    {
        // 0x004216e0, from .rdata 0x005c9698: the vertex colour is made to drive the diffuse
        // material, which is what makes the packed per-vertex colour visible at all.
        Pipe->SetRenderState(kRsColorVertex, 1);
        Pipe->SetRenderState(kRsDiffuseMaterialSource, 1);

        // 0x00421701: the current lighting flag is saved so it can be put back afterwards.
        int SavedLighting = 0;
        Pipe->GetRenderState(kRsLighting, &SavedLighting);
        Pipe->SetRenderState(kRsLighting, G_bLighting);

        // 0x0042173e: and the emulator's own modelview is installed.
        Pipe->SetModelview(GWin3DModelview);

        const unsigned VertexCount = static_cast<unsigned>(G_VertexCount);

        switch (G_PrimitiveMode)
        {
        case kWin3DPoints:
            Pipe->DrawPrimitive(1, kWin3DFvf, G_Vertices, VertexCount, nullptr);
            break;

        case kWin3DLines:
            // 0x00421784: the locked path draws through the index buffer instead, because a
            // locked pool's vertices are not in draw order.
            if (G_bBufferLocked == 0)
                Pipe->DrawPrimitive(2, kWin3DFvf, G_Vertices, VertexCount, nullptr);
            else
                Pipe->DrawIndexedPrimitiveUp(2, kWin3DFvf, G_Vertices, VertexCount, G_Indices,
                                             static_cast<unsigned>(G_IndexCount), 0);
            break;

        case kWin3DLineStrip:
            Pipe->DrawPrimitive(3, kWin3DFvf, G_Vertices, VertexCount, nullptr);
            break;

        case kWin3DTriangles:
            if (G_bBufferLocked == 0)
                Pipe->DrawPrimitive(4, kWin3DFvf, G_Vertices, VertexCount, nullptr);
            else
                Pipe->DrawIndexedPrimitiveUp(4, kWin3DFvf, G_Vertices, VertexCount, G_Indices,
                                             static_cast<unsigned>(G_IndexCount), 0);
            break;

        case kWin3DTriangleStrip:
            Pipe->DrawPrimitive(5, kWin3DFvf, G_Vertices, VertexCount, nullptr);
            break;

        case kWin3DTriangleFan:
        case kWin3DPolygon:
            // A polygon and a fan are the same draw; retail shares the case outright.
            Pipe->DrawPrimitive(6, kWin3DFvf, G_Vertices, VertexCount, nullptr);
            break;

        case kWin3DLineLoop:
        {
            // 0x00421809: a strip over everything, then a single indexed line closing the last
            // vertex back onto the first.
            Pipe->DrawPrimitive(3, kWin3DFvf, G_Vertices, VertexCount, nullptr);

            const unsigned short Closing[2] = {
                static_cast<unsigned short>(G_VertexCount - 1), 0};
            Pipe->DrawIndexedPrimitiveUp(2, kWin3DFvf, G_Vertices, VertexCount, Closing, 2, 0);
            break;
        }

        case kWin3DQuads:
        {
            // 0x00421904: quads are triangulated into an index list, four vertices becoming two
            // triangles -- (0,1,2) and (0,2,3) -- and the run is capped.
            unsigned Usable = static_cast<unsigned>(G_IndexCount);
            if (Usable > kMaxQuadIndices)
                Usable = kMaxQuadIndices;

            static unsigned short QuadIndices[(kMaxQuadIndices / 4) * 6];
            unsigned Out = 0;
            for (unsigned I = 3; I < Usable; I += 4)
            {
                const unsigned short A = G_Indices[I - 3];
                const unsigned short B = G_Indices[I - 2];
                const unsigned short C = G_Indices[I - 1];
                const unsigned short D = G_Indices[I];

                QuadIndices[Out + 0] = A;
                QuadIndices[Out + 1] = B;
                QuadIndices[Out + 2] = C;
                QuadIndices[Out + 3] = A;
                QuadIndices[Out + 4] = C;
                QuadIndices[Out + 5] = D;
                Out += 6;
            }

            Pipe->DrawIndexedPrimitiveUp(4, kWin3DFvf, G_Vertices, VertexCount, QuadIndices, Out, 0);
            break;
        }

        default:
            // 0x004219cc: retail warns (CALL [FDUMP], pointer not tested) and draws nothing.
            FDUMP(0x100002, "%s(%d) : *** WARNING: glEnd invalid vertex mode (0x%X)\n",
                  "E:\\FL\\Scratch\\Source\\Client\\cmn\\OpenGL.cpp", 0x1d2, G_PrimitiveMode);
            break;
        }

        // 0x004219f0, from .rdata 0x005c96b0: the vertex colour is switched back off, and the
        // lighting flag is restored to whatever it was.
        Pipe->SetRenderState(kRsColorVertex, 0);
        Pipe->SetRenderState(kRsLighting, SavedLighting);
    }

    // 0x00421a09: everything is reset whether or not anything was drawn.
    G_PrimitiveMode = -1;
    G_VertexCount = 0;
    G_IndexCount = 0;
    G_bBufferLocked = 0;
    G_bTextureBound = 0;
    G_BoundTexture = nullptr;

    Pipe->SetTextureStageTexture(0, nullptr);
}

int Win3DGetVertexCount()
{
    return G_VertexCount;
}

int Win3DGetIndexCount()
{
    return G_IndexCount;
}

unsigned long Win3DGetCurrentColour()
{
    return G_CurrentColour;
}

bool Win3DGetVertex(int Index, float* OutXyz3, unsigned long* OutColour)
{
    if (Index < 0 || Index >= G_VertexCount)
        return false;

    const FWin3DVertex& Vertex = G_Vertices[Index];
    if (OutXyz3 != nullptr)
    {
        OutXyz3[0] = Vertex.X;
        OutXyz3[1] = Vertex.Y;
        OutXyz3[2] = Vertex.Z;
    }
    if (OutColour != nullptr)
        *OutColour = Vertex.Diffuse;
    return true;
}

namespace
{
    // local_804 / afStack_404 in retail: 256-entry cosine/sine tables indexed 1..Cols.
    const int kMaxSphereCols = 256;
}

void Win3DBuildSpherePoints(float* OutPoints, float Radius, int Cols, int Rows)
{
    // FUN_00422fe0 (cdecl: Out, Radius, Cols, Rows). The two 256-entry float tables are filled
    // from index 1 up to Cols, and read the same way.
    float CosTable[kMaxSphereCols + 1];
    float SinTable[kMaxSphereCols + 1];

    // 0x00422ff5..0x0042301b: FILD (Rows - 1), FDIVR float DAT_005c8a5c (pi as 0x40490fdb) ->
    // FSTP float; FILD Cols, FDIVR double DAT_005c97f8 (0x401921fb60000000, 2 pi widened from
    // float) -> FSTP float.
    const float PhiStep =
        static_cast<float>(static_cast<double>(3.1415927f) / static_cast<double>(Rows - 1));
    const float ThetaStep =
        static_cast<float>(static_cast<double>(6.2831855f) / static_cast<double>(Cols));

    // 0x0042301f..0x00423041: theta starts at 0.0f and lives on the FPU for the whole loop --
    // FCOS / FSIN of the unrounded angle go straight to FSTP float, and FADD float adds the
    // stored step. TEST/JLE: nothing for Cols <= 0.
    double Theta = 0.0;
    for (int Index = 1; Index <= Cols; ++Index)
    {
        CosTable[Index] = static_cast<float>(cos(Theta));
        SinTable[Index] = static_cast<float>(sin(Theta));
        Theta += static_cast<double>(ThetaStep);
    }

    // 0x0042304e..0x004230ad: phi the same way, one ring per row (TEST/JLE, then DEC/JNZ). Per
    // ring: Y = f(cos(phi) * Radius) and SR = f(sin(phi) * Radius), each rounded once; per column
    // [+4] = Y, [+0] = f(SR * cos[i]), [+8] = f(SR * sin[i]).
    if (Rows > 0)
    {
        double Phi = 0.0;
        float* Out = OutPoints;
        int RowsLeft = Rows;
        do
        {
            const float Y = static_cast<float>(cos(Phi) * static_cast<double>(Radius));
            const float SinPhiRadius = static_cast<float>(sin(Phi) * static_cast<double>(Radius));
            for (int Index = 1; Index <= Cols; ++Index)
            {
                Out[1] = Y;
                Out[0] = static_cast<float>(static_cast<double>(SinPhiRadius) *
                                            static_cast<double>(CosTable[Index]));
                Out[2] = static_cast<float>(static_cast<double>(SinPhiRadius) *
                                            static_cast<double>(SinTable[Index]));
                Out += 3;
            }
            --RowsLeft;
            Phi += static_cast<double>(PhiStep);
        } while (RowsLeft != 0);
    }
}

void Win3DDrawSphereGrid(const float* Points, int Cols, int Rows)
{
    // FUN_004230c0 (cdecl: Points, Cols, Rows). 0x004230c4: PUSH 0xde1 / CALL FUN_00421ed0 --
    // GL_TEXTURE_2D is DISABLED first (FUN_00421ed0 is glDisable; glEnable is FUN_00421dd0), so
    // the wireframe grid draws untextured.
    Win3DDisable(kWin3DTexture2D);

    // Skip the first ring (the pole row) and draw every ring up to but not including the last as
    // a closed loop.
    const float* Row = Points + static_cast<size_t>(Cols) * 3;
    if (Rows - 1 > 1)
    {
        int RingsLeft = Rows - 2;
        do
        {
            Win3DBegin(kWin3DLineLoop);
            for (int Col = 0; Col < Cols; ++Col)
            {
                Win3DVertex3f(Row[0], Row[1], Row[2]);
                Row += 3;
            }
            Win3DEnd();
            --RingsLeft;
        } while (RingsLeft != 0);
    }

    // Every longitude line: all Rows points of one column, as a line strip.
    if (Cols > 0)
    {
        const float* ColStart = Points;
        int ColsLeft = Cols;
        do
        {
            Win3DBegin(kWin3DLineStrip);
            const float* V = ColStart;
            for (int RowIndex = 0; RowIndex < Rows; ++RowIndex)
            {
                Win3DVertex3f(V[0], V[1], V[2]);
                V += static_cast<size_t>(Cols) * 3;
            }
            Win3DEnd();
            ColStart += 3;
            --ColsLeft;
        } while (ColsLeft != 0);
    }
}

void Win3DDrawWireSphere(void* Camera, float const* Xform12, float Radius, int Cols, int Rows)
{
    // FUN_00423180. DAT_006649c8 is a function static guarded by bit 0 of DAT_006679c8 whose
    // atexit destructor LAB_004231e0 is a bare RET, so a plain static array is the same object.
    // Its extent is the 0x3000 bytes up to the guard byte: 0x400 points.
    static float Points[0xc00];
    Win3DBuildSpherePoints(Points, Radius, Cols, Rows);
    Win3DBindModelview(Camera, Xform12);
    Win3DDrawSphereGrid(Points, Cols, Rows);
}

void Win3DVec3Add(float* Out, float const* A, float const* B)
{
    // FUN_00423b30: z, y, x summed on the FPU, then stored x, y, z.
    Out[0] = A[0] + B[0];
    Out[1] = A[1] + B[1];
    Out[2] = A[2] + B[2];
}

void Win3DVec3Sub(float* Out, float const* A, float const* B)
{
    // FUN_00423b60.
    Out[0] = A[0] - B[0];
    Out[1] = A[1] - B[1];
    Out[2] = A[2] - B[2];
}

namespace
{
    // DAT_005c9804 (1.25f) and DAT_005c9800 (1.2f): how far past the segment's half-length the
    // straight and the curved drawer stretch each quad.
    constexpr float kFuzzyLineQuadScale = 1.25f;
    constexpr float kFuzzyArcQuadScale = 1.2f;

    // FUN_00410190 (thiscall on the vector): sqrt((x*x + y*y) + z*z), left on the FPU.
    double FuzzyVecLength(float const* V)
    {
        return sqrt(static_cast<double>(V[0]) * V[0] + static_cast<double>(V[1]) * V[1] +
                    static_cast<double>(V[2]) * V[2]);
    }

    // The drawers' inline length, `(y*y + z*z) + x*x` (or z first -- the same sum), unrounded.
    double FuzzyInlineLength(float const* V)
    {
        return sqrt(static_cast<double>(V[1]) * V[1] + static_cast<double>(V[2]) * V[2] +
                    static_cast<double>(V[0]) * V[0]);
    }

    // The FUN_00422b80 cross product both drawers build inline: Out = D x E, each difference of
    // products rounded to float on its FSTP.
    void FuzzyCross(float* Out, float const* D, float const* E)
    {
        Out[0] = static_cast<float>(static_cast<double>(E[2]) * D[1] -
                                    static_cast<double>(E[1]) * D[2]);
        Out[1] = static_cast<float>(static_cast<double>(D[2]) * E[0] -
                                    static_cast<double>(E[2]) * D[0]);
        Out[2] = static_cast<float>(static_cast<double>(E[1]) * D[0] -
                                    static_cast<double>(D[1]) * E[0]);
    }

    // Common's RandomVector(Radius) (IAT 0x005c60a4) added to P through FUN_00423b30.
    void FuzzyJitter(float* Out, float const* P, float Radius)
    {
        Vector const Offset = RandomVector(Radius);
        float const OffsetXyz[3] = {Offset.x, Offset.y, Offset.z};
        Win3DVec3Add(Out, P, OffsetXyz);
    }

    // Scale V in place by Wanted / FUN_00410190(V), each product rounded to float.
    void FuzzyRescale(float* V, float Wanted)
    {
        double const Scale = static_cast<double>(Wanted) / FuzzyVecLength(V);
        V[0] = static_cast<float>(V[0] * Scale);
        V[1] = static_cast<float>(V[1] * Scale);
        V[2] = static_cast<float>(V[2] * Scale);
    }

    // One camera-facing quad (0x00423ff6..0x0042416a, 0x00423786..0x004238ee): glBindTexture of
    // the frame's handle, GL_QUADS, then (0,0) Mid + Along, (1,0) Mid + Side, (1,1) Mid - Along,
    // (0,1) Mid - Side, glEnd.
    void FuzzyEmitQuad(unsigned long Handle, float const* Mid, float const* Along,
                       float const* Side)
    {
        float Corner[3];
        Win3DBindTexture(reinterpret_cast<void*>(Handle));
        Win3DBegin(kWin3DQuads);
        Win3DTexCoord2f(0.0f, 0.0f);
        Win3DVec3Add(Corner, Mid, Along);
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);
        Win3DTexCoord2f(1.0f, 0.0f);
        Win3DVec3Add(Corner, Mid, Side);
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);
        Win3DTexCoord2f(1.0f, 1.0f);
        Win3DVec3Sub(Corner, Mid, Along);
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);
        Win3DTexCoord2f(0.0f, 1.0f);
        Win3DVec3Sub(Corner, Mid, Side);
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);
        Win3DEnd();
    }
}

void Win3DDrawFuzzyLine(FWin3DFuzzyLine const* Line)
{
    // FUN_00423be0. 0x00423bf0: a set flag hands the record to the arc drawer.
    if (Line->bCurved != 0)
    {
        Win3DDrawFuzzyArc(Line);
        return;
    }

    // 0x00423c0a..0x00423c62: Dir = End - Start; its length is kept as a float, and divided
    // unrounded by Width for the segment count ftol(floor(Length / Width) + 1.0).
    float Dir[3];
    Win3DVec3Sub(Dir, Line->End, Line->Start);
    double const LengthExt = FuzzyInlineLength(Dir);
    float const Length = static_cast<float>(LengthExt);
    int const Count = static_cast<int>(floor(LengthExt / Line->Width) + 1.0);

    // 0x00423c68..0x00423cc3: Step = Length / Count, and Unit = Dir rescaled to Step.
    float const Step = static_cast<float>(static_cast<double>(Length) / Count);
    float Unit[3] = {Dir[0], Dir[1], Dir[2]};
    FuzzyRescale(Unit, Step);

    // 0x00423cc5: the colour, on both paths.
    Win3DColor3f(Line->Color[0], Line->Color[1], Line->Color[2]);

    // 0x00423cca..0x00423cdc: the jitter radius; no texture takes the line-strip path.
    float const Jitter = Line->JitterScale * Line->Width;
    if (Line->Texture == nullptr)
    {
        // 0x0042419f..0x0042429f: texturing and blending off, then Start, the Count - 1 jittered
        // interior points Start + Unit * i, and End as one line strip. The x product stays on the
        // FPU into its sum; y and z are rounded to float first.
        Win3DDisable(kWin3DTexture2D);
        Win3DDisable(kWin3DBlend);
        Win3DBegin(kWin3DLineStrip);
        Win3DVertex3f(Line->Start[0], Line->Start[1], Line->Start[2]);
        for (int Index = 1; Index < Count; ++Index)
        {
            double const Scale = static_cast<double>(Index);
            float const StepY = static_cast<float>(Unit[1] * Scale);
            float const StepZ = static_cast<float>(Unit[2] * Scale);
            float const OnLine[3] = {
                static_cast<float>(Unit[0] * Scale + Line->Start[0]),
                StepY + Line->Start[1],
                StepZ + Line->Start[2]};
            float Point[3];
            FuzzyJitter(Point, OnLine, Jitter);
            Win3DVertex3f(Point[0], Point[1], Point[2]);
        }
        Win3DVertex3f(Line->End[0], Line->End[1], Line->End[2]);
        Win3DEnd();
        return;
    }

    // 0x00423ce2..0x00423d31: frame 0 of the texture (ITextureLibrary2 +0x58), additive blend.
    float Cur[3] = {Line->Start[0], Line->Start[1], Line->Start[2]};
    unsigned long Frame[5];
    DALib::TextureLib->GetTextureFrame(Line->Texture, 0, Frame);
    Win3DEnable(kWin3DBlend);
    Win3DSetBlendFunc(kWin3DBlendOne, kWin3DBlendOne);

    for (int Index = 1; Index <= Count; ++Index)
    {
        // 0x00423d40..0x00423dc2: the last point is End itself, the others Start + Unit * i
        // (FUN_00411d60 with (float)i) jittered.
        float Next[3];
        if (Index == Count)
        {
            Next[0] = Line->End[0];
            Next[1] = Line->End[1];
            Next[2] = Line->End[2];
        }
        else
        {
            float Offset[3];
            float OnLine[3];
            ScaleVec3(Offset, Unit, static_cast<float>(Index));
            Win3DVec3Add(OnLine, Line->Start, Offset);
            FuzzyJitter(Next, OnLine, Jitter);
        }

        // 0x00423dc5..0x00423e73: the side direction (Next - Cur) x (Eye - Cur).
        float ToEye[3];
        float Segment[3];
        float Side[3];
        Win3DVec3Sub(ToEye, Line->EyePos, Cur);
        Win3DVec3Sub(Segment, Next, Cur);
        FuzzyCross(Side, Segment, ToEye);

        // 0x00423e78..0x00423f44: the midpoint, and the side rescaled to half of |Mid - Cur|
        // (the root times DAT_005c75e8, 0.5 as a double).
        float Half[3];
        float HalfScaled[3];
        float Mid[3];
        float FromCur[3];
        Win3DVec3Sub(Half, Next, Cur);
        ScaleVec3(HalfScaled, Half, 0.5f);
        Win3DVec3Add(Mid, Cur, HalfScaled);
        Win3DVec3Sub(FromCur, Mid, Cur);
        FuzzyRescale(Side, static_cast<float>(FuzzyInlineLength(FromCur) * 0.5));

        // 0x00423f44..0x00423ff1: Along = (Cur - Mid) * 1.25, Side * 1.25 * SideScale.
        float Back[3];
        Win3DVec3Sub(Back, Cur, Mid);
        float const Along[3] = {Back[0] * kFuzzyLineQuadScale, Back[1] * kFuzzyLineQuadScale,
                                Back[2] * kFuzzyLineQuadScale};
        float const SideWide[3] = {Side[0] * kFuzzyLineQuadScale, Side[1] * kFuzzyLineQuadScale,
                                   Side[2] * kFuzzyLineQuadScale};
        float const SideQuad[3] = {SideWide[0] * Line->SideScale, SideWide[1] * Line->SideScale,
                                   SideWide[2] * Line->SideScale};

        FuzzyEmitQuad(Frame[0], Mid, Along, SideQuad);

        // 0x0042416f..0x0042418e.
        Cur[0] = Next[0];
        Cur[1] = Next[1];
        Cur[2] = Next[2];
    }
}

void Win3DDrawFuzzyArc(FWin3DFuzzyLine const* Line)
{
    // FUN_004231f0. 0x00423201..0x0042321d: both ends relative to the centre.
    float FromCentre[3];
    float ToEnd[3];
    Win3DVec3Sub(FromCentre, Line->Start, Line->Control);
    Win3DVec3Sub(ToEnd, Line->End, Line->Control);

    // 0x00423222..0x00423281: either radius below 0.001 (DAT_005c75f8, a double compared with
    // the unrounded root; TEST AH,5 / JNP taken = below) draws nothing.
    double const RadiusExt = FuzzyInlineLength(FromCentre);
    float const Radius = static_cast<float>(RadiusExt);
    if (RadiusExt < 0.001)
        return;
    if (FuzzyInlineLength(ToEnd) < 0.001)
        return;

    // 0x00423287..0x004232bf: the jitter radius, and ToEnd rescaled to the start radius.
    float const Jitter = Line->JitterScale * Line->Width;
    FuzzyRescale(ToEnd, Radius);

    // 0x004232c1..0x0042332a: the swept angle. The cosine is compared unrounded with -1.0f
    // (below -> -1) and then, stored, with 1.0f (above -> 1); NaN passes both.
    float const RadiusSq = Radius * Radius;
    double const CosineExt = (static_cast<double>(ToEnd[2]) * FromCentre[2] +
                              static_cast<double>(ToEnd[1]) * FromCentre[1] +
                              static_cast<double>(ToEnd[0]) * FromCentre[0]) /
                             RadiusSq;
    float Cosine = static_cast<float>(CosineExt);
    if (CosineExt < -1.0)
        Cosine = -1.0f;
    else if (Cosine > 1.0f)
        Cosine = 1.0f;
    float const Angle = static_cast<float>(acos(static_cast<double>(Cosine)));

    // 0x0042332e..0x0042337d: the angle one chord of Width subtends, acos(1 - W^2 / (2 R^2)),
    // clamped the same way on the stored float.
    double const Width = Line->Width;
    float ChordCosine = static_cast<float>(
        1.0 - Width * Width / (static_cast<double>(RadiusSq) + RadiusSq));
    if (ChordCosine < -1.0f)
        ChordCosine = -1.0f;
    else if (ChordCosine > 1.0f)
        ChordCosine = 1.0f;

    // 0x00423381..0x004233af: Count = ftol(floor(Angle / acos(chord)) + 1.0), Step = Angle /
    // Count.
    int const Count = static_cast<int>(
        floor(static_cast<double>(Angle) / acos(static_cast<double>(ChordCosine))) + 1.0);
    float const Step = static_cast<float>(static_cast<double>(Angle) / Count);

    // 0x004233b3..0x004233fb: the rotation axis FromCentre x ToEnd.
    float Axis[3];
    FuzzyCross(Axis, FromCentre, ToEnd);

    FVector const Source = {FromCentre[0], FromCentre[1], FromCentre[2]};

    if (Line->Texture == nullptr)
    {
        // 0x00423923..0x00423b2b: texturing and blending off, the colour, then Start, the
        // Count - 1 rotated and jittered interior points, and Control + ToEnd as a line strip.
        Win3DDisable(kWin3DTexture2D);
        Win3DDisable(kWin3DBlend);
        Win3DColor3f(Line->Color[0], Line->Color[1], Line->Color[2]);
        Win3DBegin(kWin3DLineStrip);
        Win3DVertex3f(Line->Start[0], Line->Start[1], Line->Start[2]);
        for (int Index = 1; Index < Count; ++Index)
        {
            // 0x00423980..0x004239e5: the half angle stays on the FPU; the axis length is the
            // inline (x*x + z*z) + y*y root.
            double const HalfAngle = static_cast<double>(Index) * Step * 0.5f;
            double const AxisLength = sqrt(static_cast<double>(Axis[0]) * Axis[0] +
                                           static_cast<double>(Axis[2]) * Axis[2] +
                                           static_cast<double>(Axis[1]) * Axis[1]);
            FQuaternion Rotation;
            Rotation.W = static_cast<float>(cos(HalfAngle));
            float Vec[3];
            ScaleVec3(Vec, Axis, static_cast<float>(sin(HalfAngle) / AxisLength));
            Rotation.X = Vec[0];
            Rotation.Y = Vec[1];
            Rotation.Z = Vec[2];

            // 0x00423a06..0x00423a6b: DAT_006679d8 slots +0x54 and +0x2c called directly.
            FMatrix Matrix;
            Win3DTransformMathEngine()->QuaternionToMatrix(Matrix, Rotation);
            FVector Rotated;
            Win3DTransformMathEngine()->TransformMatrix(Rotated, Matrix, Source);

            // 0x00423a6e..0x00423ad5.
            float const OnArc[3] = {Rotated.X + Line->Control[0], Rotated.Y + Line->Control[1],
                                    Rotated.Z + Line->Control[2]};
            float Point[3];
            FuzzyJitter(Point, OnArc, Jitter);
            Win3DVertex3f(Point[0], Point[1], Point[2]);
        }
        Win3DVertex3f(ToEnd[0] + Line->Control[0], ToEnd[1] + Line->Control[1],
                      ToEnd[2] + Line->Control[2]);
        Win3DEnd();
        return;
    }

    // 0x0042340b..0x00423459: frame 0 of the texture, additive blend. No colour is set on this
    // path.
    float Cur[3] = {Line->Start[0], Line->Start[1], Line->Start[2]};
    unsigned long Frame[5];
    DALib::TextureLib->GetTextureFrame(Line->Texture, 0, Frame);
    Win3DEnable(kWin3DBlend);
    Win3DSetBlendFunc(kWin3DBlendOne, kWin3DBlendOne);

    for (int Index = 1; Index <= Count; ++Index)
    {
        float Next[3];
        if (Index == Count)
        {
            // 0x00423466..0x00423491: the last point is Control + ToEnd.
            Win3DVec3Add(Next, Line->Control, ToEnd);
        }
        else
        {
            // 0x00423496..0x004235aa: the half angle is stored as a float (FST) after its sine
            // is taken unrounded; the cosine is of the stored float. Axis length FUN_00410190.
            double const HalfAngleExt = static_cast<double>(Index) * Step * 0.5f;
            float const HalfAngle = static_cast<float>(HalfAngleExt);
            float const Sine = static_cast<float>(sin(HalfAngleExt));
            FQuaternion Rotation;
            Rotation.W = static_cast<float>(cos(static_cast<double>(HalfAngle)));
            float Vec[3];
            ScaleVec3(Vec, Axis, static_cast<float>(Sine / FuzzyVecLength(Axis)));
            Rotation.X = Vec[0];
            Rotation.Y = Vec[1];
            Rotation.Z = Vec[2];

            FMatrix Matrix;
            Win3DTransformMathEngine()->QuaternionToMatrix(Matrix, Rotation);
            FVector Rotated;
            Win3DTransformByMatrix(Rotated, Matrix, Source);   // FUN_00423b90
            float const RotatedXyz[3] = {Rotated.X, Rotated.Y, Rotated.Z};
            float OnArc[3];
            Win3DVec3Add(OnArc, Line->Control, RotatedXyz);
            FuzzyJitter(Next, OnArc, Jitter);
        }

        // 0x004235ad..0x0042363a: the side direction (Next - Cur) x (Eye - Cur).
        float ToEye[3];
        float Segment[3];
        float Side[3];
        Win3DVec3Sub(ToEye, Line->EyePos, Cur);
        Win3DVec3Sub(Segment, Next, Cur);
        FuzzyCross(Side, Segment, ToEye);

        // 0x0042363f..0x00423706: the midpoint, and the side rescaled to |Mid - Cur| (no halving
        // here, unlike the straight drawer).
        float Half[3];
        float HalfScaled[3];
        float Mid[3];
        float FromCur[3];
        Win3DVec3Sub(Half, Next, Cur);
        ScaleVec3(HalfScaled, Half, 0.5f);
        Win3DVec3Add(Mid, HalfScaled, Cur);
        Win3DVec3Sub(FromCur, Mid, Cur);
        FuzzyRescale(Side, static_cast<float>(FuzzyInlineLength(FromCur)));

        // 0x00423708..0x00423781: Along = (Cur - Mid) * 1.2, Side * 1.2 (no SideScale).
        float Back[3];
        Win3DVec3Sub(Back, Cur, Mid);
        float const Along[3] = {Back[0] * kFuzzyArcQuadScale, Back[1] * kFuzzyArcQuadScale,
                                Back[2] * kFuzzyArcQuadScale};
        float const SideQuad[3] = {Side[0] * kFuzzyArcQuadScale, Side[1] * kFuzzyArcQuadScale,
                                   Side[2] * kFuzzyArcQuadScale};

        FuzzyEmitQuad(Frame[0], Mid, Along, SideQuad);

        // 0x004238f3..0x00423912.
        Cur[0] = Next[0];
        Cur[1] = Next[1];
        Cur[2] = Next[2];
    }
}
