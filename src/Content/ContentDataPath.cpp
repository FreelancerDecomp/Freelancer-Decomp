// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU39 (06f24080-06f24670): DATA path helper used by the mission
// controller constructors and by the ini loaders of several Content subsystems.

#include "ContentInternal.h"
#include "ContentStaticIds.h" // TU39 static IDs 06f24080..06f24260
#include "common_ini.h"
#include "common_server.h"
#include "readfile.h"

#include <cmath>
#include <stdlib.h>

// FUN_06f24280 (06f24280-06f242de), __cdecl, plain RET.
// SUB ESP,0x208: DataDir at ESP+0 (0x104), FullPath at ESP+0x104 (0x104).
// pub::GetDataPath() -> IFileSystem vtbl[+0x84] (GetFileSysPath)(DataDir, 0x104);
// _makepath(FullPath, 0, DataDir, RelativePath, 0); return Reader->open(FullPath, false).
bool __cdecl OpenContentDataIni(char const* RelativePath, INI_Reader* Reader)
{
    char DataDir[0x104];
    char FullPath[0x104];
    IFileSystem* DataFileSystem = pub::GetDataPath();
    DataFileSystem->GetFileSysPath(DataDir, 0x104);
    _makepath(FullPath, nullptr, DataDir, RelativePath, nullptr);
    return Reader->open(FullPath, false);
}

namespace
{
    // DAT_06fd0750: TU39's 3DMathEngine, created on first use (FUN_06fa8fa0) by FUN_06f243d0.
    I3DMathEngine* GDataPathMathEngine = nullptr;
}

// FUN_06f243d0 (06f243d0-06f24601), cdecl. x87 arithmetic is kept in double (the process's
// 53-bit precision control); every FSTP float of the listing is a float store here.
void __cdecl PlaceFacingAnchor(FTransform const* Anchor, float const* Heading, float const* Range,
                               FTransform* Out)
{
    // 06f243d3-06f243fc: the offset (cos h * r, 0, -(sin h * r)).
    FVector Offset;
    Offset.X = static_cast<float>(std::cos(static_cast<double>(*Heading)) * *Range);
    Offset.Y = 0.0f;
    Offset.Z = static_cast<float>(-(std::sin(static_cast<double>(*Heading)) * *Range));

    // 06f243ef-06f24439: DAT_06fd0750 slot +0x28 TransformXform into Out's position.
    if (GDataPathMathEngine == nullptr)
        GDataPathMathEngine = ContentAcquire3DMathEngine();
    FVector Position;
    GDataPathMathEngine->TransformXform(Position, *Anchor, Offset);
    Out->Translation = Position;

    // 06f24440-06f244c2: Forward = normalize(Out position - Anchor position), 1.0f DAT_06fb5810
    // over sqrt((y*y + z*z) + x*x).
    float ForwardX = static_cast<float>(static_cast<double>(Out->Translation.X) - Anchor->Translation.X);
    float ForwardY = static_cast<float>(static_cast<double>(Out->Translation.Y) - Anchor->Translation.Y);
    float ForwardZ = static_cast<float>(static_cast<double>(Out->Translation.Z) - Anchor->Translation.Z);
    double Inverse = 1.0 / std::sqrt((static_cast<double>(ForwardY) * ForwardY +
                                      static_cast<double>(ForwardZ) * ForwardZ) +
                                     static_cast<double>(ForwardX) * ForwardX);
    ForwardX = static_cast<float>(ForwardX * Inverse);
    ForwardY = static_cast<float>(ForwardY * Inverse);
    ForwardZ = static_cast<float>(ForwardZ * Inverse);

    // 06f244c4-06f2450a: Right = (z - y * 0, x * 0 - z * 0, y * 0 - x) with the 0.0f
    // DAT_06fb444c products kept.
    double const Zero = 0.0f;
    double const YZero = ForwardY * Zero;
    float RightX = static_cast<float>(ForwardZ - YZero);
    float RightY = static_cast<float>(ForwardX * Zero - ForwardZ * Zero);
    float RightZ = static_cast<float>(YZero - ForwardX);

    // 06f2450e-06f2455b: normalized over sqrt((z*z + y*y) + x*x).
    Inverse = 1.0 / std::sqrt((static_cast<double>(RightZ) * RightZ +
                               static_cast<double>(RightY) * RightY) +
                              static_cast<double>(RightX) * RightX);
    RightX = static_cast<float>(RightX * Inverse);
    RightY = static_cast<float>(RightY * Inverse);
    RightZ = static_cast<float>(RightZ * Inverse);

    // 06f2455d-06f245a4: Up = -(Forward x Right) componentwise as retail forms it.
    float const UpX = static_cast<float>(-(static_cast<double>(ForwardZ) * RightY -
                                           static_cast<double>(ForwardY) * RightZ));
    float const UpY = static_cast<float>(-(static_cast<double>(RightZ) * ForwardX -
                                           static_cast<double>(ForwardZ) * RightX));
    float const UpZ = static_cast<float>(-(static_cast<double>(ForwardY) * RightX -
                                           static_cast<double>(RightY) * ForwardX));

    // 06f245a9-06f245fa: the columns Right, Up, Forward into Out's rotation.
    Out->Rotation.D[0][0] = RightX;
    Out->Rotation.D[0][1] = UpX;
    Out->Rotation.D[0][2] = ForwardX;
    Out->Rotation.D[1][0] = RightY;
    Out->Rotation.D[1][1] = UpY;
    Out->Rotation.D[1][2] = ForwardY;
    Out->Rotation.D[2][0] = RightZ;
    Out->Rotation.D[2][1] = UpZ;
    Out->Rotation.D[2][2] = ForwardZ;
}
