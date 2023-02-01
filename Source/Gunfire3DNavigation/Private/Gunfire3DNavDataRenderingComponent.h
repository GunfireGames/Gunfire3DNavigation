// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "Components/PrimitiveComponent.h"

#include "Gunfire3DNavDataRenderingComponent.generated.h"

UCLASS(hidecategories = Object, editinlinenew)
class GUNFIRE3DNAVIGATION_API UGunfire3DNavRenderingComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UGunfire3DNavRenderingComponent();

	//~ Begin UPrimitiveComponent Interface
	virtual void OnRegister()  override;
	virtual void OnUnregister()  override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface

	//~ Begin USceneComponent Interface
public:
	virtual FBoxSphereBounds CalcBounds(const FTransform &LocalToWorld) const override;
	//~ End USceneComponent Interface

public:
	void ForceUpdate() { bForceUpdate = true; }
	bool IsForcingUpdate() const { return bForceUpdate; }

	static bool IsNavigationShowFlagSet(const UWorld* World);

protected:
	void TimerFunction();

protected:
	uint32 bCollectNavigationData : 1;
	uint32 bForceUpdate : 1;
	FTimerHandle TimerHandle;
};