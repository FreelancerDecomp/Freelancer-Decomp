// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#include "RpGpuBackend.h"

#include <cstddef>
#include <iterator>

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
        bool Create(HWND Hwnd, std::uint32_t Width, std::uint32_t Height, bool bVSync) override
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

        bool Present() override
        {
            if (!bReady_)
                return false;
            FrameArgb_ = ClearArgb_;
            FrameWidth_ = Width_;
            FrameHeight_ = Height_;
            return true;
        }

        HWND Hwnd() const override { return Hwnd_; }
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
            const std::size_t Pixels = static_cast<std::size_t>(FrameWidth_) * FrameHeight_;
            Out->Bgra.resize(Pixels * 4);
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
        HWND Hwnd_ = nullptr;
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
