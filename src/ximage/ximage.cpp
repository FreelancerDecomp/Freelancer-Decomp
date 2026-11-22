// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "dacom.h"
#include "msvcrt_compat.h" // MsvcrtFtol
#include "ximage.h"
#include "render/factory_names.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdarg.h>
#include <stdio.h>

// Tiled images submitted through the caller's draw callback.
// Intermediate floating-point conversions preserve the existing rounding sites.

namespace
{
    void* const kRegisterPriority = (void*)0x40000000;
    const FlULong kBigImageDescSize = 0x2C;
    const unsigned int kFormatScratchSize = 0x2000; // FUN_06f81a40 _vsnprintf cap
    const char kBigImageCpp[] =
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\ximage\\bigimage.cpp"; // 06f84060
    const char kWarningFormat[] = "%s(%d) : WARNING:General:%s";                  // 06f84040
    const char kNoticeFormat[] = "%s(%d) : NOTICE:General:%s";                    // 06f840d4
    const char kSetLevelFailed[] = " failed to set texture level data ";          // 06f8401c
    const char kAllocateFailed[] = "Failed to allocate %d by %d, %d bpp texture."; // 06f840a4

    // DAT_06f84150 (22 PixelFormat ids, returned by FUN_06f828f0).
    const FlULong kFormatIds[22] = {
        0,
        0x29,
        0x14,
        0x17,
        0x18,
        0x1A,
        0x19,
        0x15,
        0x16,
        0,
        0x31545844,
        0x32545844,
        0x33545844,
        0x34545844,
        0x35545844,
        0x504F4144,
        0x544F4144,
        0x41414144,
        0x4C414144,
        0x31414144,
        0x34414144,
        0x38414144};

    // DAT_06f841a8: 15 records of 0x4C bytes; ximage only reads record+4 (bits per pixel).
    const FlULong kFormatBits[15] = {0, 8, 24, 16, 16, 16, 16, 32, 32, 0, 0, 0, 0, 0, 0};

    // FUN_06f82900: index of Id in DAT_06f84150, 0 when absent.
    int FormatIdToIndex(FlULong Id)
    {
        int Index = 0;
        while (Index < 22)
        {
            if (kFormatIds[Index] == Id)
                return Index;
            Index = Index + 1;
        }
        return 0;
    }

    // FUN_06f82920 + record+4: CMP 0xE / JLE -> record[Value]; else record 0 (bpp 0).
    FlULong FormatBits(FlULong Value)
    {
        FlULong Index = 0;
        if (Value <= 14)
            Index = Value;
        return kFormatBits[Index];
    }

    // Every caller turns the bit count into bytes the same way: (bits + 7) >> 3.
    FlULong FormatBytes(const FlULong* Format)
    {
        return (FormatBits(*Format) + 7u) >> 3;
    }

    // FUN_06f81a70 thiscall RET 4: *this = *param (one dword). Used to pass a PixelFormat by value.
    void CopyDword(FlULong* Dest, const FlULong* Src)
    {
        *Dest = *Src;
    }

    // FUN_06f81a40: _vsnprintf(Buffer, 0x2000, Format, ...) then return Buffer.
    char* FormatScratch(char* Buffer, const char* Format, ...)
    {
        va_list Args;
        va_start(Args, Format);
        _vsnprintf(Buffer, kFormatScratchSize, Format, Args);
        va_end(Args);
        return Buffer;
    }

    // FUN_06f81180 allocation-failure FDUMP: the message carries the WHOLE image size and a
    // literal 0x10 bpp (PUSH 0x10), not the tile that failed.
    void DumpAllocateFail(int Line, FlULong Width, FlULong Height)
    {
        char Message[kFormatScratchSize];
        FDUMP(0x100002, kWarningFormat, kBigImageCpp, Line,
              FormatScratch(Message, kAllocateFailed, Width, Height, 0x10));
    }

    void DumpSetLevelWarning(int Line)
    {
        FDUMP(0x100002, kWarningFormat, kBigImageCpp, Line, kSetLevelFailed);
    }

    void DumpSetLevelNotice(int Line)
    {
        FDUMP(0x100003, kNoticeFormat, kBigImageCpp, Line, kSetLevelFailed);
    }

    // FUN_06f81a10: round up to a power of two (POT input stays, 0 -> 0). SAR, not SHR.
    FlLong RoundUpPot(FlULong Value)
    {
        FlLong Work = (FlLong)(Value);
        FlLong Out = 1;
        if ((Work & (Work - 1)) == 0)
        {
            if (Work == 0)
                return Out >> 1;
            while (Work != 0)
            {
                Work = Work >> 1;
                Out = Out << 1;
            }
            return Out >> 1;
        }
        if (Work == 0)
            return Out;
        while (Work != 0)
        {
            Work = Work >> 1;
            Out = Out << 1;
        }
        return Out;
    }

    // FUN_06f822d0: 0 for an empty rect; otherwise swap inverted edges and return 1.
    int NormalizeRect(RECT* Rect)
    {
        const FlLong Width = Rect->right - Rect->left;
        if (Width == 0)
            return 0;
        const FlLong Height = Rect->bottom - Rect->top;
        if (Height == 0)
            return 0;
        if (Width < 0)
        {
            const FlLong Left = Rect->left;
            Rect->left = Rect->right;
            Rect->right = Left;
        }
        if (Height <= 0)
        {
            const FlLong Top = Rect->top;
            Rect->top = Rect->bottom;
            Rect->bottom = Top;
        }
        return 1;
    }

    // FUN_06f82740: smart-pointer reset (Release + null) of an interface slot.
    template <typename TInterface>
    void ReleaseInterface(TInterface** Slot)
    {
        if (*Slot != nullptr)
        {
            (*Slot)->Release();
            *Slot = nullptr;
        }
    }

    GENRESULT CreateTexture(IRenderPipeline* Pipeline, FlLong Width, FlLong Height, FlULong* Format,
                            void** OutHandle)
    {
        // OutHandle receives a pointer to RP8's 8-byte wrapper, not IDirect3DTexture8*.
        // Every retail call passes Levels = 1, Flags = 0.
        return Pipeline->CreateTexture((int)(Width), (int)(Height),
                                                   (unsigned int*)(Format), 1, 0,
                                                   OutHandle);
    }

    GENRESULT DestroyTexture(IRenderPipeline* Pipeline, void* Handle)
    {
        return Pipeline->DestroyTexture(Handle);
    }

    GENRESULT SetTextureLevelData(IRenderPipeline* Pipeline, void* Handle, FlULong SrcW,
                                  FlULong SrcH, FlULong Pitch, FlULong* Format,
                                  const void* Pixels)
    {
        // Every retail call: level 0, trailing two arguments 0.
        return Pipeline->SetTextureLevelData(
            Handle, 0, (int)(SrcW), (int)(SrcH),
            (unsigned int)(Pitch), (unsigned int*)(Format), Pixels,
            nullptr, nullptr);
    }

    // 0x48-byte object (operator_new(0x48) @06f82445).
    class FBigImage : public IBigImage2
    {
    public:
        FBigImage()
            : Pipeline_(nullptr)
            , TextureLibrary_(nullptr)
            , Handles_(nullptr)
            , Uvs_(nullptr)
            , TileCount_(0)
            , TilesX_(0)
            , TilesY_(0)
            , Width_(0)
            , Height_(0)
            , PackedRgb_(0x00FFFFFF)
            , Alpha_(0xFF)
            , Pad0_(0)
            , Pad1_(0)
            , Pad2_(0)
            , FormatId_((FlULong)(FormatIdToIndex(0)))
            , DrawTile_(nullptr)
            , AbilitySquareOnly_(0)
            , AbilityMaxWidth_(0)
            , AbilityMaxHeight_(0)
            , RefCount_(1)
        {
        }

        ~FBigImage()
        {
            if (Handles_ != nullptr)
            {
                FlLong Index = 0;
                while (Index < (FlLong)(TileCount_))
                {
                    if (Handles_[Index] != nullptr)
                        DestroyTexture(Pipeline_, Handles_[Index]);
                    Index = Index + 1;
                }
            }
            // Delete the byte arrays through their allocation type.
            delete[] (unsigned char*)Handles_;
            delete[] (unsigned char*)Uvs_;
            ReleaseInterface(&TextureLibrary_);
            ReleaseInterface(&Pipeline_);
        }

        // LAB_06f82760: {IID, offset} table 06f830e4 (IBigImage2, IBigImage, null terminator).
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            struct FQiEntry
            {
                const char* Iid;
                FlULong ByteOffset;
            };
            static const FQiEntry Table[] = {
                {Iids::BigImage2, 0}, // 06f830e4 / 06f830e8
                {Iids::BigImage, 0},  // 06f830ec / 06f830f0
                {nullptr, 0},         // 06f830f4
            };

            unsigned int Index = 0;
            while (Table[Index].Iid != nullptr)
            {
                if (std::strcmp(Table[Index].Iid, Iid) == 0)
                {
                    unsigned char* Base = (unsigned char*)(this);
                    const FlULong Offset = Table[Index].ByteOffset;
                    IDAComponent* Face = nullptr;
                    if ((Offset & 0x80000000u) != 0)
                        Face = *(IDAComponent**)(Base + (Offset & 0x7FFFFFFFu));
                    else
                        Face = (IDAComponent*)(Base + Offset);
                    Face->AddRef();
                    *Out = Face;
                    return GR_OK;
                }
                Index = Index + 1;
            }
            *Out = nullptr;
            return GR_INTERFACE_UNSUPPORTED;
        }

        // LAB_06f82810.
        FlULong DACOM_API AddRef() override
        {
            RefCount_ = RefCount_ + 1;
            return RefCount_;
        }

        // FUN_06f82820: JBE skips DEC; on zero, store 1, run the destructor, operator_delete.
        FlULong DACOM_API Release() override
        {
            if (RefCount_ != 0)
                RefCount_ = RefCount_ - 1;
            if (RefCount_ != 0)
                return RefCount_;
            RefCount_ = 1;
            delete this;
            return 0;
        }

        // Reserved — retail LAB_06f82640 is RET 0xC, no body, no name (EAX is the caller's).
        // formerly Slot3; TODO: original purpose and return contract.
        GENRESULT DACOM_API NoOp(void* A, void* B) override
        {
            (void)A;
            (void)B;
            return GR_OK;
        }

        // LAB_06f82060. Returns retail's EAX: 0 for an empty source rect, otherwise TilesX.
        GENRESULT DACOM_API Draw(const RECT* Src, const RECT* Dest) override
        {
            RECT Rect;
            if (Src != nullptr)
            {
                Rect = *Src;
                if (NormalizeRect(&Rect) == 0)
                {
                    Rect.right = (FlLong)(Width_);
                    Rect.bottom = (FlLong)(Height_);
                    Rect.top = 0;
                    Rect.left = 0;
                }
            }
            else
            {
                Rect.right = (FlLong)(Width_);
                Rect.bottom = (FlLong)(Height_);
                Rect.top = 0;
                Rect.left = 0;
            }

            RebuildUv(&Rect);

            const FlLong SrcW = Rect.right - Rect.left;
            if (SrcW == 0)
                return 0;
            const FlLong SrcH = Rect.bottom - Rect.top;
            if (SrcH == 0)
                return 0;

            // 06f820f7..06f82139: FILD/FIDIV int by int, FSTP float.
            float DestX = (float)(Dest->left);
            const float ScaleX = (float)((double)(Dest->right - Dest->left) /
                                                    (double)(SrcW));
            const float ScaleY = (float)((double)(Dest->bottom - Dest->top) /
                                                    (double)(SrcH));

            const FlLong TilesX = (FlLong)(TilesX_);
            if (TilesX <= 0)
                return (GENRESULT)(TilesX);

            FlLong Col = 0;
            while (Col < (FlLong)(TilesX_))
            {
                float DestY = (float)(Dest->top);
                // Column U span is read from the row-0 tile of this column (06f8215a).
                const float* ColUv = Uvs_ + Col * 8;
                const float USpan = ColUv[2] - ColUv[0];
                FlULong PixW = AbilityMaxWidth_;
                if (Col == (FlLong)(TilesX_) - 1)
                {
                    const FlULong Rem = Width_ % AbilityMaxWidth_;
                    if (Rem != 0)
                        PixW = Rem;
                }
                const float ColW = (float)((double)(PixW) *
                                                      (double)(ScaleX) *
                                                      (double)(USpan));

                FlLong Row = 0;
                while (Row < (FlLong)(TilesY_))
                {
                    const FlULong Index = TilesX_ * (FlULong)(Row) +
                                                (FlULong)(Col);
                    float* Uv = Uvs_ + Index * 8;
                    // V span stays on the FPU stack (06f821b9..06f821f6).
                    const double VSpan = (double)(Uv[5]) - (double)(Uv[1]);
                    FlULong PixH = AbilityMaxHeight_;
                    if (Row == (FlLong)(TilesY_) - 1)
                    {
                        const FlULong Rem = Height_ % AbilityMaxHeight_;
                        if (Rem != 0)
                            PixH = Rem;
                    }
                    const float RowH = (float)((double)(PixH) *
                                                          (double)(ScaleY) * VSpan);

                    // FCOMP 0.0 / TEST AH,0x44 / JNP: skip only when USpan == 0 (unordered draws).
                    if (!(USpan == 0.0f))
                    {
                        if (DrawTile_ != nullptr)
                        {
                            // 06f8221a..06f82273: callback Y endpoints reverse the destination RECT.
                            float DestXy[4];
                            DestXy[0] = (float)((double)(DestX) - 0.5);
                            DestXy[1] = (float)((double)(RowH) +
                                                           (double)(DestY) - 0.5);
                            DestXy[2] = (float)((double)(ColW) +
                                                           (double)(DestX) - 0.5);
                            DestXy[3] = (float)((double)(DestY) - 0.5);
                            const FlULong Color =
                                ((FlULong)(Alpha_) << 24) | PackedRgb_;
                            DrawTile_(Handles_[Index], Color, DestXy, Uv);
                        }
                        DestY = (float)((double)(RowH) +
                                                   (double)(DestY));
                    }
                    Row = Row + 1;
                }
                DestX = (float)((double)(ColW) + (double)(DestX));
                Col = Col + 1;
            }
            return (GENRESULT)(TilesX_);
        }

        // LAB_06f82650: RET 0xC with EAX = Height.
        GENRESULT DACOM_API GetSize(FlULong* Width, FlULong* Height) override
        {
            *Width = Width_;
            *Height = Height_;
            return (GENRESULT)(Height_);
        }

        // LAB_06f82670: FMUL 255.0f (DAT_06f830e0), _ftol, low byte -> +0x2c; EAX = _ftol result.
        GENRESULT DACOM_API SetAlpha(float Alpha) override
        {
            const FlLong Value = MsvcrtFtol((double)(Alpha) * 255.0);
            Alpha_ = (unsigned char)(Value & 0xFF);
            return (GENRESULT)(Value);
        }

        // FUN_06f82690: G, R, B through _ftol*255 with OR 0xFFFFFF00 / SHL chain -> 0xFFRRGGBB.
        // RET 8 with EAX = this.
        GENRESULT DACOM_API SetColor(const float* Rgb) override
        {
            FlULong Packed = (FlULong)(MsvcrtFtol((double)(Rgb[1]) * 255.0)) & 0xFFu;
            FlULong Red = (FlULong)(MsvcrtFtol((double)(Rgb[0]) * 255.0));
            Red = Red | 0xFFFFFF00u;
            Red = Red << 8;
            Packed = Packed | Red;
            Packed = Packed << 8;
            const FlULong Blue = (FlULong)(MsvcrtFtol((double)(Rgb[2]) * 255.0)) & 0xFFu;
            Packed = Packed | Blue;
            PackedRgb_ = Packed;
            return (GENRESULT)((intptr_t)((IBigImage2*)(this)));
        }

        // LAB_06f826f0.
        FlULong DACOM_API GetTileCount() override
        {
            return TileCount_;
        }

        // LAB_06f82700: signed range check against TileCount, then handle + SetRect(0,0,W,H).
        GENRESULT DACOM_API GetTile(int Index, void** OutHandle, RECT* OutRect) override
        {
            if (Index < 0 || Index >= (FlLong)(TileCount_))
                return GR_GENERIC;
            *OutHandle = Handles_[Index];
            SetRect(OutRect, 0, 0, (int)(Width_), (int)(Height_));
            return GR_OK;
        }

        // LAB_06f81a80: re-upload pixels into the existing tiles (same order as FUN_06f81180).
        GENRESULT DACOM_API SetFromBuffer(const void* Pixels, FlULong* Format,
                                          FlULong Width, FlULong Height,
                                          FlULong Pitch) override
        {
            if (Width != Width_ || Height != Height_ || Pixels == nullptr || Format == nullptr)
                return GR_INVALID_PARMS;

            const FlULong TileW = AbilityMaxWidth_;
            const FlULong TileH = AbilityMaxHeight_;
            const FlLong FullX = (FlLong)(Width / TileW);
            const FlLong FullY = (FlLong)(Height / TileH);
            const FlULong Bpp = FormatBytes(Format);
            if (TileCount_ == 0)
                return GR_OK;

            const unsigned char* Base = (const unsigned char*)(Pixels);
            FlLong Index = 0;
            FlLong Row = 0;
            FlLong Col = 0;
            while (Row < FullY)
            {
                Col = 0;
                while (Col < FullX)
                {
                    const unsigned char* TilePixels = Base + TileW * Bpp * Col + TileH * Row * Pitch;
                    if (SetTextureLevelData(Pipeline_, Handles_[Index], TileW, TileH, Pitch, Format,
                                            TilePixels) != GR_OK)
                    {
                        DumpSetLevelNotice(0x21e);
                        return GR_GENERIC;
                    }
                    Index = Index + 1;
                    Col = Col + 1;
                }
                const FlULong RemX = Width % TileW;
                if (RemX != 0)
                {
                    const unsigned char* TilePixels = Base + TileW * Bpp * Col + TileH * Row * Pitch;
                    if (SetTextureLevelData(Pipeline_, Handles_[Index], RemX, TileH, Pitch, Format,
                                            TilePixels) != GR_OK)
                    {
                        DumpSetLevelNotice(0x22b);
                        return GR_GENERIC;
                    }
                    Index = Index + 1;
                }
                Row = Row + 1;
            }

            const FlULong RemY = Height % TileH;
            if (RemY == 0)
                return GR_OK;
            Col = 0;
            while (Col < FullX)
            {
                const unsigned char* TilePixels = Base + TileW * Bpp * Col + TileH * Row * Pitch;
                if (SetTextureLevelData(Pipeline_, Handles_[Index], TileW, RemY, Pitch, Format,
                                        TilePixels) != GR_OK)
                {
                    DumpSetLevelNotice(0x23e);
                    return GR_GENERIC;
                }
                Index = Index + 1;
                Col = Col + 1;
            }
            const FlULong RemX = Width % TileW;
            if (RemX == 0)
                return GR_OK;
            const unsigned char* TilePixels = Base + TileW * Bpp * Col + TileH * Row * Pitch;
            if (SetTextureLevelData(Pipeline_, Handles_[Index], RemX, RemY, Pitch, Format,
                                    TilePixels) != GR_OK)
            {
                DumpSetLevelNotice(0x24c);
                return GR_GENERIC;
            }
            return GR_OK;
        }

        // formerly Slot11; TODO: original operation purpose.
        GENRESULT DACOM_API ReturnMinusSeven(void* Unused) override
        {
            (void)Unused;
            return (GENRESULT)-7;
        }

        // Body of FUN_06f823f0 after the inline ctor (06f8249a..06f82573). Returns the
        // function's fail local: -1 unless the tile upload returned non-zero.
        GENRESULT Initialize(FBigImageDesc* Desc)
        {
            if (Desc->Size <= kBigImageDescSize)
                DrawTile_ = Desc->DrawTile;

            GENRESULT Result = GR_GENERIC;
            IRenderPipeline* Pipeline = Desc->Pipeline;
            if (Pipeline == nullptr)
                return Result;
            if (Pipeline_ != Pipeline)
            {
                ReleaseInterface(&Pipeline_);
                Pipeline_ = Pipeline;
                Pipeline->AddRef();
            }

            // Results are not tested (06f824e0 / 06f824ee / 06f824fc).
            Pipeline_->QueryDeviceAbility(Render::TextureSquareOnly, &AbilitySquareOnly_);
            Pipeline_->QueryDeviceAbility(Render::TextureMaxWidth, &AbilityMaxWidth_);
            Pipeline_->QueryDeviceAbility(Render::TextureMaxHeight, &AbilityMaxHeight_);

            ReleaseInterface(&TextureLibrary_);
            if (Pipeline_->QueryInterface(Iids::TextureLibraryA,
                                          (void**)(&TextureLibrary_)) < 0)
                return Result;

            int bUploaded = 0;
            if (Desc->ImageSource != nullptr)
                bUploaded = LoadFromImageSource(Desc->ImageSource, Desc->SrcFormat);
            else
                bUploaded = CreateTiles(Desc->SrcFormat, Desc->DstFormat, Desc->Pixels, Desc->Width,
                                        Desc->Height, Desc->Pitch);
            if (bUploaded != 0)
                Result = GR_OK;
            return Result;
        }

    private:
        int LoadFromImageSource(IBigImageSource* ImageSource, FlULong* SrcFormat)
        {
            FlULong SourceFormat = (FlULong)(FormatIdToIndex(0));
            if (ImageSource->GetFormat(&SourceFormat) != GR_OK)
                return 0;

            FlULong Width = 0;
            FlULong Height = 0;
            if (ImageSource->GetSize(&Width, &Height) != GR_OK)
                return 0;

            const FlULong SrcBpp = FormatBytes(SrcFormat);
            const FlULong AltBpp = FormatBytes(&SourceFormat);

            // Preserve wrapped byte sizing and the existing C-runtime allocation pair.
            void* Buffer = std::malloc(SrcBpp * Width * Height);
            FlULong CopiedFormat = 0;
            CopyDword(&CopiedFormat, SrcFormat);
            int bResult = 0;
            if (ImageSource->CopyPixels(CopiedFormat, Buffer, SrcBpp * Width, 0) == GR_OK)
            {
                bResult = CreateTiles(SrcFormat, SrcFormat, Buffer, Width, Height, SrcBpp * Width);
            }
            else
            {
                std::free(Buffer);
                Buffer = std::malloc(AltBpp * Width * Height);
                CopyDword(&CopiedFormat, &SourceFormat);
                if (ImageSource->CopyPixels(CopiedFormat, Buffer, AltBpp * Width, 0) != GR_OK)
                {
                    std::free(Buffer);
                    return 0;
                }
                bResult = CreateTiles(SrcFormat, &SourceFormat, Buffer, Width, Height, AltBpp * Width);
            }
            if (Buffer != nullptr)
                std::free(Buffer);
            return bResult;
        }

        int CreateTiles(FlULong* SrcFormat, FlULong* DstFormat, const void* Pixels,
                        FlULong Width, FlULong Height, FlULong Pitch)
        {
            if (TextureLibrary_->PickGpuFormat(SrcFormat, &FormatId_) < 0)
                return 0;

            const FlULong TileW = AbilityMaxWidth_;
            const FlULong TileH = AbilityMaxHeight_;
            const FlLong FullX = (FlLong)(Width / TileW);
            const FlULong CountX = (FlULong)(FullX) + ((Width % TileW != 0) ? 1u : 0u);
            Width_ = Width;
            const FlLong FullY = (FlLong)(Height / TileH);
            const FlULong CountY = (FlULong)(FullY) + ((Height % TileH != 0) ? 1u : 0u);
            TileCount_ = CountY * CountX;
            Height_ = Height;
            const FlULong Bpp = FormatBytes(DstFormat);
            if (TileCount_ == 0)
                return 1;

            // Byte arrays preserve the wrapped DWORD extents; only handles are zeroed.
            Uvs_ = (float*)new unsigned char[TileCount_ * 32];
            // Retail TileCount_ * 4: one texture handle each (sizeof is 4 on Win32, 8 on x64).
            Handles_ = (void**)new unsigned char[TileCount_ * sizeof(void*)];
            std::memset(Handles_, 0, TileCount_ * sizeof(void*));

            const unsigned char* Base = (const unsigned char*)(Pixels);
            FlLong Index = 0;
            FlLong Row = 0;
            FlLong Col = 0;
            while (Row < FullY)
            {
                Col = 0;
                while (Col < FullX)
                {
                    const unsigned char* TilePixels = Base + TileW * Bpp * Col + TileH * Row * Pitch;
                    if (CreateTexture(Pipeline_, (FlLong)(TileW), (FlLong)(TileH),
                                      &FormatId_, &Handles_[Index]) != GR_OK)
                    {
                        DumpAllocateFail(0x179, Width, Height);
                        return 0;
                    }
                    if (SetTextureLevelData(Pipeline_, Handles_[Index], TileW, TileH, Pitch, DstFormat,
                                            TilePixels) != GR_OK)
                    {
                        DumpSetLevelWarning(0x17f);
                        DestroyTexture(Pipeline_, Handles_[Index]);
                        Handles_[Index] = nullptr;
                        return 0;
                    }
                    Index = Index + 1;
                    Col = Col + 1;
                }

                const FlULong RemX = Width % TileW;
                if (RemX != 0)
                {
                    const unsigned char* TilePixels = Base + TileW * Bpp * Col + TileH * Row * Pitch;
                    if (AbilitySquareOnly_ != 0)
                    {
                        if (CreateTexture(Pipeline_, (FlLong)(TileW), (FlLong)(TileH),
                                          &FormatId_, &Handles_[Index]) != GR_OK)
                        {
                            DumpAllocateFail(0x18f, Width, Height);
                            return 0;
                        }
                    }
                    else if (CreateTexture(Pipeline_, RoundUpPot(RemX), (FlLong)(TileH),
                                           &FormatId_, &Handles_[Index]) != GR_OK)
                    {
                        DumpAllocateFail(0x197, Width, Height);
                        return 0;
                    }
                    if (SetTextureLevelData(Pipeline_, Handles_[Index], RemX, TileH, Pitch, DstFormat,
                                            TilePixels) != GR_OK)
                    {
                        DumpSetLevelWarning(0x19f);
                        DestroyTexture(Pipeline_, Handles_[Index]);
                        Handles_[Index] = nullptr;
                        return 0;
                    }
                    Index = Index + 1;
                }
                Row = Row + 1;
            }

            const FlULong RemY = Height % TileH;
            if (RemY != 0)
            {
                const FlLong PotY = RoundUpPot(RemY);
                Col = 0;
                while (Col < FullX)
                {
                    const unsigned char* TilePixels = Base + TileW * Bpp * Col + TileH * Row * Pitch;
                    if (AbilitySquareOnly_ != 0)
                    {
                        if (CreateTexture(Pipeline_, (FlLong)(TileW), (FlLong)(TileH),
                                          &FormatId_, &Handles_[Index]) != GR_OK)
                        {
                            DumpAllocateFail(0x1b6, Width, Height);
                            return 0;
                        }
                    }
                    else if (CreateTexture(Pipeline_, (FlLong)(TileW), PotY, &FormatId_,
                                           &Handles_[Index]) != GR_OK)
                    {
                        DumpAllocateFail(0x1be, Width, Height);
                        return 0;
                    }
                    if (SetTextureLevelData(Pipeline_, Handles_[Index], TileW, RemY, Pitch, DstFormat,
                                            TilePixels) != GR_OK)
                    {
                        DumpSetLevelWarning(0x1c5);
                        DestroyTexture(Pipeline_, Handles_[Index]);
                        Handles_[Index] = nullptr;
                        return 0;
                    }
                    Index = Index + 1;
                    Col = Col + 1;
                }

                const FlULong RemX = Width % TileW;
                if (RemX != 0)
                {
                    const unsigned char* TilePixels = Base + TileW * Bpp * Col + TileH * Row * Pitch;
                    if (AbilitySquareOnly_ != 0)
                    {
                        // 06f8173b..06f81760: square side = max(POT(RemX), POT(RemY)), signed JLE.
                        FlLong Side = RoundUpPot(RemX);
                        if (Side <= PotY)
                            Side = PotY;
                        else
                            Side = RoundUpPot(RemX);
                        if (CreateTexture(Pipeline_, Side, Side, &FormatId_, &Handles_[Index]) != GR_OK)
                        {
                            DumpAllocateFail(0x1e1, Width, Height);
                            return 0;
                        }
                    }
                    else if (CreateTexture(Pipeline_, RoundUpPot(RemX), PotY, &FormatId_,
                                           &Handles_[Index]) != GR_OK)
                    {
                        DumpAllocateFail(0x1e9, Width, Height);
                        return 0;
                    }
                    if (SetTextureLevelData(Pipeline_, Handles_[Index], RemX, RemY, Pitch, DstFormat,
                                            TilePixels) != GR_OK)
                    {
                        DumpSetLevelWarning(0x1ef);
                        DestroyTexture(Pipeline_, Handles_[Index]);
                        Handles_[Index] = nullptr;
                        return 0;
                    }
                }
            }

            TilesX_ = (FlULong)(Col) + ((Width % TileW != 0) ? 1u : 0u);
            TilesY_ = (FlULong)(Row) + ((Height % TileH != 0) ? 1u : 0u);
            return 1;
        }

        void RebuildUv(const RECT* Src)
        {
            FlULong Col = 0;
            while (Col < TilesX_)
            {
                FlULong SpanW = AbilityMaxWidth_;
                const FlULong LeftTile = (FlULong)(Src->left) / SpanW;
                const FlULong LeftRem = (FlULong)(Src->left) % SpanW;
                const FlULong RightTile = (FlULong)(Src->right) / SpanW;
                const FlULong RightRem = (FlULong)(Src->right) % SpanW;
                if (LeftTile > Col || RightTile < Col)
                {
                    FlULong RowClear = 0;
                    while (RowClear < TilesY_)
                    {
                        int Corner = 0;
                        while (Corner < 4)
                        {
                            float* Uv = Uvs_ + ((TilesX_ * RowClear + Col) * 4 + Corner) * 2;
                            Uv[0] = 0.0f;
                            Uv[1] = 0.0f;
                            Corner = Corner + 1;
                        }
                        RowClear = RowClear + 1;
                    }
                    Col = Col + 1;
                    continue;
                }

                if (Col >= TilesX_ - 1)
                {
                    const FlULong Rem = Width_ % SpanW;
                    if (Rem != 0)
                        SpanW = Rem;
                }
                // FILD qword (unsigned remainder) / FIDIV dword (span): kept on the FPU stack.
                const double U0 = (LeftTile == Col)
                                      ? (double)(LeftRem) / (double)((FlLong)(SpanW))
                                      : 0.0;
                const double U1 = (RightTile == Col)
                                      ? (double)(RightRem) / (double)((FlLong)(SpanW))
                                      : 1.0;

                FlULong Row = 0;
                while (Row < TilesY_)
                {
                    float* Uv = Uvs_ + (TilesX_ * Row + Col) * 8;
                    FlULong SpanH = AbilityMaxHeight_;
                    const FlULong TopTile = (FlULong)(Src->top) / SpanH;
                    const FlULong TopRem = (FlULong)(Src->top) % SpanH;
                    const FlULong BotTile = (FlULong)(Src->bottom) / SpanH;
                    const FlULong BotRem = (FlULong)(Src->bottom) % SpanH;
                    if (TopTile > Row || BotTile < Row)
                    {
                        Uv[0] = (float)(U0);
                        Uv[2] = (float)(U1);
                        Uv[4] = (float)(U0);
                        Uv[6] = (float)(U1);
                        Uv[1] = 0.0f;
                        Uv[3] = 0.0f;
                        Uv[5] = 0.0f;
                        Uv[7] = 0.0f;
                    }
                    else
                    {
                        if (Row >= TilesY_ - 1)
                        {
                            const FlULong Rem = Height_ % SpanH;
                            if (Rem != 0)
                                SpanH = Rem;
                        }
                        const double V0 = (TopTile == Row)
                                              ? (double)(TopRem) / (double)((FlLong)(SpanH))
                                              : 0.0;
                        const double V1 = (BotTile == Row)
                                              ? (double)(BotRem) / (double)((FlLong)(SpanH))
                                              : 1.0;
                        Uv[0] = (float)(U0);
                        Uv[1] = (float)(V0);
                        Uv[2] = (float)(U1);
                        Uv[3] = (float)(V0);
                        Uv[4] = (float)(U0);
                        Uv[5] = (float)(V1);
                        Uv[6] = (float)(U1);
                        Uv[7] = (float)(V1);
                    }
                    Row = Row + 1;
                }
                Col = Col + 1;
            }
        }

        IRenderPipeline* Pipeline_;     // +0x04
        ITextureLibrary2* TextureLibrary_; // +0x08
        void** Handles_;                  // +0x0C
        float* Uvs_;                      // +0x10 8 floats per tile
        FlULong TileCount_;         // +0x14
        FlULong TilesX_;            // +0x18
        FlULong TilesY_;            // +0x1C
        FlULong Width_;             // +0x20
        FlULong Height_;            // +0x24
        FlULong PackedRgb_;         // +0x28
        unsigned char Alpha_;             // +0x2C
        unsigned char Pad0_;
        unsigned char Pad1_;
        unsigned char Pad2_;
        FlULong FormatId_;          // +0x30
        FBigImageDrawTile DrawTile_;      // +0x34
        FlULong AbilitySquareOnly_; // +0x38
        FlULong AbilityMaxWidth_;   // +0x3C
        FlULong AbilityMaxHeight_;  // +0x40
        FlULong RefCount_;          // +0x44
    };

    FL_LAYOUT_ASSERT(sizeof(FBigImage) == 0x48, "operator_new(0x48) @06f82445");

    // 0xC-byte factory (operator_new(0xc) @06f8232c): vptr 06f83070, RefCount, Name.
    class FBigImageFactory : public IComponentFactory
    {
    public:
        FBigImageFactory() : RefCount_(1), Name_(Render::Factory::BigImage) {}

        // LAB_06f82370: *Out = 0, CMPSB 0x17 bytes vs "1.11_IComponentFactory" (== strcmp).
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            *Out = nullptr;
            if (std::strcmp(Iid, Iids::ComponentFactory) != 0)
                return GR_INTERFACE_UNSUPPORTED;
            *Out = (IComponentFactory*)(this);
            AddRef();
            return GR_OK;
        }

        // LAB_06f823b0.
        FlULong DACOM_API AddRef() override
        {
            RefCount_ = RefCount_ + 1;
            return RefCount_;
        }

        // FUN_06f823c0.
        FlULong DACOM_API Release() override
        {
            if (RefCount_ != 0)
                RefCount_ = RefCount_ - 1;
            if (RefCount_ != 0)
                return RefCount_;
            RefCount_ = 1;
            delete this;
            return 0;
        }

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override
        {
            FBigImageDesc* Big = (FBigImageDesc*)(Desc);
            if (Big->Size != kBigImageDescSize || std::strcmp(Big->Name, Name_) != 0)
            {
                *Out = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }

            FBigImage* Image = new (std::nothrow) FBigImage();
            if (Image == nullptr)
            {
                *Out = nullptr;
                return GR_OUT_OF_MEMORY;
            }

            const GENRESULT Result = Image->Initialize(Big);
            if (Result != GR_OK)
            {
                delete Image;
                *Out = nullptr;
                return Result;
            }
            *Out = (IBigImage2*)(Image);
            return Result;
        }

    private:
        FlULong RefCount_; // +0x04
        const char* Name_;       // +0x08 s_BigImage_06f840f0
    };

    FL_LAYOUT_ASSERT(sizeof(FBigImageFactory) == 0x0C, "operator_new(0xc) @06f8232c");

    // FUN_06f82320: DACOM_Acquire, operator_new(0xC) (null passes a null factory on, as
    // retail does), RegisterComponent(factory, "BigImage", 0x40000000), drop the creation ref.
    void RegisterBigImage()
    {
        ICOManager* Manager = DACOM_Acquire();
        FBigImageFactory* Factory = new (std::nothrow) FBigImageFactory();
        Manager->RegisterComponent(Factory, Render::Factory::BigImage,
                                   kRegisterPriority);
        Factory->Release();
    }
}

// FUN_06f828d0: DEC reason / JNZ -> return 1; attach registers the factory.
BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    (void)Instance;
    (void)Reserved;
    if (Reason == DLL_PROCESS_ATTACH)
        RegisterBigImage();
    return TRUE;
}
