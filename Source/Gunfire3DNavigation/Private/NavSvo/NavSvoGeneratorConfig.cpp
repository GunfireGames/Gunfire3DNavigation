// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "NavSvoGeneratorConfig.h"

#include "Gunfire3DNavData.h"
#include "SparseVoxelOctree/SparseVoxelOctreeTile.h"
#include "SparseVoxelOctree/SparseVoxelOctreeUtils.h"

#if PROFILE_SVO_GENERATION && !UE_BUILD_TEST
// We do so many tight loops in the generation that things like array bounds checking can
// really add up and throw off the stats, so only profile in test builds.
#error "Only profile SVO generation in test builds"
#endif

FNavSvoGeneratorConfig::FNavSvoGeneratorConfig(const FVector& InSeedLocation, const AGunfire3DNavData* InNavDataActor)
	: FSvoConfig(InSeedLocation, InNavDataActor->VoxelSize, InNavDataActor->TilePoolSize, InNavDataActor->TileLayerIndex)
	, bDoAsyncGeometryGathering(InNavDataActor->bDoAsyncGeometryGathering)
{
	float AgentHalfHeightFloat = InNavDataActor->GetConfig().AgentHeight * 0.5f;
	float AgentRadiusFloat = InNavDataActor->GetConfig().AgentRadius;

	AgentHalfHeight = FMath::CeilToInt(AgentRadiusFloat / GetVoxelSize());
	AgentRadius = FMath::CeilToInt(AgentHalfHeightFloat / GetVoxelSize());

	// We need enough padding to handle whichever axis needs the most, XY, or Z.
	const uint32 NumPaddingVoxels = FMath::Max(AgentHalfHeight, AgentRadius);
	// To make all our math easier, round the number of padding voxels up to a leaf size
	const uint32 MinNumPaddingLeaves = (NumPaddingVoxels / SVO_VOXEL_GRID_EXTENT) + 1;

	const uint32 ActualLeafNodesPerAxis = FMath::RoundToInt(GetTileResolution() / GetLeafResolution());

	// Round up the number of leaf nodes we need to the next power of two. This is pretty
	// wasteful since typically we'll just need two extra leaf nodes (one to pad each
	// side), but for the Morton code range to be contiguous we need the number to be a
	// power of two.
	NumLeafNodesPerAxis = FMath::RoundUpToPowerOfTwo(ActualLeafNodesPerAxis + (MinNumPaddingLeaves * 2));
	NumPaddingLeafNodesPerAxis = NumLeafNodesPerAxis - ActualLeafNodesPerAxis;
	NumUnusedPaddingLeafNodes = ActualLeafNodesPerAxis - (MinNumPaddingLeaves * 2);

	// Expand our bounds by the agent radius and half height so we'll gather data that can
	// generate padding in our tile.
	const float XYPadding = GetVoxelSize() * AgentRadius;
	const float ZPadding = GetVoxelSize() * AgentHalfHeight;
	BoundsPadding.Set(XYPadding, XYPadding, ZPadding);

	const FIntVector MinLeafNode(NumUnusedPaddingLeafNodes / 2);
	const FIntVector MaxLeafNode(NumLeafNodesPerAxis - (NumUnusedPaddingLeafNodes / 2) - 1);

	MinPaddedLeafCode = FSvoUtils::CoordToMorton(MinLeafNode);
	MaxPaddedLeafCode = FSvoUtils::CoordToMorton(MaxLeafNode);

	SetTilePoolSizeFixed(InNavDataActor->bFixedTilePoolSize);
}
