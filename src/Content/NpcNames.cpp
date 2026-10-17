// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU42, the NPC name part (06f31650-06f32000). See NpcNames.h.
//
// Listing: DLLS\BIN\Content.dll.txt. The mad-lib ids 0x3f17..0x3f24 are the name patterns the
// client formats; the values appended here are what FUN_06f322a0 / FUN_06f32320 /
// FUN_06f32350 / FUN_06f323c0 (NpcShips.h) read back out of the two FmtStr.

#include "NpcNames.h"

#include "FactionProps.h"
#include "NpcShips.h"            // FMsnNpc, the name value readers
#include "common_server.h"       // pub::Reputation, pub::GetType, pub::GetShipArchSTRID

#include <cstdint>

int GNpcNameReputationCount = 0;

// FUN_06f31650 (06f31650-06f3166f).
FNpcNameRequest::FNpcNameRequest()
    : NameIndex(0)
    , Desig(0)
    , DesigNumber(0)
    , FormationSize(1)
    , EscortNumber(0)
    , Field_0x14(0)
    , RankLevel(0)
    , ShipArchId(0)
{
}

// FUN_06f31670 (06f31670-06f31694). +0x14..+0x1c are left unwritten, as in retail (the only
// caller, TU29 06ef4d48 / 06ef4d53, writes +0x18 and +0x1c before every BuildNpcName;
// +0x14 has no reader).
FNpcNameRequest::FNpcNameRequest(int InNameIndex, int InFormationSize, int InEscortNumber,
                                 int InDesig, int InDesigNumber)
    : NameIndex(InNameIndex)
    , Desig(InDesig)
    , DesigNumber(InDesigNumber)
    , FormationSize(InFormationSize)
    , EscortNumber(InEscortNumber)
{
}

// Inlined in FUN_06f31910 (06f3194c-06f31980) and FUN_06f31d40 (06f31e2f-06f31e68).
FNpcNameReputation::FNpcNameReputation(FNpcNameParams const* Params)
{
    pub::Reputation::Alloc(Reputation, Params->First, Params->Second);
    pub::Reputation::SetAffiliation(Reputation, Params->Affiliation);
    ++GNpcNameReputationCount;
}

// FUN_06f316c0 (06f316c0-06f318f2).
FNpcNameReputation::~FNpcNameReputation()
{
    int PilotFirst;
    int PilotLast;
    int LargeShipName = 1;
    int Desig = 1;
    int DesigNumber;
    {
        // 06f316ee-06f31781: two empty FmtStr locals, pub::Reputation::GetName(+8, First,
        // Second).
        FmtStr First;
        FmtStr Second;
        pub::Reputation::GetName(Reputation, First, Second);
        // 06f31787-06f3179b: FUN_06f322a0 -> the pilot first / last name.
        GetSecondNameValues(&First, &Second, &PilotFirst, &PilotLast);
        // 06f317a0-06f317ca: Second 0x3f1a / 0x3f1c -> the large ship name, node 3 (the
        // FUN_06f32320 body inlined).
        if (Second.MadLibId == 0x3f1a || Second.MadLibId == 0x3f1c)
            LargeShipName = (int)Second.Head->Next->Next->Value;
        // 06f317cd-06f31803: the switch table 06f318f4 / 06f31900 (the FUN_06f32350 body
        // inlined): the formation designation.
        ::GetFirstNameValue(&First, &Second, &Desig);
        // 06f31810-06f31833: FUN_06f323c0 -> the designation number; FUN_06f32420 rolls a value
        // the destructor never reads (the rand() call stays).
        GetFirstNameLastValue(&First, &Second, &DesigNumber);
        int Unused;
        RollNpcNameValue(&First, &Second, &Unused);
        // 06f31841-06f31854: ~FmtStr Second, ~FmtStr First.
    }

    // 06f31856-06f318b1: the faction name tables of the affiliation (FUN_06eb6bb0 ->
    // FUN_06f51370, not null-tested); each used entry queued for release (bImmediate 0).
    unsigned int Affiliation = GetAffiliation();
    FFactionNames* Names = GetFactionNames(&Affiliation);
    if (PilotFirst != 1 || PilotLast != 1)
        Names->ReleasePilotName(PilotFirst, PilotLast, false);
    if (LargeShipName != 1)
        Names->ReleaseLargeShipName(LargeShipName, false);
    if (Desig != 1)
        Names->ReleaseFormationDesig(Desig, DesigNumber, false);

    // 06f318b1-06f318c3: pub::Reputation::Free when a reputation was allocated.
    if (Reputation != -1)
        pub::Reputation::Free(Reputation);
    // 06f318c6-06f318ce.
    --GNpcNameReputationCount;
    // 06f318db: the base dtor FUN_06ea8cc0 (vptr reset only) follows.
}

// FUN_06f31ec0 (06f31ec0-06f31fff).
bool FNpcNameReputation::GetFormationDesig(int* Out) const
{
    FmtStr First;
    FmtStr Second;
    pub::Reputation::GetName(Reputation, First, Second);
    // 06f31f79-06f31fc9: *Out = 1, then the switch table 06f32004 / 06f32010 (the FUN_06f32350
    // body inlined).
    return ::GetFirstNameValue(&First, &Second, Out);
}

// FUN_06f32020 (06f32020-06f32162).
bool FNpcNameReputation::GetFormationDesigNumber(int* Out) const
{
    FmtStr First;
    FmtStr Second;
    pub::Reputation::GetName(Reputation, First, Second);
    // 06f320d9-06f3212c: *Out = -1, then the switch table 06f32168 / 06f32174 (the FUN_06f323c0
    // body inlined: node 4, 0x3f24 node 3).
    return GetFirstNameLastValue(&First, &Second, Out);
}

// FUN_06f32190 (06f32190-06f32298).
bool FNpcNameReputation::RollNameValue(int* Out) const
{
    FmtStr First;
    FmtStr Second;
    pub::Reputation::GetName(Reputation, First, Second);
    // 06f3224c-06f3226c: rand() * 5, signed / 32768 (CDQ, AND 0x7fff, SAR 0xf), + 1.
    return RollNpcNameValue(&First, &Second, Out);
}

// FUN_06f319b0 (06f319b0-06f319cc).
bool __cdecl PickNpcNameGender(unsigned int Affiliation, FNpcNameParams* Params)
{
    return GetFactionProps(&Affiliation)->PickGender(&Params->Gender);
}

// FUN_06f319d0 (06f319d0-06f31d3b).
bool __cdecl BuildNpcName(unsigned int Affiliation, FNpcNameRequest const* Request,
                          FNpcNameParams* Out, bool bNamedLeader)
{
    // 06f319d3-06f319f9.
    Out->Affiliation = Affiliation;
    FFactionNames* Names = GetFactionNames(&Affiliation);

    if (Request->NameIndex != 0)
    {
        // 06f31a03-06f31a2c: the arch type tested against 0x30000 (TEST + SETNZ) = FIGHTER
        // 0x10000 | FREIGHTER 0x20000 (Common's object type name table 063ec8a0).
        unsigned int ShipType;
        pub::GetType(Request->ShipArchId, ShipType);
        bool const bFighterOrFreighter = (ShipType & 0x30000) != 0;

        // 06f31a29-06f31a64: the first name's pattern.
        unsigned int FirstId;
        if (Request->FormationSize == 1)
            FirstId = bFighterOrFreighter ? 0x3f17 : 0x3f19;
        else if (bFighterOrFreighter)
            FirstId = Request->EscortNumber == 0 ? 0x3f20 : 0x3f1d;
        else
            FirstId = 0x3f1b;
        Out->First.begin_mad_lib(FirstId);
        // 06f31a6a-06f31a89: short group name, the type name.
        Out->First.append_string(Names->GetShortGroupName());
        Out->First.append_string(Names->GetNameId(Request->NameIndex));
        // 06f31a8b-06f31ab6: the formation designation - the request's own (counted as used)
        // or a new reserved one.
        int Desig = Request->Desig;
        int DesigNumber = Request->DesigNumber;
        if (Desig != 0)
            Names->AddFormationDesigUse(Desig, DesigNumber);
        else
            Names->PickFormationDesig(&Desig, &DesigNumber, true);
        // 06f31abb-06f31ad3.
        Out->First.append_string(static_cast<unsigned int>(Desig));
        Out->First.append_int(static_cast<unsigned int>(DesigNumber));
        Out->First.end_mad_lib();

        if (bFighterOrFreighter)
        {
            // 06f31adf-06f31b19: the second name's pattern.
            unsigned int SecondId;
            if (Request->FormationSize == 1)
                SecondId = 0x3f18;
            else if (Request->EscortNumber == 0)
                SecondId = 0x3f21;
            else
                SecondId = 0x3f1e;
            Out->Second.begin_mad_lib(SecondId);
            int PilotFirst = 0;
            int PilotLast = 0;
            if (Out->Second.MadLibId != 0x3f1e)
            {
                // 06f31b1b-06f31b64: rank designation, then a reserved pilot first + last name.
                Out->Second.append_string(Names->GetRankDesig(&Request->RankLevel));
                Names->PickPilotName(&PilotFirst, &PilotLast, Out->Gender, true);
                Out->Second.append_string(static_cast<unsigned int>(PilotFirst));
                Out->Second.append_string(static_cast<unsigned int>(PilotLast));
                Out->Second.end_mad_lib();
                return true;
            }
            // 06f31b74-06f31bc5: "Escort" (0x3f1f), the escort number, rank designation, a
            // reserved last name only (null OutFirst).
            Out->Second.append_string(0x3f1f);
            Out->Second.append_int(static_cast<unsigned int>(Request->EscortNumber));
            Out->Second.append_string(Names->GetRankDesig(&Request->RankLevel));
            Names->PickPilotName(nullptr, &PilotLast, Out->Gender, true);
            Out->Second.append_string(static_cast<unsigned int>(PilotLast));
            Out->Second.end_mad_lib();
            return true;
        }

        // 06f31bd5-06f31c32: any other ship type: 0x3f1a / 0x3f1c, the ship arch's name, the
        // faction's large ship designation, a reserved large ship name.
        Out->Second.begin_mad_lib(Request->FormationSize == 1 ? 0x3f1a : 0x3f1c);
        unsigned int ShipName;
        pub::GetShipArchSTRID(Request->ShipArchId, ShipName);
        Out->Second.append_string(ShipName);
        Out->Second.append_string(static_cast<unsigned int>(Names->GetLargeShipDesig()));
        int LargeShipName;
        Names->PickLargeShipName(&LargeShipName, true);
        Out->Second.append_string(static_cast<unsigned int>(LargeShipName));
        Out->Second.end_mad_lib();
        return true;
    }

    // 06f31c42-06f31cb1: a formation leader also gets 0x3f24 with the short group name and a
    // designation.
    if (bNamedLeader)
    {
        Out->First.begin_mad_lib(0x3f24);
        Out->First.append_string(Names->GetShortGroupName());
        int Desig = Request->Desig;
        int DesigNumber = Request->DesigNumber;
        if (Desig != 0)
            Names->AddFormationDesigUse(Desig, DesigNumber);
        else
            Names->PickFormationDesig(&Desig, &DesigNumber, true);
        Out->First.append_string(static_cast<unsigned int>(Desig));
        Out->First.append_int(static_cast<unsigned int>(DesigNumber));
        Out->First.end_mad_lib();
    }

    // 06f31cb7-06f31d2c: 0x3f22 with the rank designation, or 0x3f23; then a reserved pilot
    // first + last name.
    if (Request->RankLevel != 0)
    {
        Out->Second.begin_mad_lib(0x3f22);
        Out->Second.append_string(Names->GetRankDesig(&Request->RankLevel));
    }
    else
    {
        Out->Second.begin_mad_lib(0x3f23);
    }
    int PilotFirst = 0;
    int PilotLast = 0;
    Names->PickPilotName(&PilotFirst, &PilotLast, Out->Gender, true);
    Out->Second.append_string(static_cast<unsigned int>(PilotFirst));
    Out->Second.append_string(static_cast<unsigned int>(PilotLast));
    Out->Second.end_mad_lib();
    return true;
}

// FUN_06f31910 (06f31910-06f319a6).
bool __cdecl CreateNpcNameReputation(FNpcNameParams const* Params, FContentPlayerStateBase** Out)
{
    // 06f31927-06f31980: operator new(0xc) and the inlined ctor (Alloc, SetAffiliation, ++count)
    // under EH states 0/1 (06fae9c0 delete, 06fae9cb base dtor).
    FNpcNameReputation* Object = new FNpcNameReputation(Params);
    *Out = Object;
    return Object != nullptr;
}

// FUN_06f31d40 (06f31d40-06f31eb1).
bool __cdecl CreateMsnNpcReputation(FMsnNpc const* Npc, FContentPlayerStateBase** Out)
{
    // 06f31d58-06f31e0c: a local name block (two inline FmtStr ctors, +0x30 = 0, +0x34 = -1;
    // EH state 0 = its dtor FUN_06ef4e60): an empty first name, the second name the mad lib
    // Npc->IndividualName, Affiliation = Npc->Affiliation.
    FNpcNameParams Name;
    Name.Second.begin_mad_lib(static_cast<unsigned int>(Npc->IndividualName));
    Name.Second.end_mad_lib();
    Name.Affiliation = Npc->Affiliation;

    // 06f31e0a-06f31e68: operator new(0xc) and the inlined ctor with &Name (EH states 1/2).
    FNpcNameReputation* Object = new FNpcNameReputation(&Name);
    *Out = Object;
    return Object != nullptr;
    // 06f31e7d-06f31e9d: the block's dtor - ~FmtStr Second, ~FmtStr First (EH state 3).
}
