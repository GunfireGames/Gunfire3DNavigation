// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "NavSvoGeneratorConfig.h"
#include "NavSvoCollider.h"
#include "SparseVoxelOctree/EditableSparseVoxelOctree.h"

class FNavDataGenerator;
class FNavSvoGenerator;

class GUNFIRE3DNAVIGATION_API FNavSvoTileGenerator : public FNoncopyable, public FGCObject
{
public:
	FNavSvoTileGenerator(const class FNavSvoGenerator& InParent, const FNavSvoGeneratorConfig& InConfig);

	// Builds the octree for the tile
	void DoWork();

	// Returns whether 'DoWork' has completed
	bool IsWorkComplete() const { return bIsComplete; }

	// Adds a tile to the list of tiles to be built
	bool AddTile(const FIntVector& TileCoord);

	// Determines whether this task has any tiles to build.
	bool HasTiles() const { return (Tiles.Num() > 0); }

	int32 NumTiles() const { return Tiles.Num(); }

	bool ContainsTileInBounds(const FIntVector& MinTileCoord, const FIntVector& MaxTileCoord) const;

	// Used after work is complete on this generator to get tiles one by one. When this
	// returns null there are no more tiles.
	FSvoTile* GetNextGeneratedTile();

	//~ Begin FGCObject Interface
	// Collects all objects we depend on staying in memory.
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FNavSvoTileGenerator"); }
	//~ End FGCObject Interface

private:
	struct FIntBox
	{
		FIntVector Min;
		FIntVector Max;

		FORCEINLINE bool IsInsideOrOn(const FIntVector& In) const
		{
			return ((In.X >= Min.X) && (In.X <= Max.X) && (In.Y >= Min.Y) && (In.Y <= Max.Y) && (In.Z >= Min.Z) && (In.Z <= Max.Z));
		}
	};

	// Struct containing generation data relevant to a specific tile
	struct FTileGenerationData : public TSharedFromThis<FTileGenerationData>
	{
		// Coordinates of the tile being generated
		FIntVector TileCoord;

		// Minimum coordinate of the tile being generated
		FVector TileMin;

		// World space bounds of the tile, clipped by any overlapping navigation bounds
		// then expanded to include any additional space we need to consider due to
		// padding. If the tile overlaps multiple navigation bounds this can include space
		// we won't end up using in the final tile data, but that's such a rare case it's
		// not worth the complexity of dealing with this as an array of bounds.
		FBox GatherBounds;

		// Same as GatherBounds, but in voxel space
		FIntBox FillBounds;

		// The voxel coordinates of areas in the tile where we overlap navigation bounds.
		TArray<FIntBox> VoxelBounds;

		// Interface the octree uses to gather collision data
		FNavigationOctreeCollider CollisionInterface;
	};

	void BuildPaddingOffsetCodes();

	// Builds the tile
	bool FillVoxels(FTileGenerationData& Tile, TBitArray<>& Voxels) const;
	bool FillTriangles(FTileGenerationData& Tile, const FVector& TileMin, TBitArray<>& Voxels) const;
	bool FillBlockers(const FTileGenerationData& Tile, const FVector& TileMin, TBitArray<>& Voxels) const;

	template<typename VectorType>
	VectorType SwizzleCoord(const VectorType& Coord, const FIntVector& AxisMap) const
	{
		return VectorType(Coord[AxisMap.X], Coord[AxisMap.Y], Coord[AxisMap.Z]);
	};

	template<typename VectorType>
	VectorType UnswizzleCoord(const VectorType& Coord, const FIntVector& AxisMap) const
	{
		VectorType Ret;
		Ret[AxisMap.X] = Coord.X;
		Ret[AxisMap.Y] = Coord.Y;
		Ret[AxisMap.Z] = Coord.Z;
		return Ret;
	};

	bool RasterizeTriangle(FTileGenerationData& Tile, TArrayView<FVector> Verts, FIntVector AxisMap, TBitArray<>& Voxels) const;

	// Pads out all existing voxels by a specified amount
	void PadVoxels(const FTileGenerationData& Tile, const TBitArray<>& Voxels, TBitArray<>& PaddedVoxels) const;

	void CreateTileFromVoxels(const FTileGenerationData& Tile, const TBitArray<>& Voxels, FSvoTile& TileOut) const;

	// Removes unnecessary nodes where all the children are in the same state as the parent
	void OptimizeTiles(FTileGenerationData& Tile);

	// Helper for OptimizeTiles
	ENodeState CollapseUnneededNodes(FSvoTile& Tile, FSvoNode& Node) const;

private:
	// SVO generator that called this tile generator
	TWeakPtr<const FNavDataGenerator, ESPMode::ThreadSafe> ParentWeakPtr;

	// Config as it was when the parent generator started this generator
	FNavSvoGeneratorConfig Config;

	// Flag is set once 'DoWork' completes
	FThreadSafeBool bIsComplete;

	TArray<uint32> PaddingOffsetCodes;

	TStatArray<TSharedRef<FTileGenerationData>> Tiles;

	TStatArray<FSvoTile> GeneratedTiles;

	int32 NextGeneratedTile = 0;

public:
	uint32 PendingTicks = 0;
	uint32 TriCount = 0;

#if PROFILE_SVO_GENERATION
	uint64 CreateCycle = 0;
	uint64 GatherCycles = 0;
	uint64 AddCycles = 0;
	uint64 AddTicks = 0;
	mutable uint32 TotalTris = 0;
	mutable uint32 UsedTris = 0;
	mutable uint64 GenerateCycles = 0;
	mutable uint64 PadCycles = 0;
	mutable uint64 FillCycles = 0;
	mutable uint64 NodeCycles = 0;
#endif
};

// An async task class which wraps FNavSvoTileGenerator
class GUNFIRE3DNAVIGATION_API FNavSvoTileGeneratorWrapper : public FNonAbandonableTask
{
public:
	TSharedRef<FNavSvoTileGenerator> TileGenerator;

public:
	FNavSvoTileGeneratorWrapper(TSharedRef<FNavSvoTileGenerator> InTileGenerator)
		: TileGenerator(InTileGenerator)
	{}

	void DoWork()
	{
		TileGenerator->DoWork();
	}

	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FNavSvoTileGenerator, STATGROUP_ThreadPoolAsyncTasks);
	}
};

typedef FAsyncTask<FNavSvoTileGeneratorWrapper> FNavSvoTileGeneratorTask;