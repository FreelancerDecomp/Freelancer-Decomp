#pragma once
#include "../include/portable_integer_conversion.h"
#include <cstdint>
#include <cstring>

namespace ThornCurvePolynomial
{
    inline double Catmull(const float* p0,const float* p1,const float* p2,const float* p3,float u)
    {
        const double a=p0 ? *p0:*p1,b=*p1,c=*p2,d=p3 ? *p3:*p2;
        const double t=u,t2=t*t,t3=t*t2;
        const double quadratic=((2.0*a-5.0*b)+4.0*c)-d;
        const double cubic=((3.0*b-a)-3.0*c)+d;
        return (((quadratic*t2+cubic*t3)+(c-a)*t)+2.0*b)*0.5;
    }
    template<class Key>
    inline double Lerp(const Key& left,const Key& right,const float* time)
    {
        const double value=left.Value;
        const double scale=(static_cast<double>(*time)-left.T)/(static_cast<double>(right.T)-left.T);
        return value+(static_cast<double>(right.Value)-value)*scale;
    }
    template<class Knot>
    inline double Hermite(const Knot& a,const Knot& b,const float* time)
    {
        if (a.T==*time) return a.Value;
        if (b.T==*time) return b.Value;
        const double dt=static_cast<double>(b.T)-a.T;
        const float normalized=static_cast<float>((static_cast<double>(*time)-a.T)/dt);
        std::uint32_t outgoing;
        std::memcpy(&outgoing,&a.MOut,sizeof(outgoing));
        if (outgoing==0xffffffffu) return a.Value;
        const double t=normalized,t2=t*t,t3=t*t2;
        const double tangentA=((t3-2.0*t2)+t)*a.MOut*dt;
        const double tangentB=(t3-t2)*b.MIn*dt;
        const double left=((2.0*t3-3.0*t2)+1.0)*a.Value;
        const double right=(3.0*t2-2.0*t3)*b.Value;
        return ((tangentA+tangentB)+left)+right;
    }
    template<class Knot>
    inline double EvaluateHermite(const Knot* knots,int count,const float* time)
    {
        if (!knots || count<=0) return 0;
        if (count==1) return knots[0].Value;
        const int last=count-1;
        if (*time<knots[0].T) return knots[0].Value;
        if (*time>knots[last].T || *time==knots[last].T) return knots[last].Value;
        const double location=((static_cast<double>(*time)-knots[0].T)/
            (static_cast<double>(knots[last].T)-knots[0].T))*last;
        const auto word=static_cast<std::uint32_t>(PortableIntegerConversion::ToInt64Bits(&location));
        std::int32_t seed;std::memcpy(&seed,&word,sizeof(seed));
        // Invalid/unordered data must not turn the seed or one-past endpoint
        // into an out-of-bounds C++ reference. Finite sorted inputs keep the
        // original seed-dependent backward/forward pair selection.
        int index=seed<0 ? 0:seed>last ? last:seed;
        if (knots[index].T>*time)
        {
            while (index>0 && !(knots[index].T<*time)) --index;
            return Hermite(knots[index],knots[index+1],time);
        }
        while (index<=last && !(knots[index].T>*time)) ++index;
        if (index>last) return Hermite(knots[last-1],knots[last],time);
        if (index==0) return knots[0].Value;
        return Hermite(knots[index-1],knots[index],time);
    }
}
