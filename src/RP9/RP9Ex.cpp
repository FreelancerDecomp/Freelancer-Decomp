// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// RP9 Ex: the port-only Direct3D 9 extensions of RP9.dll -- IRenderPipelineEx and IRPDrawEx
// (render/IRenderPipelineEx.h). Retail and RP8 have nothing like this; the code below is new
// port feature work, not a mirror of an RP8 function. docs/re/rp9.md, "Ex extensions",
// describes the contract.
//
// How it stays out of the way of the RP8-mirrored fixed-function code:
// - Nothing here runs unless a consumer QueryInterfaces Iids::RenderPipelineEx / RPDrawEx --
//   except the INTZ depth buffer, which CreateBuffers creates instead of the auto depth-stencil
//   whenever the device supports it (transparent to fixed-function rendering).
// - Every device write that bypasses RP9's lazy caches happens inside BeginExPass/EndExPass,
//   and EndExPass makes the caches true again: render states go through the cache itself and
//   are re-queued to their pre-pass values, fixed-function stages the pass touched are saved
//   and re-sent, shaders are unbound, colour target / depth surface / viewport are restored.
#include "RP9Ex.h"
#include "readfile.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// d3dx9.h after RP9.h (render/d3d8_min.h already pulled the Windows SDK d3d9.h).
#include <d3dx9.h>

namespace
{
    const int kExFdumpError = 0x100001;
    const int kExFdumpWarning = 0x100002;
    const int kExFdumpNotice = 0x100003;
    const char kExSource[] = "RP9Ex.cpp";

    // The INTZ FOURCC: a depth-stencil format that can also be sampled (24-bit depth, 8-bit
    // stencil; the sampled value is the depth in .r). Supported by every D3D10-class driver.
    const D3DFORMAT kIntzFormat = (D3DFORMAT)(MAKEFOURCC('I', 'N', 'T', 'Z'));

    // ps_3_0 has 16 samplers; fixed-function stages are 0..7.
    const unsigned long kExSamplerCount = 16;

    // One FDUMP line per text line ("%s(%d) : <KIND>:General:RP9Ex: <text>"), so a long
    // compiler log is never cut by the 0x400-byte line buffer of the log hook.
    void ExLog(int Flags, const char* Kind, int Line, const char* Text)
    {
        const char* Cursor = Text;
        while (Cursor != nullptr && *Cursor != '\0')
        {
            const char* End = Cursor;
            while (*End != '\0' && *End != '\n' && (End - Cursor) < 0x300)
                ++End;
            char Chunk[0x301];
            const size_t Length = (size_t)(End - Cursor);
            memcpy(Chunk, Cursor, Length);
            Chunk[Length] = '\0';
            if (Length != 0 && Chunk[Length - 1] == '\r')
                Chunk[Length - 1] = '\0';
            if (Chunk[0] != '\0')
                FDUMP(Flags, "%s(%d) : %s:General:RP9Ex: %s", kExSource, Line, Kind, Chunk);
            Cursor = (*End == '\n') ? End + 1 : End;
        }
    }

    void ExLogf(int Flags, const char* Kind, int Line, const char* Format, ...)
    {
        char Text[0x800];
        va_list Args;
        va_start(Args, Format);
        _vsnprintf(Text, sizeof(Text), Format, Args);
        va_end(Args);
        Text[sizeof(Text) - 1] = '\0';
        ExLog(Flags, Kind, Line, Text);
    }

    // Reads a whole FileSystem-relative file (the open descriptor RP9's LoadTexture uses).
    bool ReadFsFile(IFileSystem* FileSystem, const char* Path, std::vector<char>* Out)
    {
        Out->clear();
        if (FileSystem == nullptr || Path == nullptr)
            return false;
        FFileSystemDesc Desc;
        memset(&Desc, 0, sizeof(Desc));
        Desc.Size = 0x34;
        Desc.ComponentName = "FileSystem";
        Desc.Path = Path;
        Desc.Access = 0x80000000; // GENERIC_READ
        Desc.ShareMode = 1;       // FILE_SHARE_READ
        Desc.Creation = 3;        // OPEN_EXISTING
        Desc.Flags = 0x8000080;
        Desc.Field30 = 0xFFFFFFFF;
        const int Handle = FileSystem->OpenByDesc(&Desc);
        if (Handle == -1)
            return false;
        const unsigned long Size = FileSystem->GetSize((unsigned long)(Handle), nullptr);
        bool bOk = Size != 0xFFFFFFFF;
        if (bOk && Size != 0)
        {
            Out->resize(Size);
            unsigned long Got = 0;
            bOk = FileSystem->Read((unsigned long)(Handle), &(*Out)[0], Size, &Got, 0) != 0 &&
                  Got == Size;
        }
        FileSystem->CloseFile((unsigned long)(Handle));
        if (!bOk)
            Out->clear();
        return bOk;
    }

    // "SHADERS\\x.hlsl" -> "SHADERS\\" (empty without a directory part).
    std::string DirectoryOf(const std::string& Path)
    {
        const size_t Slash = Path.find_last_of("\\/");
        if (Slash == std::string::npos)
            return std::string();
        return Path.substr(0, Slash + 1);
    }

    // #include "x" / <x>: first next to the top-level file, then from the FileSystem root.
    class FRp9ExShaderInclude final : public ID3DXInclude
    {
    public:
        FRp9ExShaderInclude(IFileSystem* InFileSystem, const std::string& InDirectory)
            : FileSystem(InFileSystem), Directory(InDirectory)
        {
        }

        // STDMETHOD keeps the base's COM_DECLSPEC_NOTHROW on the override.
        STDMETHOD(Open)(D3DXINCLUDE_TYPE /*Type*/, LPCSTR FileName, LPCVOID /*ParentData*/,
                        LPCVOID* OutData, UINT* OutBytes) override
        {
            std::vector<char> Data;
            bool bFound = false;
            if (!Directory.empty())
                bFound = ReadFsFile(FileSystem, (Directory + FileName).c_str(), &Data);
            if (!bFound)
                bFound = ReadFsFile(FileSystem, FileName, &Data);
            if (!bFound)
            {
                ExLogf(kExFdumpError, "ERROR", __LINE__, "shader include \"%s\" not found",
                       FileName);
                return E_FAIL;
            }
            char* Copy = new char[Data.size() + 1];
            if (!Data.empty())
                memcpy(Copy, &Data[0], Data.size());
            Copy[Data.size()] = '\0';
            *OutData = Copy;
            *OutBytes = (UINT)(Data.size());
            return S_OK;
        }

        STDMETHOD(Close)(LPCVOID Data) override
        {
            delete[] (const char*)(Data);
            return S_OK;
        }

    private:
        IFileSystem* FileSystem;
        std::string Directory;
    };

    // One D3DXCompileShader run. The compiler's text is logged: as ERROR on failure, as WARNING
    // when it compiled with warnings.
    bool CompileStage(const std::string& Source, const std::string& Path, ID3DXInclude* Include,
                      const std::string& Entry, const char* Profile,
                      std::vector<unsigned char>* OutCode)
    {
        LPD3DXBUFFER Code = nullptr;
        LPD3DXBUFFER Errors = nullptr;
        const HRESULT Hr = D3DXCompileShader(Source.c_str(), (UINT)(Source.size()), nullptr,
                                             Include, Entry.c_str(), Profile, 0, &Code,
                                             &Errors, nullptr);
        const bool bOk = SUCCEEDED(Hr) && Code != nullptr;
        if (!bOk)
            ExLogf(kExFdumpError, "ERROR", __LINE__, "%s: %s %s failed to compile (hr 0x%08x)",
                   Path.c_str(), Profile, Entry.c_str(), (unsigned int)(Hr));
        if (Errors != nullptr)
        {
            const char* Text = (const char*)(Errors->GetBufferPointer());
            if (Text != nullptr)
                ExLog(bOk ? kExFdumpWarning : kExFdumpError, bOk ? "WARNING" : "ERROR",
                      __LINE__, Text);
            Errors->Release();
        }
        if (!bOk)
        {
            if (Code != nullptr)
                Code->Release();
            return false;
        }
        const unsigned char* Bytes = (const unsigned char*)(Code->GetBufferPointer());
        OutCode->assign(Bytes, Bytes + Code->GetBufferSize());
        Code->Release();
        return true;
    }

    // Shader objects from the program's bytecode, where missing (new device, first compile).
    HRESULT CreateProgramShaders(IDirect3DDevice9* Device, FRPExProgram* Program)
    {
        if (Device == nullptr || Program->VsBytecode.empty() || Program->PsBytecode.empty())
            return S_OK;
        HRESULT Hr = S_OK;
        if (Program->Vs == nullptr)
            Hr = Device->CreateVertexShader((const DWORD*)(&Program->VsBytecode[0]), &Program->Vs);
        if (SUCCEEDED(Hr) && Program->Ps == nullptr)
            Hr = Device->CreatePixelShader((const DWORD*)(&Program->PsBytecode[0]), &Program->Ps);
        if (FAILED(Hr))
            ExLogf(kExFdumpError, "ERROR", __LINE__, "%s: creating the shaders failed (hr 0x%08x)",
                   Program->Path.c_str(), (unsigned int)(Hr));
        return Hr;
    }

    void ReleaseProgramShaders(FRPExProgram* Program)
    {
        if (Program->Vs != nullptr)
        {
            Program->Vs->Release();
            Program->Vs = nullptr;
        }
        if (Program->Ps != nullptr)
        {
            Program->Ps->Release();
            Program->Ps = nullptr;
        }
    }

    // Read + compile both stages; only when both compiled (and, with a device, both shader
    // objects were made) does the program switch to the new code. Otherwise the previous
    // bytecode and shaders stay.
    bool CompileProgram(IDirect3DDevice9* Device, FRPExProgram* Program)
    {
        std::vector<char> File;
        if (!ReadFsFile(Program->FileSystem, Program->Path.c_str(), &File))
        {
            ExLogf(kExFdumpError, "ERROR", __LINE__, "%s: cannot read the shader file",
                   Program->Path.c_str());
            return false;
        }
        // #line names the file in the compiler's messages (it reports "memory" otherwise).
        std::string Source = "#line 1 \"";
        for (size_t Index = 0; Index < Program->Path.size(); ++Index)
            Source += Program->Path[Index] == '\\' ? '/' : Program->Path[Index];
        Source += "\"\n";
        if (!File.empty())
            Source.append(&File[0], File.size());

        FRp9ExShaderInclude Include(Program->FileSystem, DirectoryOf(Program->Path));
        std::vector<unsigned char> VsCode;
        std::vector<unsigned char> PsCode;
        const bool bVs = CompileStage(Source, Program->Path, &Include, Program->VsEntry, "vs_3_0",
                                      &VsCode);
        const bool bPs = CompileStage(Source, Program->Path, &Include, Program->PsEntry, "ps_3_0",
                                      &PsCode);
        if (!bVs || !bPs)
        {
            if (!Program->VsBytecode.empty())
                ExLogf(kExFdumpError, "ERROR", __LINE__, "%s: keeping the previous shader code",
                       Program->Path.c_str());
            return false;
        }

        FRPExProgram Fresh;
        Fresh.FileSystem = nullptr;
        Fresh.Vs = nullptr;
        Fresh.Ps = nullptr;
        Fresh.VsBytecode.swap(VsCode);
        Fresh.PsBytecode.swap(PsCode);
        if (FAILED(CreateProgramShaders(Device, &Fresh)))
        {
            ReleaseProgramShaders(&Fresh);
            return false;
        }
        ReleaseProgramShaders(Program);
        Program->Vs = Fresh.Vs;
        Program->Ps = Fresh.Ps;
        Program->VsBytecode.swap(Fresh.VsBytecode);
        Program->PsBytecode.swap(Fresh.PsBytecode);
        ExLogf(kExFdumpNotice, "NOTICE", __LINE__, "%s: compiled (%s, %s)", Program->Path.c_str(),
               Program->VsEntry.c_str(), Program->PsEntry.c_str());
        return true;
    }

    // ERPExFormat -> D3DFORMAT and bytes per texel; D3DFMT_UNKNOWN for an unknown value.
    D3DFORMAT ExFormatToD3d(ERPExFormat Format, unsigned long* OutBytes)
    {
        switch (Format)
        {
        case RPExFormatA8R8G8B8: *OutBytes = 4; return D3DFMT_A8R8G8B8;
        case RPExFormatL8: *OutBytes = 1; return D3DFMT_L8;
        case RPExFormatA16B16G16R16F: *OutBytes = 8; return D3DFMT_A16B16G16R16F;
        case RPExFormatR16F: *OutBytes = 2; return D3DFMT_R16F;
        case RPExFormatR32F: *OutBytes = 4; return D3DFMT_R32F;
        case RPExFormatG16R16F: *OutBytes = 4; return D3DFMT_G16R16F;
        default: *OutBytes = 0; return D3DFMT_UNKNOWN;
        }
    }

    void ReleaseExTextureGpu(FRPExTexture* Texture)
    {
        if (Texture->Surface != nullptr)
        {
            Texture->Surface->Release();
            Texture->Surface = nullptr;
        }
        if (Texture->Texture != nullptr)
        {
            Texture->Texture->Release();
            Texture->Texture = nullptr;
        }
    }

    // A D3DPOOL_DEFAULT render target at its requested size (absolute, or a fraction of the
    // back buffer). Failure leaves the handle without GPU objects (binding it then fails).
    HRESULT CreateRenderTargetGpu(IDirect3DDevice9* Device, FRPExTexture* Target,
                                  unsigned long BackBufferWidth, unsigned long BackBufferHeight)
    {
        unsigned long Width = Target->TargetDesc.Width;
        unsigned long Height = Target->TargetDesc.Height;
        if (Width == 0 || Height == 0)
        {
            Width = (unsigned long)((float)(BackBufferWidth) * Target->TargetDesc.BackBufferScale);
            Height = (unsigned long)((float)(BackBufferHeight) * Target->TargetDesc.BackBufferScale);
        }
        if (Width == 0)
            Width = 1;
        if (Height == 0)
            Height = 1;
        unsigned long Bytes = 0;
        const D3DFORMAT Format = ExFormatToD3d(Target->Format, &Bytes);
        IDirect3DTexture9* Texture = nullptr;
        HRESULT Hr = Device->CreateTexture((UINT)(Width), (UINT)(Height), 1, D3DUSAGE_RENDERTARGET,
                                           Format, D3DPOOL_DEFAULT, &Texture, nullptr);
        IDirect3DSurface9* Surface = nullptr;
        if (SUCCEEDED(Hr))
            Hr = Texture->GetSurfaceLevel(0, &Surface);
        if (FAILED(Hr))
        {
            if (Texture != nullptr)
                Texture->Release();
            ExLogf(kExFdumpWarning, "WARNING", __LINE__,
                   "render target %lux%lu (format %u) could not be created (hr 0x%08x)", Width,
                   Height, (unsigned int)(Target->Format), (unsigned int)(Hr));
            return Hr;
        }
        Target->Texture = Texture;
        Target->Surface = Surface;
        Target->Width = Width;
        Target->Height = Height;
        return S_OK;
    }

    // A D3DPOOL_MANAGED one-level volume texture filled from the kept texels.
    HRESULT CreateVolumeGpu(IDirect3DDevice9* Device, FRPExTexture* Volume)
    {
        unsigned long Bytes = 0;
        const D3DFORMAT Format = ExFormatToD3d(Volume->Format, &Bytes);
        IDirect3DVolumeTexture9* Texture = nullptr;
        HRESULT Hr = Device->CreateVolumeTexture((UINT)(Volume->Width), (UINT)(Volume->Height),
                                                 (UINT)(Volume->Depth), 1, 0, Format,
                                                 D3DPOOL_MANAGED, &Texture, nullptr);
        if (SUCCEEDED(Hr))
        {
            D3DLOCKED_BOX Box;
            Hr = Texture->LockBox(0, &Box, nullptr, 0);
            if (SUCCEEDED(Hr))
            {
                const unsigned long RowBytes = Volume->Width * Bytes;
                const unsigned char* Source = Volume->VolumeData.empty() ? nullptr
                                                                         : &Volume->VolumeData[0];
                for (unsigned long Slice = 0; Slice < Volume->Depth; ++Slice)
                {
                    unsigned char* SliceBits = (unsigned char*)(Box.pBits) + Slice * Box.SlicePitch;
                    for (unsigned long Row = 0; Row < Volume->Height; ++Row)
                    {
                        unsigned char* Dest = SliceBits + Row * Box.RowPitch;
                        if (Source != nullptr)
                            memcpy(Dest, Source + (Slice * Volume->Height + Row) * RowBytes,
                                   RowBytes);
                        else
                            memset(Dest, 0, RowBytes);
                    }
                }
                Texture->UnlockBox(0);
            }
        }
        if (FAILED(Hr))
        {
            if (Texture != nullptr)
                Texture->Release();
            ExLogf(kExFdumpWarning, "WARNING", __LINE__,
                   "volume texture %lux%lux%lu (format %u) could not be created (hr 0x%08x)",
                   Volume->Width, Volume->Height, Volume->Depth, (unsigned int)(Volume->Format),
                   (unsigned int)(Hr));
            return Hr;
        }
        Volume->Texture = Texture;
        return S_OK;
    }

    DWORD ExFilterToD3d(ERPExFilter Filter)
    {
        return Filter == RPExFilterLinear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
    }

    DWORD ExMipFilterToD3d(ERPExMipFilter Filter)
    {
        if (Filter == RPExMipPoint)
            return D3DTEXF_POINT;
        if (Filter == RPExMipLinear)
            return D3DTEXF_LINEAR;
        return D3DTEXF_NONE;
    }

    DWORD ExAddressToD3d(ERPExAddress Address)
    {
        if (Address == RPExAddressClamp)
            return D3DTADDRESS_CLAMP;
        if (Address == RPExAddressMirror)
            return D3DTADDRESS_MIRROR;
        return D3DTADDRESS_WRAP;
    }

    // Pass-scoped render state through RP9's own cache: it reaches the device at the next
    // flush, and EndExPass re-queues the pre-pass value (FRp9RenderStateCache::RestorePending).
    void ExSetPassState(FDirectX9* Self, D3DRENDERSTATETYPE State, unsigned long Value)
    {
        Self->RenderStates_.MarkDirty((unsigned int)(State), Value);
    }

    // No depth surface bound: depth test and write off too (D3D9 leaves them undefined there).
    void ExUnbindDepth(FDirectX9* Self)
    {
        FRp9ExState* Ex = Self->Ex_;
        Self->Device_->SetDepthStencilSurface(nullptr);
        Ex->bDepthBound = false;
        ExSetPassState(Self, D3DRS_ZENABLE, D3DZB_FALSE);
        ExSetPassState(Self, D3DRS_ZWRITEENABLE, FALSE);
    }

    // The pass's colour target (null = the back buffer the pass started on) and depth.
    // SetRenderTarget also resets the device viewport to the whole target.
    HRESULT ExSelectTarget(FDirectX9* Self, FRPExTexture* Target, bool bKeepDepth)
    {
        FRp9ExState* Ex = Self->Ex_;
        IDirect3DDevice9* Device = Self->Device_;
        IDirect3DSurface9* Surface = Target != nullptr ? Target->Surface : Ex->SavedColor;
        if (Surface == nullptr)
            return E_FAIL;
        // A texture cannot be sampled while it is the render target.
        for (unsigned long Sampler = 0; Target != nullptr && Sampler < kExSamplerCount; ++Sampler)
        {
            if (Ex->BoundTextures[Sampler] != nullptr && Ex->BoundTextures[Sampler] == Target->Texture)
            {
                Device->SetTexture(Sampler, nullptr);
                Ex->BoundTextures[Sampler] = nullptr;
            }
        }
        HRESULT Hr = Device->SetRenderTarget(0, Surface);
        if (FAILED(Hr))
        {
            ExLogf(kExFdumpWarning, "WARNING", __LINE__, "SetRenderTarget failed (hr 0x%08x)",
                   (unsigned int)(Hr));
            return Hr;
        }
        D3DSURFACE_DESC Desc;
        if (SUCCEEDED(Surface->GetDesc(&Desc)))
        {
            Ex->TargetWidth = Desc.Width;
            Ex->TargetHeight = Desc.Height;
        }
        Ex->CurrentTarget = Target;
        if (bKeepDepth && Ex->SavedDepth != nullptr)
        {
            // The scene depth cannot be sampled while it is the bound depth surface.
            for (unsigned long Sampler = 0; Sampler < kExSamplerCount; ++Sampler)
            {
                if ((Ex->SceneDepthSamplers & (1ul << Sampler)) != 0)
                {
                    Device->SetTexture(Sampler, nullptr);
                    Ex->BoundTextures[Sampler] = nullptr;
                }
            }
            Ex->SceneDepthSamplers = 0;
            Hr = Device->SetDepthStencilSurface(Ex->SavedDepth);
            Ex->bDepthBound = SUCCEEDED(Hr);
        }
        else
            ExUnbindDepth(Self);
        return S_OK;
    }

    // Binds a D3D texture on a sampler with explicit states. The first touch of a
    // fixed-function stage saves its cache entries (restored by EndExPass).
    GENRESULT ExBindSampler(FDirectX9* Self, unsigned long Sampler, IDirect3DBaseTexture9* Texture,
                            const FRPExSamplerState* State, bool bSceneDepth)
    {
        FRp9ExState* Ex = Self->Ex_;
        IDirect3DDevice9* Device = Self->Device_;
        if (bSceneDepth && Ex->bDepthBound)
            ExUnbindDepth(Self);
        const unsigned long Bit = 1ul << Sampler;
        if ((Ex->TouchedSamplers & Bit) == 0)
        {
            if (Sampler < FRp9DeviceStateCaches::StageCount)
                Self->ExSaveStage(Sampler, &Ex->SavedStages[Sampler]);
            Ex->TouchedSamplers |= Bit;
        }
        const HRESULT Hr = Device->SetTexture(Sampler, Texture);
        if (FAILED(Hr))
        {
            ExLogf(kExFdumpWarning, "WARNING", __LINE__, "SetTexture(%lu) failed (hr 0x%08x)",
                   Sampler, (unsigned int)(Hr));
            return (GENRESULT)(Hr);
        }
        Ex->BoundTextures[Sampler] = Texture;
        FRPExSamplerState Default;
        Default.MinMagFilter = RPExFilterPoint;
        Default.MipFilter = RPExMipNone;
        Default.AddressU = RPExAddressClamp;
        Default.AddressV = RPExAddressClamp;
        Default.AddressW = RPExAddressClamp;
        const FRPExSamplerState* Use = State != nullptr ? State : &Default;
        Device->SetSamplerState(Sampler, D3DSAMP_MAGFILTER, ExFilterToD3d(Use->MinMagFilter));
        Device->SetSamplerState(Sampler, D3DSAMP_MINFILTER, ExFilterToD3d(Use->MinMagFilter));
        Device->SetSamplerState(Sampler, D3DSAMP_MIPFILTER, ExMipFilterToD3d(Use->MipFilter));
        Device->SetSamplerState(Sampler, D3DSAMP_ADDRESSU, ExAddressToD3d(Use->AddressU));
        Device->SetSamplerState(Sampler, D3DSAMP_ADDRESSV, ExAddressToD3d(Use->AddressV));
        Device->SetSamplerState(Sampler, D3DSAMP_ADDRESSW, ExAddressToD3d(Use->AddressW));
        if (bSceneDepth && Texture != nullptr)
            Ex->SceneDepthSamplers |= Bit;
        else
            Ex->SceneDepthSamplers &= ~Bit;
        return GR_OK;
    }

    // EndExPass's body (also run by DestroyGpuResources on a pass left open).
    void ExEndPassInternal(FDirectX9* Self)
    {
        FRp9ExState* Ex = Self->Ex_;
        IDirect3DDevice9* Device = Self->Device_;
        if (Device != nullptr)
        {
            Device->SetVertexShader(nullptr);
            Device->SetPixelShader(nullptr);
            for (unsigned long Sampler = 0; Sampler < kExSamplerCount; ++Sampler)
            {
                if ((Ex->TouchedSamplers & (1ul << Sampler)) == 0)
                    continue;
                if (Sampler < FRp9DeviceStateCaches::StageCount)
                    Self->ExRestoreStage(Sampler, &Ex->SavedStages[Sampler]);
                else
                    Device->SetTexture(Sampler, nullptr);
            }
            if (Ex->SavedColor != nullptr)
                Device->SetRenderTarget(0, Ex->SavedColor);
            Device->SetDepthStencilSurface(Ex->SavedDepth);
            // SetRenderTarget reset the device viewport; put back the cached one RP9's
            // SetViewportRect / SetDepthRange last sent, with its validity bytes.
            Self->ViewportCache_ = Ex->SavedViewport;
            Self->bViewportRectValid_ = Ex->bSavedViewportRectValid;
            Self->bDepthRangeValid_ = Ex->bSavedDepthRangeValid;
            Device->SetViewport(&Self->ViewportCache_);
        }
        Self->RenderStates_.RestorePending(Ex->SavedPending);
        if (Ex->SavedColor != nullptr)
        {
            Ex->SavedColor->Release();
            Ex->SavedColor = nullptr;
        }
        if (Ex->SavedDepth != nullptr)
        {
            Ex->SavedDepth->Release();
            Ex->SavedDepth = nullptr;
        }
        Ex->TouchedSamplers = 0;
        Ex->SceneDepthSamplers = 0;
        memset(Ex->BoundTextures, 0, sizeof(Ex->BoundTextures));
        Ex->BoundProgram = nullptr;
        Ex->CurrentTarget = nullptr;
        Ex->bDepthBound = false;
        Ex->bInPass = false;
    }

    // The IRPDraw::DrawPrimitive sequence: the stream cache selects the FVF, FlushDrawState
    // sends the queued render states (the pass's included), DrawPrimitiveUP, stream cache reset.
    GENRESULT ExDrawUp(FDirectX9* Self, unsigned long Prim, unsigned long Fvf, const void* Verts,
                       unsigned long Count)
    {
        const unsigned int PrimCount = Self->PrimCountFromVerts(Prim, (unsigned int)(Count));
        if ((int)(PrimCount) <= 0 || Verts == nullptr)
            return GR_INVALID_PARMS;
        Self->StreamCache_.SelectVertexStream(nullptr, Fvf);
        Self->FlushDrawState(Self->Device_);
        const HRESULT Hr = Self->Device_->DrawPrimitiveUP((D3DPRIMITIVETYPE)(Prim), PrimCount, Verts,
                                                          Self->FvfStride((unsigned int)(Fvf)));
        // D3D9 DrawPrimitiveUP leaves stream 0 unbound; the cache forgets it as DrawPrimitive does.
        Self->StreamCache_.Reset();
        if (FAILED(Hr))
            ExLogf(kExFdumpWarning, "WARNING", __LINE__, "DrawPrimitiveUP failed (hr 0x%08x)",
                   (unsigned int)(Hr));
        return (GENRESULT)(Hr);
    }

    GENRESULT NeedExPass(FDirectX9* Self, const char* Method)
    {
        if (Self->Ex_->bInPass && Self->Device_ != nullptr)
            return GR_OK;
        ExLogf(kExFdumpWarning, "WARNING", __LINE__, "%s() called outside of an Ex pass", Method);
        return GR_GENERIC;
    }
}

FRp9ExState::FRp9ExState()
    : SceneDepth(), FallbackDepth(nullptr), BackBufferWidth(0), BackBufferHeight(0),
      bInPass(false), bDepthBound(false), SavedColor(nullptr), SavedDepth(nullptr),
      bSavedViewportRectValid(0), bSavedDepthRangeValid(0), TouchedSamplers(0),
      SceneDepthSamplers(0), BoundProgram(nullptr), CurrentTarget(nullptr), TargetWidth(0),
      TargetHeight(0)
{
    SceneDepth.Kind = Rp9ExTextureSceneDepth;
    SceneDepth.Format = RPExFormatR32F;
    memset(&SavedViewport, 0, sizeof(SavedViewport));
    memset(SavedPending, 0, sizeof(SavedPending));
    memset(SavedStages, 0, sizeof(SavedStages));
    memset(BoundTextures, 0, sizeof(BoundTextures));
}

// ---- lifetime hooks (called from the RP8-mirrored code in RP9.cpp) ----

bool FDirectX9::ExWantsIntzDepth(D3DFORMAT AdapterFormat, D3DFORMAT DepthFormat)
{
    // Only where INTZ keeps the picked bit depth: INTZ is 24-bit depth + 8-bit stencil. A D16
    // or D32 choice keeps today's auto depth-stencil (and reports no scene depth).
    if (D3d_ == nullptr)
        return false;
    if (DepthFormat != D3DFMT_D24S8 && DepthFormat != D3DFMT_D24X8 &&
        DepthFormat != D3DFMT_D24X4S4)
        return false;
    return SUCCEEDED(D3d_->CheckDeviceFormat(AdapterIndex_, (D3DDEVTYPE)(DeviceType_),
                                             AdapterFormat, D3DUSAGE_DEPTHSTENCIL,
                                             D3DRTYPE_TEXTURE, kIntzFormat));
}

HRESULT FDirectX9::ExCreateDeviceResources(bool bIntzDepth, D3DFORMAT DepthFormat)
{
    FRp9ExState* Ex = Ex_;
    IDirect3DSurface9* BackBuffer = nullptr;
    if (SUCCEEDED(Device_->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &BackBuffer)))
    {
        D3DSURFACE_DESC Desc;
        if (SUCCEEDED(BackBuffer->GetDesc(&Desc)))
        {
            Ex->BackBufferWidth = Desc.Width;
            Ex->BackBufferHeight = Desc.Height;
        }
        BackBuffer->Release();
    }

    if (bIntzDepth)
    {
        // The device was made with EnableAutoDepthStencil = FALSE: the INTZ texture's level 0
        // is the depth surface from now on (same size as the back buffer, no multisampling).
        IDirect3DTexture9* Texture = nullptr;
        IDirect3DSurface9* Surface = nullptr;
        HRESULT Hr = Device_->CreateTexture((UINT)(Ex->BackBufferWidth),
                                            (UINT)(Ex->BackBufferHeight), 1, D3DUSAGE_DEPTHSTENCIL,
                                            kIntzFormat, D3DPOOL_DEFAULT, &Texture, nullptr);
        if (SUCCEEDED(Hr))
            Hr = Texture->GetSurfaceLevel(0, &Surface);
        if (SUCCEEDED(Hr))
        {
            Ex->SceneDepth.Texture = Texture;
            Ex->SceneDepth.Surface = Surface;
            Ex->SceneDepth.Width = Ex->BackBufferWidth;
            Ex->SceneDepth.Height = Ex->BackBufferHeight;
            Hr = Device_->SetDepthStencilSurface(Surface);
            ExLogf(kExFdumpNotice, "NOTICE", __LINE__, "scene depth: INTZ %lux%lu",
                   Ex->BackBufferWidth, Ex->BackBufferHeight);
        }
        else
        {
            if (Texture != nullptr)
                Texture->Release();
            ExLogf(kExFdumpWarning, "WARNING", __LINE__,
                   "INTZ depth texture failed (hr 0x%08x); plain depth surface instead, no scene "
                   "depth", (unsigned int)(Hr));
            Hr = Device_->CreateDepthStencilSurface((UINT)(Ex->BackBufferWidth),
                                                    (UINT)(Ex->BackBufferHeight), DepthFormat,
                                                    D3DMULTISAMPLE_NONE, 0, FALSE,
                                                    &Ex->FallbackDepth, nullptr);
            if (SUCCEEDED(Hr))
                Hr = Device_->SetDepthStencilSurface(Ex->FallbackDepth);
        }
        if (FAILED(Hr))
            return Hr;
    }

    for (size_t Index = 0; Index < Ex->Programs.size(); ++Index)
        CreateProgramShaders(Device_, Ex->Programs[Index]);
    for (size_t Index = 0; Index < Ex->Textures.size(); ++Index)
    {
        FRPExTexture* Texture = Ex->Textures[Index];
        if (Texture->Texture != nullptr)
            continue;
        if (Texture->Kind == Rp9ExTextureRenderTarget)
            CreateRenderTargetGpu(Device_, Texture, Ex->BackBufferWidth, Ex->BackBufferHeight);
        else if (Texture->Kind == Rp9ExTextureVolume)
            CreateVolumeGpu(Device_, Texture);
    }
    return S_OK;
}

void FDirectX9::ExReleaseDeviceResources()
{
    FRp9ExState* Ex = Ex_;
    if (Ex == nullptr)
        return;
    if (Ex->bInPass)
        ExEndPassInternal(this);
    // The device must not hold the surface being released (Reset requires every
    // D3DPOOL_DEFAULT resource gone).
    if (Device_ != nullptr && (Ex->SceneDepth.Surface != nullptr || Ex->FallbackDepth != nullptr))
        Device_->SetDepthStencilSurface(nullptr);
    ReleaseExTextureGpu(&Ex->SceneDepth);
    if (Ex->FallbackDepth != nullptr)
    {
        Ex->FallbackDepth->Release();
        Ex->FallbackDepth = nullptr;
    }
    for (size_t Index = 0; Index < Ex->Textures.size(); ++Index)
    {
        if (Ex->Textures[Index]->Kind == Rp9ExTextureRenderTarget)
            ReleaseExTextureGpu(Ex->Textures[Index]);
    }
}

void FDirectX9::ExReleaseAllGpu()
{
    FRp9ExState* Ex = Ex_;
    if (Ex == nullptr)
        return;
    ExReleaseDeviceResources();
    for (size_t Index = 0; Index < Ex->Programs.size(); ++Index)
        ReleaseProgramShaders(Ex->Programs[Index]);
    for (size_t Index = 0; Index < Ex->Textures.size(); ++Index)
        ReleaseExTextureGpu(Ex->Textures[Index]);
}

void FDirectX9::ExDestroyAll()
{
    FRp9ExState* Ex = Ex_;
    if (Ex == nullptr)
        return;
    ExReleaseAllGpu();
    for (size_t Index = 0; Index < Ex->Programs.size(); ++Index)
    {
        FRPExProgram* Program = Ex->Programs[Index];
        if (Program->FileSystem != nullptr)
            Program->FileSystem->Release();
        delete Program;
    }
    Ex->Programs.clear();
    for (size_t Index = 0; Index < Ex->Textures.size(); ++Index)
        delete Ex->Textures[Index];
    Ex->Textures.clear();
}

// ---- IRenderPipelineEx ----

GENRESULT DACOM_API FDirectX9::GetExCaps(FRPExCaps* Out)
{
    // Size-versioned (FRPExCaps): a caller built against an older header passes its smaller
    // Size and gets only the fields it knows about.
    if (Out == nullptr || Out->Size < RPExCapsSizeV1)
        return GR_INVALID_PARMS;
    if (Device_ == nullptr)
        return GR_GENERIC;
    D3DCAPS9 Caps;
    memset(&Caps, 0, sizeof(Caps));
    const HRESULT Hr = Device_->GetDeviceCaps(&Caps);
    if (FAILED(Hr))
        return (GENRESULT)(Hr);
    FRPExCaps Full;
    memset(&Full, 0, sizeof(Full));
    Full.VertexShaderVersion = (D3DSHADER_VERSION_MAJOR(Caps.VertexShaderVersion) << 8) |
                               D3DSHADER_VERSION_MINOR(Caps.VertexShaderVersion);
    Full.PixelShaderVersion = (D3DSHADER_VERSION_MAJOR(Caps.PixelShaderVersion) << 8) |
                              D3DSHADER_VERSION_MINOR(Caps.PixelShaderVersion);
    Full.MaxTextureWidth = Caps.MaxTextureWidth;
    Full.MaxTextureHeight = Caps.MaxTextureHeight;
    Full.MaxVolumeExtent = Caps.MaxVolumeExtent;
    Full.bSceneDepth = Ex_->SceneDepth.Texture != nullptr ? 1 : 0;
    Full.bVolumeTextures = (Caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) != 0 ? 1 : 0;
    const UINT Adapter = AdapterIndex_;
    D3DDISPLAYMODE Mode;
    if (SUCCEEDED(Device_->GetDisplayMode(0, &Mode)))
    {
        const D3DDEVTYPE Type = (D3DDEVTYPE)(DeviceType_);
        Full.bFloat16Target =
            SUCCEEDED(D3d_->CheckDeviceFormat(Adapter, Type, Mode.Format, D3DUSAGE_RENDERTARGET,
                                              D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F)) ? 1 : 0;
        Full.bFloat16Blending =
            SUCCEEDED(D3d_->CheckDeviceFormat(
                Adapter, Type, Mode.Format,
                D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE,
                D3DFMT_A16B16G16R16F)) ? 1 : 0;
        Full.bFloat32Target =
            SUCCEEDED(D3d_->CheckDeviceFormat(Adapter, Type, Mode.Format, D3DUSAGE_RENDERTARGET,
                                              D3DRTYPE_TEXTURE, D3DFMT_R32F)) ? 1 : 0;
    }
    // Version 2: shader instruction budgets and the adapter name.
    Full.MaxVertexShaderSlots = Caps.MaxVertexShader30InstructionSlots;
    Full.MaxPixelShaderSlots = Caps.MaxPixelShader30InstructionSlots;
    Full.MaxPixelShaderInstructionsExecuted = Caps.MaxPShaderInstructionsExecuted;
    D3DADAPTER_IDENTIFIER9 Identifier;
    memset(&Identifier, 0, sizeof(Identifier));
    if (D3d_ != nullptr && SUCCEEDED(D3d_->GetAdapterIdentifier(Adapter, 0, &Identifier)))
    {
        strncpy(Full.AdapterDescription, Identifier.Description,
                sizeof(Full.AdapterDescription) - 1);
        Full.AdapterDescription[sizeof(Full.AdapterDescription) - 1] = '\0';
    }
    const unsigned long Filled =
        Out->Size < (unsigned long)(sizeof(FRPExCaps)) ? Out->Size
                                                       : (unsigned long)(sizeof(FRPExCaps));
    Full.Size = Filled;
    memcpy(Out, &Full, Filled);
    return GR_OK;
}

GENRESULT DACOM_API FDirectX9::CreateExProgram(IFileSystem* FileSystem, const char* Path,
                                               const char* VsEntry, const char* PsEntry,
                                               FRPExProgram** Out)
{
    if (Out == nullptr)
        return GR_INVALID_PARMS;
    *Out = nullptr;
    if (FileSystem == nullptr || Path == nullptr || VsEntry == nullptr || PsEntry == nullptr)
        return GR_INVALID_PARMS;
    FRPExProgram* Program = new FRPExProgram();
    Program->FileSystem = FileSystem;
    FileSystem->AddRef();
    Program->Path = Path;
    Program->VsEntry = VsEntry;
    Program->PsEntry = PsEntry;
    Program->Vs = nullptr;
    Program->Ps = nullptr;
    Ex_->Programs.push_back(Program);
    *Out = Program;
    // Without a device only the bytecode is made; ExCreateDeviceResources makes the shaders.
    return CompileProgram(Device_, Program) ? GR_OK : GR_GENERIC;
}

GENRESULT DACOM_API FDirectX9::DestroyExProgram(FRPExProgram* Program)
{
    FRp9ExState* Ex = Ex_;
    for (size_t Index = 0; Index < Ex->Programs.size(); ++Index)
    {
        if (Ex->Programs[Index] != Program)
            continue;
        if (Ex->bInPass && Ex->BoundProgram == Program && Device_ != nullptr)
        {
            Device_->SetVertexShader(nullptr);
            Device_->SetPixelShader(nullptr);
            Ex->BoundProgram = nullptr;
        }
        ReleaseProgramShaders(Program);
        if (Program->FileSystem != nullptr)
            Program->FileSystem->Release();
        Ex->Programs.erase(Ex->Programs.begin() + (std::ptrdiff_t)(Index));
        delete Program;
        return GR_OK;
    }
    return GR_INVALID_PARMS;
}

GENRESULT DACOM_API FDirectX9::ReloadExPrograms()
{
    FRp9ExState* Ex = Ex_;
    bool bAllOk = true;
    for (size_t Index = 0; Index < Ex->Programs.size(); ++Index)
    {
        FRPExProgram* Program = Ex->Programs[Index];
        if (!CompileProgram(Device_, Program))
            bAllOk = false;
        // A reload inside a pass rebinds the (possibly new) shaders of the bound program.
        if (Ex->bInPass && Ex->BoundProgram == Program && Device_ != nullptr)
        {
            Device_->SetVertexShader(Program->Vs);
            Device_->SetPixelShader(Program->Ps);
        }
    }
    return bAllOk ? GR_OK : GR_GENERIC;
}

GENRESULT DACOM_API FDirectX9::CreateExRenderTarget(const FRPExRenderTargetDesc* Desc,
                                                    FRPExTexture** Out)
{
    if (Out == nullptr || Desc == nullptr)
        return GR_INVALID_PARMS;
    *Out = nullptr;
    unsigned long Bytes = 0;
    if (ExFormatToD3d(Desc->Format, &Bytes) == D3DFMT_UNKNOWN)
        return GR_INVALID_PARMS;
    if ((Desc->Width == 0 || Desc->Height == 0) && !(Desc->BackBufferScale > 0.0f))
        return GR_INVALID_PARMS;
    FRPExTexture* Target = new FRPExTexture();
    Target->Kind = Rp9ExTextureRenderTarget;
    Target->Format = Desc->Format;
    Target->TargetDesc = *Desc;
    if (Device_ != nullptr && (DeviceReady_ & 1) != 0)
    {
        const HRESULT Hr =
            CreateRenderTargetGpu(Device_, Target, Ex_->BackBufferWidth, Ex_->BackBufferHeight);
        if (FAILED(Hr))
        {
            delete Target;
            return (GENRESULT)(Hr);
        }
    }
    Ex_->Textures.push_back(Target);
    *Out = Target;
    return GR_OK;
}

GENRESULT DACOM_API FDirectX9::CreateExVolumeTexture(unsigned long Width, unsigned long Height,
                                                     unsigned long Depth, ERPExFormat Format,
                                                     const void* Data, FRPExTexture** Out)
{
    if (Out == nullptr)
        return GR_INVALID_PARMS;
    *Out = nullptr;
    unsigned long Bytes = 0;
    if (ExFormatToD3d(Format, &Bytes) == D3DFMT_UNKNOWN || Width == 0 || Height == 0 || Depth == 0)
        return GR_INVALID_PARMS;
    FRPExTexture* Volume = new FRPExTexture();
    Volume->Kind = Rp9ExTextureVolume;
    Volume->Format = Format;
    Volume->Width = Width;
    Volume->Height = Height;
    Volume->Depth = Depth;
    if (Data != nullptr)
    {
        const unsigned char* Source = (const unsigned char*)(Data);
        Volume->VolumeData.assign(Source, Source + Width * Height * Depth * Bytes);
    }
    if (Device_ != nullptr)
    {
        const HRESULT Hr = CreateVolumeGpu(Device_, Volume);
        if (FAILED(Hr))
        {
            delete Volume;
            return (GENRESULT)(Hr);
        }
    }
    Ex_->Textures.push_back(Volume);
    *Out = Volume;
    return GR_OK;
}

GENRESULT DACOM_API FDirectX9::DestroyExTexture(FRPExTexture* Texture)
{
    FRp9ExState* Ex = Ex_;
    if (Ex->bInPass)
    {
        ExLogf(kExFdumpWarning, "WARNING", __LINE__, "destroy_ex_texture() inside an Ex pass");
        return GR_GENERIC;
    }
    for (size_t Index = 0; Index < Ex->Textures.size(); ++Index)
    {
        if (Ex->Textures[Index] != Texture)
            continue;
        ReleaseExTextureGpu(Texture);
        Ex->Textures.erase(Ex->Textures.begin() + (std::ptrdiff_t)(Index));
        delete Texture;
        return GR_OK;
    }
    return GR_INVALID_PARMS;
}

GENRESULT DACOM_API FDirectX9::GetExTextureSize(FRPExTexture* Texture, unsigned long* OutWidth,
                                                unsigned long* OutHeight)
{
    if (Texture == nullptr || OutWidth == nullptr || OutHeight == nullptr)
        return GR_INVALID_PARMS;
    if (Texture->Texture == nullptr)
        return GR_GENERIC;
    *OutWidth = Texture->Width;
    *OutHeight = Texture->Height;
    return GR_OK;
}

GENRESULT DACOM_API FDirectX9::GetSceneDepth(FRPExTexture** Out)
{
    if (Out == nullptr)
        return GR_INVALID_PARMS;
    *Out = nullptr;
    if (Ex_->SceneDepth.Texture == nullptr)
        return GR_GENERIC;
    *Out = &Ex_->SceneDepth;
    return GR_OK;
}

GENRESULT DACOM_API FDirectX9::BeginExPass(const FRPExPassDesc* Desc)
{
    const GENRESULT Ready =
        NeedBuffers("begin_ex_pass() called outside of create_buffers/destroy_buffers", __LINE__);
    if (Ready != GR_OK)
        return Ready;
    FRp9ExState* Ex = Ex_;
    if (Ex->bInPass)
    {
        ExLogf(kExFdumpWarning, "WARNING", __LINE__, "begin_ex_pass(): Ex passes do not nest");
        return GR_GENERIC;
    }
    if (Desc == nullptr)
        return GR_INVALID_PARMS;
    FRPExTexture* Target = Desc->ColorTarget;
    if (Target != nullptr && (Target->Kind != Rp9ExTextureRenderTarget || Target->Surface == nullptr))
        return GR_INVALID_PARMS;

    IDirect3DSurface9* Color = nullptr;
    const HRESULT ColorHr = Device_->GetRenderTarget(0, &Color);
    if (FAILED(ColorHr))
    {
        ExLogf(kExFdumpWarning, "WARNING", __LINE__, "GetRenderTarget failed (hr 0x%08x)",
               (unsigned int)(ColorHr));
        return (GENRESULT)(ColorHr);
    }
    IDirect3DSurface9* Depth = nullptr;
    if (FAILED(Device_->GetDepthStencilSurface(&Depth))) // D3DERR_NOTFOUND: none bound
        Depth = nullptr;
    Ex->SavedColor = Color;
    Ex->SavedDepth = Depth;
    Ex->SavedViewport = ViewportCache_;
    Ex->bSavedViewportRectValid = bViewportRectValid_;
    Ex->bSavedDepthRangeValid = bDepthRangeValid_;
    for (unsigned int StateId = 0; StateId < FRp9RenderStateCache::StateCount; ++StateId)
        Ex->SavedPending[StateId] = RenderStates_.Entries[StateId].Pending;
    Ex->TouchedSamplers = 0;
    Ex->SceneDepthSamplers = 0;
    memset(Ex->BoundTextures, 0, sizeof(Ex->BoundTextures));
    Ex->BoundProgram = nullptr;
    Ex->CurrentTarget = nullptr;
    Ex->bDepthBound = Depth != nullptr;
    Ex->bInPass = true;

    // The pass's starting render states (through the cache; EndExPass re-queues the old ones).
    ExSetPassState(this, D3DRS_ALPHABLENDENABLE, FALSE);
    ExSetPassState(this, D3DRS_ALPHATESTENABLE, FALSE);
    ExSetPassState(this, D3DRS_FOGENABLE, FALSE);
    ExSetPassState(this, D3DRS_STENCILENABLE, FALSE);
    ExSetPassState(this, D3DRS_CULLMODE, D3DCULL_NONE);
    ExSetPassState(this, D3DRS_LIGHTING, FALSE);
    ExSetPassState(this, D3DRS_COLORWRITEENABLE, 0xF);

    const HRESULT Hr = ExSelectTarget(this, Target, (Desc->Flags & RPExPassKeepDepth) != 0);
    if (FAILED(Hr))
    {
        ExEndPassInternal(this);
        return (GENRESULT)(Hr);
    }
    if ((Desc->Flags & RPExPassClearColor) != 0)
    {
        const HRESULT ClearHr = Device_->Clear(0, nullptr, D3DCLEAR_TARGET, Desc->ClearColor, 1.0f, 0);
        if (FAILED(ClearHr))
            ExLogf(kExFdumpWarning, "WARNING", __LINE__, "Clear failed (hr 0x%08x)",
                   (unsigned int)(ClearHr));
    }
    return GR_OK;
}

GENRESULT DACOM_API FDirectX9::EndExPass()
{
    if (!Ex_->bInPass)
    {
        ExLogf(kExFdumpWarning, "WARNING", __LINE__, "end_ex_pass() without begin_ex_pass()");
        return GR_GENERIC;
    }
    ExEndPassInternal(this);
    return GR_OK;
}

GENRESULT DACOM_API FDirectX9::SetExRenderTarget(FRPExTexture* ColorTarget,
                                                 unsigned long bKeepDepth)
{
    const GENRESULT Ready = NeedExPass(this, "set_ex_render_target");
    if (Ready != GR_OK)
        return Ready;
    if (ColorTarget != nullptr &&
        (ColorTarget->Kind != Rp9ExTextureRenderTarget || ColorTarget->Surface == nullptr))
        return GR_INVALID_PARMS;
    return (GENRESULT)(ExSelectTarget(this, ColorTarget, bKeepDepth != 0));
}

GENRESULT DACOM_API FDirectX9::BindExProgram(FRPExProgram* Program)
{
    const GENRESULT Ready = NeedExPass(this, "bind_ex_program");
    if (Ready != GR_OK)
        return Ready;
    if (Program == nullptr)
    {
        Device_->SetVertexShader(nullptr);
        Device_->SetPixelShader(nullptr);
        Ex_->BoundProgram = nullptr;
        return GR_OK;
    }
    // A program that never compiled has no shaders; its compile errors are already logged.
    if (Program->Vs == nullptr || Program->Ps == nullptr)
        return GR_GENERIC;
    Device_->SetVertexShader(Program->Vs);
    Device_->SetPixelShader(Program->Ps);
    Ex_->BoundProgram = Program;
    return GR_OK;
}

GENRESULT DACOM_API FDirectX9::SetExVertexConstants(unsigned long StartRegister, const float* Data,
                                                    unsigned long Vector4Count)
{
    if (Device_ == nullptr || Data == nullptr)
        return GR_GENERIC;
    return (GENRESULT)(Device_->SetVertexShaderConstantF((UINT)(StartRegister), Data,
                                                         (UINT)(Vector4Count)));
}

GENRESULT DACOM_API FDirectX9::SetExPixelConstants(unsigned long StartRegister, const float* Data,
                                                   unsigned long Vector4Count)
{
    if (Device_ == nullptr || Data == nullptr)
        return GR_GENERIC;
    return (GENRESULT)(Device_->SetPixelShaderConstantF((UINT)(StartRegister), Data,
                                                        (UINT)(Vector4Count)));
}

GENRESULT DACOM_API FDirectX9::BindExTexture(unsigned long Sampler, FRPExTexture* Texture,
                                             const FRPExSamplerState* State)
{
    const GENRESULT Ready = NeedExPass(this, "bind_ex_texture");
    if (Ready != GR_OK)
        return Ready;
    if (Sampler >= kExSamplerCount)
        return GR_INVALID_PARMS;
    if (Texture != nullptr)
    {
        if (Texture->Texture == nullptr)
            return GR_GENERIC; // lost render target, or no scene depth
        if (Texture == Ex_->CurrentTarget)
        {
            ExLogf(kExFdumpWarning, "WARNING", __LINE__,
                   "bind_ex_texture(): the texture is the current render target");
            return GR_GENERIC;
        }
    }
    const bool bSceneDepth = Texture != nullptr && Texture == &Ex_->SceneDepth;
    return ExBindSampler(this, Sampler, Texture != nullptr ? Texture->Texture : nullptr, State,
                         bSceneDepth);
}

GENRESULT DACOM_API FDirectX9::BindExPipelineTexture(unsigned long Sampler, void* Handle,
                                                     const FRPExSamplerState* State)
{
    const GENRESULT Ready = NeedExPass(this, "bind_ex_pipeline_texture");
    if (Ready != GR_OK)
        return Ready;
    if (Sampler >= kExSamplerCount)
        return GR_INVALID_PARMS;
    IDirect3DBaseTexture9* Texture = PipelineTextureFromHandle(Handle);
    if (Handle != nullptr && Texture == nullptr)
        return GR_GENERIC;
    return ExBindSampler(this, Sampler, Texture, State, false);
}

// ---- IRPDrawEx ----

GENRESULT DACOM_API FDirectX9::DrawFullscreenQuad()
{
    const GENRESULT Ready = NeedExPass(this, "draw_fullscreen_quad");
    if (Ready != GR_OK)
        return Ready;
    if (Ex_->BoundProgram == nullptr || Ex_->TargetWidth == 0 || Ex_->TargetHeight == 0)
        return GR_GENERIC;
    // Direct3D 9 puts pixel centres on integer coordinates, texel centres sit half a texel in:
    // shifting the quad by half a pixel left and up (1/W, 1/H in clip units) makes pixel (i, j)
    // sample UV ((i + 0.5) / W, (j + 0.5) / H) exactly.
    const float HalfX = 1.0f / (float)(Ex_->TargetWidth);
    const float HalfY = 1.0f / (float)(Ex_->TargetHeight);
    struct FQuadVertex
    {
        float X;
        float Y;
        float Z;
        float U;
        float V;
    };
    const FQuadVertex Quad[4] = {
        {-1.0f - HalfX, 1.0f + HalfY, 0.0f, 0.0f, 0.0f},
        {1.0f - HalfX, 1.0f + HalfY, 0.0f, 1.0f, 0.0f},
        {-1.0f - HalfX, -1.0f + HalfY, 0.0f, 0.0f, 1.0f},
        {1.0f - HalfX, -1.0f + HalfY, 0.0f, 1.0f, 1.0f},
    };
    return ExDrawUp(this, D3DPT_TRIANGLESTRIP, D3DFVF_XYZ | D3DFVF_TEX1, Quad, 4);
}

GENRESULT DACOM_API FDirectX9::DrawExPrimitiveUp(unsigned long Prim, unsigned long Fvf,
                                                 const void* Verts, unsigned long Count)
{
    const GENRESULT Ready = NeedExPass(this, "draw_ex_primitive_up");
    if (Ready != GR_OK)
        return Ready;
    if (Ex_->BoundProgram == nullptr)
        return GR_GENERIC;
    return ExDrawUp(this, Prim, Fvf, Verts, Count);
}
