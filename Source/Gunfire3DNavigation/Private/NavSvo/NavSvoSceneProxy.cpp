// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "NavSvoSceneProxy.h"

#include "Gunfire3DNavData.h"
#include "NavSvoGenerator.h"
#include "SparseVoxelOctree/EditableSparseVoxelOctree.h"

#include "Misc/EngineVersionComparison.h"

const FColor FNavSvoSceneProxy::LayerColors[] =
{
	FColor::Red,	// Voxel layer
	FColor::Orange,	// Layer 0, leaf nodes
	FColor::Magenta,
	FColor::Green,
	FColor::Blue,
	FColor::Cyan,
	FColor::Yellow
};

FNavSvoSceneProxy::FNavSvoSceneProxy(const UPrimitiveComponent* InComponent, const AGunfire3DNavData* NavData)
	: FPrimitiveSceneProxy(InComponent)
	, VertexFactory(GetScene().GetFeatureLevel(), "FNavSvoSceneProxy")
{
	NavDebugMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Gunfire3DNavigation/VertexColorWireframeMaterial"));
	NavDebugMaterial->AddToRoot();

	TArray<FTileBuildData> BuildTiles;
	GatherData(NavData, BuildTiles);

	InitRenderData(BuildTiles);
}

FNavSvoSceneProxy::~FNavSvoSceneProxy()
{
	VertexBuffers.PositionVertexBuffer.ReleaseResource();
	VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
	VertexBuffers.ColorVertexBuffer.ReleaseResource();
	IndexBuffer.ReleaseResource();
	VertexFactory.ReleaseResource();
}

SIZE_T FNavSvoSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

uint32 FNavSvoSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + FPrimitiveSceneProxy::GetAllocatedSize();
}

void FNavSvoSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			const bool bVisible = (bool)(View->Family->EngineShowFlags.Navigation);
			if (!bVisible)
			{
				continue;
			}

			for (const FTileData& TileData : Tiles)
			{
				if (View->ViewFrustum.IntersectBox(TileData.Bounds.GetCenter(), TileData.Bounds.GetExtent()))
				{
					FMeshBatch& Mesh = Collector.AllocateMesh();
					FMeshBatchElement& BatchElement = Mesh.Elements[0];

					Mesh.VertexFactory = &VertexFactory;
					Mesh.MaterialRenderProxy = NavDebugMaterial->GetRenderProxy();
					Mesh.Type = PT_LineList;

					BatchElement.IndexBuffer = &IndexBuffer;
					BatchElement.PrimitiveUniformBufferResource = &GIdentityPrimitiveUniformBuffer;
					BatchElement.FirstIndex = TileData.StartVert;
					BatchElement.NumPrimitives = TileData.NumLines;
					BatchElement.MinVertexIndex = TileData.StartVert;
					BatchElement.MaxVertexIndex = TileData.StartVert + (TileData.NumLines * 2) - 1;

					Collector.AddMesh(ViewIndex, Mesh);
				}
			}
		}
	}
}

FPrimitiveViewRelevance FNavSvoSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	const bool bVisible = (bool)View->Family->EngineShowFlags.Navigation;
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = bVisible && IsShown(View);
	Result.bDynamicRelevance = true;
	// ideally the TranslucencyRelevance should be filled out by the material, here we do it conservative
	Result.bSeparateTranslucency = Result.bNormalTranslucency = bVisible && IsShown(View);
	return Result;
}

void FNavSvoSceneProxy::InitRenderData(const TArray<FTileBuildData>& BuildData)
{
	uint32 NumUsedTiles = 0;
	uint32 NumLines = 0;

	for (const FTileBuildData& Build : BuildData)
	{
		NumLines += Build.Lines.Num();

		if (Build.Lines.Num() > 0)
		{
			NumUsedTiles++;
		}
	}

	if (NumLines == 0)
	{
		return;
	}

	VertexBuffers.PositionVertexBuffer.Init(NumLines * 2);
	// We don't use the tangents or UV's from the static mesh, but we need something
	// there to bind so we just make 1 element buffers.
	VertexBuffers.StaticMeshVertexBuffer.Init(1, 1);
	VertexBuffers.ColorVertexBuffer.Init(NumLines * 2);
	IndexBuffer.Indices.SetNumUninitialized(NumLines * 2);

	uint32 CurVert = 0;

	Tiles.Reserve(NumUsedTiles);

	for (const FTileBuildData& Build : BuildData)
	{
		// Only add tiles that have something to draw
		if (Build.Lines.Num() == 0)
			continue;

		FTileData& TileData = Tiles.AddDefaulted_GetRef();
		TileData.Bounds = Build.Octree->GetBoundsForNode(Build.Tile.GetNodeInfo());
		TileData.StartVert = CurVert;
		TileData.NumLines = Build.Lines.Num();

		for (const FLine& Line : Build.Lines)
		{
			VertexBuffers.PositionVertexBuffer.VertexPosition(CurVert + 0) = FVector3f(Line.A.X, Line.A.Y, Line.A.Z);
			VertexBuffers.PositionVertexBuffer.VertexPosition(CurVert + 1) = FVector3f(Line.B.X, Line.B.Y, Line.B.Z);
			VertexBuffers.ColorVertexBuffer.VertexColor(CurVert + 0) = Line.Color;
			VertexBuffers.ColorVertexBuffer.VertexColor(CurVert + 1) = Line.Color;

			IndexBuffer.Indices[CurVert + 0] = CurVert + 0;
			IndexBuffer.Indices[CurVert + 1] = CurVert + 1;

			CurVert += 2;
		}
	}

	ENQUEUE_RENDER_COMMAND(LineSetVertexBuffersInit)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
#if UE_VERSION_OLDER_THAN(5, 4, 0)
			VertexBuffers.PositionVertexBuffer.InitResource();
			VertexBuffers.StaticMeshVertexBuffer.InitResource();
			VertexBuffers.ColorVertexBuffer.InitResource();
#else
			VertexBuffers.PositionVertexBuffer.InitResource(RHICmdList);
			VertexBuffers.StaticMeshVertexBuffer.InitResource(RHICmdList);
			VertexBuffers.ColorVertexBuffer.InitResource(RHICmdList);
#endif

			FLocalVertexFactory::FDataType Data;
			VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(&VertexFactory, Data);
			VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&VertexFactory, Data);
			VertexBuffers.StaticMeshVertexBuffer.BindTexCoordVertexBuffer(&VertexFactory, Data);
			VertexBuffers.ColorVertexBuffer.BindColorVertexBuffer(&VertexFactory, Data);
#if UE_VERSION_OLDER_THAN(5, 4, 0)
			VertexFactory.SetData(Data);

			VertexFactory.InitResource();
			IndexBuffer.InitResource();
#else
			VertexFactory.SetData(RHICmdList, Data);

			VertexFactory.InitResource(RHICmdList);
			IndexBuffer.InitResource(RHICmdList);
#endif

#if WITH_EDITOR
			// We don't use any one-frame proxy materials, so make sure to register our
			// core material as being used, otherwise the editor verification will fail.
			SetUsedMaterialForVerification({ NavDebugMaterial });
#endif
		});
}

void FNavSvoSceneProxy::GatherData(const AGunfire3DNavData* NavData, TArray<FTileBuildData>& TilesOut) const
{
	if (NavData)
	{
		const FEditableSvo* Octree = nullptr;

		if (const FNavSvoGenerator* Generator = (FNavSvoGenerator*)NavData->GetGenerator())
		{
			Octree = Generator->GetOctree();
		}
		else
		{
			Octree = NavData->GetOctree();
		}

		if (Octree)
		{
			TilesOut.Reserve(Octree->GetNumTiles());

			for (const FSvoTile& Tile : Octree->GetTiles())
			{
				FTileBuildData& TileData = TilesOut.Add_GetRef({ NavData, Octree, Tile });

				// Collect obstructed voxels
				if (NavData->bDrawShell)
				{
					if (Tile.HasNodesAllocated())
					{
						GatherExternalFaces(TileData, Tile.GetNodeInfo());
					}
				}

				// Collect obstructed areas
				if (NavData->bDrawOctree)
				{
					GatherNodes(TileData);
				}
			}
		}
	}
}

bool FNavSvoSceneProxy::ShouldDraw(const AGunfire3DNavData* NavData, bool bIsBlocked) const
{
	return
		((NavData->DrawType == ENav3DDrawType::Open) && !bIsBlocked) ||
		((NavData->DrawType == ENav3DDrawType::Blocked) && bIsBlocked);
}

void FNavSvoSceneProxy::GatherExternalFaces(FTileBuildData& Build, const FSvoNode& Node) const
{
	const FSvoConfig& Config = Build.Octree->GetConfig();

	if (Node.GetNodeState() == ENodeState::Blocked)
	{
		for (FSvoNeighborConstIterator NeighborIter(*Build.Octree, Node); NeighborIter; ++NeighborIter)
		{
			const ESvoNeighbor Neighbor = NeighborIter.GetNeighbor();
			const FSvoNode& NeighborNode = NeighborIter.GetNeighborNodeChecked();

			if (!IsNeighborBlocked(Build, NeighborNode, FSvoUtils::GetOppositeNeighbor(Neighbor)))
			{
				FVector NodeLocation;
				Build.Octree->GetLocationForLink(Node.GetSelfLink(), NodeLocation);

				AddNeighborFace(Build, Neighbor, NodeLocation, Config.GetResolutionForLayer(Node.GetSelfLink().LayerIdx) * 0.5f);
			}
		}
	}
	else if (Node.IsLeafNode())
	{
		FVector NodeLocation;
		ensure(Build.Octree->GetLocationForLink(Node.GetSelfLink(), NodeLocation));

		for (FSvoVoxelIterator VoxelIter; VoxelIter; ++VoxelIter)
		{
			const char VoxelIdx = VoxelIter.GetIndex();
			const bool bIsBlocked = Node.IsVoxelBlocked(VoxelIdx);

			if (!bIsBlocked)
			{
				continue;
			}

			FSvoNodeLink VoxelLink = Node.GetSelfLink();
			VoxelLink.VoxelIdx = VoxelIdx;

			const FVector VoxelLocation = Config.GetVoxelLocation(VoxelIdx, NodeLocation);

			for (FSvoNeighborConstIterator NeighborIter(*Build.Octree, VoxelLink); NeighborIter; ++NeighborIter)
			{
				const FSvoNodeLink NeighborLink = NeighborIter.GetNeighborLink();
				const FSvoNode& NeighborNode = NeighborIter.GetNeighborNodeChecked();

				const bool bIsNeighborBlocked =
					(NeighborNode.GetNodeState() == ENodeState::Blocked) ||
					(NeighborLink.IsVoxelNode() && NeighborNode.IsVoxelBlocked(NeighborLink.VoxelIdx));

				// If we're blocked and the neighbor is not blocked, this is an exterior face
				if (bIsNeighborBlocked)
				{
					continue;
				}

				const ESvoNeighbor Neighbor = NeighborIter.GetNeighbor();
				AddNeighborFace(Build, Neighbor, VoxelLocation, Config.GetVoxelSize() * 0.5f);
			}
		}
	}

	if (Node.HasChildren())
	{
		for (uint8 i = 0; i < 8; ++i)
		{
			FSvoNodeLink ChildLink = Node.GetChildLink(i);
			if (const FSvoNode* ChildNode = Build.Octree->GetNodeFromLink(ChildLink))
			{
				GatherExternalFaces(Build, *ChildNode);
			}
		}
	}
}

bool FNavSvoSceneProxy::IsNeighborBlocked(FTileBuildData& Build, const FSvoNode& Node, ESvoNeighbor Neighbor) const
{
	ENodeState State = Node.GetNodeState();

	// If this node is fully blocked or fully open this is pretty simple
	if (State == ENodeState::Blocked)
	{
		return true;
	}
	else if (State == ENodeState::Open)
	{
		return false;
	}

	// If the node is a leaf and partially blocked, we need to check each voxel bordering
	// this neighbor.
	if (Node.IsLeafNode())
	{
		for (uint8 VoxelIdx : FSvoUtils::GetTouchingNeighborVoxels(FSvoUtils::GetOppositeNeighbor(Neighbor)))
		{
			if (!Node.IsVoxelBlocked(VoxelIdx))
			{
				return false;
			}
		}

		return true;
	}
	// If the node is not a leaf node we need to recurse into all the children bordering
	// the neighbor and make sure they are fully blocked.
	else
	{
		for (uint8 ChildIdx : FSvoUtils::GetChildrenTouchingNeighbor(Neighbor))
		{
			FSvoNodeLink ChildLink = Node.GetChildLink(ChildIdx);
			const FSvoNode* ChildNode = Build.Octree->GetNodeFromLink(ChildLink);

			if (!IsNeighborBlocked(Build, *ChildNode, Neighbor))
			{
				return false;
			}
		}

		return true;
	}
}

void FNavSvoSceneProxy::AddNeighborFace(FTileBuildData& Build, ESvoNeighbor Neighbor, const FVector& Center, FVector::FReal Extent) const
{
	const FIntVector Coord = FSvoUtils::GetNeighborDirection(Neighbor);
	const FVector Offset(Coord.X, Coord.Y, Coord.Z);
	const FVector FaceCenter = Center + (Offset * Extent);

	switch (Neighbor)
	{
	case ESvoNeighbor::Front:
	case ESvoNeighbor::Back:
		AddFace(Build,
			FaceCenter + FVector(0.0f, Extent, Extent),
			FaceCenter + FVector(0.0f, Extent, -Extent),
			FaceCenter + FVector(0.0f, -Extent, -Extent),
			FaceCenter + FVector(0.0f, -Extent, Extent),
			LayerColors[0]);
		break;

	case ESvoNeighbor::Right:
	case ESvoNeighbor::Left:
		AddFace(Build,
			FaceCenter + FVector(Extent, 0.0f, Extent),
			FaceCenter + FVector(Extent, 0.0f, -Extent),
			FaceCenter + FVector(-Extent, 0.0f, -Extent),
			FaceCenter + FVector(-Extent, 0.0f, Extent),
			LayerColors[0]);
		break;

	case ESvoNeighbor::Top:
	case ESvoNeighbor::Bottom:
		AddFace(Build,
			FaceCenter + FVector(Extent, Extent, 0.0f),
			FaceCenter + FVector(Extent, -Extent, 0.0f),
			FaceCenter + FVector(-Extent, -Extent, 0.0f),
			FaceCenter + FVector(-Extent, Extent, 0.0f),
			LayerColors[0]);
		break;
	}

}

void FNavSvoSceneProxy::GatherNodes(FTileBuildData& Build) const
{
	uint8 TileLayerIdx = Build.Octree->GetConfig().GetTileLayerIndex();
	uint8 MinLayerIdx = (Build.NavData->bDrawSingleLayer) ? FMath::Min(Build.NavData->DrawLayerIndex, TileLayerIdx) : 0;
	uint8 MaxLayerIdx = (Build.NavData->bDrawSingleLayer) ? MinLayerIdx : TileLayerIdx;
	uint8 MaxNodeLayerIdx = FMath::Min(MaxLayerIdx, (uint8)(TileLayerIdx - 1));

	if (MaxLayerIdx == TileLayerIdx)
	{
		AddNode(Build, Build.Tile.GetNodeInfo());
	}

	if (Build.Tile.HasNodesAllocated())
	{
		for (uint8 LayerIdx = MinLayerIdx; LayerIdx <= MaxNodeLayerIdx; ++LayerIdx)
		{
			for (const FSvoNode& Node : Build.Tile.GetNodesForLayer(LayerIdx))
			{
				AddNode(Build, Node);
			}
		}
	}
}

void FNavSvoSceneProxy::AddNode(FTileBuildData& Build, const FSvoNode& Node) const
{
	const FSvoConfig& Config = Build.Octree->GetConfig();

	const uint8 LayerIdx = Node.GetSelfLink().LayerIdx;
	const float LayerResolution = Config.GetResolutionForLayer(LayerIdx);
	const FVector NodeExtent(LayerResolution * 0.5f);

	FVector NodeLocation;
	ensure(Build.Octree->GetLocationForLink(Node.GetSelfLink(), NodeLocation));

	if (LayerIdx == SVO_LEAF_LAYER)
	{
		if (Build.NavData->bIncludeVoxelAreas && Node.GetNodeState() == ENodeState::PartiallyBlocked)
		{
			FVector VoxelExtent = Config.GetVoxelExtent();
			FVector VoxelLocation;

			for (FSvoVoxelIterator VoxelIter; VoxelIter; ++VoxelIter)
			{
				const char VoxelIdx = VoxelIter.GetIndex();
				const bool bIsBlocked = Node.IsVoxelBlocked(VoxelIdx);
				if (ShouldDraw(Build.NavData, bIsBlocked))
				{
					VoxelLocation = Config.GetVoxelLocation(VoxelIdx, NodeLocation);
					AddBox(Build, VoxelLocation, VoxelExtent, LayerColors[0]);
				}
			}
		}
		else
		{
			const bool bIsBlocked = (Node.GetNodeState() != ENodeState::Open);
			if (ShouldDraw(Build.NavData, bIsBlocked))
			{
				AddBox(Build, NodeLocation, NodeExtent, LayerColors[LayerIdx+1]);
			}
		}
	}
	else
	{
		const bool IsBlocked = (Node.GetNodeState() != ENodeState::Open);
		if (ShouldDraw(Build.NavData, IsBlocked))
		{
			AddBox(Build, NodeLocation, NodeExtent, LayerColors[LayerIdx+1]);
		}
	}
}

void FNavSvoSceneProxy::AddFace(FTileBuildData& Build, const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FColor& Color) const
{
	Build.Lines.Add({ A, B, Color });
	Build.Lines.Add({ B, C, Color });
	Build.Lines.Add({ C, D, Color });
	Build.Lines.Add({ D, A, Color });
}

void FNavSvoSceneProxy::AddBox(FTileBuildData& Build, const FVector& Center, const FVector& Extent, const FColor& Color) const
{
	Build.Lines.Add({ Center + FVector(Extent.X, Extent.Y, Extent.Z), Center + FVector(Extent.X, -Extent.Y, Extent.Z), Color });
	Build.Lines.Add({ Center + FVector(Extent.X, -Extent.Y, Extent.Z), Center + FVector(-Extent.X, -Extent.Y, Extent.Z), Color });
	Build.Lines.Add({ Center + FVector(-Extent.X, -Extent.Y, Extent.Z), Center + FVector(-Extent.X, Extent.Y, Extent.Z), Color });
	Build.Lines.Add({ Center + FVector(-Extent.X, Extent.Y, Extent.Z), Center + FVector(Extent.X, Extent.Y, Extent.Z), Color });

	Build.Lines.Add({ Center + FVector(Extent.X, Extent.Y, -Extent.Z), Center + FVector(Extent.X, -Extent.Y, -Extent.Z), Color });
	Build.Lines.Add({ Center + FVector(Extent.X, -Extent.Y, -Extent.Z), Center + FVector(-Extent.X, -Extent.Y, -Extent.Z), Color });
	Build.Lines.Add({ Center + FVector(-Extent.X, -Extent.Y, -Extent.Z), Center + FVector(-Extent.X, Extent.Y, -Extent.Z), Color });
	Build.Lines.Add({ Center + FVector(-Extent.X, Extent.Y, -Extent.Z), Center + FVector(Extent.X, Extent.Y, -Extent.Z), Color });

	Build.Lines.Add({ Center + FVector(Extent.X, Extent.Y, Extent.Z), Center + FVector(Extent.X, Extent.Y, -Extent.Z), Color });
	Build.Lines.Add({ Center + FVector(Extent.X, -Extent.Y, Extent.Z), Center + FVector(Extent.X, -Extent.Y, -Extent.Z), Color });
	Build.Lines.Add({ Center + FVector(-Extent.X, -Extent.Y, Extent.Z), Center + FVector(-Extent.X, -Extent.Y, -Extent.Z), Color });
	Build.Lines.Add({ Center + FVector(-Extent.X, Extent.Y, Extent.Z), Center + FVector(-Extent.X, Extent.Y, -Extent.Z), Color });
}
