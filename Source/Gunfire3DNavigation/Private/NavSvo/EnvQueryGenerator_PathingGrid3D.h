// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "DataProviders/AIDataProvider.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"

#include "EnvQueryGenerator_PathingGrid3D.generated.h"

//
// Navigation grid, generates points in a 3D grid on 3D navmesh
// with paths to/from context
//
UCLASS(meta = (DisplayName = "Points: 3D Pathing Grid"))
class GUNFIRE3DNAVIGATION_API UEnvQueryGenerator_PathingGrid3D : public UEnvQueryGenerator
{
	GENERATED_BODY()

	// Half of square's extent, like a radius
	UPROPERTY(EditDefaultsOnly, Category = Generator)
	FAIDataProviderFloatValue GridHalfSize;

	// Grid spacing in the X/Y/Z axis
	UPROPERTY(EditDefaultsOnly, Category = Generator)
	FAIDataProviderFloatValue SpaceBetween;

	// The minimum height of the grid relative to the context (a negative value here is
	// below the context that many units). Will be clamped to a multiple of SpaceBetween.
	UPROPERTY(EditDefaultsOnly, Category = Generator)
	FAIDataProviderFloatValue MinHeight;

	// The maximum height of the grid relative to the context. Will be clamped to a
	// multiple of SpaceBetween.
	UPROPERTY(EditDefaultsOnly, Category = Generator)
	FAIDataProviderFloatValue MaxHeight;

	UPROPERTY(EditDefaultsOnly, Category = Generator)
	TSubclassOf<UEnvQueryContext> GenerateAround;

	UEnvQueryGenerator_PathingGrid3D();

	virtual void GenerateItems(FEnvQueryInstance& QueryInstance) const override;

	virtual FText GetDescriptionTitle() const override;
	virtual FText GetDescriptionDetails() const override;
};
