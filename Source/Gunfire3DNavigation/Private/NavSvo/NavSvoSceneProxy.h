// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "DynamicMeshBuilder.h"
#include "PrimitiveSceneProxy.h"

class AGunfire3DNavData;
class FEditableSvo;
class FSvoNode;
class FSvoTile;
enum class ESvoNeighbor : uint8;

class GUNFIRE3DNAVIGATION_API FNavSvoSceneProxy : public FPrimitiveSceneProxy
{
public:
	FNavSvoSceneProxy(const UPrimitiveComponent* InComponent, const AGunfire3DNavData* NavData);
	virtual ~FNavSvoSceneProxy();

	// Begin FPrimitiveSceneProxy overrides
	virtual SIZE_T GetTypeHash() const override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual uint32 GetMemoryFootprint() const override;
	// End FPrimitiveSceneProxy overrides

protected:
	struct FTileData
	{
		FBox Bounds;
		uint32 StartVert;
		uint32 NumLines;
	};

	struct FLine
	{
		FVector A;
		FVector B;
		FColor Color;
	};

	struct FTileBuildData
	{
		const AGunfire3DNavData* NavData;
		const FEditableSvo* Octree;
		const FSvoTile& Tile;
		TArray<FLine> Lines;
	};

	void InitRenderData(const TArray<FTileBuildData>& BuildData);

	void GatherData(const AGunfire3DNavData* NavData, TArray<FTileBuildData>& TilesOut) const;
	bool ShouldDraw(const AGunfire3DNavData* NavData, bool bIsBlocked) const;
	void GatherExternalFaces(FTileBuildData& Build, const FSvoNode& Node) const;
	bool IsNeighborBlocked(FTileBuildData& Build, const FSvoNode& Node, ESvoNeighbor Neighbor) const;
	void AddNeighborFace(FTileBuildData& Build, ESvoNeighbor Neighbor, const FVector& Center, FVector::FReal Extent) const;
	void GatherNodes(FTileBuildData& Build) const;
	void AddNode(FTileBuildData& Build, const FSvoNode& Node) const;

	void AddFace(FTileBuildData& Build, const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FColor& Color) const;
	void AddBox(FTileBuildData& Build, const FVector& Center, const FVector& Extent, const FColor& Color) const;

protected:
	TArray<FTileData> Tiles;

	UMaterial* NavDebugMaterial;
	FLocalVertexFactory VertexFactory;
	FStaticMeshVertexBuffers VertexBuffers;
	FDynamicMeshIndexBuffer32 IndexBuffer;

	static const FColor LayerColors[];
};
