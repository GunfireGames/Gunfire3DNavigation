// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "NavSvoPathQuery.h"

#include "Gunfire3DNavigationUtils.h"
#include "SparseVoxelOctree/SparseVoxelOctree.h"

#define SVO_NEIGHBOR_MASK 0x7

DECLARE_CYCLE_STAT(TEXT("FindPath (Query)"), STAT_FindPath_Query, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("TestPath (Query)"), STAT_TestPath_Query, STATGROUP_Gunfire3DNavigation);

FNavSvoPathQuery::FNavSvoPathQuery(const FSparseVoxelOctree& InOctree, int32 MaxSearchNodes)
	: Super(InOctree, MaxSearchNodes)
{}

bool FNavSvoPathQuery::FindPath(FSvoNodeLink InStartNodeLink, FSvoNodeLink InGoalNodeLink, float InCostLimit, const FGunfire3DNavQueryFilter& InFilter, FGunfire3DNavPathQueryResults& InOutResults)
{
	SCOPE_CYCLE_COUNTER(STAT_FindPath_Query);

	ResetForNewQuery();

	if (!InStartNodeLink.IsValid() || !InGoalNodeLink.IsValid())
	{
		InOutResults.Status = (uint8)(EGunfire3DNavQueryFlags::Failure | EGunfire3DNavQueryFlags::InvalidParam);
		return false;
	}

	StartNodeLink = InStartNodeLink;
	GoalNodeLink = InGoalNodeLink;
	CostLimit = InCostLimit;

	// If the start and end node are the same, just add the end node to the pool
	if (InStartNodeLink == InGoalNodeLink)
	{
		BestSearchNode = NodePool.GetNode(InGoalNodeLink);
		if (BestSearchNode != nullptr)
		{
			InOutResults.PathNodeCount = 1;
			InOutResults.Status |= (uint8)EGunfire3DNavQueryFlags::Success;
			return true;
		}

		InOutResults.Status |= (uint8)EGunfire3DNavQueryFlags::Failure;
		return false;
	}

	// Run query
	const bool QueryResult = SearchNodes(InStartNodeLink, InFilter, InOutResults);
	if (QueryResult)
	{
		// If the end was not found, mark that this is only a partial path.
		if (BestSearchNode->NodeLink != GoalNodeLink)
		{
			InOutResults.Status |= (uint16)EGunfire3DNavPathQueryFlags::PartialPath;
		}

		InOutResults.PathCost = BestSearchNode->FCost;
		InOutResults.PathLength = FMath::Sqrt(BestSearchNode->TravelDistSqrd);

		// Reverse the found path to get the result from start to finish and count the
		// number of nodes which make up the path.
		FNavSvoNode* PrevSearchNode = nullptr;
		FNavSvoNode* SearchNode = BestSearchNode;
		do
		{
			FNavSvoNode* NextSearchNode = NodePool.GetNodeAtIndex(SearchNode->ParentIdx);
			SearchNode->ParentIdx = NodePool.GetNodeIndex(PrevSearchNode);
			PrevSearchNode = SearchNode;
			SearchNode = NextSearchNode;

			// If there are more nodes than the loop limit then we've likely got a
			// cyclical path
			if (++InOutResults.PathNodeCount >= NodeVisitationLimit)
			{
				InOutResults.Status |= (uint16)EGunfire3DNavPathQueryFlags::CyclicalPath;
				break;
			}
		} while (SearchNode != nullptr);

		// Store the path points in the results

		InOutResults.PathPortalPoints.Reserve(InOutResults.PathNodeCount);

		// NOTE: We skip the first node as there is no portal location yet.
		const FNavSvoNode* PathSearchNode = NodePool.GetNodeAtIndex(PrevSearchNode->ParentIdx);
		while (PathSearchNode != nullptr && (uint32)InOutResults.PathPortalPoints.Num() < InOutResults.PathNodeCount)
		{
			InOutResults.PathPortalPoints.Add(FNavPathPoint(PathSearchNode->PortalLocation, PathSearchNode->NodeLink.GetID()));
			PathSearchNode = NodePool.GetNodeAtIndex(PathSearchNode->ParentIdx);
		}

		return true;
	}

	return false;
}

bool FNavSvoPathQuery::TestPath(FSvoNodeLink InStartNodeLink, FSvoNodeLink InGoalNodeLink, float InCostLimit, const FGunfire3DNavQueryFilter& InFilter, FGunfire3DNavPathQueryResults& InOutResults)
{
	SCOPE_CYCLE_COUNTER(STAT_TestPath_Query);

	ResetForNewQuery();

	if (!InStartNodeLink.IsValid() || !InGoalNodeLink.IsValid())
	{
		InOutResults.Status = (uint8)(EGunfire3DNavQueryFlags::Failure | EGunfire3DNavQueryFlags::InvalidParam);
		return false;
	}

	StartNodeLink = InStartNodeLink;
	GoalNodeLink = InGoalNodeLink;
	CostLimit = InCostLimit;

	// If the start and end node are the same, just add the start node to the pool
	if (InStartNodeLink == InGoalNodeLink)
	{
		BestSearchNode = NodePool.GetNode(InStartNodeLink);
		InOutResults.Status = (uint8)EGunfire3DNavQueryFlags::Success;
		return true;
	}

	// Run query
	const bool QueryResult = SearchNodes(InStartNodeLink, InFilter, InOutResults);
	return QueryResult;
}

void FNavSvoPathQuery::ResetForNewQuery()
{
	Super::ResetForNewQuery();

	GoalNodeLink = SVO_INVALID_NODELINK;
	CostLimit = 0.f;
}

bool FNavSvoPathQuery::CanOpenNeighbor(ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink, const FSvoNode& NeighborNode, float NeighborCost, float NeighborDistanceSqrd)
{
	// Ensure we aren't exceeding the maximum cost limit
	if (CostLimit > 0.f && NeighborCost > CostLimit)
	{
		return false;
	}

	return true;
}

bool FNavSvoPathQuery::OnNodeVisited(FNavSvoNode& SearchNode, const FSvoNode& Node)
{
	// If the goal has been reached, stop searching.
	if (SearchNode.NodeLink == GoalNodeLink)
	{
		BestSearchNode = &SearchNode;
		return false;
	}

	return true;
}