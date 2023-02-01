// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "SparseVoxelOctreeCommon.h"

struct FSvoConfig
{
public:
	FSvoConfig(EForceInit) { /* For Serialization */ }
	FSvoConfig(const FVector& InSeedLocation, float InVoxelSize, uint32 InTilePoolSize, uint8 InTileLayerIdx);
	FSvoConfig(const FSvoConfig& Other);

	void Init();

	void Serialize(FArchive& Ar);

	// Tests if another config is compatible with this one to determine if the tree needs
	// to be destroyed and rebuilt
	bool IsCompatibleWith(const FSvoConfig& Other) const;

	// Tile Pool
	bool IsTilePoolSizeFixed() const { return bFixedTilePoolSize; }
	void SetTilePoolSizeFixed(bool bValue) { bFixedTilePoolSize = bValue; }

	// Accessors
	const FVector& GetSeedLocation() const { return SeedLocation; }
	float GetVoxelSize() const { return VoxelSize; }
	uint32 GetTilePoolSize() const { return TilePoolSize; }
	uint8 GetTileLayerIndex() const { return TileLayerIdx; }
	uint32 GetNumNodesPerTile() const { return NumNodesPerTile; }
	float GetTileResolution() const { return TileResolution; }
	float GetLeafResolution() const { return LeafResolution; }
	const FVector& GetTileExtent() const { return TileExtent; }
	const FVector& GetLeafExtent() const { return LeafExtent; }
	const FVector& GetVoxelExtent() const { return VoxelExtent; }

	// Setters
	void SetTilePoolSize(uint32 Size) { TilePoolSize = Size; }

	// Gets the node resolution of the specified layer
	float GetResolutionForLayer(uint8 LayerIdx) const;

	// Gets the node resolution for the specified node link
	float GetResolutionForLink(const struct FSvoNodeLink& Link) const;

	// Returns the resolution of layer one step higher than the layer provided. This will
	// max out at the voxel resolution.
	float GetChildResolutionForLayer(uint8 LayerIdx) const;

	// Retrieves the location of the first child of the node at the given location and
	// layer resolution.
	void GetFirstChildLocation(const FVector& NodeLocation, uint8 NodeLayerIdx, ECellOffset Offset, FVector& OutLocation) const;

	// Given a leaf node's location, returns the the location of a specified voxel
	FVector GetVoxelLocation(const FIntVector& VoxelCoord, const FVector& NodeLocation) const;
	FVector GetVoxelLocation(uint8 VoxelIdx, const FVector& NodeLocation) const;

	// Returns the world bounds for a tile at a given location
	FBox GetTileBounds(const FVector& TileLocation) const;
	FBox GetTileBounds(const FIntVector& TileCoord) const;

	// Converts a world-relative location to coordinates based on the seed location
	FIntVector LocationToCoord(const FVector& Location, float Resolution) const;

	// Converts coordinates back to a world-relative location based on the seed location
	FVector CoordToLocation(const FIntVector& Coord, float Resolution) const;

	// Converts tile coordinates back to a world-relative location based on the seed
	// location.
	FVector TileCoordToLocation(const FIntVector& Coord) const;

	// Given a location converts to relative coords and then into a Morton code.
	TMortonCode LocationToMorton(const FVector& TileMinLocation, const FVector& Location, float Resolution) const;

	// Given a Morton code, converts back to relative coords and into a world-space
	// location.
	FVector MortonToLocation(const FVector& TileMinLocation, TMortonCode MortonCode, float Resolution) const;

protected:
	// Location from which all root nodes are relative.
	FVector SeedLocation = FVector::ZeroVector;

	// Size of voxel
	float VoxelSize = 0.f;

	// The total number of tiles available for use by the octree
	uint32 TilePoolSize = 0;

	// If true, the tile pool can be expanded to accommodate more tiles
	bool bFixedTilePoolSize = false;

	// The node size within the octree to be considered a tile
	uint8 TileLayerIdx = 0;

	// TRANSIENT: Calculated number of nodes per tile
	uint32 NumNodesPerTile = 0;

	// TRANSIENT: Calculated extent of a voxel based on 'VoxelSize'
	FVector VoxelExtent = FVector::ZeroVector;

	// TRANSIENT: Calculated resolution of 'TileLayerIdx'
	float TileResolution = 0.f;

	// TRANSIENT: Calculated extent of a node within the 'TileLayerIdx' layer of the
	// octree.
	FVector TileExtent = FVector::ZeroVector;

	// TRANSIENT: Calculated resolution of the leaf layer based on the 'VoxelSize'
	float LeafResolution = 0.f;

	// TRANSIENT: Calculate extent of a node within the leaf layer of the octree
	FVector LeafExtent = FVector::ZeroVector;
};