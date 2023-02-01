// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

// Default Settings
#define NAVDATA_DEFAULT_MAX_NODES 2048
#define NAVDATA_DEFAULT_HEURISTIC_SCALE 1.f
#define NAVDATA_DEFAULT_BASE_TRAVERSAL_COST 1.f

enum class EGunfire3DNavQueryFlags : uint8
{
	// Result flags
	Success				= 1 << 0,
	Failure				= 1 << 1,

	// Detail flags
	InvalidParam		= 1 << 2,
	UnknownLocation		= 1 << 3,
	OutOfMemory			= 1 << 4,
	OutOfNodes			= 1 << 5,

	UserFlags			= 1 << 6,
};
ENUM_CLASS_FLAGS(EGunfire3DNavQueryFlags);

struct FGunfire3DNavQueryConstraints
{
public:
	void Reset()
	{
		Bounds.Reset();
	}

	// Returns true if any constraints have been set (e.g. Radius, MaxHeight, etc.)
	bool HasConstraints() const;

	// Returns true if the provided bounds within any constraints. If necessary,
	// resizes the provided bounds to fit all constraints.
	bool ConstrainBounds(FBox& InOutBounds) const;

	// All nodes queried must be within all inclusion bounds. Paths nodes will also be
	// constrained to these bounds. This is explicitly a pointer to a reference to
	// prevent the need to copy data every time. This should be cleared prior to
	// destroying the referenced data.
	const TArray<FBox>& GetBoundsConstraints() const { return Bounds; }
	void SetBoundsConstraints(const TArray<FBox>& Constraints) { Bounds = Constraints; }
	void AddBoundsConstraint(const FBox& Constraints) { Bounds.Add(Constraints); }

private:
	TArray<FBox> Bounds;
};

struct FGunfire3DNavQueryResults
{
	// The overall status of the query
	uint16 Status = 0;

	// Number of nodes queried to check if they could be opened.
	uint32 NumNodesQueried = 0;

	// Number of unique nodes opened.
	uint32 NumNodesOpened = 0;

	// Number of nodes that were opened more than once.
	uint32 NumNodesReopened = 0;

	// How many times nodes were visited (had their neighbors opened)
	uint32 NumNodesVisited = 0;

	virtual ~FGunfire3DNavQueryResults() {}
	virtual void Reset()
	{
		Status = 0;
		NumNodesQueried = 0;
		NumNodesOpened = 0;
		NumNodesReopened = 0;
		NumNodesVisited = 0;
	}
};

class GUNFIRE3DNAVIGATION_API FGunfire3DNavQueryFilter : public INavigationQueryFilterInterface
{
	friend class AGunfire3DNavData;

public:
	//~ Begin INavigationQueryFilterInterface Interface
	virtual void Reset() override {}

	virtual void SetAreaCost(uint8 AreaType, float Cost) override {}
	virtual void SetFixedAreaEnteringCost(uint8 AreaType, float Cost) override {}
	virtual void SetExcludedArea(uint8 AreaType) override {}
	virtual void SetAllAreaCosts(const float* CostArray, const int32 Count) override {}
	virtual void GetAllAreaCosts(float* CostArray, float* FixedCostArray, const int32 Count) const override {}
	virtual void SetBacktrackingEnabled(const bool bBacktracking) override {}
	virtual bool IsBacktrackingEnabled() const override { return false; }
	virtual bool IsEqual(const INavigationQueryFilterInterface* Other) const override;
	virtual void SetIncludeFlags(uint16 Flags) override {}
	virtual uint16 GetIncludeFlags() const override { return 0; }
	virtual void SetExcludeFlags(uint16 Flags) override {}
	virtual uint16 GetExcludeFlags() const override { return 0; }
	virtual FVector GetAdjustedEndLocation(const FVector& EndLocation) const override { return EndLocation; }

	virtual INavigationQueryFilterInterface* CreateCopy() const override;
	//~ End INavigationQueryFilterInterface Interface

	// A scalar applied to the cost of nodes during path finding. The larger the scale,
	// the more the path finding will favor choosing nodes that are closer to the
	// destination, regardless of obstacles.
	float GetHeuristicScale() const { return HeuristicScale; }
	void SetHeuristicScale(float Scale) { HeuristicScale = Scale; }

	// The cost of moving to a new node, at minimum, during path finding. This affects
	// which nodes are explored during the path finding process. The larger the cost, the
	// more the path finding will favor shorter distances traveled from the start of the
	// path.
	float GetBaseTraversalCost() const { return BaseTraversalCost; }
	void SetBaseTraversalCost(float Cost) { BaseTraversalCost = Cost; }

	// All nodes queried must be within all constraints. Paths nodes will also be
	// constrained to these bounds.
	FGunfire3DNavQueryConstraints& GetConstraints() { return Constraints; }
	const FGunfire3DNavQueryConstraints& GetConstraints() const { return Constraints; }
	void SetConstraints(const FGunfire3DNavQueryConstraints& NewConstraints) { Constraints = NewConstraints; }

	// If valid, called every time a node is visited. Returning false form this function
	// will stop the search.
	TFunction<bool(NavNodeRef)> OnNodeVisited;

private:
	float HeuristicScale = NAVDATA_DEFAULT_HEURISTIC_SCALE;
	float BaseTraversalCost = NAVDATA_DEFAULT_BASE_TRAVERSAL_COST;

	FGunfire3DNavQueryConstraints Constraints;
};
