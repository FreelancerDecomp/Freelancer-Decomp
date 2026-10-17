// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CPlayerTradeOffer -- one player's side of a trade in progress (retail 0x0631d620-
// 0x0631d960 plus ICF-shared accessors). Declared in common_tradeoffer.h.
#include "common_tradeoffer.h"

// ---- CPlayerTradeOffer (see class comment in common_tradeoffer.h) ----
//
// Re-audited 2026-09-09 (D59): this class previously had no real data members at all -- every
// accessor below read/wrote raw offsets on `this` via reinterpret_cast because no ported caller
// had ever needed to actually construct one (until 2026-09-24 Server-side trade logic used its
// own FTradeOffer struct; IServerImpl::InitiateTrade now news real ones). The special members below establish the real,
// byte-confirmed layout, so the accessors now go through named members instead of hardcoded
// offsets; observable behavior at each documented offset is unchanged.

CPlayerTradeOffer::CPlayerTradeOffer()
    : SourcePlayerId(0)
    , TargetPlayerId(0)
    , TargetOffer(nullptr)
    , MoneyOffered(0)
    , Accepted(false)
{
    // Common.dll.txt 0x0631d8a0-0x0631d8db. List starts empty (retail allocates just the
    // list's own empty sentinel head).
}

CPlayerTradeOffer::CPlayerTradeOffer(CPlayerTradeOffer const& Other)
    : SourcePlayerId(Other.SourcePlayerId)
    , TargetPlayerId(Other.TargetPlayerId)
    , TargetOffer(Other.TargetOffer)
    , List(Other.List)
    , MoneyOffered(Other.MoneyOffered)
    , Accepted(Other.Accepted)
{
    // Common.dll.txt 0x0631d630-0x0631d710.
}

CPlayerTradeOffer& CPlayerTradeOffer::operator=(CPlayerTradeOffer const& Other)
{
    // Common.dll.txt 0x0631d720-0x0631d82e. No whole-function self test: the only one is the
    // std::list operator=' own `&List == &Other.List` check (0631d73e), around the list copy.
    SourcePlayerId = Other.SourcePlayerId;
    TargetPlayerId = Other.TargetPlayerId;
    TargetOffer = Other.TargetOffer;
    List = Other.List;
    MoneyOffered = Other.MoneyOffered;
    Accepted = Other.Accepted;
    return *this;
}

CPlayerTradeOffer::~CPlayerTradeOffer()
{
    // Common.dll.txt 0x0631d8e0-0x0631d932. Items' own destructor frees its storage; nothing
    // else here owns any resources.
}

unsigned long CPlayerTradeOffer::GetMoneyOffered()
{
    return MoneyOffered;
}

void CPlayerTradeOffer::SetMoneyOffered(unsigned long Money)
{
    MoneyOffered = Money;
}

bool CPlayerTradeOffer::GetAccepted()
{
    return Accepted;
}

void CPlayerTradeOffer::SetAccepted(bool bAccepted)
{
    Accepted = bAccepted;
}

void CPlayerTradeOffer::SetSourcePlayerID(unsigned int Id)
{
    SourcePlayerId = Id;
}

// Previously declared with no definition (never odr-used, so no prior link error). Added
// alongside SetTargetPlayerID below since both use the same +0x8 field.
unsigned int CPlayerTradeOffer::GetTargetPlayerID()
{
    return TargetPlayerId;
}

void CPlayerTradeOffer::SetTargetPlayerID(unsigned int Id)
{
    TargetPlayerId = Id;
}

void CPlayerTradeOffer::SetTargetOffer(CPlayerTradeOffer* Offer)
{
    TargetOffer = Offer;
}

// 0x06261dc0: LEA EAX,[ECX+0x10] / RET.
EquipDescList& CPlayerTradeOffer::GetList()
{
    return List;
}

// 0x062d1ad0: MOV EAX,[ECX+4] / RET.
unsigned int CPlayerTradeOffer::GetSourcePlayerID()
{
    return SourcePlayerId;
}

// 0x0629fb90: MOV EAX,[ECX+0xc] / RET.
CPlayerTradeOffer* CPlayerTradeOffer::GetTargetOffer()
{
    return TargetOffer;
}
