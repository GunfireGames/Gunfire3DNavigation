// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "Gunfire3DNavQueryFilter.h"
#include "NavSvoNode.h"

enum class ENavSvoQueryTieBreaker
{
	Nearest,
	Furthest,
};

class FNavSvoQuery
{
public:
	FNavSvoQuery(const class FSparseVoxelOctree& InOctree, int32 MaxSearchNodes);
	virtual ~FNavSvoQuery() {}

protected:
	bool SearchNodes(FSvoNodeLink StartNodeLink, const FGunfire3DNavQueryFilter& InFilter, FGunfire3DNavQueryResults& InOutResults);

	virtual void ResetForNewQuery();

	bool GetPortalLocation(FSvoNodeLink FromLink, FSvoNodeLink ToLink, ESvoNeighbor Neighbor, FVector& OutLocation) const;

	virtual float GetHeuristic(FSvoNodeLink FromLink) const;
	virtual float GetHeuristicScale() const;
	virtual float GetTraversalCost(FSvoNodeLink FromLink, FSvoNodeLink ToLink, const FVector& PortalLocation) const;

	virtual FSvoNodeLink GetGoal() const = 0;
	virtual ENavSvoQueryTieBreaker GetCostTieBreaker() const = 0;

	virtual bool OnNodeVisited(FNavSvoNode& SearchNode, const FSvoNode& Node) { return true; }
	virtual bool CanOpenNeighbor(ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink, const FSvoNode& NeighborNode, float NeighborCost, float NeighborDistanceSqrd) { return true; }
	virtual void OnOpenNeighbor(FNavSvoNode& FromSearchNode, FNavSvoNode& NeighborSearchNode) {}

private:
	inline FNavSvoNode* TryAddSearchNode(FSvoNodeLink NodeLink);

	bool OpenNeighbor(FNavSvoNode& FromSearchNode, const FSvoNode& FromNode, ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink, const FSvoNode& NeighborNode);
	bool OpenNeighbors(FNavSvoNode& SearchNode, const FSvoNode& Node);

	bool OpenNeighborNode(FNavSvoNode& FromSearchNode, const FSvoNode& FromNode, ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink, const FSvoNode& NeighborNode);
	bool OpenChildrenOnNeighborNode(FNavSvoNode& FromSearchNode, const FSvoNode& FromNode, ESvoNeighbor Neighbor, const FSvoNode& NeighborNode);
	bool OpenVoxelsOnNeighborNode(FNavSvoNode& FromSearchNode, const FSvoNode& FromNode, ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink, const FSvoNode& NeighborNode);

protected:
	const class FSparseVoxelOctree& Octree;

	FNavSvoNodePool NodePool;
	FNavSvoNodeQueue OpenList;
		
	// The starting location of the search
	FSvoNodeLink StartNodeLink = SVO_INVALID_NODELINK;

	// The best node found during the search
	FNavSvoNode* BestSearchNode = nullptr;

	// Maximum number of nodes to visit while searching the open node list.
	uint32 NodeVisitationLimit = 0.f;

	const FGunfire3DNavQueryFilter* Filter = nullptr;
	FGunfire3DNavQueryResults* Results = nullptr;
};