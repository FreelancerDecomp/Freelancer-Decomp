// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU34 (06f00670-06f03fa0): generic bar scripts and GCS exclusions.
// See GenericScripts.h. The bar behavior builder FUN_06f00b90 and its script-set helpers
// (06f00870-06f01a30, fed by the TU17 bar character and its offer) are at the end.

// 06f00670-06f00850: TU34's own 16 static-ID initializers (DAT_06fcf474..DAT_06fcf438),
// _initterm 06fcd93c-06fcd978, before GGcsExclusions (06fcd97c) and GGenericScripts
// (06fcd980).
#include "ContentStaticIds.h"

#include "GenericScripts.h"

#include "ContentInternal.h"
#include "ContentPlayerState.h"  // FindContentPlayerState, FContentPlayerState
#include "RandomMission.h"       // FRandomMissionLoader::CanAccept, FRandomMissionData
#include "RandomMissionDB.h"     // GRmRankDifficulties
#include "common_iface.h"      // CharacterBehaviorDescription
#include "common_ini.h"
#include "common_server.h"
#include "common_text.h"         // FmtStr

#include <cstdlib>
#include <cstring>

// _initterm order 06fcd97c (LAB_06f01af0) then 06fcd980 (LAB_06f01c00).
std::map<unsigned int, std::vector<std::string>> GGcsExclusions;
std::vector<std::string> GGenericScripts[0x108];

namespace
{
    struct FGenericScriptSegment
    {
        int Value;
        char const* Name;
    };

    // .data 06fce380: 0x42 {value, name} pairs read only by FUN_06f01d70 (06f01ea4 /
    // 06f01ecd); every value equals its index.
    FGenericScriptSegment const GGenericScriptSegments[0x42] = {
        {0x00, "FIDGET"}, {0x01, "A01aG"},  {0x02, "A02aG"},  {0x03, "A02cG"},
        {0x04, "A02dG"},  {0x05, "A02eB"},  {0x06, "A03aG"},  {0x07, "A04aG"},
        {0x08, "C01aR"},  {0x09, "D01aN"},  {0x0a, "D01bG"},  {0x0b, "D01cG"},
        {0x0c, "D01dG"},  {0x0d, "D01fG"},  {0x0e, "D01fGm"}, {0x0f, "D01hG"},
        {0x10, "D01kG"},  {0x11, "D01kGm"}, {0x12, "D02aP"},  {0x13, "D02aPm"},
        {0x14, "D02bP"},  {0x15, "D02dP"},  {0x16, "D02eP"},  {0x17, "D03aN"},
        {0x18, "D04aG"},  {0x19, "D04bB"},  {0x1a, "D05aP"},  {0x1b, "D05bP"},
        {0x1c, "D06aG"},  {0x1d, "D07aG"},  {0x1e, "D07bG"},  {0x1f, "F01aB"},
        {0x20, "F01bB"},  {0x21, "F01dG"},  {0x22, "F01eG"},  {0x23, "F01fG"},
        {0x24, "F01jG"},  {0x25, "F02aP"},  {0x26, "F03aR"},  {0x27, "F03bR"},
        {0x28, "F03cR"},  {0x29, "F03dR"},  {0x2a, "F04aG"},  {0x2b, "F04dG"},
        {0x2c, "F04fG"},  {0x2d, "F04gP"},  {0x2e, "F05aG"},  {0x2f, "F05bG"},
        {0x30, "F05cG"},  {0x31, "F06aP"},  {0x32, "F07aP"},  {0x33, "F07bP"},
        {0x34, "F07cP"},  {0x35, "F08dR"},  {0x36, "F08bR"},  {0x37, "F08cR"},
        {0x38, "F09bP"},  {0x39, "F10aR"},  {0x3a, "F10bR"},  {0x3b, "F10bRm"},
        {0x3c, "W01cG"},  {0x3d, "W01bB"},  {0x3e, "W02aF"},  {0x3f, "W03bB"},
        {0x40, "W04bP"},  {0x41, "W04cP"},
    };

    // FUN_06f02aa0 (06f02aa0-06f02b08) with Pos 0 and Count = Left.size(): _memicmp over
    // the shorter length, then shorter < longer.
    int CompareScriptNames(std::string const& Left, std::string const& Right)
    {
        size_t const Count = Left.size() < Right.size() ? Left.size() : Right.size();
        int const Result = _memicmp(Left.c_str(), Right.c_str(), Count);
        if (Result != 0)
            return Result;
        if (Left.size() < Right.size())
            return -1;
        return Left.size() != Right.size() ? 1 : 0;
    }

    // The pick idiom of FUN_06f020c0 (06f02252 / 06f022bf): a list of one is taken without
    // calling rand; otherwise rand() * Count / 0x8000 (CDQ, AND 0x7fff, SAR 15).
    unsigned int PickIndex(unsigned int Count)
    {
        if (Count - 1 == 0)
            return 0;
        return static_cast<unsigned int>((rand() * static_cast<int>(Count)) / 0x8000);
    }
}

// FUN_06f01d70 (06f01d70-06f0208f).
void __cdecl LoadGenericScripts(char const* Path)
{
    INI_Reader Reader;
    if (!OpenContentDataIni(Path, &Reader))
        return;

    while (Reader.read_header())
    {
        if (!Reader.is_header("GenericScripts"))
            continue;

        // 06f01df7-06f01e02: fresh defaults per section; a script before its set_* lines
        // indexes past the table exactly as retail does.
        int Gender = 2;
        int Posture = 2;
        int Segment = 0x42;
        while (Reader.read_value())
        {
            if (Reader.is_value("set_gender"))
            {
                Gender = _stricmp(Reader.get_value_string(), "male") != 0 ? 1 : 0;
            }
            else if (Reader.is_value("set_posture"))
            {
                Posture = _stricmp(Reader.get_value_string(), "stand") != 0 ? 1 : 0;
            }
            else if (Reader.is_value("set_segment"))
            {
                char const* Name = Reader.get_value_string();
                Segment = 0x42;
                for (int Index = 0; Index < 0x42; ++Index)
                {
                    if (_stricmp(GGenericScriptSegments[Index].Name, Name) == 0)
                    {
                        Segment = GGenericScriptSegments[Index].Value;
                        break;
                    }
                }
            }
            else if (Reader.is_value("script"))
            {
                std::string Script(Reader.get_value_string());
                // 06f01f23-06f01f4a: ((Posture + 2 * (Gender + 2 * Segment)) << 4) +
                // DAT_06fcf478, then insert(that->_Last, 1, Script) -- raw pointer arithmetic
                // into the static table, no bounds check, no clamp.
                // VERY STRANGE, REPRODUCED ON PURPOSE: retail indexes past the end of the
                // table here. The defaults (Gender 2, Posture 2, Segment 0x42 for a missing or
                // unknown set_segment) reach slot 0x10e of a 0x108-slot table (DAT_06fd0558,
                // 0x60 bytes past its end at 06fd04f8; slot 0x108 is GGcsExclusions'
                // storage). Only a corrupt ini (a script line before its set_* lines, or an
                // unknown segment name) gets there. The port does the same raw access into
                // whatever follows its own table -- undefined behaviour, as in retail.
                std::vector<std::string>* Scripts = GGenericScripts + (Posture + 2 * (Gender + 2 * Segment));
                Scripts->insert(Scripts->end(), 1, Script);                   // FUN_06f031d0
            }
        }
    }
}

// FUN_06f01c70 (06f01c70-06f01d63): 0x42 x 2 x 2 vectors, each erased begin..end.
void __cdecl UnloadGenericScripts()
{
    for (int Index = 0; Index < 0x108; ++Index)
        GGenericScripts[Index].clear();
}

// FUN_06f020c0 (06f020c0-06f02329).
char const* __cdecl PickGenericScript(int Segment, int Gender, int Posture, unsigned int VoiceId)
{
    int const Slot = (Posture != 0 ? 1 : 0) + 2 * ((Gender != 1 ? 1 : 0) + 2 * Segment);
    std::vector<std::string>& Scripts = GGenericScripts[Slot];

    std::vector<std::string>::iterator Chosen;
    std::map<unsigned int, std::vector<std::string>>::iterator Excluded = GGcsExclusions.find(VoiceId);
    if (Excluded != GGcsExclusions.end())
    {
        // 06f02130-06f0229c: candidates = the scripts this voice is not excluded from.
        std::vector<std::vector<std::string>::iterator> Candidates;
        Candidates.reserve(Scripts.size());
        for (std::vector<std::string>::iterator Script = Scripts.begin(); Script != Scripts.end(); ++Script)
        {
            bool bExcluded = false;
            for (std::vector<std::string>::iterator Name = Excluded->second.begin();
                 Name != Excluded->second.end(); ++Name)
            {
                // FUN_06f02aa0: Script->compare(0, size, Name->c_str(), Name->size()) of a
                // case-insensitive string (traits compare = _memicmp).
                if (CompareScriptNames(*Script, *Name) == 0)
                {
                    bExcluded = true;
                    break;
                }
            }
            if (!bExcluded)
                Candidates.insert(Candidates.end(), 1, Script);                  // FUN_06f937c0
        }

        if (Candidates.empty())
        {
            Chosen = Scripts.end();
        }
        else
        {
            unsigned int const Index = PickIndex(static_cast<unsigned int>(Candidates.size()));
            Chosen = Index < Candidates.size() ? Candidates[Index] : Scripts.end();
        }
    }
    else if (Scripts.empty())                                                 // FUN_06ead810
    {
        Chosen = Scripts.end();
    }
    else
    {
        Chosen = Scripts.begin() + PickIndex(static_cast<unsigned int>(Scripts.size()));
    }

    if (Chosen == Scripts.end())
        return nullptr;
    return Chosen->c_str();
}

// FUN_06f020a0 (06f020a0-06f020b6).
char const* __cdecl PickFidgetScript(int Gender, int Posture)
{
    return PickGenericScript(0, Gender, Posture, 0);
}

// FUN_06f01ab0 (06f01ab0-06f01ae1).
void AppendBehaviorText(CharacterBehaviorDescription* Behavior, char const* Text,
                        unsigned int Value)
{
    if (Text != nullptr && Text[0] != '\0')
    {
        Behavior->StringArray2[Behavior->StringArray2Count] = Text;
        Behavior->StringArray2Dword[Behavior->StringArray2Count] = Value;
        ++Behavior->StringArray2Count;
    }
}

// ---------------------------------------------------------------------------------------
// The bar behavior builder (06f00870-06f01a30).
// ---------------------------------------------------------------------------------------

namespace
{
    // DAT_06fd0c34 (TU47; static init LAB_06f4b030, the only writer): (DAT_06fbb230 1.3689 -
    // DAT_06fb5810 1.0) * DAT_06fb605c 0.5 + 1.0, stored as a float. Read only by
    // FUN_06f4b050.
    float const GRmOfferDifficultyTolerance = (1.3689f - 1.0f) * 0.5f + 1.0f;

    // FUN_06f4b050 (06f4b050-06f4b0c5, cdecl; TU47, its only caller is FUN_06f00870
    // 06f0098c): the difficulty of Player's stage in GRmRankDifficulties -- the exact
    // record, else the last one below it (signed JGE), else the first; an empty table reads
    // begin() as retail does -- against Difficulty: -1 when Difficulty lies below
    // stage / tolerance, 1 when above stage * tolerance, else 0.
    int CompareRmOfferDifficulty(FContentPlayerState const* Player, float Difficulty)
    {
        int const Stage = static_cast<int>(Player->MissionNumber);
        std::vector<FRmRankDifficulty>::const_iterator Previous = GRmRankDifficulties.begin();
        float StageDifficulty = 0.0f;
        bool bFound = false;
        for (std::vector<FRmRankDifficulty>::const_iterator It = GRmRankDifficulties.begin();
             It != GRmRankDifficulties.end(); ++It)
        {
            if (It->Stage == Stage)
            {
                StageDifficulty = It->Difficulty;
                bFound = true;
                break;
            }
            if (It->Stage > Stage)
                break;
            Previous = It;
        }
        if (!bFound)
            StageDifficulty = Previous->Difficulty;

        // 06f4b084-06f4b097: FDIVR, FCOMP Difficulty, TEST AH,0x41 / JNZ: stage / tolerance
        // above Difficulty (C0 and C3 clear) -> -1; below, equal or unordered go on. The x87
        // runs at the CRT's 53-bit precision, hence double.
        double const Stage64 = static_cast<double>(StageDifficulty);
        double const Tolerance = static_cast<double>(GRmOfferDifficultyTolerance);
        if (Stage64 / Tolerance > static_cast<double>(Difficulty))
            return -1;
        // 06f4b0a6-06f4b0bb: FLD Difficulty, FCOMPP stage * tolerance, TEST AH,0x41 / JNZ:
        // Difficulty above -> 1; below, equal or unordered -> 0.
        if (static_cast<double>(Difficulty) > Stage64 * Tolerance)
            return 1;
        return 0;
    }

    // The inlined "trader" / "shipdealer" / "equipment" stricmp chain (06f00bf4-06f00c51 and
    // its copies): the roles that sell something.
    bool IsShopRole(FBarCharacter const* Character)
    {
        char const* Role = Character->Role.Data;
        return _stricmp("trader", Role) == 0 || _stricmp("shipdealer", Role) == 0 ||
               _stricmp("equipment", Role) == 0;
    }

    bool IsBartenderRole(FBarCharacter const* Character)
    {
        return _stricmp("bartender", Character->Role.Data) == 0;
    }

    // FUN_06f00b10 (06f00b10-06f00b5f, cdecl): a generic script of Segment for the
    // character's gender and voice, appended with Value (the AppendBehaviorText body
    // inlined). Every "pick, then AppendBehaviorText when non-null" and every inlined copy of
    // this body in the builders below is the same operation.
    void AppendGenericScript(CharacterBehaviorDescription* Behavior, int Segment,
                             FBarCharacter const* Character, int Posture, unsigned int Value)
    {
        AppendBehaviorText(Behavior,
                           PickGenericScript(Segment, Character->Gender, Posture,
                                             Character->Voice),
                           Value);
    }

    // FUN_06f019c0 (06f019c0-06f01a1c, cdecl): the bribe text -- mad-lib TextIds with the
    // bribe's group, its price, then up to three hostile groups until the first -1. Retail
    // reads the dword after the third id (+0x1c) before the count test; nothing depends on it.
    void FormatBribeOfferText(FmtStr* Text, FBarBribeOffer const* Bribe)
    {
        Text->begin_mad_lib(static_cast<unsigned int>(Bribe->TextIds));
        Text->append_rep_group(Bribe->Group);
        Text->append_int(static_cast<unsigned int>(Bribe->Price));
        for (unsigned int Index = 0; Index < 3 && Bribe->Ids[Index] != 0xffffffffu; ++Index)
            Text->append_rep_group(Bribe->Ids[Index]);
        Text->end_mad_lib();
    }

    // FUN_06f01a30 (06f01a30-06f01aa4, cdecl): the knowledge texts -- +0x240 mad-lib 0x3efe
    // with Field_0x04; +0x228 mad-lib 0x3efd + rand() / 0x8000 (always 0: rand() < 0x8000,
    // but the draw is made) with Field_0x08 and the price.
    void FormatKnowOfferText(CharacterBehaviorDescription* Behavior, FBaseNpcKnow const* Know)
    {
        Behavior->DescFmtStr.begin_mad_lib(0x3efe);
        Behavior->DescFmtStr.append_string(static_cast<unsigned int>(Know->Field_0x04));
        Behavior->DescFmtStr.end_mad_lib();
        Behavior->NameFmtStr.begin_mad_lib(static_cast<unsigned int>(rand() / 0x8000 + 0x3efd));
        Behavior->NameFmtStr.append_string(static_cast<unsigned int>(Know->Field_0x08));
        Behavior->NameFmtStr.append_int(static_cast<unsigned int>(Know->Price));
        Behavior->NameFmtStr.end_mad_lib();
    }

    // FUN_06f00870 (06f00870-06f00b0e, cdecl): the scripts of a mission offer. EH state 0 is
    // the local FmtStr, built only past the live-mission test.
    void BuildMissionOfferScripts(FBarCharacter const* Character,
                                  CharacterBehaviorDescription* Behavior, int Posture,
                                  FContentPlayerState* State)
    {
        Behavior->Field21c = 3;
        AppendGenericScript(Behavior, 0x24, Character, Posture, 0);
        // 06f008c4-06f008d2: FUN_06f487c0, a player already on a mission.
        if ((State->GetLiveMission() & 0x3fffffff) != 0)
        {
            AppendGenericScript(Behavior, 0x2c, Character, Posture, 0);
            return;
        }

        FmtStr Reason;
        FBarMissionOffer const* Offer = static_cast<FBarMissionOffer const*>(Character->Offer.Object);
        // 06f00920-06f00936: loader slot 7 (+0x1c) with the code written into the Posture
        // argument slot ([ESP+0x40]); the posture itself stays in EDI.
        int Code = Posture;
        if (!Offer->Data.Loader->CanAccept(Offer->Data.Mission, &Reason, &Code))
        {
            // 06f00aa1-06f00ac1: code 1 -> 0x2b, code 2 -> 0x2a, anything else nothing.
            if (Code == 1)
                AppendGenericScript(Behavior, 0x2b, Character, Posture, 0);
            else if (Code == 2)
                AppendGenericScript(Behavior, 0x2a, Character, Posture, 0);
            return;
        }

        if (!pub::SinglePlayer())
        {
            AppendGenericScript(Behavior, 0x31, Character, Posture, 0);
        }
        else
        {
            AppendGenericScript(Behavior, 0x2d, Character, Posture, 0);
            // 06f0097c-06f009ab: signed JGE / JNE on FUN_06f4b050 of the mission's
            // difficulty (+0x4c).
            int const Fit = CompareRmOfferDifficulty(State, Offer->Data.Mission->Difficulty);
            int const Segment = (Fit < 0) ? 0x2e : ((Fit == 0) ? 0x2f : 0x30);
            AppendGenericScript(Behavior, Segment, Character, Posture, 0);
        }
        AppendGenericScript(Behavior, 8, Character, Posture, 1);
        AppendGenericScript(Behavior, 0x32, Character, Posture, 2);
        AppendGenericScript(Behavior, 0x39, Character, Posture, 2);
        AppendGenericScript(Behavior, 0x34, Character, Posture, 3);
        AppendGenericScript(Behavior, pub::SinglePlayer() ? 0x3a : 0x3b, Character, Posture, 3);
        AppendGenericScript(Behavior, 0x2c, Character, Posture, 6);
        // 06f00a85-06f00a99: FmtStr::operator= from the mission's description (+0x24).
        Behavior->NameFmtStr = Offer->Data.Mission->Description;
    }

    // FUN_06f01420 (06f01420-06f015dd, cdecl): the bartender's scripts. Unsigned JBE on
    // Vnpc (06f01546).
    void BuildBartenderScripts(CharacterBehaviorDescription* Behavior,
                               FBarCharacter const* Character, int Posture, unsigned int Vnpc)
    {
        if (Character->TalkCount != 0)
        {
            AppendGenericScript(Behavior, pub::SinglePlayer() ? 0xd : 0xe, Character, Posture, 0);
            AppendGenericScript(Behavior, 0x15, Character, Posture, 0);
            return;
        }
        if (!pub::SinglePlayer())
        {
            AppendGenericScript(Behavior, 0x11, Character, Posture, 0);
            AppendGenericScript(Behavior, 0x1b, Character, Posture, 0);
            return;
        }
        if (Vnpc > 1)
        {
            AppendGenericScript(Behavior, 0x10, Character, Posture, 0);
        }
        else
        {
            AppendGenericScript(Behavior, 0xf, Character, Posture, 0);
            AppendGenericScript(Behavior, 0x16, Character, Posture, 0);
            AppendGenericScript(Behavior, 0x17, Character, Posture, 0);
        }
        AppendGenericScript(Behavior, 0x1b, Character, Posture, 0);
    }

    // FUN_06f011f0 (06f011f0-06f0141b, cdecl): the scripts of a character with a vnpc record.
    // false (06f0132d) only for vnpc value 1, which ends the builder without offer scripts.
    bool BuildVnpcScripts(CharacterBehaviorDescription* Behavior, FBarCharacter const* Character,
                          int Posture, unsigned int Vnpc)
    {
        bool const bResult = true;      // [ESP+0x13] = 1 at 06f0120a
        if (IsShopRole(Character))
            return bResult;
        if (IsBartenderRole(Character))
        {
            BuildBartenderScripts(Behavior, Character, Posture, Vnpc);
            return bResult;
        }

        // 06f01275-06f012b2: FUN_06f48770 writes into the Character argument slot
        // ([ESP+0x20]; the pointer stays in ESI); not found -> 0.
        FContentPlayerState* State = FindContentPlayerState(Character->PlayerId);
        unsigned int Value = 0;
        if (!State->FindVnpcValue3(State->GetBaseRecord()->BaseId, &Character->Nickname.Id,
                                   &Value))
            Value = 0;
        if (Value == 0)
        {
            BuildBartenderScripts(Behavior, Character, Posture, Vnpc);
            return bResult;
        }
        if (Value == 1)
        {
            AppendGenericScript(Behavior, 0xc, Character, Posture, 0);
            return false;
        }
        if (!pub::SinglePlayer())
            return bResult;
        if (Value == 2)
        {
            AppendGenericScript(Behavior, 0xb, Character, Posture, 0);
            AppendGenericScript(Behavior, 0x1b, Character, Posture, 0);
        }
        else
        {
            AppendGenericScript(Behavior, 0xa, Character, Posture, 0);
            AppendGenericScript(Behavior, 0x14, Character, Posture, 0);
        }
        return bResult;
    }

    // FUN_06f01600 (06f01600-06f0199b, cdecl): the scripts of the character's offer; jump
    // table 06f0199c on OfferKind - 1 (1 bribe 06f0163a, 2 mission 06f01619, 3 rumor
    // 06f01844, 4 knowledge 06f01719, else 06f01905). The price tests are unsigned (JAE).
    void BuildOfferScripts(FBarCharacter const* Character, CharacterBehaviorDescription* Behavior,
                           int Posture, FContentPlayerState* State, bool bVnpc, int OfferKind)
    {
        switch (OfferKind)
        {
        case BAR_OFFER_MISSION:
            BuildMissionOfferScripts(Character, Behavior, Posture, State);
            return;

        case BAR_OFFER_BRIBE:
        {
            AppendGenericScript(Behavior, 0x23, Character, Posture, 0);
            AppendGenericScript(Behavior, 0x25, Character, Posture, 0);
            AppendGenericScript(Behavior, 8, Character, Posture, 1);
            AppendGenericScript(Behavior, 0x32, Character, Posture, 2);
            AppendGenericScript(Behavior, 0x34, Character, Posture, 3);
            FBarBribeOffer const* Bribe = static_cast<FBarBribeOffer const*>(Character->Offer.Object);
            // 06f016a0-06f016b2: the cash goes into the Posture argument slot ([ESP+0x1c]).
            int Cash = Posture;
            pub::Player::InspectCash(Character->PlayerId, Cash);
            AppendGenericScript(Behavior,
                                (static_cast<unsigned int>(Cash) < static_cast<unsigned int>(Bribe->Price)) ? 0x35 : 0x39,
                                Character, Posture, 2);
            AppendGenericScript(Behavior, pub::SinglePlayer() ? 0x3a : 0x3b, Character, Posture, 3);
            FormatBribeOfferText(&Behavior->NameFmtStr, Bribe);
            Behavior->Field21c = 1;
            return;
        }

        case BAR_OFFER_KNOW:
        {
            AppendGenericScript(Behavior, IsBartenderRole(Character) ? 0x20 : 0x22, Character,
                                Posture, 0);
            AppendGenericScript(Behavior, 0x25, Character, Posture, 0);
            FBaseNpcKnow const* Know = Character->Offer.Know;
            // 06f0175d-06f01789: jump table 06f019ac on RepLevel (unsigned JA past 3).
            switch (Know->RepLevel)
            {
            case 0:
            case 1:
                AppendGenericScript(Behavior, 0x26, Character, Posture, 0);
                break;
            case 2:
                AppendGenericScript(Behavior, 0x27, Character, Posture, 0);
                break;
            case 3:
                AppendGenericScript(Behavior, 0x28, Character, Posture, 0);
                break;
            default:
                break;
            }
            AppendGenericScript(Behavior, 8, Character, Posture, 1);
            // 06f0179e-06f017af: the cash goes into the Character argument slot ([ESP+0x14]).
            int Cash = static_cast<int>(reinterpret_cast<intptr_t>(Character));
            pub::Player::InspectCash(Character->PlayerId, Cash);
            if (static_cast<unsigned int>(Cash) < static_cast<unsigned int>(Know->Price))
            {
                AppendGenericScript(Behavior, 0x35, Character, Posture, 2);
            }
            else
            {
                AppendGenericScript(Behavior, 8, Character, Posture, 4);
                AppendGenericScript(Behavior, 0x38, Character, Posture, 2);
                AppendGenericScript(Behavior, 0x39, Character, Posture, 2);
            }
            AppendGenericScript(Behavior, 0x33, Character, Posture, 3);
            AppendGenericScript(Behavior, pub::SinglePlayer() ? 0x3a : 0x3b, Character, Posture, 3);
            FormatKnowOfferText(Behavior, Know);
            Behavior->Field21c = 4;
            return;
        }

        case BAR_OFFER_RUMOR:
        {
            AppendGenericScript(Behavior, IsBartenderRole(Character) ? 0x1f : 0x21, Character,
                                Posture, 0);
            AppendGenericScript(Behavior, 0x25, Character, Posture, 0);
            AppendGenericScript(Behavior, 0x36, Character, Posture, 0);
            AppendGenericScript(Behavior, 8, Character, Posture, 1);
            AppendGenericScript(Behavior, 0x38, Character, Posture, 0);
            AppendGenericScript(Behavior, 0x39, Character, Posture, 0);
            // 06f018b8-06f018f0: mad-lib Ids[0], then the following ids until the first 0.
            // No bound on the eight slots, as in retail (the data never fills them).
            FBaseNpcRumor const* Rumor = Character->Offer.Rumor;
            Behavior->DescFmtStr.begin_mad_lib(static_cast<unsigned int>(Rumor->Ids[0]));
            for (int const* Id = &Rumor->Ids[1]; *Id != 0; ++Id)
                Behavior->DescFmtStr.append_string(static_cast<unsigned int>(*Id));
            Behavior->DescFmtStr.end_mad_lib();
            Behavior->Field21c = 2;
            return;
        }

        default:
            // 06f01905-06f01997.
            if (IsBartenderRole(Character))
            {
                AppendGenericScript(Behavior, 5, Character, Posture, 0);
                return;
            }
            if (IsShopRole(Character))
                return;
            if (!bVnpc)
                AppendGenericScript(Behavior, 1, Character, Posture, 0);
            AppendGenericScript(Behavior, 2, Character, Posture, 0);
            return;
        }
    }

    // FUN_06f015e0 (06f015e0-06f015fb, cdecl; the character argument is not read).
    unsigned long CreateBarBehavior(FBarCharacter const* Unused,
                                    CharacterBehaviorDescription const* Behavior)
    {
        (void)Unused;
        if (Behavior->StringArray2Count == 0)
            return 0xffffffffu;
        return pub::GF::CharacterBehaviorCreate(*Behavior);
    }

    // The "rand() * 101 / 32768" roll of FUN_06f00b90 (IMUL 0x65, CDQ / AND 0x7fff / SAR 15).
    int RollPercent()
    {
        return (rand() * 101) / 0x8000;
    }

    // FUN_06f00b90 (06f00b90-06f011ea, cdecl): the behavior of a bar character -- generic
    // scripts by role, then the vnpc and offer scripts -- created through
    // pub::GF::CharacterBehaviorCreate; -1 when no script was added. The description local
    // ([ESP+0x2c], ctor FUN_06eac990, dtor FUN_06eacae0) is EH state 1.
    unsigned long BuildBarBehavior(FBarCharacter const* Character)
    {
        CharacterBehaviorDescription Behavior;
        // 06f00be8-06f00c28.
        Behavior.Field00 = static_cast<int>(Character->RoomId);
        Behavior.Field05 = 0;
        Behavior.Field06 = 1;
        Behavior.Field10 = 1;
        Behavior.Field04 = 0;
        Behavior.IdArrayCount = 0;
        Behavior.Field08 = 0;
        Behavior.Field0c = 0;
        // 06f00c57-06f00c82: NEG / SBB / AND 2.
        bool const bShop = IsShopRole(Character);
        Behavior.Field224 = 1;
        Behavior.Field21c = bShop ? 2 : 0;
        Behavior.Field220 = Character->CharacterId;
        Behavior.Filter = Character->PlayerId;

        FContentPlayerState* State = FindContentPlayerState(Character->PlayerId);
        // [ESP+0x14]: retail leaves it unset when the query fails.
        unsigned long PostureValue = 0;
        pub::GF::GetCharacterPlacementPosture(Character->RoomId, Character->Placement,
                                              PostureValue);
        int const Posture = static_cast<int>(PostureValue);
        unsigned int const Vnpc =
            State->GetVnpcValue2(State->GetBaseRecord()->BaseId, Character->Nickname.Id);
        // 06f00cc9-06f00d11: GetReputation(State +8, affiliation) (status ignored; the
        // attitude slot is unset when it fails). FCOMP DAT_06fb444c (0.0), TEST AH,1 / JE:
        // C0 clear (not below, ordered) keeps 1.
        int const Reputation = State->GetReputation();
        unsigned int const Affiliation = Character->Affiliation;
        float Attitude = 0.0f;
        pub::Reputation::GetReputation(Reputation, Affiliation, Attitude);
        bool const bFriendly = Attitude >= 0.0f;
        int const OfferKind = Character->Offer.Kind;

        if (Vnpc == 0)
        {
            if (IsBartenderRole(Character))
            {
                // 06f00d3c-06f00e29.
                AppendGenericScript(&Behavior, 0x3d, Character, Posture, 0);
                AppendGenericScript(&Behavior, 0x3f, Character, Posture, 0);
                AppendGenericScript(&Behavior, 0x40, Character, Posture, 0);
                bool const bChance = static_cast<unsigned int>(RollPercent()) < 0x19;
                if (pub::SinglePlayer() && bChance)
                {
                    AppendGenericScript(&Behavior, 9, Character, Posture, 0);
                    AppendGenericScript(&Behavior, 0x12, Character, Posture, 0);
                    AppendGenericScript(&Behavior, 0x19, Character, Posture, 0);
                    AppendGenericScript(&Behavior, 0x1a, Character, Posture, 0);
                }
                else
                {
                    AppendGenericScript(&Behavior, 9, Character, Posture, 0);
                    AppendGenericScript(&Behavior, 0x13, Character, Posture, 0);
                }
                if (static_cast<unsigned int>(RollPercent()) < 0x14)
                    AppendGenericScript(&Behavior, 0x3e, Character, Posture, 0);
            }
            else if (!IsShopRole(Character))
            {
                // 06f00e67-06f00f68.
                AppendGenericScript(&Behavior, 0x3c, Character, Posture, 0);
                AppendGenericScript(&Behavior, 0x41, Character, Posture, 0);
                bool const bChance = static_cast<unsigned int>(RollPercent()) < 0x19;
                if (pub::SinglePlayer() && bChance)
                {
                    AppendGenericScript(&Behavior, 9, Character, Posture, 0);
                    AppendGenericScript(&Behavior, 0x12, Character, Posture, 0);
                    AppendGenericScript(&Behavior, 0x18, Character, Posture, 0);
                    AppendGenericScript(&Behavior, 0x1a, Character, Posture, 0);
                }
                else
                {
                    AppendGenericScript(&Behavior, 9, Character, Posture, 0);
                    AppendGenericScript(&Behavior, 0x13, Character, Posture, 0);
                }
                AppendGenericScript(&Behavior, 0x1c, Character, Posture, 0);
                // 06f00f31-06f00f56: a fresh FUN_06f49b00 lookup; the base's local faction.
                FContentBase const* Base =
                    FindContentPlayerState(Character->PlayerId)->GetBaseRecord();
                AppendGenericScript(&Behavior,
                                    (Base->LocalFaction == Character->Affiliation) ? 0x1d : 0x1e,
                                    Character, Posture, 0);
            }
        }

        if (bFriendly)
        {
            // 06f00f86-06f0107b.
            if (Vnpc != 0)
            {
                if (!BuildVnpcScripts(&Behavior, Character, Posture, Vnpc))
                    return CreateBarBehavior(Character, &Behavior);
            }
            else if (OfferKind != 0 && static_cast<unsigned int>(RollPercent()) < 0x14 &&
                     !IsShopRole(Character) && !IsBartenderRole(Character))
            {
                AppendGenericScript(&Behavior, 1, Character, Posture, 0);
                AppendGenericScript(&Behavior, 4, Character, Posture, 0);
            }
            BuildOfferScripts(Character, &Behavior, Posture, State, Vnpc != 0, OfferKind);
        }
        else if (IsShopRole(Character) || IsBartenderRole(Character))
        {
            // 06f01165-06f011a3.
            if (Vnpc != 0 && !BuildVnpcScripts(&Behavior, Character, Posture, Vnpc))
                return CreateBarBehavior(Character, &Behavior);
            BuildOfferScripts(Character, &Behavior, Posture, State, Vnpc != 0, OfferKind);
        }
        else
        {
            // 06f010cc-06f01163.
            if (Vnpc == 0)
                AppendGenericScript(&Behavior, 1, Character, Posture, 0);
            AppendGenericScript(&Behavior, 3, Character, Posture, 0);
            if (Vnpc != 0)
            {
                AppendGenericScript(&Behavior, 6, Character, Posture, 0);
                AppendGenericScript(&Behavior, 7, Character, Posture, 0);
            }
        }
        // 06f011ac-06f011cb: FUN_06f015e0, then the description's dtor.
        return CreateBarBehavior(Character, &Behavior);
    }
}

// FUN_06f00b60 (06f00b60-06f00b80): node +0x20 is the character's Placement; node +8 the
// character.
void __cdecl StartBarCharacterScript(FBarCharacterList::iterator Character)
{
    if (Character->Placement == -1)
        return;
    Character->SetBehavior(BuildBarBehavior(&*Character));
}

// FUN_06f02330 (06f02330-06f02657).
void __cdecl LoadGcsExclusions(char const* Path)
{
    INI_Reader Reader;
    // [ESP+0x1c] (06f023ec / 06f02436): one frame slot for the whole load, written only by
    // pub::GetVoiceID, which leaves it untouched for an empty name -- an empty "voice" reuses
    // the previous voice's id. Before the first voice line retail holds stack garbage there;
    // the port starts at 0.
    unsigned int VoiceId = 0;
    if (!OpenContentDataIni(Path, &Reader))
        return;

    while (Reader.read_header())
    {
        if (!Reader.is_header("GCS_Exclusions"))
            continue;

        // 06f023ac: the current voice starts at end(); a "script" before any "voice"
        // writes through it in retail as well (GCSExcl.ini always opens with a voice).
        std::map<unsigned int, std::vector<std::string>>::iterator Current = GGcsExclusions.end();
        while (Reader.read_value())
        {
            if (Reader.is_value("voice"))
            {
                pub::GetVoiceID(VoiceId, Reader.get_value_string());
                Current = GGcsExclusions.find(VoiceId);                        // FUN_06f7b0f0
                if (Current == GGcsExclusions.end())
                {
                    // 06f02422-06f024e5: an empty vector temporary (EH state 1), the pair
                    // copied from it (FUN_06f029e0, state 2), insert FUN_06f027d0; the pair
                    // dies first (06f02470-06f02498), the empty vector after (06f024a8-).
                    std::vector<std::string> const Empty;
                    std::pair<unsigned int const, std::vector<std::string>> const Entry(VoiceId, Empty);
                    Current = GGcsExclusions.insert(Entry).first;               // FUN_06f027d0
                }
            }
            else if (Reader.is_value("script"))
            {
                std::string Script(Reader.get_value_string());
                Current->second.insert(Current->second.end(), 1, Script);       // FUN_06f031d0
            }
        }
    }
}
