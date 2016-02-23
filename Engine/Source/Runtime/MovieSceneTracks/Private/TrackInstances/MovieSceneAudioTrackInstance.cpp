// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTracksPrivatePCH.h"
#include "MovieSceneAudioTrackInstance.h"
#include "IMovieScenePlayer.h"
#include "SoundDefinitions.h"
#include "Runtime/Engine/Classes/Kismet/GameplayStatics.h"
#include "Runtime/Engine/Public/AudioDecompress.h"
#include "MovieSceneAudioTrack.h"
#include "MovieSceneAudioSection.h"


FMovieSceneAudioTrackInstance::FMovieSceneAudioTrackInstance( UMovieSceneAudioTrack& InAudioTrack )
{
	AudioTrack = &InAudioTrack;
}


void FMovieSceneAudioTrackInstance::Update( float Position, float LastPosition, const TArray<UObject*>& RuntimeObjects, class IMovieScenePlayer& Player, FMovieSceneSequenceInstance& SequenceInstance, EMovieSceneUpdatePass UpdatePass ) 
{
	const TArray<UMovieSceneSection*>& AudioSections = AudioTrack->GetAudioSections();

	TArray<AActor*> Actors = GetRuntimeActors(RuntimeObjects);

	if (Player.GetPlaybackStatus() == EMovieScenePlayerStatus::Playing)
	{
		if (Position > LastPosition)
		{
			TMap<int32, TArray<UMovieSceneAudioSection*> > AudioSectionsBySectionIndex;
			for (int32 i = 0; i < AudioSections.Num(); ++i)
			{
				UMovieSceneAudioSection* AudioSection = Cast<UMovieSceneAudioSection>(AudioSections[i]);
				if (AudioSection->IsActive())
				{
					int32 SectionIndex = AudioSection->GetRowIndex();
					AudioSectionsBySectionIndex.FindOrAdd(SectionIndex).Add(AudioSection);
				}
			}

			for (TMap<int32, TArray<UMovieSceneAudioSection*> >::TIterator It(AudioSectionsBySectionIndex); It; ++It)
			{
				int32 RowIndex = It.Key();
				TArray<UMovieSceneAudioSection*>& MovieSceneAudioSections = It.Value();

				for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
				{
					TWeakObjectPtr<UAudioComponent> Component = GetAudioComponent(Actors[ActorIndex], RowIndex);

					bool bComponentIsPlaying = false;
					if (Component.IsValid())
					{
						for (int32 i = 0; i < MovieSceneAudioSections.Num(); ++i)
						{
							UMovieSceneAudioSection* AudioSection = MovieSceneAudioSections[i];
							if (AudioSection->IsTimeWithinAudioRange(Position))
							{
								if (!AudioSection->IsTimeWithinAudioRange(LastPosition) || !Component->IsPlaying())
								{
									PlaySound(AudioSection, Component, Position);
								}
								bComponentIsPlaying = true;
							}
						}
					}

					if (!bComponentIsPlaying)
					{
						StopSound(RowIndex);
					}
				}
			}
		}
		else
		{
			StopAllSounds();
		}
	}
	else if (Player.GetPlaybackStatus() == EMovieScenePlayerStatus::Scrubbing)
	{
		// handle scrubbing
		if (!FMath::IsNearlyEqual(Position, LastPosition))
		{
			for (int32 i = 0; i < AudioSections.Num(); ++i)
			{
				auto AudioSection = Cast<UMovieSceneAudioSection>(AudioSections[i]);
				if (AudioSection->IsActive())
				{
					int32 RowIndex = AudioSection->GetRowIndex();
				
					for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
					{
						TWeakObjectPtr<UAudioComponent> Component = GetAudioComponent(Actors[ActorIndex], RowIndex);
						if (Component.IsValid())
						{
							if (AudioSection->IsTimeWithinAudioRange(Position) && !Component->IsPlaying())
							{
								PlaySound(AudioSection, Component, Position);
								// Fade out the sound at the same volume in order to simply
								// set a short duration on the sound, far from ideal soln
								Component->FadeOut(AudioTrackConstants::ScrubDuration, 1.f);
							}
						}
					}
				}
			}
		}
	}
	else
	{
		// beginning scrubbing, stopped, recording
		StopAllSounds();
	}

	// handle locality of non-master audio
	if (!AudioTrack->IsAMasterTrack())
	{
		for (int32 RowIndex = 0; RowIndex < PlaybackAudioComponents.Num(); ++RowIndex)
		{
			for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
			{
				TWeakObjectPtr<UAudioComponent> Component = GetAudioComponent(Actors[ActorIndex], RowIndex);
				if (Component.IsValid())
				{
					if (Component->IsPlaying())
					{
						FAudioDevice* AudioDevice = Component->GetAudioDevice();
						FActiveSound* ActiveSound = AudioDevice->FindActiveSound(Component.Get());
						ActiveSound->bLocationDefined = true;
						ActiveSound->Transform = Actors[ActorIndex]->GetTransform();
					}
				}
			}
		}
	}
}


void FMovieSceneAudioTrackInstance::PlaySound(UMovieSceneAudioSection* AudioSection, TWeakObjectPtr<UAudioComponent> Component, float Time)
{
	if (!Component.IsValid())
	{
		return;
	}

	float PitchMultiplier = 1.f / AudioSection->GetAudioDilationFactor();
	
	Component->bAllowSpatialization = !AudioTrack->IsAMasterTrack();
	Component->Stop();
	Component->SetSound(AudioSection->GetSound());
	Component->SetVolumeMultiplier(1.f);
	Component->SetPitchMultiplier(PitchMultiplier);
	Component->bIsUISound = true;
	Component->Play(Time - AudioSection->GetAudioStartTime());
}


void FMovieSceneAudioTrackInstance::StopSound(int32 RowIndex)
{
	if (RowIndex >= PlaybackAudioComponents.Num())
	{
		return;
	}

	TMap<AActor*, TWeakObjectPtr<UAudioComponent>>& AudioComponents = PlaybackAudioComponents[RowIndex];
	for (TMap<AActor*, TWeakObjectPtr<UAudioComponent>>::TIterator It(AudioComponents); It; ++It)
	{
		if (It.Value().IsValid())
		{
			It.Value()->Stop();
		}
	}
}


void FMovieSceneAudioTrackInstance::StopAllSounds()
{
	for (int32 i = 0; i < PlaybackAudioComponents.Num(); ++i)
	{
		TMap<AActor*, TWeakObjectPtr<UAudioComponent>>& AudioComponents = PlaybackAudioComponents[i];
		for (TMap<AActor*, TWeakObjectPtr<UAudioComponent>>::TIterator It(AudioComponents); It; ++It)
		{
			if (It.Value().IsValid())
			{
				It.Value()->Stop();
			}
		}
	}
}


TWeakObjectPtr<UAudioComponent> FMovieSceneAudioTrackInstance::GetAudioComponent(AActor* Actor, int32 RowIndex)
{
	if (RowIndex + 1 > PlaybackAudioComponents.Num())
	{
		while (PlaybackAudioComponents.Num() < RowIndex + 1)
		{
			TMap<AActor*, TWeakObjectPtr<UAudioComponent>> AudioComponentMap;
			PlaybackAudioComponents.Add(AudioComponentMap);
		}
	}

	if (PlaybackAudioComponents[RowIndex].Find(Actor) == nullptr || !(*PlaybackAudioComponents[RowIndex].Find(Actor)).IsValid())
	{
		USoundCue* TempPlaybackAudioCue = NewObject<USoundCue>();
		
		UAudioComponent* AudioComponent = FAudioDevice::CreateComponent(TempPlaybackAudioCue, nullptr, Actor, false, false);

		PlaybackAudioComponents[RowIndex].Add(Actor);
		PlaybackAudioComponents[RowIndex][Actor] = TWeakObjectPtr<UAudioComponent>(AudioComponent);
	}

	return *PlaybackAudioComponents[RowIndex].Find(Actor);
}


TArray<AActor*> FMovieSceneAudioTrackInstance::GetRuntimeActors(const TArray<UObject*>& RuntimeObjects) const
{
	TArray<AActor*> Actors;

	for (int32 ObjectIndex = 0; ObjectIndex < RuntimeObjects.Num(); ++ObjectIndex)
	{
		if (RuntimeObjects[ObjectIndex]->IsA<AActor>())
		{
			Actors.Add(Cast<AActor>(RuntimeObjects[ObjectIndex]));
		}
	}

	if (AudioTrack->IsAMasterTrack())
	{
		check(Actors.Num() == 0);
		Actors.Add(nullptr);
	}

	return Actors;
}
