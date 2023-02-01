// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "Gunfire3DNavPath.h"
#include "Gunfire3DNavQueryFilter.h"

#include "NavigationData.h"

#include "Gunfire3DNavData.generated.h"

class FEditableSvo;
class FNavSvoGenerator;

UENUM()
enum class ENav3DDrawType : uint8
{
	Open,
	Blocked,
};

UCLASS(config=Engine, defaultconfig, hidecategories=(Input,Rendering,Tags,Transformation,Actor,Layers,Replication), notplaceable)
class GUNFIRE3DNAVIGATION_API AGunfire3DNavData : public ANavigationData
{
	friend class UGunfire3DNavRenderingComponent;
	friend class FNavSvoGenerator;
	friend class ANavSvoDebugActor;
	friend class FNavSvoSceneProxy;

	GENERATED_BODY()

public:
	AGunfire3DNavData();

	// If enabled, the outside of all obstructed areas will be drawn during debug
	// rendering
	UPROPERTY(EditInstanceOnly, Category = "Display")
	bool bDrawShell = true;

	// If enabled, the cells of the octree will be drawn during debug rendering
	UPROPERTY(EditInstanceOnly, Category = "Display|Octree")
	bool bDrawOctree = false;

	UPROPERTY(EditInstanceOnly, Category = "Display|Octree")
	ENav3DDrawType DrawType = ENav3DDrawType::Blocked;

	UPROPERTY(EditInstanceOnly, Category = "Display|Octree")
	bool bIncludeVoxelAreas = false;

	UPROPERTY(EditInstanceOnly, Category = "Display|Octree")
	bool bDrawSingleLayer = false;

	// Determines the layer to draw
	UPROPERTY(EditInstanceOnly, Category = "Display|Octree", meta = (ClampMin = "0", ClampMax = "10", EditCondition = bDrawSingleLayer))
	uint8 DrawLayerIndex = 0;

	// The size of a voxel in the octree. A 3D grid of cubes this size will be overlaid on
	// the world and any voxels with any collision inside them will be marked as blocked.
	// We allow a nav agent to be centered at any point in a voxel, so while an agent can
	// potentially get right up to the edge of collision in open areas, the smallest
	// opening an agent will ever be able to navigate through is 3 times this value.
	// Smaller values will allow an agent to navigate tighter spaces, but can drastically
	// increase memory usage and generation time.
	UPROPERTY(EditDefaultsOnly, Category = "Generation", config, meta = (ClampMin = "4.0"))
	float VoxelSize = 32.0f;

	// The number of layers of resolution in each tile. Higher values here make searches
	// more efficient, at the expense of making the tile size larger.
	UPROPERTY(EditDefaultsOnly, Category = "Generation", config, meta = (ClampMin = "1", ClampMax = "5", DisplayName = "Num Tile Layers"))
	uint8 TileLayerIndex = 3;

#if WITH_EDITORONLY_DATA
	// The size of the tiles, based on Voxel Size and Num Tile Layers. This is the
	// smallest unit of granularity for building navigation, so any rebuilding at runtime
	// will cover a cube of this size.
	UPROPERTY(VisibleDefaultsOnly, Category = "Generation")
	float TileSize = 0.0f;
#endif

	// The size of the tile pool and the limit of the number of tiles which can be
	// generated.
	UPROPERTY(EditDefaultsOnly, Category = "Generation", config, meta = (ClampMin = "1"))
	uint32 TilePoolSize = 4096;

	// If false, the tile pool will expand by 'Tile Pool Size' amount when full and new
	// tile is required.
	UPROPERTY(EditDefaultsOnly, Category = "Generation", config)
	bool bFixedTilePoolSize = false;

	// The maximum number of threads allowed to generate tiles at once.
	UPROPERTY(EditDefaultsOnly, Category = "Generation", config, AdvancedDisplay, meta = (ClampMin = "1"), AdvancedDisplay)
	int32 MaxTileGenerationJobs = 1024;

	// The maximum number of tiles to generate on a single generation thread.
	UPROPERTY(EditDefaultsOnly, Category = "Generation", config, AdvancedDisplay, meta = (ClampMin = "1"), AdvancedDisplay)
	int32 MaxTilesPerGenerationJob = 1;

	// If true, geometry gathered for tile generation will be collected within the worker
	// thread however this will limit the maximum number of tile generation jobs to only
	// one at a time, essentially ignoring what was entered into 'Max Tile Generation
	// Jobs'.
	UPROPERTY(EditDefaultsOnly, Category = "Generation", config, AdvancedDisplay)
	bool bDoAsyncGeometryGathering = false;

	// Specifies default limit to nodes used when performing navigation queries.
	// 
	// Can be overridden by passing custom FNavigationQueryFilter
	UPROPERTY(EditDefaultsOnly, Category = "Query", config, meta = (ClampMin = "0"))
	uint32 DefaultMaxSearchNodes = NAVDATA_DEFAULT_MAX_NODES;

	// This scale will be applied to the heuristic of the A* algorithm. The larger the
	// number, the more the algorithm will favor being closer to the destination and care
	// less about the length of the path.
	// 
	// Can be overridden by passing custom FNavigationQueryFilter
	UPROPERTY(EditDefaultsOnly, Category = "Query", config, AdvancedDisplay, meta = (ClampMin = "1.0"))
	float DefaultHeuristicScale = NAVDATA_DEFAULT_HEURISTIC_SCALE;

	// This is the base cost of a node during the path finding process. The larger the
	// number, the more the algorithm will favor shorter paths and care less about how
	// close it is to the destination.
	// 
	// Can be overridden by passing custom FNavigationQueryFilter
	UPROPERTY(EditDefaultsOnly, Category = "Query", config, AdvancedDisplay, meta = (ClampMin = "0.0"))
	float DefaultBaseTraversalCost = NAVDATA_DEFAULT_BASE_TRAVERSAL_COST;

public:
	// Tells the rendering component to redraw. If 'bForce' is true the redraw will occur
	// regardless of whether navigation is flagged as drawing.
	void RequestDrawingUpdate(bool bForce = false);

	// A helper to get the supported area classes without duping off the whole array with
	// strings and stuff, like GetSupportedAreas.
	void GetSupportedAreaClasses(TArray<TWeakObjectPtr<UClass>>& AreaIDs) const;
	
	///> Node Queries

	// NOTE: The maximum distance is the maximum length of a path to a node and is not a
	// radius. A value of zero disables this constraint.

	// Returns true if the specified node was found and its location was obtained.
	bool GetNodeLocation(NavNodeRef NodeRef, FVector& OutLocation) const;

	// Returns true if the bounds of the specified node could be calculated
	bool GetNodeBounds(NavNodeRef NodeRef, FBox& OutBounds) const;

	// Returns the node at the given location.
	//
	// NOTE: This will only return a valid node if it isn't blocked and exists within the
	// generated bounds.
	NavNodeRef GetNodeAtLocation(const FVector& Location) const;

	// Finds the closest node within an extent from the supplied origin.
	NavNodeRef FindClosestNode(const FVector& Origin, const FVector& QueryExtent, FSharedConstNavQueryFilter QueryFilter = nullptr) const;

	// Finds the closest reachable node from the supplied origin.
	NavNodeRef FindClosestReachableNode(const FVector& Origin, float MaxDistance, FSharedConstNavQueryFilter QueryFilter = nullptr) const;

	// Finds a reachable location from the supplied origin.
	NavNodeRef FindRandomReachableNode(const FVector& Origin, float MaxDistance, FSharedConstNavQueryFilter QueryFilter = nullptr) const;

	// Collects all reachable nodes from the supplied origin.
	bool GatherReachableNodes(const FVector& Origin, float MaxDistance, TArray<NavNodeRef>& OutResult, FSharedConstNavQueryFilter QueryFilter = nullptr) const;

	// Iterates over all reachable nodes from the supplied origin, calling the lambda
	// function for each one visisted.
	//
	// NOTE: If the lambda returns false, the search will be stopped.
	bool ForEachReachableNode(const FVector& Origin, float MaxDistance, TFunction<bool(NavNodeRef)> Lambda, FSharedConstNavQueryFilter QueryFilter = nullptr) const;

	// Returns true if the given point is within the bounds that are being used to
	// generate this navigation data.
	bool IsLocationWithinGenerationBounds(const FVector& Location) const;

	// Returns true if the given node is within the bounds that are being used to generate
	// navigation data.
	bool IsNodeWithinGenerationBounds(NavNodeRef NodeRef) const;

	// Generating 3D nav can crush the CPU, so normally we don't run it on a bunch of
	// threads at once. If you can trade CPU for faster generation though, like during
	// loading, this will increase the thread utilization.
	static void SetGenerationBoostMode(bool Enabled);
	static bool IsGenerationBoostMode() { return bGenerationBoostMode; }

protected:
	// Returns the octree generated for this navigation volume
	const FEditableSvo* GetOctree() const { return Octree.Get(); }

	// Mutable version of GetOctree for generator
	FEditableSvo* GetOctree() { return Octree.Get(); }

	// Sets the octree generated for this navigation volume. Called by the generator.
	void SetOctree(TSharedPtr<FEditableSvo, ESPMode::ThreadSafe> InOctree);

	// Destroys octree and performs any necessary cleanup. Also called by the generator.
	void DestroyOctree();

	// Determines if the octree has been generated or not
	bool HasValidOctree() const;

	//~ Begin ANavigationData Interface
public:
	// Handles cleaning up any dependencies
	virtual void CleanUp() override;

	// Determines whether the navigation data needs to be rebuilt
	virtual bool NeedsRebuild() const override;

	// Determines whether the navigation can be generated during runtime
	virtual bool SupportsRuntimeGeneration() const override;

	// Determines whether this data can be streamed in or not.
	virtual bool SupportsStreaming() const override;

	// Called by the UNavigationSystem when a level is streamed in during game play
	virtual void OnStreamingLevelAdded(ULevel* InLevel, UWorld* InWorld) override;

	// Called by the UNavigationSystem when a level is removed in during game play
	virtual void OnStreamingLevelRemoved(ULevel* InLevel, UWorld* InWorld) override;

	virtual void SetConfig(const FNavDataConfig& Src) override;

	// Sets whether the generator should only build active tiles. All tiles will be
	// rebuilt otherwise.
	virtual void RestrictBuildingToActiveTiles(bool bInRestrictBuildingToActiveTiles) override;

protected:
	virtual void FillConfig(FNavDataConfig& Dest) override;

public:
	// Constructs the generator for this navigation area
	virtual void ConditionalConstructGenerator() override;

	// Forces all current build tasks to complete on the main thread.
	virtual void EnsureBuildCompletion() override;

	// Retrieves the overall bounds of this navigation area
	virtual FBox GetBounds() const override;

#if !UE_BUILD_SHIPPING
	virtual uint32 LogMemUsed() const override;
#endif // !UE_BUILD_SHIPPING

	// Constructs the debug rendering component for this navigation area
	virtual UPrimitiveComponent* ConstructRenderingComponent() override;

	// Callback registered with ANavigationData to find a path
	static FPathFindingResult FindPath(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query);

	// Callback registered with ANavigationData for testing the path to a location
	static bool TestPath(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query, int32* NumVisitedNodes);

	// Callback for testing the path to a location via hierarchical graph
	static bool TestHierarchicalPath(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query, int32* NumVisitedNodes);

	// Raycast implementation required by ANavigationData
	static bool NavDataRaycast(const ANavigationData* Self, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier = nullptr);

	// Raycasts batched for efficiency
	virtual void BatchRaycast(TArray<FNavigationRaycastWork>& Workload, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier = nullptr) const override;

	// Tries to move current nav location towards target constrained to navigable area.
	virtual bool FindMoveAlongSurface(const FNavLocation& StartLocation, const FVector& TargetPosition, FNavLocation& OutLocation, FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;

	// Finds a random point in the entirety of the navigation volume
	virtual FNavLocation GetRandomPoint(FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;

	// Finds a random location in Radius, reachable from Origin
	virtual bool GetRandomReachablePointInRadius(const FVector& Origin, float Radius, FNavLocation& OutResult, FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;

	// Finds a random location in navigable space, in given Radius
	virtual bool GetRandomPointInNavigableRadius(const FVector& Origin, float Radius, FNavLocation& OutResult, FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;

	// Tries to project given Point to this navigation type, within given QueryExtent.
	virtual bool ProjectPoint(const FVector& Point, FNavLocation& OutLocation, const FVector& QueryExtent, FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;

	// Batches ProjectPoint's work for efficiency
	virtual void BatchProjectPoints(TArray<FNavigationProjectionWork>& Workload, const FVector& Extent, FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;

	// Project batch of points using shared search filter. This version is not requiring
	// user to pass in Extent, and is instead relying on
	// FNavigationProjectionWork.ProjectionLimit.
	virtual void BatchProjectPoints(TArray<FNavigationProjectionWork>& Workload, FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;

	// Calculates path from PathStart to PathEnd and retrieves its cost.
	virtual ENavigationQueryResult::Type CalcPathCost(const FVector& PathStart, const FVector& PathEnd, float& OutPathCost, FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;

	// Calculates path from PathStart to PathEnd and retrieves its length.
	virtual ENavigationQueryResult::Type CalcPathLength(const FVector& PathStart, const FVector& PathEnd, float& OutPathLength, FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;

	// Calculates path from PathStart to PathEnd and retrieves its length.
	virtual ENavigationQueryResult::Type CalcPathLengthAndCost(const FVector& PathStart, const FVector& PathEnd, float& OutPathLength, float& OutPathCost, FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;

	// Determines whether the specified NavNodeRef is still valid
	virtual bool IsNodeRefValid(NavNodeRef NodeRef) const override;

	// Checks if specified navigation node contains given location
	virtual bool DoesNodeContainLocation(NavNodeRef NodeRef, const FVector& WorldSpaceLocation) const override;
	//~ End ANavigationData Interface

	//~ Begin AActor Interface
protected:
	virtual void PostInitProperties() override;

	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR
	// Navigation instances are dynamically spawned and should not be copied
	virtual bool ShouldExport() override { return false; }
#endif // WITH_EDITOR
	//~ End AActor Interface

	//~ Begin UObject Interface
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface

protected:
	// Forces the default filter to be created
	void RecreateDefaultFilter();

	// Will return the default query if the supplied query is invalid.
	const FNavigationQueryFilter& ResolveFilterRef(FSharedConstNavQueryFilter Filter) const
	{
		return *(Filter.IsValid() ? Filter.Get() : GetDefaultQueryFilter().Get());
	}

	// Grabs the navigation data chunk for a streaming in/out level
	class UNavSvoStreamingData* GetStreamingLevelData(const ULevel* InLevel) const;

	// Called by the installed implementation's generator once it's finished generating.
	void OnGenerationComplete();

	// Informs the rendering component to refresh
	void UpdateDrawing();

	// Helper function to retrieve the generator as a NavSvoGenerator type
	FNavSvoGenerator* GetNavSvoGenerator();
	const FNavSvoGenerator* GetNavSvoGenerator() const;

private:
	// Generated octree for this implementation
	TSharedPtr<FEditableSvo, ESPMode::ThreadSafe> Octree;

	static bool bGenerationBoostMode;
};