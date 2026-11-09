#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ThornCompactLight
{
    static_assert(sizeof(float)==4 && sizeof(double)==8,"32/64-bit compact-light scalars required");
    template<class T> inline T Read(const unsigned char* source,unsigned offset)
    {
        T value;std::memcpy(&value,source+offset,sizeof(value));return value;
    }
    template<class T> inline void Write(unsigned char* target,unsigned offset,T value)
    {
        std::memcpy(target+offset,&value,sizeof(value));
    }
    inline float Float(std::uint32_t bits)
    {
        float value;std::memcpy(&value,&bits,sizeof(value));return value;
    }
    inline double Double(std::uint64_t bits)
    {
        double value;std::memcpy(&value,&bits,sizeof(value));return value;
    }
    inline void Convert(const unsigned char* source,unsigned char* target)
    {
        if (Read<std::int32_t>(source,0x24)!=0) Write(target,0,std::int32_t(3));
        else if (Read<float>(source,0x1c)<0.0f) {
            Write(target,0,std::int32_t(1));
            Write(target,0x60,Float(0x40490fdb));Write(target,0x64,Float(0x40490fdb));
        } else {
            Write(target,0,std::int32_t(2));
            double cutoff=static_cast<double>(Read<float>(source,0x1c))*Float(0x3c8efa35);
            const double limit=Double(UINT64_C(0x400921fb60000000));
            if (!(cutoff<limit)) cutoff=limit;
            Write(target,0x60,static_cast<float>(cutoff));
            double theta=cutoff*Float(0x3f8ccccd);
            if (!(theta<Float(0x40490fdb))) theta=Float(0x40490fdb);
            Write(target,0x64,static_cast<float>(theta));
        }
        const double scale=Float(0x3b808081);
        const float red=static_cast<float>(static_cast<double>(Read<std::int32_t>(source,0))*scale);
        const float green=static_cast<float>(static_cast<double>(Read<std::int32_t>(source,4))*scale);
        const std::int32_t blue=Read<std::int32_t>(source,8);
        Write(target,4,red);Write(target,8,green);
        Write(target,12,static_cast<float>(static_cast<double>(blue)*scale));Write(target,16,1.0f);
        for (unsigned offset=0x14;offset<0x34;offset+=4) Write(target,offset,std::uint32_t(0));
        Write(target,0x50,1.0f);
        if (static_cast<double>(Read<float>(source,0x18))<Double(UINT64_C(0x3f1a36e2eb1c432d))) {
            Write(target,0x58,Float(0x38d1b717));
            const double radius=std::sqrt(Double(UINT64_C(0x47efffffe0000000)));
            Write(target,0x54,0.0f);Write(target,0x5c,0.0f);
            Write(target,0x4c,static_cast<float>(radius));
        } else {
            Write(target,0x54,1.0f);Write(target,0x58,0.0f);
            const double range=Read<float>(source,0x18);
            Write(target,0x5c,static_cast<float>(1.0/((range*range)*0.25)));
            // Source may overlap the just-published attenuation coefficient.
            const double reread=Read<float>(source,0x18);
            Write(target,0x4c,static_cast<float>(reread+reread));
        }
    }
}
