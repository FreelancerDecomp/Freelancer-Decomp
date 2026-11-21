// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// FL_FACET_OFFSET(retail, Derived, Base): the byte offset of the Base interface (facet) inside
// a Derived object, for the {iid, offset} QueryInterface tables retail's components use.
//
// On Win32 it is the retail number, so the tables stay the constant data retail has. The
// x64 build (PORT(x64), docs/x64.md) has 8-byte vtable pointers, so the facets sit at other
// offsets; there the offset is taken from the class layout itself.

#if defined(_WIN64)
#include <cstdint>

template <class Derived, class Base>
inline long FlFacetOffset()
{
    // Any non-null address works: static_cast only adds the base's fixed offset.
    Derived* const Probe = reinterpret_cast<Derived*>(static_cast<std::uintptr_t>(0x1000));
    return static_cast<long>(reinterpret_cast<const char*>(static_cast<Base*>(Probe)) -
                             reinterpret_cast<const char*>(Probe));
}

#define FL_FACET_OFFSET(Retail, Derived, Base) FlFacetOffset<Derived, Base>()
#else
#define FL_FACET_OFFSET(Retail, Derived, Base) (Retail)
#endif
