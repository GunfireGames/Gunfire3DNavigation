// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "NavSvoQuery.h"

#include "Gunfire3DNavigationUtils.h"
#include "SparseVoxelOctree/SparseVoxelOctree.h"

#define SVO_VOXEL_NEIGHBOR_FLAG 0x40

DECLARE_CYCLE_STAT(TEXT("SearchNodes"), STAT_SearchNodes, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("OpenNeighbor"), STAT_OpenNeighbor, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("OpenNeighborChildren"), STAT_OpenNeighborChildren, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("OpenVoxelsOnNeighborNode"), STAT_OpenVoxelsOnNeighborNode, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("OpenNeighbors"), STAT_OpenNeighbors, STATGROUP_Gunfire3DNavigation);

//////////////////////////////////////////////////////////////////////////
// NavSvoQuery
//////////////////////////////////////////////////////////////////////////

FNavSvoQuery::FNavSvoQuery(const FSparseVoxelOctree& InOctree, int32 MaxSearchNodes)
	: Octree(InOctree)
	, NodePool(MaxSearchNodes, FMath::RoundUpToPowerOfTwo(MaxSearchNodes / 4))
	, OpenList(MaxSearchNodes)
	, NodeVisitationLimit(MaxSearchNodes * 4u)
{}

FNavSvoNode* FNavSvoQuery::TryAddSearchNode(FSvoNodeLink NodeLink)
{
	FNavSvoNode* NewNode = NodePool.GetNode(NodeLink);

	// If unable to get this node then the pool has been exhausted.
	if (NewNode == nullptr)
	{
		Results->Status |= (uint8)EGunfire3DNavQueryFlags::OutOfNodes;
	}

	return NewNode;
}

void FNavSvoQuery::ResetForNewQuery()
{
	StartNodeLink = SVO_INVALID_NODELINK;
	BestSearchNode = nullptr;
	Filter = nullptr;
	Results = nullptr;
}

bool FNavSvoQuery::SearchNodes(FSvoNodeLink InStartNodeLink, const FGunfire3DNavQueryFilter& InFilter, FGunfire3DNavQueryResults& InOutResults)
{
	SCOPE_CYCLE_COUNTER(STAT_SearchNodes);

	StartNodeLink = InStartNodeLink;
	BestSearchNode = nullptr;
	Filter = &InFilter;
	Results = &InOutResults;

	// Everything is pre-allocated prior to the query to just assign the mem allocation value now.
	Results->MemUsed = GetMemUsed();

	// Bail if the node pool or open list failed to instantiate
	if (NodePool.GetMaxNodes() == 0)
	{
		Results->Status = (uint8)(EGunfire3DNavQueryFlags::Failure | EGunfire3DNavQueryFlags::OutOfMemory);
		return false;
	}

	// Don't continue if the octree is empty
	if (!Octree.IsValid())
	{
		Results->Status = (uint8)(EGunfire3DNavQueryFlags::Failure | EGunfire3DNavQueryFlags::InvalidParam);
		return false;
	}

	if (!StartNodeLink.IsValid())
	{
		Results->Status = (uint8)(EGunfire3DNavQueryFlags::Failure | EGunfire3DNavQueryFlags::InvalidParam);
		return false;
	}

	// Reset pool and open list
	NodePool.Clear();
	OpenList.Clear();

	// Create starting node to seed the process
	FNavSvoNode* StartSearchNode = TryAddSearchNode(StartNodeLink);
	if (StartSearchNode == nullptr)
	{
		Results->Status |= (uint8)EGunfire3DNavQueryFlags::Failure;
		return false;
	}

	// Mark the start node as open for posterity
	StartSearchNode->Flags = NAVSVONODE_OPEN;

	// Seed the initial heuristic
	StartSearchNode->Heuristic = MAX_flt;

	// Seed the best search node with the start search node
	BestSearchNode = StartSearchNode;

	// Add the starting search node to the open list and begin iterating
	OpenList.Push(StartSearchNode);
	while (!OpenList.IsEmpty())
	{
		FNavSvoNode& SearchNode = *OpenList.Pop();
		SearchNode.Flags &= ~NAVSVONODE_OPEN;
		SearchNode.Flags |= NAVSVONODE_CLOSED;

		const FSvoNodeLink& NodeLink = SearchNode.NodeLink;
		const FSvoNode& Node = *(Octree.GetNodeFromLink(NodeLink));

		// Notify derivative that a node is being visited and optionally cancel the
		// search.
		if (!OnNodeVisited(SearchNode, Node))
		{
			break;
		}

		// Notify caller that a node is being visited and optionally cancel the
		// search.
		if (Filter->OnNodeVisited && !Filter->OnNodeVisited(SearchNode.NodeLink.GetID()))
		{
			break;
		}

		OpenNeighbors(SearchNode, Node);

		// Failsafe for cycles in navigation graph resulting in infinite loop
		if (++Results->NumNodesVisited == NodeVisitationLimit)
		{
			break;
		}
	}

	Results->Status |= (uint8)EGunfire3DNavQueryFlags::Success;
	return true;
}

bool FNavSvoQuery::OpenNeighbor(FNavSvoNode& FromSearchNode, const FSvoNode& FromNode, ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink, const FSvoNode& NeighborNode)
{
	SCOPE_CYCLE_COUNTER(STAT_OpenNeighbor);

	// Do not process invalid neighbors
	if (!NeighborLink.IsValid())
	{
		return false;
	}

	// Do not backtrack to self
	if (FromSearchNode.NodeLink == NeighborLink)
	{
		return false;
	}

	// Do not backtrack to the parent node that we're coming from
	FNavSvoNode* ParentSearchNode = NodePool.GetNodeAtIndex(FromSearchNode.ParentIdx);
	if (ParentSearchNode != nullptr && ParentSearchNode->NodeLink == NeighborLink)
	{
		return false;
	}

	FNavSvoNode* NeighborSearchNode = NodePool.FindNode(NeighborLink);
	const bool bIsNeighborAlreadyOpen = (NeighborSearchNode != nullptr && (NeighborSearchNode->Flags & NAVSVONODE_OPEN) != 0);
	const bool bIsNeighborAlreadyClosed = (NeighborSearchNode != nullptr && (NeighborSearchNode->Flags & NAVSVONODE_CLOSED) != 0);

	// Don't process already closed nodes
	//
	// NOTE: Nodes are closed once they are visited on the open-list.
	if (bIsNeighborAlreadyClosed)
	{
		return false;
	}

	// Find the portal location between the two nodes
	FVector NeighborPortalLocation;
	const bool bPortalLocationValid = GetPortalLocation(FromSearchNode.NodeLink, NeighborLink, Neighbor, NeighborPortalLocation);
	if (!bPortalLocationValid)
	{
		return false;
	}

	// Calculate the linear distance to this node
	const float TraversalDeltaToNeighborSqrd = FVector::DistSquared(FromSearchNode.PortalLocation, NeighborPortalLocation);
	const float NeighborTotalTravelDistSqrd = FromSearchNode.TravelDistSqrd + TraversalDeltaToNeighborSqrd;

	// Calculate the cost of this node
	const float NeighborHeuristic = GetHeuristic(NeighborLink);
	const float NeighborTraversalCost = FromSearchNode.GCost + GetTraversalCost(FromSearchNode.NodeLink, NeighborLink, NeighborPortalLocation);
	const float NeighborTotalCost = NeighborTraversalCost + NeighborHeuristic;
	bool bIsNeighborCheaper = true;

	// If the neighbor node is currently on the open-list, we need to decide whether
	// to keep the existing node cost or update it with this new path.
	if (bIsNeighborAlreadyOpen)
	{
		const float ExistingNeighborCost = NeighborSearchNode->FCost;
		if (ExistingNeighborCost == NeighborTotalCost)
		{
			ENavSvoQueryTieBreaker TieBreaker = GetCostTieBreaker();
			switch (TieBreaker)
			{
			case ENavSvoQueryTieBreaker::Nearest:
				bIsNeighborCheaper = (NeighborTraversalCost < NeighborSearchNode->GCost);
				break;

			case ENavSvoQueryTieBreaker::Furthest:
				bIsNeighborCheaper = (NeighborTraversalCost > NeighborSearchNode->GCost);
				break;
			}
		}
		else if (ExistingNeighborCost < NeighborTotalCost)
		{
			bIsNeighborCheaper = true;
		}
		else
		{
			bIsNeighborCheaper = false;
		}
	}

	// Don't open this node again if the existing cost is better
	if (!bIsNeighborCheaper)
	{
		return false;
	}

	// As a final check, allow derivative queries a chance to prevent a neighbor from
	// opening.
	const bool bCanOpenNeighbor = CanOpenNeighbor(Neighbor, NeighborLink, NeighborNode, NeighborTotalCost, NeighborTotalTravelDistSqrd);
	if (!bCanOpenNeighbor)
	{
		return false;
	}

	// If this is the first time visiting this node, add it to the pool so we can
	// track its cost.
	if (NeighborSearchNode == nullptr)
	{
		NeighborSearchNode = TryAddSearchNode(NeighborLink);

		// If unable to get this node then the pool has been exhausted.
		if (NeighborSearchNode == nullptr)
		{
			return false;
		}
	}

	// In this case, the node can be put on the frontier and explored

	NeighborSearchNode->ParentIdx = NodePool.GetNodeIndex(&FromSearchNode);
	NeighborSearchNode->FCost = NeighborTotalCost;
	NeighborSearchNode->GCost = NeighborTraversalCost;
	NeighborSearchNode->Heuristic = NeighborHeuristic;
	NeighborSearchNode->Neighbor = Neighbor;
	NeighborSearchNode->PortalLocation = NeighborPortalLocation;
	NeighborSearchNode->TravelDistSqrd = NeighborTotalTravelDistSqrd;
	NeighborSearchNode->Flags &= ~NAVSVONODE_CLOSED;

	// If this node is already in queue to be processed, update its position.
	// Otherwise, add it to the frontier for the first time.
	if (NeighborSearchNode->Flags & NAVSVONODE_OPEN)
	{
		OpenList.Modify(NeighborSearchNode);
		++Results->NumNodesReopened;
	}
	else
	{
		NeighborSearchNode->Flags |= NAVSVONODE_OPEN;
		OpenList.Push(NeighborSearchNode);
		++Results->NumNodesOpened;
	}

	// If this is the closest node to the goal, store it as the last best-known
	// node. This is how partial searches are determined.
	if (BestSearchNode == nullptr || NeighborSearchNode->Heuristic < BestSearchNode->Heuristic)
	{
		BestSearchNode = NeighborSearchNode;
	}

	// Update stats
	Results->NumNodesQueried = NodePool.GetNodeCount();

	// Allow the implementation to handle nodes being opened.
	OnOpenNeighbor(FromSearchNode, *NeighborSearchNode);

	return true;
}

bool FNavSvoQuery::OpenNeighbors(FNavSvoNode& FromSearchNode, const FSvoNode& FromNode)
{
	SCOPE_CYCLE_COUNTER(STAT_OpenNeighbors);

	bool bNeighborsOpened = false;

	// Iterate over each neighbor and open them if they are not blocked
	for (FSvoNeighborConstIterator NeighborIter(Octree, FromSearchNode.NodeLink); NeighborIter; ++NeighborIter)
	{
		const ESvoNeighbor Neighbor = NeighborIter.GetNeighbor();
		const FSvoNode& NeighborNode = NeighborIter.GetNeighborNodeChecked();
		const FSvoNodeLink NeighborLink = NeighborIter.GetNeighborLink();

		bNeighborsOpened |= OpenNeighborNode(FromSearchNode, FromNode, Neighbor, NeighborLink, NeighborNode);
	}

	return bNeighborsOpened;
}

bool FNavSvoQuery::OpenNeighborNode(FNavSvoNode& FromSearchNode, const FSvoNode& FromNode, ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink, const FSvoNode& NeighborNode)
{
	bool bNeighborOpened = false;

	// 1.) If the neighbor node is completely empty, then we can just open directly
	// 
	// 2.) If this is a leaf node that is partially blocked, we need to check each
	// voxel on the neighboring face and open the non-blocked nodes.
	// 
	// 3.) If this is a regular node that has children, we need to go deeper into the
	// node until we reach an open child without any children that are on the
	// neighboring face and open those node.
	if (NeighborLink.IsVoxelNode())
	{
		if (NeighborNode.IsVoxelBlocked(NeighborLink.VoxelIdx))
		{
			// Don't open blocked voxels
			return false;
		}
		else
		{
			// Open empty voxels
			bNeighborOpened = OpenNeighbor(FromSearchNode, FromNode, Neighbor, NeighborLink, NeighborNode);
		}
	}
	else if (NeighborNode.GetNodeState() == ENodeState::Blocked)
	{
		// Don't open blocked nodes
		return false;
	}
	else if (NeighborNode.GetNodeState() == ENodeState::Open)
	{
		// Open empty nodes
		bNeighborOpened = OpenNeighbor(FromSearchNode, FromNode, Neighbor, NeighborLink, NeighborNode);
	}
	else if (NeighborLink.IsLeafNode())
	{
		bNeighborOpened = OpenVoxelsOnNeighborNode(FromSearchNode, FromNode, Neighbor, NeighborLink, NeighborNode);
	}
	else // Parent node with children
	{
		bNeighborOpened = OpenChildrenOnNeighborNode(FromSearchNode, FromNode, Neighbor, NeighborNode);
	}

	return bNeighborOpened;
}

bool FNavSvoQuery::OpenChildrenOnNeighborNode(FNavSvoNode& FromSearchNode, const FSvoNode& FromNode, ESvoNeighbor Neighbor, const FSvoNode& NeighborNode)
{
	SCOPE_CYCLE_COUNTER(STAT_OpenNeighborChildren);

	bool bNeighborsOpened = false;

	// We need the opposite neighbor, since we're looking for nodes in the neighbor
	// that touch us.
	const ESvoNeighbor OppositeNeighbor = FSvoUtils::GetOppositeNeighbor(Neighbor);

	// Add each child that has the parent as a neighbor
	for (uint8 NeighborChildIdx : FSvoUtils::GetChildrenTouchingNeighbor(OppositeNeighbor))
	{
		const FSvoNodeLink NeighborChildLink = NeighborNode.GetChildLink(NeighborChildIdx);
		const FSvoNode* NeighborChildNode = Octree.GetNodeFromLink(NeighborChildLink);
		check(NeighborChildNode);

		bNeighborsOpened |= OpenNeighborNode(FromSearchNode, FromNode, Neighbor, NeighborChildLink, *NeighborChildNode);
	}

	return bNeighborsOpened;
}

bool FNavSvoQuery::OpenVoxelsOnNeighborNode(FNavSvoNode& FromSearchNode, const FSvoNode& FromNode, ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink, const FSvoNode& NeighborNode)
{
	SCOPE_CYCLE_COUNTER(STAT_OpenVoxelsOnNeighborNode);

	bool bNeighborOpened = false;
	FSvoNodeLink NeighborVoxelLink = NeighborLink;

	// Add all face voxels that aren't blocked
	for (uint8 FaceVoxelIndex : FSvoUtils::GetTouchingNeighborVoxels(Neighbor))
	{
		NeighborVoxelLink.VoxelIdx = FaceVoxelIndex;
		if (!NeighborNode.IsVoxelBlocked(NeighborVoxelLink.VoxelIdx))
		{
			bNeighborOpened |= OpenNeighbor(FromSearchNode, FromNode, Neighbor, NeighborVoxelLink, NeighborNode);
		}
	}

	return bNeighborOpened;
}

bool FNavSvoQuery::GetPortalLocation(FSvoNodeLink FromLink, FSvoNodeLink ToLink, ESvoNeighbor Neighbor, FVector& OutLocation) const
{
	const FSvoConfig& OctreeConfig = Octree.GetConfig();
	const float FromResolution = OctreeConfig.GetResolutionForLink(FromLink);
	const float ToResolution = OctreeConfig.GetResolutionForLink(ToLink);

	FVector NodeLocation;
	float NodeExtent;

	// Use the smallest of the two nodes
	if (FromResolution < ToResolution)
	{
		Octree.GetLocationForLink(FromLink, NodeLocation);
		NodeExtent = (FromResolution * 0.5f);
	}
	else
	{
		// Inverse neighbor index if using child so we get the correct face
		Neighbor = FSvoUtils::GetOppositeNeighbor(Neighbor);

		Octree.GetLocationForLink(ToLink, NodeLocation);
		NodeExtent = (ToResolution * 0.5f);
	}

	FBox NodeBounds = FBox::BuildAABB(NodeLocation, FVector(NodeExtent));

	// Constrain the portal location so it's not outside of the desired constraints
	if (Filter->GetConstraints().HasConstraints())
	{
		if (!Filter->GetConstraints().ConstrainBounds(NodeBounds))
		{
			return false;
		}
	}

	const FVector FaceDirection(FSvoUtils::GetNeighborDirection(Neighbor));
	OutLocation = NodeBounds.GetCenter() + (FaceDirection * NodeBounds.GetExtent().X);

	// TODO: Ideally we would slide this point along the face towards the previous
	// node so we don't always enter at the exact center.

	return true;
}

float FNavSvoQuery::GetHeuristic(FSvoNodeLink FromLink) const
{
	// Calculate the Manhattan distance, in voxels, between the portal location and
	// the end location. This will provide a stable heuristic amongst all nodes,
	// regardless of size.

	const FSvoConfig& OctreeConfig = Octree.GetConfig();
	const float Resolution = OctreeConfig.GetVoxelSize();
	const float HeuristicScale = GetHeuristicScale();
	const FSvoNodeLink GoalLink = GetGoal();

	FBox FromBounds, GoalBounds;
	Octree.GetBoundsForLink(FromLink, FromBounds);
	Octree.GetBoundsForLink(GoalLink, GoalBounds);

	const FVector GoalLocation = GoalBounds.GetCenter();
	const FVector ClosestFromLocation = FromBounds.GetClosestPointTo(GoalLocation);

	const FIntVector FromCoord = OctreeConfig.LocationToCoord(ClosestFromLocation, Resolution);
	const FIntVector GoalCoord = OctreeConfig.LocationToCoord(GoalLocation, Resolution);

	const float Cost = FGunfire3DNavigationUtils::GetManhattanDistance(FromCoord, GoalCoord);
	return Cost * HeuristicScale;
}

float FNavSvoQuery::GetHeuristicScale() const
{
	return Filter->GetHeuristicScale();
}

float FNavSvoQuery::GetTraversalCost(FSvoNodeLink FromLink, FSvoNodeLink ToLink, const FVector& PortalLocation) const
{
	// We use the same unit for every node->node traversal cost so that larger nodes
	// don't incur a higher penalty than smaller ones. We basically want all open
	// space neighbors to be considered equal, distance-traveled-wise.
	//
	// TODO: Eventually we need to be adding additional costs provided by the nodes
	// themselves. This should *not* be based on node size but instead a designer
	// influenced cost (e.g. avoid fire, water, etc.)
	float TraversalCost = Filter->GetBaseTraversalCost();
	TraversalCost *= (1.f - (Octree.GetConfig().GetResolutionForLink(ToLink) / Octree.GetConfig().GetTileResolution()));
	return TraversalCost;
}

uint32 FNavSvoQuery::GetMemUsed() const
{
	return sizeof(*this) +
		NodePool.GetMemUsed() +
		OpenList.GetMemUsed();
}