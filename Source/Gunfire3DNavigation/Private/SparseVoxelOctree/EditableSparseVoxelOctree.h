// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "SparseVoxelOctree.h"

#include "Containers/StaticBitArray.h"

class FSvoTile;

class GUNFIRE3DNAVIGATION_API FEditableSvo : public FSparseVoxelOctree, public TSharedFromThis<FEditableSvo, ESPMode::ThreadSafe>
{
	typedef FSparseVoxelOctree Super;

public:
	FEditableSvo(const FSvoConfig& InConfig);

	//~ Begin FSparseVoxelOctree Interface
	// Destroys all data within the octree
	virtual void Reset() override;

	// Saves octree data to an archive
	virtual void Serialize(FArchive& Ar) override;

	// Returns the amount of memory used by the octree
	uint32 GetMemUsed() const override;
	//~ End FSparseVoxelOctree Interface

	// Adds a tile to this octree from another octree
	void CopyTile(const FSvoTile& SourceTile, bool bPreserveNeighborLinks);
	void CopyTilesFrom(const FSparseVoxelOctree& Source, const TArray<FIntVector>& TileCoords, bool bPreserveNeighborLinks);

	// Adds a tile to this octree.  This is move operation and NOT a copy.
	void AssumeTile(FSvoTile& SourceTile, bool bPreserveNeighborLinks);
	void AssumeTilesFrom(FSparseVoxelOctree& Source, bool bPreserveNeighborLinks);

	// Removes a tile, ensuring all neighboring links are marked dirty so they can
	// be re-linked.
	void RemoveTile(const FSvoNodeLink& Link);
	void RemoveTileAtCoord(const FIntVector& Coord);
	void RemoveMatchingTiles(const FSparseVoxelOctree& Source);

	void BeginBatchEdit() { ++BatchEditRefCounter; }
	void EndBatchEdit();
	bool IsBatchEditing() const { return (BatchEditRefCounter > 0); }

protected:
	void MarkNeighborsDirty(const FSvoNodeLink& Link);

	// Validates all nodes in the 'DirtyNodes' array.  This will handle re-linking neighbors,
	// etc. for any modified nodes.
	void FinalizeNodes();

	// Determines if there are any nodes that yet to be finalized.  This check can be used
	// to determine if the data is valid for normal operation.
	bool AreNodesFinalized() const { return (DirtyNodes.Num() == 0); }

protected:
	// When the octree is modified, nodes that need to be refreshed (e.g. neighbors
	// re-linked) are added to this list and should be processed once any processing has
	// completed.
	TMap<FSvoNodeLink, ESvoNeighborFlags> DirtyNodes;

	int32 BatchEditRefCounter;
};

typedef TSharedPtr<FEditableSvo, ESPMode::ThreadSafe> FEditableSvoSharedPtr;