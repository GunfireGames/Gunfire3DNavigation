// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "NavSvoQuery.h"

class FNavSvoNodeQuery : public FNavSvoQuery
{
	typedef FNavSvoQuery Super;

public:
	FNavSvoNodeQuery(const FSparseVoxelOctree& InOctree, int32 MaxSearchNodes, const FVector& InNodeQueryExtent);

	// Finds the closest within the provided extents for the given context
	FSvoNodeLink FindClosestNode(const FVector& Origin, FVector* OutClosestPointOnNode = nullptr);

	// Finds the closest reachable node from the supplied origin.
	FSvoNodeLink FindClosestReachableNode(const FVector& Origin, float DistanceLimit, const FGunfire3DNavQueryFilter& InFilter, FGunfire3DNavQueryResults& InOutResults);

	// Finds a reachable location from the supplied origin.
	FSvoNodeLink FindRandomReachableNode(const FVector& Origin, float DistanceLimit, const FGunfire3DNavQueryFilter& InFilter, FGunfire3DNavQueryResults& InOutResults);

	// Collects all reachable nodes from the supplied origin.
	bool SearchReachableNodes(const FVector& Origin, float DistanceLimit, TFunction<bool(NavNodeRef)> InNodeVisitedCallback, const FGunfire3DNavQueryFilter& InFilter, FGunfire3DNavQueryResults& InOutResults);

	// Returns the point closest to the origin within the bounds of the specified node.
	bool FindClosestPointInNode(FSvoNodeLink NodeLink, const FVector& Origin, FVector& OutPoint);

	// Returns a random point within the bounds of the specified node
	bool FindRandomPointInNode(FSvoNodeLink NodeLink, FVector& OutPoint);

private:
	//~ Begin FNavSvoQuery
	virtual void ResetForNewQuery() override;
	virtual FSvoNodeLink GetGoal() const override { return StartNodeLink; }
	virtual ENavSvoQueryTieBreaker GetCostTieBreaker() const override { return ENavSvoQueryTieBreaker::Nearest; }
	virtual float GetHeuristicScale() const override;
	virtual float GetTraversalCost(FSvoNodeLink FromLink, FSvoNodeLink ToLink, const FVector& PortalLocation) const override;
	virtual bool CanOpenNeighbor(ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink, const FSvoNode& NeighborNode, float NeighborCost, float NeighborDistanceSqrd) override;
	virtual bool OnNodeVisited(FNavSvoNode& SearchNode, const FSvoNode& Node) override;
	//~ End FNavSvoQuery

private:
	// Max distance to search for a node when calling FindClosestNode
	const FVector NodeQueryExtent = FVector::ZeroVector;

	// The maximum, linear distance to search through nodes
	float DistanceLimitSqrd = 0.f;

	// If true, the cost of each node visited will be randomized
	bool bRandomizeCost = false;

	// Callback to notify users that a node has been visited
	//
	// NOTE: This is essentially the same as setting the callback on the filter however
	// prevents needing to duplicate the filter in cases where the callback is passed in
	// directly.
	TFunction<bool(NavNodeRef)> NodeVisitedCallback;
};