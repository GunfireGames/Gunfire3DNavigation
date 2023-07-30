// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "NavSvoDebugActor.h"

#include "Gunfire3DNavData.h"
#include "SparseVoxelOctree/EditableSparseVoxelOctree.h"

#include "DrawDebugHelpers.h"
#include "NavigationSystem.h"
#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif
#include "Components/BillboardComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavSvoDebugActor)

///> Constants

// Path Colors
static const FColor kValidPathColor = FColor::Blue;
static const FColor kBlockedPathColor = FColor::Red;

// Node Colors
static const FColor kCurrentNodeColor = FColor::Green;
static const FColor kVisitedNodeColor = FColor::Cyan;
static const FColor kBlockedNodeColor = FColor::Red;
static const FColor kErrorNodeColor = FColor::Magenta;

// Layer Colors
static const FColor kLayerColors[] =
{
	FColor::Red,	// Voxel layer
	FColor::Orange,	// Layer 0, leaf nodes
	FColor::Magenta,
	FColor::Green,
	FColor::Blue,
	FColor::Cyan,
	FColor::Yellow
};

ANavSvoDebugActor::ANavSvoDebugActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

	StartPosition = CreateTestPosition(TEXT("StartPosition"), FVector::ZeroVector, StartSprite);
	EndPosition = CreateTestPosition(TEXT("EndPosition"), FVector::ForwardVector * 300.f, EndSprite);

#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		PrimaryActorTick.bCanEverTick = true;
		PrimaryActorTick.bStartWithTickEnabled = true;
	}
#endif
}

USceneComponent* ANavSvoDebugActor::CreateTestPosition(const TCHAR* Name, const FVector& Offset, TObjectPtr<UBillboardComponent>& Sprite)
{
	USceneComponent* TestPosition = CreateDefaultSubobject<USceneComponent>(Name);
	if (TestPosition)
	{
		TestPosition->SetRelativeLocation(Offset);
		TestPosition->SetupAttachment(RootComponent);

#if WITH_EDITORONLY_DATA
		if (HasAnyFlags(RF_ClassDefaultObject) == false)
		{
			TestPosition->TransformUpdated.AddLambda(
				[this](USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
				{
					RebuildAll();
				});

			TStringBuilder<64> Builder;
			Builder.Append(Name);
			Builder.Append(TEXT("_Sprite"));

			Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(Builder.ToString());
			if (Sprite)
			{
				Sprite->bIsScreenSizeScaled = true;
				Sprite->SetupAttachment(TestPosition);
			}
		}
#endif // WITH_EDITORONLY_DATA
	}

	return TestPosition;
}

void ANavSvoDebugActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// Force the navigation data to refresh on placement so the agent properties are setup
	// correctly.
	RefreshNavData();
}

#if WITH_EDITOR
void ANavSvoDebugActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	static const FName NAME_Agent("Agent");
	static const FName NAME_Path("Path");
	static const FName NAME_Raycast("Raycast");

	if (PropertyChangedEvent.Property)
	{
		FName CategoryName = FObjectEditorUtils::GetCategoryFName(PropertyChangedEvent.Property);
		FName PropertyName = PropertyChangedEvent.Property->GetFName();

		if (CategoryName == NAME_Agent)
		{
			RefreshNavData();
		}
		else if (CategoryName == NAME_Path ||
				 PropertyName == GET_MEMBER_NAME_CHECKED(ANavSvoDebugActor, bDrawPath))
		{
			if (bDrawPath)
			{
				RebuildPath();
			}
		}
		else if (CategoryName == NAME_Raycast||
				 PropertyName == GET_MEMBER_NAME_CHECKED(ANavSvoDebugActor, bDrawRaycast))
		{
			if (bDrawRaycast)
			{
				RebuildRaycast();
			}
		}
	}
}

void ANavSvoDebugActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (bFinished)
	{
		RebuildAll();
	}
}
#endif  // WITH_EDITOR

void ANavSvoDebugActor::RefreshNavData()
{
	if (UNavigationSystemV1* NavSys = Cast<UNavigationSystemV1>(GetWorld()->GetNavigationSystem()))
	{
		FNavAgentProperties NavAgentProperties(AgentRadius, AgentHeight);
		NavAgentProperties.SetPreferredNavData(AGunfire3DNavData::StaticClass());

		// Find the navigation data that best fits the specified agent properties.
		NavData = Cast<AGunfire3DNavData>(NavSys->GetNavDataForProps(NavAgentProperties));
	}
	else
	{
		NavData = nullptr;
	}
}

bool ANavSvoDebugActor::CanRebuild() const
{
	return (NavData != nullptr && StartPosition != nullptr && EndPosition != nullptr);
}

void ANavSvoDebugActor::RebuildAll()
{
	if (CanRebuild())
	{
		if (bDrawPath || bDrawPathSearch)
		{
			RebuildPath();
		}

		if (bDrawRaycast || bDrawRaycastSearch)
		{
			RebuildRaycast();
		}
	}
}

void ANavSvoDebugActor::RebuildPath()
{
	if (!CanRebuild())
	{
		return;
	}

	UNavigationSystemV1* NavSys = Cast<UNavigationSystemV1>(GetWorld()->GetNavigationSystem());
	const FVector StartLocation = StartPosition->GetComponentLocation();
	const FVector EndLocation = EndPosition->GetComponentLocation();

	FSharedNavQueryFilter NavQueryFilter = NavData->GetDefaultQueryFilter()->GetCopy();
	NavQueryFilter->SetMaxSearchNodes(MaxPathSearchNodes);

	FGunfire3DNavQueryFilter* NavFilterImpl = static_cast<FGunfire3DNavQueryFilter*>(NavQueryFilter->GetImplementation());
	NavFilterImpl->SetHeuristicScale(PathHeuristicScale);
	NavFilterImpl->SetBaseTraversalCost(NodeBaseTraversalCost);
	NavFilterImpl->OnNodeVisited = [this](NavNodeRef NavNode) -> bool
	{
		PathSearchNodes.Add(NavNode);
		return true;
	};
	PathSearchNodes.Reset();

	FPathFindingQuery NavQuery(this, *NavData, StartLocation, EndLocation, NavQueryFilter);
	NavQuery.SetAllowPartialPaths(bAllowPartialPath);
	NavQuery.CostLimit = PathCostLimit;

	// Setup flags
	if (!bStringPullPath)
	{
		NavQuery.NavDataFlags |= (uint32)EGunfire3DNavPathFlags::SkipStringPulling;
	}

	if (!bSmoothPath)
	{
		NavQuery.NavDataFlags |= (uint32)EGunfire3DNavPathFlags::SkipSmoothing;
	}

	// Reset path search timer.
	if (bAutoStepPathSearch)
	{
		CurrentPathSearchStep = 0;
		PathSearchNodeTimer = 0.f;
	}

	FPathFindingResult PathResult;

	// Time the path finding procedure
	const double StartTime = FPlatformTime::Seconds();
	{
		PathResult = NavSys->FindPathSync(NavQuery);
	}
	const double EndTime = FPlatformTime::Seconds();
	const float Duration = (EndTime - StartTime);

	NavPath = PathResult.Path;
	bPathExists = PathResult.IsSuccessful();
	bIsPartialPath = PathResult.IsPartial();
	PathSearchTime = (Duration * 1000.0f); // Convert to ms

	///> Update Stats

	if (NavPath.IsValid())
	{
		const FGunfire3DNavPath* NavPath3D = static_cast<FGunfire3DNavPath*>(PathResult.Path.Get());

		bPathNodeLimitReached = bPathExists ? NavPath3D->DidSearchReachedLimit() : false;

		const FGunfire3DNavPathQueryResults& GenerationInfo = NavPath3D->GetGenerationInfo();
		PathLength = GenerationInfo.PathLength;
		PathCost = GenerationInfo.PathCost;
		NumPathNodesVisited = GenerationInfo.NumNodesVisited;
		NumPathNodesQueried = GenerationInfo.NumNodesQueried;
		NumPathNodesOpened = GenerationInfo.NumNodesOpened;
		NumPathNodesReopened = GenerationInfo.NumNodesReopened;
		QueryMemoryUsed = GenerationInfo.MemUsed;
	}
}

void ANavSvoDebugActor::RebuildRaycast()
{
	if (CanRebuild())
	{
		const FVector StartLocation = StartPosition->GetComponentLocation();
		const FVector EndLocation = EndPosition->GetComponentLocation();

		FSharedNavQueryFilter NavQueryFilter = NavData->GetDefaultQueryFilter()->GetCopy();
		NavQueryFilter->SetMaxSearchNodes(MaxRaycastSearchNodes);

		bRaycastHit = NavData->Raycast(StartLocation, EndLocation, RayHitLocation, NavQueryFilter, this);
	}
}

void ANavSvoDebugActor::Tick(float DeltaSeconds)
{
	UWorld* World = GetWorld();

	if (NavData == nullptr)
	{
		return;
	}

	if (bDrawPath)
	{
		DrawPath(World);
	}

	if (bDrawPathSearch)
	{
		DrawPathSearch(World, DeltaSeconds);
	}

	if (bDrawRaycast || bDrawRaycastSearch)
	{
		DrawRaycast(World, DeltaSeconds);
	}

	if (bDrawNeighbors)
	{
		DrawNeighbors(World);
	}
}

void ANavSvoDebugActor::DrawPath(UWorld* World) const
{
	if (!NavPath.IsValid())
	{
		return;
	}

	const FGunfire3DNavPath* NavPath3D = StaticCast<FGunfire3DNavPath*>(NavPath.Get());
	const TArray<FNavPathPoint>& PathPoints = NavPath3D->GetPathPoints();
	if (PathPoints.Num() > 1)
	{
		// Draw the path locations
		NavPath3D->DebugDraw(NavData, kValidPathColor, nullptr, false, 0.f);

		// Draw the octree nodes which this path passes through
		if (bDrawPathNodes)
		{
			const FEditableSvo* Octree = NavData->GetOctree();

			FBox NodeBounds(ForceInit);

			// The associated node link is stored with each path point which we can use to draw the nodes.
			for (const FNavPathPoint& PathPoint : PathPoints)
			{
				FSvoNodeLink NodeLink(PathPoint.NodeRef);
				Octree->GetBoundsForLink(NodeLink, NodeBounds);
				DrawDebugBox(World, NodeBounds.GetCenter(), NodeBounds.GetExtent(), FQuat::Identity, kCurrentNodeColor, false, -1.f, 0, 3.f);
			}
		}

		// Draw agent capsule at start and end points
		DrawDebugCapsule(World, PathPoints[0].Location, NavData->GetConfig().AgentHeight / 2.f, NavData->GetConfig().AgentRadius, FQuat::Identity, kCurrentNodeColor);
		DrawDebugCapsule(World, PathPoints.Last().Location, NavData->GetConfig().AgentHeight / 2.f, NavData->GetConfig().AgentRadius, FQuat::Identity, kCurrentNodeColor);
	}
}

void ANavSvoDebugActor::DrawPathSearch(UWorld* World, float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
	// Draw Search Nodes
	if (!bDrawPathSearch || !NavPath.IsValid())
	{
		return;
	}

	const int32 NumSearchNodes = PathSearchNodes.Num();
	if (NumSearchNodes <= 0)
	{
		return;
	}

	const FGunfire3DNavPath* NavPath3D = StaticCast<FGunfire3DNavPath*>(NavPath.Get());
	const FGunfire3DNavPathQueryResults& GenerationInfo = NavPath3D->GetGenerationInfo();

	if (bAutoStepPathSearch && CurrentPathSearchStep < NumSearchNodes)
	{
		PathSearchNodeTimer += DeltaSeconds;
		if (PathSearchNodeTimer >= (1.f / PathSearchAutoStepRate))
		{
			CurrentPathSearchStep += (PathSearchNodeTimer * PathSearchAutoStepRate);
			PathSearchNodeTimer = 0.f;
		}
	}

	CurrentPathSearchStep = FMath::Clamp(CurrentPathSearchStep, 0, NumSearchNodes);
	if (CurrentPathSearchStep <= 0)
	{
		return;
	}

	const FEditableSvo* Octree = NavData->GetOctree();
	FBox CurrentNodeBounds(ForceInit);

	// Draw Current Node
	const int32 LastNodeIdx = (CurrentPathSearchStep - 1);
	const FSvoNodeLink CurrentNodeLink(PathSearchNodes[LastNodeIdx]);
	Octree->GetBoundsForLink(CurrentNodeLink, CurrentNodeBounds);

	if (CurrentPathSearchStep > 1)
	{
		FBox NodeBounds(ForceInit);

		// Draw all nodes that aren't the current node
		for (int32 NodeIdx = 0; NodeIdx < LastNodeIdx; ++NodeIdx)
		{
			FSvoNodeLink NodeLink(PathSearchNodes[NodeIdx]);
			Octree->GetBoundsForLink(NodeLink, NodeBounds);

			const bool bPathContainsNode = GenerationInfo.PathPortalPoints.ContainsByPredicate([NodeLink](const FNavPathPoint& RHS)
			{
				return RHS.NodeRef == NodeLink.GetID();
			});

			// Draw node
			if (bPathContainsNode)
			{
				DrawDebugBox(World, NodeBounds.GetCenter(), NodeBounds.GetExtent(), FQuat::Identity, kVisitedNodeColor, false, -1.f, ESceneDepthPriorityGroup::SDPG_Foreground, 3.f);
			}
			else
			{
				DrawDebugBox(World, NodeBounds.GetCenter(), NodeBounds.GetExtent(), FQuat::Identity, kCurrentNodeColor);
			}
		}

		// Draw line to current node
		DrawDebugLine(World, NodeBounds.GetCenter(), CurrentNodeBounds.GetCenter(), kVisitedNodeColor, false, -1, ESceneDepthPriorityGroup::SDPG_Foreground, NavigationDebugDrawing::PathLineThickness);
	}

	DrawDebugBox(World, CurrentNodeBounds.GetCenter(), CurrentNodeBounds.GetExtent(), FQuat::Identity, kCurrentNodeColor);
#endif
}

void ANavSvoDebugActor::DrawRaycast(UWorld* World, float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
	if (bDrawRaycastSearch)
	{
		FSparseVoxelOctree::FRaycastDebug& Debug = NavData->GetOctree()->RaycastDebug;

		const int32 PrevDebugStep = Debug.DebugStep;

		if (bAutoStepRaycastSearch)
		{
			RaycastStepTimer += DeltaSeconds;

			if (RaycastStepTimer >= (1.f / RaycastSearchAutoStepRate))
			{
				CurrentRaycastStep += (RaycastStepTimer * RaycastSearchAutoStepRate);
				if (CurrentRaycastStep >= Debug.NumSteps)
				{
					CurrentRaycastStep = 0;
				}

				RaycastStepTimer = 0.0f;

				Debug.DebugStep = CurrentRaycastStep;
			}
		}
		else
		{
			Debug.DebugStep = CurrentRaycastStep;
		}

		// Force the raycast to rebuild 
		if (Debug.DebugStep != PrevDebugStep)
		{
			RebuildRaycast();
		}

		FColor NodeColor = kCurrentNodeColor;

		switch (Debug.State)
		{
		case FEditableSvo::EDebugState::Hit:
			NodeColor = kBlockedNodeColor;
			break;

		case FEditableSvo::EDebugState::Error:
			NodeColor = kErrorNodeColor;
			break;

		case FEditableSvo::EDebugState::Step:
			NodeColor = kVisitedNodeColor;
			break;
		}

		DrawDebugBox(World, Debug.NodeBounds.GetCenter(), Debug.NodeBounds.GetExtent(), NodeColor, false, -1.f, 0, 3.0f);
		DrawDebugLine(World, Debug.RayStart, Debug.RayEnd, kCurrentNodeColor, false, -1.f, 0, 3.0f);
	}
	else
	{
		const FVector StartLocation = StartPosition->GetComponentLocation();
		const FVector EndLocation = EndPosition->GetComponentLocation();

		if (bRaycastHit)
		{
			DrawDebugLine(World, StartLocation, RayHitLocation, kValidPathColor, false, -1.f, 0, 3.0f);
			DrawDebugLine(World, RayHitLocation, EndLocation, kBlockedPathColor, false, -1.f, 0, 3.0f);
		}
		else
		{
			DrawDebugLine(World, StartLocation, EndLocation, kValidPathColor, false, -1.f, 0, 3.0f);
		}
	}
#endif // !UE_BUILD_SHIPPING
}

void ANavSvoDebugActor::DrawNeighbors(UWorld* World) const
{
#if !UE_BUILD_SHIPPING
	const FEditableSvo* Octree = NavData->GetOctree();
	FSvoNodeLink NodeAtLocation = Octree->GetLinkForLocation(GetActorLocation(), true);
	if (!NodeAtLocation.IsValid())
	{
		return;
	}

	// Draw the node for the queried location
	FBox Bounds;
	Octree->GetBoundsForLink(NodeAtLocation, Bounds);
	DrawDebugBox(World, Bounds.GetCenter(), Bounds.GetExtent(), FQuat::Identity, kCurrentNodeColor);

	// Now draw all neighbors
	for (FSvoNeighborConstIterator NeighborIter(*Octree, NodeAtLocation); NeighborIter; ++NeighborIter)
	{
		const FSvoNodeLink NeighborLink = NeighborIter.GetNeighborLink();
		const FColor NeighborColor = (NeighborLink.IsVoxelNode()) ? kLayerColors[0] : kLayerColors[NeighborLink.LayerIdx + 1];

		Octree->GetBoundsForLink(NeighborLink, Bounds);
		DrawDebugBox(World, Bounds.GetCenter(), Bounds.GetExtent(), FQuat::Identity, NeighborColor);
	}
#endif
}
