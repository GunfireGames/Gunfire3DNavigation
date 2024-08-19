// Copyright Gunfire Games, LLC. All Rights Reserved.

// Most of this is taken directly from RecastNavMeshGenerator.cpp, with slight modifications

#pragma once

#include "AI/NavigationSystemHelpers.h"
#include "AI/Navigation/NavigationRelevantData.h"

#include "StatArray.h"

// A simple struct for storing geometry. This is expected to be binary compatible with
// FRecastGeometryCache since we're stuffing it in FNavigationRelevantData::CollisionData
// which is currently always used with that.
struct FGeometryCache
{
	struct FHeader
	{
		FNavigationRelevantData::FCollisionDataHeader Validation;

		int32 NumVerts;
		int32 NumFaces;

		// If Recast is installed then there will be additional information we don't care about in the header
		// but we need to account for it when copying buffers.
#if WITH_RECAST
		FWalkableSlopeOverride SlopeOverride;
#endif // WITH_RECAST
	} Header;

	FVector::FReal* Verts = nullptr;
	int32* Indices = nullptr;

	FGeometryCache(const uint8* Memory)
	{
		Header = *((FHeader*)Memory);
		Verts = (FVector::FReal*)(Memory + sizeof(FGeometryCache));
		Indices = (int32*)(Memory + sizeof(FGeometryCache) + (sizeof(FVector::FReal) * Header.NumVerts * 3));
	}
};

struct FGunfire3DNavigationGeometryExport : FNavigableGeometryExport
{
	FGunfire3DNavigationGeometryExport(struct FNavigationRelevantData& InData);

	struct FNavigationRelevantData& Data;
	TStatArray<FVector::FReal> VertexBuffer;
	TStatArray<int32> IndexBuffer;

	void StoreCollisionCache();

	//~ Begin FNavigableGeometryExport Interface
	virtual void ExportChaosTriMesh(const Chaos::FTriangleMeshImplicitObject* const TriMesh, const FTransform& LocalToWorld) override;
	virtual void ExportChaosConvexMesh(const FKConvexElem* const Convex, const FTransform& LocalToWorld) override;
	virtual void ExportChaosHeightField(const Chaos::FHeightField* const Heightfield, const FTransform& LocalToWorld) override;
	virtual void ExportChaosHeightFieldSlice(const FNavHeightfieldSamples& PrefetchedHeightfieldSamples, const int32 NumRows, const int32 NumCols, const FTransform& LocalToWorld, const FBox& SliceBox) override;
	virtual void ExportRigidBodySetup(UBodySetup& BodySetup, const FTransform& LocalToWorld) override;
	virtual void ExportCustomMesh(const FVector* InVertices, int32 NumVerts, const int32* InIndices, int32 NumIndices, const FTransform& LocalToWorld) override;
	virtual void AddNavModifiers(const FCompositeNavModifier& Modifiers) override;
	virtual void SetNavDataPerInstanceTransformDelegate(const FNavDataPerInstanceTransformDelegate& InDelegate) override;
	//~ End FNavigableGeometryExport Interface
};
