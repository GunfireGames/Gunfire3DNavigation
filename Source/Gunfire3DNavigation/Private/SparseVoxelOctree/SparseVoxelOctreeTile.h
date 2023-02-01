// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "SparseVoxelOctreeNode.h"
#include "IteratorHelpers.h"

//
// A tile is our top level node. The navigable space is partitioned into a 3D grid of
// tiles.
//
class FSvoTile
{
	typedef FSvoTile ThisClass;
	friend class FEditableSvo;
	friend class FNavSvoTileGenerator;

public:
	FSvoTile() {}
	FSvoTile(uint32 TileID, uint8 TileLayerIdx, const FIntVector& TileCoord);

	// Creates all nodes needed for this tile
	void AllocateNodes(uint32 NumNodes, uint8 NumLayers);

	// Releases all memory held by the nodes of this tile
	void ReleaseMemory();

	// This will remove all nodes that aren't in use by the layers.  This should only be
	// called once a tile is considered read only as any modification will likely stomp
	// memory.
	void TrimExcessNodes();

	// Serialize the tile to an archive
	void Serialize(FArchive& Ar);

	friend FArchive& operator<<(FArchive& Ar, FSvoTile& Tile)
	{
		Tile.Serialize(Ar);
		return Ar;
	}

	// Copy the data from another tile
	void Copy(const FSvoTile& SourceTile);

	// Assume the data of another tile
	void Assume(FSvoTile& SourceTile);

	///> Accessors

	uint32 GetID() const { return NodeInfo.GetSelfLink().TileID; }
	FSvoNodeLink GetSelfLink() const { return NodeInfo.GetSelfLink(); }
	const FSvoNode& GetNodeInfo() const { return NodeInfo; }
	FSvoNode& GetNodeInfo() { return NodeInfo; }

	// Returns the coordinate relative to the seed location used to generate this tile.
	const FIntVector& GetCoord() const { return Coord; }

	// Determines whether this tile has any internal node memory
	bool HasNodesAllocated() const { return NodePool.Num() > 0; }

	// Returns the number of used nodes within the full allocation of nodes
	uint32 GetNumNodes(uint8 LayerIdx) const;

	// Returns the total number of allocated nodes.  This is not the same as the number of
	// used nodes.  See 'GetNumNodes'
	uint32 GetMaxNodes(uint8 LayerIdx) const;

	// Returns a node at a given index
	FORCEINLINE const FSvoNode* GetNode(uint8 LayerIdx, uint32 NodeIdx, bool bActiveOnly = true) const;
	FORCEINLINE FSvoNode* GetNode(uint8 LayerIdx, uint32 NodeIdx, bool bActiveOnly = true);

	// Returns the index of a node for a given layer.  This is not an index into max nodes
	// but rather a relative index into the pool given to the specified layer on init.
	int32 GetNodeIndex(const FSvoNode& Node) const;

	// Returns all the active nodes in an iterator you can use with a ranged for
	//  Ex: for (FSvoNode& CurNode : GetNodesForLayer(SVO_LEAF_LAYER))
	FConditionalRangeIterator<const FSvoNode> GetNodesForLayer(uint8 LayerIdx) const;
	FConditionalRangeIterator<FSvoNode> GetNodesForLayer(uint8 LayerIdx);

	// Returns the link of the specified neighbor for this tile.
	FSvoNodeLink GetNeighborLink(ESvoNeighbor Neighbor) const;

	//> Utility

	static uint32 CalcTileID(const FIntVector& TileCoord) { return GetTypeHash(TileCoord); }

	// Counts the memory used by this tile
	uint32 GetMemUsed() const;

	// Resets all data for this tile, making it invalid
	void Reset();

	// Verifies that the data for the tile is valid
	void Verify(const FSparseVoxelOctree* Octree = nullptr) const;

	// Add the node to the active list
	FSvoNode* EnsureNodeExists(uint8 LayerIdx, uint32 NodeIdx, bool& bCreated);

protected:
	void VerifyChildren(const FSvoNode& NodeInfo, const class FSparseVoxelOctree* Octree) const;
	void VerifyNeighbor(const FSvoNode* Node, ESvoNeighbor Neighbor, const class FSparseVoxelOctree* Octree) const;

protected:
	struct FSvoLayer
	{
		uint32 StartNode = 0;
		uint32 NumNodes = 0;
		uint32 MaxNodes = 0;
	};

	// Basic node info about this tile
	FSvoNode NodeInfo;

	// World location of the center of the tile
	FIntVector Coord = FIntVector::ZeroValue;

	// List of nodes within the tile
	TArray<FSvoNode> NodePool;

	// Layer information within the tile
	TArray<FSvoLayer> Layers;
};

//////////////////////////////////////////////////////////////////////////////////////////

const FSvoNode* FSvoTile::GetNode(uint8 LayerIdx, uint32 NodeIdx, bool bActiveOnly) const
{
	if (Layers.IsValidIndex(LayerIdx))
	{
		const FSvoLayer& Layer = Layers[LayerIdx];

		if (NodeIdx < Layer.MaxNodes)
		{
			const FSvoNode& Node = NodePool[Layer.StartNode + NodeIdx];
			if (!bActiveOnly || Node.IsActive())
			{
				return &Node;
			}
		}
	}
	else if (LayerIdx == NodeInfo.GetSelfLink().LayerIdx)
	{
		// If the tile layer was requested, return the node representing this tile.
		ensure(NodeIdx == 0);
		return &NodeInfo;
	}

	return nullptr;
}

FSvoNode* FSvoTile::GetNode(uint8 LayerIdx, uint32 NodeIdx, bool bActiveOnly)
{
	return MUTABLE_ACCESSOR(FSvoNode*, GetNode(LayerIdx, NodeIdx, bActiveOnly));
}
