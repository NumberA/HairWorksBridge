// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMovieScenePlayer.h"
#include "UMGSequencePlayer.generated.h"

class UWidgetAnimation;
class UMovieSceneBindings;

UCLASS(transient)
class UMG_API UUMGSequencePlayer : public UObject, public IMovieScenePlayer
{
	GENERATED_UCLASS_BODY()

public:
	void InitSequencePlayer( const UWidgetAnimation& InAnimation, UUserWidget& UserWidget );

	/** Updates the running movie */
	void Tick( float DeltaTime );

	/** Begins playing or restarts an animation */
	void Play( float StartAtTime, int32 InNumLoopsToPlay, EUMGSequencePlayMode::Type InPlayMode);

	/** Stops a running animation and resets time */
	void Stop();

	/** Pauses a running animation */
	void Pause();

	/** Gets the current time position in the player (in seconds). */
	double GetTimeCursorPosition() const { return TimeCursorPosition; }

	/** @return The current animation being played */
	const UWidgetAnimation* GetAnimation() const { return Animation; }

	/** Sets the number of loops to play */
	void SetNumLoopsToPlay(int32 InNumLoopsToPlay);

	/** IMovieScenePlayer interface */
	virtual void GetRuntimeObjects( TSharedRef<FMovieSceneSequenceInstance> MovieSceneInstance, const FGuid& ObjectHandle, TArray< UObject* >& OutObjects ) const override;
	virtual void UpdateCameraCut(UObject* CameraObject, UObject* UnlockIfCameraObject) const override {}
	virtual void SetViewportSettings(const TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) override {}
	virtual void GetViewportSettings(TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) const override {}
	virtual void AddOrUpdateMovieSceneInstance( class UMovieSceneSection& MovieSceneSection, TSharedRef<FMovieSceneSequenceInstance> InstanceToAdd ) override {}
	virtual void RemoveMovieSceneInstance( class UMovieSceneSection& MovieSceneSection, TSharedRef<FMovieSceneSequenceInstance> InstanceToRemove ) override {}
	virtual TSharedRef<FMovieSceneSequenceInstance> GetRootMovieSceneSequenceInstance() const override { return RootMovieSceneInstance.ToSharedRef(); }
	virtual EMovieScenePlayerStatus::Type GetPlaybackStatus() const override;

	DECLARE_EVENT_OneParam(UUMGSequencePlayer, FOnSequenceFinishedPlaying, UUMGSequencePlayer&);
	FOnSequenceFinishedPlaying& OnSequenceFinishedPlaying() { return OnSequenceFinishedPlayingEvent; }

private:
	/** Animation being played */
	UPROPERTY()
	const UWidgetAnimation* Animation;

	TMap<FGuid, TArray<UObject*> > GuidToRuntimeObjectMap;

	/** The root movie scene instance to update when playing. */
	TSharedPtr<class FMovieSceneSequenceInstance> RootMovieSceneInstance;

	/** Time range of the animation */
	TRange<float> TimeRange;

	/** The current time cursor position within the sequence (in seconds) */
	double TimeCursorPosition;

	/** The offset from 0 to the start of the animation (in seconds) */
	double AnimationStartOffset;

	/** Status of the player (e.g play, stopped) */
	EMovieScenePlayerStatus::Type PlayerStatus;

	/** Delegate to call when a sequence has finished playing */
	FOnSequenceFinishedPlaying OnSequenceFinishedPlayingEvent;

	/** The number of times to loop the animation playback */
	int32 NumLoopsToPlay;

	/** The number of loops completed since the last call to Play() */
	int32 NumLoopsCompleted;

	/** The current playback mode. */
	EUMGSequencePlayMode::Type PlayMode;

	/** True if the animation is playing forward, otherwise false and it's playing in reverse. */
	bool bIsPlayingForward;
};
