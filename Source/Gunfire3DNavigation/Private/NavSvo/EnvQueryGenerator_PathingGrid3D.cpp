// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "EnvQueryGenerator_PathingGrid3D.h"
#include "Gunfire3DNavData.h"

#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "Math/GenericOctree.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EnvQueryGenerator_PathingGrid3D)

#define LOCTEXT_NAMESPACE "Gunfire3DNavigation"

//
// Simple vector-based semantics for storing locations in the octree
//
struct FEQSOctreeSemantics
{
	// When a leaf gets more than this number of elements, it will split itself into a
	// node with multiple child leaves
	enum { MaxElementsPerLeaf = 16 };

	// This is used for incremental updates. When removing an element, larger values will
	// cause leaves to be removed and collapsed into a parent node.
	enum { MinInclusiveElementsPerNode = 7 };

	// How deep the tree can go.
	enum { MaxNodeDepth = 12 };

	typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

	FORCEINLINE static FBoxCenterAndExtent GetBoundingBox(const FVector& Element)
	{
		return FBoxCenterAndExtent(Element, FVector::ZeroVector);
	}

	FORCEINLINE static bool AreElementsEqual(const FVector& A, const FVector& B)
	{
		return (A == B);
	}

	FORCEINLINE static void SetElementId(const FVector& Element, FOctreeElementId2 OctreeElementID)
	{}

	FORCEINLINE static void ApplyOffset(FVector& Element, FVector Offset)
	{
		Element += Offset;
	}
};

// Octree to hold all desired EQS query location for quick look-ups based on node bounds.
typedef TOctree2<FVector, FEQSOctreeSemantics> FEQSOctree;

UEnvQueryGenerator_PathingGrid3D::UEnvQueryGenerator_PathingGrid3D()
{
	GenerateAround = UEnvQueryContext_Querier::StaticClass();
	GridHalfSize.DefaultValue = 500.0f;
	SpaceBetween.DefaultValue = 250.0f;
	MinHeight.DefaultValue = -500.0f;
	MaxHeight.DefaultValue = 500.0f;

	ItemType = UEnvQueryItemType_Point::StaticClass();
}

void UEnvQueryGenerator_PathingGrid3D::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	UObject* BindOwner = QueryInstance.Owner.Get();
	GridHalfSize.BindData(BindOwner, QueryInstance.QueryID);
	SpaceBetween.BindData(BindOwner, QueryInstance.QueryID);
	MinHeight.BindData(BindOwner, QueryInstance.QueryID);
	MaxHeight.BindData(BindOwner, QueryInstance.QueryID);

	float RadiusValue = GridHalfSize.GetValue();
	float DensityValue = SpaceBetween.GetValue();

	const int32 ItemCountXY = FMath::TruncToInt((RadiusValue * 2.0f / DensityValue) + 1);
	const int32 ItemCountHalf = ItemCountXY / 2;
	const int32 MinZ = FMath::TruncToInt(MinHeight.GetValue() / DensityValue);
	const int32 MaxZ = FMath::TruncToInt(MaxHeight.GetValue() / DensityValue);

	const AGunfire3DNavData* NavData = Cast<AGunfire3DNavData>(FEQSHelpers::FindNavigationDataForQuery(QueryInstance));

	// This is only designed to work with 3D nav data, so early out if this isn't that
	if (NavData == nullptr)
	{
		return;
	}

	TArray<FVector> ContextLocations;
	QueryInstance.PrepareContext(GenerateAround, ContextLocations);

	const int32 MaxNumItems = ItemCountXY * (MaxZ - MinZ) * ContextLocations.Num();
	QueryInstance.ReserveItemData(MaxNumItems);

	FSharedNavQueryFilter Filter = NavData->GetDefaultQueryFilter()->GetCopy();
	FNavigationQueryFilter* QueryFilter = Filter.Get();
	
	uint32 DefaultMaxSearchNodes = QueryFilter->GetMaxSearchNodes();
	QueryFilter->SetMaxSearchNodes(DefaultMaxSearchNodes * 4);

	// Keep track of all items that have been added to the results so we don't duplicate.
	// This is necessary as we may receive multiple nodes from 3D navigation that both
	// contain the same location.
	TSet<FVector> AddedItemLookup;

	// For each context location, store all potential points within an octree then query
	// the 3D navigation for nodes that are reachable within our radius, adding any points
	// that are within each node bounds as results.
	for (const FVector& ContextLocation : ContextLocations)
	{
		FNavLocation ContextNavLocation;
		if (NavData->ProjectPoint(ContextLocation, ContextNavLocation, NavData->GetDefaultQueryExtent(), Filter))
		{
			FEQSOctree EQSOctree(ContextNavLocation.Location, RadiusValue);

			// Add all test locations to the octree
			for (int32 x = 0; x < ItemCountXY; ++x)
			{
				for (int32 y = 0; y < ItemCountXY; ++y)
				{
					for (int32 z = MinZ; z <= MaxZ; ++z)
					{
						const FVector Offset(DensityValue * (x - ItemCountHalf), DensityValue * (y - ItemCountHalf), DensityValue * z);
						const FVector TestLocation = ContextNavLocation.Location + Offset;

						// Only add the location to the octree if it is within the defined
						// navigable space.
						if (NavData->IsLocationWithinGenerationBounds(TestLocation))
						{
							EQSOctree.AddElement(TestLocation);
						}
					}
				}
			}

			// Function called on every node visited during the navigation query.
			auto OnNodeVisited = [&NavData, &EQSOctree, &AddedItemLookup, &QueryInstance](NavNodeRef NodeRef) -> bool
			{
				FBox NodeBounds;
				if (NavData->GetNodeBounds(NodeRef, NodeBounds))
				{
					// If true, the specified EQS octree-node will be searched
					auto CanOpenNode = [&NodeBounds](FEQSOctree::FNodeIndex ParentNodeIndex, FEQSOctree::FNodeIndex NodeIndex, const FBoxCenterAndExtent& OctreeNodeBounds) -> bool
					{
						return (OctreeNodeBounds.GetBox().Intersect(NodeBounds));
					};

					// Function called on every element explored in the EQS octree
					auto OnElementExplored = [&NodeBounds, &AddedItemLookup, &QueryInstance](FEQSOctree::FNodeIndex ParentNodeIndex, const FVector& Element)
					{
						if (NodeBounds.IsInsideOrOn(Element))
						{
							if (!AddedItemLookup.Find(Element))
							{
								AddedItemLookup.Add(Element);
								QueryInstance.AddItemData<UEnvQueryItemType_Point>(Element);
							}
						}
					};

					// Grab all potential destination points within the node bounds
					EQSOctree.FindElementsWithPredicate(CanOpenNode, OnElementExplored);
				}

				// Return true to keep the navigation query going.
				return true;
			};

			// Query the navigation data for all nodes that are reachable from the context
			// location.
			//
			// NOTE: The search will end if the node pool is depleted. This is controlled
			// by the query filter. Do not make this some crazy value as it will affect
			// performance.
			NavData->ForEachReachableNode(ContextNavLocation.Location, 0.f, OnNodeVisited, QueryFilter->AsShared());
		}
	}
}

FText UEnvQueryGenerator_PathingGrid3D::GetDescriptionTitle() const
{
	return FText::Format(LOCTEXT("PathingGrid3DDescriptionGenerateAroundContext", "{0}: generate around {1}"),
		Super::GetDescriptionTitle(), UEnvQueryTypes::DescribeContext(GenerateAround));
};

FText UEnvQueryGenerator_PathingGrid3D::GetDescriptionDetails() const
{
	FText Desc = FText::Format(LOCTEXT("PathingGrid3DDescription", "radius: {0}, space between: {1}, min height: {2}, max height: {3}"),
		FText::FromString(GridHalfSize.ToString()),
		FText::FromString(SpaceBetween.ToString()),
		FText::FromString(MinHeight.ToString()),
		FText::FromString(MaxHeight.ToString()));

	return Desc;
}

#undef LOCTEXT_NAMESPACE
