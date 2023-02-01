// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "Gunfire3DNavPath.h"

#include "DrawDebugHelpers.h"
#include "NavigationSystem.h"

const FNavPathType FGunfire3DNavPath::Type;

FGunfire3DNavPath::FGunfire3DNavPath()
	: bStringPull(true)
	, bSmooth(true)
{
	PathType = FGunfire3DNavPath::Type;
}

void FGunfire3DNavPath::ApplyFlags(uint32 NavDataFlags)
{
	if (NavDataFlags & (uint32)EGunfire3DNavPathFlags::SkipStringPulling)
	{
		bStringPull = false;
	}

	if (NavDataFlags & (uint32)EGunfire3DNavPathFlags::SkipSmoothing)
	{
		bSmooth = false;
	}
}

void FGunfire3DNavPath::ResetForRepath()
{
	Super::ResetForRepath();

	GenerationInfo.Reset();
}

void FGunfire3DNavPath::DebugDraw(const ANavigationData* NavData, const FColor PathColor, class UCanvas* Canvas, const bool bPersistent, const float LifeTime, const uint32 NextPathPointIndex) const
{
#if ENABLE_DRAW_DEBUG

	static const FColor Grey(100, 100, 100);
	const int32 NumPathVerts = PathPoints.Num();

	UWorld* World = NavData->GetWorld();

	for (int32 VertIdx = 0; VertIdx < NumPathVerts - 1; ++VertIdx)
	{
		// Draw box at vert
		FVector const VertLoc = PathPoints[VertIdx].Location;
		DrawDebugSolidBox(World, VertLoc, NavigationDebugDrawing::PathNodeBoxExtent, VertIdx < int32(NextPathPointIndex) ? Grey : PathColor, bPersistent, LifeTime);

		// Draw line to next loc
		FVector const NextVertLoc = PathPoints[VertIdx + 1].Location;
		DrawDebugLine(World, VertLoc, NextVertLoc, VertIdx < int32(NextPathPointIndex) - 1 ? Grey : PathColor, bPersistent
			, LifeTime, /*DepthPriority*/0
			, /*Thickness*/NavigationDebugDrawing::PathLineThickness);
	}

	// Draw last vert
	if (NumPathVerts > 0)
	{
		DrawDebugBox(World, PathPoints[NumPathVerts - 1].Location, NavigationDebugDrawing::PathNodeBoxExtent, PathColor, bPersistent);
	}

#endif
}