// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#include "RpGpuBackend.h"

#include <cstddef>
#include <iterator>
#include <new>

namespace
{
    // The modes the Null backend reports, largest first as SDL lists them. Common sizes, so the
    // options screen offers the ones the game's own resolution list has.
    constexpr FRpGpuDisplayMode kNullModes[] = {
        {1920, 1080, 60}, {1600, 1200, 60}, {1280, 1024, 60}, {1280, 720, 60},
        {1024, 768, 60},  {800, 600, 60},   {640, 480, 60},
    };

    // No device: every call succeeds and nothing reaches a screen. The presented frame is the
    // clear colour, which ReadBack hands out, so screenshots work without a GPU.
    class FRpGpuNullBackend final : public IRpGpuBackend
    {
    public:
        bool Create(RpWindowHandle Hwnd, std::uint32_t Width, std::uint32_t Height, bool bVSync) override
        {
            (void)bVSync;
            Hwnd_ = Hwnd;
            Width_ = Width;
            Height_ = Height;
            bReady_ = true;
            return true;
        }

        void Destroy() override
        {
            bReady_ = false;
            Hwnd_ = nullptr;
            Width_ = 0;
            Height_ = 0;
            FrameWidth_ = 0;
            FrameHeight_ = 0;
        }

        bool IsReady() const override { return bReady_; }
        void SetClearColor(std::uint32_t Argb) override { ClearArgb_ = Argb; }

        // Draws nothing: the frame is the colour of its last colour clear, or the SetClearColor
        // colour when it has none, so screenshots stay meaningful without a GPU.
        bool Present(const FRpGpuFrameRecording& Frame) override
        {
            if (!bReady_)
                return false;
            std::uint32_t Argb = ClearArgb_;
            for (const FRpGpuCommand& Command : Frame.Commands)
            {
                if (Command.Kind == FRpGpuCommand::EKind::Clear && Command.bClearColor)
                    Argb = Command.ClearArgb;
            }
            FrameArgb_ = Argb;
            FrameWidth_ = Width_;
            FrameHeight_ = Height_;
            return true;
        }

        void UploadTexture(std::uint64_t Key, FRpGpuTextureImage&& Image) override
        {
            (void)Key;
            (void)Image;
        }

        // Takes anything: the images are dropped. Saying yes spares RPGPU the DXT decode.
        bool SupportsBlockCompression() const override { return true; }

        void ForgetTexture(std::uint64_t Key) override { (void)Key; }
        bool UploadVertexBlock(std::uint64_t Key, std::vector<FRpGpuVertex>&& Vertices) override
        {
            (void)Key;
            (void)Vertices;
            return true;
        }
        void ForgetVertexBlock(std::uint64_t Key) override { (void)Key; }
        bool IsVertexBlockLost(std::uint64_t Key) const override
        {
            (void)Key;
            return false;
        }

        RpWindowHandle Hwnd() const override { return Hwnd_; }
        std::uint32_t Width() const override { return Width_; }
        std::uint32_t Height() const override { return Height_; }

        bool QueryDisplayModes(std::vector<FRpGpuDisplayMode>* Out) override
        {
            Out->assign(std::begin(kNullModes), std::end(kNullModes));
            return true;
        }

        bool ReadBack(FRpGpuFrame* Out) override
        {
            if (FrameWidth_ == 0 || FrameHeight_ == 0)
                return false;
            Out->Width = FrameWidth_;
            Out->Height = FrameHeight_;
            // Readback is for display-sized frames. Bound each side first, so neither the pixel
            // count nor the byte count can wrap a 32-bit size_t, and fail instead of throwing.
            constexpr std::uint32_t kMaxReadBackSide = 16384;
            if (FrameWidth_ > kMaxReadBackSide || FrameHeight_ > kMaxReadBackSide)
                return false;
            const std::uint64_t Pixels64 = static_cast<std::uint64_t>(FrameWidth_) * FrameHeight_;
            const std::uint64_t Bytes64 = Pixels64 * 4;
            if (Bytes64 > Out->Bgra.max_size())
                return false;
            const std::size_t Pixels = static_cast<std::size_t>(Pixels64);
            try
            {
                Out->Bgra.resize(static_cast<std::size_t>(Bytes64));
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }
            const std::uint8_t Pixel[4] = {
                static_cast<std::uint8_t>(FrameArgb_ & 0xFF),
                static_cast<std::uint8_t>((FrameArgb_ >> 8) & 0xFF),
                static_cast<std::uint8_t>((FrameArgb_ >> 16) & 0xFF),
                static_cast<std::uint8_t>((FrameArgb_ >> 24) & 0xFF),
            };
            for (std::size_t Index = 0; Index < Pixels; ++Index)
            {
                for (std::size_t Byte = 0; Byte < 4; ++Byte)
                    Out->Bgra[Index * 4 + Byte] = Pixel[Byte];
            }
            return true;
        }

        const char* LastError() const override { return ""; }

    private:
        RpWindowHandle Hwnd_ = nullptr;
        bool bReady_ = false;
        std::uint32_t Width_ = 0;
        std::uint32_t Height_ = 0;
        std::uint32_t ClearArgb_ = 0xFF000000u;
        // The last presented frame.
        std::uint32_t FrameArgb_ = 0;
        std::uint32_t FrameWidth_ = 0;
        std::uint32_t FrameHeight_ = 0;
    };
}

std::unique_ptr<IRpGpuBackend> CreateRpGpuNullBackend()
{
    return std::make_unique<FRpGpuNullBackend>();
}
