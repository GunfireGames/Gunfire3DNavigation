// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "SparseVoxelOctree/EditableSparseVoxelOctree.h"

#include "AI/Navigation/NavigationDataChunk.h"

#include "NavSvoStreamingData.generated.h"

UCLASS()
class GUNFIRE3DNAVIGATION_API UNavSvoStreamingData : public UNavigationDataChunk
{
	GENERATED_BODY()

public:
	virtual ~UNavSvoStreamingData()
	{
		ReleaseData();
	}

	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface

	// Releases any stored data
	void ReleaseData();

	// Gets the current octree if it exists
	FEditableSvo* GetOctree();

	// Gets and potentially creates/resets the octree of a specified tile type
	FEditableSvo* EnsureOctree(const FSvoConfig& SourceConfig);

	// Level associated with this streaming data.
	UPROPERTY(Transient)
	TObjectPtr<const ULevel> Level = nullptr;

private:
	// Octree containing all relevant tiles/data
	FEditableSvoSharedPtr Octree = nullptr;
};