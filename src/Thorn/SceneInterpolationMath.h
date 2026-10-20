#pragma once
#include "ExtendedProgressCodec.h"
#include "FpEnvironment.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ThornSceneInterpolation
{
    static_assert(sizeof(float)==4 && sizeof(double)==8,"32/64-bit floating-point layout required");
    // Double expressions replace retained x87 intermediates. Keep the explicit
    // float boundaries and memory publication order; callers own callbacks.
    inline void Lerp(const float* from,const float* to,const float* progress,float* output)
    {
        const double a=*from,b=*to;
        *output=static_cast<float>(a+(b-a)*static_cast<double>(*progress));
    }
    inline void Camera(float* output, const float* target, const float* captured,
                       double progress, int mask)
    {
        // Writes are observable through overlapping input arrays. Preserve the
        // original FOV, aspect, near, far order and reread each selected field.
        const unsigned order[] = {0, 3, 1, 2};
        for (unsigned index : order) {
            if ((static_cast<unsigned>(mask) & (1u << index)) == 0) continue;
            const double from = captured[index], to = target[index];
            output[index] = static_cast<float>(from + (to - from) * progress);
        }
    }
    inline double WrapIntermediate(double value)
    {
        if (ThornFpEnvironment::Precision() != 24 || !std::isfinite(value) || value == 0.0) return value;
        // Round the significand without imposing binary32's exponent range.
        int exponent;
        const double fraction = std::frexp(value, &exponent);
        unsigned char bytes[10];
        ThornExtendedProgress::StoreDouble(bytes, fraction);
        return std::ldexp(static_cast<double>(ThornExtendedProgress::LoadFloat(bytes)), exponent);
    }
    inline float WrapScale(float* last, double progress, float rounded, bool spatial)
    {
        const double change = WrapIntermediate(progress - static_cast<double>(*last));
        const float delta = static_cast<float>(change);
        if (change == 0.0) return 0.0f;
        if (!((spatial ? static_cast<double>(rounded) : progress) < 1.0)) return 1.0f;
        const double remaining = WrapIntermediate(1.0 - static_cast<double>(*last));
        const float scale = remaining == 0.0 ? 1.0f :
            static_cast<float>(WrapIntermediate(static_cast<double>(delta) / remaining));
        *last = rounded;
        return scale;
    }
    inline void SpatialWrap(float* last, const unsigned char* bytes, float* roundedOutput, float* scaleOutput)
    {
        const double progress = ThornExtendedProgress::LoadDouble(bytes);
        const float rounded = ThornExtendedProgress::LoadFloat(bytes);
        const float scale = WrapScale(last, progress, rounded, true);
        std::memcpy(roundedOutput, &rounded, sizeof(rounded));
        std::memcpy(scaleOutput, &scale, sizeof(scale));
    }
    inline void FloorWrap(float* last, const unsigned char* bytes, float* scaleOutput)
    {
        const double progress = ThornExtendedProgress::LoadDouble(bytes);
        const float rounded = ThornExtendedProgress::LoadFloat(bytes);
        const float scale = WrapScale(last, progress, rounded, false);
        std::memcpy(scaleOutput, &scale, sizeof(scale));
    }
    inline void SpatialPosition(float* output,const float* from,const float* to,const float* scale)
    {
        for (int index=0;index<3;++index) Lerp(from+index,to+index,scale,output+index);
    }
    inline void LightDirection(float* output,const float* from,const float* to,const float* progress)
    {
        const double x=from[0],y=from[1],z=from[2];
        const double dx=static_cast<double>(to[0])-x,dy=static_cast<double>(to[1])-y;
        const float dz=static_cast<float>(static_cast<double>(to[2])-z);
        const float productX=static_cast<float>(dx*static_cast<double>(*progress));
        const double productY=dy*static_cast<double>(*progress),productZ=static_cast<double>(dz)*static_cast<double>(*progress);
        const float resultX=static_cast<float>(static_cast<double>(productX)+x);
        const float resultY=static_cast<float>(productY+y),resultZ=static_cast<float>(productZ+z);
        output[0]=resultX;output[1]=resultY;output[2]=resultZ;
    }
    inline void AxisDelta(const float* progress,const float* angle,float* applied,float* delta)
    {
        const double change=static_cast<double>(*progress)*static_cast<double>(*angle)-static_cast<double>(*applied);
        *delta=static_cast<float>(change);
        // Applied may alias Delta: the original rereads it after publishing Delta.
        *applied=static_cast<float>(change+static_cast<double>(*applied));
    }
    inline bool AttachDirection(float* output,const float* target,const float* origin)
    {
        const double z=static_cast<double>(target[2])-origin[2];
        const double y=static_cast<double>(target[1])-origin[1];
        const double x=static_cast<double>(target[0])-origin[0];
        output[0]=static_cast<float>(x);output[1]=static_cast<float>(y);output[2]=static_cast<float>(z);
        const double roundedX=output[0];
        return ((roundedX*roundedX+y*y)+z*z)!=0.0;
    }
    inline void AxisRotation(float* rotation,int axisId,const float* degrees)
    {
        const float identity[9]={1,0,0,0,1,0,0,0,1};
        std::memcpy(rotation,identity,sizeof(identity));
        const unsigned kind=static_cast<unsigned>(axisId);
        if (kind>5) return;
        const std::uint32_t bits=0x3c8efa35u;
        float radiansPerDegree;
        std::memcpy(&radiansPerDegree,&bits,sizeof(bits));
        const double radians=static_cast<double>(*degrees)*radiansPerDegree;
        const float cosine=static_cast<float>(std::cos(radians));
        const double sine=std::sin(radians);
        const unsigned axis=kind%3;
        const unsigned first=axis==0 ? 3:0,second=axis==2 ? 3:6;
        for (unsigned index=0;index<3;++index) {
            const double a=rotation[first+index],b=rotation[second+index];
            rotation[first+index]=static_cast<float>(axis==1 ? a*cosine+b*sine : a*cosine-b*sine);
            rotation[second+index]=static_cast<float>(axis==1 ? b*cosine-a*sine : b*cosine+a*sine);
        }
    }
    inline float PublishParticle(float* runtime,const float* value)
    {
        const double loaded=*value;
        const float argument=static_cast<float>(loaded);
        *runtime=static_cast<float>(loaded);
        return argument;
    }
}
