// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "LeapMotionControllerPrivatePCH.h"
#include "LeapMotionControllerComponent.h"

#include "LeapMotionControllerPlugin.h"
#include "LeapMotionDevice.h"
#include "LeapMotionHandActor.h"
#include "LeapMotionTypes.h"

#include "Runtime/HeadMountedDisplay/Public/IHeadMountedDisplay.h"


ULeapMotionControllerComponent::ULeapMotionControllerComponent(const class FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

//#	define LM_ASSETS_FOLDER TEXT("/Game") // used by in-project plugin
#	define LM_ASSETS_FOLDER TEXT("/LeapMotionController") // used by in-engine plugin

	ArmMesh = ConstructorHelpers::FObjectFinder<UStaticMesh>(LM_ASSETS_FOLDER TEXT("/LM_CapsuleMesh")).Object;
	PalmMesh = ConstructorHelpers::FObjectFinder<UStaticMesh>(LM_ASSETS_FOLDER TEXT("/LM_TorusMesh")).Object;
	FingerMesh = ConstructorHelpers::FObjectFinder<UStaticMesh>(LM_ASSETS_FOLDER TEXT("/LM_CapsuleMesh")).Object;
	Material = ConstructorHelpers::FObjectFinder<UMaterialInterface>(LM_ASSETS_FOLDER TEXT("/LM_HandColor")).Object;

	// Make sure this component ticks
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bAutoActivate = true;

	Scale = 5.0f;
	ScaleForHmdMode = 1.6f;

	bHmdMode = false;
	bShowCollider = true;
	bShowMesh = true;

	OffsetFromHMDToLeapDevice = FVector(7.0f, 0.0f, 0.0f);
}

void ULeapMotionControllerComponent::GetAllHandIds(TArray<int32>& OutHandIds) const
{
	OutHandIds.Empty();
	for (TPair<int32, ALeapMotionHandActor*> HandIdActorPair : HandActors)
	{
		OutHandIds.Add(HandIdActorPair.Key);
	}
}

void ULeapMotionControllerComponent::GetAllHandActors(TArray<ALeapMotionHandActor*>& OutHandActors) const
{
	OutHandActors.Empty();
	for (TPair<int32, ALeapMotionHandActor*> HandIdActorPair : HandActors)
	{
		OutHandActors.Add(HandIdActorPair.Value);
	}
}

ALeapMotionHandActor* ULeapMotionControllerComponent::GetHandActor(int32 HandId) const
{
	ALeapMotionHandActor*const* HandActor = HandActors.Find(HandId);
	return HandActor ? *HandActor : nullptr;
}

ALeapMotionHandActor* ULeapMotionControllerComponent::GetOldestLeftOrRightHandActor(ELeapSide LeapSide) const
{
	ALeapMotionHandActor* OldestHand = nullptr;
	for (TPair<int32, ALeapMotionHandActor*> HandIdActorPair : HandActors)
	{
		ALeapMotionHandActor* HandActor = HandIdActorPair.Value;
		if (HandActor->HandSide == LeapSide)
		{
			if (!OldestHand || HandActor->CreationTime < OldestHand->CreationTime) // If two hand are created at the same time, selection here is dependent on HandIds' order in the map
			{
				OldestHand = HandActor;
			}
		}
	}
	return OldestHand;
}

void ULeapMotionControllerComponent::UseHmdMode(bool EnableOrDisable)
{
	bHmdMode = EnableOrDisable;
	
	FLeapMotionDevice* Device = FLeapMotionControllerPlugin::GetLeapDeviceSafe();
	if (Device && Device->IsConnected())
	{
		Device->SetHmdPolicy(bHmdMode);
	}
}

void ULeapMotionControllerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bAutoAttachToPlayerCamera)
	{
		AttachControllerToPlayerCamera(0);
	}

	AddAndRemoveHands();
	UdpateHandsPositions(DeltaTime);
}

void ULeapMotionControllerComponent::PostInitProperties()
{
	Super::PostInitProperties();
	UseHmdMode(bHmdMode);
}

#if WITH_EDITOR
void ULeapMotionControllerComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UseHmdMode(bHmdMode);
}
#endif

void ULeapMotionControllerComponent::AddAndRemoveHands()
{
	FLeapMotionDevice* Device = FLeapMotionControllerPlugin::GetLeapDeviceSafe();
	if (Device && Device->IsConnected())
	{
		Device->SetReferenceFrameOncePerTick();

		TArray<int32> AllHands; Device->GetAllHandsIds(AllHands);

		TSet<int32> oldIds; 
		TSet<int32> newIds;

		for (int32 HandId : LastFrameHandIds)
		{
			oldIds.Add(HandId);
		}
		for (int32 HandId : AllHands) 
		{
			newIds.Add(HandId); 
		}

		// Removed callbacks are triggered, while reference Leap Motion frame is no longer available.
		for (int32 id : oldIds) 
		{
			if (!newIds.Find(id)) 
			{
				OnHandRemoved.Broadcast(id); 
				OnHandRemovedImpl(id); 
				LastFrameHandIds.Remove(id); 
			} 
		}
		for (int32 id : newIds) 
		{
			if (!oldIds.Find(id)) 
			{
				LastFrameHandIds.Add(id); 
				OnHandAddedImpl(id); 
				OnHandAdded.Broadcast(id); 
			} 
		}

		check(newIds.Num() == LastFrameHandIds.Num());
	}
}

void ULeapMotionControllerComponent::UdpateHandsPositions(float DeltaSeconds)
{
	for (TPair<int32, ALeapMotionHandActor*> HandIdActorPair : HandActors)
	{
		int32 Id = HandIdActorPair.Key;
		OnHandUpdatedImpl(Id, DeltaSeconds);
		OnHandUpdated.Broadcast(Id, DeltaSeconds);
	}
}

void ULeapMotionControllerComponent::AttachControllerToPlayerCamera(int PlayerIndex)
{
	APlayerCameraManager* PlayerCameraManager = UGameplayStatics::GetPlayerCameraManager(this, PlayerIndex);

	// Handle attachment to player
	if (PlayerCameraManager)
	{
		bool bJustAttached = false;
		if (PlayerCameraManager->GetRootComponent() != GetAttachParent())
		{
			// attach
			AttachTo(PlayerCameraManager->GetRootComponent(), NAME_None, EAttachLocation::KeepRelativeOffset, false);
			bJustAttached = true;
		}

		const bool bUsingHmd = GEngine->HMDDevice.IsValid() && GEngine->HMDDevice->IsHeadTrackingAllowed(); // didn't know how to resolve linking to UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled()

		// Update just once for desktop mode
		if (bJustAttached || bUsingHmd != bHmdMode)
		{
			bHmdMode = bUsingHmd;
			UseHmdMode(bHmdMode);

			if (!bHmdMode)
			{
				// Update relative position once for desktop use
				FVector OffsetFromCameraToLeapDeviceForUnitScale(20.0f, 0.0f, -20.0f);
				SetRelativeLocationAndRotation(OffsetFromCameraToLeapDeviceForUnitScale * Scale, FRotator::ZeroRotator);
			}
		}


		// Update position: once for desktop mode, continuously for HMD
		if (bHmdMode)
		{
			// Update position every frame when using HMD
			SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);

			// Take into account HMD's positional tracking.
			FQuat OrientationAsQuat;
			FVector HmdPosition;
			GEngine->HMDDevice->GetCurrentOrientationAndPosition(OrientationAsQuat, HmdPosition);

			FRotator ControllerRotation = FRotator::ZeroRotator;
			ControllerRotation.Yaw = PlayerCameraManager->GetRootComponent()->GetComponentRotation().Yaw - OrientationAsQuat.Rotator().Yaw;

			AddWorldOffset(ControllerRotation.RotateVector(HmdPosition));

			AddLocalOffset(OffsetFromHMDToLeapDevice);
		}
	}
}

void ULeapMotionControllerComponent::OnHandAddedImpl(int32 HandId)
{
	FVector SpawnLocation = GetComponentLocation();
	FRotator SpawnRotation = GetComponentRotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = GetOwner()->GetInstigator();

	if (bHmdMode)
	{
		FRotator ForwardTilt(-90, 0, 0);
		FRotator Roll(0, 0, 180);
		SpawnRotation = (SpawnRotation.Quaternion() * Roll.Quaternion() * ForwardTilt.Quaternion()).Rotator();
	}

	UClass* HandBlueprintClass = HandBlueprint;
	ALeapMotionHandActor* handActor = GetWorld()->SpawnActor<ALeapMotionHandActor>(HandBlueprintClass != nullptr ? HandBlueprintClass : ALeapMotionHandActor::StaticClass(), SpawnLocation, SpawnRotation, SpawnParams);

	if (handActor)
	{
		HandActors.Add(HandId) = handActor;

#		if WITH_EDITOR
			handActor->SetActorLabel(*FString::Printf(TEXT("LeapHand:%d"), HandId));
#		endif

		handActor->AttachRootComponentTo(this, NAME_None, EAttachLocation::KeepWorldPosition, true);

		handActor->bShowCollider = bShowCollider;
		handActor->bShowMesh = bShowMesh;
		handActor->bShowArm = bShowArm;
		handActor->Scale = bHmdMode ? ScaleForHmdMode : Scale;
		handActor->Init(HandId, BoneBlueprint);

	}
}

void ULeapMotionControllerComponent::OnHandRemovedImpl(int32 HandId)
{
	ALeapMotionHandActor** HandActor = HandActors.Find(HandId);
	if (HandActor)
	{
		(*HandActor)->Destroy();
		HandActors.Remove(HandId);
	}
}

void ULeapMotionControllerComponent::OnHandUpdatedImpl(int32 HandId, float DeltaSeconds)
{
	ALeapMotionHandActor** actorPtr = HandActors.Find(HandId);
	if (actorPtr) 
	{
		(*actorPtr)->Update(DeltaSeconds); 
	}
}

