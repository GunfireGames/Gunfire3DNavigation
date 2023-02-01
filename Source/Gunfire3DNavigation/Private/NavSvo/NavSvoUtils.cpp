// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "NavSvoUtils.h"

#include "Gunfire3DNavigationUtils.h"
#include "Gunfire3DNavPath.h"
#include "Gunfire3DNavQueryFilter.h"
#include "NavSvo/NavSvoLocationQuery.h"
#include "NavSvo/NavSvoPathQuery.h"
#include "SparseVoxelOctree/SparseVoxelOctree.h"

///> Profiling stats
DECLARE_CYCLE_STAT(TEXT("CleanUpPath"), STAT_CleanUpPath, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("StringPullPath"), STAT_StringPullPath, STATGROUP_Gunfire3DNavigation);
DECLARE_CYCLE_STAT(TEXT("SmoothPath"), STAT_SmoothPath, STATGROUP_Gunfire3DNavigation);

void FNavSvoUtils::CleanUpPath(TArray<FNavPathPoint>& InOutPathPoints)
{
	SCOPE_CYCLE_COUNTER(STAT_CleanUpPath);

	// For each node B with neighbors A and C: Remove if the direction of AB is the same
	// as BC as this means that B is in the middle of the line from A to C.
	for (int32 PathPointIdx = 0; PathPointIdx < InOutPathPoints.Num() - 2; ++PathPointIdx)
	{
		FNavPathPoint& PathPointA = InOutPathPoints[PathPointIdx];
		FNavPathPoint& PathPointB = InOutPathPoints[PathPointIdx + 1];
		FNavPathPoint& PathPointC = InOutPathPoints[PathPointIdx + 2];

		FVector DirAB = (PathPointB.Location - PathPointA.Location);
		DirAB.Normalize();

		FVector DirBC = (PathPointC.Location - PathPointB.Location);
		DirBC.Normalize();

		if (DirAB.Equals(DirBC))
		{
			InOutPathPoints.RemoveAt(PathPointIdx + 1);
			--PathPointIdx;
		}
	}
}

// TODO: This isn't really string pulling and is just an algorithm which raycast between
// path nodes to remove redundant points. This is fine for now however we should implement
// a funnel algorithm which should yield better results.
void FNavSvoUtils::StringPullPath(const FSparseVoxelOctree& Octree, TArray<FNavPathPoint>& InOutPathPoints)
{
	SCOPE_CYCLE_COUNTER(STAT_StringPullPath);

	Gunfire3DNavigation::FRaycastResult RaycastResult;
	int32 LastPathPointIdx = InOutPathPoints.Num() - 1;

	for (int32 PathPointIdx = 0; PathPointIdx < LastPathPointIdx; ++PathPointIdx)
	{
		const FNavPathPoint& PathPointA = InOutPathPoints[PathPointIdx];
		const int32 NextPathPointIdx = PathPointIdx + 1;

		// Find the last node we can raycast against
		for (int32 FuturePathPointIdx = LastPathPointIdx; FuturePathPointIdx > NextPathPointIdx; --FuturePathPointIdx)
		{
			const FNavPathPoint& PathPointB = InOutPathPoints[FuturePathPointIdx];

			// If we successfully ray cast to this node then we need to remove all
			// other nodes in between.
			if (!Octree.Raycast(PathPointA.Location, PathPointB.Location, RaycastResult))
			{
				InOutPathPoints.RemoveAt(NextPathPointIdx, FuturePathPointIdx - NextPathPointIdx);
				LastPathPointIdx = InOutPathPoints.Num() - 1;
				break;
			}
		}
	}
}

void FNavSvoUtils::SmoothPath(const FSparseVoxelOctree& Octree, TArray<FNavPathPoint>& InOutPathPoints, float Alpha, uint8 Iterations)
{
	// https://qroph.github.io/2018/07/30/smooth-paths-using-catmull-rom-splines.html
	// https://andrewhungblog.wordpress.com/2017/03/03/catmull-rom-splines-in-plain-english/

	SCOPE_CYCLE_COUNTER(STAT_SmoothPath);

	// Don't bother processing straight line paths
	if (InOutPathPoints.Num() < 3)
	{
		return;
	}

	const int32 NumPathPoints = InOutPathPoints.Num();
	const int32 LastPathPointIdx = NumPathPoints - 1;
	const int32 SecondLastPathPointIdx = NumPathPoints - 2;
	
	// NOTE: For each path point, Catmull-Rom uses the previous point as well as the next
	// two points to calculate a curve for that segment. Because of this, we have to
	// create two extra points at the start and end of the path so we can calculate the
	// curves for the start and end segments properly. We do this by simply extending the
	// path in the same direction and length of the start and end segments, respectively.
	// These points are not added to the path and are only used to find the curve for the
	// currently processed segment.

	// Calculate an extra node at the start of the path to allow us to create a curve at
	// the first node.
	const FVector FirstSegmentDelta = (InOutPathPoints[0].Location - InOutPathPoints[1].Location);
	const float FirstSegmentDist = FirstSegmentDelta.Length();
	const FVector FirstPathPointPrev = InOutPathPoints[0].Location + FirstSegmentDelta.GetSafeNormal() * FirstSegmentDist;

	// Calculate an extra node at the end of the path to allow us to create a curve at the
	// last node.
	const FVector LastSegmentDelta = (InOutPathPoints[LastPathPointIdx].Location - InOutPathPoints[SecondLastPathPointIdx].Location);
	const float LastSegmentDist = LastSegmentDelta.Length();
	const FVector LastPathPointNext = InOutPathPoints[LastPathPointIdx].Location + LastSegmentDelta.GetSafeNormal() * LastSegmentDist;

	Gunfire3DNavigation::FRaycastResult RaycastResult;
	TArray<FNavPathPoint> NewPathPoints;
	NewPathPoints.Reserve(NumPathPoints * Iterations);

	for (int32 PathPointIdx = 0; PathPointIdx < LastPathPointIdx; ++PathPointIdx)
	{
		const FVector P0 = (PathPointIdx == 0) ? FirstPathPointPrev : InOutPathPoints[PathPointIdx - 1].Location;
		const FVector P1 = InOutPathPoints[PathPointIdx].Location;
		const FVector P2 = InOutPathPoints[PathPointIdx + 1].Location;
		const FVector P3 = (PathPointIdx == SecondLastPathPointIdx) ? LastPathPointNext : InOutPathPoints[PathPointIdx + 2].Location;

		// Add the starting handle
		// 
		// NOTE: The end handle will be added when processing the next path point.
		NewPathPoints.Add(P1);

		// Add all smoothing nodes between the start and end handles
		for (uint8 Iteration = 1; Iteration < (Iterations + 1); ++Iteration)
		{
			const float T = (float)Iteration / (float)(Iterations + 1);

			const float T0 = 0.f;
			const float T1 = T0 + FMath::Pow(FVector::Distance(P0, P1), Alpha);
			const float T2 = T1 + FMath::Pow(FVector::Distance(P1, P2), Alpha);
			const float T3 = T2 + FMath::Pow(FVector::Distance(P2, P3), Alpha);

			const FVector NewPoint = FMath::CubicCRSplineInterpSafe(P0, P1, P2, P3, T0, T1, T2, T3, FMath::Lerp(T1, T2, T));
			const FSvoNodeLink NodeLink = Octree.GetLinkForLocation(NewPoint);

			// Ensure the new path point is a valid point on the octree and that it can be
			// accessed by the start and end points of the segment without running into
			// anything.
			if (NodeLink.IsValid() &&
				!Octree.Raycast(NewPoint, P1, RaycastResult) &&
				!Octree.Raycast(NewPoint, P2, RaycastResult))
			{
				NewPathPoints.Add(FNavPathPoint(NewPoint, NodeLink.GetID()));
			}
		}
	}

	// Now add the destination and update the original path points.
	NewPathPoints.Add(InOutPathPoints[NumPathPoints - 1]);

	InOutPathPoints = NewPathPoints;
}