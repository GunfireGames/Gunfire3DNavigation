// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "Gunfire3DNavData.h"

#include "Gunfire3DNavDataRenderingComponent.h"
#include "Gunfire3DNavigationCustomVersion.h"
#include "Gunfire3DNavigationTypes.h"
#include "Gunfire3DNavigationUtils.h"
#include "NavSvo/NavSvoGenerator.h"
#include "NavSvo/NavSvoPathQuery.h"
#include "NavSvo/NavSvoLocationQuery.h"
#include "NavSvo/NavSvoStreamingData.h"
#include "NavSvo/NavSvoUtils.h"
#include "SparseVoxelOctree/EditableSparseVoxelOctree.h"

#include "NavigationSystem.h"
#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif

// Profiling stats
DECLARE_CYCLE_STAT(TEXT("FindPath"), STAT_FindPath, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("TestPath"), STAT_TestPath, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("CalcPathLengthAndCost"), STAT_CalcPathLengthAndCost, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("Raycast"), STAT_Raycast, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("BatchRaycast"), STAT_BatchRaycast, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("ProjectPoint"), STAT_ProjectPoint, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("BatchProjectPoints"), STAT_BatchProjectPoints, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("GetRandomReachablePointInRadius"), STAT_GetRandomReachablePointInRadius, STATGROUP_Gunfire3DNavigation);

bool AGunfire3DNavData::bGenerationBoostMode = false;

AGunfire3DNavData::AGunfire3DNavData()
{
#if WITH_EDITOR
	TileSize = FSvoUtils::CalcResolutionForLayer(TileLayerIndex, VoxelSize);
#endif

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		// NAVTODO: Currently no distinct support for hierarchical path finding so just
		// redirecting to standard path finding.

		// Register path finding implementations
		FindPathImplementation = FindPath;
		FindHierarchicalPathImplementation = FindPath;

		// Register path testing implementations
		TestPathImplementation = TestPath;
		TestHierarchicalPathImplementation = TestPath;

		// Register raycast implementation
		RaycastImplementation = NavDataRaycast;
	}
}

void AGunfire3DNavData::Serialize(FArchive& Ar)
{
	// Mark that we are using the latest custom version
	Ar.UsingCustomVersion(FGunfire3DNavigationCustomVersion::GUID);

	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		// Destroy current octree
		DestroyOctree();

		bool bHasOctree = false;
		Ar << bHasOctree;

		// Create and populate the octree
		if (bHasOctree)
		{
			Octree = MakeShareable(new FEditableSvo(EForceInit::ForceInit));
			check(Octree);
			Octree->Serialize(Ar);
		}
	}
	else if (Ar.IsSaving())
	{
		bool bHasOctree = Octree.IsValid();
		Ar << bHasOctree;

		if (bHasOctree)
		{
			//
			// If streaming is supported then only save out tiles relevant to the current
			// level. Otherwise, save out the entire octree.
			//
			const bool bIsStreamingSupported = (IsRegistered() && SupportsStreaming() && !IsRunningCommandlet());
			if (bIsStreamingSupported)
			{
				TArray<FIntVector> TileCoordsToSave;

				// If streaming is supported then only save the tile information
				// associated with the nav data actor's level. That way we don't load data
				// we'll be streaming in anyway via UNavSvoStreamingData.
				ULevel* Level = GetLevel();
				TArray<FBox> LevelNavBounds = GetNavigableBoundsInLevel(Level);
				Octree->GetTileCoords(LevelNavBounds, TileCoordsToSave);

				// Create a new config with the appropriate tile type
				const FSvoConfig& SourceConfig = Octree->GetConfig();
				FSvoConfig Config = FSvoConfig(SourceConfig);

				// If we're not just saving the main octree, create a new one to hold the
				// appropriate data.
				FEditableSvo* TempOctree = new FEditableSvo(Config);
				TempOctree->CopyTilesFrom(*Octree, TileCoordsToSave, false /* bPreserveNeighborLinks */);
				TempOctree->Serialize(Ar);
				delete TempOctree;
			}
			else
			{
				// If saving out the entire tree and it has the same tile type, just
				// serialize out the main octree.
				Octree->Serialize(Ar);
			}		
		}
	}
}

void AGunfire3DNavData::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		RecreateDefaultFilter();
	}

#if WITH_EDITOR
	TileSize = FSvoUtils::CalcResolutionForLayer(TileLayerIndex, VoxelSize);
#endif
}

#if WITH_EDITOR

void AGunfire3DNavData::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	// Category Names
	static const FName NAME_Generation = FName(TEXT("Generation"));
	static const FName NAME_RuntimeGeneration = FName(TEXT("RuntimeGeneration"));
	static const FName NAME_Display = FName(TEXT("Display"));
	static const FName NAME_Query = FName(TEXT("Query"));

	// Property Names
	static const FName NAME_VoxelSize = GET_MEMBER_NAME_CHECKED(AGunfire3DNavData, VoxelSize);
	static const FName NAME_TilePoolSize = GET_MEMBER_NAME_CHECKED(AGunfire3DNavData, TilePoolSize);
	static const FName NAME_FixedTilePoolSize = GET_MEMBER_NAME_CHECKED(AGunfire3DNavData, bFixedTilePoolSize);
	static const FName NAME_TileLayerIndex = GET_MEMBER_NAME_CHECKED(AGunfire3DNavData, TileLayerIndex);

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != NULL)
	{
		FName CategoryName = FObjectEditorUtils::GetCategoryFName(PropertyChangedEvent.Property);
		FName PropertyName = PropertyChangedEvent.Property->GetFName();

		if (CategoryName == NAME_Generation)
		{
			// Force regeneration if any of these properties change
			if (PropertyName == NAME_VoxelSize ||
				PropertyName == NAME_TilePoolSize ||
				PropertyName == NAME_FixedTilePoolSize ||
				PropertyName == NAME_TileLayerIndex)
			{
				TileSize = FSvoUtils::CalcResolutionForLayer(TileLayerIndex, VoxelSize);

				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
				if (!HasAnyFlags(RF_ClassDefaultObject) && NavSys->GetIsAutoUpdateEnabled())
				{
					RebuildAll();
				}
			}
		}
		else if (CategoryName == NAME_Display)
		{
			RequestDrawingUpdate();
		}
		else if (CategoryName == NAME_Query)
		{
			RecreateDefaultFilter();
		}
		// BEGIN: HACK
		// NOTE: This was taken directly from 'ARecastNavMesh::PostEditChangeProperty'
		//		 If this is ever fixed, we should remove this hack.
		//
		else if (PropertyChangedEvent.Property->GetFName() == NAME_RuntimeGeneration)
		{
			// @todo this contraption is required to clear RuntimeGeneration value in
			// DefaultEngine.ini if it gets set to its default value (UE-23762). This is
			// hopefully a temporary solution since it's an Core-level issue (UE-23873).
			if (RuntimeGeneration == ERuntimeGenerationType::Static)
			{
				const FString EngineIniFilename = FPaths::ConvertRelativePathToFull(GetDefault<UEngine>()->GetDefaultConfigFilename());
				GConfig->SetString(TEXT("/Script/Gunfire3DNavigation.Gunfire3DNavData"), *NAME_RuntimeGeneration.ToString(), TEXT("Static"), *EngineIniFilename);
				GConfig->Flush(false);
			}
		}
		// END: HACK
	}
}
#endif // WITH_EDITOR

void AGunfire3DNavData::SetConfig(const FNavDataConfig& Src)
{
	NavDataConfig = Src;
}

void AGunfire3DNavData::FillConfig(FNavDataConfig& Dest)
{
	Dest = NavDataConfig;
}

void AGunfire3DNavData::RecreateDefaultFilter()
{
	DefaultQueryFilter->SetFilterType<FGunfire3DNavQueryFilter>();

	///> Set default values

	DefaultQueryFilter->SetMaxSearchNodes(DefaultMaxSearchNodes);

	FGunfire3DNavQueryFilter* FilterImpl = StaticCast<FGunfire3DNavQueryFilter*>(DefaultQueryFilter->GetImplementation());
	FilterImpl->SetHeuristicScale(DefaultHeuristicScale);
	FilterImpl->SetBaseTraversalCost(DefaultBaseTraversalCost);

	// Always constrain queries to the navigable bounds as the octree can build outside of
	// these.
	const TArray<FBox> NavigableBounds = GetNavigableBounds();
	FilterImpl->GetConstraints().SetBoundsConstraints(NavigableBounds);
}

void AGunfire3DNavData::ConditionalConstructGenerator()
{
	if (NavDataGenerator.IsValid())
	{
		NavDataGenerator->CancelBuild();
		NavDataGenerator.Reset();
	}

	UWorld* World = GetWorld();
	check(World);
	const bool bRequiresGenerator = SupportsRuntimeGeneration() || !World->IsGameWorld();
	if (bRequiresGenerator)
	{
		NavDataGenerator = MakeShareable(new FNavSvoGenerator(this));

		if (UNavigationSystemV1* NavigationSystem = Cast<UNavigationSystemV1>(World->GetNavigationSystem()))
		{
			RestrictBuildingToActiveTiles(NavigationSystem->IsActiveTilesGenerationEnabled());
		}
	}
}

UPrimitiveComponent* AGunfire3DNavData::ConstructRenderingComponent()
{
	return NewObject<UGunfire3DNavRenderingComponent>(this, TEXT("Gunfire3DNavDataRenderingComp"), RF_Transient);
}

FBox AGunfire3DNavData::GetBounds() const
{
	FBox Bounds(ForceInit);

	if (Octree.IsValid())
	{
		Octree->GetBounds(Bounds);
	}

	return Bounds;
}

void AGunfire3DNavData::CleanUp()
{
	Super::CleanUp();

	if (NavDataGenerator.IsValid())
	{
		NavDataGenerator->CancelBuild();
		NavDataGenerator.Reset();
	}

	DestroyOctree();
}

FPathFindingResult AGunfire3DNavData::FindPath(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query)
{
	SCOPE_CYCLE_COUNTER(STAT_FindPath);

	const AGunfire3DNavData* Self = Cast<const AGunfire3DNavData>(Query.NavData.Get());
	if (Self == nullptr || !Self->Octree.IsValid())
	{
		return ENavigationQueryResult::Error;
	}

	// Resolve the path to be filled. This could be an existing path that was passed along
	// with the query however, if that is missing, a new path instance will be created.
	FNavPathSharedPtr SharedPathPtr = Query.PathInstanceToFill;
	FGunfire3DNavPath* NavPath = (SharedPathPtr != nullptr) ? SharedPathPtr->CastPath<FGunfire3DNavPath>() : nullptr;
	if (NavPath != nullptr)
	{
		NavPath->ResetForRepath();
	}
	else
	{
		// No path has been instance has been provided so create a new one.
		SharedPathPtr = Self->CreatePathInstance<FGunfire3DNavPath>(Query);
		NavPath = SharedPathPtr->CastPath<FGunfire3DNavPath>();
	}

	if (NavPath == nullptr)
	{
		return ENavigationQueryResult::Error;
	}

	const FNavigationQueryFilter& ResolvedQueryFilter = Self->ResolveFilterRef(Query.QueryFilter);
	const FGunfire3DNavQueryFilter* QueryFilterImpl = StaticCast<const FGunfire3DNavQueryFilter*>(ResolvedQueryFilter.GetImplementation());
	const FVector AdjustedEndLocation = ResolvedQueryFilter.GetAdjustedEndLocation(Query.EndLocation);
	const uint32 MaxSearchNodes = ResolvedQueryFilter.GetMaxSearchNodes();
	const FVector NodeQueryExtent = Self->GetDefaultQueryExtent();

	FVector StartLocation, EndLocation;

	// Pass the query flags along to the path so we know what parameters from which it was
	// generated.
	NavPath->ApplyFlags(Query.NavDataFlags);

	// Use a node query to find the best open location for the specified start and end
	// locations
	FNavSvoNodeQuery NodeQuery(*Self->Octree, MaxSearchNodes, NodeQueryExtent);
	const FSvoNodeLink StartNodeLink = NodeQuery.FindClosestNode(Query.StartLocation, &StartLocation);
	if (!StartNodeLink.IsValid())
	{
		return ENavigationQueryResult::Fail;
	}
	const FSvoNodeLink EndNodeLink = NodeQuery.FindClosestNode(AdjustedEndLocation, &EndLocation);
	if (!EndNodeLink.IsValid())
	{
		return ENavigationQueryResult::Fail;
	}

	FGunfire3DNavPathQueryResults& PathQueryResults = NavPath->GetGenerationInfo();
	FNavSvoPathQuery PathQuery(*Self->Octree, MaxSearchNodes);
	const bool bPathFound = PathQuery.FindPath(StartNodeLink, EndNodeLink, Query.CostLimit, *QueryFilterImpl, PathQueryResults);
	if (!bPathFound)
	{
		return ENavigationQueryResult::Fail;
	}

	// If we were unable to reach the goal and partial path aren't allowed, return
	// failure.
	if (PathQueryResults.IsPartial() && !Query.bAllowPartialPaths)
	{
		return ENavigationQueryResult::Fail;
	}
	
	// Copy all nodes of the successful path to the output path
	TArray<FNavPathPoint>& PathPoints = NavPath->GetPathPoints();
	{
		PathPoints.Reserve(FMath::Min(2u, PathQueryResults.PathNodeCount + 2));

		// The path query will only have the portal points along the corridor for the path
		// so add the requested start point first.
		PathPoints.Add(FNavPathPoint(StartLocation, StartNodeLink.GetID()));

		// Add all points between the start and end location
		PathPoints.Append(PathQueryResults.PathPortalPoints);

		// If this is a partial path, mark it as such. Otherwise, copy the end location to
		// the path nodes as it will not have been a part of the search query.
		if (PathQueryResults.IsPartial())
		{
			NavPath->SetIsPartial(true);

			// This means path finding algorithm reached node pool limit. This can mean
			// that the resulting path is way off.
			NavPath->SetSearchReachedLimit(PathQueryResults.RanOutOfNodes());
		}
		else
		{
			PathPoints.Add(FNavPathPoint(EndLocation, EndNodeLink.GetID()));
		}
	}

	// Clean up the path so it no longer contains duplicate nodes along each line segment.
	FNavSvoUtils::CleanUpPath(PathPoints);

	// Tighten up the path to be more direct
	if (NavPath->WantsStringPulling())
	{
		FNavSvoUtils::StringPullPath(*Self->Octree, PathPoints);
	}

	// Smooth the path
	if (NavPath->WantsSmoothing())
	{
		// NOTE: Hard coding these values for now until I find a good place for them to
		// live.
		FNavSvoUtils::SmoothPath(*Self->Octree, PathPoints, 0.5f /* Centripetal */, 3 /* Iterations */);
	}

	// Mark that this path is ready to be used.
	NavPath->MarkReady();

	// Build and return the final result
	FPathFindingResult RetVal(ENavigationQueryResult::Success);
	RetVal.Path = SharedPathPtr;
	return RetVal;
}

bool AGunfire3DNavData::GetNodeLocation(NavNodeRef NodeRef, FVector& OutLocation) const
{
	if (Octree.IsValid())
	{
		const FSvoNodeLink NodeLink(NodeRef);
		return Octree->GetLocationForLink(NodeLink, OutLocation);
	}

	return false;
}

bool AGunfire3DNavData::GetNodeBounds(NavNodeRef NodeRef, FBox& OutBounds) const
{
	if (Octree.IsValid())
	{
		const FSvoNodeLink NodeLink(NodeRef);
		return Octree->GetBoundsForLink(NodeLink, OutBounds);
	}

	return false;
}

NavNodeRef AGunfire3DNavData::GetNodeAtLocation(const FVector& Location) const
{
	if (Octree.IsValid())
	{
		const FSvoNodeLink NodeLink = Octree->GetLinkForLocation(Location);
		return NodeLink.GetID();
	}

	return INVALID_NAVNODEREF;
}

NavNodeRef AGunfire3DNavData::FindClosestNode(const FVector& Origin, const FVector& QueryExtent, FSharedConstNavQueryFilter QueryFilter) const
{
	if (Octree.IsValid())
	{
		// Resolve the query filter
		const FNavigationQueryFilter& ResolvedQueryFilter = ResolveFilterRef(QueryFilter);
		const uint32 MaxSearchNodes = ResolvedQueryFilter.GetMaxSearchNodes();
		const FVector NodeQueryExtent = GetDefaultQueryExtent();

		FNavSvoNodeQuery NodeQuery(*Octree, MaxSearchNodes, NodeQueryExtent);
		const FSvoNodeLink NodeLink = NodeQuery.FindClosestNode(Origin);

		return NodeLink.GetID();
	}

	return INVALID_NAVNODEREF;
}

NavNodeRef AGunfire3DNavData::FindClosestReachableNode(const FVector& Origin, float MaxDistance, FSharedConstNavQueryFilter QueryFilter) const
{
	if (Octree.IsValid())
	{
		// Resolve the query filter
		const FNavigationQueryFilter& ResolvedQueryFilter = ResolveFilterRef(QueryFilter);
		const FGunfire3DNavQueryFilter* QueryFilterImpl = StaticCast<const FGunfire3DNavQueryFilter*>(ResolvedQueryFilter.GetImplementation());
		const uint32 MaxSearchNodes = ResolvedQueryFilter.GetMaxSearchNodes();
		const FVector NodeQueryExtent = GetDefaultQueryExtent();

		FGunfire3DNavQueryResults QueryResults;
		FNavSvoNodeQuery NodeQuery(*Octree, MaxSearchNodes, NodeQueryExtent);
		const FSvoNodeLink NodeLink = NodeQuery.FindClosestReachableNode(Origin, MaxDistance, *QueryFilterImpl, QueryResults);

		return NodeLink.GetID();
	}

	return INVALID_NAVNODEREF;
}

NavNodeRef AGunfire3DNavData::FindRandomReachableNode(const FVector& Origin, float MaxDistance, FSharedConstNavQueryFilter QueryFilter) const
{
	if (Octree.IsValid())
	{
		// Resolve the query filter
		const FNavigationQueryFilter& ResolvedQueryFilter = ResolveFilterRef(QueryFilter);
		const FGunfire3DNavQueryFilter* QueryFilterImpl = StaticCast<const FGunfire3DNavQueryFilter*>(ResolvedQueryFilter.GetImplementation());
		const uint32 MaxSearchNodes = ResolvedQueryFilter.GetMaxSearchNodes();
		const FVector NodeQueryExtent = GetDefaultQueryExtent();

		FGunfire3DNavQueryResults QueryResults;
		FNavSvoNodeQuery NodeQuery(*Octree, MaxSearchNodes, NodeQueryExtent);
		const FSvoNodeLink NodeLink = NodeQuery.FindRandomReachableNode(Origin, MaxDistance , *QueryFilterImpl, QueryResults);

		return NodeLink.GetID();
	}

	return INVALID_NAVNODEREF;
}

bool AGunfire3DNavData::GatherReachableNodes(const FVector& Origin, float MaxDistance, TArray<NavNodeRef>& OutResult, FSharedConstNavQueryFilter QueryFilter) const
{
	if (Octree.IsValid())
	{
		// Resolve the query filter
		const FNavigationQueryFilter& ResolvedQueryFilter = ResolveFilterRef(QueryFilter);
		const FGunfire3DNavQueryFilter* QueryFilterImpl = StaticCast<const FGunfire3DNavQueryFilter*>(ResolvedQueryFilter.GetImplementation());
		const uint32 MaxSearchNodes = ResolvedQueryFilter.GetMaxSearchNodes();
		const FVector NodeQueryExtent = GetDefaultQueryExtent();

		auto OnNodeVisited = [&OutResult](NavNodeRef NavNode) -> bool
		{
			OutResult.Add(NavNode);
			return true;
		};

		FGunfire3DNavQueryResults QueryResults;
		FNavSvoNodeQuery NodeQuery(*Octree, MaxSearchNodes, NodeQueryExtent);
		const bool bQueryStatus = NodeQuery.SearchReachableNodes(Origin, MaxDistance, OnNodeVisited, *QueryFilterImpl, QueryResults);
		return bQueryStatus;
	}
	
	return false;
}

bool AGunfire3DNavData::ForEachReachableNode(const FVector& Origin, float MaxDistance, TFunction<bool(NavNodeRef)> Lambda, FSharedConstNavQueryFilter QueryFilter) const
{
	if (Octree.IsValid())
	{
		// Resolve the query filter
		const FNavigationQueryFilter& ResolvedQueryFilter = ResolveFilterRef(QueryFilter);
		const FGunfire3DNavQueryFilter* QueryFilterImpl = StaticCast<const FGunfire3DNavQueryFilter*>(ResolvedQueryFilter.GetImplementation());
		const uint32 MaxSearchNodes = ResolvedQueryFilter.GetMaxSearchNodes();
		const FVector NodeQueryExtent = GetDefaultQueryExtent();

		FGunfire3DNavQueryResults QueryResults;
		FNavSvoNodeQuery NodeQuery(*Octree, MaxSearchNodes, NodeQueryExtent);
		const bool bQueryStatus = NodeQuery.SearchReachableNodes(Origin, MaxDistance, Lambda, *QueryFilterImpl, QueryResults);

		return bQueryStatus;
	}

	return false;
}

bool AGunfire3DNavData::IsLocationWithinGenerationBounds(const FVector& Location) const
{
	if (const FNavSvoGenerator* Generator = GetNavSvoGenerator())
	{
		const TArray<FBox>& InclusionBounds = Generator->GetInclusionBounds();
		for (const FBox& Bounds : InclusionBounds)
		{
			if (Bounds.IsInsideOrOn(Location))
			{
				return true;
			}
		}
	}

	return false;
}

void AGunfire3DNavData::SetGenerationBoostMode(bool Enabled)
{
	if (bGenerationBoostMode != Enabled)
	{
#if PROFILE_SVO_GENERATION
		FPlatformMisc::LowLevelOutputDebugString(*FString::Printf(TEXT("[%s][%3llu] Boost %s\n"),
			*FDateTime::UtcNow().ToString(TEXT("%Y.%m.%d-%H.%M.%S:%s")), GFrameCounter % 1000,
			Enabled ? TEXT("enabled") : TEXT("disabled")));
#endif

		bGenerationBoostMode = Enabled;
	}
}

bool AGunfire3DNavData::IsNodeWithinGenerationBounds(NavNodeRef NodeRef) const
{
	if (const FNavSvoGenerator* Generator = GetNavSvoGenerator())
	{
		FBox NodeBounds;
		GetNodeBounds(NodeRef, NodeBounds);

		const TArray<FBox>& InclusionBounds = Generator->GetInclusionBounds();
		return FGunfire3DNavigationUtils::AABBIntersectsAABBs(NodeBounds, InclusionBounds);
	}

	return false;
}

bool AGunfire3DNavData::TestPath(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query, int32* NumVisitedNodes)
{
	SCOPE_CYCLE_COUNTER(STAT_TestPath);

	const AGunfire3DNavData* Self = Cast<const AGunfire3DNavData>(Query.NavData.Get());
	if (Self == nullptr || !Self->Octree.IsValid())
	{
		return false;
	}

	const FNavigationQueryFilter& ResolvedQueryFilter = Self->ResolveFilterRef(Query.QueryFilter);
	const FGunfire3DNavQueryFilter* QueryFilterImpl = StaticCast<const FGunfire3DNavQueryFilter*>(ResolvedQueryFilter.GetImplementation());
	const FVector AdjustedEndLocation = ResolvedQueryFilter.GetAdjustedEndLocation(Query.EndLocation);
	const uint32 MaxSearchNodes = ResolvedQueryFilter.GetMaxSearchNodes();
	const FVector NodeQueryExtent = Self->GetDefaultQueryExtent();

	// Use a node query to find the best open location for the specified start and end
	// locations
	FNavSvoNodeQuery NodeQuery(*Self->Octree, MaxSearchNodes, NodeQueryExtent);
	const FSvoNodeLink StartNodeLink = NodeQuery.FindClosestNode(Query.StartLocation);
	if (!StartNodeLink.IsValid())
	{
		return false;
	}
	const FSvoNodeLink EndNodeLink = NodeQuery.FindClosestNode(AdjustedEndLocation);
	if (!EndNodeLink.IsValid())
	{
		return false;
	}

	FGunfire3DNavPathQueryResults PathQueryResults;
	FNavSvoPathQuery PathQuery(*Self->Octree, MaxSearchNodes);
	const bool bPathFound = PathQuery.TestPath(StartNodeLink, EndNodeLink, Query.CostLimit, *QueryFilterImpl, PathQueryResults);

	if (NumVisitedNodes != nullptr)
	{
		*NumVisitedNodes = PathQueryResults.NumNodesVisited;
	}

	return bPathFound;
}

bool AGunfire3DNavData::NeedsRebuild() const
{
	bool bLooksLikeNeeded = !HasValidOctree();
	
	if (NavDataGenerator.IsValid())
	{
		bLooksLikeNeeded |= NavDataGenerator->GetNumRemaningBuildTasks() > 0;
	}

	return bLooksLikeNeeded;
}

void AGunfire3DNavData::EnsureBuildCompletion()
{
	Super::EnsureBuildCompletion();

	// NOTE: This has been taken from ARecastNavMesh::EnsureBuildCompletion()
	//
	// Doing this as a safety net solution due to UE-20646, which was basically a result
	// of random over-releasing of default filter's shared pointer (it seemed). We might
	// have time to get back to this time some time in next 3 years :D
	RecreateDefaultFilter();
}

bool AGunfire3DNavData::SupportsRuntimeGeneration() const
{
	// NOTE: This has been taken from ARecastNavMesh::SupportsRuntimeGeneration()
	//
	// Generator should be disabled for Static navigation data
	return (RuntimeGeneration != ERuntimeGenerationType::Static);
}

bool AGunfire3DNavData::SupportsStreaming() const
{
	// NOTE: This has been taken from ARecastNavMesh::SupportsStreaming()
	//
	// Actually nothing prevents us to support streaming with dynamic generation Right now
	// streaming in sub-level causes navmesh to build itself, so no point to stream tiles
	// in
	return (RuntimeGeneration != ERuntimeGenerationType::Dynamic);
}

void AGunfire3DNavData::OnStreamingLevelAdded(ULevel* InLevel, UWorld* InWorld)
{
	if (SupportsStreaming())
	{
		UNavSvoStreamingData* NavDataChunk = GetStreamingLevelData(InLevel);
		if (NavDataChunk != nullptr)
		{
			check(Octree.IsValid());

			UNavSvoStreamingData* NavSvoStreamingData = Cast<UNavSvoStreamingData>(NavDataChunk);
			if (ensure(NavSvoStreamingData))
			{
				if (FEditableSvo* StreamedSvo = NavSvoStreamingData->GetOctree())
				{
					//Octree->EmplaceTiles(StreamedSvo);
				}
			}
		}
	}
}

void AGunfire3DNavData::OnStreamingLevelRemoved(ULevel* InLevel, UWorld* InWorld)
{
	if (SupportsStreaming())
	{
		UNavSvoStreamingData* NavDataChunk = GetStreamingLevelData(InLevel);
		if (NavDataChunk != nullptr)
		{
			check(Octree.IsValid());

			UNavSvoStreamingData* NavSvoStreamingData = Cast<UNavSvoStreamingData>(NavDataChunk);
			if (ensure(NavSvoStreamingData))
			{
				if (FEditableSvo* StreamedSvo = NavSvoStreamingData->GetOctree())
				{
					//Octree->RemoveMatchingTiles(*StreamedSvo);
				}
			}
		}
	}
}

UNavSvoStreamingData* AGunfire3DNavData::GetStreamingLevelData(const ULevel* InLevel) const
{
	FName ThisName = GetFName();
	int32 ChunkIndex = InLevel->NavDataChunks.IndexOfByPredicate([&](UNavigationDataChunk* Chunk)
	{
		return Chunk->NavigationDataName == ThisName;
	});

	UNavSvoStreamingData* StreamingData = (ChunkIndex != INDEX_NONE) ? Cast<UNavSvoStreamingData>(InLevel->NavDataChunks[ChunkIndex]) : nullptr;
	if (StreamingData != nullptr)
	{
		StreamingData->Level = InLevel;
	}

	return StreamingData;
}

void AGunfire3DNavData::OnGenerationComplete()
{
	UWorld* World = GetWorld();
	if (IsValid(World))
	{
#if WITH_EDITOR
		// Create navigation data containers for levels that can be streamed in.
		if (!World->IsGameWorld())
		{
			const auto& Levels = World->GetLevels();
			for (auto Level : Levels)
			{
				// Persistent levels should not contain streaming data
				if (Level->IsPersistentLevel())
				{
					continue;
				}

				UNavSvoStreamingData* StreamingData = GetStreamingLevelData(Level);
				bool bShouldClear = (StreamingData != nullptr);

				if (SupportsStreaming())
				{
					TArray<FIntVector> TileCoords;
					TArray<FBox> LevelNavBounds = GetNavigableBoundsInLevel(Level);
					if (Octree.IsValid())
					{
						Octree->GetTileCoords(LevelNavBounds, TileCoords);
					}

					// Create new chunk only if we have something to save in it
					if (TileCoords.Num())
					{
						// Create a new streaming data chunk if this level doesn't already
						// have one.
						if (StreamingData == nullptr)
						{
							StreamingData = NewObject<UNavSvoStreamingData>(Level);
							StreamingData->NavigationDataName = GetFName();
							Level->NavDataChunks.Add(StreamingData);
						}

						{
							check(Octree.IsValid());

							UNavSvoStreamingData* NavSvoStreamingData = Cast<UNavSvoStreamingData>(StreamingData);
							check(NavSvoStreamingData);

							const FSvoConfig& Config = Octree->GetConfig();
							FSvoConfig NewConfig(Config);

							FEditableSvo* DestOctree = NavSvoStreamingData->EnsureOctree(NewConfig);
							if (ensure(DestOctree))
							{
								DestOctree->CopyTilesFrom(*Octree, TileCoords, false /* bPreserveNeighborLinks */);
							}
						}

						StreamingData->MarkPackageDirty();
						bShouldClear = false;
					}
				}

				// If existing streaming data was found for this level but we now have
				// nothing to store, delete the existing data.
				if (bShouldClear)
				{
					StreamingData->ReleaseData();
					StreamingData->MarkPackageDirty();
					Level->NavDataChunks.Remove(StreamingData);
				}
			}
		}

		RequestDrawingUpdate(true /* bForce */);
#endif // WITH_EDITOR

		if (UNavigationSystemV1* NavigationSystem = Cast<UNavigationSystemV1>(World->GetNavigationSystem()))
		{
			NavigationSystem->OnNavigationGenerationFinished(*this);
		}
	}
}

void AGunfire3DNavData::RestrictBuildingToActiveTiles(bool bInRestrictBuildingToActiveTiles)
{
	if (!Octree.IsValid())
	{
		return;
	}

	if (FNavSvoGenerator* Generator = GetNavSvoGenerator())
	{
		Generator->RestrictBuildingToActiveTiles(bInRestrictBuildingToActiveTiles);
	}
}

FNavSvoGenerator* AGunfire3DNavData::GetNavSvoGenerator()
{
	return static_cast<FNavSvoGenerator*>(GetGenerator());
}

const FNavSvoGenerator* AGunfire3DNavData::GetNavSvoGenerator() const
{
	return static_cast<const FNavSvoGenerator*>(GetGenerator());
}

void AGunfire3DNavData::UpdateDrawing()
{
#if !UE_BUILD_SHIPPING
	UGunfire3DNavRenderingComponent* NavDataRenderComp = Cast<UGunfire3DNavRenderingComponent>(RenderingComp);
	if (NavDataRenderComp != nullptr && NavDataRenderComp->IsVisible() && (NavDataRenderComp->IsForcingUpdate() || UGunfire3DNavRenderingComponent::IsNavigationShowFlagSet(GetWorld())))
	{
		RenderingComp->MarkRenderStateDirty();
	}
#endif // UE_BUILD_SHIPPING
}

void AGunfire3DNavData::RequestDrawingUpdate(bool bForce /* = false */)
{
#if !UE_BUILD_SHIPPING
	if (bForce || UGunfire3DNavRenderingComponent::IsNavigationShowFlagSet(GetWorld()))
	{
		if (bForce)
		{
			UGunfire3DNavRenderingComponent* NavRenderingComp = Cast<UGunfire3DNavRenderingComponent>(RenderingComp);
			if (NavRenderingComp)
			{
				NavRenderingComp->ForceUpdate();
			}
		}

		DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.Requesting nav volume redraw"),
			STAT_FSimpleDelegateGraphTask_RequestingNavDataRedraw,
			STATGROUP_TaskGraphTasks);

		FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
			FSimpleDelegateGraphTask::FDelegate::CreateUObject(this, &ThisClass::UpdateDrawing),
			GET_STATID(STAT_FSimpleDelegateGraphTask_RequestingNavDataRedraw), NULL, ENamedThreads::GameThread);
	}
#endif // !UE_BUILD_SHIPPING
}

void AGunfire3DNavData::GetSupportedAreaClasses(TArray<TWeakObjectPtr<UClass>>& Areas) const
{
	for (const FSupportedAreaData& Area : SupportedAreas)
	{
		if (const UClass* Class = Area.AreaClass)
		{
			Areas.Add(const_cast<UClass*>(Class));
		}
	}
}

void AGunfire3DNavData::SetOctree(FEditableSvoSharedPtr InOctree)
{
	if (Octree != InOctree)
	{
		DestroyOctree();
		Octree = InOctree;
	}
}

void AGunfire3DNavData::DestroyOctree()
{
	Octree = nullptr;
}

bool AGunfire3DNavData::HasValidOctree() const
{
	return (Octree.IsValid() && Octree->IsValid());
}

bool AGunfire3DNavData::NavDataRaycast(const ANavigationData* Self, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier /* = nullptr */)
{
	SCOPE_CYCLE_COUNTER(STAT_Raycast);

	const AGunfire3DNavData* OurSelf = Cast<const AGunfire3DNavData>(Self);
	check(OurSelf);

	if (OurSelf == nullptr || !OurSelf->Octree.IsValid())
	{
		HitLocation = RayStart;
		return true;
	}

	Gunfire3DNavigation::FRaycastResult Result;
	OurSelf->Octree->Raycast(RayStart, RayEnd, Result);

	HitLocation = Result.HitLocation;
	return Result.HasHit();
}

void AGunfire3DNavData::BatchRaycast(TArray<FNavigationRaycastWork>& Workload, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier /* = nullptr */) const
{
	SCOPE_CYCLE_COUNTER(STAT_BatchRaycast);

	if (!Octree.IsValid())
	{
		return;
	}

	for (FNavigationRaycastWork& Work : Workload)
	{
		Gunfire3DNavigation::FRaycastResult Result;
		Octree->Raycast(Work.RayStart, Work.RayEnd, Result);

		if (Result.HasHit())
		{
			Work.bDidHit = true;
			Work.HitLocation = Result.HitLocation;
		}
	}
}

bool AGunfire3DNavData::FindMoveAlongSurface(const FNavLocation& StartLocation, const FVector& TargetPosition, FNavLocation& OutLocation, FSharedConstNavQueryFilter QueryFilter /* = nullptr */, const UObject* Querier /* = nullptr */) const
{
	ensureMsgf(false, TEXT("Function not implemented (%s)"), TEXT("AGunfire3DNavData::FindMoveAlongSurface"));

	return false;
}

FNavLocation AGunfire3DNavData::GetRandomPoint(FSharedConstNavQueryFilter QueryFilter /* = nullptr */, const UObject* Querier /* = nullptr */) const
{
	ensureMsgf(false, TEXT("Function not implemented (%s)"), TEXT("AGunfire3DNavData::GetRandomPoint"));

	return FNavLocation();
}

bool AGunfire3DNavData::GetRandomPointInNavigableRadius(const FVector& Origin, float Radius, FNavLocation& OutResult, FSharedConstNavQueryFilter QueryFilter /* = nullptr */, const UObject* Querier /* = nullptr */) const
{
	ensureMsgf(false, TEXT("Function not implemented (%s)"), TEXT("AGunfire3DNavData::GetRandomPointInNavigableRadius"));

	return false;
}

// TODO: This is only finding the random point inside of a single node. It, instead, needs
// to find a random node and then find a random point inside that node. - JMM
bool AGunfire3DNavData::GetRandomReachablePointInRadius(const FVector& Origin, float Radius, FNavLocation& OutResult, FSharedConstNavQueryFilter QueryFilter /* = nullptr */, const UObject* Querier /* = nullptr */) const
{
	SCOPE_CYCLE_COUNTER(STAT_GetRandomReachablePointInRadius);

	if (Octree.IsValid() && Radius >= 0.f)
	{
		// Resolve the query filter
		const FNavigationQueryFilter& ResolvedQueryFilter = ResolveFilterRef(QueryFilter);
		const uint32 MaxSearchNodes = ResolvedQueryFilter.GetMaxSearchNodes();
		const FVector NodeQueryExtent = GetDefaultQueryExtent();

		FNavSvoNodeQuery NodeQuery(*Octree, MaxSearchNodes, NodeQueryExtent);
		const FSvoNodeLink NodeLink = NodeQuery.FindClosestNode(Origin);

		if (NodeLink.IsValid())
		{
			FVector Location;
			if (NodeQuery.FindRandomPointInNode(NodeLink, Location))
			{
				OutResult.Location = Location;
				OutResult.NodeRef = NodeLink.GetID();
				return true;
			}
		}
	}

	return false;
}

bool AGunfire3DNavData::ProjectPoint(const FVector& Point, FNavLocation& OutLocation, const FVector& QueryExtent, FSharedConstNavQueryFilter QueryFilter /* = nullptr */, const UObject* Querier /* = nullptr */) const
{
	SCOPE_CYCLE_COUNTER(STAT_ProjectPoint);

	if (Octree.IsValid())
	{
		// Resolve the query filter
		const FNavigationQueryFilter& ResolvedQueryFilter = ResolveFilterRef(QueryFilter);
		const uint32 MaxSearchNodes = ResolvedQueryFilter.GetMaxSearchNodes();
		const FVector NodeQueryExtent = GetDefaultQueryExtent();

		FNavSvoNodeQuery NodeQuery(*Octree, MaxSearchNodes, NodeQueryExtent);
		const FSvoNodeLink NodeLink = NodeQuery.FindClosestNode(Point);

		if (NodeLink.IsValid())
		{
			FVector Location;
			if (NodeQuery.FindClosestPointInNode(NodeLink, Point, Location))
			{
				OutLocation.Location = Location;
				OutLocation.NodeRef = NodeLink.GetID();
				return true;
			}
		}
	}

	return false;
}

void AGunfire3DNavData::BatchProjectPoints(TArray<FNavigationProjectionWork>& Workload, FSharedConstNavQueryFilter QueryFilter /* = nullptr */, const UObject* Querier /* = nullptr */) const
{
	SCOPE_CYCLE_COUNTER(STAT_BatchProjectPoints);

	if (Octree.IsValid())
	{
		for (FNavigationProjectionWork& Work : Workload)
		{
			Work.bResult = ProjectPoint(Work.Point, Work.OutLocation, Work.ProjectionLimit.GetExtent(), QueryFilter);
		}
	}
}

void AGunfire3DNavData::BatchProjectPoints(TArray<FNavigationProjectionWork>& Workload, const FVector& Extent, FSharedConstNavQueryFilter QueryFilter /* = nullptr */, const UObject* Querier /* = nullptr */) const
{
	SCOPE_CYCLE_COUNTER(STAT_BatchProjectPoints);

	if (Octree.IsValid())
	{
		for (FNavigationProjectionWork& Work : Workload)
		{
			Work.bResult = ProjectPoint(Work.Point, Work.OutLocation, Extent, QueryFilter);
		}
	}
}

ENavigationQueryResult::Type AGunfire3DNavData::CalcPathCost(const FVector& PathStart, const FVector& PathEnd, float& OutPathCost, FSharedConstNavQueryFilter QueryFilter /* = nullptr */, const UObject* Querier /* = nullptr */) const
{
	float TmpPathLength = 0.f;
	ENavigationQueryResult::Type Result = CalcPathLengthAndCost(PathStart, PathEnd, TmpPathLength, OutPathCost, QueryFilter, Querier);
	return Result;
}

ENavigationQueryResult::Type AGunfire3DNavData::CalcPathLength(const FVector& PathStart, const FVector& PathEnd, float& OutPathLength, FSharedConstNavQueryFilter QueryFilter /* = nullptr */, const UObject* Querier /* = nullptr */) const
{
	float TmpPathCost = 0.f;
	ENavigationQueryResult::Type Result = CalcPathLengthAndCost(PathStart, PathEnd, OutPathLength, TmpPathCost, QueryFilter, Querier);
	return Result;
}

ENavigationQueryResult::Type AGunfire3DNavData::CalcPathLengthAndCost(const FVector& PathStart, const FVector& PathEnd, float& OutPathLength, float& OutPathCost, FSharedConstNavQueryFilter QueryFilter /* = nullptr */, const UObject* Querier /* = nullptr */) const
{
	SCOPE_CYCLE_COUNTER(STAT_CalcPathLengthAndCost);

	if (!Octree.IsValid())
	{
		return ENavigationQueryResult::Error;
	}

	// Resolve the query filter
	const FNavigationQueryFilter& ResolvedQueryFilter = ResolveFilterRef(QueryFilter);
	const FGunfire3DNavQueryFilter* QueryFilterImpl = StaticCast<const FGunfire3DNavQueryFilter*>(ResolvedQueryFilter.GetImplementation());
	const FVector AdjustedEndLocation = ResolvedQueryFilter.GetAdjustedEndLocation(PathEnd);
	const uint32 MaxSearchNodes = ResolvedQueryFilter.GetMaxSearchNodes();
	const FVector NodeQueryExtent = GetDefaultQueryExtent();
	const float CostLimit = MAX_flt;

	FVector StartLocation, EndLocation;

	// Use a node query to find the best open location for the specified start and end
	// locations
	FNavSvoNodeQuery NodeQuery(*Octree, MaxSearchNodes, NodeQueryExtent);
	const FSvoNodeLink StartNodeLink = NodeQuery.FindClosestNode(PathStart, &StartLocation);
	if (!StartNodeLink.IsValid())
	{
		return ENavigationQueryResult::Fail;
	}
	const FSvoNodeLink EndNodeLink = NodeQuery.FindClosestNode(PathEnd, &EndLocation);
	if (!EndNodeLink.IsValid())
	{
		return ENavigationQueryResult::Fail;
	}

	FGunfire3DNavPathQueryResults PathQueryResults;
	FNavSvoPathQuery PathQuery(*Octree, MaxSearchNodes);
	const bool bPathFound = PathQuery.FindPath(StartNodeLink, EndNodeLink, CostLimit, *QueryFilterImpl, PathQueryResults);

	OutPathLength = PathQueryResults.PathLength;
	OutPathCost = PathQueryResults.PathCost;

	return bPathFound ? ENavigationQueryResult::Success : ENavigationQueryResult::Fail;
}

bool AGunfire3DNavData::IsNodeRefValid(NavNodeRef NodeRef) const
{
	if (!Octree.IsValid() || NodeRef == INVALID_NAVNODEREF)
	{
		return false;
	}

	FSvoNodeLink NodeLink(NodeRef);
	const bool bSuccess = (Octree->GetNodeFromLink(NodeRef) != nullptr);
	return bSuccess;
}

bool AGunfire3DNavData::DoesNodeContainLocation(NavNodeRef NodeRef, const FVector& WorldSpaceLocation) const
{
	FBox NodeBounds;
	GetNodeBounds(NodeRef, NodeBounds);
	return NodeBounds.IsInsideOrOn(WorldSpaceLocation);
}

#if !UE_BUILD_SHIPPING

uint32 AGunfire3DNavData::LogMemUsed() const
{
	const uint32 SuperMemUsed = Super::LogMemUsed();

	uint32 MemUsed = 0;
	if (HasValidOctree())
	{
		MemUsed += Octree->GetMemUsed();
	}

	UE_LOG(LogNavigation, Warning, TEXT("%s: AGunfire3DNavData: %u\n    self: %d"), *GetName(), MemUsed, sizeof(AGunfire3DNavData));

	return MemUsed + SuperMemUsed;
}

#endif // !UE_BUILD_SHIPPING