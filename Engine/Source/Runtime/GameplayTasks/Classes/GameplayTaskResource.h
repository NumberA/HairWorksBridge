// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GameplayTaskResource.generated.h"

UCLASS(Abstract, config = "Game", hidedropdown)
class GAMEPLAYTASKS_API UGameplayTaskResource : public UObject
{
	GENERATED_BODY()

protected:
	/** Overrides AutoResourceID. -1 means auto ID will be applied */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Task Resource", meta = (ClampMin = "-1", ClampMax = "15", UIMin = "-1", UIMax = "15", EditCondition = "bManuallySetID"), config)
	int32 ManualResourceID;

private:
	UPROPERTY()
	int8 AutoResourceID;

public:
	UPROPERTY()
	uint32 bManuallySetID : 1;

public:

	UGameplayTaskResource(const FObjectInitializer& ObjectInitializer);

	uint8 GetResourceID() const
	{
		return bManuallySetID || (ManualResourceID != INDEX_NONE) ? ManualResourceID : AutoResourceID;
	}

	template <class T>
	static uint8 GetResourceID()
	{
		return GetDefault<T>()->GetResourceID();
	}

	static uint8 GetResourceID(TSubclassOf<UGameplayTaskResource>& RequiredResource)
	{
		return RequiredResource->GetDefaultObject<UGameplayTaskResource>()->GetResourceID();
	}

	virtual void PostInitProperties() override;

protected:
#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif // WITH_EDITOR

	void UpdateAutoResourceID();
};

