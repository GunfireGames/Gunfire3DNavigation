// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "NavSvoCollider.h"

#include "Gunfire3DNavigationGeometryExport.h"

#include "DrawDebugHelpers.h"
#include "NavigationSystem.h"
#include "NavAreas/NavArea.h"
#if WITH_RECAST
// Recast-specific helpers
#include "NavMesh/RecastHelpers.h"
#endif

// Profiling stats
DECLARE_CYCLE_STAT(TEXT("GatherGeometry (FNavigationOctreeCollider)"), STAT_FNavigationOctreeCollider_GatherGeometry, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("GatherGeometry : Lazy Geometry Export (FNavigationOctreeCollider)"), STAT_FNavigationOctreeCollider_GatherGeometry_LazyGeometryExport, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("GatherGeometry : Landscape Slices Exporting (FNavigationOctreeCollider)"), STAT_FNavigationOctreeCollider_GatherGeometry_LandscapeSlicesExporting, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("GatherGeometrySources (FNavigationOctreeCollider)"), STAT_FNavigationOctreeCollider_GatherGeometrySources, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("GatherGeometryFromSources (FNavigationOctreeCollider)"), STAT_FNavigationOctreeCollider_GatherGeometryFromSources, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("GatherGeometryFromSources : Landscape Slices Exporting (FNavigationOctreeCollider)"), STAT_FNavigationOctreeCollider_GatherGeometryFromSources_LandscapeSlicesExporting, STATGROUP_Gunfire3DNavigation);

// Memory stats
DECLARE_MEMORY_STAT(TEXT("Raw Geometry (FNavigationOctreeCollider)"), STAT_FNavigationOctreeCollider_RawGeometry, STATGROUP_Gunfire3DNavigation);
DECLARE_MEMORY_STAT(TEXT("Modifiers (FNavigationOctreeCollider)"), STAT_FNavigationOctreeCollider_Modifiers, STATGROUP_Gunfire3DNavigation);

//////////////////////////////////////////////////////////////////////////
//

FNavigationOctreeCollider::FNavigationOctreeCollider()
#if STATS
	: Modifiers(GET_STATID(STAT_FNavigationOctreeCollider_Modifiers))
	, Blockers(GET_STATID(STAT_FNavigationOctreeCollider_Modifiers))
#endif
{}

FNavigationOctreeCollider::~FNavigationOctreeCollider()
{
	DEC_DWORD_STAT_BY(STAT_FNavigationOctreeCollider_RawGeometry, CulledTriangles.GetAllocatedSize());
	DEC_DWORD_STAT_BY(STAT_Gunfire3DNavigation_TotalMemory, CulledTriangles.GetAllocatedSize());
}

bool FNavigationOctreeCollider::HasCollisionData() const
{
	return (CulledTriangles.Num() > 0 || Blockers.Num() > 0) || NavigationRelevantData.Num() > 0;;
}

void FNavigationOctreeCollider::ValidateAndAppendGeometry(TSharedRef<FNavigationRelevantData, ESPMode::ThreadSafe> ElementData, const FBox& Bounds)
{
	const FNavigationRelevantData& DataRef = ElementData.Get();
	if (DataRef.IsCollisionDataValid())
	{
		AppendGeometry(DataRef.CollisionData, Bounds, DataRef.NavDataPerInstanceTransformDelegate);
	}
}

void FNavigationOctreeCollider::AppendGeometry(const TNavStatArray<uint8>& RawCollisionCache, const FBox& Bounds, const FNavDataPerInstanceTransformDelegate& InTransformsDelegate)
{
	if (RawCollisionCache.Num() == 0)
	{
		return;
	}

	FGeometryCache CollisionCache(RawCollisionCache.GetData());

	TArray<FTransform> PerInstanceTransform;

	// Gather per instance transforms
	if (InTransformsDelegate.IsBound())
	{
		InTransformsDelegate.Execute(Bounds, PerInstanceTransform);
		if (PerInstanceTransform.Num() == 0)
		{
			return;
		}
	}

	if (CollisionCache.Header.NumFaces == 0)
	{
		return;
	}

	// We could do a more fancy separating axis test here to check that the triangle 100%
	// doesn't overlap the bounds, but it's a lot slower than this simple "is this
	// triangle fully outside one of the planes of this box" check, and this will exclude
	// the bulk of the triangles we don't care about.
	const auto IsOutOfBounds = [Bounds](const FNavigationOctreeCollider::FTriangle& Tri) -> bool
	{
		return
			(FMath::Max3(Tri.Vertices[0].X, Tri.Vertices[1].X, Tri.Vertices[2].X) < Bounds.Min.X) ||
			(FMath::Max3(Tri.Vertices[0].Y, Tri.Vertices[1].Y, Tri.Vertices[2].Y) < Bounds.Min.Y) ||
			(FMath::Max3(Tri.Vertices[0].Z, Tri.Vertices[1].Z, Tri.Vertices[2].Z) < Bounds.Min.Z) ||
			(FMath::Min3(Tri.Vertices[0].X, Tri.Vertices[1].X, Tri.Vertices[2].X) > Bounds.Max.X) ||
			(FMath::Min3(Tri.Vertices[0].Y, Tri.Vertices[1].Y, Tri.Vertices[2].Y) > Bounds.Max.Y) ||
			(FMath::Min3(Tri.Vertices[0].Z, Tri.Vertices[1].Z, Tri.Vertices[2].Z) > Bounds.Max.Z);
	};

	FVector TriangleVertices[3];

	bool bIsInstanced = (PerInstanceTransform.Num() > 0);
	const int32 NumInstances = FMath::Max(1, PerInstanceTransform.Num());

	const FVector::FReal* VertexCoords = CollisionCache.Verts;

	for (int32 Face = 0; Face < CollisionCache.Header.NumFaces; ++Face)
	{
		const int32* VertexIndexPtr = &CollisionCache.Indices[Face * 3];

		for (uint8 TriangleVertexIdx = 0; TriangleVertexIdx < 3; ++TriangleVertexIdx)
		{
			const int32 CoordTriangleIdx = VertexIndexPtr[TriangleVertexIdx];
			const FVector::FReal* VertexCoordPtr = &VertexCoords[CoordTriangleIdx * 3];

			for (uint8 VertexCoordIdx = 0; VertexCoordIdx < 3; ++VertexCoordIdx)
			{
				TriangleVertices[TriangleVertexIdx][VertexCoordIdx] = VertexCoordPtr[VertexCoordIdx];
			}

#if WITH_RECAST
			// Because Recast is injected into the NavigationSystem and processes
			// geometry when stored, we need to undo that to put things back into
			// Unreal-compatible coordinates.
			TriangleVertices[TriangleVertexIdx] = Recast2UnrealPoint(TriangleVertices[TriangleVertexIdx]);
#endif // WITH_RECAST
		}

		// We need to reverse the winding order of the triangles so they face correctly
		// in Unreal to account for adjustments by recast.
#if WITH_RECAST
		Swap(TriangleVertices[1], TriangleVertices[2]);
#endif // WITH_RECAST

		// If this geometry is instanced, we need to apply the world transforms and add
		// each triangle.  Otherwise we just add this triangle as-is.
		if (bIsInstanced)
		{
			for (const FTransform& InstanceTransform : PerInstanceTransform)
			{
				FTriangle Triangle;
				Triangle.Vertices[0] = InstanceTransform.TransformPosition(TriangleVertices[0]);
				Triangle.Vertices[1] = InstanceTransform.TransformPosition(TriangleVertices[1]);
				Triangle.Vertices[2] = InstanceTransform.TransformPosition(TriangleVertices[2]);

#if PROFILE_SVO_GENERATION
				++TotalTriangles;
#endif
				if (!IsOutOfBounds(Triangle))
				{
					CulledTriangles.AddElement(Triangle);

#if PROFILE_SVO_GENERATION
					++UsedTriangles;
#endif
				}
			}
		}
		else
		{
			FTriangle Triangle;
			Triangle.Vertices[0] = TriangleVertices[0];
			Triangle.Vertices[1] = TriangleVertices[1];
			Triangle.Vertices[2] = TriangleVertices[2];

#if PROFILE_SVO_GENERATION
			++TotalTriangles;
#endif
			if (!IsOutOfBounds(Triangle))
			{
				CulledTriangles.AddElement(Triangle);

#if PROFILE_SVO_GENERATION
				++UsedTriangles;
#endif
			}
		}
	}
}

void FNavigationOctreeCollider::AppendModifier(const FCompositeNavModifier& Modifier, const FBox& Bounds, const FNavDataPerInstanceTransformDelegate& InTransformsDelegate)
{
	if (Modifier.GetAreas().Num() == 0)
	{
		return;
	}

	FModifier ModifierElement;

	// Gather per instance transforms if any
	if (InTransformsDelegate.IsBound())
	{
		InTransformsDelegate.Execute(Bounds, ModifierElement.PerInstanceTransform);
		// skip this modifier in case there is no instances for this tile
		if (ModifierElement.PerInstanceTransform.Num() == 0)
		{
			return;
		}
	}

	ModifierElement.Areas = Modifier.GetAreas();

	// We cache off info about modifiers, but we don't currently support them as a way to
	// tag areas of flying nav. We would like to support the case of treating them as
	// essentially blocking geo though, so if their area flags are zero (don't generate
	// nav here), extract out some relevant info.
	for (int32 AreaIdx = ModifierElement.Areas.Num() - 1; AreaIdx >= 0; --AreaIdx)
	{
		const FAreaNavModifier& Area = ModifierElement.Areas[AreaIdx];

		UNavArea* NavArea = Cast<UNavArea>(Area.GetAreaClass().GetDefaultObject());
		if (!NavArea || NavArea->GetAreaFlags() != 0 || !SupportedAreas.Contains(NavArea->GetClass()))
		{
			continue;
		}

		// Currently we only support the convex shape type, since that's what we're
		// currently using in game, but we could add support for the others too.
		if (Area.GetShapeType() == ENavigationShapeType::Convex)
		{
			FConvexNavAreaData ConvexData;
			Area.GetConvex(ConvexData);

			Blockers.Emplace(MoveTemp(ConvexData));

			// Since we converted the area to a blocker remove it from the list
			ModifierElement.Areas.RemoveAtSwap(AreaIdx);
		}
	}

	Modifiers.Add(MoveTemp(ModifierElement));
}

void FNavigationOctreeCollider::GatherGeometry(UWorld* World, const FNavDataConfig& NavDataConfig, const FBox& Bounds)
{
	SCOPE_CYCLE_COUNTER(STAT_FNavigationOctreeCollider_GatherGeometry);

	UNavigationSystemV1* NavigationSystem = Cast<UNavigationSystemV1>(World->GetNavigationSystem());
	FNavigationOctree* NavigationOctree = NavigationSystem ? NavigationSystem->GetMutableNavOctree() : nullptr;
	if (NavigationOctree == nullptr)
		return;

	NavigationOctree->FindElementsWithBoundsTest(Bounds, [&](const FNavigationOctreeElement& Element)
	{
		const bool bShouldUse = Element.ShouldUseGeometry(NavDataConfig);
		if (bShouldUse)
		{
			const TNavigationData& NavData = Element.Data;

			bool bDumpGeometryData = false;

			if (NavData->IsPendingLazyGeometryGathering() || NavData->IsPendingLazyModifiersGathering())
			{
				const bool bSupportsSlices = NavData->SupportsGatheringGeometrySlices();

				if (bSupportsSlices == false || NavData->IsPendingLazyModifiersGathering() == true)
				{
					SCOPE_CYCLE_COUNTER(STAT_FNavigationOctreeCollider_GatherGeometry_LazyGeometryExport);
					NavigationOctree->DemandLazyDataGathering(*NavData);
				}

				if (bSupportsSlices == true)
				{
					SCOPE_CYCLE_COUNTER(STAT_FNavigationOctreeCollider_GatherGeometry_LandscapeSlicesExporting);

					INavRelevantInterface* NavRelevant = const_cast<INavRelevantInterface*>(Cast<const INavRelevantInterface>(Element.GetOwner()));
					if (NavRelevant)
					{
						NavRelevant->PrepareGeometryExportSync();

						FGunfire3DNavigationGeometryExport GeomExport(*NavData);
						NavRelevant->GatherGeometrySlice(GeomExport, Bounds);
						GeomExport.StoreCollisionCache();

						bDumpGeometryData = true;
					}
					else
					{
						UE_LOG(LogNavigation, Error, TEXT("GatherGeometry: got an invalid NavRelevant instance!"));
					}
				}
			}

			const bool bExportGeometry = NavData->HasGeometry();
			if (bExportGeometry)
			{
				ValidateAndAppendGeometry(NavData, Bounds);

				if (bDumpGeometryData)
				{
					const_cast<FNavigationRelevantData&>(*NavData).CollisionData.Empty();
				}
			}

			const FCompositeNavModifier ModifierInstance = Element.GetModifierForAgent(&NavDataConfig);
			if (ModifierInstance.IsEmpty() == false)
			{
				AppendModifier(ModifierInstance, Bounds, NavData->NavDataPerInstanceTransformDelegate);
			}
		}
	});

	INC_DWORD_STAT_BY(STAT_FNavigationOctreeCollider_RawGeometry, CulledTriangles.GetAllocatedSize());
	INC_DWORD_STAT_BY(STAT_Gunfire3DNavigation_TotalMemory, CulledTriangles.GetAllocatedSize());
}

void FNavigationOctreeCollider::GatherGeometrySources(UWorld* World, const FNavDataConfig& NavDataConfig, const FBox& Bounds)
{
	SCOPE_CYCLE_COUNTER(STAT_FNavigationOctreeCollider_GatherGeometrySources);

	UNavigationSystemV1* NavigationSystem = Cast<UNavigationSystemV1>(World->GetNavigationSystem());
	FNavigationOctree* NavigationOctreeInstance = NavigationSystem ? NavigationSystem->GetMutableNavOctree() : nullptr;
	check(NavigationOctreeInstance);

	NavigationOctreeCached = NavigationOctreeInstance->AsShared();

	NavigationRelevantData.Reset();

	NavDataConfigCached = NavDataConfig;

	NavigationOctreeInstance->FindElementsWithBoundsTest(Bounds, [&](const FNavigationOctreeElement& Element)
		{
			const bool bShouldUse = Element.ShouldUseGeometry(NavDataConfig);
			if (bShouldUse)
			{
				const bool bExportGeometry = (Element.Data->HasGeometry() || Element.Data->IsPendingLazyGeometryGathering());
				const bool bExportModifiers = (Element.Data->IsPendingLazyModifiersGathering() || Element.Data->Modifiers.HasMetaAreas() == true || Element.Data->Modifiers.IsEmpty() == false);

				if (bExportGeometry || bExportModifiers)
				{
					NavigationRelevantData.Add(Element.Data);
				}
			}
		});
}

void FNavigationOctreeCollider::GatherGeometryFromSources(const FBox& Bounds)
{
	SCOPE_CYCLE_COUNTER(STAT_FNavigationOctreeCollider_GatherGeometryFromSources);

	for (TNavigationData& NavData : NavigationRelevantData)
	{
		if (NavData->GetOwner() == nullptr)
		{
			UE_LOG(LogNavigation, Warning, TEXT("GatherGeometry: skipping an element with no longer valid Owner"));
			continue;
		}

		bool bDumpGeometryData = false;

		if (NavData->IsPendingLazyGeometryGathering() && NavData->SupportsGatheringGeometrySlices())
		{
			SCOPE_CYCLE_COUNTER(STAT_FNavigationOctreeCollider_GatherGeometryFromSources_LandscapeSlicesExporting);

			INavRelevantInterface* NavRelevant = Cast<INavRelevantInterface>(NavData->GetOwner());
			if (NavRelevant)
			{
				NavRelevant->PrepareGeometryExportSync();

				FGunfire3DNavigationGeometryExport GeomExport(*NavData);
				NavRelevant->GatherGeometrySlice(GeomExport, Bounds);
				GeomExport.StoreCollisionCache();

				bDumpGeometryData = true;
			}
			else
			{
				UE_LOG(LogNavigation, Error, TEXT("GatherGeometry: got an invalid NavRelevant instance!"));
			}
		}

		if (NavData->IsPendingLazyGeometryGathering() || NavData->IsPendingLazyModifiersGathering())
		{
			NavigationOctreeCached->DemandLazyDataGathering(*NavData);
		}

		const bool bExportGeometry = NavData->HasGeometry();
		if (bExportGeometry)
		{
			ValidateAndAppendGeometry(NavData, Bounds);

			if (bDumpGeometryData)
			{
				const_cast<FNavigationRelevantData&>(*NavData).CollisionData.Empty();
			}
		}

		const FCompositeNavModifier ModifierInstance = NavData->Modifiers.HasMetaAreas() ? NavData->Modifiers.GetInstantiatedMetaModifier(&NavDataConfigCached, NavData->SourceObject) : NavData->Modifiers;
		if (ModifierInstance.IsEmpty() == false)
		{
			AppendModifier(ModifierInstance, Bounds, NavData->NavDataPerInstanceTransformDelegate);
		}
	}
}
