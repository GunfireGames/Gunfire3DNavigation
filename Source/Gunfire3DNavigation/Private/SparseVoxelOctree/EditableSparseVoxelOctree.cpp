// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "EditableSparseVoxelOctree.h"

#include "Gunfire3DNavigationCustomVersion.h"
#include "Gunfire3DNavigationUtils.h"
#include "SparseVoxelOctreeUtils.h"

#include "AI/NavigationSystemBase.h"

// Profiling stats
DECLARE_CYCLE_STAT(TEXT("CopyTile (FEditableSvo)"), STAT_FEditableSvo_CopyTile, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("AssumeTile (FEditableSvo)"), STAT_FEditableSvo_AssumeTile, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("RemoveTile (FEditableSvo)"), STAT_FEditableSvo_RemoveTile, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("EnsureNodeExistsAtLocation (FEditableSvo)"), STAT_FEditableSvo_EnsureNodeExistsAtLocation, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("FinalizeNodes (FEditableSvo)"), STAT_FEditableSvo_FinalizeNodes, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("FinalizeNodes : Sort (FEditableSvo)"), STAT_FEditableSvo_FinalizeNodes_Sort, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("PadVoxels (FEditableSvo)"), STAT_FEditableSvo_PadVoxels, STATGROUP_Gunfire3DNavigation);

FEditableSvo::FEditableSvo(const FSvoConfig& InConfig)
	: Super(InConfig)
	, BatchEditRefCounter(0)
{
}

void FEditableSvo::Reset()
{
	Super::Reset();

	// Clear all dirty nodes
	DirtyNodes.Empty();
}

void FEditableSvo::Serialize(FArchive& Ar)
{
	// Write our custom version to the archive.  This will only occur during a save.
	Ar.UsingCustomVersion(FGunfire3DNavigationCustomVersion::GUID);

	if (Ar.IsSaving())
	{
		// Force nodes to finalize if saving so links are saved properly
		FinalizeNodes();
	}

	Super::Serialize(Ar);
}

void FEditableSvo::CopyTile(const FSvoTile& SourceTile, bool bPreserveNeighborLinks)
{
	SCOPE_CYCLE_COUNTER(STAT_FEditableSvo_CopyTile);

	FSvoTile* DestTile = EnsureTileActiveAtCoord(SourceTile.GetCoord());
	if (DestTile != nullptr)
	{
		BeginBatchEdit();
		{
			const FSvoNodeLink TileNodeLink = DestTile->GetSelfLink();

			// Copy tile data
			DestTile->Copy(SourceTile);

			// Link the neighbors for the source tile so we can mark them as dirty
			LinkNeighborsForNodeHierarchically(TileNodeLink, bPreserveNeighborLinks);

			// We need to mark all neighbors as dirty so they (and possibly their children)
			// can have their neighbors re-linked.
			MarkNeighborsDirty(TileNodeLink);
		}
		EndBatchEdit();
	}
}

void FEditableSvo::CopyTilesFrom(const FSparseVoxelOctree& SourceOctree, const TArray<FIntVector>& TileCoords, bool bPreserveNeighborLinks)
{
	if (Config.IsCompatibleWith(SourceOctree.GetConfig()))
	{
		BeginBatchEdit();
		{
			for (const FIntVector& TileCoord : TileCoords)
			{
				if (const FSvoTile* SourceTile = SourceOctree.GetTileAtCoord(TileCoord))
				{
					CopyTile(*SourceTile, bPreserveNeighborLinks);
				}
			}
		}
		EndBatchEdit();
	}
}

void FEditableSvo::AssumeTile(FSvoTile& SourceTile, bool bPreserveNeighborLinks)
{
	SCOPE_CYCLE_COUNTER(STAT_FEditableSvo_AssumeTile);

	FSvoTile* DestTile = EnsureTileActiveAtCoord(SourceTile.GetCoord());
	if (DestTile != nullptr)
	{
		BeginBatchEdit();
		{
			const FSvoNodeLink TileNodeLink = DestTile->GetSelfLink();

			// Take the data from the source tile and store it in our tree
			DestTile->Assume(SourceTile);

			// Link the neighbors for the source tile so we can mark them as dirty
			LinkNeighborsForNodeHierarchically(TileNodeLink, bPreserveNeighborLinks);

			// We need to mark all neighbors as dirty so they (and possibly their children)
			// can have their neighbors re-linked.
			MarkNeighborsDirty(TileNodeLink);
		}
		EndBatchEdit();
	}
}

void FEditableSvo::AssumeTilesFrom(FSparseVoxelOctree& Source, bool bPreserveNeighborLinks)
{
	if (Source.GetConfig().IsCompatibleWith(Config))
	{
		BeginBatchEdit();
		{
			for (FSvoTile& Tile : Source.GetTiles())
			{
				AssumeTile(Tile, bPreserveNeighborLinks);
			}
		}
		EndBatchEdit();
	}
}

void FEditableSvo::RemoveTile(const FSvoNodeLink& NodeLink)
{
	SCOPE_CYCLE_COUNTER(STAT_FEditableSvo_RemoveTile);

	if (NodeLink.IsValid())
	{
		if (ensure(NodeLink.LayerIdx == Config.GetTileLayerIndex()))
		{
			BeginBatchEdit();
			{
				// We need to mark all neighbors as dirty so they (and possibly their children)
				// can have their neighbors re-linked.
				MarkNeighborsDirty(NodeLink);

				// If the tile being removed is marked dirty, remove it from the list so
				// we don't bother trying to update it.
				DirtyNodes.Remove(NodeLink);

				// Release the tile's memory
				ReleaseTileByLink(NodeLink);
			}
			EndBatchEdit();
		}
	}
}

void FEditableSvo::RemoveTileAtCoord(const FIntVector& Coord)
{
	const FSvoNodeLink TileLink = GetTileLinkAtCoord(Coord);
	RemoveTile(TileLink);
}

void FEditableSvo::RemoveMatchingTiles(const FSparseVoxelOctree& Source)
{
	if (Source.GetConfig().IsCompatibleWith(Config))
	{
		BeginBatchEdit();
		{
			for (const FSvoTile& Tile : Source.GetTiles())
			{
				RemoveTileAtCoord(Tile.GetCoord());
			}
		}
		EndBatchEdit();
	}
}

void FEditableSvo::MarkNeighborsDirty(const FSvoNodeLink& Link)
{
	// We need to mark all neighbors as dirty so they (and possibly their children) can have
	// their neighbors re-linked.
	for (FSvoNeighborConstIterator NeighborIter(*this, Link); NeighborIter; ++NeighborIter)
	{
		const ESvoNeighbor Neighbor = NeighborIter.GetNeighbor();
		const FSvoNodeLink NeighborLink = NeighborIter.GetNeighborLink();
		const FSvoNode& NeighborNode = NeighborIter.GetNeighborNodeChecked();

		// All neighbors should be at this resolution or lower as we cannot link from a
		// larger node to a smaller node.
		ensure(NeighborLink.LayerIdx >= Link.LayerIdx);

		// Really all we care about are the neighbors that are at the same resolution
		// as this node. This is because if the layer is a lower resolution, we know
		// there isn't anything linking back to us in this direction and that we also
		// aren't allowed to link to any nodes at a higher resolution. Only nodes that
		// are at our resolution are sure to be pointing back to this node (as well as
		// possibly any children within the neighbor).
		if (NeighborLink.LayerIdx == Link.LayerIdx)
		{
			const ESvoNeighbor OppositeNeighbor = FSvoUtils::GetOppositeNeighbor(Neighbor);
			const ESvoNeighborFlags OppositeNeighborFlag = (ESvoNeighborFlags)(1 << (uint8)OppositeNeighbor);

			ESvoNeighborFlags& NeighborFlags = DirtyNodes.FindOrAdd(NeighborLink);
			EnumAddFlags(NeighborFlags, OppositeNeighborFlag);
		}
	}
}

void FEditableSvo::FinalizeNodes()
{
	if (DirtyNodes.Num() > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_FEditableSvo_FinalizeNodes);

		// Sort so we operate on nodes from the top down. Note that this is necessary for
		// LinkNeighborForNode to work correctly as it expects the parent neighbors to
		// already have been linked before linking a child's neighbors.
		{
			SCOPE_CYCLE_COUNTER(STAT_FEditableSvo_FinalizeNodes_Sort);
			DirtyNodes.KeySort([](const FSvoNodeLink& A, const FSvoNodeLink& B)
			{
				return (A.LayerIdx > B.LayerIdx);
			});
		}

		// Iterate over each dirty node and update their neighbor links
		for (TPair<FSvoNodeLink, ESvoNeighborFlags>& DirtyNode : DirtyNodes)
		{
			// When adding a new node, we need to first link the node to its neighbors and
			// then take each neighbor and re-link any of their neighbors links that point
			// back towards this node. We have to start at the first node that is at or
			// below the layer level of this node's parent.  This is because a
			// higher-resolution node can link to a lower-resolution node but not
			// vice-versa so it's possible that the neighbor node should now link to this
			// node instead of the lower-resolution node that used to be empty but now
			// contains this new node.

			ensureAlways(DirtyNode.Value != ESvoNeighborFlags::None);

			for (ESvoNeighbor Neighbor : FSvoUtils::GetAllNeighbors())
			{
				const ESvoNeighborFlags NeighborFlag = (ESvoNeighborFlags)(1 << (uint8)Neighbor);
				if (EnumHasAnyFlags(DirtyNode.Value, NeighborFlag))
				{
					LinkNeighborForNodeHierarchically(DirtyNode.Key, Neighbor);
				}
			}
		}

#if !UE_BUILD_SHIPPING && SVO_VERIFY_NODES
		VerifyNodeData();
#endif

		// Free up the memory from the set before processing.
		DirtyNodes.Empty();
	}
}

uint32 FEditableSvo::GetMemUsed() const
{
	const uint32 SuperMemUsed = Super::GetMemUsed();

	uint32 MemUsed = 0;
	MemUsed += DirtyNodes.GetAllocatedSize();

	return SuperMemUsed + MemUsed;
}

void FEditableSvo::EndBatchEdit()
{
	ensureAlways(BatchEditRefCounter > 0);
	--BatchEditRefCounter;

	if (BatchEditRefCounter <= 0)
	{
		BatchEditRefCounter = 0;
		FinalizeNodes();
	}
}