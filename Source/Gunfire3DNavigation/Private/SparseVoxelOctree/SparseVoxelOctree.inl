// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "SparseVoxelOctreeUtils.h"

DECLARE_CYCLE_STAT(TEXT("QueryNodes (FSparseVoxelOctree)"), STAT_FSparseVoxelOctree_QueryNodes, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("GetAllActiveTileCoords (FSparseVoxelOctree)"), STAT_FSparseVoxelOctree_GetAllActiveTileCoords, STATGROUP_Gunfire3DNavigation);

const FSvoNode* FSparseVoxelOctree::GetNodeFromLink(const FSvoNodeLink& Link) const
{
	const FSvoNode* FoundNode = nullptr;

	if (Link.IsValid())
	{
		if (const FSvoTile* Tile = GetTile(Link.TileID))
		{
			if (Link.LayerIdx == Config.GetTileLayerIndex())
			{
				FoundNode = &Tile->GetNodeInfo();
			}
			else
			{
				FoundNode = Tile->GetNode(Link.LayerIdx, Link.NodeIdx, true);
			}
		}
	}

	// Make sure the link stored on the node and the provided link are the same.
	// 
	// NOTE: We intentionally ignore the voxel data here as that isn't relevant during
	// node lookup.
	if (FoundNode != nullptr)
	{
		FSvoNodeLink SelfLink = FoundNode->GetSelfLink();
		ensureAlways((SelfLink.GetID() | SVO_NODE_VOXEL_MASK) == (Link.GetID() | SVO_NODE_VOXEL_MASK));
	}

	return FoundNode;
}

FSvoNode* FSparseVoxelOctree::GetNodeFromLink(const FSvoNodeLink& Link)
{
	return MUTABLE_ACCESSOR(FSvoNode*, GetNodeFromLink(Link));
}

FSvoTile* FSparseVoxelOctree::GetTileAtCoord(const FIntVector& Coord)
{
	return MUTABLE_ACCESSOR(FSvoTile*, GetTileAtCoord(Coord));
}

FSvoTile* FSparseVoxelOctree::GetTileAtLocation(const FVector& Location)
{
	return MUTABLE_ACCESSOR(FSvoTile*, GetTileAtLocation(Location));
}

FSvoTile* FSparseVoxelOctree::GetTileForLink(const FSvoNodeLink& NodeLink)
{
	return MUTABLE_ACCESSOR(FSvoTile*, GetTileForLink(NodeLink));
}
