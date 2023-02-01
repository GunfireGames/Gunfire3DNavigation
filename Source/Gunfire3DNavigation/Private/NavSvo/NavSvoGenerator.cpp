// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "NavSvoGenerator.h"

#include "Gunfire3DNavigationUtils.h"
#include "NavSvoTileGenerator.h"
#include "Gunfire3DNavData.h"
#include "SparseVoxelOctree/SparseVoxelOctreeUtils.h"

#include "NavigationSystem.h"

// Profiling stats
DECLARE_CYCLE_STAT(TEXT("ProcessTileTasks (FNavSvoGenerator)"), STAT_NavSvoGenerator_ProcessTileTasks, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("AddGeneratedTiles (FNavSvoGenerator)"), STAT_NavSvoGenerator_AddGeneratedTiles, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("MarkDirtyTiles (FNavSvoGenerator)"), STAT_NavSvoGenerator_MarkDirtyTiles, STATGROUP_Gunfire3DNavigation);

// Memory stats
DECLARE_MEMORY_STAT(TEXT("Pending Tiles (FNavSvoGenerator)"), STAT_NavSvoGenerator_PendingTiles, STATGROUP_Gunfire3DNavigation);
DECLARE_MEMORY_STAT(TEXT("Building Tiles (FNavSvoGenerator)"), STAT_NavSvoGenerator_BuildingTiles, STATGROUP_Gunfire3DNavigation);

//////////////////////////////////////////////////////////////////////////////////////////
//
// Tunables for optimizing generation time. Any tweaking of these should be combined with
// profiling the results on all supported platforms with PROFILE_SVO_GENERATION to ensure
// the new numbers are producing good results.
//

// This is really just for debugging purposes, if you want to force the generation onto
// the main thread. The performance hit is way too terrible to ever actually want to do this.
TAutoConsoleVariable<int32> CVarAsyncTileBuildingEnabled(TEXT("NavSvo.AsyncTileBuilding"), 1, TEXT("Enables/disables async svo tile building."), ECVF_Cheat);

// The maximum number of generation tasks to have running at once. Ideally this should be
// at least 2, so we can have a new task gathering triangles on the main thread while
// another task is doing work on a thread.
TAutoConsoleVariable<int32> CVarNavSvoMaxTasks(TEXT("NavSvo.MaxTasks"), 2, TEXT("Maximum number of tile generator tasks."), ECVF_Cheat);

// Boost mode is for when we're in a loading screen, and we can afford to take a hit to
// the framerate by running more tasks at once.
TAutoConsoleVariable<int32> CVarNavSvoBoostMaxTasks(TEXT("NavSvo.BoostMaxTasks"), 4, TEXT("Maximum number of tile generator tasks when boosted."), ECVF_Cheat);

// The main tunable for tweaking. The is a soft cap on the number of triangles per task,
// so if a task is close to this number it could go a ways over if the next tile is dense.
// In general you want to tune this so there are enough triangles that a task takes long
// enough to make up for the time spent queuing the triangles for the next task, but not
// larger than necessary since each triangle is another 72 bytes of memory allocated to
// buffer the data for the thread.
//
// 10K triangles:  0.69 MB
// 100K triangles: 6.9 MB
TAutoConsoleVariable<int32> CVarMaxTrisPerTask(TEXT("NavSvo.MaxTrisPerTask"), 10000, TEXT("Max number of triangles processed by each task."), ECVF_Cheat);

// A soft cap on how long in ms we can spend doing work on the main thread per frame. This
// should be kept as low as possible to avoid affecting the framerate, while still being
// high enough to launch new tasks without leaving them pending for too long.
TAutoConsoleVariable<float> CVarNavSvoMaxTimePerTick(TEXT("NavSvo.MaxTimePerTick"), 0.5f, TEXT("Amount of time in ms we can spend each frame doing work on the main thread."), ECVF_Cheat);
TAutoConsoleVariable<float> CVarNavSvoBoostMaxTimePerTick(TEXT("NavSvo.BoostMaxTimePerTick"), 5.0f, TEXT("Amount of time in ms we can spend each frame doing work on the main thread when boosted."), ECVF_Cheat);

// If a task has been pending for this many frames, force it to start. This is for the
// case where we're gathering a bunch of tiles with barely any collision geo in them so
// we're not hitting our triangle cap, so we eventually get the task started.
TAutoConsoleVariable<int32> CVarMaxPendingTicks(TEXT("NavSvo.MaxPendingTicks"), 5, TEXT("Max number of frames a task can gather more tiles before it is forced to start"), ECVF_Cheat);

//////////////////////////////////////////////////////////////////////////////////////////

FNavSvoGenerator::FNavSvoGenerator(AGunfire3DNavData* InNavDataActor)
	: NavDataActor(InNavDataActor)
	, Config(FVector::ZeroVector, InNavDataActor)
	, bRestrictBuildingToActiveTiles(false)
#if STATS
	, PendingTiles(GET_STATID(STAT_NavSvoGenerator_PendingTiles))
	, RunningGenerators(GET_STATID(STAT_NavSvoGenerator_BuildingTiles))
#endif
{
	// Gather all current navigation bounds
	UpdateNavigationBounds();

	bool bOctreeNeedsConstruction = true;
	if (NavDataActor->HasValidOctree())
	{
		bOctreeNeedsConstruction = !GetOctree()->GetConfig().IsCompatibleWith(Config);
	}

	// Conditionally construct the octree
	if (bOctreeNeedsConstruction)
	{
		ConstructOctree();
	}
}

void FNavSvoGenerator::ConstructOctree()
{
	// There is should not be any active build tasks
	CancelBuild();

	// Recreate Config
	Config = FNavSvoGeneratorConfig(FVector::ZeroVector, NavDataActor);

	FEditableSvoSharedPtr Octree = MakeShareable(new FEditableSvo(Config));
	NavDataActor->SetOctree(Octree);
}

UWorld* FNavSvoGenerator::GetWorld() const
{
	return NavDataActor->GetWorld();
}

FEditableSvo* FNavSvoGenerator::GetOctree()
{
	return NavDataActor->GetOctree();
}

const FEditableSvo* FNavSvoGenerator::GetOctree() const
{
	return NavDataActor->GetOctree();
}

bool FNavSvoGenerator::RebuildAll()
{
	// Recreate octree
	NavDataActor->DestroyOctree();
	ConstructOctree();

	// If rebuilding all no point in keeping "old" invalidated areas
	TArray<FNavigationDirtyArea> DirtyAreas;
	for (const FBox& AreaBounds : InclusionBounds)
	{
		FNavigationDirtyArea DirtyArea(AreaBounds, ENavigationDirtyFlag::All | ENavigationDirtyFlag::NavigationBounds);
		DirtyAreas.Add(DirtyArea);
	}

	if (DirtyAreas.Num())
	{
		MarkDirtyTiles(DirtyAreas);
	}
	else
	{
		// There are no navigation bounds to build, probably nav volume was resized and we just need to update debug draw
		NavDataActor->RequestDrawingUpdate();
	}

	return true;
}

void FNavSvoGenerator::RebuildDirtyAreas(const TArray<FNavigationDirtyArea>& DirtyAreas)
{
	FEditableSvo* Octree = GetOctree();
	if (Octree == nullptr)
	{
		ConstructOctree();
		RebuildAll();
	}
	else
	{
		MarkDirtyTiles(DirtyAreas);
	}
}

void FNavSvoGenerator::CancelBuild()
{
	// Remove all pending tiles
	PendingTiles.Empty();

	// If we were in the process of filling a generator, delete it
	if (PendingGenerator != nullptr)
	{
		delete PendingGenerator;
		PendingGenerator = nullptr;
	}

	// Cancel all build tasks
	for (FRunningGenerator& RunningGenerator : RunningGenerators)
	{
		if (RunningGenerator.AsyncTask)
		{
			RunningGenerator.AsyncTask->EnsureCompletion();
			delete RunningGenerator.AsyncTask;
			RunningGenerator.AsyncTask = nullptr;
		}
	}
	RunningGenerators.Empty();
}

bool FNavSvoGenerator::IsBuildInProgressCheckDirty() const
{
	return ((RunningGenerators.Num() > 0) ||
			(CompletedGenerators.Num() > 0) ||
			(PendingGenerator != nullptr) ||
			(PendingTiles.Num() > 0));
}

bool FNavSvoGenerator::HasDirtyAreas(const FBox& Bounds) const
{
	// If this generator isn't currently building then it won't have anything to test
	// against
	if (!IsBuildInProgressCheckDirty())
	{
		return false;
	}

	const FBox ClampedBounds = Bounds.Overlap(Bounds);

	// Check the overall generation bounds as an early-out
	if (!ClampedBounds.IsValid)
	{
		return false;
	}

	const FIntVector MinCoord = Config.LocationToCoord(ClampedBounds.Min, Config.GetTileResolution());
	const FIntVector MaxCoord = Config.LocationToCoord(ClampedBounds.Max, Config.GetTileResolution());

	// Check Pending Tiles
	for (const FPendingTile& PendingTile : PendingTiles)
	{
		if (FSvoUtils::IsCoordInBounds(PendingTile, MinCoord, MaxCoord))
		{
			return true;
		}
	}

	// Check generating tiles
	return IsCoordGenerating(MinCoord, MaxCoord);
}

bool FNavSvoGenerator::IsCoordGenerating(const FIntVector& MinCoord, const FIntVector& MaxCoord) const
{
	// Check the pending generator
	if (PendingGenerator != nullptr)
	{
		if (PendingGenerator->ContainsTileInBounds(MinCoord, MaxCoord))
		{
			return true;
		}
	}

	// Check running generators
	for (const FRunningGenerator& RunningGenerator : RunningGenerators)
	{
		if (RunningGenerator.Generator.ContainsTileInBounds(MinCoord, MaxCoord))
		{
			return true;
		}
	}

	// Checked completed generators too. Until they've had their results added they're
	// still considered generating.
	for (const TSharedRef<FNavSvoTileGenerator>& CompletedGenerator : CompletedGenerators)
	{
		if (CompletedGenerator->ContainsTileInBounds(MinCoord, MaxCoord))
		{
			return true;
		}
	}


	return false;
}

int32 FNavSvoGenerator::GetNumRemaningBuildTasks() const
{
	return PendingTiles.Num() + (PendingGenerator ? 1 : 0) + RunningGenerators.Num();
}

int32 FNavSvoGenerator::GetNumRunningBuildTasks() const
{
	return RunningGenerators.Num();
}

void FNavSvoGenerator::EnsureBuildCompletion()
{
	const bool bHasTasks = GetNumRemaningBuildTasks() > 0;

	do
	{
		TickBuildTasks(16);

		// Block until tasks are finished
		for (FRunningGenerator& RunningGenerator : RunningGenerators)
		{
			RunningGenerator.AsyncTask->EnsureCompletion();
		}
	} while (GetNumRemaningBuildTasks() > 0);

	// If there were tasks to be processed, redraw afterwards
	if (bHasTasks)
	{
		NavDataActor->RequestDrawingUpdate();
	}
}

void FNavSvoGenerator::TickAsyncBuild(float DeltaSeconds)
{
	if (NavDataActor == nullptr)
	{
		return;
	}

	// Determine the maximum allowed number of worker threads
	const int32 MaxTileGeneratorTasks = AGunfire3DNavData::IsGenerationBoostMode() ?
		CVarNavSvoBoostMaxTasks.GetValueOnGameThread() :
		CVarNavSvoMaxTasks.GetValueOnGameThread();

	const int32 NumTasksToSubmit = FMath::Max(0, MaxTileGeneratorTasks - GetNumRunningBuildTasks());

	int32 NumUpdateTiles = TickBuildTasks(NumTasksToSubmit);

	// If anything has changed, request a drawing update.
	if (NumUpdateTiles > 0)
	{
		NavDataActor->RequestDrawingUpdate();
	}
}

int32 FNavSvoGenerator::TickBuildTasks(const int32 MaxTasksToSubmit)
{
	SCOPE_CYCLE_COUNTER(STAT_NavSvoGenerator_ProcessTileTasks);

	FEditableSvo* Octree = GetOctree();
	check(Octree);

	// We should never be batch editing at this point.
	ensure(Octree->IsBatchEditing() == false);
	Octree->BeginBatchEdit();

	const bool bHasTasksAtStart = (GetNumRemaningBuildTasks() > 0);
	int32 NumUpdatedTiles = 0;

	if (PendingGenerator != nullptr)
	{
		++PendingGenerator->PendingTicks;
	}

	const double MaxTickMS = AGunfire3DNavData::IsGenerationBoostMode() ?
		CVarNavSvoBoostMaxTimePerTick.GetValueOnGameThread() :
		CVarNavSvoMaxTimePerTick.GetValueOnGameThread();
	const uint64 MaxCycles = FMath::CeilToInt(MaxTickMS / (FPlatformTime::GetSecondsPerCycle64() * 1000.0));
	const uint64 EndCycle = FPlatformTime::Cycles64() + MaxCycles;

	// Check the async tasks and add their generators to the list to have their results
	// added if complete.
	for (int32 RunningGeneratorIdx = RunningGenerators.Num() - 1; RunningGeneratorIdx >= 0; --RunningGeneratorIdx)
	{
		FRunningGenerator& RunningGenerator = RunningGenerators[RunningGeneratorIdx];
		check(RunningGenerator.AsyncTask);

		if (RunningGenerator.AsyncTask->IsDone())
		{
			// Add the generator to the list of completed generators to allow it to be
			// added to the main octree
			TSharedRef<FNavSvoTileGenerator>& TileGeneratorRef = RunningGenerator.AsyncTask->GetTask().TileGenerator;
			CompletedGenerators.Add(TileGeneratorRef);

			// Destroy the tile generator task
			delete RunningGenerator.AsyncTask;
			RunningGenerator.AsyncTask = nullptr;

			// Remove from the list of building tiles
			RunningGenerators.RemoveAtSwap(RunningGeneratorIdx, 1, false);
		}
	}

	const bool AddedTiles = (CompletedGenerators.Num() > 0);

	// Add all completed generator data
	if (AddedTiles)
	{
#if PROFILE_SVO_GENERATION
		for (TSharedRef<FNavSvoTileGenerator>& Generator : CompletedGenerators)
		{
			Generator->AddTicks++;
		}
#endif

		for (int32 i = 0; i < CompletedGenerators.Num(); ++i)
		{
			const bool AddedAllTiles = AddGeneratedTiles(*CompletedGenerators[i], EndCycle);

			if (AddedAllTiles)
			{
				CompletedGenerators.RemoveAt(i);
				--i;
			}

			// By default AddGeneratedTiles will always add at least one tile, so we can't
			// get into a case where we're stuck because our per-frame time is too low.
			// Only allow that for the first task though, if we're hitting the cap we
			// don't want every task to get a shot.
			if (FPlatformTime::Cycles64() >= EndCycle)
			{
				break;
			}
		}
	}

	// Submit new tasks. We do this after we've added any completed tiles since they share
	// the same timeout, and we'd rather get completed tasks added to the octree before we
	// kick off new ones.
	ProcessPendingTiles(Octree, MaxTasksToSubmit, EndCycle);

	// If the octree has been updated, finalize the nodes to complete neighbor links, etc.
	check(Octree->IsBatchEditing());
	Octree->EndBatchEdit();

#if !UE_BUILD_SHIPPING && SVO_VERIFY_NODES
	if (AddedTiles)
	{
		Octree->VerifyNodeData(true);
	}
#endif

	// Perform operations once the build is finished
	const bool bHasTasksAtEnd = (GetNumRemaningBuildTasks() > 0);
	if (bHasTasksAtStart && !bHasTasksAtEnd)
	{
		// Release memory, list could be quite big after map load
		if (PendingTiles.Num() == 0)
		{
			PendingTiles.Empty(32);
		}

		// Notify owner once all generation has completed.
		NavDataActor->OnGenerationComplete();
	}

	return NumUpdatedTiles;
}

void FNavSvoGenerator::ProcessPendingTiles(FEditableSvo* Octree, int32 MaxTasksToSubmit, const uint64 EndCycle)
{
	int32 NumSubmittedTasks = 0;
	int32 NumBuildingCoords = 0;
	uint64 GatherCyclesThisTick = 0;

	// Submit pending tile elements
	for (int32 PendingTileIdx = (PendingTiles.Num() - 1); PendingTileIdx >= 0; --PendingTileIdx)
	{
		FPendingTile& PendingTile = PendingTiles[PendingTileIdx];

		const bool PendingGeneratorFull =
			PendingGenerator != nullptr &&
			PendingGenerator->TriCount >= (uint32)CVarMaxTrisPerTask.GetValueOnGameThread();

		// If the pending generator is full and we can't queue any more tasks there's no
		// point in checking more tiles, just break out.
		if (PendingGeneratorFull && (NumSubmittedTasks >= MaxTasksToSubmit))
		{
			break;
		}

		// Don't submit this tile for generation if it's currently building.  It will be
		// submitted after the processing task completes.
		if (IsCoordGenerating(PendingTile, PendingTile))
		{
			++NumBuildingCoords;
			continue;
		}

		// If we don't already have a pending generator we're filling, create one now
		if (PendingGenerator == nullptr)
		{
			PendingGenerator = new FNavSvoTileGenerator(*this, Config);

#if PROFILE_SVO_GENERATION
			FPlatformMisc::LowLevelOutputDebugString(*FString::Printf(TEXT("[%s][%3llu] 0x%p created\n"),
				*FDateTime::UtcNow().ToString(TEXT("%Y.%m.%d-%H.%M.%S:%s")), GFrameCounter % 1000, PendingGenerator));
#endif
		}

		// If the pending generator isn't full, gather another tile
		if (!PendingGeneratorFull)
		{
			const uint64 GatherStartTime = FPlatformTime::Cycles64();

			// Copy all the geometry for this tile into the generator.
			if (!PendingGenerator->AddTile(PendingTile))
			{
				// In this case there isn't anything to build for this tile so we need to
				// be sure the main octree is updated to reflect this as it may have had data
				// previously.
				Octree->RemoveTileAtCoord(PendingTile);
			}

			const uint64 GatherCycles = FPlatformTime::Cycles64() - GatherStartTime;

			GatherCyclesThisTick += GatherCycles;
#if PROFILE_SVO_GENERATION
			PendingGenerator->GatherCycles += GatherCycles;
#endif

			// Pending tile should be dealt with at this point and can be removed.
			PendingTiles.RemoveAt(PendingTileIdx);
		}

		// We've spent more than our max gather time for this frame, stop queuing more tiles.
		if (FPlatformTime::Cycles64() >= EndCycle)
		{
			break;
		}

		// See if this generator is ready to go
		if (NumSubmittedTasks < MaxTasksToSubmit && TryRunPendingGenerator())
		{
			++NumSubmittedTasks;
		}

		// If we've submitted the maximum number of build tasks for this update, bail.
		if (NumSubmittedTasks >= MaxTasksToSubmit)
		{
			break;
		}
	}

	if (NumSubmittedTasks < MaxTasksToSubmit)
	{
		// This is for the case where we have pending tiles, but they're all for stuff
		// already queued up to build or building. In that case, we want to force start
		// the current pending task, if there is one.
		const bool AllPendingTilesBuilding = (PendingTiles.Num() == NumBuildingCoords);

		TryRunPendingGenerator(AllPendingTilesBuilding);
	}
}

bool FNavSvoGenerator::TryRunPendingGenerator(bool ForceStart)
{
	if (PendingGenerator == nullptr)
	{
		return false;
	}

	if (PendingGenerator->HasTiles())
	{	
		const bool IsGeneratorReady =
			ForceStart ||
			PendingGenerator->TriCount >= (uint32)CVarMaxTrisPerTask.GetValueOnGameThread() ||
			(PendingTiles.Num() == 0) ||
			(PendingGenerator->PendingTicks > (uint32)CVarMaxPendingTicks.GetValueOnGameThread());

		if (!IsGeneratorReady)
		{
			return false;
		}

		TSharedRef<FNavSvoTileGenerator> TileGeneratorRef = MakeShareable(PendingGenerator);
		PendingGenerator = nullptr;

		if (CVarAsyncTileBuildingEnabled.GetValueOnGameThread() > 0)
		{
			// Create a new async task and kick it off to start the generation process
			TUniquePtr<FNavSvoTileGeneratorTask> TileGenerationTask = MakeUnique<FNavSvoTileGeneratorTask>(TileGeneratorRef);
			TileGenerationTask->StartBackgroundTask();

			// Create a build tile and cache it off so we can keep track of its progress.
			FRunningGenerator TileGeneratorInfo(TileGeneratorRef.Get());
			TileGeneratorInfo.AsyncTask = TileGenerationTask.Release();
			RunningGenerators.Add(TileGeneratorInfo);
		}
		else
		{
			// Perform tile generation on the main thread
			TileGeneratorRef->DoWork();
			CompletedGenerators.Add(TileGeneratorRef);
		}

#if PROFILE_SVO_GENERATION
		FPlatformMisc::LowLevelOutputDebugString(*FString::Printf(TEXT("[%s][%3llu] 0x%p started\n"),
			*FDateTime::UtcNow().ToString(TEXT("%Y.%m.%d-%H.%M.%S:%s")), GFrameCounter % 1000,
			&TileGeneratorRef.Get()));
#endif

		return true;
	}
	else
	{
		delete PendingGenerator;
		PendingGenerator = nullptr;
		return false;
	}
}

bool FNavSvoGenerator::AddGeneratedTiles(FNavSvoTileGenerator& TileGenerator, const uint64 EndCycle)
{
	SCOPE_CYCLE_COUNTER(STAT_NavSvoGenerator_AddGeneratedTiles);

	bool AddedAllTiles = true;

#if PROFILE_SVO_GENERATION
	const uint64 AddStartCycle = FPlatformTime::Cycles64();
#endif

	FEditableSvo* Octree = GetOctree();

	while (FSvoTile* Tile = TileGenerator.GetNextGeneratedTile())
	{
		// If filtering specific tiles, make sure we don't add any that aren't in the list
		const bool bCanAddTile = WhitelistedTiles.Num() == 0 || IsTileWhitelisted(Tile->GetCoord());

		if (bCanAddTile)
		{
			Octree->AssumeTile(*Tile, true);

			if (FPlatformTime::Cycles64() >= EndCycle)
			{
				AddedAllTiles = false;
				break;
			}
		}
	}

#if PROFILE_SVO_GENERATION
	TileGenerator.AddCycles += FPlatformTime::Cycles64() - AddStartCycle;

	if (AddedAllTiles)
	{
		const uint64 TotalCycles = FPlatformTime::Cycles64() - TileGenerator.CreateCycle;

		FPlatformMisc::LowLevelOutputDebugString(*FString::Printf(TEXT("[%s][%3llu] 0x%p took %.2f ms [%d pending ticks, %d add ticks, %d tris (%d culled), %d tiles] (%.2f ms gather time, %.2f ms generate time, %.2f ms pad time, %.2f ms node time, %.2f ms add time))\n"),
			*FDateTime::UtcNow().ToString(TEXT("%Y.%m.%d-%H.%M.%S:%s")), GFrameCounter % 1000,
			&TileGenerator,
			FPlatformTime::ToMilliseconds64(TotalCycles),
			TileGenerator.PendingTicks,
			TileGenerator.AddTicks,
			TileGenerator.UsedTris,
			TileGenerator.TotalTris - TileGenerator.UsedTris,
			TileGenerator.NumTiles(),
			FPlatformTime::ToMilliseconds64(TileGenerator.GatherCycles),
			FPlatformTime::ToMilliseconds64(TileGenerator.GenerateCycles),
			FPlatformTime::ToMilliseconds64(TileGenerator.PadCycles),
			FPlatformTime::ToMilliseconds64(TileGenerator.FillCycles),
			FPlatformTime::ToMilliseconds64(TileGenerator.NodeCycles),
			FPlatformTime::ToMilliseconds64(TileGenerator.AddCycles)
		));
	}
#endif

	return AddedAllTiles;
}

void FNavSvoGenerator::OnNavigationBoundsChanged()
{
	UpdateNavigationBounds();

	// TODO: In the event looking up tiles every time we add/remove is too slow, we'll need
	//		 to restrict the nav svo to a set number of tiles.  Here we would make the
	//		 decision whether or not to destroy the current octree and recreating it with
	//		 the new number of tiles.
	if (!IsGameStaticNavData())
	{
		if (FEditableSvo* Octree = GetOctree())
		{
			if (!Octree->GetConfig().IsCompatibleWith(Config))
			{
				// Destroy the octree as it will need to be rebuilt
				NavDataActor->DestroyOctree();
			}
		}
	}
}

void FNavSvoGenerator::UpdateNavigationBounds()
{
	const UNavigationSystemV1* NavigationSystem = Cast<UNavigationSystemV1>(GetWorld()->GetNavigationSystem());

	if (!NavigationSystem)
	{
		return;
	}

	TotalNavBounds = FBox(ForceInit);

	// Collect bounding geometry
	if (NavigationSystem->ShouldGenerateNavigationEverywhere() == false)
	{
		if (NavDataActor)
		{
			TArray<FBox> SupportedBounds;
			NavigationSystem->GetNavigationBoundsForNavData(*NavDataActor, SupportedBounds);
			InclusionBounds.Reset(SupportedBounds.Num());

			for (const FBox& Bounds : SupportedBounds)
			{
				if (!FGunfire3DNavigationUtils::AABBsContainAABB(InclusionBounds, Bounds))
				{
					InclusionBounds.Add(Bounds);
					TotalNavBounds += Bounds;
				}
			}

			return;
		}
	}
	else
	{
		TotalNavBounds = NavigationSystem->GetWorldBounds();
		if (TotalNavBounds.IsValid)
		{
			InclusionBounds.Reset(1);
			InclusionBounds.Add(TotalNavBounds);
			return;
		}
	}

	InclusionBounds.Reset();
}

void FNavSvoGenerator::MarkDirtyTiles(const TArray<FNavigationDirtyArea>& DirtyAreas)
{
	SCOPE_CYCLE_COUNTER(STAT_NavSvoGenerator_MarkDirtyTiles);

	const bool bGameStaticNavData = IsGameStaticNavData();

	const FEditableSvo* Octree = GetOctree();
	const bool bOctreeHasTiles = (Octree->GetNumTiles() != 0);
	const bool bHasInclusionBounds = (InclusionBounds.Num() != 0);

	// If there aren't any inclusion bounds for this nav data type and the octree is empty,
	// therefore meaning we won't be clearing any tiles, just bail.
	if (!bHasInclusionBounds && !bOctreeHasTiles)
	{
		return;
	}

	FBox OctreeBounds;
	Octree->GetBounds(OctreeBounds);

	// Collect all nodes that need to be regenerated
	TSet<FPendingTile> DirtyTiles;
	for (const FNavigationDirtyArea& DirtyArea : DirtyAreas)
	{
		// Store flags for readability.
		const bool bIsGeometry = DirtyArea.HasFlag(ENavigationDirtyFlag::Geometry);
		const bool bIsDynamicModifier = DirtyArea.HasFlag(ENavigationDirtyFlag::DynamicModifier);
		const bool bIsNavigationBounds = DirtyArea.HasFlag(ENavigationDirtyFlag::NavigationBounds);

		// TODO: Agent height is generally used for modifiers which we don't currently support.
		//		 Adding this for posterity only.
		const bool bUseAgentHeight = DirtyArea.HasFlag(ENavigationDirtyFlag::UseAgentHeight);

		// For static navigation volumes, we don't allow the size to change, only the geometry
		// and/or modifiers
		if (bGameStaticNavData && bIsNavigationBounds)
		{
			continue;
		}

		// This flag is used to determine if this area needs to test each tile's bounds
		// that it overlaps against the inclusion bounds as an optimization to bypass
		// unnecessary tests. This will remain false if this area doesn't represent navigation
		// bounds and is fully encapsulated by an inclusion bound.
		bool bNeedsTileIntersectionTest = false;

		// Adjusted bounds represent any non-navigable area bounds that are trimmed to fit
		// within the current working set of navigable bounds.
		FBox AdjustedAreaBounds = DirtyArea.Bounds.ExpandBy(Config.BoundsPadding);

		if (bIsNavigationBounds)
		{
			if (!FGunfire3DNavigationUtils::AABBIntersectsAABBs(AdjustedAreaBounds, InclusionBounds))
			{
				// If this area isn't within the inclusion bounds set, then it will be nav
				// bounds which have been removed.  In that case, only process bounds which
				// intersect the octree itself.  Otherwise this will just process a bunch
				// of tiles we don't care about.

				// If the octree doesn't have any tiles, bail as there won't be any to remove
				if (!bOctreeHasTiles)
				{
					continue;
				}

				// As an early out, ensure the bounds are within the overall bounds of the octree
				if (!FGunfire3DNavigationUtils::AABBIntersectsAABB(OctreeBounds, AdjustedAreaBounds))
				{
					continue;
				}

				// At this point we know the box is at least nearby.

				// Now trim off any excess that isn't within the octree bounds area.
				AdjustedAreaBounds = FGunfire3DNavigationUtils::CalculateAABBOverlap(OctreeBounds, AdjustedAreaBounds);

				// TOOD: Would be more exact to try to get the tile count within these bounds
				//		 however that will only be efficient once the upper-portion of the
				//		 SVO is utilized for look-ups.  For now we will still process
				//		 some inconsequential tiles.
			}
		}
		else
		{
			// If this isn't an area which will be expanding the current navigation bounds,
			// then we need to trim back the box to only the intersection with the current
			// navigation bounds.  An example of where this matters is if the bounds represents
			// a change in geometry but NOT a change in navigation bounds.

			// As an early out, ensure the bounds are within the overall bounds of all navigable bounds
			if (!FGunfire3DNavigationUtils::AABBIntersectsAABB(TotalNavBounds, AdjustedAreaBounds))
			{
				continue;
			}

			// At this point we know the box is at least nearby.

			// Now trim off any excess that isn't within the total bounds area.
			AdjustedAreaBounds = FGunfire3DNavigationUtils::CalculateAABBOverlap(TotalNavBounds, AdjustedAreaBounds);

			// Next check if there are any overlaps with current navigable bounds
			if (!FGunfire3DNavigationUtils::AABBIntersectsAABBs(AdjustedAreaBounds, InclusionBounds))
			{
				continue;
			}

			// If this area isn't fully enclosed within an inclusion volume then we'll need
			// to test each tile that this area overlaps as there could be empty ones it
			// touches.  If it were to be fully enclosed then we know the area it resides
			// within will need to be or has already been fully built and there is no need
			// to exclude tiles that it overlaps.
			bNeedsTileIntersectionTest = !FGunfire3DNavigationUtils::AABBsContainAABB(InclusionBounds, AdjustedAreaBounds);
		}

		// Do not process invalid areas
		if (AdjustedAreaBounds.GetVolume() <= 0.0f)
		{
			continue;
		}

		// Determine which tiles this area overlaps
		FIntVector MinAreaCoord, MaxAreaCoord;
		FSvoUtils::GetCoordsForBounds(Config.GetSeedLocation(), AdjustedAreaBounds, Config.GetTileResolution(), MinAreaCoord, MaxAreaCoord);

		for (FCoordIterator CoordIter(MinAreaCoord, MaxAreaCoord); CoordIter; ++CoordIter)
		{
			FIntVector TileCoord = CoordIter.GetCoord();

			// Check if the tile is already pending.  If not, see if it passes the test to be added.
			FPendingTile* DirtyTile = DirtyTiles.Find(TileCoord);
			if (DirtyTile == nullptr)
			{
				// Test if the if the tile is active.  It is possible that building will be restricted to only the active tiles.
				// If not restricted then all tiles will pass this test.
				if (!IsTileWhitelisted(TileCoord))
				{
					continue;
				}

				// If this area isn't fully encapsulated by an inclusion bound then we need
				// to only allow tiles that are overlapping inclusion bounds to be processed.
				// 'bNeedsTileTest' will always be false for areas that are not navigable bounds.
				if (bNeedsTileIntersectionTest)
				{
					FVector TileLocation = FSvoUtils::CoordToLocation(Config.GetSeedLocation(), TileCoord, Config.GetTileResolution());
					FBox TileBounds = FBox::BuildAABB(TileLocation, Config.GetTileExtent());

					if (!FGunfire3DNavigationUtils::AABBIntersectsAABBs(TileBounds, InclusionBounds))
					{
						continue;
					}
				}

				// Create a new pending tile entry and add it to the list of dirty tiles
				FPendingTile NewDirtyTile(TileCoord);

				FSetElementId NewDirtyTileIdx = DirtyTiles.Add(NewDirtyTile);
				DirtyTile = &DirtyTiles[NewDirtyTileIdx];
			}
		}
	}

	if (DirtyTiles.Num() > 0)
	{
		// Merge all pending tiles into one container
		for (const FPendingTile& PendingTile : PendingTiles)
		{
			FPendingTile* DirtyTile = DirtyTiles.Find(PendingTile);
			if (!DirtyTile)
			{
				DirtyTiles.Add(PendingTile);
			}
		}

		// Dump results into array
		PendingTiles.Empty(DirtyTiles.Num());
		for (const FPendingTile& DirtyTile : DirtyTiles)
		{
			PendingTiles.Add(DirtyTile);
		}

		// Now sort all pending tiles based on distance from players
		SortPendingTiles();
	}
}

void FNavSvoGenerator::SortPendingTiles()
{
	TArray<FVector> SeedLocations;

	// Collect all player positions to be used as seeds for sorting.
	if (UWorld* World = GetWorld())
	{
		FGunfire3DNavigationUtils::GetPlayerLocations(World, SeedLocations);
	}

	// If no players are found, use the world original as the seed.
	if (SeedLocations.Num() == 0)
	{
		SeedLocations.Add(FVector::ZeroVector);
	}

	// Calculate shortest distances between tiles and players
	for (FPendingTile& PendingTile : PendingTiles)
	{
		FVector TileCenter = FSvoUtils::CoordToLocation(Config.GetSeedLocation(), PendingTile, Config.GetTileResolution());

		for (FVector SeedLocation : SeedLocations)
		{
			const float DistSq = FVector::DistSquared(TileCenter, SeedLocation);
			if (DistSq < PendingTile.SeedDistance)
			{
				PendingTile.SeedDistance = DistSq;
			}
		}
	}

	// nearest tiles should be at the end of the list
	PendingTiles.Sort();
}

bool FNavSvoGenerator::IsGameStaticNavData() const
{
	return (GetWorld()->IsGameWorld() && NavDataActor->GetRuntimeGenerationMode() != ERuntimeGenerationType::Dynamic);
}

bool FNavSvoGenerator::IsTileWhitelisted(const FIntVector& TileCoord) const
{
	return (!bRestrictBuildingToActiveTiles || WhitelistedTiles.Contains(TileCoord));
}

void FNavSvoGenerator::RestrictBuildingToActiveTiles(bool bInRestrictBuildingToActiveTiles)
{
	if (bRestrictBuildingToActiveTiles != bInRestrictBuildingToActiveTiles)
	{
		bRestrictBuildingToActiveTiles = bInRestrictBuildingToActiveTiles;

		// Clear the active tile list
		WhitelistedTiles.Empty();

		// Rebuild the active tile list
		if (bRestrictBuildingToActiveTiles)
		{
			if (const FEditableSvo* Octree = GetOctree())
			{
				for (const FSvoTile& Tile : Octree->GetTiles())
				{
					WhitelistedTiles.Add(Tile.GetCoord());
				}
			}
		}
	}
}

uint32 FNavSvoGenerator::LogMemUsed() const
{
	const uint32 SuperMemUsed = Super::LogMemUsed();

	uint32 MemUsed = 0;
	MemUsed += WhitelistedTiles.GetAllocatedSize();
	MemUsed += InclusionBounds.GetAllocatedSize();
	MemUsed += PendingTiles.GetAllocatedSize();
	MemUsed += RunningGenerators.GetAllocatedSize();

	UE_LOG(LogNavigation, Warning, TEXT("    FNavSvoGenerator: %u\n    self: %d"), MemUsed, sizeof(FNavSvoGenerator));

	return SuperMemUsed + MemUsed;
}
