// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "NavSvoLocationQuery.h"

// Gunfire
#include "Gunfire3DNavigationUtils.h"
#include "SparseVoxelOctree/SparseVoxelOctree.h"

// Unreal
#include "Containers/CircularQueue.h"

DECLARE_CYCLE_STAT(TEXT("FindClosestNode"), STAT_FindClosestNode, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("FindReachableNodes"), STAT_FindReachableNodes, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("FindClosestReachableNode"), STAT_FindClosestReachableNode, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("FindRandomReachableNode"), STAT_FindRandomReachableNode, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("SearchReachableNode"), STAT_SearchReachableNodes, STATGROUP_Gunfire3DNavigation);

FNavSvoNodeQuery::FNavSvoNodeQuery(const FSparseVoxelOctree& InOctree, int32 MaxSearchNodes, const FVector& InNodeQueryExtent)
	: Super(InOctree, MaxSearchNodes)
	, NodeQueryExtent(InNodeQueryExtent)
{}

FSvoNodeLink FNavSvoNodeQuery::FindClosestNode(const FVector& Origin, FVector* OutClosestPointOnNode)
{
	SCOPE_CYCLE_COUNTER(STAT_FindClosestNode);

	static const float kEpsilon = 0.01f;

	// If the tree hasn't been generated, bail
	if (!Octree.IsValid())
	{
		return SVO_INVALID_NODELINK;
	}

	// First, grab the node at this location and see if it is even blocked to begin with.
	FSvoNodeLink LocationLink = Octree.GetLinkForLocation(Origin);
	if (LocationLink.IsValid())
	{
		if (OutClosestPointOnNode != nullptr)
		{
			*OutClosestPointOnNode = Origin;
		}

		return LocationLink;
	}

	const FSvoConfig Config = Octree.GetConfig();
	const FBox QueryBounds = FBox::BuildAABB(Origin, NodeQueryExtent);
	const uint32 MaxSearchNodes = NodePool.GetMaxNodes();
	uint32 NumNodesSearched = 0;

	// Don't continue if an invalid number of search nodes was passed in.
	if (MaxSearchNodes == 0)
	{
		return SVO_INVALID_NODELINK;
	}

	struct FRelevantNode
	{
		FSvoNodeLink Link;
		const FSvoNode* Node;
		FBox Bounds;
	};
	TArray<FRelevantNode> RelevantTiles;
	RelevantTiles.Reserve(MaxSearchNodes);

	// Collect all tiles in range
	Octree.GetTilesInBounds(QueryBounds, [&](const FSvoTile& Tile)
	{
		FBox TileBounds = Config.GetTileBounds(Tile.GetCoord());
		RelevantTiles.Add({ Tile.GetSelfLink(), &Tile.GetNodeInfo(), TileBounds });

		// Bail if the search limit has been reached as there is no point in collecting
		// more tiles.
		return (uint32(RelevantTiles.Num()) < MaxSearchNodes);
	});

	// Don't continue if there aren't any tiles within the search radius
	if (RelevantTiles.Num() == 0)
	{
		return SVO_INVALID_NODELINK;
	}

	// We want to find the closest location first to sort the found tiles by how close
	// they are to the center of the query.
	RelevantTiles.Sort([&Origin](const FRelevantNode& A, const FRelevantNode& B)
	{
		float DistancesSqrdA = FVector::DistSquared(A.Bounds.GetCenter(), Origin);
		float DistancesSqrdB = FVector::DistSquared(B.Bounds.GetCenter(), Origin);
		return DistancesSqrdA < DistancesSqrdB;
	});

	// Create relevant node queues so we can process by size
	TCircularQueue<FRelevantNode> RelevantNodes(MaxSearchNodes);
	TCircularQueue<FRelevantNode> RelevantVoxelNodes(MaxSearchNodes);

	// Copy all found tiles into the node queue
	for (int32 TileIdx = (RelevantTiles.Num() - 1); TileIdx >= 0; --TileIdx)
	{
		RelevantNodes.Enqueue(RelevantTiles[TileIdx]);
	}

	// Release the tile array memory as we now have everything in the queue.
	RelevantTiles.Empty();

	FSvoNodeLink BestNavNode = SVO_INVALID_NODELINK;
	float ClosestDistanceSqrd = MAX_flt;

	// Process all non-voxel nodes
	while ((RelevantNodes.Count() > 0) && (NumNodesSearched < MaxSearchNodes))
	{
		FRelevantNode RelevantNode;
		RelevantNodes.Dequeue(RelevantNode);
		++NumNodesSearched;

		const ENodeState State = RelevantNode.Node->GetNodeState();

		if (State == ENodeState::Open)
		{
			FVector ClosestPointOnNode = RelevantNode.Bounds.GetClosestPointTo(Origin);

			float DistSqrd = FVector::DistSquared(Origin, ClosestPointOnNode);
			if (DistSqrd < ClosestDistanceSqrd)
			{
				ClosestDistanceSqrd = DistSqrd;
				BestNavNode = RelevantNode.Link;

				// Pull the point back into the node a bit so it doesn't land right on the
				// edge of the node
				if (OutClosestPointOnNode != nullptr)
				{
					*OutClosestPointOnNode = ClosestPointOnNode + (RelevantNode.Bounds.GetCenter() - ClosestPointOnNode).GetSafeNormal() * kEpsilon;
				}
			}
		}
		else if (State == ENodeState::PartiallyBlocked)
		{
			if (RelevantNode.Node->IsLeafNode())
			{
				FSvoNodeLink VoxelLink = RelevantNode.Link;
				for (FSvoVoxelIterator VoxelIter; VoxelIter; ++VoxelIter)
				{
					VoxelLink.VoxelIdx = VoxelIter.GetIndex();
					const bool bVoxelIsBlocked = RelevantNode.Node->IsVoxelBlocked(VoxelLink.VoxelIdx);
					if (!bVoxelIsBlocked)
					{
						FBox VoxelBounds;
						Octree.GetBoundsForLink(VoxelLink, VoxelBounds);

						if (FGunfire3DNavigationUtils::AABBIntersectsAABB(QueryBounds, VoxelBounds))
						{
							FVector ClosestPointOnNode = VoxelBounds.GetClosestPointTo(Origin);

							float DistSqrd = FVector::DistSquared(Origin, ClosestPointOnNode);
							if (DistSqrd < ClosestDistanceSqrd)
							{
								ClosestDistanceSqrd = DistSqrd;
								BestNavNode = VoxelLink;

								// Pull the point back into the node a bit so it doesn't
								// land right on the edge of the node
								if (OutClosestPointOnNode != nullptr)
								{
									*OutClosestPointOnNode = ClosestPointOnNode + (VoxelBounds.GetCenter() - ClosestPointOnNode).GetSafeNormal() * kEpsilon;
								}
							}
						}
					}
				}
			}
			else
			{
				for (uint8 ChildIdx = 0; ChildIdx < 8; ++ChildIdx)
				{
					FSvoNodeLink ChildNodeLink = RelevantNode.Node->GetChildLink(ChildIdx);
					const FSvoNode* ChildNode = Octree.GetNodeFromLink(ChildNodeLink);
					check(ChildNode);

					if (ChildNode->GetNodeState() != ENodeState::Blocked)
					{
						FBox NodeBounds = Octree.GetBoundsForNode(*ChildNode);
						if (FGunfire3DNavigationUtils::AABBIntersectsAABB(QueryBounds, NodeBounds))
						{
							FVector ClosestPointOnNode = RelevantNode.Bounds.GetClosestPointTo(Origin);

							float DistSqrd = FVector::DistSquared(Origin, ClosestPointOnNode);
							if (DistSqrd < ClosestDistanceSqrd)
							{
								RelevantNodes.Enqueue({ ChildNodeLink, ChildNode, NodeBounds });
							}
						}
					}
				}
			}
		}
	}

	return BestNavNode;
}

FSvoNodeLink FNavSvoNodeQuery::FindClosestReachableNode(const FVector& Origin, float DistanceLimit, const FGunfire3DNavQueryFilter& InFilter, FGunfire3DNavQueryResults& InOutResults)
{
	SCOPE_CYCLE_COUNTER(STAT_FindClosestReachableNode);

	DistanceLimitSqrd = (DistanceLimit * DistanceLimit);

	bool bQueryResult = SearchNodes(FindClosestNode(Origin), InFilter, InOutResults);
	if (bQueryResult)
	{
		if (BestSearchNode != nullptr)
		{
			return BestSearchNode->NodeLink;
		}
	}

	return SVO_INVALID_NODELINK;
}

FSvoNodeLink FNavSvoNodeQuery::FindRandomReachableNode(const FVector& Origin, float DistanceLimit, const FGunfire3DNavQueryFilter& InFilter, FGunfire3DNavQueryResults& InOutResults)
{
	SCOPE_CYCLE_COUNTER(STAT_FindRandomReachableNode);

	DistanceLimitSqrd = (DistanceLimit * DistanceLimit);
	bRandomizeCost = true;

	const bool bQueryResult = SearchNodes(FindClosestNode(Origin), InFilter, InOutResults);
	if (bQueryResult)
	{
		if (BestSearchNode != nullptr)
		{
			return BestSearchNode->NodeLink;
		}
	}

	return SVO_INVALID_NODELINK;
}

bool FNavSvoNodeQuery::SearchReachableNodes(const FVector& Origin, float DistanceLimit, TFunction<bool(NavNodeRef)> InNodeVisitedCallback, const FGunfire3DNavQueryFilter& InFilter, FGunfire3DNavQueryResults& InOutResults)
{
	SCOPE_CYCLE_COUNTER(STAT_SearchReachableNodes);

	DistanceLimitSqrd = (DistanceLimit * DistanceLimit);
	NodeVisitedCallback = InNodeVisitedCallback;

	bool bQueryResult = SearchNodes(FindClosestNode(Origin), InFilter, InOutResults);
	return bQueryResult;
}

bool FNavSvoNodeQuery::FindClosestPointInNode(FSvoNodeLink NodeLink, const FVector& Origin, FVector& OutPoint)
{
	FBox NodeBounds;
	if (Octree.GetBoundsForLink(NodeLink, NodeBounds))
	{
		OutPoint = NodeBounds.GetClosestPointTo(Origin);
		return true;
	}

	return false;
}

bool FNavSvoNodeQuery::FindRandomPointInNode(FSvoNodeLink NodeLink, FVector& OutPoint)
{
	FBox NodeBounds;
	if (Octree.GetBoundsForLink(NodeLink, NodeBounds))
	{
		OutPoint = FMath::RandPointInBox(NodeBounds);
		return true;
	}

	return false;
}

void FNavSvoNodeQuery::ResetForNewQuery()
{
	Super::ResetForNewQuery();

	DistanceLimitSqrd = 0.f;
	bRandomizeCost = false;
	NodeVisitedCallback.Reset();
}

float FNavSvoNodeQuery::GetHeuristicScale() const
{
	return (bRandomizeCost) ? FMath::Rand() : Super::GetHeuristicScale();
}

float FNavSvoNodeQuery::GetTraversalCost(FSvoNodeLink FromLink, FSvoNodeLink ToLink, const FVector& PortalLocation) const
{
	return (bRandomizeCost) ? FMath::Rand() : Super::GetTraversalCost(FromLink, ToLink, PortalLocation);
}

bool FNavSvoNodeQuery::CanOpenNeighbor(ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink, const FSvoNode& NeighborNode, float NeighborCost, float NeighborDistanceSqrd)
{
	if (DistanceLimitSqrd > 0.f && NeighborDistanceSqrd > DistanceLimitSqrd)
	{
		return false;
	}

	return true;
}

bool FNavSvoNodeQuery::OnNodeVisited(FNavSvoNode& SearchNode, const FSvoNode& Node)
{
	// Notify query-function that a node is being visited and allow it to bail on the
	// search if desired.
	if (NodeVisitedCallback && !NodeVisitedCallback(SearchNode.NodeLink.GetID()))
	{
		return false;
	}

	return true;
}