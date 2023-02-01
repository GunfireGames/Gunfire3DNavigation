#pragma once

#include "SparseVoxelOctree.h"

template<typename TOctree, typename TTile, typename TNode>
FSvoNeighborIteratorBase<TOctree, TTile, TNode>::FSvoNeighborIteratorBase(TOctree& InOctree, FSvoNodeLink InNodeLink, bool bInSkipInvalid /* = true */)
	: Octree(InOctree)
	, Node(nullptr)
	, NodeLink(InNodeLink)
	, Neighbor(ESvoNeighbor::Front)
	, NeighborLink(SVO_INVALID_NODELINK)
	, NeighborNode(nullptr)
	, bSkipInvalid(bInSkipInvalid)
{
	Node = Octree.GetNodeFromLink(NodeLink);
	if (ensure(Node != nullptr))
	{
		UpdateNeighbor();
	}
	else
	{
		ForceComplete();
	}
}

template<typename TOctree, typename TTile, typename TNode>
FSvoNeighborIteratorBase<TOctree, TTile, TNode>::FSvoNeighborIteratorBase(TOctree& InOctree, TNode& InNode, bool bInSkipInvalid /* = true */)
	: Octree(InOctree)
	, Node(&InNode)
	, NodeLink(SVO_INVALID_NODELINK)
	, Neighbor(ESvoNeighbor::Front)
	, NeighborLink(SVO_INVALID_NODELINK)
	, NeighborNode(nullptr)
	, bSkipInvalid(bInSkipInvalid)
{
	NodeLink = InNode.GetSelfLink();
	if (ensure(NodeLink.IsValid()))
	{
		UpdateNeighbor();
	}
	else
	{
		ForceComplete();
	}
}

template<typename TOctree, typename TTile, typename TNode>
FSvoNeighborIteratorBase<TOctree, TTile, TNode>::FSvoNeighborIteratorBase(TOctree& InOctree, TTile& Tile, bool bInSkipInvalid /* = true */)
	: Octree(InOctree)
	, Node(nullptr)
	, NodeLink(SVO_INVALID_NODELINK)
	, Neighbor(ESvoNeighbor::Front)
	, NeighborLink(SVO_INVALID_NODELINK)
	, NeighborNode(nullptr)
	, bSkipInvalid(bInSkipInvalid)
{
	Node = &Tile.GetNodeInfo();
	if (ensure(NodeLink.IsValid()))
	{
		UpdateNeighbor();
	}
	else
	{
		ForceComplete();
	}
}

template<typename TOctree, typename TTile, typename TNode>
void FSvoNeighborIteratorBase<TOctree, TTile, TNode>::UpdateNeighbor()
{
	NeighborLink = SVO_INVALID_NODELINK;
	NeighborNode = nullptr;

	// Don't process if we've already reached the final neighbor
	if (IsComplete())
	{
		return;
	}

	// If the neighbor voxel is in our leaf node we just calculate the correct index
	if (NodeLink.IsVoxelNode() && FSvoUtils::IsVoxelCoordValid(GetNeighborVoxelCoord()))
	{
		NeighborLink = NodeLink;
		NeighborLink.VoxelIdx = GetNeighborVoxelIndex();
		NeighborNode = Node;
	}
	else
	{
		// We've hit the edge of the node so add the owning leaf node's neighbor in the
		// same direction
		NeighborLink = Node->GetNeighborLink(Octree, Neighbor);
		NeighborNode = Octree.GetNodeFromLink(NeighborLink);

		// If we're moving from a voxel one leaf node to another voxel on a another leaf
		// node, return the exact voxel neighbor if the leaf is only partially blocked.
		// Otherwise, just return the leaf node, since technically that's the highest
		// resolution.
		if (NodeLink.IsVoxelNode())
		{
			if (NeighborNode != nullptr && NeighborLink.IsLeafNode())
			{
				const bool bNeighborIsVoxel = (NeighborNode->GetNodeState() == ENodeState::PartiallyBlocked);
				if (bNeighborIsVoxel)
				{
					NeighborLink.VoxelIdx = FSvoUtils::GetNeighborVoxel(NodeLink.VoxelIdx, Neighbor);
				}
			}
		}
	}

	// If this neighbor is invalid, move to the next one.
	if (bSkipInvalid && NeighborNode == nullptr)
	{
		this->operator ++();
	}
}
