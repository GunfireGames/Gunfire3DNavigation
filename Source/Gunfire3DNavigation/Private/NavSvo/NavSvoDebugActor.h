// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "Gunfire3DNavQueryFilter.h"

#include "NavSvoDebugActor.generated.h"

class UBillboardComponent;

UCLASS(hidecategories = (Input, Rendering, Tags, Actor, Layers, Replication), meta = (DisplayName = "3D Navigation Debug Actor"))
class ANavSvoDebugActor : public AActor
{
	GENERATED_BODY()

protected:
	// Defines the radius of the navigation agent which is used to identify navigation
	// data that best fits the agent size.
	UPROPERTY(EditAnywhere, Category = "Agent", meta = (ClampMin = "0.0"))
	float AgentRadius = 0.f;

	// Defines the height of the navigation agent which is used to identify navigation
	// data that best fits the agent size.
	UPROPERTY(EditAnywhere, Category = "Agent", meta = (ClampMin = "0.0"))
	float AgentHeight = 0.f;

	// Sets the maximum number of nodes that the pathing algorithm is allow to expand
	// before returning a partial path (or failing if partial paths are not allowed).
	UPROPERTY(EditAnywhere, Category = "Path")
	uint32 MaxPathSearchNodes = NAVDATA_DEFAULT_MAX_NODES;

	// Determines how 'greedy' the pathing algorithm is while searching. The higher the
	// value, the more the search will expand nodes closer to the destination.
	UPROPERTY(EditAnywhere, Category = "Path")
	float PathHeuristicScale = NAVDATA_DEFAULT_HEURISTIC_SCALE;

	// Determines how expensive it is, at minimum, to move through a node. The higher the
	// value, the more the search will favor there being less nodes in a path overall.
	// 
	// NOTE: Nodes of all sizes are weighted the same.
	UPROPERTY(EditAnywhere, Category = "Path")
	float NodeBaseTraversalCost = NAVDATA_DEFAULT_BASE_TRAVERSAL_COST;

	// If greater than zero, determines the maximum traversal cost allowed for a path.
	UPROPERTY(EditAnywhere, Category = "Path")
	float PathCostLimit = 0.f;

	// If true, a valid path will be returned even if the destination could not be
	// reached.
	UPROPERTY(EditAnywhere, Category = "Path")
	bool bAllowPartialPath = true;

	// If true, the path will be tightened up to be more direct.
	UPROPERTY(EditAnywhere, Category = "Path")
	bool bStringPullPath = true;

	// If true, the path will be smoothed to remove any harsh angles.
	UPROPERTY(EditAnywhere, Category = "Path")
	bool bSmoothPath = true;

	// The total length of the path
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path|Results")
	float PathLength = 0.f;

	// The total cost of the path
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path|Results")
	float PathCost = 0.f;

	// Number of nodes searched while finding this path (not including parent nodes)
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path|Results")
	int32 NumPathNodesVisited = 0;

	// Number of nodes queried to check if they could be opened.
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path|Results")
	int32 NumPathNodesQueried = 0;

	// Number of unique nodes opened.
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path|Results")
	int32 NumPathNodesOpened = 0;

	// Number of nodes that were opened more than once.
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path|Results")
	int32 NumPathNodesReopened = 0;

	// The amount of memory required, in bytes, to allocate enough nodes for this search.
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path|Results")
	int32 QueryMemoryUsed = 0;

	// How long, in milliseconds, the path took to complete
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path|Results")
	float PathSearchTime = 0.f;

	// The path was unable to be completed but has returned the closest it came to the
	// destination.
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path|Results")
	bool bIsPartialPath = false;

	// A valid path was found (complete or partial)
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path|Results")
	bool bPathExists = false;

	// The number of nodes specified in 'Max Path Search Nodes' was reached during the
	// pathing request. This will typically result in a partial path.
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path|Results")
	bool bPathNodeLimitReached = false;

	// If true, the path found from 'Start Position' to 'End Position' will be drawn in
	// the scene.
	UPROPERTY(EditAnywhere, Category = "Path|Drawing")
	bool bDrawPath = true;

	// If true, all nodes that the path passes through will be drawn.
	UPROPERTY(EditAnywhere, Category = "Path|Drawing", meta = (EditCondition = bDrawPath))
	bool bDrawPathNodes = true;

	// If true, the nodes searched during the pathing query will be drawn.
	UPROPERTY(EditAnywhere, Category = "Path|Drawing", meta = (EditCondition = bDrawPath))
	bool bDrawPathSearch = false;

	// If drawing the path search, this represents the current step in the search. This
	// can be manually set and will automatically update if 'Auto Step Path Search' is
	// enabled.
	UPROPERTY(EditAnywhere, Category = "Path|Drawing", meta = (EditCondition = bDrawPathSearch))
	int32 CurrentPathSearchStep = 0;

	// If true, and drawing the path search, the nodes searched will be displayed at the
	// interval specified in 'Path Search Auto Step Rate'.
	UPROPERTY(EditAnywhere, Category = "Path|Drawing", meta = (EditCondition = bDrawPathSearch))
	bool bAutoStepPathSearch = true;

	// Determines the number of nodes to draw per second when 'Auto Step Path Search'
	// is true.
	UPROPERTY(EditAnywhere, Category = "Path|Drawing", meta = (EditCondition = bAutoStepPathSearch))
	int32 PathSearchAutoStepRate = 5;

	// Sets the maximum number of nodes that the raycast algorithm is allow to search
	UPROPERTY(EditAnywhere, Category = "Raycast")
	uint32 MaxRaycastSearchNodes = 4096;

	// If true, the raycast from 'Start Position' to 'End Position' will be drawn in the
	// scene.
	// 
	// Green:	No Hit
	// Red:		Hit
	// Magenta:	Error
	// Cyan:	Nodes Searched (only if 'Draw Raycast Steps' is enabled)
	UPROPERTY(EditAnywhere, Category = "Raycast|Drawing")
	bool bDrawRaycast = false;

	// If true, all nodes that were explored during the raycast will be drawn in Cyan.
	UPROPERTY(EditAnywhere, Category = "Raycast|Drawing", meta = (EditCondition = bDrawRaycast))
	bool bDrawRaycastSearch = false;

	// If drawing the raycast steps, this represents the current step in the test. This
	// can be manually set and will automatically update if 'Auto Step Raycast Search'
	// is enabled.
	UPROPERTY(EditAnywhere, Category = "Raycast|Drawing", meta = (EditCondition = bDrawRaycastSearch))
	int32 CurrentRaycastStep = 0;

	// If true, and drawing raycast steps, the nodes searched will be displayed at the
	// interval specified in 'Raycast Search Auto Step Rate'.
	UPROPERTY(EditAnywhere, Category = "Raycast|Drawing", meta = (EditCondition = bDrawRaycastSearch))
	bool bAutoStepRaycastSearch = true;

	// Determines the number of nodes to draw per second when 'Auto Step Raycast Search'
	// is true.
	UPROPERTY(EditAnywhere, Category = "Raycast|Drawing", meta = (EditCondition = bAutoStepRaycastSearch))
	int32 RaycastSearchAutoStepRate = 5;

	// If true, all neighbors will be drawn around the node that is at this actors
	// location.
	UPROPERTY(EditAnywhere, Category = "Octree|Drawing")
	bool bDrawNeighbors = false;

	UPROPERTY(VisibleAnywhere, Category = "Test Locations")
	TObjectPtr<USceneComponent> StartPosition = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Test Locations")
	TObjectPtr<USceneComponent> EndPosition = nullptr;

public:
	ANavSvoDebugActor();

	virtual void PostRegisterAllComponents() override;
	virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// Called before editor copy, true allow export
	virtual bool ShouldExport() override { return false; }
	virtual bool IsEditorOnly() const { return true; }
	virtual bool EditorCanAttachTo(const AActor* InParent, FText& OutReason) const override { return false; }

	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#endif  // WITH_EDITOR

	virtual bool IsLevelBoundsRelevant() const override { return false; }

private:
	USceneComponent* CreateTestPosition(const TCHAR* Name, const FVector& Offset, TObjectPtr<UBillboardComponent>& Sprite);
	void RefreshNavData();
	bool CanRebuild() const;
	void RebuildAll();
	void RebuildPath();
	void RebuildRaycast();

	// NAVSVO_TODO: All of this needs to be moved into a render component and taken off of
	//				the main thread.
	void DrawPath(UWorld* World) const;
	void DrawPathSearch(UWorld* World, float DeltaSeconds);
	void DrawRaycast(UWorld* World, float DeltaSeconds);
	void DrawNeighbors(UWorld* World) const;

private:
	UPROPERTY()
	TObjectPtr<const class AGunfire3DNavData> NavData = nullptr;
	FNavPathSharedPtr NavPath = nullptr;

	UPROPERTY()
	TObjectPtr<UBillboardComponent> StartSprite = nullptr;

	UPROPERTY()
	TObjectPtr<UBillboardComponent> EndSprite = nullptr;

	bool bRaycastHit = false;
	FVector RayHitLocation;
	float RaycastStepTimer = 0.0f;

	TArray<NavNodeRef> PathSearchNodes;
	float PathSearchNodeTimer = 0.f;
};