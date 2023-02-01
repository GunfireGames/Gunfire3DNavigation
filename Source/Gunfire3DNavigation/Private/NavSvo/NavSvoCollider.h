// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "Gunfire3DNavigationTypes.h"
#include "NavSvoGeneratorConfig.h"
#include "SparseVoxelOctree/SparseVoxelOctreeUtils.h"

// Collision interface that test against the nav octree
struct FNavigationOctreeCollider
{
	typedef TSharedRef<FNavigationRelevantData, ESPMode::ThreadSafe> TNavigationData;

	// Area modifiers (e.g. cost modifiers, off-mesh links, etc.)
	struct FModifier
	{
		TArray<FAreaNavModifier> Areas;
		TArray<FTransform> PerInstanceTransform;
	};
	TStatArray<FModifier> Modifiers;

	TStatArray<FConvexNavAreaData> Blockers;

	struct FTriangle
	{
		FVector Vertices[3];
	};
	TChunkedArray<FTriangle> CulledTriangles;

	TStatArray<TNavigationData> NavigationRelevantData;

	FNavDataConfig NavDataConfigCached;
	TSharedPtr<class FNavigationOctree, ESPMode::ThreadSafe> NavigationOctreeCached = nullptr;

#if PROFILE_SVO_GENERATION
	uint32 TotalTriangles = 0;
	uint32 UsedTriangles = 0;
#endif

public:
	FNavigationOctreeCollider();
	~FNavigationOctreeCollider();

	// Returns whether any data exists for collision tests.
	bool HasCollisionData() const;

protected:
	//////////////////////////////////////////////////////////////////////////////////////
	//
	// The following functions are taken directly from RecastNavMeshGenerator.cpp with
	// some modifications
	//
	void ValidateAndAppendGeometry(TSharedRef<FNavigationRelevantData, ESPMode::ThreadSafe> ElementData, const FBox& Bounds);
	void AppendGeometry(const TNavStatArray<uint8>& RawCollisionCache, const FBox& Bounds, const FNavDataPerInstanceTransformDelegate& InTransformsDelegate);
	void AppendModifier(const FCompositeNavModifier& Modifier, const FBox& Bounds, const FNavDataPerInstanceTransformDelegate& InTransformsDelegate);

public:
	// For synchronous gathering. Call this on the main thread and all the gathering will
	// be done there.
	void GatherGeometry(UWorld* World, const FNavDataConfig& NavDataConfig, const FBox& Bounds);

	// Used for async gathering. Call GatherGeometrySources on the main thread to cache
	// off the sources, then GatherGeometryFromSources on the worker thread to process
	// the sources.
	void GatherGeometrySources(UWorld* World, const FNavDataConfig& NavDataConfig, const FBox& Bounds);
	void GatherGeometryFromSources(const FBox& Bounds);

public:
	// A helper for generating the Blockers array. If we remove that and support areas
	// fully we may not need this anymore.
	TArray<TWeakObjectPtr<UClass>> SupportedAreas;
};
