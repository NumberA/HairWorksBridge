// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "AbilitySystemPrivatePCH.h"
#include "Abilities/Tasks/AbilityTask_WaitMovementModeChange.h"

UAbilityTask_WaitMovementModeChange::UAbilityTask_WaitMovementModeChange(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RequiredMode = MOVE_None;
}

UAbilityTask_WaitMovementModeChange* UAbilityTask_WaitMovementModeChange::CreateWaitMovementModeChange(class UObject* WorldContextObject, EMovementMode NewMode)
{
	auto MyObj = NewTask<UAbilityTask_WaitMovementModeChange>(WorldContextObject);
	MyObj->RequiredMode = NewMode;
	return MyObj;
}

void UAbilityTask_WaitMovementModeChange::Activate()
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActor());
	if (Character)
	{
		Character->MovementModeChangedDelegate.AddDynamic(this, &UAbilityTask_WaitMovementModeChange::OnMovementModeChange);
		MyCharacter = Character;
	}
}

void UAbilityTask_WaitMovementModeChange::OnMovementModeChange(ACharacter * Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	if (Character)
	{
		if (UCharacterMovementComponent *MoveComp = Cast<UCharacterMovementComponent>(Character->GetMovementComponent()))
		{
			if (RequiredMode == MOVE_None || MoveComp->MovementMode == RequiredMode)
			{
				OnChange.Broadcast(MoveComp->MovementMode);
				EndTask();
				return;
			}
		}
	}
}

void UAbilityTask_WaitMovementModeChange::OnDestroy(bool AbilityEnded)
{
	if (MyCharacter.IsValid())
	{
		MyCharacter->MovementModeChangedDelegate.RemoveDynamic(this, &UAbilityTask_WaitMovementModeChange::OnMovementModeChange);
	}

	Super::OnDestroy(AbilityEnded);
}