// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "SparseVoxelOctreeTile.h"

#include "Gunfire3DNavigationCustomVersion.h"
#include "SparseVoxelOctree.h"
#include "SparseVoxelOctreeUtils.h"

// Profiling stats
DECLARE_CYCLE_STAT(TEXT("TrimExcessNodes (FSvoLayer)"), STAT_FSvoLayer_TrimExcessNodes, STATGROUP_Gunfire3DNavigation);

FSvoTile::FSvoTile(uint32 TileID, uint8 TileLayerIdx, const FIntVector& TileCoord)
	: Coord(TileCoord)
{
	NodeInfo.Init(FSvoNodeLink(TileID, TileLayerIdx, 0), true);
}

void FSvoTile::AllocateNodes(uint32 NumNodes, uint8 NumLayers)
{
	NodePool.Reset(NumNodes);
	Layers.Reset(NumLayers);

	if (NumNodes > 0)
	{
		// Preallocate the maximum number of nodes for the tile
		NodePool.AddDefaulted(NumNodes);

		// Create layers
		Layers.AddDefaulted(NumLayers);

		int32 NodeStartIdx = 0;
		int32 TopLayerIdx = (Layers.Num() - 1);

		// Assign each layer its portion of the node pool
		for (uint8 LayerIdx = 0; LayerIdx < Layers.Num(); LayerIdx++)
		{
			FSvoLayer& Layer = Layers[(TopLayerIdx - LayerIdx)];

			Layer.StartNode = NodeStartIdx;
			Layer.MaxNodes = FMath::Pow(8.0f, LayerIdx + 1.0f);

			NodeStartIdx += Layer.MaxNodes;
		}
	}
}

void FSvoTile::ReleaseMemory()
{
	Layers.Empty();
	NodePool.Empty();
}

void FSvoTile::TrimExcessNodes()
{
	// The Morton algorithm we use for fast lookups depends on the nodes being contiguous,
	// but any unused nodes at the end of a layer should never be referenced so we can
	// safely trim them off.
	SCOPE_CYCLE_COUNTER(STAT_FSvoLayer_TrimExcessNodes);

	// If we're fully blocked or fully open we don't need any nodes, so free them all
	if (NodeInfo.GetNodeState() != ENodeState::PartiallyBlocked)
	{
		ReleaseMemory();
		return;
	}

	for (int32 i = 0; i < Layers.Num(); ++i)
	{
		FSvoLayer& CurLayer = Layers[i];

		const uint32 LayerStart = CurLayer.StartNode;
		const uint32 LayerEnd = CurLayer.StartNode + CurLayer.MaxNodes;

		// Walk the layer nodes backwards until we hit a valid node.
		uint32 NumNodesToRemove = 0;
		for (uint32 NodeIdx = (LayerEnd - 1); NodeIdx >= LayerStart; --NodeIdx)
		{
			if (!NodePool[NodeIdx].IsActive())
			{
				++NumNodesToRemove;
			}
			else
			{
				break;
			}
		}

		// We're either using all our nodes, of the last one in the layer is used, so
		// there's nothing we can trim.
		if (NumNodesToRemove == 0)
		{
			continue;
		}

		// Remove the unused nodes. Don't free the memory yet though, since we may trim
		// multiple layers.
		NodePool.RemoveAt(LayerEnd - NumNodesToRemove, NumNodesToRemove, false);

		CurLayer.MaxNodes -= NumNodesToRemove;

		// We should only be trimming off unused nodes, so NumNodes shouldn't need an
		// update, but verify that.
		ensureAlways(CurLayer.NumNodes <= CurLayer.MaxNodes);

		// If we removed nodes, update layers below us to reflect their new start
		for (int32 j = i - 1; j >= 0; --j)
		{
			FSvoLayer& OtherLayer = Layers[j];
			OtherLayer.StartNode -= NumNodesToRemove;
		}
	}

	// Now that we're done removing nodes, free any unused memory
	NodePool.Shrink();
}

void FSvoTile::Serialize(FArchive& Ar)
{
	// Get the custom version from the archive
	int32 Version = Ar.CustomVer(FGunfire3DNavigationCustomVersion::GUID);

	Ar << NodeInfo;

	if (Ar.IsLoading())
	{
		ReleaseMemory();
	}

	if (Version < FGunfire3DNavigationCustomVersion::NodeLinkBaseAdded)
	{
		FVector Location;
		Ar << Location;

		if (FSvoConfig* Config = FGunfire3DNavigationCustomVersion::SvoConfig)
		{
			Coord = Config->LocationToCoord(Location, Config->GetTileResolution());
		}
	}
	else
	{
		Ar << Coord;
	}

	Ar << NodePool;

	int32 NumLayers = Layers.Num();
	Ar << NumLayers;

	if (Ar.IsLoading())
	{
		Layers.SetNum(NumLayers);
	}

	for (FSvoLayer& Layer : Layers)
	{
		Ar << Layer.StartNode;
		Ar << Layer.NumNodes;
		Ar << Layer.MaxNodes;
	}

	if (Version < FGunfire3DNavigationCustomVersion::NodePropsChanged)
	{
		NodeInfo.UpdateOldNode();

		for (FSvoNode& Node : NodePool)
		{
			if (Node.IsLeafNode())
			{
				break;
			}

			Node.UpdateOldNode();
		}
	}

#if !UE_BUILD_SHIPPING && SVO_VERIFY_NODES
	if (Ar.IsLoading())
	{
		Verify();
	}
#endif
}

uint32 FSvoTile::GetMaxNodes(uint8 LayerIdx) const
{
	if (Layers.IsValidIndex(LayerIdx))
	{
		return Layers[LayerIdx].MaxNodes;
	}

	return 0;
}

uint32 FSvoTile::GetNumNodes(uint8 LayerIdx) const
{
	if (Layers.IsValidIndex(LayerIdx))
	{
		return Layers[LayerIdx].NumNodes;
	}

	return 0;
}

int32 FSvoTile::GetNodeIndex(const FSvoNode& Node) const
{
	if (NodePool.Num() > 0)
	{
		check(&Node >= &NodePool[0] && &Node <= &NodePool[NodePool.Num() - 1]);

		const uint8 NodeLayer = Node.GetSelfLink().LayerIdx;
		const int32 PoolIndex = &Node - &NodePool[0];
		return PoolIndex - Layers[NodeLayer].StartNode;
		
	}

	return INDEX_NONE;
}

FConditionalRangeIterator<const FSvoNode> FSvoTile::GetNodesForLayer(uint8 LayerIdx) const
{
	if (Layers.IsValidIndex(LayerIdx))
	{
		const FSvoLayer& Layer = Layers[LayerIdx];

		if (Layer.NumNodes > 0)
		{
			const FSvoNode* StartNode = &NodePool[Layer.StartNode];
			return FConditionalRangeIterator<const FSvoNode>(StartNode, StartNode + Layer.MaxNodes);
		}
	}

	return FConditionalRangeIterator<const FSvoNode>(nullptr, nullptr);
}

FConditionalRangeIterator<FSvoNode> FSvoTile::GetNodesForLayer(uint8 LayerIdx)
{
	if (Layers.IsValidIndex(LayerIdx))
	{
		const FSvoLayer& Layer = Layers[LayerIdx];

		if (Layer.NumNodes > 0)
		{
			FSvoNode* StartNode = &NodePool[Layer.StartNode];
			return FConditionalRangeIterator<FSvoNode>(StartNode, StartNode + Layer.MaxNodes);
		}
	}

	return FConditionalRangeIterator<FSvoNode>(nullptr, nullptr);
}

FSvoNodeLink FSvoTile::GetNeighborLink(ESvoNeighbor Neighbor) const
{
	return GetNodeInfo().GetNeighborLink(*this, Neighbor);
}

FSvoNode* FSvoTile::EnsureNodeExists(uint8 LayerIdx, uint32 NodeIdx, bool& bCreated)
{
	bCreated = false;

	if (FSvoNode* Node = GetNode(LayerIdx, NodeIdx, false))
	{
		if (!Node->IsActive())
		{
			Node->Init(FSvoNodeLink(NodeInfo.GetSelfLink().TileID, LayerIdx, NodeIdx), false);

			++Layers[LayerIdx].NumNodes;

			bCreated = true;
		}

		return Node;
	}

	return nullptr;
}

void FSvoTile::Copy(const FSvoTile& SourceTile)
{
#if !UE_BUILD_SHIPPING && SVO_VERIFY_NODES
	SourceTile.Verify();
#endif

	// Duplicate basic node information
	NodeInfo = SourceTile.NodeInfo;

	Coord = SourceTile.Coord;

	// Duplicate the node pool
	NodePool = SourceTile.NodePool;

	// Create layers to point to the new node pool
	Layers = SourceTile.Layers;

#if !UE_BUILD_SHIPPING && SVO_VERIFY_NODES
	Verify();
#endif
}

void FSvoTile::Assume(FSvoTile& SourceTile)
{
#if !UE_BUILD_SHIPPING && SVO_VERIFY_NODES
	SourceTile.Verify();
#endif

	// Release any existing memory for this tile
	ReleaseMemory();

	// Duplicate basic node information
	NodeInfo = SourceTile.NodeInfo;

	Coord = SourceTile.Coord;

	// Take over the memory for the node pool and layers
	NodePool = MoveTemp(SourceTile.NodePool);
	Layers = MoveTemp(SourceTile.Layers);

#if !UE_BUILD_SHIPPING && SVO_VERIFY_NODES
	Verify();
#endif

	if (NodePool.Num() == 0)
	{
		NodeInfo.SetNodeState(ENodeState::Open);
	}
}

uint32 FSvoTile::GetMemUsed() const
{
	uint32 MemUsed = 0;
	MemUsed += NodePool.GetAllocatedSize();
	MemUsed += Layers.GetAllocatedSize();

	return MemUsed;
}

void FSvoTile::Reset()
{
	NodeInfo.Reset();

	// Release any existing memory for this tile
	ReleaseMemory();
}

void FSvoTile::Verify(const FSparseVoxelOctree* Octree /* = nullptr */) const
{
	// All tiles being verified should be active
	ensureAlways(NodeInfo.IsActive());

	// A tile should never have a parent
	ensureAlways(!NodeInfo.GetParentLink().IsValid());

	// Make sure the tile's link to itself is valid
	FSvoNodeLink TileLink = NodeInfo.GetSelfLink();
	ensureAlways(TileLink.IsValid());

	// If we have child nodes, verify them recursively
	if (NodeInfo.HasChildren())
	{
		VerifyChildren(NodeInfo, Octree);
	}

	// Verify our layers
	for (int32 i = Layers.Num() - 1; i >= 0; --i)
	{
		const FSvoLayer& CurLayer = Layers[i];

		// Make sure that our range of nodes is in the pool
		ensure(CurLayer.StartNode + CurLayer.MaxNodes <= (uint32)NodePool.Num());

		int32 NumActive = 0;

		// Test that our iterator is correctly returning just active nodes, and count them
		for (const FSvoNode& CurNode : GetNodesForLayer(i))
		{
			ensureAlways(CurNode.IsActive());
			++NumActive;
		}

		// Ensure that the number of active nodes found matches what we expect
		ensureAlways(NumActive == CurLayer.NumNodes);

		// Make sure that the next layer starts after the end of our nodes
		if (i > 0)
		{
			const FSvoLayer& NextLayer = Layers[i - 1];
			ensureAlways(CurLayer.StartNode + CurLayer.MaxNodes == NextLayer.StartNode);
		}
	}
}

void FSvoTile::VerifyChildren(const FSvoNode& CurNodeInfo, const FSparseVoxelOctree* Octree) const
{
	FSvoNodeLink SelfLink = CurNodeInfo.GetSelfLink();

	for (int32 i = 0; i < 8; ++i)
	{
		FSvoNodeLink ChildLink = CurNodeInfo.GetChildLink(i);

		if (ChildLink.IsValid())
		{
			const FSvoNode* ChildNode = GetNode(ChildLink.LayerIdx, ChildLink.NodeIdx, true);

			if (ensure(ChildNode) && !ChildNode->IsLeafNode())
			{
				FSvoNodeLink ChildSelfLink = ChildNode->GetSelfLink();
				ensure(ChildSelfLink == ChildLink);

				// Validate that all the neighbor links are correct
				for (ESvoNeighbor Neighbor : FSvoUtils::GetAllNeighbors())
				{
					VerifyNeighbor(ChildNode, Neighbor, Octree);
				}

				if (ChildNode->HasChildren())
				{
					VerifyChildren(*ChildNode, Octree);
				}
			}
		}
	}
}

void FSvoTile::VerifyNeighbor(const FSvoNode* Node, ESvoNeighbor Neighbor, const FSparseVoxelOctree* Octree) const
{
	FSvoNodeLink NeighborLink = Node->GetNeighborLink(*this, Neighbor);
	if (!NeighborLink.IsValid())
	{
		return;
	}

	const FSvoTile* NeighborTile = nullptr;

	// Resolved the tile to which the neighbor belongs
	if (NeighborLink.TileID == Node->GetSelfLink().TileID)
	{
		NeighborTile = this;
	}
	else if (Octree != nullptr)
	{
		// If a valid octree has been provided then continue checking nodes on external
		// tiles.
		NeighborTile = Octree->GetTileForLink(NeighborLink);
	}

	if (NeighborTile != nullptr)
	{
		const FSvoNode* NeighborNode = NeighborTile->GetNode(NeighborLink.LayerIdx, NeighborLink.NodeIdx, true);

		// We have a neighbor, get the link back to us
		ESvoNeighbor OppositeNeighbor = FSvoUtils::GetOppositeNeighbor(Neighbor);
		FSvoNodeLink OppositeNeighborLink = NeighborNode->GetNeighborLink(*NeighborTile, OppositeNeighbor);
		ensureAlways(OppositeNeighborLink.IsValid());

		if (OppositeNeighborLink != Node->GetSelfLink())
		{
			// The link point back to the original node from the neighbor node should
			// always be the same tile.
			ensureAlways(OppositeNeighborLink.TileID == Node->GetSelfLink().TileID);

			bool IsParentLink = false;

			FSvoNodeLink ParentLink = Node->GetParentLink();
			while (ParentLink.IsValid())
			{
				if (OppositeNeighborLink == ParentLink)
				{
					IsParentLink = true;
					break;
				}
				
				if (const FSvoNode* ParentNode = GetNode(ParentLink.LayerIdx, ParentLink.NodeIdx, true))
				{
					ParentLink = ParentNode->GetParentLink();
				}
				else
				{
					// In this case the parent node was unable to be resolved.
					//
					// IMPORTANT: This should be investigated and fixed asap.
					ensureAlways(false);
					break;
				}
			}

			ensureAlways(IsParentLink);
		}
	}
}
