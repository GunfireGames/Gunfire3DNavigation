// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "AI/Navigation/NavigationTypes.h"

class FNavSvoUtils
{
public:
	// Removes all superfluous nodes along straight lines of the path.
	static void CleanUpPath(TArray<FNavPathPoint>& InOutPathPoints);

	// Strips a path of all nodes that are between nodes which have direct line of sight
	// to one another.
	static void StringPullPath(const class FSparseVoxelOctree& Octree, TArray<FNavPathPoint>& InOutPathPoints);

	// Smooths the path to remove any harsh angles via Catmull-Rom
	// 
	// NOTE: 'Alpha' specifies the shape of the spline. Most common shapes are Uniform
	// (0.0), Centripetal (0.5), and Chordal (1.0). See
	// http://www.cemyuksel.com/research/catmullrom_param/ for more information.
	static void SmoothPath(const class FSparseVoxelOctree& Octree, TArray<FNavPathPoint>& InOutPathPoints, float Alpha, uint8 Iterations);
};