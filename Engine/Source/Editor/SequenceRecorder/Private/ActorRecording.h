// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimationRecordingSettings.h"
#include "Components/SkinnedMeshComponent.h"
#include "ActorRecordingSettings.h"

#include "ActorRecording.generated.h"

UCLASS(MinimalAPI, Transient)
class UActorRecording : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/** Check whether it is worth recording this actor - i.e. is it going to affect the end result of the sequence */
	static bool IsRelevantForRecording(AActor* Actor);

	/** Start this queued recording. Sequence can be nullptr */
	bool StartRecording(class ULevelSequence* CurrentSequence = nullptr, float CurrentSequenceTime = 0.0f);

	/** Stop this recording. Has no effect if we are not currently recording. Sequence can be nullptr */
	bool StopRecording(class ULevelSequence* CurrentSequence = nullptr);

	/** Tick this recording */
	void Tick(float DeltaSeconds, ULevelSequence* CurrentSequence = nullptr, float CurrentSequenceTime = 0.0f);

	/** Whether we are currently recording */
	bool IsRecording() const;

	/** Simulate a de-spawned actor */
	void InvalidateObjectToRecord();

	/** Get the Guid that identifies our spawnable in a recorded sequence */
	const FGuid& GetSpawnableGuid() const
	{
		return Guid;
	}

private:
	/** Check component validity for recording */
	bool ValidComponent(USceneComponent* SceneComponent) const;

	/** Adds us to a folder for better sequence organization */
	void FindOrAddFolder(UMovieScene* MovieScene);

	/** Start recording actor properties to a sequence */
	void StartRecordingActorProperties(ULevelSequence* CurrentSequence, float CurrentSequenceTime);

	/** Start recording component properties to a sequence */
	TSharedPtr<class FMovieSceneAnimationSectionRecorder> StartRecordingComponentProperties(const FName& BindingName, USceneComponent* SceneComponent, UObject* BindingContext, ULevelSequence* CurrentSequence, float CurrentSequenceTime);

	/** Start recording components that are added at runtime */
	void StartRecordingNewComponents(ULevelSequence* CurrentSequence, float CurrentSequenceTime);

	/** Helper function to grab all scene components in the actor's hierarchy */
	void GetSceneComponents(TArray<USceneComponent*>& OutArray, bool bIncludeNonCDO = true);

	/** Sync up tracked components with the actor */
	void SyncTrackedComponents(bool bIncludeNonCDO = true);

public:
	/** The actor we want to record */
	UPROPERTY(EditAnywhere, Category = "Actor Recording")
	TLazyObjectPtr<AActor> ActorToRecord;

	UPROPERTY(EditAnywhere, Category = "Actor Recording")
	FActorRecordingSettings ActorSettings;

	/** Whether we should specify the target animation or auto-create it */
	UPROPERTY(EditAnywhere, Category = "Animation Recording")
	bool bSpecifyTargetAnimation;

	/** The target animation we want to record to */
	UPROPERTY(EditAnywhere, Category = "Animation Recording", meta=(EditCondition = "bSpecifyTargetAnimation"))
	TWeakObjectPtr<UAnimSequence> TargetAnimation;

	/** The settings to apply to this actor's animation */
	UPROPERTY(EditAnywhere, Category = "Animation Recording")
	FAnimationRecordingSettings AnimationSettings;

	/** Whether this actor recording was triggered from an actor spawn */
	bool bWasSpawnedPostRecord;

private:
	/** This actor's current set of section recorders */
	TArray<TSharedPtr<class IMovieSceneSectionRecorder>> SectionRecorders;

	/** Track components to check if any have changed */
	TArray<TWeakObjectPtr<USceneComponent>> TrackedComponents;

	/** Flag to track whether we created new components */
	bool bNewComponentAddedWhileRecording;

	/** Guid that identifies our spawnable in a recorded sequence */
	FGuid Guid;
};