#pragma once
#include <cmath>
#include <cstring>

namespace ThornSplineQuaternion
{
    template<class Quat>
    inline float InverseScale(const Quat& q)
    {
        const double x=q.X,y=q.Y,z=q.Z,w=q.W;
        return static_cast<float>(1.0/(((x*x+y*y)+z*z)+w*w));
    }
    inline float SquadBlend(float t)
    {
        const double product=(1.0-static_cast<double>(t))*static_cast<double>(t);
        return static_cast<float>(product+product);
    }
    template<class Quat>
    inline void Add(Quat& dst,const Quat& a,const Quat& b)
    {
        const double z=static_cast<double>(a.Z)+b.Z, y=static_cast<double>(a.Y)+b.Y;
        const double x=static_cast<double>(a.X)+b.X, w=static_cast<double>(a.W)+b.W;
        dst.W=static_cast<float>(w); dst.X=static_cast<float>(x);
        dst.Y=static_cast<float>(y); dst.Z=static_cast<float>(z);
    }
    template<class Quat>
    inline void Scale(Quat& dst,const Quat& q,float factor)
    {
        const double w=static_cast<double>(factor)*q.W,x=static_cast<double>(factor)*q.X;
        const double y=static_cast<double>(factor)*q.Y,z=static_cast<double>(factor)*q.Z;
        dst.W=static_cast<float>(w); dst.X=static_cast<float>(x);
        dst.Y=static_cast<float>(y); dst.Z=static_cast<float>(z);
    }
    template<class Quat>
    inline void Log(Quat& dst,const Quat& q,bool roundScale,float epsilon)
    {
        if (!(std::fabs(static_cast<double>(q.W))<1.0))
        {
            dst.W=0;
            if (&dst!=&q) {
                std::memcpy(&dst.X,&q.X,sizeof(float));
                std::memcpy(&dst.Y,&q.Y,sizeof(float));
                std::memcpy(&dst.Z,&q.Z,sizeof(float));
            }
            return;
        }
        const double angle=std::acos(static_cast<double>(q.W));
        const double sine=std::sin(angle);
        if (!(sine>=epsilon))
        {
            dst.W=0;
            if (&dst!=&q) {
                std::memcpy(&dst.X,&q.X,sizeof(float));
                std::memcpy(&dst.Y,&q.Y,sizeof(float));
                std::memcpy(&dst.Z,&q.Z,sizeof(float));
            }
            return;
        }
        double scale=angle/sine;
        if (roundScale) scale=static_cast<float>(scale);
        const float x=static_cast<float>(scale*q.X),y=static_cast<float>(scale*q.Y),z=static_cast<float>(scale*q.Z);
        dst.X=x; dst.Y=y; dst.W=0; dst.Z=z;
    }
    template<class Quat>
    inline void Exp(Quat& dst,const Quat& q,bool keepNormForSin,bool roundScale,float epsilon)
    {
        const double x=q.X,y=q.Y,z=q.Z;
        const double length=std::sqrt((x*x+y*y)+z*z);
        const float norm=static_cast<float>(length);
        const double sine=std::sin(keepNormForSin ? length : static_cast<double>(norm));
        Quat result;
        result.X=q.X; result.Y=q.Y; result.Z=q.Z;
        if (sine>=epsilon)
        {
            double scale=sine/static_cast<double>(norm);
            if (roundScale) scale=static_cast<float>(scale);
            result.X=static_cast<float>(scale*x); result.Y=static_cast<float>(scale*y); result.Z=static_cast<float>(scale*z);
        }
        result.W=static_cast<float>(std::cos(static_cast<double>(norm)));
        dst=result;
    }
}
