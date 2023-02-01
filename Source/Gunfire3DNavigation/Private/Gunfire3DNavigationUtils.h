// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "Gunfire3DNavigationTypes.h"
#include "StatArray.h"

#include "GameFramework/PlayerController.h"

struct FGunfire3DNavigationUtils
{
	// Given two coordinates, returns the Manhattan distance
	static uint32 GetManhattanDistance(const FIntVector& CoordA, const FIntVector& CoordB)
	{
		return FMath::Abs(CoordA.X - CoordB.X) + FMath::Abs(CoordA.Y - CoordB.Y) + FMath::Abs(CoordA.Z - CoordB.Z);
	}

	// The purpose of this test, and the difference from FBox::Intersect, is to ignore
	// overlaps in which only one face is touching and nothing else.
	//
	// TODO: Investigate why this was necessary.  I believe this has to do with one
	//		 specific case and should probably not be what we use everywhere. - JMM
	static bool AABBIntersectsAABB(const FBox& BoxA, const FBox& BoxB)
	{
		if (BoxA.Intersect(BoxB))
		{
			if (BoxA.Min.X == BoxB.Max.X || BoxB.Min.X == BoxA.Max.X)
				return false;

			if (BoxA.Min.Y == BoxB.Max.Y || BoxB.Min.Y == BoxA.Max.Y)
				return false;

			if (BoxA.Min.Z == BoxB.Max.Z || BoxB.Min.Z == BoxA.Max.Z)
				return false;

			return true;
		}

		return false;
	}

	// Returns true if a single AABB intersects (or is contained by) any AABB in a list.
	static bool AABBIntersectsAABBs(const FBox& TestBox, const TArray<FBox>& Bounds)
	{
		for (const FBox& Box : Bounds)
		{
			if (AABBIntersectsAABB(Box, TestBox))
			{
				return true;
			}
		}

		return false;
	}

	// Returns true if a single AABB is fully contained by any AABB in the list
	static bool AABBsContainAABB(const TArray<FBox>& Bounds, const FBox& TestBox)
	{
		for (const FBox& Box : Bounds)
		{
			if (AABBContainsAABB(Box, TestBox))
			{
				return true;
			}
		}

		return false;
	}

	// This is the same as FBox::Overlap without the intersection test which is expected
	// to have been performed before making this call.
	static FBox CalculateAABBOverlap(const FBox& BoxA, const FBox& BoxB)
	{
		FVector MinVector, MaxVector;

		MinVector.X = FMath::Max(BoxA.Min.X, BoxB.Min.X);
		MaxVector.X = FMath::Min(BoxA.Max.X, BoxB.Max.X);

		MinVector.Y = FMath::Max(BoxA.Min.Y, BoxB.Min.Y);
		MaxVector.Y = FMath::Min(BoxA.Max.Y, BoxB.Max.Y);

		MinVector.Z = FMath::Max(BoxA.Min.Z, BoxB.Min.Z);
		MaxVector.Z = FMath::Min(BoxA.Max.Z, BoxB.Max.Z);

		return FBox(MinVector, MaxVector);
	}

	// Determines if a box contains or overlaps a specific vector
	static bool AABBContainsOrOverlapsVector(const FBox& Box, const FVector& Vector)
	{
		return (Vector.X >= Box.Min.X) && (Vector.X <= Box.Max.X)
			&& (Vector.Y >= Box.Min.Y) && (Vector.Y <= Box.Max.Y)
			&& (Vector.Z >= Box.Min.Z) && (Vector.Z <= Box.Max.Z);
	}

	// Main difference between this and FBox::ContainsBox is that this returns true also
	// when edges overlap
	static bool AABBContainsAABB(const FBox& BoxA, const FBox& BoxB)
	{
		return AABBContainsOrOverlapsVector(BoxA, BoxB.Min) && AABBContainsOrOverlapsVector(BoxA, BoxB.Max);
	}

	// Tests a ray against an AABB (utilized by FSparseVoxelOctree::Raycast) At the time
	// of writing this there wasn't a Line/AABB test within UE4 which returned both a min
	// and max parameter. This is an adapted and modified version of the one listed in the
	// link below to allow for an epsilon.
	// https://tavianator.com/fast-branchless-raybounding-box-intersections-part-2-nans/
	static bool RayAABBIntersect(const FVector &Origin, const FVector &Dir, const FBox& AABB, float& TMin, float& TMax)
	{
		float t1 = (AABB.Min[0] - Origin[0]) * (1.f / Dir[0]);
		float t2 = (AABB.Max[0] - Origin[0]) * (1.f / Dir[0]);

		TMin = FMath::Min(t1, t2);
		TMax = FMath::Max(t1, t2);

		for (int32 SlabIdx = 1; SlabIdx < 3; ++SlabIdx)
		{
			if (Dir[SlabIdx] != 0.0)
			{
				float T1 = (AABB.Min[SlabIdx] - Origin[SlabIdx]) * (1.f / Dir[SlabIdx]);
				float T2 = (AABB.Max[SlabIdx] - Origin[SlabIdx]) * (1.f / Dir[SlabIdx]);

				TMin = FMath::Max(TMin, FMath::Min(T1, T2));
				TMax = FMath::Min(TMax, FMath::Max(T1, T2));
			}
		}

		return TMax > FMath::Max(TMin, 0.f);
	}

	// Collects all player locations
	static void GetPlayerLocations(const UWorld* World, TArray<FVector>& Locations)
	{
		for (FConstPlayerControllerIterator PlayerIt = World->GetPlayerControllerIterator(); PlayerIt; ++PlayerIt)
		{
			if (PlayerIt->IsValid())
			{
				if (APawn* PlayerPawn = (*PlayerIt)->GetPawn())
				{
					Locations.Add(PlayerPawn->GetActorLocation());
				}
			}
		}
	}
};