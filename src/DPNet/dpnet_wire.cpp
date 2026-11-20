// Freelancer Decomp: DPNet, a DirectPlay 8 protocol implementation (port-only, not part of retail).
// Wire formats; see dpnet_wire.h.

#include "dpnet_wire.h"

#include <algorithm>
#include <cstdio>

namespace DPNet
{
    const FGuid kProviderTcpIp = {0xEBFE7BA0, 0x628D, 0x11D2, {0xAE, 0x0F, 0x00, 0x60, 0x97, 0xB0, 0x14, 0x11}};

    // ---------------------------------------------------------------------------------------
    // FGuid

    bool FGuid::IsZero() const
    {
        if (Data1 != 0 || Data2 != 0 || Data3 != 0)
            return false;
        for (uint8_t B : Data4)
            if (B != 0)
                return false;
        return true;
    }

    bool FGuid::operator==(const FGuid& Other) const
    {
        if (Data1 != Other.Data1 || Data2 != Other.Data2 || Data3 != Other.Data3)
            return false;
        for (int i = 0; i < 8; ++i)
            if (Data4[i] != Other.Data4[i])
                return false;
        return true;
    }

    void FGuid::Write(FWriter& W) const
    {
        W.U32(Data1);
        W.U16(Data2);
        W.U16(Data3);
        W.Bytes(Data4, 8);
    }

    FGuid FGuid::Read(FReader& R)
    {
        FGuid G;
        G.Data1 = R.U32();
        G.Data2 = R.U16();
        G.Data3 = R.U16();
        R.Read(G.Data4, 8);
        return G;
    }

    std::string FGuid::ToString() const
    {
        char Text[40];
        std::snprintf(Text, sizeof(Text), "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                      (unsigned)Data1, (unsigned)Data2, (unsigned)Data3, Data4[0], Data4[1], Data4[2],
                      Data4[3], Data4[4], Data4[5], Data4[6], Data4[7]);
        return Text;
    }

    // ---------------------------------------------------------------------------------------
    // Wide strings

    void WriteWide(FBytes& Out, const std::u16string& Text)
    {
        FWriter W(Out);
        for (char16_t C : Text)
            W.U16((uint16_t)C);
        W.U16(0);
    }

    std::u16string ReadWide(const uint8_t* Data, size_t Size)
    {
        std::u16string Text;
        for (size_t i = 0; i + 1 < Size; i += 2)
        {
            const char16_t C = (char16_t)(Data[i] | (Data[i + 1] << 8));
            if (C == 0)
                break;
            Text.push_back(C);
        }
        return Text;
    }

    std::u16string WideFromAscii(const char* Text)
    {
        std::u16string Out;
        for (; *Text != 0; ++Text)
            Out.push_back((char16_t)(unsigned char)*Text);
        return Out;
    }

    static size_t WideSize(const std::u16string& Text) { return (Text.size() + 1) * 2; }

    static FBytes WideBytes(const std::u16string& Text)
    {
        FBytes Out;
        WriteWide(Out, Text);
        return Out;
    }

    static FBytes AsciiBytes(const std::string& Text)
    {
        FBytes Out(Text.begin(), Text.end());
        Out.push_back(0);
        return Out;
    }

    // A field given by an (offset, size) pair, with offsets counted from Base. False when it
    // does not lie inside the message.
    static bool Field(const uint8_t* Data, size_t Size, size_t Base, uint32_t Offset, uint32_t FieldSize,
                      const uint8_t*& OutData)
    {
        OutData = nullptr;
        if (Offset == 0 || FieldSize == 0)
            return true;
        if (Base > Size || Offset > Size - Base || FieldSize > Size - Base - Offset)
            return false;
        OutData = Data + Base + Offset;
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // [MC-DPL8R] frames

    void FConnectFrame::Write(FBytes& Out) const
    {
        FWriter W(Out);
        W.U8(Command);
        W.U8(Op);
        W.U8(MsgId);
        W.U8(RspId);
        W.U32(Version);
        W.U32(SessId);
        W.U32(Timestamp);
    }

    void FSackFrame::Write(FBytes& Out) const
    {
        FWriter W(Out);
        uint8_t OutFlags = (uint8_t)(Flags & SackFlags::Response);
        if ((uint32_t)SackMask != 0)
            OutFlags |= SackFlags::SackMask1;
        if ((uint32_t)(SackMask >> 32) != 0)
            OutFlags |= SackFlags::SackMask2;
        if ((uint32_t)SendMask != 0)
            OutFlags |= SackFlags::SendMask1;
        if ((uint32_t)(SendMask >> 32) != 0)
            OutFlags |= SackFlags::SendMask2;
        W.U8(Command::CFrame);
        W.U8(ExtOp::Sack);
        W.U8(OutFlags);
        W.U8(Retry);
        W.U8(NSeq);
        W.U8(NRcv);
        W.U16(0);
        W.U32(Timestamp);
        if (OutFlags & SackFlags::SackMask1)
            W.U32((uint32_t)SackMask);
        if (OutFlags & SackFlags::SackMask2)
            W.U32((uint32_t)(SackMask >> 32));
        if (OutFlags & SackFlags::SendMask1)
            W.U32((uint32_t)SendMask);
        if (OutFlags & SackFlags::SendMask2)
            W.U32((uint32_t)(SendMask >> 32));
    }

    bool IsDataFrame(const uint8_t* Data, size_t Size) { return Size >= 4 && (Data[0] & Command::Data) != 0; }

    bool IsCommandFrame(const uint8_t* Data, size_t Size)
    {
        return Size >= 12 && (Data[0] == Command::CFrame || Data[0] == (Command::CFrame | Command::Poll));
    }

    bool DecodeCommandFrame(const uint8_t* Data, size_t Size, FCommandFrame& Out)
    {
        if (!IsCommandFrame(Data, Size))
            return false;
        FReader R(Data, Size);
        Out.Command = R.U8();
        Out.Op = R.U8();
        if (Out.Op == ExtOp::Sack)
        {
            FSackFrame& S = Out.Sack;
            S.Flags = R.U8();
            S.Retry = R.U8();
            S.NSeq = R.U8();
            S.NRcv = R.U8();
            R.U16();
            S.Timestamp = R.U32();
            uint32_t Parts[4] = {};
            const uint8_t Bits[4] = {SackFlags::SackMask1, SackFlags::SackMask2, SackFlags::SendMask1,
                                     SackFlags::SendMask2};
            for (int i = 0; i < 4; ++i)
                if (S.Flags & Bits[i])
                    Parts[i] = R.U32();
            S.SackMask = Parts[0] | ((uint64_t)Parts[1] << 32);
            S.SendMask = Parts[2] | ((uint64_t)Parts[3] << 32);
            return R.Ok();
        }
        if (Out.Op != ExtOp::Connect && Out.Op != ExtOp::Connected && Out.Op != ExtOp::ConnectedSigned &&
            Out.Op != ExtOp::HardDisconnect)
            return false;
        if (Size < 16)
            return false;
        FConnectFrame& C = Out.Connect;
        C.Command = Out.Command;
        C.Op = Out.Op;
        C.MsgId = R.U8();
        C.RspId = R.U8();
        C.Version = R.U32();
        C.SessId = R.U32();
        C.Timestamp = R.U32();
        return R.Ok();
    }

    void FDataHeader::Write(FBytes& Out) const
    {
        uint8_t OutControl = (uint8_t)(Control & ~(Control::Sack1 | Control::Sack2 | Control::Send1 | Control::Send2));
        if ((uint32_t)SackMask != 0)
            OutControl |= Control::Sack1;
        if ((uint32_t)(SackMask >> 32) != 0)
            OutControl |= Control::Sack2;
        if ((uint32_t)SendMask != 0)
            OutControl |= Control::Send1;
        if ((uint32_t)(SendMask >> 32) != 0)
            OutControl |= Control::Send2;
        FWriter W(Out);
        W.U8(Command);
        W.U8(OutControl);
        W.U8(Seq);
        W.U8(NRcv);
        if (OutControl & Control::Sack1)
            W.U32((uint32_t)SackMask);
        if (OutControl & Control::Sack2)
            W.U32((uint32_t)(SackMask >> 32));
        if (OutControl & Control::Send1)
            W.U32((uint32_t)SendMask);
        if (OutControl & Control::Send2)
            W.U32((uint32_t)(SendMask >> 32));
    }

    size_t FDataHeader::Read(const uint8_t* Data, size_t Size, FDataHeader& Out)
    {
        if (!IsDataFrame(Data, Size))
            return 0;
        FReader R(Data, Size);
        Out.Command = R.U8();
        Out.Control = R.U8();
        Out.Seq = R.U8();
        Out.NRcv = R.U8();
        uint32_t Parts[4] = {};
        const uint8_t Bits[4] = {Control::Sack1, Control::Sack2, Control::Send1, Control::Send2};
        for (int i = 0; i < 4; ++i)
            if (Out.Control & Bits[i])
                Parts[i] = R.U32();
        Out.SackMask = Parts[0] | ((uint64_t)Parts[1] << 32);
        Out.SendMask = Parts[2] | ((uint64_t)Parts[3] << 32);
        if (!R.Ok())
            return 0;
        return R.Position();
    }

    // 2.2.3: 2-byte headers (padded to a dword), then the payloads, each but the last padded
    // to a dword.
    bool DecodeCoalesced(const uint8_t* Data, size_t Size, std::vector<FSubPayload>& Out)
    {
        Out.clear();
        size_t Pos = 0;
        bool bEnd = false;
        while (!bEnd)
        {
            if (Out.size() == kMaxCoalesced || Size - Pos < 2)
                return false;
            const uint8_t SizeLo = Data[Pos];
            const uint8_t Cmd = Data[Pos + 1];
            Pos += 2;
            FSubPayload Part;
            Part.Flags = (uint8_t)(Cmd & (Command::Reliable | Command::Sequential | Command::UserMask));
            Part.Size = SizeLo | ((size_t)((Cmd & Command::CoalesceBigMask) >> 3) << 8);
            bEnd = (Cmd & Command::EndCoalesce) != 0;
            Out.push_back(Part);
        }
        if (Out.size() % 2 != 0)
        {
            if (Size - Pos < 2)
                return false;
            Pos += 2;
        }
        for (size_t i = 0; i < Out.size(); ++i)
        {
            if (Out[i].Size > Size - Pos)
                return false;
            Out[i].Data = Data + Pos;
            Pos += Out[i].Size;
            if (i + 1 < Out.size())
            {
                const size_t Pad = (4 - (Pos & 3)) & 3;
                if (Pad > Size - Pos)
                    return false;
                Pos += Pad;
            }
        }
        return true;
    }

    size_t CoalescedSize(const std::vector<size_t>& PartSizes)
    {
        size_t Pos = PartSizes.size() * 2;
        if (PartSizes.size() % 2 != 0)
            Pos += 2;
        for (size_t i = 0; i < PartSizes.size(); ++i)
        {
            Pos += PartSizes[i];
            if (i + 1 < PartSizes.size())
                Pos += (4 - (Pos & 3)) & 3;
        }
        return Pos;
    }

    void EncodeCoalesced(const std::vector<FSubPayload>& Parts, FBytes& Out)
    {
        const size_t Base = Out.size();
        for (size_t i = 0; i < Parts.size(); ++i)
        {
            uint8_t Cmd = (uint8_t)(Parts[i].Flags & (Command::Reliable | Command::Sequential | Command::UserMask));
            Cmd |= (uint8_t)(((Parts[i].Size >> 8) & 7) << 3);
            if (i + 1 == Parts.size())
                Cmd |= Command::EndCoalesce;
            Out.push_back((uint8_t)Parts[i].Size);
            Out.push_back(Cmd);
        }
        if (Parts.size() % 2 != 0)
            Out.insert(Out.end(), 2, 0);
        for (size_t i = 0; i < Parts.size(); ++i)
        {
            Out.insert(Out.end(), Parts[i].Data, Parts[i].Data + Parts[i].Size);
            if (i + 1 < Parts.size())
                Out.insert(Out.end(), (4 - ((Out.size() - Base) & 3)) & 3, 0);
        }
    }

    // ---------------------------------------------------------------------------------------
    // [MC-DPLHP] enumeration

    void FEnumQuery::Write(FBytes& Out) const
    {
        FWriter W(Out);
        W.U8(0);
        W.U8(kEnumQueryCommand);
        W.U16(EnumPayload);
        W.U8(bHasApplication ? 0x01 : 0x02);
        if (bHasApplication)
            Application.Write(W);
        W.Bytes(UserData);
    }

    bool FEnumQuery::Read(const uint8_t* Data, size_t Size, FEnumQuery& Out)
    {
        FReader R(Data, Size);
        if (R.U8() != 0 || R.U8() != kEnumQueryCommand)
            return false;
        Out.EnumPayload = R.U16();
        const uint8_t QueryType = R.U8();
        if (QueryType != 0x01 && QueryType != 0x02)
            return false;
        Out.bHasApplication = QueryType == 0x01;
        if (Out.bHasApplication)
            Out.Application = FGuid::Read(R);
        if (!R.Ok())
            return false;
        Out.UserData.assign(Data + R.Position(), Data + Size);
        return true;
    }

    // Fixed part: lead, command, EnumPayload, ReplyOffset, ResponseSize, then the 0x50-byte
    // application description. Offsets count from ReplyOffset (byte 4).
    static constexpr size_t kEnumResponseFixed = 4 + 8 + 0x50;

    void FEnumResponse::Write(FBytes& Out) const
    {
        const size_t NameSize = WideSize(SessionName);
        const size_t NameOff = kEnumResponseFixed - 4;
        const size_t ResOff = NameOff + NameSize;
        const size_t DataOff = ResOff + ApplicationReserved.size();

        FWriter W(Out);
        W.U8(0);
        W.U8(kEnumResponseCommand);
        W.U16(EnumPayload);
        W.U32(UserData.empty() ? 0 : (uint32_t)DataOff);
        W.U32((uint32_t)UserData.size());
        W.U32(0x50);
        W.U32(Flags);
        W.U32(MaxPlayers);
        W.U32(CurrentPlayers);
        W.U32((uint32_t)NameOff);
        W.U32((uint32_t)NameSize);
        W.U32(0);   // password: never sent
        W.U32(0);
        W.U32(0);   // reserved data: never used
        W.U32(0);
        W.U32(ApplicationReserved.empty() ? 0 : (uint32_t)ResOff);
        W.U32((uint32_t)ApplicationReserved.size());
        Instance.Write(W);
        Application.Write(W);
        WriteWide(Out, SessionName);
        W.Bytes(ApplicationReserved);
        W.Bytes(UserData);
    }

    bool FEnumResponse::Read(const uint8_t* Data, size_t Size, FEnumResponse& Out)
    {
        FReader R(Data, Size);
        if (R.U8() != 0 || R.U8() != kEnumResponseCommand)
            return false;
        Out.EnumPayload = R.U16();
        const uint32_t DataOff = R.U32();
        const uint32_t DataSize = R.U32();
        if (R.U32() != 0x50)
            return false;
        Out.Flags = R.U32();
        Out.MaxPlayers = R.U32();
        Out.CurrentPlayers = R.U32();
        const uint32_t NameOff = R.U32();
        const uint32_t NameSize = R.U32();
        R.U32();
        R.U32();
        R.U32();
        R.U32();
        const uint32_t ResOff = R.U32();
        const uint32_t ResSize = R.U32();
        Out.Instance = FGuid::Read(R);
        Out.Application = FGuid::Read(R);
        if (!R.Ok())
            return false;
        const uint8_t* P = nullptr;
        if (!Field(Data, Size, 4, NameOff, NameSize, P))
            return false;
        Out.SessionName = P != nullptr ? ReadWide(P, NameSize) : std::u16string();
        if (!Field(Data, Size, 4, ResOff, ResSize, P))
            return false;
        Out.ApplicationReserved = P != nullptr ? FBytes(P, P + ResSize) : FBytes();
        if (!Field(Data, Size, 4, DataOff, DataSize, P))
            return false;
        Out.UserData = P != nullptr ? FBytes(P, P + DataSize) : FBytes();
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // [MC-DPL8CS] core messages

    uint32_t MakeDpnid(uint32_t Index, uint32_t Version, const FGuid& Instance)
    {
        return ((Version << 20) | (Index & 0xFFFFF)) ^ Instance.Data1;
    }

    // The variable fields follow the fixed part in the order the specification lists them.
    void FPlayerConnectInfo::Write(FBytes& Out) const
    {
        const bool bEx = DnetVersion >= 7;
        const size_t Fixed = 4 + 12 * 4 + 32 + (bEx ? 8 : 0);
        const FBytes NameBytes = Name.empty() ? FBytes() : WideBytes(Name);
        const FBytes PasswordBytes = bHasPassword ? WideBytes(Password) : FBytes();
        const FBytes UrlBytes = Url.empty() ? FBytes() : AsciiBytes(Url);

        size_t Pos = Fixed - 4;
        auto Place = [&Pos](size_t FieldSize) -> uint32_t {
            if (FieldSize == 0)
                return 0;
            const uint32_t At = (uint32_t)Pos;
            Pos += FieldSize;
            return At;
        };
        const uint32_t AltOff = bEx ? Place(AlternateAddresses.size()) : 0;
        const uint32_t UrlOff = Place(UrlBytes.size());
        const uint32_t ConnectDataOff = Place(ConnectData.size());
        const uint32_t PasswordOff = Place(PasswordBytes.size());
        const uint32_t DataOff = Place(Data.size());
        const uint32_t NameOff = Place(NameBytes.size());

        FWriter W(Out);
        W.U32(CoreMsg::PlayerConnectInfo);
        W.U32(Flags);
        W.U32(DnetVersion);
        W.U32(NameOff);
        W.U32((uint32_t)NameBytes.size());
        W.U32(DataOff);
        W.U32((uint32_t)Data.size());
        W.U32(PasswordOff);
        W.U32((uint32_t)PasswordBytes.size());
        W.U32(ConnectDataOff);
        W.U32((uint32_t)ConnectData.size());
        W.U32(UrlOff);
        W.U32((uint32_t)UrlBytes.size());
        Instance.Write(W);
        Application.Write(W);
        if (bEx)
        {
            W.U32(AltOff);
            W.U32((uint32_t)AlternateAddresses.size());
            W.Bytes(AlternateAddresses);
        }
        W.Bytes(UrlBytes);
        W.Bytes(ConnectData);
        W.Bytes(PasswordBytes);
        W.Bytes(Data);
        W.Bytes(NameBytes);
    }

    bool FPlayerConnectInfo::Read(const uint8_t* Data, size_t Size, FPlayerConnectInfo& Out)
    {
        FReader R(Data, Size);
        if (R.U32() != CoreMsg::PlayerConnectInfo)
            return false;
        Out.Flags = R.U32();
        Out.DnetVersion = R.U32();
        // Name, data, password, connect data, URL.
        uint32_t Off[5];
        uint32_t Len[5];
        for (int i = 0; i < 5; ++i)
        {
            Off[i] = R.U32();
            Len[i] = R.U32();
        }
        Out.Instance = FGuid::Read(R);
        Out.Application = FGuid::Read(R);
        uint32_t AltOff = 0;
        uint32_t AltLen = 0;
        if (Out.DnetVersion >= 7)
        {
            AltOff = R.U32();
            AltLen = R.U32();
        }
        if (!R.Ok())
            return false;

        const uint8_t* P[6] = {};
        for (int i = 0; i < 5; ++i)
            if (!Field(Data, Size, 4, Off[i], Len[i], P[i]))
                return false;
        if (!Field(Data, Size, 4, AltOff, AltLen, P[5]))
            return false;
        Out.Name = P[0] != nullptr ? ReadWide(P[0], Len[0]) : std::u16string();
        Out.Data = P[1] != nullptr ? FBytes(P[1], P[1] + Len[1]) : FBytes();
        Out.bHasPassword = P[2] != nullptr;
        Out.Password = P[2] != nullptr ? ReadWide(P[2], Len[2]) : std::u16string();
        Out.ConnectData = P[3] != nullptr ? FBytes(P[3], P[3] + Len[3]) : FBytes();
        Out.Url.clear();
        if (P[4] != nullptr)
            for (uint32_t i = 0; i < Len[4] && P[4][i] != 0; ++i)
                Out.Url.push_back((char)P[4][i]);
        Out.AlternateAddresses = P[5] != nullptr ? FBytes(P[5], P[5] + AltLen) : FBytes();
        return true;
    }

    static constexpr size_t kSendConnectInfoFixed = 4 + 8 + 0x50 + 20;
    static constexpr size_t kNameTableEntrySize = 12 * 4;
    static constexpr size_t kMembershipSize = 4 * 4;

    void FSendConnectInfo::Write(FBytes& Out) const
    {
        // Every variable field, in the order it is packed from the end backwards.
        struct FPacked
        {
            FBytes Bytes;
            uint32_t Offset = 0;
        };
        FPacked PReply{Reply};
        FPacked PSession{bHasSessionName ? WideBytes(SessionName) : FBytes()};
        FPacked PPassword{bHasPassword ? WideBytes(Password) : FBytes()};
        FPacked PReserved{Reserved};
        FPacked PAppReserved{ApplicationReserved};
        std::vector<FPacked> PName(Entries.size()), PData(Entries.size()), PUrl(Entries.size());
        for (size_t i = 0; i < Entries.size(); ++i)
        {
            PName[i].Bytes = Entries[i].bHasName ? WideBytes(Entries[i].Name) : FBytes();
            PData[i].Bytes = Entries[i].Data;
            PUrl[i].Bytes = Entries[i].Url.empty() ? FBytes() : AsciiBytes(Entries[i].Url);
        }
        std::vector<FPacked*> Order = {&PReply, &PSession, &PPassword, &PReserved, &PAppReserved};
        for (size_t i = 0; i < Entries.size(); ++i)
        {
            Order.push_back(&PName[i]);
            Order.push_back(&PData[i]);
            Order.push_back(&PUrl[i]);
        }

        size_t Total = kSendConnectInfoFixed + Entries.size() * kNameTableEntrySize + Memberships.size() * kMembershipSize;
        for (FPacked* P : Order)
            Total += P->Bytes.size();
        size_t Tail = Total;
        for (FPacked* P : Order)
        {
            if (P->Bytes.empty())
                continue;
            Tail -= P->Bytes.size();
            P->Offset = (uint32_t)(Tail - 4);
        }

        const size_t Base = Out.size();
        FWriter W(Out);
        W.U32(CoreMsg::SendConnectInfo);
        W.U32(PReply.Offset);
        W.U32((uint32_t)PReply.Bytes.size());
        W.U32(0x50);
        W.U32(Flags);
        W.U32(MaxPlayers);
        W.U32(CurrentPlayers);
        W.U32(PSession.Offset);
        W.U32((uint32_t)PSession.Bytes.size());
        W.U32(PPassword.Offset);
        W.U32((uint32_t)PPassword.Bytes.size());
        W.U32(PReserved.Offset);
        W.U32((uint32_t)PReserved.Bytes.size());
        W.U32(PAppReserved.Offset);
        W.U32((uint32_t)PAppReserved.Bytes.size());
        Instance.Write(W);
        Application.Write(W);
        W.U32(Dpnid);
        W.U32(NameTableVersion);
        W.U32(NameTableVersionNotUsed);
        W.U32((uint32_t)Entries.size());
        W.U32((uint32_t)Memberships.size());
        for (size_t i = 0; i < Entries.size(); ++i)
        {
            const FNameTableEntry& E = Entries[i];
            W.U32(E.Dpnid);
            W.U32(E.Owner);
            W.U32(E.Flags);
            W.U32(E.Version);
            W.U32(E.VersionNotUsed);
            W.U32(E.DnetVersion);
            W.U32(PName[i].Offset);
            W.U32((uint32_t)PName[i].Bytes.size());
            W.U32(PData[i].Offset);
            W.U32((uint32_t)PData[i].Bytes.size());
            W.U32(PUrl[i].Offset);
            W.U32((uint32_t)PUrl[i].Bytes.size());
        }
        for (const FNameTableMembership& M : Memberships)
        {
            W.U32(M.Player);
            W.U32(M.Group);
            W.U32(M.Version);
            W.U32(M.VersionNotUsed);
        }
        Out.resize(Base + Total);
        for (FPacked* P : Order)
            if (!P->Bytes.empty())
                std::copy(P->Bytes.begin(), P->Bytes.end(), Out.begin() + (ptrdiff_t)(Base + 4 + P->Offset));
    }

    bool FSendConnectInfo::Read(const uint8_t* Data, size_t Size, FSendConnectInfo& Out)
    {
        FReader R(Data, Size);
        if (R.U32() != CoreMsg::SendConnectInfo)
            return false;
        const uint32_t ReplyOff = R.U32();
        const uint32_t ReplySize = R.U32();
        if (R.U32() != 0x50)
            return false;
        Out.Flags = R.U32();
        Out.MaxPlayers = R.U32();
        Out.CurrentPlayers = R.U32();
        const uint32_t SessionOff = R.U32();
        const uint32_t SessionSize = R.U32();
        const uint32_t PasswordOff = R.U32();
        const uint32_t PasswordSize = R.U32();
        const uint32_t ReservedOff = R.U32();
        const uint32_t ReservedSize = R.U32();
        const uint32_t AppReservedOff = R.U32();
        const uint32_t AppReservedSize = R.U32();
        Out.Instance = FGuid::Read(R);
        Out.Application = FGuid::Read(R);
        Out.Dpnid = R.U32();
        Out.NameTableVersion = R.U32();
        Out.NameTableVersionNotUsed = R.U32();
        const uint32_t EntryCount = R.U32();
        const uint32_t MembershipCount = R.U32();
        if (!R.Ok() || EntryCount > Size / kNameTableEntrySize || MembershipCount > Size / kMembershipSize)
            return false;

        const uint8_t* P = nullptr;
        if (!Field(Data, Size, 4, ReplyOff, ReplySize, P))
            return false;
        Out.Reply = P != nullptr ? FBytes(P, P + ReplySize) : FBytes();
        if (!Field(Data, Size, 4, SessionOff, SessionSize, P))
            return false;
        Out.bHasSessionName = P != nullptr;
        Out.SessionName = P != nullptr ? ReadWide(P, SessionSize) : std::u16string();
        if (!Field(Data, Size, 4, PasswordOff, PasswordSize, P))
            return false;
        Out.bHasPassword = P != nullptr;
        Out.Password = P != nullptr ? ReadWide(P, PasswordSize) : std::u16string();
        if (!Field(Data, Size, 4, ReservedOff, ReservedSize, P))
            return false;
        Out.Reserved = P != nullptr ? FBytes(P, P + ReservedSize) : FBytes();
        if (!Field(Data, Size, 4, AppReservedOff, AppReservedSize, P))
            return false;
        Out.ApplicationReserved = P != nullptr ? FBytes(P, P + AppReservedSize) : FBytes();

        Out.Entries.clear();
        for (uint32_t i = 0; i < EntryCount; ++i)
        {
            FNameTableEntry E;
            E.Dpnid = R.U32();
            E.Owner = R.U32();
            E.Flags = R.U32();
            E.Version = R.U32();
            E.VersionNotUsed = R.U32();
            E.DnetVersion = R.U32();
            const uint32_t NameOff = R.U32();
            const uint32_t NameSize = R.U32();
            const uint32_t DataOff = R.U32();
            const uint32_t DataSize = R.U32();
            const uint32_t UrlOff = R.U32();
            const uint32_t UrlSize = R.U32();
            if (!R.Ok())
                return false;
            if (!Field(Data, Size, 4, NameOff, NameSize, P))
                return false;
            E.bHasName = P != nullptr;
            E.Name = P != nullptr ? ReadWide(P, NameSize) : std::u16string();
            if (!Field(Data, Size, 4, DataOff, DataSize, P))
                return false;
            E.Data = P != nullptr ? FBytes(P, P + DataSize) : FBytes();
            if (!Field(Data, Size, 4, UrlOff, UrlSize, P))
                return false;
            if (P != nullptr)
                for (uint32_t c = 0; c < UrlSize && P[c] != 0; ++c)
                    E.Url.push_back((char)P[c]);
            Out.Entries.push_back(std::move(E));
        }
        Out.Memberships.clear();
        for (uint32_t i = 0; i < MembershipCount; ++i)
        {
            FNameTableMembership M;
            M.Player = R.U32();
            M.Group = R.U32();
            M.Version = R.U32();
            M.VersionNotUsed = R.U32();
            Out.Memberships.push_back(M);
        }
        return R.Ok();
    }

    void FConnectFailed::Write(FBytes& Out) const
    {
        FWriter W(Out);
        W.U32(CoreMsg::ConnectFailed);
        W.U32(ResultCode);
        W.U32(Reply.empty() ? 0 : 12);
        W.U32((uint32_t)Reply.size());
        W.Bytes(Reply);
    }

    bool FConnectFailed::Read(const uint8_t* Data, size_t Size, FConnectFailed& Out)
    {
        FReader R(Data, Size);
        if (R.U32() != CoreMsg::ConnectFailed)
            return false;
        Out.ResultCode = R.U32();
        const uint32_t Off = R.U32();
        const uint32_t Len = R.U32();
        const uint8_t* P = nullptr;
        if (!R.Ok() || !Field(Data, Size, 4, Off, Len, P))
            return false;
        Out.Reply = P != nullptr ? FBytes(P, P + Len) : FBytes();
        return true;
    }

    void FTerminateSession::Write(FBytes& Out) const
    {
        FWriter W(Out);
        W.U32(CoreMsg::TerminateSession);
        W.U32(Data.empty() ? 0 : 8);
        W.U32((uint32_t)Data.size());
        W.Bytes(Data);
    }

    bool FTerminateSession::Read(const uint8_t* Data, size_t Size, FTerminateSession& Out)
    {
        FReader R(Data, Size);
        if (R.U32() != CoreMsg::TerminateSession)
            return false;
        const uint32_t Off = R.U32();
        const uint32_t Len = R.U32();
        const uint8_t* P = nullptr;
        if (!R.Ok() || !Field(Data, Size, 4, Off, Len, P))
            return false;
        Out.Data = P != nullptr ? FBytes(P, P + Len) : FBytes();
        return true;
    }
}
