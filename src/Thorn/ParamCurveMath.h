#pragma once
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <limits>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace ThornParamMath
{
    inline bool Greater(const float* left,const float* right) { return *left>*right; }
    inline bool Equal(const float* left,const float* right) { return *left==*right; }
    inline bool LessEqual(const float* left,const float* right) { return *left<=*right; }
    inline int ClassifyTime(float time)
    {
        if (time<0.0f || time>1.0f) return -1;
        if (time==0.0f) return 0;
        if (time==1.0f) return 1;
        return 3; // Unordered inputs follow new-key construction.
    }
    inline bool ReplacementTime(int id,float input,float* output)
    {
        double value=id==0 ? 0.0:id==1 ? 1.0:static_cast<double>(input);
        if (value>1.0) value=1.0;
        if (value<0.0) value=0.0;
        if ((value==0.0 && id!=0) || (value==1.0 && id!=1)) return false;
        *output=static_cast<float>(value);
        return true;
    }
    inline float NormalizeTime(const float* time,const float* first,const float* last)
    {
        return static_cast<float>((static_cast<double>(*time)-*first)/(static_cast<double>(*last)-*first));
    }
    inline float WrappedRatio(int a,int b)
    {
        static_assert(sizeof(int)==4,"32-bit curve integers required");
        if (b==0 || (a==(-2147483647-1) && b==-1)) {
            // Avoid undefined C++ remainder while retaining the two Windows
            // integer arithmetic exceptions reported by the original IDIV.
#if defined(_WIN32)
            ::RaiseException(b==0 ? 0xc0000094u:0xc0000095u,0,0,nullptr);
#else
            std::raise(SIGFPE);
#endif
            return std::numeric_limits<float>::quiet_NaN(); // If a handler resumes.
        }
        return static_cast<float>(static_cast<double>(a%b)/static_cast<double>(b));
    }
    template<class Knot>
    inline void ApplyTangents(Knot* knots,int count,int mode)
    {
        if (count<=1) return;
        for (int index=0;index<count-1;++index) {
            Knot& current=knots[index];Knot& next=knots[index+1];
            float dt=static_cast<float>(static_cast<double>(next.T)-current.T);
            const float dv=static_cast<float>(static_cast<double>(next.Value)-current.Value);
            if (dt<1e-4f) dt=1e-4f;
            switch(mode) {
            case 0: {
                const std::uint32_t sentinel=0xffffffffu;
                std::memcpy(&current.MOut,&sentinel,4);break;
            }
            case 1: {
                const float slope=static_cast<float>(static_cast<double>(dv)/dt);
                next.MIn=slope;current.MOut=slope;break;
            }
            case 4: current.MOut=0;next.MIn=0;break;
            case 2: case 3: case 5: case 6: {
                const float slope=static_cast<float>(static_cast<double>(dv)/(static_cast<double>(dt)*0.5));
                const bool incoming=mode==2 || (mode==5 && current.Value>next.Value) ||
                    (mode==6 && current.Value<next.Value);
                if (incoming) {current.MOut=0;next.MIn=slope;}
                else {current.MOut=slope;next.MIn=0;}
                break;
            }
            default:break;
            }
        }
    }
}
