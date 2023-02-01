// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "SparseVoxelOctreeNode.h"

#include "SparseVoxelOctree.h"

FSvoNodeLink FSvoNode::GetNeighborLink(const FSvoTile& ParentTile, ESvoNeighbor Neighbor) const
{
	ensure(Neighbor != ESvoNeighbor::Self && (uint8)Neighbor < UE_ARRAY_COUNT(NeighborLinks));
	FSvoNodeLinkBase NeighborLinkBase = NeighborLinks[(uint8)Neighbor];

	///> Resolve the Tile ID

	uint32 TileID = SVO_INVALID_ID;

	if (NeighborLinkBase.IsValid())
	{
		// When the neighbor link was set, its user data was filled in with
		// ESvoNeighbor::Self if it was a part of the same tile. Otherwise, the direction
		// of the neighbor was stored.
		if (NeighborLinkBase.UserData == (uint8)ESvoNeighbor::Self)
		{
			// The neighbor is a part of the same tile
			TileID = ParentTile.GetID();
		}
		else
		{
			// The neighbor id is stored on the link and should always match the requested
			// neighbor direction.
			ensureAlways((uint8)Neighbor == NeighborLinkBase.UserData);

			// In this case, the neighbor is on an adjacent tile
			FIntVector NeighborTileCoord = ParentTile.GetCoord() + FSvoUtils::GetNeighborDirection(Neighbor);
			TileID = FSvoTile::CalcTileID(NeighborTileCoord);
		}

		// Clear the user data as this is only meant to be used internally and could
		// confuse other operations which rely on this field.
		NeighborLinkBase.UserData = 0;
	}
	else
	{
		// Invalidate the user data if the neighbor is invalid
		NeighborLinkBase.UserData = 0xF;
	}

	return FSvoNodeLink(TileID, NeighborLinkBase);
}

FSvoNodeLink FSvoNode::GetNeighborLink(const FSparseVoxelOctree& Octree, ESvoNeighbor Neighbor) const
{
	const FSvoTile* ParentTile = Octree.GetTile(GetSelfLink().TileID);
	return GetNeighborLink(*ParentTile, Neighbor);
}