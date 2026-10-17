// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_types.h"

#include <cstring>
#include <string>

// alchemy.dll — DACOM particle FX scaffolding.
// FFxEffectArchetype: the IFxEffectArchetype component (rec16/rec8 tables).
namespace AlchemyImpl
{
    class FFxEffectArchetype final : public IFxEffectArchetype
    {
    public:
        FFxEffectArchetype()
            : RefCount_(1), NextId_(0), Rec16_(nullptr), Rec16Count_(0), Rec16Capacity_(0),
              Rec8_(nullptr), Rec8Count_(0), Rec8Capacity_(0)
        {
            std::memset(Header16_, 0, sizeof(Header16_));
        }

        ~FFxEffectArchetype()
        {
            delete[] Rec8_;
            Rec8_ = nullptr;
            Rec8Count_ = 0;
            Rec8Capacity_ = 0;
            delete[] Rec16_;
            Rec16_ = nullptr;
            Rec16Count_ = 0;
            Rec16Capacity_ = 0;
        }

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override { return ++RefCount_; }
        unsigned long DACOM_API Release() override;

        GENRESULT DACOM_API ResetRecords() override;
        GENRESULT DACOM_API SetName(const char* Name) override;
        const char* DACOM_API GetName() override { return Name_.c_str(); }

        GENRESULT DACOM_API AddRec16(const FAleRec16* Rec, unsigned long* OutId,
                                     unsigned long Flag) override;
        GENRESULT DACOM_API RemoveRec16(unsigned long Id) override;
        GENRESULT DACOM_API Slot8(unsigned long Id, unsigned long Value) override;
        GENRESULT DACOM_API AddRec8(const FAleRec8* Rec) override;
        GENRESULT DACOM_API RemoveRec8(const FAleRec8* Rec) override;

        unsigned long DACOM_API GetRec16Count() override { return Rec16Count_; }
        unsigned long DACOM_API GetRec8Count() override { return Rec8Count_; }

        GENRESULT DACOM_API GetRec16(unsigned long Index, FAleRec16* Out) override;
        GENRESULT DACOM_API GetRec8(unsigned long Index, FAleRec8* Out) override;
        GENRESULT DACOM_API GetRec16ById(unsigned long Id, FAleRec16* Out) override;
        GENRESULT DACOM_API FindRec8Index(const FAleRec8* Rec, unsigned long* OutIndex) override;
        GENRESULT DACOM_API RemapRec16FieldD(unsigned long OldId, unsigned long NewId) override;
        GENRESULT DACOM_API GetHeader16(unsigned char* Out16) override;
        GENRESULT DACOM_API SetHeader16(const unsigned char* Header16) override;

        GENRESULT DACOM_API Slot20(void* /*Arg*/) override { return ResetRecords(); }

    private:
        void AppendRec16(const FAleRec16& Record);
        void AppendRec8(const FAleRec8& Record);

        unsigned long RefCount_;
        std::string Name_;
        unsigned char Header16_[16];
        unsigned long NextId_;
        FAleRec16* Rec16_;
        unsigned long Rec16Count_;
        unsigned long Rec16Capacity_;
        FAleRec8* Rec8_;
        unsigned long Rec8Count_;
        unsigned long Rec8Capacity_;
    };

} // namespace AlchemyImpl
