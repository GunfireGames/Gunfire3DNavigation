// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "SparseVoxelOctree/SparseVoxelOctreeConfig.h"

#define PROFILE_SVO_GENERATION 0

// Config to hold variables that are commonly accessed during the generation process.
struct FNavSvoGeneratorConfig : FSvoConfig
{
	FNavSvoGeneratorConfig(const FVector& InSeedLocation, const class AGunfire3DNavData* InNavDataActor);

	// The agent half height and radius in voxels
	uint32 AgentHalfHeight;
	uint32 AgentRadius;

	// The total number of leaf nodes per axis, including padding nodes
	uint32 NumLeafNodesPerAxis;
	// The number of leaf nodes that are padding.
	uint32 NumPaddingLeafNodesPerAxis;
	uint32 NumUnusedPaddingLeafNodes;

	uint32 MinPaddedLeafCode;
	uint32 MaxPaddedLeafCode;

	// Any bounding boxes for dirty areas should be expanded by this amount to ensure we
	// pull in geo from neighboring areas that could contribute to blocked space in this
	// tile due to padding.
	FVector BoundsPadding;

	bool bDoAsyncGeometryGathering;
};
