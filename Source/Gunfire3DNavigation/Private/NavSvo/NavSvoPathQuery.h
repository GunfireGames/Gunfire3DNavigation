// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "Gunfire3DNavPath.h"
#include "NavSvoQuery.h"

class FNavSvoPathQuery : public FNavSvoQuery
{
	typedef FNavSvoQuery Super;

public:
	FNavSvoPathQuery(const class FSparseVoxelOctree& InOctree, int32 MaxSearchNodes);

	// Attempts to find a path to the specified goal.
	bool FindPath(FSvoNodeLink InStartNodeLink, FSvoNodeLink InGoalNodeLink, float InCostLimit, const FGunfire3DNavQueryFilter& InParams, FGunfire3DNavPathQueryResults& InOutResults);

	// Checks if a path exists to the specified goal.
	bool TestPath(FSvoNodeLink InStartNodeLink, FSvoNodeLink InGoalNodeLink, float InCostLimit, const FGunfire3DNavQueryFilter& InParams, FGunfire3DNavPathQueryResults& InOutResults);

private:
	//~ Begin FNavSvoQuery
	virtual void ResetForNewQuery() override;
	virtual FSvoNodeLink GetGoal() const override { return GoalNodeLink; }
	virtual ENavSvoQueryTieBreaker GetCostTieBreaker() const override { return ENavSvoQueryTieBreaker::Nearest; }
	virtual bool CanOpenNeighbor(ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink, const FSvoNode& NeighborNode, float NeighborCost, float NeighborDistanceSqrd) override;
	virtual bool OnNodeVisited(FNavSvoNode& SearchNode, const FSvoNode& Node) override;
	//~ End FNavSvoQuery

private:
	FSvoNodeLink GoalNodeLink = SVO_INVALID_NODELINK;
	float CostLimit = 0.f;
};