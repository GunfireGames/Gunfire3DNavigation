// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "NavSvoStreamingData.h"

#include "Gunfire3DNavigationCustomVersion.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavSvoStreamingData)

void UNavSvoStreamingData::Serialize(FArchive& Ar)
{
	// Mark that we are using the latest custom version
	Ar.UsingCustomVersion(FGunfire3DNavigationCustomVersion::GUID);

	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		ReleaseData();

		bool bHasOctree;
		Ar << bHasOctree;
		if (bHasOctree)
		{
			Octree = MakeShareable(new FEditableSvo(EForceInit::ForceInit));
			Octree->Serialize(Ar);
		}
	}
	else if (Ar.IsSaving())
	{
		bool bHasOctree = (Octree.IsValid());
		Ar << bHasOctree;

		if (bHasOctree)
		{
			Octree->Serialize(Ar);
		}
	}
}

void UNavSvoStreamingData::ReleaseData()
{
	Octree.Reset();
}

FEditableSvo* UNavSvoStreamingData::GetOctree()
{
	return Octree.Get();
}

FEditableSvo* UNavSvoStreamingData::EnsureOctree(const FSvoConfig& SourceConfig)
{
	if (Octree.IsValid())
	{
		// If the destination octree is compatible with this octree, just reset the contained
		// data.  Otherwise, we'll need to destroy and recreate it a new octree to match.
		const FSvoConfig& Config = Octree->GetConfig();
		const bool bIsCompatibleConfig = Config.IsCompatibleWith(SourceConfig);
		const bool bCanReset = (bIsCompatibleConfig);

		if (bCanReset)
		{
			Octree->Reset();
		}
		else
		{
			ReleaseData();
		}
	}

	// Create the octree if one does not already exist or the existing one was incompatible.
	if (!Octree.IsValid())
	{
		//Octree = MakeShareable(new FEditableSvo(SourceConfig));
	}

	return Octree.Get();
}