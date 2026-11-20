// Freelancer Decomp: DPNet, a DirectPlay 8 protocol implementation (port-only, not part of retail).
//
// Wire formats of the three DirectPlay 8 protocols, as Microsoft published them:
//   [MC-DPL8R]  DirectPlay 8 Protocol: Reliable                (CFRAMEs, DFRAMEs, coalescing)
//   [MC-DPLHP]  DirectPlay 8 Protocol: Host and Port Enumeration (EnumQuery, EnumResponse)
//   [MC-DPL8CS] DirectPlay 8 Protocol: Core and Service Providers (connect and session messages)
// Everything on the wire is little-endian. Section numbers below refer to those documents.
//
// This file only encodes and decodes. The connection state machine is FLink (dpnet_link.h),
// the session layer is FServer / FClient (dpnet_session.h). See docs/dpnet.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace DPNet
{
    using FBytes = std::vector<uint8_t>;

    // ---------------------------------------------------------------------------------------
    // Byte reader and writer.

    class FWriter
    {
    public:
        explicit FWriter(FBytes& InOut) : Out(InOut) {}

        void U8(uint8_t V) { Out.push_back(V); }
        void U16(uint16_t V)
        {
            U8((uint8_t)V);
            U8((uint8_t)(V >> 8));
        }
        void U32(uint32_t V)
        {
            U16((uint16_t)V);
            U16((uint16_t)(V >> 16));
        }
        void U64(uint64_t V)
        {
            U32((uint32_t)V);
            U32((uint32_t)(V >> 32));
        }
        void Bytes(const uint8_t* Data, size_t Size) { Out.insert(Out.end(), Data, Data + Size); }
        void Bytes(const FBytes& Data) { Bytes(Data.data(), Data.size()); }
        void Zeros(size_t Count) { Out.insert(Out.end(), Count, 0); }
        size_t Size() const { return Out.size(); }
        void PatchU32(size_t Pos, uint32_t V)
        {
            for (int i = 0; i < 4; ++i)
                Out[Pos + i] = (uint8_t)(V >> (8 * i));
        }

    private:
        FBytes& Out;
    };

    // Reads past the end set bOk to false and return zeros, so a decoder can read every field
    // and check once at the end.
    class FReader
    {
    public:
        FReader(const uint8_t* InData, size_t InSize) : Data(InData), Size(InSize) {}

        uint8_t U8()
        {
            if (!Need(1))
                return 0;
            return Data[Pos++];
        }
        uint16_t U16()
        {
            const uint16_t Lo = U8();
            return (uint16_t)(Lo | (U8() << 8));
        }
        uint32_t U32()
        {
            const uint32_t Lo = U16();
            return Lo | ((uint32_t)U16() << 16);
        }
        uint64_t U64()
        {
            const uint64_t Lo = U32();
            return Lo | ((uint64_t)U32() << 32);
        }
        void Read(uint8_t* Dest, size_t Count)
        {
            if (!Need(Count))
                return;
            for (size_t i = 0; i < Count; ++i)
                Dest[i] = Data[Pos + i];
            Pos += Count;
        }
        void Skip(size_t Count)
        {
            if (Need(Count))
                Pos += Count;
        }
        size_t Position() const { return Pos; }
        size_t Remaining() const { return Size - Pos; }
        bool Ok() const { return bOk; }

    private:
        bool Need(size_t Count)
        {
            if (!bOk || Size - Pos < Count)
            {
                bOk = false;
                return false;
            }
            return true;
        }

        const uint8_t* Data;
        size_t Size;
        size_t Pos = 0;
        bool bOk = true;
    };

    // A GUID in its Windows memory layout ([MS-DTYP] 2.3.4): Data1..Data3 little-endian.
    struct FGuid
    {
        uint32_t Data1 = 0;
        uint16_t Data2 = 0;
        uint16_t Data3 = 0;
        uint8_t Data4[8] = {};

        bool IsZero() const;
        bool operator==(const FGuid& Other) const;
        bool operator!=(const FGuid& Other) const { return !(*this == Other); }

        void Write(FWriter& W) const;
        static FGuid Read(FReader& R);
        // "{EBFE7BA0-628D-11D2-AE0F-006097B01411}", upper case.
        std::string ToString() const;
    };

    // The TCP/IP service provider, CLSID_DP8SP_TCPIP.
    extern const FGuid kProviderTcpIp;

    // ---------------------------------------------------------------------------------------
    // [MC-DPL8R] 2.2: frames.

    // bCommand of a DFRAME (2.2.2) and of a coalesced sub-payload (2.2.3, where 0x01 is END_COALESCE
    // and 0x08/0x10/0x20 are size bits instead).
    namespace Command
    {
        constexpr uint8_t Data = 0x01;
        constexpr uint8_t Reliable = 0x02;
        constexpr uint8_t Sequential = 0x04;
        constexpr uint8_t Poll = 0x08;
        constexpr uint8_t NewMsg = 0x10;
        constexpr uint8_t EndMsg = 0x20;
        constexpr uint8_t User1 = 0x40;   // [MC-DPL8CS] core message
        constexpr uint8_t User2 = 0x80;   // DirectPlay Voice
        constexpr uint8_t UserMask = User1 | User2;
        constexpr uint8_t CFrame = 0x80;  // bCommand of a CFRAME (with or without Poll)

        constexpr uint8_t EndCoalesce = 0x01;
        constexpr uint8_t CoalesceBigMask = 0x38;
    }

    // bControl of a DFRAME (2.2.2).
    namespace Control
    {
        constexpr uint8_t Retry = 0x01;
        constexpr uint8_t KeepAlive = 0x02;   // KEEPALIVE_OR_CORRELATE
        constexpr uint8_t Coalesce = 0x04;
        constexpr uint8_t EndStream = 0x08;
        constexpr uint8_t Sack1 = 0x10;
        constexpr uint8_t Sack2 = 0x20;
        constexpr uint8_t Send1 = 0x40;
        constexpr uint8_t Send2 = 0x80;
    }

    // bExtOpCode of a CFRAME (2.2.1).
    namespace ExtOp
    {
        constexpr uint8_t Connect = 0x01;
        constexpr uint8_t Connected = 0x02;
        constexpr uint8_t ConnectedSigned = 0x03;
        constexpr uint8_t HardDisconnect = 0x04;
        constexpr uint8_t Sack = 0x06;
    }

    // bFlags of a SACK CFRAME (2.2.1.5).
    namespace SackFlags
    {
        constexpr uint8_t Response = 0x01;
        constexpr uint8_t SackMask1 = 0x02;
        constexpr uint8_t SackMask2 = 0x04;
        constexpr uint8_t SendMask1 = 0x08;
        constexpr uint8_t SendMask2 = 0x10;
    }

    // dwCurrentProtocolVersion: major 1. Minor 5 adds coalescing and the KeepAlive payload,
    // minor 6 adds signing (which this implementation never asks for).
    constexpr uint32_t kProtocolVersion = 0x00010006;
    constexpr uint32_t kProtocolMajorMask = 0xFFFF0000;
    constexpr uint32_t kProtocolMajor = 0x00010000;
    constexpr uint32_t kProtocolCoalesce = 0x00010005;

    // CONNECT, CONNECTED and HARD_DISCONNECT: the 16-byte command frame (2.2.1.1, 2.2.1.2, 2.2.1.4).
    struct FConnectFrame
    {
        uint8_t Command = Command::CFrame;
        uint8_t Op = ExtOp::Connect;
        uint8_t MsgId = 0;
        uint8_t RspId = 0;
        uint32_t Version = kProtocolVersion;
        uint32_t SessId = 0;
        uint32_t Timestamp = 0;

        void Write(FBytes& Out) const;
    };

    // SACK (2.2.1.5). The masks are written only when their 32-bit half is non-zero.
    struct FSackFrame
    {
        uint8_t Flags = SackFlags::Response;
        uint8_t Retry = 0;
        uint8_t NSeq = 0;
        uint8_t NRcv = 0;
        uint32_t Timestamp = 0;
        uint64_t SackMask = 0;   // bit i: sequence NRcv + 1 + i was received
        uint64_t SendMask = 0;   // bit i: sequence NSeq - 1 - i was unreliable and dropped

        void Write(FBytes& Out) const;
    };

    // A decoded command frame. The CONNECT/CONNECTED/HARD_DISCONNECT fields share the layout of
    // FConnectFrame; a SACK fills Sack.
    struct FCommandFrame
    {
        uint8_t Command = 0;
        uint8_t Op = 0;
        FConnectFrame Connect;
        FSackFrame Sack;
    };

    // 3.1.5: a CFRAME is at least 12 bytes with a lead byte of 0x80 or 0x88.
    bool IsDataFrame(const uint8_t* Data, size_t Size);
    bool IsCommandFrame(const uint8_t* Data, size_t Size);
    bool DecodeCommandFrame(const uint8_t* Data, size_t Size, FCommandFrame& Out);

    // DFRAME header (2.2.2), without signature (signing is never negotiated).
    struct FDataHeader
    {
        uint8_t Command = Command::Data;
        uint8_t Control = 0;
        uint8_t Seq = 0;
        uint8_t NRcv = 0;
        uint64_t SackMask = 0;   // bit i: sequence NRcv + 1 + i was received
        uint64_t SendMask = 0;   // bit i: sequence Seq - 1 - i was unreliable and dropped

        // Sets the Sack/Send bits of Control from the masks.
        void Write(FBytes& Out) const;
        // Returns the payload offset, or 0 when the frame is malformed.
        static size_t Read(const uint8_t* Data, size_t Size, FDataHeader& Out);
    };
    constexpr size_t kMaxDataHeaderSize = 4 + 16;

    // One payload of a coalesced DFRAME (2.2.3).
    struct FSubPayload
    {
        uint8_t Flags = 0;   // Reliable, Sequential, User1, User2
        const uint8_t* Data = nullptr;
        size_t Size = 0;
    };
    constexpr size_t kMaxCoalesced = 32;
    constexpr size_t kMaxCoalescedSize = 0x7FF;   // 11-bit size field

    bool DecodeCoalesced(const uint8_t* Data, size_t Size, std::vector<FSubPayload>& Out);
    void EncodeCoalesced(const std::vector<FSubPayload>& Parts, FBytes& Out);
    size_t CoalescedSize(const std::vector<size_t>& PartSizes);

    // ---------------------------------------------------------------------------------------
    // [MC-DPLHP] 2.2: host enumeration. Lead byte 0x00.

    constexpr uint8_t kEnumQueryCommand = 0x02;
    constexpr uint8_t kEnumResponseCommand = 0x03;

    struct FEnumQuery
    {
        uint16_t EnumPayload = 0;
        bool bHasApplication = false;
        FGuid Application;
        FBytes UserData;

        void Write(FBytes& Out) const;
        static bool Read(const uint8_t* Data, size_t Size, FEnumQuery& Out);
    };

    struct FEnumResponse
    {
        uint16_t EnumPayload = 0;
        uint32_t Flags = 0;
        uint32_t MaxPlayers = 0;
        uint32_t CurrentPlayers = 0;
        std::u16string SessionName;
        FBytes ApplicationReserved;
        FBytes UserData;   // ApplicationData: the game's reply
        FGuid Instance;
        FGuid Application;

        void Write(FBytes& Out) const;
        static bool Read(const uint8_t* Data, size_t Size, FEnumResponse& Out);
    };

    // ---------------------------------------------------------------------------------------
    // [MC-DPL8CS] 2.2: core messages, carried in DFRAMEs with Command::User1. The first dword
    // is the packet type; every offset in them counts from the end of that dword.

    namespace CoreMsg
    {
        constexpr uint32_t PlayerConnectInfo = 0xC1;
        constexpr uint32_t SendConnectInfo = 0xC2;
        constexpr uint32_t AckConnectInfo = 0xC3;
        constexpr uint32_t ConnectFailed = 0xC5;
        constexpr uint32_t DestroyPlayer = 0xD1;
        constexpr uint32_t TerminateSession = 0xDF;
        constexpr uint32_t ReqProcessCompletion = 0xE0;
        constexpr uint32_t ProcessCompletion = 0xE1;
    }

    // DPNSESSION_* (application description flags).
    namespace SessionFlags
    {
        constexpr uint32_t ClientServer = 0x00000001;
        constexpr uint32_t MigrateHost = 0x00000004;
        constexpr uint32_t NoDpnSvr = 0x00000040;
        constexpr uint32_t RequirePassword = 0x00000080;
        constexpr uint32_t NoEnums = 0x00000100;
        constexpr uint32_t FastSigned = 0x00000200;
        constexpr uint32_t FullSigned = 0x00000400;
    }

    // NAMETABLE_ENTRY_FLAG_* (2.2.1.5).
    namespace EntryFlags
    {
        constexpr uint32_t Local = 0x00000001;
        constexpr uint32_t Host = 0x00000002;
        constexpr uint32_t AllPlayersGroup = 0x00000004;
        constexpr uint32_t Group = 0x00000010;
        constexpr uint32_t Peer = 0x00000100;
        constexpr uint32_t Client = 0x00000200;
        constexpr uint32_t Server = 0x00000400;
    }

    // dwFlags of the player connect message.
    namespace ObjectType
    {
        constexpr uint32_t Client = 0x00000002;
        constexpr uint32_t Peer = 0x00000004;
    }

    // HRESULTs carried in DN_CONNECT_FAILED (2.2.1.3) and reported to callers.
    namespace Result
    {
        constexpr uint32_t Ok = 0;
        constexpr uint32_t Generic = 0x80004005;
        constexpr uint32_t AlreadyClosing = 0x80158050;
        constexpr uint32_t HostRejectedConnection = 0x80158260;
        constexpr uint32_t InvalidApplication = 0x80158300;
        constexpr uint32_t InvalidInstance = 0x80158380;
        constexpr uint32_t InvalidInterface = 0x80158390;
        constexpr uint32_t InvalidPassword = 0x80158410;
        constexpr uint32_t InvalidVersion = 0x80158460;
        constexpr uint32_t NotHost = 0x80158530;
        // Not in the specification's list; the dplay8.h values. Freelancer's client tells
        // SessionFull apart from other failures (connect_handshake.cpp).
        constexpr uint32_t SessionFull = 0x80158610;
        constexpr uint32_t TimedOut = 0x80158630;
    }

    // DPNDESTROYPLAYERREASON_*.
    namespace DestroyReason
    {
        constexpr uint32_t Normal = 1;
        constexpr uint32_t ConnectionLost = 2;
        constexpr uint32_t SessionTerminated = 3;
        constexpr uint32_t HostDestroyedPlayer = 4;
    }

    // DN_INTERNAL_MESSAGE_PLAYER_CONNECT_INFO and its _EX form (2.2.1.1, 2.2.1.2). The _EX form
    // (dwDNETVersion >= 7) adds the alternate address fields after guidApplication.
    struct FPlayerConnectInfo
    {
        uint32_t Flags = ObjectType::Client;
        uint32_t DnetVersion = 8;
        std::u16string Name;
        FBytes Data;
        std::u16string Password;
        bool bHasPassword = false;
        FBytes ConnectData;
        std::string Url;
        FGuid Instance;
        FGuid Application;
        FBytes AlternateAddresses;   // DN_ALTERNATE_ADDRESS records, as sent

        void Write(FBytes& Out) const;
        static bool Read(const uint8_t* Data, size_t Size, FPlayerConnectInfo& Out);
    };

    struct FNameTableEntry
    {
        uint32_t Dpnid = 0;
        uint32_t Owner = 0;
        uint32_t Flags = 0;
        uint32_t Version = 0;
        uint32_t VersionNotUsed = 0;
        uint32_t DnetVersion = 8;
        std::u16string Name;
        bool bHasName = false;
        FBytes Data;
        std::string Url;   // without the terminator; empty: no URL
    };

    struct FNameTableMembership
    {
        uint32_t Player = 0;
        uint32_t Group = 0;
        uint32_t Version = 0;
        uint32_t VersionNotUsed = 0;
    };

    // DN_SEND_CONNECT_INFO (2.2.1.4). The variable fields are packed from the end of the buffer
    // backwards, the way Windows does it: reply, session name, password, reserved data,
    // application reserved data, then for each entry its name, data and URL. Decoding only
    // follows the offsets.
    struct FSendConnectInfo
    {
        FBytes Reply;
        uint32_t Flags = 0;
        uint32_t MaxPlayers = 0;
        uint32_t CurrentPlayers = 0;
        std::u16string SessionName;
        bool bHasSessionName = false;
        std::u16string Password;
        bool bHasPassword = false;
        FBytes Reserved;
        FBytes ApplicationReserved;
        FGuid Instance;
        FGuid Application;
        uint32_t Dpnid = 0;
        uint32_t NameTableVersion = 0;
        uint32_t NameTableVersionNotUsed = 0;
        std::vector<FNameTableEntry> Entries;
        std::vector<FNameTableMembership> Memberships;

        void Write(FBytes& Out) const;
        static bool Read(const uint8_t* Data, size_t Size, FSendConnectInfo& Out);
    };

    // DN_CONNECT_FAILED (2.2.1.3).
    struct FConnectFailed
    {
        uint32_t ResultCode = Result::Generic;
        FBytes Reply;

        void Write(FBytes& Out) const;
        static bool Read(const uint8_t* Data, size_t Size, FConnectFailed& Out);
    };

    // DN_TERMINATE_SESSION (2.2.2.1).
    struct FTerminateSession
    {
        FBytes Data;

        void Write(FBytes& Out) const;
        static bool Read(const uint8_t* Data, size_t Size, FTerminateSession& Out);
    };

    // DN_DPNID (2.2.7): name table index in the low 20 bits, name table version above it,
    // XORed with the first dword of the session instance GUID.
    uint32_t MakeDpnid(uint32_t Index, uint32_t Version, const FGuid& Instance);

    // UTF-16 helpers for the wide strings on the wire (terminator included in their sizes).
    void WriteWide(FBytes& Out, const std::u16string& Text);
    std::u16string ReadWide(const uint8_t* Data, size_t Size);
    std::u16string WideFromAscii(const char* Text);
}
