// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "Gunfire3DNavQueryFilter.h"

#include "Gunfire3DNavData.h"
#include "Gunfire3DNavigationUtils.h"
#include "NavSvo/NavSvoQuery.h"

//////////////////////////////////////////////////////////////////////////
// Gunfire3DNavQueryConstraints
//////////////////////////////////////////////////////////////////////////

bool FGunfire3DNavQueryConstraints::HasConstraints() const
{
	return Bounds.Num() > 0;
}

bool FGunfire3DNavQueryConstraints::ConstrainBounds(FBox& InOutBounds) const
{
	// Inclusion Bounds
	if (Bounds.Num() > 0)
	{
		bool bIsWithinConstraints = false;

		// Find the clipped node bounds
		for (const FBox& ConstraintBounds : Bounds)
		{
			if (InOutBounds.Intersect(ConstraintBounds))
			{
				InOutBounds = InOutBounds.Overlap(ConstraintBounds);
				bIsWithinConstraints = true;
			}
		}

		return bIsWithinConstraints;
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Gunfire3DNavQueryFilter
//////////////////////////////////////////////////////////////////////////

bool FGunfire3DNavQueryFilter::IsEqual(const INavigationQueryFilterInterface* Other) const
{
	// TODO: This doesn't play nice with any other filter type. Epic mentions this in
	//		 FRecastQueryFilter::IsEqual which this was taken from.  If we want to address
	//		 this, we'll need to update the FRecastQueryFilter::IsEqual as well.
	return FMemory::Memcmp(this, Other, sizeof(FGunfire3DNavQueryFilter)) == 0;
}

INavigationQueryFilterInterface* FGunfire3DNavQueryFilter::CreateCopy() const
{
	return new FGunfire3DNavQueryFilter(*this);
}

//////////////////////////////////////////////////////////////////////////
// Gunfire3DNavigationQueryFilter
//////////////////////////////////////////////////////////////////////////

void UGunfire3DNavigationQueryFilter::InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const
{
	if (NavData.GetClass()->IsChildOf(AGunfire3DNavData::StaticClass()))
	{
		FSharedNavQueryFilter NavQueryFilter = NavData.GetDefaultQueryFilter()->GetCopy();
		NavQueryFilter->SetMaxSearchNodes(MaxPathSearchNodes);

		Filter.SetFilterType<FGunfire3DNavQueryFilter>();
		if (FGunfire3DNavQueryFilter* NavFilterImpl = static_cast<FGunfire3DNavQueryFilter*>(Filter.GetImplementation()))
		{
			NavFilterImpl->SetHeuristicScale(PathHeuristicScale);
			NavFilterImpl->SetBaseTraversalCost(NodeBaseTraversalCost);
		}

		Filter.SetMaxSearchNodes(MaxPathSearchNodes);
	}

	Super::InitializeFilter(NavData, Querier, Filter);
}
