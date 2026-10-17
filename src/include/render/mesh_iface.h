// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"

// Mesh interfaces shared by RendComp and materials.

struct IMaterial;

#ifndef FREELANCER_IMESH
#define FREELANCER_IMESH
struct IMesh : IDAComponent
{
    virtual int DACOM_API ResetMesh() = 0; // formerly Slot3
    virtual int DACOM_API NoOp() = 0; // formerly Slot4
    virtual int DACOM_API RefreshMaterials(unsigned long Flags) = 0; // formerly Slot5
    virtual int DACOM_API RejectOperation(void* A) = 0; // formerly Slot6
    virtual int DACOM_API RejectOperation() = 0; // formerly Slot7
    virtual int DACOM_API GetFaceGroupCount(int* Out) = 0; // formerly Slot8
    virtual int DACOM_API OpenFaceGroup(int Index, int Mode) = 0; // formerly Slot9
    virtual int DACOM_API CloseFaceGroup() = 0; // formerly Slot10
    virtual int DACOM_API AddFaceGroup(void* A) = 0; // formerly Slot11
    virtual int DACOM_API RemoveFaceGroup(int Index) = 0; // formerly Slot12
    virtual int DACOM_API GetFaceGroupPrimitiveType(unsigned int* Out) = 0; // formerly Slot13
    virtual int DACOM_API GetFaceGroupMaterial(IMaterial** Out) = 0; // formerly Slot14
    virtual int DACOM_API SetFaceGroupMaterial(IMaterial* Material) = 0; // formerly Slot15
    virtual int DACOM_API GetFaceGroupMaterialContext(void** Out) = 0; // formerly Slot16
    virtual int DACOM_API SetFaceGroupMaterialContext(void* A) = 0; // formerly Slot17
    virtual int DACOM_API OpenFaceGroupChannels(unsigned long Flags) = 0; // formerly Slot18
    virtual int DACOM_API CloseFaceGroupChannels() = 0; // formerly Slot19
    virtual int DACOM_API LockFaceGroupChannels(unsigned long Mask) = 0; // formerly Slot20
    virtual int DACOM_API UnlockFaceGroupChannels() = 0; // formerly Slot21
    virtual int DACOM_API GetFaceGroupData(int Channel, void** A, void** B, void** C, void** D) = 0; // formerly Slot22
    virtual int DACOM_API SetFaceGroupData(int Channel, void* A, void* B, void* C, void* D) = 0; // formerly Slot23
    virtual int DACOM_API GetFaceGroupIndices(int Channel, void** A, void** B) = 0; // formerly Slot24
    virtual int DACOM_API SetFaceGroupIndices(int Channel, void* Src, int Count) = 0; // formerly Slot25
    virtual int DACOM_API GetFaceGroupIndexCount(int Selector, void** Out) = 0; // formerly Slot26
    virtual int DACOM_API SetFaceGroupIndexCount(int Selector, int Count) = 0; // formerly Slot27
    virtual int DACOM_API AddFaceGroupChannels(unsigned long Flags) = 0; // formerly Slot28
    virtual int DACOM_API RemoveFaceGroupChannels(unsigned long Flags) = 0; // formerly Slot29
};
#endif

#ifndef FREELANCER_IVMESHWIRE
#define FREELANCER_IVMESHWIRE
struct IVMeshWire : IDAComponent
{

    virtual void* DACOM_API GetWireData() = 0; // formerly Slot3
    virtual int DACOM_API Draw() = 0; // formerly Slot4
    virtual void* DACOM_API GetMeshData() = 0; // formerly Slot5
};
#endif
