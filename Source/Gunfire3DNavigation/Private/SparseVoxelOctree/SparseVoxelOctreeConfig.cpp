// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "SparseVoxelOctreeConfig.h"

#include "Gunfire3DNavigationCustomVersion.h"
#include "SparseVoxelOctreeNode.h"
#include "SparseVoxelOctreeTile.h"
#include "SparseVoxelOctreeUtils.h"

FSvoConfig::FSvoConfig(const FVector& InSeedLocation, float InVoxelSize, uint32 InTilePoolSize, uint8 InTileLayerIdx)
	: SeedLocation(InSeedLocation)
	, VoxelSize(InVoxelSize)
	, TilePoolSize(InTilePoolSize)
	, bFixedTilePoolSize(false)
	, TileLayerIdx(InTileLayerIdx)
{
	Init();
}

FSvoConfig::FSvoConfig(const FSvoConfig& Other)
	: SeedLocation(Other.SeedLocation)
	, VoxelSize(Other.VoxelSize)
	, TilePoolSize(Other.TilePoolSize)
	, bFixedTilePoolSize(Other.bFixedTilePoolSize)
	, TileLayerIdx(Other.TileLayerIdx)
{
	Init();
}

void FSvoConfig::Init()
{
	// Clamp the layer index to the max supported number of layers.
	check(SVO_MAX_LAYERS > 0);
	TileLayerIdx = FMath::Min(TileLayerIdx, (uint8)(SVO_MAX_LAYERS - 1));

	VoxelExtent = FVector(VoxelSize * 0.5f);

	TileResolution = GetResolutionForLayer(TileLayerIdx);
	TileExtent = FVector(TileResolution * 0.5f);

	LeafResolution = GetResolutionForLayer(SVO_LEAF_LAYER);
	LeafExtent = FVector(LeafResolution * 0.5f);

	NumNodesPerTile = 0;
	for (uint8 LayerIdx = 0; LayerIdx < TileLayerIdx; LayerIdx++)
	{
		NumNodesPerTile += FMath::Pow(8.0f, LayerIdx + 1.0f);
	}
}

void FSvoConfig::Serialize(FArchive& Ar)
{
	// Mark that we are using the latest custom version
	Ar.UsingCustomVersion(FGunfire3DNavigationCustomVersion::GUID);

	Ar << SeedLocation;
	Ar << VoxelSize;
	Ar << TileLayerIdx;
	Ar << TilePoolSize;
	Ar << bFixedTilePoolSize;

	// Cache this config for other elements to reference
	FGunfire3DNavigationCustomVersion::SvoConfig = this;
}

bool FSvoConfig::IsCompatibleWith(const FSvoConfig& Other) const
{
	return (SeedLocation == Other.SeedLocation && VoxelSize == Other.VoxelSize && TileLayerIdx == Other.TileLayerIdx);
}

float FSvoConfig::GetResolutionForLayer(uint8 LayerIdx) const
{
	return FSvoUtils::CalcResolutionForLayer(LayerIdx, VoxelSize);
}

float FSvoConfig::GetResolutionForLink(const FSvoNodeLink& Link) const
{
	return (Link.IsVoxelNode()) ? VoxelSize : GetResolutionForLayer(Link.LayerIdx);
}

float FSvoConfig::GetChildResolutionForLayer(uint8 LayerIdx) const
{
	return (LayerIdx == SVO_LEAF_LAYER) ? VoxelSize : GetResolutionForLayer(LayerIdx - 1);
}

void FSvoConfig::GetFirstChildLocation(const FVector& NodeLocation, uint8 NodeLayerIdx, ECellOffset Offset, FVector& OutLocation) const
{
	const float NodeResolution = GetResolutionForLayer(NodeLayerIdx);
	const FVector NodeExtent(NodeResolution * 0.5f);
	OutLocation = (NodeLocation - NodeExtent);

	if (Offset != ECellOffset::Min)
	{
		const float ChildResolution = GetChildResolutionForLayer(NodeLayerIdx);

		if (Offset == ECellOffset::Center)
		{
			OutLocation += FVector(ChildResolution * 0.5f);
		}
		else if (Offset == ECellOffset::Max)
		{
			OutLocation += FVector(ChildResolution);
		}
	}
}

FVector FSvoConfig::GetVoxelLocation(const FIntVector& VoxelCoord, const FVector& NodeLocation) const
{
	FVector FirstVoxelLocation;
	GetFirstChildLocation(NodeLocation, SVO_LEAF_LAYER, ECellOffset::Center, FirstVoxelLocation);
	return (FirstVoxelLocation + (FVector(VoxelCoord) * VoxelSize));
}

FVector FSvoConfig::GetVoxelLocation(uint8 VoxelIdx, const FVector& NodeLocation) const
{
	FIntVector VoxelCoord;
	FSvoUtils::GetVoxelCoordFromIndex(VoxelIdx, VoxelCoord);
	return GetVoxelLocation(VoxelCoord, NodeLocation);
}

FBox FSvoConfig::GetTileBounds(const FVector& TileLocation) const
{
	return FBox((TileLocation - TileExtent), TileLocation + TileExtent);
}

FBox FSvoConfig::GetTileBounds(const FIntVector& TileCoord) const
{
	FVector TileLocation = TileCoordToLocation(TileCoord);
	return GetTileBounds(TileLocation);
}

FIntVector FSvoConfig::LocationToCoord(const FVector& Location, float Resolution) const
{
	return FSvoUtils::LocationToCoord(SeedLocation, Location, Resolution);
}

FVector FSvoConfig::CoordToLocation(const FIntVector& Coord, float Resolution) const
{
	return FSvoUtils::CoordToLocation(SeedLocation, Coord, Resolution);
}

FVector FSvoConfig::TileCoordToLocation(const FIntVector& Coord) const
{
	return CoordToLocation(Coord, TileResolution);
}

TMortonCode FSvoConfig::LocationToMorton(const FVector& TileMinLocation, const FVector& Location, float Resolution) const
{
	FIntVector LocationCoord = LocationToCoord(Location, Resolution);
	FIntVector MinTileCoord = LocationToCoord(TileMinLocation, Resolution);
	return FSvoUtils::CoordToMorton(LocationCoord - MinTileCoord);
}

FVector FSvoConfig::MortonToLocation(const FVector& TileMinLocation, TMortonCode MortonCode, float Resolution) const
{
	FIntVector Coord = FSvoUtils::MortonToCoord(MortonCode);
	return TileMinLocation + CoordToLocation(Coord, Resolution);
}