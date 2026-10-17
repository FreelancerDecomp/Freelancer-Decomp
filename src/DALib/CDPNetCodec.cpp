// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "CDPNetCodec.h"

// Retail FUN_065c1000 @ 0x1000: deflateInit_(strm,6,"1.1.4",0x38) + inflateInit_(strm,"1.1.4",0x38).
CDPNetCodec::CDPNetCodec()
{
    deflateInit(&DeflateStream, Z_DEFAULT_COMPRESSION);
    inflateInit(&InflateStream);
}

// Retail FUN_065c1090 @ 0x1090: deflateEnd + inflateEnd.
CDPNetCodec::~CDPNetCodec()
{
    deflateEnd(&DeflateStream);
    inflateEnd(&InflateStream);
}

bool CDPNetCodec::Compress(const void* Input, unsigned int InputSize, void* Output, unsigned int OutputCapacity)
{
    if (InputSize < 0x20)
        return false;

    DeflateStream.next_in = (Bytef*)((void*)Input);
    DeflateStream.avail_in = InputSize;
    DeflateStream.next_out = (Bytef*)Output;
    DeflateStream.avail_out = OutputCapacity;
    const int Result = deflate(&DeflateStream, Z_FINISH);
    LastCompressedSize = DeflateStream.total_out;
    deflateReset(&DeflateStream);
    return Result == Z_STREAM_END;
}

bool CDPNetCodec::Decompress(const void* Input, unsigned int InputSize, void* Output, unsigned int OutputCapacity)
{
    InflateStream.next_in = (Bytef*)((void*)Input);
    InflateStream.avail_in = InputSize;
    InflateStream.next_out = (Bytef*)Output;
    InflateStream.avail_out = OutputCapacity;
    const int Result = inflate(&InflateStream, Z_FINISH);
    LastDecompressedSize = InflateStream.total_out;
    inflateReset(&InflateStream);
    return Result == Z_STREAM_END;
}
