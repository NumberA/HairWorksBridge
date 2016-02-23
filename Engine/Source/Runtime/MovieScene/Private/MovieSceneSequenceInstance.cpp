// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "MovieScenePrivatePCH.h"
#include "MovieSceneSequenceInstance.h"
#include "MovieSceneSequence.h"

FMovieSceneSequenceInstance::FMovieSceneSequenceInstance(const UMovieSceneSequence& InMovieSceneSequence)
	: MovieSceneSequence( &InMovieSceneSequence )
{
	TimeRange = MovieSceneSequence->GetMovieScene()->GetPlaybackRange();
}

FMovieSceneSequenceInstance::~FMovieSceneSequenceInstance()
{
	for (auto& Pair : SpawnedObjects)
	{
		ensureMsgf(!Pair.Value.Get(), TEXT("Sequence instance is being destroyed but still has spawned objects"));
	}
}

FGuid FMovieSceneSequenceInstance::FindObjectId(UObject& Object) const
{
	for (auto& Pair : ObjectBindingInstances)
	{
		if (Pair.Value.RuntimeObjects.Contains(&Object))
		{
			return Pair.Key;
		}
	}

	// At this point the only possibility left is that we have not cached the object
	// in ObjectBindingInstances, so we to see if the sequence itself can tell us the GUID
	return MovieSceneSequence->FindPossessableObjectId(Object);
}

FGuid FMovieSceneSequenceInstance::FindParentObjectId(UObject& Object) const
{
	UObject* ParentObject = MovieSceneSequence->GetParentObject(&Object);
	if (ParentObject)
	{
		return FindObjectId(*ParentObject);
	}
	return FGuid();
}

UObject* FMovieSceneSequenceInstance::FindObject(const FGuid& ObjectId, const IMovieScenePlayer& Player) const
{
	// Attempt to find a possessable first
	FMovieScenePossessable* Possessable = MovieSceneSequence->GetMovieScene()->FindPossessable(ObjectId);
	if (Possessable)
	{
		UObject* ParentObject = Player.GetPlaybackContext();
		if (Possessable->GetParent().IsValid())
		{
			ParentObject = FindObject(Possessable->GetParent(), Player);
		}

		return MovieSceneSequence->FindPossessableObject(ObjectId, ParentObject);
	}
	else
	{
		return FindSpawnedObject(ObjectId);
	}
}

UObject* FMovieSceneSequenceInstance::FindSpawnedObject(const FGuid& ObjectId) const
{
	const TWeakObjectPtr<>* SpawnedObject = SpawnedObjects.Find(ObjectId);
	return SpawnedObject ? SpawnedObject->Get() : nullptr;
}

void FMovieSceneSequenceInstance::SaveState(class IMovieScenePlayer& Player)
{
	TArray<UObject*> NoObjects;
	if (ShotTrackInstance.IsValid())
	{
		ShotTrackInstance->SaveState(NoObjects, Player, *this);
	}

	for (auto& Pair : MasterTrackInstances)
	{
		Pair.Value->SaveState(NoObjects, Player, *this);
	}

	TMap<FGuid, FMovieSceneObjectBindingInstance>::TIterator ObjectIt = ObjectBindingInstances.CreateIterator();
	for (; ObjectIt; ++ObjectIt)
	{
		FMovieSceneObjectBindingInstance& ObjectBindingInstance = ObjectIt.Value();

		for (FMovieSceneInstanceMap::TIterator It = ObjectBindingInstance.TrackInstances.CreateIterator(); It; ++It)
		{
			It.Value()->SaveState(ObjectBindingInstance.RuntimeObjects, Player, *this);
		}

		for( FMovieSceneInstanceMap::TIterator It( MasterTrackInstances ); It; ++It )
		{
			It.Value()->SaveState( ObjectBindingInstance.RuntimeObjects, Player, *this );
		}
	}
}


void FMovieSceneSequenceInstance::RestoreState(class IMovieScenePlayer& Player)
{
	Player.GetSpawnRegister().DestroyObjectsSpawnedByInstance(*this, Player);

	TMap<FGuid, FMovieSceneObjectBindingInstance>::TIterator ObjectIt = ObjectBindingInstances.CreateIterator();
	for (; ObjectIt; ++ObjectIt)
	{
		FMovieSceneObjectBindingInstance& ObjectBindingInstance = ObjectIt.Value();

		for (FMovieSceneInstanceMap::TIterator It = ObjectBindingInstance.TrackInstances.CreateIterator(); It; ++It)
		{
			It.Value()->RestoreState(ObjectBindingInstance.RuntimeObjects, Player, *this);
		}

		for( FMovieSceneInstanceMap::TIterator It( MasterTrackInstances ); It; ++It )
		{
			It.Value()->RestoreState( ObjectBindingInstance.RuntimeObjects, Player, *this );
		}
	}

	TArray<UObject*> NoObjects;
	if (ShotTrackInstance.IsValid())
	{
		ShotTrackInstance->RestoreState(NoObjects, Player, *this);
	}

	for (auto& Pair : MasterTrackInstances)
	{
		Pair.Value->RestoreState(NoObjects, Player, *this);
	}
}


void FMovieSceneSequenceInstance::Update( float Position, float LastPosition, class IMovieScenePlayer& Player )
{
	// Remove any stale runtime objects
	TMap<FGuid, FMovieSceneObjectBindingInstance>::TIterator ObjectIt = ObjectBindingInstances.CreateIterator();
	for(; ObjectIt; ++ObjectIt )
	{
		FMovieSceneObjectBindingInstance& ObjectBindingInstance = ObjectIt.Value();
		for (int32 ObjectIndex = 0; ObjectIndex < ObjectBindingInstance.RuntimeObjects.Num(); )
		{
			UObject* RuntimeObject = ObjectBindingInstance.RuntimeObjects[ObjectIndex];
			if (RuntimeObject == nullptr || RuntimeObject->HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed) || RuntimeObject->IsPendingKill())
			{
				ObjectBindingInstance.RuntimeObjects.RemoveAt(ObjectIndex);
			}
			else
			{
				++ObjectIndex;
			}
		}
	}

	Player.GetSpawnRegister().PreUpdateSequenceInstance(*this, Player);

	UpdateInternal(Position, LastPosition, Player, MSUP_PreUpdate);
	UpdateInternal(Position, LastPosition, Player, MSUP_Update);
	UpdateInternal(Position, LastPosition, Player, MSUP_PostUpdate);

	Player.GetSpawnRegister().PostUpdateSequenceInstance(*this, Player);
}

void FMovieSceneSequenceInstance::UpdateInternal( float Position, float LastPosition, class IMovieScenePlayer& Player, EMovieSceneUpdatePass UpdatePass )
{
	// Refresh time range so that spawnables can be created if they fall within the playback range, or destroyed if not
	UMovieScene* MovieScene = MovieSceneSequence->GetMovieScene();
	TimeRange = MovieScene->GetPlaybackRange();

	TArray<UObject*> NoObjects;

	// update each master track
	for( FMovieSceneInstanceMap::TIterator It( MasterTrackInstances ); It; ++It )
	{
		if (It.Value()->HasUpdatePasses() & UpdatePass)
		{
			It.Value()->Update( Position, LastPosition, NoObjects, Player, *this, UpdatePass);
		}
	}

	// update tracks bound to objects
	TMap<FGuid, FMovieSceneObjectBindingInstance>::TIterator ObjectIt = ObjectBindingInstances.CreateIterator();
	for(; ObjectIt; ++ObjectIt )
	{
		FMovieSceneObjectBindingInstance& ObjectBindingInstance = ObjectIt.Value();
		
		for( FMovieSceneInstanceMap::TIterator It = ObjectBindingInstance.TrackInstances.CreateIterator(); It; ++It )
		{
			if (It.Value()->HasUpdatePasses() & UpdatePass)
			{
				It.Value()->Update( Position, LastPosition, ObjectBindingInstance.RuntimeObjects, Player, *this, UpdatePass );
			}
		}
	}

	// update shot track last to make sure spawnable cameras are there, and to override sub-shots
	if (ShotTrackInstance.IsValid())
	{
		if (ShotTrackInstance->HasUpdatePasses() & UpdatePass)
		{
			ShotTrackInstance->Update(Position, LastPosition, NoObjects, Player, *this, UpdatePass );
		}
	}
}


void FMovieSceneSequenceInstance::RefreshInstance( IMovieScenePlayer& Player )
{
	UMovieScene* MovieScene = MovieSceneSequence->GetMovieScene();
	TimeRange = MovieScene->GetPlaybackRange();

	UMovieSceneTrack* ShotTrack = MovieScene->GetShotTrack();

	if (ShotTrack != nullptr)
	{
		FMovieSceneInstanceMap ShotTrackInstanceMap;

		if (ShotTrackInstance.IsValid())
		{
			ShotTrackInstanceMap.Add(ShotTrack, ShotTrackInstance);
		}

		TArray<UObject*> Objects;
		TArray<UMovieSceneTrack*> Tracks;
		Tracks.Add(ShotTrack);
		RefreshInstanceMap(Tracks, Objects, ShotTrackInstanceMap, Player);

		ShotTrackInstance = ShotTrackInstanceMap.FindRef(ShotTrack);
	}
	else if(ShotTrackInstance.IsValid())
	{
		ShotTrackInstance->ClearInstance(Player, *this);
		ShotTrackInstance.Reset();
	}

	// Get all the master tracks and create instances for them if needed
	const TArray<UMovieSceneTrack*>& MasterTracks = MovieScene->GetMasterTracks();
	TArray<UObject*> Objects;
	RefreshInstanceMap( MasterTracks, Objects, MasterTrackInstances, Player );

	TSet< FGuid > FoundObjectBindings;
	// Get all tracks for each object binding and create instances for them if needed
	const TArray<FMovieSceneBinding>& ObjectBindings = MovieScene->GetBindings();
	for( int32 BindingIndex = 0; BindingIndex < ObjectBindings.Num(); ++BindingIndex )
	{
		const FMovieSceneBinding& ObjectBinding = ObjectBindings[BindingIndex];

		// Create an instance for this object binding
		FMovieSceneObjectBindingInstance& BindingInstance = ObjectBindingInstances.FindOrAdd( ObjectBinding.GetObjectGuid() );
		BindingInstance.ObjectGuid = ObjectBinding.GetObjectGuid();

		FoundObjectBindings.Add( ObjectBinding.GetObjectGuid() );

		// Populate the runtime objects for this instance of the binding.
		// @todo sequencer: SubSequences: We need to know which actors were removed and which actors were added so we know which saved actor state to restore/create
		BindingInstance.RuntimeObjects.Empty();
		Player.GetRuntimeObjects( SharedThis( this ), BindingInstance.ObjectGuid, BindingInstance.RuntimeObjects );

		// Refresh the instance's tracks
		const TArray<UMovieSceneTrack*>& Tracks = ObjectBinding.GetTracks();
		RefreshInstanceMap( Tracks, BindingInstance.RuntimeObjects, BindingInstance.TrackInstances, Player );
	}

	IMovieSceneSpawnRegister& SpawnRegister = Player.GetSpawnRegister();

	// Remove object binding instances which are no longer bound
	TMap<FGuid, FMovieSceneObjectBindingInstance>::TIterator It = ObjectBindingInstances.CreateIterator();
	for( ; It; ++It )
	{
		if( !FoundObjectBindings.Contains( It.Key() ) )
		{
			SpawnRegister.DestroySpawnedObject(It.Key(), *this, Player);

			// The instance no longer is bound to an existing guid
			It.RemoveCurrent();
		}
	}
}


struct FTrackInstanceEvalSorter
{
	bool operator()( const TSharedPtr<IMovieSceneTrackInstance> A, const TSharedPtr<IMovieSceneTrackInstance> B ) const
	{
		return A->EvalOrder() < B->EvalOrder();
	}
};


void FMovieSceneSequenceInstance::RefreshInstanceMap( const TArray<UMovieSceneTrack*>& Tracks, const TArray<UObject*>& RuntimeObjects, FMovieSceneInstanceMap& TrackInstances, IMovieScenePlayer& Player  )
{
	// All the tracks we found during this pass
	TSet< UMovieSceneTrack* > FoundTracks;

	// For every track, check if it has an instance, if not create one, otherwise refresh that instance
	for( int32 TrackIndex = 0; TrackIndex < Tracks.Num(); ++TrackIndex )
	{
		UMovieSceneTrack* Track = Tracks[TrackIndex];

		// A new track has been encountered
		FoundTracks.Add( Track );

		// See if the track has an instance
		TSharedPtr<IMovieSceneTrackInstance> Instance = TrackInstances.FindRef( Track );
		if ( !Instance.IsValid() )
		{
			// The track does not have an instance, create one
			Instance = Track->CreateInstance();
			Instance->RefreshInstance( RuntimeObjects, Player, *this );
			Instance->SaveState(RuntimeObjects, Player, *this);

			TrackInstances.Add( Track, Instance );
		}
		else
		{
			// The track has an instance, refresh it
			Instance->RefreshInstance( RuntimeObjects, Player, *this );
			Instance->SaveState(RuntimeObjects, Player, *this);
		}

	}

	// Remove entries which no longer have a track associated with them
	FMovieSceneInstanceMap::TIterator It = TrackInstances.CreateIterator();
	for( ; It; ++It )
	{
		if( !FoundTracks.Contains( It.Key().Get() ) )
		{
			It.Value()->ClearInstance( Player, *this );

			// This track was not found in the moviescene's track list so it was removed.
			It.RemoveCurrent();
		}
	}

	// Sort based on evaluation order
	TrackInstances.ValueSort(FTrackInstanceEvalSorter());
}

void FMovieSceneSequenceInstance::UpdateObjectBinding(const FGuid& ObjectId, IMovieScenePlayer& Player)
{
	UMovieSceneSequence* Sequence = MovieSceneSequence.Get();
	auto* BindingInstance = ObjectBindingInstances.Find(ObjectId);

	if (!BindingInstance || !Sequence)
	{
		return;
	}

	// Update the runtime objects
	BindingInstance->RuntimeObjects.Reset();

	TWeakObjectPtr<UObject>* WeakSpawnedObject = SpawnedObjects.Find(ObjectId);
	if (WeakSpawnedObject)
	{
		UObject* SpawnedObject = WeakSpawnedObject->Get();
		if (SpawnedObject)
		{
			BindingInstance->RuntimeObjects.Add(SpawnedObject);
		}
	}
	else
	{
		Player.GetRuntimeObjects(SharedThis(this), BindingInstance->ObjectGuid, BindingInstance->RuntimeObjects);
	}

	const FMovieSceneBinding* ObjectBinding = Sequence->GetMovieScene()->GetBindings().FindByPredicate([&](const FMovieSceneBinding& In){
		return In.GetObjectGuid() == ObjectId;
	});

	// Refresh the instance map, if we found the binding itself
	if (ObjectBinding)
	{
		RefreshInstanceMap(ObjectBinding->GetTracks(), BindingInstance->RuntimeObjects, BindingInstance->TrackInstances, Player);
	}
}

void FMovieSceneSequenceInstance::OnObjectSpawned(const FGuid& ObjectId, UObject& SpawnedObject, IMovieScenePlayer& Player)
{
	auto* BindingInstance = ObjectBindingInstances.Find(ObjectId);

	if (!BindingInstance)
	{
		return;
	}

	SpawnedObjects.Add(ObjectId, &SpawnedObject);

	// Add it to the instance's runtime objects array, and update any child possessable binding instances
	BindingInstance->RuntimeObjects.Reset();
	BindingInstance->RuntimeObjects.Emplace(&SpawnedObject);

	UMovieSceneSequence* Sequence = GetSequence();
	FMovieSceneSpawnable* Spawnable = Sequence ? Sequence->GetMovieScene()->FindSpawnable(ObjectId) : nullptr;
	if (Spawnable)
	{
		for (const FGuid& Child : Spawnable->GetChildPossessables())
		{
			UpdateObjectBinding(Child, Player);
		}
	}
}

void FMovieSceneSequenceInstance::OnSpawnedObjectDestroyed(const FGuid& ObjectId, IMovieScenePlayer& Player)
{
	auto* BindingInstance = ObjectBindingInstances.Find(ObjectId);
	if (!BindingInstance)
	{
		return;
	}

	SpawnedObjects.Remove(ObjectId);

	// Destroy the object
	BindingInstance->RuntimeObjects.Reset();

	// Update any child possessable object bindings
	UMovieSceneSequence* Sequence = GetSequence();
	FMovieSceneSpawnable* Spawnable = Sequence ? Sequence->GetMovieScene()->FindSpawnable(ObjectId) : nullptr;
	if (Spawnable)
	{
		for (const FGuid& Child : Spawnable->GetChildPossessables())
		{
			UpdateObjectBinding(Child, Player);
		}
	}
}