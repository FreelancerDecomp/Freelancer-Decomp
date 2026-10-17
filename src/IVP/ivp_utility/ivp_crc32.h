// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// ============================================================================
// The IVP hash containers' CRC-32 table (retail .data:DAT_063edcdc, the
// reflected IEEE 802.3 table, polynomial 0xEDB88320). Retail keeps it as
// initialised data; the port fills it once at load time from a dynamic
// initializer in ivp_vhash.cpp (g_Crc32Table_063edcdc_init). Nothing reads it
// during static initialisation, so its load-time order is irrelevant; the
// TU still sits where common_stubs.cpp used to (first in Common.vcxproj).
//
// Users: IVP_VHash_Store and IVP_VHash_HashPointer (ivp_vhash.cpp), IVP_Hash
// (ivp_hash.cpp), IVP_U_Active_Value_Hash::GetHashIndex (ivp_active_value.cpp).
// ivp_ov_element.cpp still builds its own identical copy (g_OvCrc32Table);
// merging the two is a separate, behaviour-neutral cleanup.
// ============================================================================

extern unsigned g_Crc32Table_063edcdc[256];

// The byte-at-a-time CRC-32 over the four bytes of a 32-bit key, with the
// 0xffffffff start value and no final inversion. Retail inlines this loop at
// 0x63526d0 / 0x63527e0 / 0x6352930 / 0x63529e0 and keeps a called copy at
// 0x635d4d0 (IVP_VHash_HashPointer); the callers OR in 0x80000000 themselves.
inline unsigned HashKey_063edcdc(unsigned key)
{
    unsigned char* bytes = reinterpret_cast<unsigned char*>(&key);
    unsigned crc = 0xffffffff;
    for (int i = 0; i < 4; ++i)
    {
        crc = (crc >> 8) ^ g_Crc32Table_063edcdc[(bytes[i] ^ crc) & 0xff];
    }
    return crc;
}
