// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "AI/NavDataGenerator.h"
#include "NavSvoGeneratorConfig.h"
#include "StatArray.h"

class AGunfire3DNavData;
class FNavSvoTileGenerator;

// Class that handles generation of the svo for a navigation area
class GUNFIRE3DNAVIGATION_API FNavSvoGenerator : public FNavDataGenerator
{
	typedef FNavDataGenerator Super;

public:
	FNavSvoGenerator(AGunfire3DNavData* InNavDataActor);

	UWorld* GetWorld() const;
	const class FEditableSvo* GetOctree() const;
	AGunfire3DNavData* GetNavDataActor() const { return NavDataActor; }

	//////////////////////////////////////////////////////////////////////////////////////
	//
	// Begin FNavDataGenerator Interface
	//

	// Rebuilds all known navigation data
	virtual bool RebuildAll() override;

	// Asks generator to update navigation affected by DirtyAreas
	virtual void RebuildDirtyAreas(const TArray<FNavigationDirtyArea>& DirtyAreas) override;

	// Cancels build, may block until current running async tasks are finished
	virtual void CancelBuild() override;

	// Determines whether this generator is performing navigation building actions at the moment
	virtual bool IsBuildInProgressCheckDirty() const override;

	// Returns true if there are any dirty areas in the specified bounds
	virtual bool HasDirtyAreas(const FBox& Bounds) const override;

	// Returns number of remaining tasks till build is complete
	virtual int32 GetNumRemaningBuildTasks() const override;

	// Returns number of currently running tasks
	virtual int32 GetNumRunningBuildTasks() const override;

	// Blocks until build is complete
	virtual void EnsureBuildCompletion() override;

	// Handles updating async build tasks
	virtual void TickAsyncBuild(float DeltaSeconds) override;

	// Notification received when the bounds of the associated navigation area have changed
	virtual void OnNavigationBoundsChanged() override;

	// Determines how much memory the generator is currently using (for debug only)
	virtual uint32 LogMemUsed() const override;

#if ENABLE_VISUAL_LOG
	// Asks the generator to export all navigation data to the specified file
	virtual void ExportNavigationData(const FString& FileName) const override { /* TODO: */ }

	// Asks the generator to provide a snapshot of the current state via the Visual Log
	virtual void GrabDebugSnapshot(struct FVisualLogEntry* Snapshot, const FBox& BoundingBox, const FName& CategoryName, ELogVerbosity::Type Verbosity) const override { /* TODO: */ }
#endif
	//
	// End FNavDataGenerator Interface
	//
	//////////////////////////////////////////////////////////////////////////////////////

	// Total navigable area box, sum of all navigation volumes bounding boxes
	FBox GetTotalBounds() const { return TotalNavBounds; }

	// All navigation area bounding boxes
	const TArray<FBox>& GetInclusionBounds() const { return InclusionBounds; }

	// Whether navigation data is static; does not support rebuild from geometry
	bool IsGameStaticNavData() const;

	// If 'bInRestrictBuildingToActiveTiles' is true, restricts all built tiles to only
	// the ones that are already active in the octree.
	void RestrictBuildingToActiveTiles(bool bInRestrictBuildingToActiveTiles);

private:
	// Creates a new octree and assigns it to the 'Owner'
	void ConstructOctree();

	// Grabs a non-const reference to the octree
	class FEditableSvo* GetOctree();

	// Starts new tasks and processes results from finished tasks
	int32 TickBuildTasks(const int32 MaxTasksToSubmit);

	void ProcessPendingTiles(FEditableSvo* Octree, int32 MaxTasksToSubmit, const uint64 EndCycle);

	// If we have a pending generator and it's ready to go, start it
	bool TryRunPendingGenerator(bool ForceStart = false);

	// Adds all tiles from a generator to their respective svo
	bool AddGeneratedTiles(FNavSvoTileGenerator& TileGenerator, const uint64 EndCycle);

	// Updates cached list of navigation bounds
	void UpdateNavigationBounds();

	// Marks nodes within the octree that are affected by the specified areas
	void MarkDirtyTiles(const TArray<FNavigationDirtyArea>& DirtyAreas);

	// Sorts all pending tiles by distance from players
	void SortPendingTiles();

	// Determines if the specified tile is in the set of tiles that should be built.
	// NOTE: Can be restricted.  See 'RestrictBuildingToActiveTiles'.
	bool IsTileWhitelisted(const FIntVector& TileCoord) const;

	bool IsCoordGenerating(const FIntVector& MinCoord, const FIntVector& MaxCoord) const;

private:
	AGunfire3DNavData* NavDataActor = nullptr;

	// Parameters used during generation
	FNavSvoGeneratorConfig Config;

	// Total bounding box that includes all volumes, in unreal units
	FBox TotalNavBounds;

	// If true, all building will be restricted to the tiles in the 'ActiveTiles' list.
	bool bRestrictBuildingToActiveTiles;

	// If 'bRestrictBuildingToActiveTiles' is true, then only the tiles in this list can
	// be built.
	TSet<FIntVector> WhitelistedTiles;

	// Bounding geometry definition
	TArray<FBox> InclusionBounds;

	// Pending tiles are tiles that are in queue to be rebuilt.
	struct FPendingTile : FIntVector
	{
		float SeedDistance;

		FPendingTile(const FIntVector& InCoord) : FIntVector(InCoord), SeedDistance(MAX_flt) {}
		bool operator<(const FPendingTile& Other) const { return Other.SeedDistance < SeedDistance; }
	};
	TStatArray<FPendingTile> PendingTiles;

	// The next generator, that we are currently gathering geometry for
	FNavSvoTileGenerator* PendingGenerator = nullptr;

	// Tiles being built asynchronously
	struct FRunningGenerator
	{
		FRunningGenerator(const FNavSvoTileGenerator& InGenerator)
			: Generator(InGenerator)
		{}

		const FNavSvoTileGenerator& Generator;
		FAsyncTask<class FNavSvoTileGeneratorWrapper>* AsyncTask;
	};

	TStatArray<FRunningGenerator> RunningGenerators;

	// Tile generators which have completed
	TArray<TSharedRef<FNavSvoTileGenerator>> CompletedGenerators;
};