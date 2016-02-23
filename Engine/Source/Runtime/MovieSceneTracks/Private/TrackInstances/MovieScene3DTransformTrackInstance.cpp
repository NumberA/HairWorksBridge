// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTracksPrivatePCH.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieScene3DTransformTrackInstance.h"
#include "MovieScene3DTransformTrack.h"

FMovieScene3DTransformTrackInstance::FMovieScene3DTransformTrackInstance( UMovieScene3DTransformTrack& InTransformTrack )
{
	TransformTrack = &InTransformTrack;
}


void FMovieScene3DTransformTrackInstance::SaveState(const TArray<UObject*>& RuntimeObjects, IMovieScenePlayer& Player, FMovieSceneSequenceInstance& SequenceInstance)
{
	for (int32 ObjIndex = 0; ObjIndex < RuntimeObjects.Num(); ++ObjIndex)
	{
		USceneComponent* SceneComponent = MovieSceneHelpers::SceneComponentFromRuntimeObject(RuntimeObjects[ObjIndex]);
		if (SceneComponent != nullptr)
		{
			if (InitTransformMap.Find(RuntimeObjects[ObjIndex]) == nullptr)
			{
				InitTransformMap.Add(RuntimeObjects[ObjIndex], SceneComponent->GetRelativeTransform());
			}
			if (InitMobilityMap.Find(RuntimeObjects[ObjIndex]) == nullptr)
			{
				InitMobilityMap.Add(RuntimeObjects[ObjIndex], SceneComponent->Mobility);
			}
		}
	}
}


void FMovieScene3DTransformTrackInstance::RestoreState(const TArray<UObject*>& RuntimeObjects, IMovieScenePlayer& Player, FMovieSceneSequenceInstance& SequenceInstance)
{
	for (int32 ObjIndex = 0; ObjIndex < RuntimeObjects.Num(); ++ObjIndex)
	{
		if (!IsValid(RuntimeObjects[ObjIndex]))
		{
			continue;
		}

		USceneComponent* SceneComponent = MovieSceneHelpers::SceneComponentFromRuntimeObject(RuntimeObjects[ObjIndex]);
		if (SceneComponent != nullptr)
		{
			FTransform *Transform = InitTransformMap.Find(RuntimeObjects[ObjIndex]);
			if (Transform != nullptr)
			{
				SceneComponent->SetRelativeTransform(*Transform);
			}

			EComponentMobility::Type* ComponentMobility = InitMobilityMap.Find(RuntimeObjects[ObjIndex]);
			if (ComponentMobility != nullptr)
			{
				SceneComponent->SetMobility(*ComponentMobility);
			}
		}
	}
}

void FMovieScene3DTransformTrackInstance::UpdateRuntimeMobility(const TArray<UObject*>& RuntimeObjects)
{
	for( int32 ObjIndex = 0; ObjIndex < RuntimeObjects.Num(); ++ObjIndex )
	{
		USceneComponent* SceneComponent = MovieSceneHelpers::SceneComponentFromRuntimeObject(RuntimeObjects[ObjIndex]);

		if (SceneComponent != nullptr)
		{
			if (SceneComponent->Mobility != EComponentMobility::Movable)
			{
				if (InitMobilityMap.Find(RuntimeObjects[ObjIndex]) == nullptr)
				{
					InitMobilityMap.Add(RuntimeObjects[ObjIndex], SceneComponent->Mobility);
				}

				SceneComponent->SetMobility(EComponentMobility::Movable);
			}
		}
	}
}

void FMovieScene3DTransformTrackInstance::Update( float Position, float LastPosition, const TArray<UObject*>& RuntimeObjects, class IMovieScenePlayer& Player, FMovieSceneSequenceInstance& SequenceInstance, EMovieSceneUpdatePass UpdatePass ) 
{
	if (UpdatePass == MSUP_PreUpdate)
	{
		UpdateRuntimeMobility(RuntimeObjects);
	}

	FVector Translation;
	FRotator Rotation;
	FVector Scale;

	if( TransformTrack->Eval( Position, LastPosition, Translation, Rotation, Scale ) )
	{
		for( int32 ObjIndex = 0; ObjIndex < RuntimeObjects.Num(); ++ObjIndex )
		{
			USceneComponent* SceneComponent = MovieSceneHelpers::SceneComponentFromRuntimeObject(RuntimeObjects[ObjIndex]);

			if (SceneComponent != nullptr)
			{
				if (UpdatePass == MSUP_PreUpdate)
				{
					SceneComponent->ResetRelativeTransform();
				}
				else if (UpdatePass == MSUP_Update)
				{
					SceneComponent->AddRelativeLocation(Translation);
					SceneComponent->AddRelativeRotation(Rotation);
					SceneComponent->SetRelativeScale3D(Scale);
				}
			}
		}
	}
}
 
void FMovieScene3DTransformTrackInstance::RefreshInstance( const TArray<UObject*>& RuntimeObjects, IMovieScenePlayer& Player, FMovieSceneSequenceInstance& SequenceInstance )
{
	UpdateRuntimeMobility(RuntimeObjects);
}
