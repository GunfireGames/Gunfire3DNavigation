// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "SparseVoxelOctree.h"

#include "Gunfire3DNavigationCustomVersion.h"
#include "Gunfire3DNavigationUtils.h"
#include "SparseVoxelOctreeUtils.h"

#include "AI/NavigationSystemBase.h"
#include "Containers/CircularQueue.h"

// Profiling stats
DECLARE_CYCLE_STAT(TEXT("Generate (FSparseVoxelOctree)"), STAT_FSparseVoxelOctree_Generate, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("PopulateNode (FSparseVoxelOctree)"), STAT_FSparseVoxelOctree_PopulateNode, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("EnsureChildrenForNode (FSparseVoxelOctree)"), STAT_FSparseVoxelOctree_EnsureChildrenForNode, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("LinkNeighbors (FSparseVoxelOctree)"), STAT_FSparseVoxelOctree_LinkNeighbors, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("EnsureNodeExists (FSparseVoxelOctree)"), STAT_FSparseVoxelOctree_EnsureNodeExists, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("EnsureTileActiveAtCoord (FSparseVoxelOctree)"), STAT_FSparseVoxelOctree_EnsureTileActiveAtCoord, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("ReleaseTile (FSparseVoxelOctree)"), STAT_FSparseVoxelOctree_ReleaseTile, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("FindNodeLinkForLocation (FSparseVoxelOctree)"), STAT_FSparseVoxelOctree_FindNodeLinkForLocation, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("Raycast (FSparseVoxelOctree)"), STAT_FSparseVoxelOctree_Raycast, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("GetActiveTileCoords (FSparseVoxelOctree)"), STAT_FSparseVoxelOctree_GetActiveTileCoords, STATGROUP_Gunfire3DNavigation);

// We use this epsilon to push/pull the ray intersect values as needed to ensure
// overlaps
const float FSparseVoxelOctree::kRaycastEpsilon = 0.01f;

FSparseVoxelOctree::FSparseVoxelOctree(const FSvoConfig& InConfig)
	: Config(InConfig)
{
	if (InConfig.GetTilePoolSize() > 0)
	{
		MaxTiles = InConfig.GetTilePoolSize();
		Tiles.Reserve(MaxTiles);
	}
}

FSparseVoxelOctree::~FSparseVoxelOctree()
{
	// Clean up
	Reset();
}

void FSparseVoxelOctree::Reset()
{
	// Reset active tiles and look up tables
	Tiles.Reset();
}

void FSparseVoxelOctree::Serialize(FArchive& Ar)
{
	// Write our custom version to the archive.  This will only occur during a save.
	Ar.UsingCustomVersion(FGunfire3DNavigationCustomVersion::GUID);

	if (Ar.IsLoading())
	{
		Reset();

		Config.Serialize(Ar);
		Config.Init();

		Ar << MaxTiles;
		Tiles.Reserve(MaxTiles);

		Ar << Tiles;

#if !UE_BUILD_SHIPPING && SVO_VERIFY_NODES
		VerifyNodeData();
#endif
	}
	else if (Ar.IsSaving())
	{
		Config.Serialize(Ar);

		Ar << MaxTiles;
		Ar << Tiles;
	}
}

void FSparseVoxelOctree::GetBounds(FBox& OutBounds) const
{
	OutBounds.Init();

	if (!IsValid())
		return;

	// Expand by the bounds of all active tiles.  We don't really care about the nodes
	// here.
	const float TileResolution = Config.GetResolutionForLayer(Config.GetTileLayerIndex());
	const FVector LayerExtent(TileResolution * 0.5f);

	for (const FSvoTile& Tile : GetTiles())
	{
		const FVector TileLocation = Config.TileCoordToLocation(Tile.GetCoord());
		OutBounds += (TileLocation - LayerExtent);
		OutBounds += (TileLocation + LayerExtent);
	}
}

void FSparseVoxelOctree::LinkNeighbors()
{
	SCOPE_CYCLE_COUNTER(STAT_FSparseVoxelOctree_LinkNeighbors);

	if (!IsValid())
		return;

	// Link all tiles first so the nodes can link to other tiles if needed.
	for (const FSvoTile& Tile : GetTiles())
	{
		LinkNeighborsForNode(Tile.GetSelfLink());
	}

	// Now link all nodes for each tile, from the lowest resolution to the highest
	for (const FSvoTile& Tile : GetTiles())
	{
		for (int8 LayerIdx = (int8)Config.GetTileLayerIndex() - 1; LayerIdx >= SVO_LEAF_LAYER; --LayerIdx)
		{
			for (const FSvoNode& Node : Tile.GetNodesForLayer(LayerIdx))
			{
				LinkNeighborsForNode(Node.GetSelfLink());
			}
		}
	}
}

void FSparseVoxelOctree::LinkNeighborsForNode(const FSvoNodeLink& NodeLink)
{
	// Construct each neighbor
	for (ESvoNeighbor Neighbor : FSvoUtils::GetAllNeighbors())
	{
		LinkNeighborForNode(NodeLink, Neighbor);
	}
}

void FSparseVoxelOctree::LinkNeighborForNode(const FSvoNodeLink& NodeLink, ESvoNeighbor Neighbor)
{
	// Mapping from a child node index (0-7) and neighbor to the corresponding parent
	// neighbor. This will return either the same neighbor that's passed in if it's an
	// external face on the parent, or self if that face touches another child.
	static const ESvoNeighbor ChildToParentNeighborLUT[8][6] =
	{
		// Front				Right					Top					Back				Left				Bottom
		{ ESvoNeighbor::Self,	ESvoNeighbor::Self,		ESvoNeighbor::Self,	ESvoNeighbor::Back,	ESvoNeighbor::Left,	ESvoNeighbor::Bottom },	// Node 0
		{ ESvoNeighbor::Front,	ESvoNeighbor::Self,		ESvoNeighbor::Self,	ESvoNeighbor::Self,	ESvoNeighbor::Left,	ESvoNeighbor::Bottom },	// Node 1
		{ ESvoNeighbor::Self,	ESvoNeighbor::Right,	ESvoNeighbor::Self,	ESvoNeighbor::Back,	ESvoNeighbor::Self,	ESvoNeighbor::Bottom },	// Node 2
		{ ESvoNeighbor::Front,	ESvoNeighbor::Right,	ESvoNeighbor::Self,	ESvoNeighbor::Self,	ESvoNeighbor::Self,	ESvoNeighbor::Bottom },	// Node 3
		{ ESvoNeighbor::Self,	ESvoNeighbor::Self,		ESvoNeighbor::Top,	ESvoNeighbor::Back,	ESvoNeighbor::Left,	ESvoNeighbor::Self },	// Node 4
		{ ESvoNeighbor::Front,	ESvoNeighbor::Self,		ESvoNeighbor::Top,	ESvoNeighbor::Self,	ESvoNeighbor::Left,	ESvoNeighbor::Self },	// Node 5
		{ ESvoNeighbor::Self,	ESvoNeighbor::Right,	ESvoNeighbor::Top,	ESvoNeighbor::Back,	ESvoNeighbor::Self,	ESvoNeighbor::Self },	// Node 6
		{ ESvoNeighbor::Front,	ESvoNeighbor::Right,	ESvoNeighbor::Top,	ESvoNeighbor::Self,	ESvoNeighbor::Self,	ESvoNeighbor::Self },	// Node 7
	};

	// Mapping from a child node index (0-7) and neighbor to the corresponding child in
	// that direction. Note that this will wrap around if the child is in a neighbor of
	// the parent, and in that case it's the child of the parent sibling in that
	// direction.
	static const uint8 ChildNeighborLUT[8][6] =
	{
		// Front	Right	Top		Back	Left	Bottom
		{ 1,		2,		4,		1,		2,		4		},  // Node 0
		{ 0,		3,		5,		0,		3,		5		},  // Node 1
		{ 3,		0,		6,		3,		0,		6		},	// Node 2
		{ 2,		1,		7,		2,		1,		7		},	// Node 3
		{ 5,		6,		0,		5,		6,		0		},	// Node 4
		{ 4,		7,		1,		4,		7,		1		},	// Node 5
		{ 7,		4,		2,		7,		4,		2		},	// Node 6
		{ 6,		5,		3,		6,		5,		3		},	// Node 7
	};

	FSvoTile* Tile = GetTile(NodeLink.TileID);
	ensure(Tile != nullptr && Tile->GetNodeInfo().IsActive());

	if (NodeLink.LayerIdx == Config.GetTileLayerIndex())
	{
		// Tiles always link to the tile next to them since there aren't any lower
		// resolution nodes to worry about.
		const FIntVector NeighborTileCoord = Tile->GetCoord() + FSvoUtils::GetNeighborDirection(Neighbor);

		const FSvoNodeLink NeighborLink = GetTileLinkAtCoord(NeighborTileCoord);
		if (GetTileForLink(NeighborLink) != nullptr)
		{
			Tile->GetNodeInfo().SetNeighborLink(Neighbor, NeighborLink);
		}
		else
		{
			Tile->GetNodeInfo().SetNeighborLink(Neighbor, SVO_INVALID_NODELINK);
		}
	}
	else
	{
		FSvoNode* Node = Tile->GetNode(NodeLink.LayerIdx, NodeLink.NodeIdx);
		ensure(Node != nullptr && Node->IsActive());

		// By default set the neighbor link to invalid. The only time it should actually
		// stay that way is if a node is touching the face of a tile and there's no other
		// tile in that direction.
		Node->SetNeighborLink(Neighbor, SVO_INVALID_NODELINK);

		// There should always be 8 children for every node so we can quickly calculate
		// the sibling idx from the modulo
		const uint8 SiblingIdx = (NodeLink.NodeIdx % 8);

		// Look up which parent neighbor contains this nodes neighbor
		const ESvoNeighbor ParentNeighborDir = ChildToParentNeighborLUT[SiblingIdx][(uint8)Neighbor];

		// Look up sibling index for neighbor
		const uint8 NeighborSiblingIdx = ChildNeighborLUT[SiblingIdx][(uint8)Neighbor];

		// 'Self' is a special case to handle neighbors that are siblings of the node
		// currently being processed.
		if (ParentNeighborDir == ESvoNeighbor::Self)
		{
			Node->SetNeighborLink(Neighbor, FSvoNodeLink(NodeLink.TileID, NodeLink.LayerIdx, (NodeLink.NodeIdx - SiblingIdx) + NeighborSiblingIdx));
		}
		// Otherwise, consider all surrounding parent-neighbor nodes if this node has a
		// parent. NOTE: Only the top most layer and the tile should be parent-less during
		// this process. Later, the tile will become the parent of the top layer.
		else if (FSvoNode* ParentNode = GetNodeFromLink(Node->GetParentLink()))
		{
			const FSvoNodeLink ParentNeighborLink = ParentNode->GetNeighborLink(*this, ParentNeighborDir);
			if (ParentNeighborLink.IsValid())
			{
				const FSvoNode* ParentNeighborNode = GetNodeFromLink(ParentNeighborLink);

				// This node should *always* be valid.  If this assert is popping, *do not
				// ignore it*!
				if (ensureAlways(ParentNeighborNode))
				{
					// If this parent-neighbor node has children, assign this child
					if (ParentNeighborNode->HasChildren())
					{
						Node->SetNeighborLink(Neighbor, ParentNeighborNode->GetChildLink(NeighborSiblingIdx));
					}
					// Otherwise, assign this neighbor to the parent neighbor
					else
					{
						Node->SetNeighborLink(Neighbor, ParentNeighborLink);
					}
				}
			}
		}
	}
}

void FSparseVoxelOctree::LinkNeighborsForNodeHierarchically(const FSvoNodeLink& NodeLink, bool bInvalidOnly)
{
	for (FSvoNeighborConstIterator NeighborIter(*this, NodeLink, false /* bSkipInvalid */); NeighborIter; ++NeighborIter)
	{
		const ESvoNeighbor Neighbor = NeighborIter.GetNeighbor();
		const FSvoNodeLink NeighborLink = NeighborIter.GetNeighborLink();

		// Only link for this neighbor if not preserving existing neighbor links or the
		// current neighbor link is invalid.
		if (!bInvalidOnly || !NeighborLink.IsValid())
		{
			LinkNeighborForNodeHierarchically(NodeLink, Neighbor);
		}
	}
}

void FSparseVoxelOctree::LinkNeighborForNodeHierarchically(const FSvoNodeLink& NodeLink, ESvoNeighbor Neighbor)
{
	const FSvoNode* Node = GetNodeFromLink(NodeLink);

	// Do not process inactive nodes
	if (Node == nullptr || !Node->IsActive())
		return;

	// Re-link the neighbor for this node
	LinkNeighborForNode(NodeLink, Neighbor);

	// Now grab all touching child nodes and update them to point to this node as well
	if (Node->HasChildren())
	{
		for (uint8 ChildIdx : FSvoUtils::GetChildrenTouchingNeighbor(Neighbor))
		{
			LinkNeighborForNodeHierarchically(Node->GetChildLink(ChildIdx), Neighbor);
		}
	}
}

FSvoTile* FSparseVoxelOctree::EnsureTileActiveAtCoord(const FIntVector& Coord)
{
	SCOPE_CYCLE_COUNTER(STAT_FSparseVoxelOctree_EnsureTileActiveAtCoord);

	FSvoTile* Tile = GetTileAtCoord(Coord);
	if (Tile == nullptr)
	{
		if (Tiles.Num() == MaxTiles)
		{
			if (!Config.IsTilePoolSizeFixed())
			{
				// If the tile pool is full and we're allowed to expand it, increment the
				// number of tiles.
				// WARNING: It should be noted that this can potentially invalidate all
				//			references to any tiles within the tile pool.  Any code
				//			making calls to ensure nodes are active (tiles are nodes) need
				//			to design around this.
				MaxTiles += Config.GetTilePoolSize();
				Tiles.Reserve(MaxTiles);
			}
			else
			{
				static bool bWarned = false;
				if (!bWarned)
				{
					UE_LOG(LogNavigation, Warning, TEXT("FSparseVoxelOctree::EnsureTileActiveAtCoord : Out of tiles; Aborting!)"));
					bWarned = true;
				}

				return nullptr;
			}
		}

		const uint32 TileID = FSvoTile::CalcTileID(Coord);

		Tile = &Tiles.Add(TileID, FSvoTile(TileID, Config.GetTileLayerIndex(), Coord));

		ensureAlways(!Tile->GetNodeInfo().HasChildren());

#if !UE_BUILD_SHIPPING && SVO_VERIFY_NODES
		VerifyNodeData();
#endif
	}

	ensureAlways(Tile->GetNodeInfo().IsActive());

	return Tile;
}

FSvoTile* FSparseVoxelOctree::EnsureTileActiveAtLocation(const FVector& Location)
{
	const FIntVector TileCoord = Config.LocationToCoord(Location, Config.GetTileResolution());
	return EnsureTileActiveAtCoord(TileCoord);
}

void FSparseVoxelOctree::ReleaseTile(FSvoTile* Tile)
{
	SCOPE_CYCLE_COUNTER(STAT_FSparseVoxelOctree_ReleaseTile);

	if (!IsValid())
		return;

	if (Tile != nullptr)
	{
#if !UE_BUILD_SHIPPING && SVO_VERIFY_NODES
		Tile->Verify();
#endif

		const uint32 TileID = Tile->GetID();

		// Reset the tile to prevent errant data the next time it's used.
		Tile->Reset();

		const bool bWasRemoved = (Tiles.Remove(TileID) > 0);

#if !UE_BUILD_SHIPPING && SVO_VERIFY_NODES
		VerifyNodeData();
#endif

		ensure(bWasRemoved);
	}
}

void FSparseVoxelOctree::ReleaseTileAtCoord(const FIntVector& Coord)
{
	FSvoTile* Tile = GetTileAtCoord(Coord);
	ReleaseTile(Tile);
}

void FSparseVoxelOctree::ReleaseTileByLink(const FSvoNodeLink& Link)
{
	FSvoTile* Tile = GetTileForLink(Link);
	ReleaseTile(Tile);
}

const FSvoTile* FSparseVoxelOctree::GetTileAtCoord(const FIntVector& Coord) const
{
	const uint32 TileID = FSvoTile::CalcTileID(Coord);
	return GetTile(TileID);
}

const FSvoTile* FSparseVoxelOctree::GetTileAtLocation(const FVector& Location) const
{
	const FIntVector TileCoord = Config.LocationToCoord(Location, Config.GetTileResolution());
	return GetTileAtCoord(TileCoord);
}

const FSvoTile* FSparseVoxelOctree::GetTileForLink(const FSvoNodeLink& NodeLink) const
{
	return GetTile(NodeLink.TileID);
}

FSvoNodeLink FSparseVoxelOctree::GetTileLinkAtCoord(const FIntVector& Coord) const
{
	const uint32 TileID = FSvoTile::CalcTileID(Coord);
	return FSvoNodeLink(TileID, Config.GetTileLayerIndex(), 0);
}

FSvoNodeLink FSparseVoxelOctree::GetTileLinkAtLocation(const FVector& Location) const
{
	const FIntVector TileCoord = Config.LocationToCoord(Location, Config.GetTileResolution());
	return GetTileLinkAtCoord(TileCoord);
}

bool FSparseVoxelOctree::ContainsLocation(const FVector& Location) const
{
	return (GetTileAtLocation(Location) != nullptr);
}

bool FSparseVoxelOctree::GetLocationForLink(const FSvoNodeLink& Link, FVector& OutLocation) const
{
	if (!Link.IsValid())
	{
		return false;
	}

	if (const FSvoTile* Tile = GetTile(Link.TileID))
	{
		if (Link.LayerIdx == Config.GetTileLayerIndex())
		{
			OutLocation = Config.TileCoordToLocation(Tile->GetCoord());
			return true;
		}
		else
		{
			if (const FSvoNode* Node = Tile->GetNode(Link.LayerIdx, Link.NodeIdx))
			{
				OutLocation = GetLocationForNode(*Node, *Tile);

				if (Link.IsVoxelNode())
				{
					OutLocation = Config.GetVoxelLocation(Link.VoxelIdx, OutLocation);
				}

				return true;
			}
		}
	}

	return false;
}

FVector FSparseVoxelOctree::GetLocationForNode(const FSvoNode& Node, const FSvoTile& Tile) const
{
	const FSvoNodeLink NodeLink = Node.GetSelfLink();

	const FVector TileLocation = Config.TileCoordToLocation(Tile.GetCoord());
	const FBox TileBounds = Config.GetTileBounds(TileLocation);
	const float NodeSize = Config.GetResolutionForLink(NodeLink);

	FVector Location = Config.MortonToLocation(TileBounds.Min, NodeLink.NodeIdx, NodeSize);

	if (NodeLink.IsVoxelNode())
	{
		Location = Config.GetVoxelLocation(NodeLink.VoxelIdx, Location);
	}

	return Location;
}

FSvoNodeLink FSparseVoxelOctree::GetLinkForLocation(const FVector& Location, bool AllowBlocked) const
{
	SCOPE_CYCLE_COUNTER(STAT_FSparseVoxelOctree_FindNodeLinkForLocation);

	// If the tree hasn't been generated, bail
	if (!IsValid())
	{
		return SVO_INVALID_NODELINK;
	}

	// First determine if the location is even within the a tile of the octree.
	const FSvoNodeLink TileLink = GetTileLinkAtLocation(Location);
	const FSvoTile* Tile = GetTileForLink(TileLink);

	if (Tile == nullptr)
	{
		return SVO_INVALID_NODELINK;
	}

	// Starting at the tile, work our way down into the tree until we either the desired
	// layer, find an empty node or reach the leaf layer.
	FSvoNodeLink CurNodeLink = TileLink;
	const FSvoNode* CurNode = &Tile->GetNodeInfo();

	while (CurNodeLink.IsValid())
	{
		// If this is the tile layer we already looked up the node info, otherwise look it
		// up
		if (CurNodeLink.LayerIdx != Config.GetTileLayerIndex())
		{
			CurNode = Tile->GetNode(CurNodeLink.LayerIdx, CurNodeLink.NodeIdx);
		}

		if (!ensure(CurNode))
		{
			UE_LOG(LogNavigation, Warning, TEXT("FSparseVoxelOctree::GetLinkForLocation : Failed to find node; Aborting\n"
				"    TileID: %d\n"
				"    LayerIdx: %d\n"
				"    NodeIdx: %d\n"
				"    VoxelIdx: %d\n"
				"    UserData: %d"),
				CurNodeLink.TileID, CurNodeLink.LayerIdx, CurNodeLink.NodeIdx, CurNodeLink.VoxelIdx, CurNodeLink.UserData);

			return SVO_INVALID_NODELINK;
		}

		ensure(CurNode->GetSelfLink() == CurNodeLink);

		// If at the leaf layer we either:
		//	Return an invalid link id if fully blocked.
		//	Search for the overlapping voxel and return its state
		if (CurNodeLink.IsLeafNode())
		{
			FVector VoxelLocation;

			if (CurNode->GetNodeState() == ENodeState::Open)
			{
				return CurNode->GetSelfLink();
			}
			else if (CurNode->GetNodeState() == ENodeState::Blocked)
			{
				// If we're returning blocked nodes, since this one is fully blocked the
				// highest resolution node is the leaf.
				if (AllowBlocked)
				{
					return CurNode->GetSelfLink();
				}
				// If fully blocked, no voxel will be valid so return an invalid link
				else
				{
					return SVO_INVALID_NODELINK;
				}
			}
			else
			{
				// Knowing that the location is within the bounds of this leaf node, we
				// can deduce the voxel location easily via coords.
				FIntVector VoxelCoord = GetRelativeChildCoord(CurNodeLink, Location);

				// This can happen due to floating point errors.  To fix this, we just
				// increment the negative values by one.
				// NOTE: There should *never* be an error greater than 1!
				if (!FSvoUtils::IsVoxelCoordValid(VoxelCoord))
				{
					if (VoxelCoord.X < 0) ++VoxelCoord.X;
					if (VoxelCoord.Y < 0) ++VoxelCoord.Y;
					if (VoxelCoord.Z < 0) ++VoxelCoord.Z;

					// The voxel coord should definitely be valid at this point
					ensure(FSvoUtils::IsVoxelCoordValid(VoxelCoord));
				}

				CurNodeLink.VoxelIdx = FSvoUtils::GetVoxelIndexForCoord(VoxelCoord);

				const bool bVoxelIsBlocked = CurNode->IsVoxelBlocked(CurNodeLink.VoxelIdx);

				if (!bVoxelIsBlocked || AllowBlocked)
				{
					return CurNodeLink;
				}
				else
				{
					return SVO_INVALID_NODELINK;
				}
			}
		}
		// If at a normal layer, we either:
		//		a.) Descend to the next layer if children exist
		//		b.) Return this node for it is just open space
		else if (CurNode->GetNodeState() == ENodeState::PartiallyBlocked)
		{
			static const FIntVector ChildCoordExtents(SVO_OCTANT_GRID_EXTENT);

			const FIntVector ChildCoord = GetRelativeChildCoord(CurNodeLink, Location);
			if (ensure(FSvoUtils::IsCoordValid(ChildCoord, ChildCoordExtents)))
			{
				const uint32 CoordIdx = FSvoUtils::GetIndexForCoord(ChildCoord, ChildCoordExtents);
				CurNodeLink = CurNode->GetChildLink(CoordIdx);
			}
			else
			{
				return SVO_INVALID_NODELINK;
			}
		}
		else
		{
			// This node has no children. If it's open it's the highest resolution link
			// we can find for this coordinate.
			return (CurNode->GetNodeState() == ENodeState::Open) ? CurNodeLink : SVO_INVALID_NODELINK;
		}
	}

	return SVO_INVALID_NODELINK;
}

FBox FSparseVoxelOctree::GetBoundsForNode(const FSvoNode& Node) const
{
	const FSvoNodeLink NodeLink = Node.GetSelfLink();
	const FSvoTile* Tile = GetTile(NodeLink.TileID);
	check(Tile);

	const FVector NodeLocation = GetLocationForNode(Node, *Tile);

	const float NodeResolution = Config.GetResolutionForLayer(NodeLink.LayerIdx);
	return FBox::BuildAABB(NodeLocation, FVector(NodeResolution * 0.5f));
}

bool FSparseVoxelOctree::GetBoundsForLink(const FSvoNodeLink& Link, FBox& OutBounds) const
{
	if (Link.IsValid())
	{
		// Ensure the bounds are zeroed out
		OutBounds.Init();

		FVector NodeLocation;
		if (GetLocationForLink(Link, NodeLocation))
		{
			const float NodeResolution = Config.GetResolutionForLink(Link);
			OutBounds = FBox::BuildAABB(NodeLocation, FVector(NodeResolution * 0.5f));

			return true;
		}
	}

	return false;
}

void FSparseVoxelOctree::GetFirstChildLocation(FSvoNodeLink NodeLink, ECellOffset Offset, FVector& OutLocation) const
{
	// Clear the voxel index so we don't attempt to find the child of a voxel, which isn't
	// valid.  This means that this effectively finds the first sibling for voxel node
	// links but ensures that we don't end up with a strange offset.
	NodeLink.VoxelIdx = SVO_NO_VOXEL;

	// Grab the location for the parent node which we will use in the next step as the
	// base for finding the child.
	FVector NodeLocation;
	GetLocationForLink(NodeLink, NodeLocation);

	Config.GetFirstChildLocation(NodeLocation, NodeLink.LayerIdx, Offset, OutLocation);
}

FIntVector FSparseVoxelOctree::GetRelativeChildCoord(const FSvoNodeLink& NodeLink, const FVector& Location) const
{
	// Get the location of the first child to allow us to find its coords
	//
	// NOTE: If the node link is pointing to a voxel, the leaf node will be used.
	FVector FirstChildLocation;
	GetFirstChildLocation(NodeLink, ECellOffset::Center, FirstChildLocation);

	// Get the resolution of the child layer of the node
	const float ChildResolution = Config.GetChildResolutionForLayer(NodeLink.LayerIdx);

	// Get the world-space coordinates of the first child location and the desired
	// location in terms of the child resolution.
	const FIntVector FirstChildCoord = Config.LocationToCoord(FirstChildLocation, ChildResolution);
	const FIntVector LocationCoord = Config.LocationToCoord(Location, ChildResolution);

	// Now find the difference between the two coordinates to determine the relative
	// coordinate offset from the first child node.
	//
	// NOTE: It is expected that this will potentially return an invalid coordinate (e.g.
	// outside of the bounds of a voxel grid).  It is the onus of the caller to handle
	// this case.
	return (LocationCoord - FirstChildCoord);
}

struct FTileIntersection
{
	float MinT;
	float MaxT;
	FVector MinLocation;
	FSvoNodeLink TileNodeLink;
	FVector TileMinLocation;
};

struct FTileRaycastInfo
{
	FVector RayStart;
	FVector RayEnd;
	FVector RaySegment;
	FVector RayDir;
	float RayLength;

	FTileIntersection TileInfo;
};

bool FSparseVoxelOctree::Raycast(const FVector& RayStart, const FVector& RayEnd, Gunfire3DNavigation::FRaycastResult& Result) const
{
	SCOPE_CYCLE_COUNTER(STAT_FSparseVoxelOctree_Raycast);

	// Initialize to the end to represent no point found.
	Result.HitLocation.Location = FNavLocation(RayEnd, SVO_INVALID_NODELINK);

	// If the tree hasn't been generated, bail
	if (!IsValid())
	{
		return false;
	}

	// Setup a few ray helpers
	const FVector RaySegment = (RayEnd - RayStart);
	const FVector RayDir = RaySegment.GetSafeNormal();
	const float RayLength = RaySegment.Size();
	const FBox RayBounds = FBox(ForceInit) + RayStart + RayEnd;

	TArray<FTileIntersection> TileIntersections;

	// Collect all tile nodes that the ray intersects
	GetTilesInBounds(RayBounds, [&](const FSvoTile& Tile)
	{
		const FVector TileLocation = Config.TileCoordToLocation(Tile.GetCoord());
		const FBox TileBounds = Config.GetTileBounds(TileLocation);

		float TileMinT, TileMaxT;

		if (FGunfire3DNavigationUtils::RayAABBIntersect(RayStart, RayDir, TileBounds, TileMinT, TileMaxT))
		{
			// The intersection test can return parameter outside of the line segment
			// we're testing so clamp them here.
			TileMinT = FMath::Max(kRaycastEpsilon, TileMinT + kRaycastEpsilon);
			TileMaxT = FMath::Clamp(TileMaxT - kRaycastEpsilon, kRaycastEpsilon, RayLength);

			if (TileMaxT > 0.0f && (TileMaxT - TileMinT) > kRaycastEpsilon)
			{
				const FVector MaxLocation = RayStart + (RayDir * TileMaxT);
				const FVector MinLocation = RayStart + (RayDir * TileMinT);

				TileIntersections.Add({ TileMinT, TileMaxT, MinLocation, Tile.GetSelfLink(), TileBounds.Min });
			}
		}

		return true;
	});

	// If the ray does not intersect a tile volume then there is no intersection
	if (TileIntersections.Num() == 0)
	{
		return false;
	}

	// Sort intersections by distance along the ray
	TileIntersections.Sort([](const FTileIntersection& A, const FTileIntersection& B)
	{
		return A.MinT < B.MinT;
	});

	FTileRaycastInfo Info;
	Info.RayStart = RayStart;
	Info.RayEnd = RayEnd;
	Info.RaySegment = RaySegment;
	Info.RayDir = RayDir;
	Info.RayLength = RayLength;

#if !UE_BUILD_SHIPPING
	RaycastDebug.NumSteps = 0;
	RaycastDebug.State = EDebugState::Error;
#endif

	// Test the ray against each tile.
	for (const FTileIntersection& TileIntersection : TileIntersections)
	{
		Info.TileInfo = TileIntersection;

		// If this returns true we got a hit in this tile and we're done. Otherwise,
		// continue into the next tile.
		if (RaycastTile(Info, Result))
		{
			return true;
		}
	}

	return false;
}

bool FSparseVoxelOctree::RaycastTile(const FTileRaycastInfo& Info, Gunfire3DNavigation::FRaycastResult& Result) const
{
	float CurrentRayT = Info.TileInfo.MinT;
	FVector CurrentRayLocation = Info.TileInfo.MinLocation;
	FSvoNodeLink CurNodeLink = Info.TileInfo.TileNodeLink;
	const FSvoTile* Tile = GetTile(CurNodeLink.TileID);

#if !UE_BUILD_SHIPPING
	auto DebugRay = [this, &CurNodeLink, &Info, &CurrentRayT, &CurrentRayLocation](EDebugState State)
	{
		if (RaycastDebug.DebugStep == (RaycastDebug.NumSteps - 1))
		{
			RaycastDebug.State = State;
			RaycastDebug.RayStart = Info.RayStart;
			RaycastDebug.RayEnd = CurrentRayLocation;
			GetBoundsForLink(CurNodeLink, RaycastDebug.NodeBounds);
		}
	};
#else
	#define DebugRay(DebugState)
#endif

	while (CurNodeLink.IsValid())
	{
#if !UE_BUILD_SHIPPING
		RaycastDebug.NumSteps++;
		DebugRay(EDebugState::Step);
#endif

		// We exited the tile bounds without a hit, let the next tile have a try
		if (CurrentRayT >= Info.TileInfo.MaxT)
		{
			DebugRay(EDebugState::Exit);
			return false;
		}

		const FSvoNode* Node = GetNodeFromLink(CurNodeLink);
		check(Node);
		check(Tile == GetTile(CurNodeLink.TileID));
		const FVector NodeLocation = GetLocationForNode(*Node, *Tile);

		// Get the Morton code for the current node
		float NodeResolution = Config.GetResolutionForLayer(CurNodeLink.LayerIdx);
		const TMortonCode NodeMortonCode = CurNodeLink.NodeIdx;
		const int8 NodeSiblingIdx = FSvoUtils::GetChildIndex(NodeMortonCode);

		enum class EAdvanceRay { Success, Done, Error };

		// A helper to handle moving the current ray location to the exit position/time
		// for the current node.
		auto AdvanceRay = [this, &CurNodeLink, &Info, &CurrentRayT, &CurrentRayLocation]() -> EAdvanceRay
		{
			// Get the node bounds and inflate them a bit to account for edge/corner overlaps.
			FBox NodeBounds;
			ensure(GetBoundsForLink(CurNodeLink, NodeBounds));
			NodeBounds = NodeBounds.ExpandBy(kRaycastEpsilon);

			// Get the the locations at which the ray intersects this node.
			float NodeMinT, NodeMaxT;
			const bool bIntersects = FGunfire3DNavigationUtils::RayAABBIntersect(Info.RayStart, Info.RayDir, NodeBounds, NodeMinT, NodeMaxT);
			//ensure(bIntersects);

			// We force the current parameter to advance by the Epsilon to
			// prevent infinite loops.
			NodeMaxT = FMath::Clamp(NodeMaxT, CurrentRayT + kRaycastEpsilon, Info.TileInfo.MaxT);
			//ensure(NodeMaxT > CurrentRayT || NodeMaxT == Info.TileInfo.MaxT);

			// Move the current ray time/position to the exit point on this node
			CurrentRayT = NodeMaxT;
			CurrentRayLocation = Info.RayStart + (Info.RayDir * CurrentRayT);

			// If we've reached the max extents of this tile break out to evaluate the
			// next tile.
			if (CurrentRayT >= Info.TileInfo.MaxT)
			{
				return EAdvanceRay::Done;
			}

			if (CurrentRayT >= Info.RayLength)
			{
				return EAdvanceRay::Done;
			}

			// This *should* always intersect however, if for some reason it
			// doesn't, the current parameter will be pushed out a bit to
			// prevent infinite loops.
			if (!bIntersects)
			{
				return EAdvanceRay::Error;
			}

			return EAdvanceRay::Success;
		};

		// If this entire node is blocked we're done, return the hit.
		if (Node->GetNodeState() == ENodeState::Blocked)
		{
			DebugRay(EDebugState::Hit);
			Result.HitTime = (CurrentRayT / Info.RayLength);
			Result.HitLocation.Location = (Info.RayStart + (Info.RayEnd - Info.RayStart) * Result.HitTime);
			Result.HitLocation.NodeRef = CurNodeLink.GetID();
			return true;
		}

		// If this node is open we want to advance the ray to the other side of it.
		bool bAdvanceRay = (Node->GetNodeState() == ENodeState::Open);

		// If the node is partially blocked we need to descend into the child nodes.
		if (Node->GetNodeState() == ENodeState::PartiallyBlocked)
		{
			if (CurNodeLink.IsLeafNode())
			{
				// Knowing that the location is within the bounds of this leaf node,
				// we can deduce the voxel location
				// via coords.
				const FIntVector VoxelCoord = GetRelativeChildCoord(CurNodeLink, CurrentRayLocation);

				// If exploring voxels we first need to determine if we are entering
				// this node for the first time, in which case we need to calculate
				// the voxel index.  If we are moving from one voxel to another within
				// this leaf then the link will represent a voxel and we can assume
				// this we are referencing a voxel directly and not just the leaf node
				// as a whole.
				if (!CurNodeLink.IsVoxelNode() && FSvoUtils::IsVoxelCoordValid(VoxelCoord))
				{
					// If this coord is valid, then the location lies within this node
					// so calculate the voxel index.
					CurNodeLink.VoxelIdx = FSvoUtils::GetVoxelIndexForCoord(VoxelCoord);
				}

				// Continue if we're processing a voxel within this leaf.  Otherwise
				// we will process the neighbors of this leaf to find the neighboring
				// node/voxel to move to.
				if (CurNodeLink.IsVoxelNode())
				{
					// If this voxel is blocked then return the hit
					if (Node->IsVoxelBlocked(CurNodeLink.VoxelIdx))
					{
						DebugRay(EDebugState::Hit);
						Result.HitTime = (CurrentRayT / Info.RayLength);
						Result.HitLocation.Location = (Info.RayStart + (Info.RaySegment * Result.HitTime));
						Result.HitLocation.NodeRef = CurNodeLink.GetID();
						return true;
					}

					// If the voxel is open take care of updating the current ray
					// location.
					const EAdvanceRay AdvanceRet = AdvanceRay();
					if (AdvanceRet == EAdvanceRay::Done)
					{
						DebugRay(EDebugState::Exit);
						return false;
					}
					else if (AdvanceRet == EAdvanceRay::Error)
					{
						DebugRay(EDebugState::Error);
						CurNodeLink = Node->GetParentLink();
						continue;
					}

					// Get the ray location as a relative voxel coord to the leaf node.
					const FIntVector NeighborVoxelCoord = GetRelativeChildCoord(CurNodeLink, CurrentRayLocation);

					// The current voxel coord and neighbor voxel coord *should*
					// always be different however, if for some reason they aren't,
					// the current parameter will have been pushed out already after
					// the intersect so move up to the parent to try again.
					const bool bAreVoxelsDifferent = (NeighborVoxelCoord != VoxelCoord);
					if (/*ensure*/(bAreVoxelsDifferent) == false)
					{
						DebugRay(EDebugState::Error);
						CurNodeLink = Node->GetParentLink();
						continue;
					}

					// If this coord is valid (part of the same leaf node) then move
					// to the neighbor and do not process node neighbors.  Otherwise
					// we'll need to process the leaf node neighbor which we do by
					// resetting the voxel index so this is treated as a leaf and not
					// a voxel.
					if (FSvoUtils::IsVoxelCoordValid(NeighborVoxelCoord))
					{
						// Set the voxel id and mark that we are currently exploring voxels
						CurNodeLink.VoxelIdx = FSvoUtils::GetVoxelIndexForCoord(NeighborVoxelCoord);
					}
					else
					{
						CurNodeLink.VoxelIdx = SVO_NO_VOXEL;
						bAdvanceRay = true;
					}
				}
			}
			else
			{
				// Get the Morton code for the current ray location at the child's
				// resolution
				const float ChildNodeResolution = Config.GetResolutionForLayer(Node->GetChildLink(0).LayerIdx);
				const TMortonCode ChildMortonCode = Config.LocationToMorton(Info.TileInfo.TileMinLocation, CurrentRayLocation, ChildNodeResolution);

				// If the child is indeed a child of the current node then move in further
				// to process it.  Otherwise we need to move up higher in the tree.
				if ((ChildMortonCode >> 3) == NodeMortonCode)
				{
					// Build child link
					const uint8 ChildIdx = FSvoUtils::GetChildIndex(ChildMortonCode);
					CurNodeLink = Node->GetChildLink(ChildIdx);
				}
				else
				{
					DebugRay(EDebugState::Step);

					// In this case, the currently processed location is not a child of
					// this node so we need to climb higher in the tree.  This can happen
					// when a neighbor isn't found from a child node and is in the process
					// of climbing back up the tree to find the containing node.
					CurNodeLink = Node->GetParentLink();
				}
			}
		}

		// If node descending through children or exploring voxels then we will be
		// exploring neighbors in search of the next node to intersect the ray.
		if (bAdvanceRay)
		{
			const EAdvanceRay AdvanceRet = AdvanceRay();

			if (AdvanceRet == EAdvanceRay::Done)
			{
				DebugRay(EDebugState::Exit);
				return false;
			}
			else if (AdvanceRet == EAdvanceRay::Error)
			{
				DebugRay(EDebugState::Error);
				CurNodeLink = Node->GetParentLink();
				continue;
			}

			// Resolve world node coordinates for current node and the expected
			// neighbor
			const FIntVector NodeCoord = Config.LocationToCoord(NodeLocation, NodeResolution);
			const FIntVector NeighborCoord = Config.LocationToCoord(CurrentRayLocation, NodeResolution);

			// The node coord and neighbor coord *should* always be different however,
			// if for some reason they aren't, the current parameter will have been
			// pushed out already after the intersect so move up to the parent to try
			// again.
			const bool bAreDifferentNodes = (NeighborCoord != NodeCoord);
			if (/*ensure*/(bAreDifferentNodes) == false)
			{
				DebugRay(EDebugState::Error);
				CurNodeLink = Node->GetParentLink();
				continue;
			}

			// Because the ray can traverse multiple tiles, we need to ensure that the
			// coord is still valid for the Morton code base as we can't deal with
			// negative Morton codes.  If the coord isn't valid then break out of this
			// tile node intersection test as no further coords will be valid.
			const FIntVector MinTileCoord = Config.LocationToCoord(Info.TileInfo.TileMinLocation, NodeResolution);
			const FIntVector NeighborMortonCoord = (NeighborCoord - MinTileCoord);
			if (!FSvoUtils::IsValidMortonCoord(NeighborMortonCoord))
			{
				DebugRay(EDebugState::Exit);
				return false;
			}

			// Calculate the Morton code now that we know it's valid for this base.
			const TMortonCode NeighborMortonCode = FSvoUtils::CoordToMorton(NeighborMortonCoord);

			// Now that we know which sibling this is, we can use the LUT above to
			// determine which neighbor (if any) this corresponds with.
			ESvoNeighbor Neighbor = FSvoUtils::GetNeighborType(FSvoUtils::GetChildIndex(NeighborMortonCode), NodeSiblingIdx);

			// If this neighbor is valid (not Self) we need to determine if it is a
			// part of the node we are currently processing or the neighbor of our
			// parent.  To do this, we compare the parent Morton codes of this node
			// and the neighbor. If they are the same, then we can use the neighbor
			// idx as is.  If not the same, we need to get the opposite neighbor idx
			// as this will correspond with the child of the parent neighbor.
			if (Neighbor != ESvoNeighbor::Self)
			{
				if (!FSvoUtils::AreSiblings(NodeMortonCode, NeighborMortonCode))
				{
					Neighbor = FSvoUtils::GetOppositeNeighbor(Neighbor);
				}

				CurNodeLink = Node->GetNeighborLink(*this, Neighbor);
			}
			else
			{
				// In this case, our neighbor is in a 26 neighbor spectrum (a
				// diagonal) which we don't support so we'll need to move back up the
				// tree until we find a suitable node to process.
				CurNodeLink = Node->GetParentLink();
			}
		}
	}

	return false;
}

void FSparseVoxelOctree::GetTileCoords(const TArray<FBox>& BoundsArray, TArray<FIntVector>& OutTileCoords) const
{
	SCOPE_CYCLE_COUNTER(STAT_FSparseVoxelOctree_GetActiveTileCoords);

	if (!IsValid())
		return;

	for (const FBox& Bounds : BoundsArray)
	{
		FIntVector MinTileCoord, MaxTileCoord;
		FSvoUtils::GetCoordsForBounds(Config.GetSeedLocation(), Bounds, Config.GetTileResolution(), MinTileCoord, MaxTileCoord);

		FCoordIterator TileCoordIter(MinTileCoord, MaxTileCoord);
		OutTileCoords.Reserve(OutTileCoords.Num() + TileCoordIter.GetNumCoords());

		while (TileCoordIter)
		{
			const FIntVector& TileCoord = TileCoordIter.GetCoord();
			if (HasTileAtCoord(TileCoord))
			{
				OutTileCoords.Add(TileCoord);
			}

			++TileCoordIter;
		}
	}
}

void FSparseVoxelOctree::GetTilesInBounds(const FBox& QueryBounds, TFunctionRef<bool(const FSvoTile& CurTile)> TileFunc) const
{
	if (!IsValid())
		return;

	FIntVector MinTileCoord, MaxTileCoord;
	FSvoUtils::GetCoordsForBounds(Config.GetSeedLocation(), QueryBounds, Config.GetTileResolution(), MinTileCoord, MaxTileCoord);

	FCoordIterator TileCoordIter(MinTileCoord, MaxTileCoord);
	while (TileCoordIter)
	{
		const FIntVector& TileCoord = TileCoordIter.GetCoord();

		if (const FSvoTile* CurTile = GetTileAtCoord(TileCoord))
		{
			if (!TileFunc(*CurTile))
			{
				return;
			}
		}

		++TileCoordIter;
	}
}

uint32 FSparseVoxelOctree::GetMemUsed() const
{
	uint32 MemUsed = sizeof(this);

	MemUsed += Tiles.GetAllocatedSize();
	for (auto& Tile : Tiles)
	{
		MemUsed += Tile.Value.GetMemUsed();
	}

	return MemUsed;
}

void FSparseVoxelOctree::VerifyNodeData(bool VerifyExternalLinks) const
{
	ensureAlways(Tiles.Num() <= MaxTiles);

	// Verify each tile
	for (const FSvoTile& Tile : GetTiles())
	{
		if (VerifyExternalLinks)
		{
			Tile.Verify(this);
		}
		else
		{
			Tile.Verify();
		}

		const FIntVector& Coord = Tile.GetCoord();
		ensure(FSvoTile::CalcTileID(Coord) == Tile.GetID());
	}
}
