// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// 32-bit handle values that name an object by its address.
//
// Retail interfaces such as IFileSystem pass handles as `unsigned long` and return them as `int`,
// and some implementations use the object's address as the handle. That works on Win32, where an
// address fits in 32 bits. On x64 (docs/x64.md) it does not, and the interfaces stay 32-bit, so
// there the value is a 32-bit id from a table instead:
//
// - Win32: the address itself, exactly as retail.
// - x64:   an id, the same for the same address until Forget. Ids start at 1, so 0 and
//          0xFFFFFFFF (the interfaces' "none" values) never name an object.
//
// The table belongs to the module that includes this header (each DLL has its own copy), so a
// value must be decoded by the module that made it. Kernel handles (HANDLE) need none of this:
// Windows keeps their values within 32 bits on x64.

#include <cstdint>

#if defined(FL_PTR64)
#include <mutex>
#include <unordered_map>
#endif

namespace HandleValue
{
#if defined(FL_PTR64)
    namespace Detail
    {
        struct FTable
        {
            std::mutex Lock;
            std::unordered_map<const void*, FlULong> Ids;
            std::unordered_map<FlULong, const void*> Objects;
            FlULong NextId = 1;
        };

        inline FTable& Table()
        {
            static FTable Instance;
            return Instance;
        }
    }

    // The value that names Object (null gives 0).
    inline FlULong Of(const void* Object)
    {
        if (Object == nullptr)
            return 0;
        Detail::FTable& Table = Detail::Table();
        std::lock_guard<std::mutex> Guard(Table.Lock);
        const auto Found = Table.Ids.find(Object);
        if (Found != Table.Ids.end())
            return Found->second;
        FlULong Id = Table.NextId;
        while (Id == 0 || Id == 0xFFFFFFFFu || Table.Objects.contains(Id))
            Id = Id + 1;
        Table.NextId = Id + 1;
        Table.Ids.emplace(Object, Id);
        Table.Objects.emplace(Id, Object);
        return Id;
    }

    // The object a value names, or null for a value this module never made.
    inline const void* ObjectOf(FlULong Value)
    {
        if (Value == 0)
            return nullptr;
        Detail::FTable& Table = Detail::Table();
        std::lock_guard<std::mutex> Guard(Table.Lock);
        const auto Found = Table.Objects.find(Value);
        return Found != Table.Objects.end() ? Found->second : nullptr;
    }

    // Drops Object's id when the object is freed, so the table does not grow with every address
    // ever used. An address that is used again later gets a new id.
    inline void Forget(const void* Object)
    {
        if (Object == nullptr)
            return;
        Detail::FTable& Table = Detail::Table();
        std::lock_guard<std::mutex> Guard(Table.Lock);
        const auto Found = Table.Ids.find(Object);
        if (Found == Table.Ids.end())
            return;
        Table.Objects.erase(Found->second);
        Table.Ids.erase(Found);
    }
#else
    inline FlULong Of(const void* Object)
    {
        return (FlULong)(Object);
    }

    inline const void* ObjectOf(FlULong Value)
    {
        return (const void*)(Value);
    }

    inline void Forget(const void*)
    {
    }
#endif

    // ObjectOf, typed.
    template <class T>
    T* To(FlULong Value)
    {
        return (T*)(ObjectOf(Value));
    }
}
