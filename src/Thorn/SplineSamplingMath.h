#pragma once
#include "SceneGeometryMath.h"
#include <cmath>
#include <cstring>

namespace ThornSplineSampling
{
    template<class Vector>
    inline void BuildPositionMatrix(const Vector& tangent,float* matrix)
    {
        Vector forward=tangent;
        ThornSceneGeometry::Normalize(forward.X,forward.Y,forward.Z,true);
        forward.X=-forward.X; forward.Y=-forward.Y; forward.Z=-forward.Z;
        const double x=forward.X,y=forward.Y,z=forward.Z;
        const float sideX=static_cast<float>(z-y*0.0);
        const float sideY=static_cast<float>(x*0.0-z*0.0);
        const double sideZWide=y*0.0-x;
        const float sideZ=static_cast<float>(sideZWide);
        const float sideInverse=static_cast<float>(1.0/std::sqrt(
            (sideZWide*sideZ+static_cast<double>(sideY)*sideY)+static_cast<double>(sideX)*sideX));
        const double sx=static_cast<double>(sideX)*sideInverse,sy=static_cast<double>(sideY)*sideInverse;
        const float sz=static_cast<float>(static_cast<double>(sideZ)*sideInverse);
        const double ux=y*sz-z*sy;
        const float uy=static_cast<float>(z*sx-x*sz);
        const double uzWide=x*sy-y*sx;
        const float uz=static_cast<float>(uzWide);
        const float upInverse=static_cast<float>(1.0/std::sqrt(
            (uzWide*uz+static_cast<double>(uy)*uy)+ux*ux));
        matrix[0]=static_cast<float>(sx); matrix[1]=static_cast<float>(ux*upInverse); matrix[2]=forward.X;
        matrix[3]=static_cast<float>(sy); matrix[4]=static_cast<float>(static_cast<double>(uy)*upInverse); matrix[5]=forward.Y;
        matrix[6]=sz; matrix[7]=static_cast<float>(static_cast<double>(uz)*upInverse); matrix[8]=forward.Z;
    }

    template<class Vector>
    inline void Accumulate(Vector& previous,const Vector& current,const float& offset,float& segment,float& time)
    {
        const double x=static_cast<double>(current.X)-previous.X;
        const double y=static_cast<double>(current.Y)-previous.Y;
        const double z=static_cast<double>(current.Z)-previous.Z;
        Vector snapshot;
        std::memcpy(&snapshot,&current,sizeof(snapshot));
        std::memcpy(&previous.X,&snapshot.X,sizeof(float));
        std::memcpy(&previous.Z,&snapshot.Z,sizeof(float));
        std::memcpy(&previous.Y,&snapshot.Y,sizeof(float));
        const double length=std::sqrt((z*z+y*y)+x*x);
        const double total=length+static_cast<double>(segment);
        segment=static_cast<float>(total);
        time=static_cast<float>(total+static_cast<double>(offset));
    }

    inline void NormalizeSamples(float* samples,float total,int count)
    {
        const double inverse=1.0/static_cast<double>(total);
        for (int index=0;index<count;++index)
            samples[index]=static_cast<float>(inverse*static_cast<double>(samples[index]));
    }

    template<class Vector>
    inline void SubtractGhost(Vector& output,const Vector& a,const Vector& b)
    {
        const double z=static_cast<double>(a.Z)-b.Z;
        const double y=static_cast<double>(a.Y)-b.Y;
        const double x=static_cast<double>(a.X)-b.X;
        output.X=static_cast<float>(x);output.Y=static_cast<float>(y);output.Z=static_cast<float>(z);
    }

    template<class Vector>
    inline void ExtendEndGhost(const Vector& last,Vector& ghost)
    {
        const double dx=static_cast<double>(last.X)-ghost.X,dy=static_cast<double>(last.Y)-ghost.Y;
        const float dz=static_cast<float>(static_cast<double>(last.Z)-ghost.Z);
        const float x=static_cast<float>(dx+last.X),y=static_cast<float>(dy+last.Y);
        const float z=static_cast<float>(static_cast<double>(dz)+last.Z);
        ghost.X=x;ghost.Y=y;ghost.Z=z;
    }
}
