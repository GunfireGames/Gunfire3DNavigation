// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "SparseVoxelOctreeNode.h"
#include "SparseVoxelOctreeTile.h"
#include "SparseVoxelOctreeConfig.h"
#include "StatArray.h"
#include "IteratorHelpers.h"

//////////////////////////////////////////////////////////////////////////
//
// Sparse Voxel Octree
//
// Implementation from:
//
// "GAME AI PRO 3: Collected Wisdom of Game AI Professionals"
// "21. 3D Flight Navigation Using Sparse Voxel Octrees", p.265-274
// https://www.amazon.com/Game-AI-Pro-Collected-Professionals/dp/1498742580
// (see 'Schwarz-Seidel Octree')
//
// Referenced sources:
//
// Schwarz-Seidel Octree:
// http://research.michael-schwarz.com/publ/files/vox-siga10.pdf
// General SVO construction, Morton code handling
// (see 'Zhou Octree')
//
// Zhou Octree:
// http://kunzhou.net/2010/ParallelOctree-preprint.pdf
// General SVO construction, helpful information on neighbor linking
//
// Helpful Morton code articles:
//
// Jeroen Baert's Blog:
// http://www.forceflow.be/2013/10/07/morton-encodingdecoding-through-bit-interleaving-implementations/
// Author of LibMorton
//
// Asger Hoedt:
// http://asgerhoedt.dk/?p=276
//
// Morton code order reference in Unreal's left-handed coordinate system
// NOTE: This does not match Jeroen Baert's (LibMorton) example order in his blog.
//
//     4-------5     Z (up)
//    /|      /|     |
//   / |     / |     |
//  6-------7  |     -------- X (forward)
//  |  0----|--1    /
//  | /     | /    /
//  2-------3     Y (right)
//
//////////////////////////////////////////////////////////////////////////

namespace Gunfire3DNavigation
{
	struct FRaycastResult;
}

class GUNFIRE3DNAVIGATION_API FSparseVoxelOctree
{
	typedef FSparseVoxelOctree ThisClass;

	friend class FNavSvoTileGenerator;

public:
	FSparseVoxelOctree(const FSvoConfig& InConfig);
	virtual ~FSparseVoxelOctree();

	// Destroys all data within the octree
	virtual void Reset();

	// Determines if this octree has been generated or not
	bool IsValid() const { return (GetNumTiles() > 0); }

	// Saves octree data to an archive
	virtual void Serialize(FArchive& Ar);

	// Retrieves the config used to generate this octree
	const FSvoConfig& GetConfig() const { return Config; }

	// Returns the bounds for a given layer
	void GetBounds(FBox& OutBounds) const;

	// Determines if a point lies within the octree or not
	bool ContainsLocation(const FVector& Location) const;

	// Given a node link, retrieves the corresponding node
	FORCEINLINE const FSvoNode* GetNodeFromLink(const FSvoNodeLink& Link) const;
	FORCEINLINE FSvoNode* GetNodeFromLink(const FSvoNodeLink& Link);

	// Given a link, returns the location of the node it represents
	bool GetLocationForLink(const FSvoNodeLink& Link, FVector& OutLocation) const;

	// Finds the highest resolution unblocked node which contains the given point, if any.
	// You can set AllowBlocked to get a link back even if it's blocked, but that is
	// mostly for debugging.
	FSvoNodeLink GetLinkForLocation(const FVector& Location, bool AllowBlocked = false) const;

	// Returns the bounds for a node
	FBox GetBoundsForNode(const FSvoNode& Node) const;
	bool GetBoundsForLink(const FSvoNodeLink& Link, FBox& OutBounds) const;

	// Returns a tile by index
	const FSvoTile* GetTile(uint32 TileID) const { return Tiles.Find(TileID); }
	FSvoTile* GetTile(uint32 TileID) { return Tiles.Find(TileID); }

	// Returns the tile if it exists.  Otherwise, returns nullptr.
	const FSvoTile* GetTileAtCoord(const FIntVector& Coord) const;
	FORCEINLINE FSvoTile* GetTileAtCoord(const FIntVector& Coord);

	const FSvoTile* GetTileAtLocation(const FVector& Location) const;
	FORCEINLINE FSvoTile* GetTileAtLocation(const FVector& Location);

	const FSvoTile* GetTileForLink(const FSvoNodeLink& NodeLink) const;
	FORCEINLINE FSvoTile* GetTileForLink(const FSvoNodeLink& NodeLink);

	typedef FMapValueIterator<TMap<uint32, FSvoTile>, TMap<uint32, FSvoTile>::TRangedForIterator> FTileIterator;
	typedef FMapValueIterator<const TMap<uint32, FSvoTile>, TMap<uint32, FSvoTile>::TRangedForConstIterator> FTileConstIterator;

	// Returns all the active tiles in an iterator you can use with a ranged for
	//  Ex: for (FSvoTile& CurTile : GetTiles())
	FTileIterator GetTiles() { return Tiles; }
	FTileConstIterator GetTiles() const { return Tiles; }

	// Returns the number of active tiles
	int32 GetNumTiles() const {	return Tiles.Num(); }

	// Returns the node link for a tile at a given location
	FSvoNodeLink GetTileLinkAtCoord(const FIntVector& Coord) const;
	FSvoNodeLink GetTileLinkAtLocation(const FVector& Location) const;

	// Returns true if the specified tile coords exists within the octree
	bool HasTileAtCoord(const FIntVector& Coord) const { return (GetTileAtCoord(Coord) != nullptr); }

	// Gathers the coordinates for all active tiles that overlap the specified bounds.
	void GetTileCoords(const TArray<FBox>& BoundsArray, TArray<FIntVector>& OutTileCoords) const;

	// Calls the given callback for each active tile in the specified bounds
	void GetTilesInBounds(const FBox& QueryBounds, TFunctionRef<bool(const FSvoTile& CurTile)> TileFunc) const;

	// Casts a ray through the octree, returning true and filling out 'OutT' with the parameter along the ray
	bool Raycast(const FVector& RayStart, const FVector& RayEnd, Gunfire3DNavigation::FRaycastResult& Result) const;

	// Returns the amount of memory used by the octree
	virtual uint32 GetMemUsed() const;

	// Ensures all node data within the octree is valid
	void VerifyNodeData(bool VerifyExternalLinks = false) const;

protected:
	FVector GetLocationForNode(const FSvoNode& Node, const FSvoTile& Tile) const;

	// Links all nodes to their appropriate neighbors for all layers
	void LinkNeighbors();

	//
	// Helpers for linking a specific neighbor.
	// NOTE: This does NOT update the neighbor links back towards this node.
	//

	// Links all the neighbors for a given node
	void LinkNeighborsForNode(const FSvoNodeLink& NodeLink);

	// Links the specified neighbor for a given node
	void LinkNeighborForNode(const FSvoNodeLink& NodeLink, ESvoNeighbor Neighbor);

	// Links all neighbors for a given node and its children. If bInvalidOnly is true,
	// only neighbor links that have not yet been assigned will be linked.
	void LinkNeighborsForNodeHierarchically(const FSvoNodeLink& NodeLink, bool bInvalidOnly);

	// Links the neighbor for a given node, and for all of its children that also touch
	// that neighbor.
	void LinkNeighborForNodeHierarchically(const FSvoNodeLink& NodeLink, ESvoNeighbor Neighbor);

	// Returns the tile at the given coords, creating it if necessary.  This will initialize
	// the memory for the tile.
	FSvoTile* EnsureTileActiveAtCoord(const FIntVector& Coord);
	FSvoTile* EnsureTileActiveAtLocation(const FVector& Location);

	// Releases the memory for a specified tile.  Note that any clean up work related to
	// neighbors, etc. should have already been done prior to making this call.
	void ReleaseTile(FSvoTile* Tile);
	void ReleaseTileAtCoord(const FIntVector& Coord);
	void ReleaseTileByLink(const FSvoNodeLink& Link);

	// Retrieves the location of the first child of the NodeLink supplied.  This is
	// primarily a helper function to retrieve the location of the node before calling
	// FSvoConfig::GetFirstchildLocation.
	//
	// NOTE: If a voxel node link is supplied, the first SIBLING location will be
	// returned.
	void GetFirstChildLocation(FSvoNodeLink NodeLink, ECellOffset Offset, FVector& OutLocation) const;

	// Given a node and a location, this will return the locations coordinate value
	// relative to the first child of the node.
	// 
	// NOTE: If a voxel node link is supplied, its leaf node will be used instead.
	FIntVector GetRelativeChildCoord(const FSvoNodeLink& NodeLink, const FVector& Location) const;

	bool RaycastTile(const struct FTileRaycastInfo& Info, Gunfire3DNavigation::FRaycastResult& Result) const;

protected:
	// Configuration that defines this octree
	FSvoConfig Config;

	// All available tiles
	TMap<uint32, FSvoTile> Tiles;
	int32 MaxTiles = 0;

	static const float kRaycastEpsilon;

#if !UE_BUILD_SHIPPING
public:
	enum EDebugState { Step, Hit, Exit, Error };

	struct FRaycastDebug
	{
		int32 DebugStep = -1;
		int32 NumSteps = 0;
		EDebugState State = EDebugState::Error;
		FBox NodeBounds;
		FVector RayStart;
		FVector RayEnd;
	};

	mutable FRaycastDebug RaycastDebug;
#endif
};

typedef TSharedPtr<FSparseVoxelOctree, ESPMode::ThreadSafe> FSvoSharedPtr;

#include "SparseVoxelOctree.inl"