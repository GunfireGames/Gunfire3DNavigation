// Copyright Gunfire Games, LLC. All Rights Reserved.
// Most of this is taken directly from "RecastNavMeshGenerator.cpp, Unreal Engine 4" with slight modifications

#include "Gunfire3DNavigationGeometryExport.h"

#include "PhysicsEngine/BodySetup.h"

namespace Gunfire3DNavigationGeometryExport
{
	void ExportChaosHeightFieldSlice(const FNavHeightfieldSamples& PrefetchedHeightfieldSamples, const int32 NumRows, const int32 NumCols, const FTransform& LocalToWorld
		, TStatArray<FVector::FReal>& VertexBuffer, TStatArray<int32>& IndexBuffer, const FBox& SliceBox
		, FBox& UnrealBounds)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_Gunfire3DNavigationGeometryExport_ExportHeightFieldSlice);

		// calculate the actual start and number of columns we want
		const FBox LocalBox = SliceBox.TransformBy(LocalToWorld.Inverse());
		const bool bMirrored = (LocalToWorld.GetDeterminant() < 0.f);

		const int32 MinX = FMath::Clamp(FMath::FloorToInt(LocalBox.Min.X) - 1, 0, NumCols);
		const int32 MinY = FMath::Clamp(FMath::FloorToInt(LocalBox.Min.Y) - 1, 0, NumRows);
		const int32 MaxX = FMath::Clamp(FMath::CeilToInt(LocalBox.Max.X) + 1, 0, NumCols);
		const int32 MaxY = FMath::Clamp(FMath::CeilToInt(LocalBox.Max.Y) + 1, 0, NumRows);
		const int32 SizeX = MaxX - MinX;
		const int32 SizeY = MaxY - MinY;

		if (SizeX <= 0 || SizeY <= 0)
		{
			// slice is outside bounds, skip
			return;
		}

		const int32 VertOffset = VertexBuffer.Num() / 3;
		const int32 NumVerts = SizeX * SizeY;
		const int32 NumQuads = (SizeX - 1) * (SizeY - 1);
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_Gunfire3DNavigationGeometryExport_AllocatingMemory);
			VertexBuffer.Reserve(VertexBuffer.Num() + NumVerts * 3);
			IndexBuffer.Reserve(IndexBuffer.Num() + NumQuads * 3 * 2);
		}

		for (int32 IdxY = 0; IdxY < SizeY; IdxY++)
		{
			for (int32 IdxX = 0; IdxX < SizeX; IdxX++)
			{
				const int32 CoordX = IdxX + MinX;
				const int32 CoordY = IdxY + MinY;
				const int32 SampleIdx = CoordY * NumCols + CoordX; // #PHYSTODO bMirrored support


				const FVector UnrealCoords = LocalToWorld.TransformPosition(FVector(CoordX, CoordY, PrefetchedHeightfieldSamples.Heights[SampleIdx]));
				VertexBuffer.Add(UnrealCoords.X);
				VertexBuffer.Add(UnrealCoords.Y);
				VertexBuffer.Add(UnrealCoords.Z);
			}
		}

		for (int32 IdxY = 0; IdxY < SizeY - 1; IdxY++)
		{
			for (int32 IdxX = 0; IdxX < SizeX - 1; IdxX++)
			{
				const int32 CoordX = IdxX + MinX;
				const int32 CoordY = IdxY + MinY;
				const int32 SampleIdx = CoordY * (NumCols - 1) + CoordX;  // #PHYSTODO bMirrored support

				const bool bIsHole = PrefetchedHeightfieldSamples.Holes[SampleIdx];
				if (bIsHole)
				{
					continue;
				}

				const int32 I0 = IdxY * SizeX + IdxX;
				int32 I1 = I0 + 1;
				int32 I2 = I0 + SizeX;
				const int32 I3 = I2 + 1;
				if (bMirrored)
				{
					Swap(I1, I2);
				}

				IndexBuffer.Add(VertOffset + I0);
				IndexBuffer.Add(VertOffset + I3);
				IndexBuffer.Add(VertOffset + I1);

				IndexBuffer.Add(VertOffset + I0);
				IndexBuffer.Add(VertOffset + I2);
				IndexBuffer.Add(VertOffset + I3);
			}
		}

	}
}

//////////////////////////////////////////////////////////////////////////
FGunfire3DNavigationGeometryExport::FGunfire3DNavigationGeometryExport(FNavigationRelevantData& InData)
	: Data(InData)
{
	Data.Bounds = FBox(ForceInit);
}

void FGunfire3DNavigationGeometryExport::StoreCollisionCache()
{
	const int32 NumFaces = IndexBuffer.Num() / 3;
	const int32 NumVerts = VertexBuffer.Num() / 3;

	if (NumFaces == 0 || NumVerts == 0)
	{
		Data.CollisionData.Empty();
		return;
	}

	FGeometryCache::FHeader HeaderInfo;
	HeaderInfo.NumFaces = NumFaces;
	HeaderInfo.NumVerts = NumVerts;

	const int32 HeaderSize = sizeof(FGeometryCache::FHeader);
	const int32 CoordsSize = sizeof(float) * 3 * NumVerts;
	const int32 IndicesSize = sizeof(int32) * 3 * NumFaces;
	const int32 CacheSize = HeaderSize + CoordsSize + IndicesSize;

	// reserve + add combo to allocate exact amount (without any overhead/slack)
	Data.CollisionData.Reserve(CacheSize);
	Data.CollisionData.AddUninitialized(CacheSize);

	// store collisions
	uint8* RawMemory = Data.CollisionData.GetData();
	FGeometryCache* CacheMemory = (FGeometryCache*)RawMemory;
	CacheMemory->Header = HeaderInfo;
	CacheMemory->Verts = nullptr;
	CacheMemory->Indices = nullptr;

	FMemory::Memcpy(RawMemory + HeaderSize, VertexBuffer.GetData(), CoordsSize);
	FMemory::Memcpy(RawMemory + HeaderSize + CoordsSize, IndexBuffer.GetData(), IndicesSize);
}

void FGunfire3DNavigationGeometryExport::ExportChaosTriMesh(const Chaos::FTriangleMeshImplicitObject* const TriMesh, const FTransform& LocalToWorld)
{
	// This class is only used for exporting heightfield slices, nothing else should be called
	checkNoEntry();
}

void FGunfire3DNavigationGeometryExport::ExportChaosConvexMesh(const FKConvexElem* const Convex, const FTransform& LocalToWorld)
{
	// This class is only used for exporting heightfield slices, nothing else should be called
	checkNoEntry();
}

void FGunfire3DNavigationGeometryExport::ExportChaosHeightField(const Chaos::FHeightField* const Heightfield, const FTransform& LocalToWorld)
{
	// This class is only used for exporting heightfield slices, nothing else should be called
	checkNoEntry();
}

void FGunfire3DNavigationGeometryExport::ExportChaosHeightFieldSlice(const FNavHeightfieldSamples& PrefetchedHeightfieldSamples, const int32 NumRows, const int32 NumCols, const FTransform& LocalToWorld, const FBox& SliceBox)
{
	Gunfire3DNavigationGeometryExport::ExportChaosHeightFieldSlice(PrefetchedHeightfieldSamples, NumRows, NumCols, LocalToWorld, VertexBuffer, IndexBuffer, SliceBox, Data.Bounds);
}

void FGunfire3DNavigationGeometryExport::ExportRigidBodySetup(UBodySetup& BodySetup, const FTransform& LocalToWorld)
{
	// This class is only used for exporting heightfield slices, nothing else should be called
	checkNoEntry();
}

void FGunfire3DNavigationGeometryExport::ExportCustomMesh(const FVector* InVertices, int32 NumVerts, const int32* InIndices, int32 NumIndices, const FTransform& LocalToWorld)
{
	// This class is only used for exporting heightfield slices, nothing else should be called
	checkNoEntry();
}

void FGunfire3DNavigationGeometryExport::AddNavModifiers(const FCompositeNavModifier& Modifiers)
{
	// This class is only used for exporting heightfield slices, nothing else should be called
	checkNoEntry();
}

void FGunfire3DNavigationGeometryExport::SetNavDataPerInstanceTransformDelegate(const FNavDataPerInstanceTransformDelegate& InDelegate)
{
	// This class is only used for exporting heightfield slices, nothing else should be called
	checkNoEntry();
}