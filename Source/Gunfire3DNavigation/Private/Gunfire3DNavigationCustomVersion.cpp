// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "Gunfire3DNavigationCustomVersion.h"

#include "SparseVoxelOctree/SparseVoxelOctreeConfig.h"

// Initialize Statics
const FGuid FGunfire3DNavigationCustomVersion::GUID(0x8EE8740C, 0xE2E4451C, 0x9881C96F, 0xB03956CA);
FSvoConfig* FGunfire3DNavigationCustomVersion::SvoConfig = nullptr;

// Register the custom version with core
FCustomVersionRegistration GRegisterGunfire3DNavigationCustomVersion(FGunfire3DNavigationCustomVersion::GUID, FGunfire3DNavigationCustomVersion::LatestVersion, TEXT("Gunfire3DNavigationVer"));
