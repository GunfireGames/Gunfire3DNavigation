// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "Gunfire3DNavQueryFilter.h"

#include "NavigationData.h"

enum class EGunfire3DNavPathFlags : uint8
{
	SkipStringPulling	= 1 << 0,
	SkipSmoothing		= 1 << 1,
};

enum class EGunfire3DNavPathQueryFlags : uint8
{
	// Detail flags
	PartialPath		= (uint8)EGunfire3DNavQueryFlags::UserFlags << 0,
	CyclicalPath	= (uint8)EGunfire3DNavQueryFlags::UserFlags << 1,
};
ENUM_CLASS_FLAGS(EGunfire3DNavPathQueryFlags);

struct FGunfire3DNavPathQueryResults : FGunfire3DNavQueryResults
{
	uint32 PathNodeCount = 0;
	float PathLength = 0.f;
	float PathCost = 0.f;
	TArray<FNavPathPoint> PathPortalPoints;

	virtual void Reset() override
	{
		FGunfire3DNavQueryResults::Reset();

		PathNodeCount = 0;
		PathLength = 0.f;
		PathCost = 0.f;
		PathPortalPoints.Reset();
	}

	bool IsPartial()
	{
		return (Status & (uint8)EGunfire3DNavPathQueryFlags::PartialPath) != 0;
	}

	bool RanOutOfNodes()
	{
		return (Status & (uint8)EGunfire3DNavQueryFlags::OutOfNodes) != 0;
	}
};

struct GUNFIRE3DNAVIGATION_API FGunfire3DNavPath : public FNavigationPath
{
	typedef FNavigationPath Super;

	FGunfire3DNavPath();

	// If true, the path will be tightened up to be more direct.
	bool WantsStringPulling() const { return bStringPull; }
	void SetWantsStringPulling(bool bValue) { bStringPull = bValue; }

	// If true, the path will be smoothed to remove harsh angles
	bool WantsSmoothing() const { return bSmooth; }
	void SetWantsSmoothing(bool bValue) { bSmooth = bValue; }

	// Information about how the path was generated
	FGunfire3DNavPathQueryResults& GetGenerationInfo() { return GenerationInfo; }
	const FGunfire3DNavPathQueryResults& GetGenerationInfo() const { return GenerationInfo; }

	// Applies custom flags to the path
	void ApplyFlags(uint32 NavDataFlags);

	//~ Begin FNavigationPath Interface

	// Resets all variables describing generated path before attempting new path finding
	// call. This function will NOT reset setup variables like goal actor, filter,
	// observer, etc
	virtual void ResetForRepath() override;

	// This is a duplicate of FNavigationPath::DebugDraw less the
	// NavigationDebugDrawing::PathOffset which isn't needed for nav volumes
	virtual void DebugDraw(const ANavigationData* NavData, const FColor PathColor, class UCanvas* Canvas, const bool bPersistent, const float LifeTime, const uint32 NextPathPointIndex = 0) const override;

	//~ End FNavigationPath Interface

public:
	static const FNavPathType Type;

private:
	bool bStringPull = true;
	bool bSmooth = true;

	FGunfire3DNavPathQueryResults GenerationInfo;
};