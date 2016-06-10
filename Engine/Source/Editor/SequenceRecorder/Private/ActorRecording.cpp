// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "SequenceRecorderPrivatePCH.h"
#include "ActorRecording.h"
#include "SequenceRecorderSettings.h"
#include "SequenceRecorderUtils.h"
#include "MovieScene3DTransformSectionRecorder.h"
#include "MovieSceneAnimationSectionRecorder.h"
#include "AssetSelection.h"
#include "MovieSceneSkeletalAnimationTrack.h"
#include "MovieSceneSkeletalAnimationSection.h"
#include "MovieSceneSpawnTrack.h"
#include "MovieScene3DTransformTrack.h"
#include "MovieScene3DTransformSection.h"
#include "MovieSceneBoolSection.h"
#include "KismetEditorUtilities.h"
#include "BlueprintEditorUtils.h"
#include "MovieSceneFolder.h"
#include "CameraRig_Crane.h"
#include "CameraRig_Rail.h"
#include "Animation/SkeletalMeshActor.h"
#include "SequenceRecorder.h"
#include "Runtime/Core/Public/Features/IModularFeatures.h"

static const FName SequencerActorTag(TEXT("SequencerActor"));
static const FName MovieSceneSectionRecorderFactoryName("MovieSceneSectionRecorderFactory");

UActorRecording::UActorRecording(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bWasSpawnedPostRecord = false;
	Guid.Invalidate();
	bNewComponentAddedWhileRecording = false;

	if(!HasAnyFlags(RF_ClassDefaultObject))
	{
		const USequenceRecorderSettings* Settings = GetDefault<USequenceRecorderSettings>();
		AnimationSettings = Settings->DefaultAnimationSettings;
	}
}

bool UActorRecording::IsRelevantForRecording(AActor* Actor)
{
	// don't record actors that sequencer has spawned itself!
	if(Actor->ActorHasTag(SequencerActorTag))
	{
		return false;
	}

	TInlineComponentArray<USceneComponent*> SceneComponents(Actor);
	const USequenceRecorderSettings* Settings = GetDefault<USequenceRecorderSettings>();

	for(USceneComponent* SceneComponent : SceneComponents)
	{
		for (TSubclassOf<USceneComponent> ComponentClass : Settings->ComponentClassesToRecord)
		{
			if (SceneComponent->IsA(ComponentClass))
			{
				return true;
			}
		}
	}

	return false;
}

bool UActorRecording::StartRecording(ULevelSequence* CurrentSequence, float CurrentSequenceTime)
{
	bNewComponentAddedWhileRecording = false;

	if(ActorToRecord.IsValid())
	{
		if(CurrentSequence != nullptr)
		{
			StartRecordingActorProperties(CurrentSequence, CurrentSequenceTime);
		}
		else
		{
			TSharedPtr<FMovieSceneAnimationSectionRecorder> AnimationRecorder = MakeShareable(new FMovieSceneAnimationSectionRecorder(AnimationSettings, TargetAnimation.Get()));
			AnimationRecorder->CreateSection(ActorToRecord.Get(), nullptr, FGuid(), 0.0f);
			AnimationRecorder->Record(0.0f);
			SectionRecorders.Add(AnimationRecorder);			
		}
	}

	return true;
}

static FString GetUniqueSpawnableName(UMovieScene* MovieScene, const FString& BaseName)
{
	FString BlueprintName = BaseName;
	auto DuplName = [&](FMovieSceneSpawnable& InSpawnable)
	{
		return InSpawnable.GetName() == BlueprintName;
	};

	int32 Index = 2;
	FString UniqueString;
	while (MovieScene->FindSpawnable(DuplName))
	{
		BlueprintName.RemoveFromEnd(UniqueString);
		UniqueString = FString::Printf(TEXT(" (%d)"), Index++);
		BlueprintName += UniqueString;
	}

	return BlueprintName;
}

void UActorRecording::GetSceneComponents(TArray<USceneComponent*>& OutArray, bool bIncludeNonCDO/*=true*/)
{
	// it is not enough to just go through the owned components array here
	// we need to traverse the scene component hierarchy as well, as some components may be 
	// owned by other actors (e.g. for pooling) and some may not be part of the hierarchy
	if(ActorToRecord.IsValid())
	{
		USceneComponent* RootComponent = ActorToRecord->GetRootComponent();
		if(RootComponent)
		{
			// note: GetChildrenComponents clears array!
			RootComponent->GetChildrenComponents(true, OutArray);
			OutArray.Add(RootComponent);
		}

		// add owned components that are *not* part of the hierarchy
		TInlineComponentArray<USceneComponent*> OwnedComponents(ActorToRecord.Get());
		for(USceneComponent* OwnedComponent : OwnedComponents)
		{
			if(OwnedComponent->GetAttachParent() == nullptr && OwnedComponent != RootComponent)
			{
				OutArray.Add(OwnedComponent);
			}
		}

		if(!bIncludeNonCDO)
		{
			AActor* CDO = Cast<AActor>(ActorToRecord->GetClass()->GetDefaultObject());

			auto ShouldRemovePredicate = [&](UActorComponent* PossiblyRemovedComponent)
				{
					// try to find a component with this name in the CDO
					for (UActorComponent* SearchComponent : CDO->GetComponents())
					{
						if (SearchComponent->GetClass() == PossiblyRemovedComponent->GetClass() &&
							SearchComponent->GetFName() == PossiblyRemovedComponent->GetFName())
						{
							return false;
						}
					}

					// remove if its not found
					return true;
				};

			OutArray.RemoveAllSwap(ShouldRemovePredicate);
		}
	}
}

void UActorRecording::SyncTrackedComponents(bool bIncludeNonCDO/*=true*/)
{
	TArray<USceneComponent*> NewComponentArray;
	GetSceneComponents(NewComponentArray, bIncludeNonCDO);

	// Expire section recorders that are watching components no longer attached to our actor
	TSet<USceneComponent*> ExpiredComponents;
	for (TWeakObjectPtr<USceneComponent>& WeakComponent : TrackedComponents)
	{
		if (USceneComponent* Component = WeakComponent.Get())
		{
			ExpiredComponents.Add(Component);
		}
	}
	for (USceneComponent* Component : NewComponentArray)
	{
		ExpiredComponents.Remove(Component);
	}

	for (TSharedPtr<IMovieSceneSectionRecorder>& SectionRecorder : SectionRecorders)
	{
		if (USceneComponent* Component = Cast<USceneComponent>(SectionRecorder->GetSourceObject()))
		{
			if (ExpiredComponents.Contains(Component))
			{
				SectionRecorder->InvalidateObjectToRecord();
			}
		}
	}

	TrackedComponents.Reset(NewComponentArray.Num());
	for(USceneComponent* SceneComponent : NewComponentArray)
	{
		TrackedComponents.Add(SceneComponent);
	}
}

void UActorRecording::InvalidateObjectToRecord()
{
	ActorToRecord = nullptr;
	for(auto& SectionRecorder : SectionRecorders)
	{
		SectionRecorder->InvalidateObjectToRecord();
	}
}

bool UActorRecording::ValidComponent(USceneComponent* SceneComponent) const
{
	if(SceneComponent != nullptr)
	{
		const USequenceRecorderSettings* Settings = GetDefault<USequenceRecorderSettings>();
		for (TSubclassOf<USceneComponent> ComponentClass : Settings->ComponentClassesToRecord)
		{			
			if (ComponentClass != nullptr && SceneComponent->IsA(ComponentClass))
			{
				return true;
			}
		}
	}

	return false;
}

void UActorRecording::FindOrAddFolder(UMovieScene* MovieScene)
{
	check(ActorToRecord.IsValid());

	FName FolderName(NAME_None);
	if(ActorToRecord.Get()->IsA<ACharacter>() || ActorToRecord.Get()->IsA<ASkeletalMeshActor>())
	{
		FolderName = TEXT("Characters");
	}
	else if(ActorToRecord.Get()->IsA<ACameraActor>() || ActorToRecord.Get()->IsA<ACameraRig_Crane>() || ActorToRecord.Get()->IsA<ACameraRig_Rail>())
	{
		FolderName = TEXT("Cameras");
	}
	else
	{
		FolderName = TEXT("Misc");
	}

	// look for a folder to put us in
	UMovieSceneFolder* FolderToUse = nullptr;
	for(UMovieSceneFolder* Folder : MovieScene->GetRootFolders())
	{
		if(Folder->GetFolderName() == FolderName)
		{
			FolderToUse = Folder;
			break;
		}
	}

	if(FolderToUse == nullptr)
	{
		FolderToUse = NewObject<UMovieSceneFolder>(MovieScene, NAME_None, RF_Transactional);
		FolderToUse->SetFolderName(FolderName);
		MovieScene->GetRootFolders().Add(FolderToUse);
	}

	FolderToUse->AddChildObjectBinding(Guid);
}

void UActorRecording::StartRecordingActorProperties(ULevelSequence* CurrentSequence, float CurrentSequenceTime)
{
	if(CurrentSequence != nullptr)
	{
		// set up our spawnable for this actor
		UMovieScene* MovieScene = CurrentSequence->GetMovieScene();

		AActor* Actor = ActorToRecord.Get();
		FString TemplateName = GetUniqueSpawnableName(MovieScene, Actor->GetName());

		UClass* ActorClass = Actor->GetClass();
		AActor* ObjectTemplate = NewObject<AActor>(MovieScene, ActorClass, *TemplateName);
		
		if (ObjectTemplate)
		{
			TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshComponents;
			ObjectTemplate->GetComponents(SkeletalMeshComponents);
			for(USkeletalMeshComponent* SkeletalMeshComponent : SkeletalMeshComponents)
			{
				SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				SkeletalMeshComponent->bEnableUpdateRateOptimizations = false;
				SkeletalMeshComponent->MeshComponentUpdateFlag = EMeshComponentUpdateFlag::AlwaysTickPoseAndRefreshBones;
				SkeletalMeshComponent->ForcedLodModel = 1;
			}

			Guid = MovieScene->AddSpawnable(TemplateName, *ObjectTemplate);
		}

		// now add tracks to record
		if(Guid.IsValid())
		{
			// add our folder
			FindOrAddFolder(MovieScene);

			// force set recording to record translations as we need this with no animation
			UMovieScene3DTransformSectionRecorderSettings* TransformSettings = ActorSettings.GetSettingsObject<UMovieScene3DTransformSectionRecorderSettings>();
			check(TransformSettings);
			TransformSettings->bRecordTransforms = true;

			// grab components so we can track attachments
			// don't include non-CDO here as they wont be part of our initial BP (duplicated above)
			// we will catch these 'extra' components on the first tick
			const bool bIncludeNonCDO = false;
			SyncTrackedComponents(bIncludeNonCDO);

			TInlineComponentArray<USceneComponent*> SceneComponents(ActorToRecord.Get());

			// check if components need recording
			TInlineComponentArray<USceneComponent*> ValidSceneComponents;
			for(TWeakObjectPtr<USceneComponent>& SceneComponent : TrackedComponents)
			{
				if(ValidComponent(SceneComponent.Get()))
				{
					ValidSceneComponents.Add(SceneComponent.Get());

					// add all parent components too
					TArray<USceneComponent*> ParentComponents;
					SceneComponent->GetParentComponents(ParentComponents);
					for(USceneComponent* ParentComponent : ParentComponents)
					{	
						ValidSceneComponents.AddUnique(ParentComponent);
					}
				}
			}

			TSharedPtr<FMovieSceneAnimationSectionRecorder> FirstAnimRecorder = nullptr;
			for(USceneComponent* SceneComponent : ValidSceneComponents)
			{
				TSharedPtr<FMovieSceneAnimationSectionRecorder> AnimRecorder = StartRecordingComponentProperties(SceneComponent->GetFName(), SceneComponent, ActorToRecord.Get(), CurrentSequence, CurrentSequenceTime);
				if(!FirstAnimRecorder.IsValid() && AnimRecorder.IsValid())
				{
					FirstAnimRecorder = AnimRecorder;
				}
			}

			// we need to create a transform track even if we arent recording transforms
			if (FSequenceRecorder::Get().GetTransformRecorderFactory().CanRecordObject(ActorToRecord.Get()))
			{
				UMovieScene3DTransformSectionRecorderSettings* Settings = ActorSettings.GetSettingsObject<UMovieScene3DTransformSectionRecorderSettings>();
				check(Settings);

				TSharedPtr<IMovieSceneSectionRecorder> Recorder = FSequenceRecorder::Get().GetTransformRecorderFactory().CreateSectionRecorder(Settings->bRecordTransforms, FirstAnimRecorder);
				if(Recorder.IsValid())
				{ 
					Recorder->CreateSection(ActorToRecord.Get(), MovieScene, Guid, CurrentSequenceTime);
					Recorder->Record(CurrentSequenceTime);
					SectionRecorders.Add(Recorder);
				}
			}

			TArray<IMovieSceneSectionRecorderFactory*> ModularFeatures = IModularFeatures::Get().GetModularFeatureImplementations<IMovieSceneSectionRecorderFactory>(MovieSceneSectionRecorderFactoryName);
			for (IMovieSceneSectionRecorderFactory* Factory : ModularFeatures)
			{
				if (Factory->CanRecordObject(ActorToRecord.Get()))
				{
					TSharedPtr<IMovieSceneSectionRecorder> Recorder = Factory->CreateSectionRecorder(ActorSettings);
					if (Recorder.IsValid())
					{
						Recorder->CreateSection(ActorToRecord.Get(), MovieScene, Guid, CurrentSequenceTime);
						Recorder->Record(CurrentSequenceTime);
						SectionRecorders.Add(Recorder);
					}
				}
			}	
		}
	}
}

TSharedPtr<FMovieSceneAnimationSectionRecorder> UActorRecording::StartRecordingComponentProperties(const FName& BindingName, USceneComponent* SceneComponent, UObject* BindingContext, ULevelSequence* CurrentSequence, float CurrentSequenceTime)
{
	// first create a possessable for this component to be controlled by
	UMovieScene* OwnerMovieScene = CurrentSequence->GetMovieScene();

	const FGuid PossessableGuid = OwnerMovieScene->AddPossessable(BindingName.ToString(), SceneComponent->GetClass());

	// Set up parent/child guids for possessables within spawnables
	FMovieScenePossessable* ChildPossessable = OwnerMovieScene->FindPossessable(PossessableGuid);
	if (ensure(ChildPossessable))
	{
		ChildPossessable->SetParent(Guid);
	}

	FMovieSceneSpawnable* ParentSpawnable = OwnerMovieScene->FindSpawnable(Guid);
	if (ParentSpawnable)
	{
		ParentSpawnable->AddChildPossessable(PossessableGuid);
	}

	// BindingName must be the component's path relative to its owner Actor
	FLevelSequenceObjectReference ObjectReference(FUniqueObjectGuid(), BindingName.ToString());

	CurrentSequence->BindPossessableObject(PossessableGuid, ObjectReference);

	// First try built-in animation recorder...
	TSharedPtr<FMovieSceneAnimationSectionRecorder> AnimationRecorder = nullptr;
	if (FSequenceRecorder::Get().GetAnimationRecorderFactory().CanRecordObject(SceneComponent))
	{
		AnimationRecorder = FSequenceRecorder::Get().GetAnimationRecorderFactory().CreateSectionRecorder(this);
		AnimationRecorder->CreateSection(SceneComponent, OwnerMovieScene, PossessableGuid, CurrentSequenceTime);
		AnimationRecorder->Record(CurrentSequenceTime);
		SectionRecorders.Add(AnimationRecorder);
	}

	// ...and transform...
	if (FSequenceRecorder::Get().GetTransformRecorderFactory().CanRecordObject(SceneComponent))
	{
		TSharedPtr<IMovieSceneSectionRecorder> Recorder = FSequenceRecorder::Get().GetTransformRecorderFactory().CreateSectionRecorder(true, nullptr);
		if (Recorder.IsValid())
		{
			Recorder->CreateSection(SceneComponent, OwnerMovieScene, PossessableGuid, CurrentSequenceTime);
			Recorder->Record(CurrentSequenceTime);
			SectionRecorders.Add(Recorder);
		}
	}

	// ...now any external recorders
	TArray<IMovieSceneSectionRecorderFactory*> ModularFeatures = IModularFeatures::Get().GetModularFeatureImplementations<IMovieSceneSectionRecorderFactory>(MovieSceneSectionRecorderFactoryName);
	for (IMovieSceneSectionRecorderFactory* Factory : ModularFeatures)
	{
		if (Factory->CanRecordObject(SceneComponent))
		{
			TSharedPtr<IMovieSceneSectionRecorder> Recorder = Factory->CreateSectionRecorder(ActorSettings);
			if (Recorder.IsValid())
			{
				Recorder->CreateSection(SceneComponent, OwnerMovieScene, PossessableGuid, CurrentSequenceTime);
				Recorder->Record(CurrentSequenceTime);
				SectionRecorders.Add(Recorder);
			}
		}
	}

	return AnimationRecorder;
}

void UActorRecording::Tick(float DeltaSeconds, ULevelSequence* CurrentSequence, float CurrentSequenceTime)
{
	if (IsRecording())
	{
		if(CurrentSequence)
		{
			// check our components to see if they have changed
			static TArray<USceneComponent*> SceneComponents;
			GetSceneComponents(SceneComponents);

			if(TrackedComponents.Num() != SceneComponents.Num())
			{
				StartRecordingNewComponents(CurrentSequence, CurrentSequenceTime);
			}
		}

		for(auto& SectionRecorder : SectionRecorders)
		{
			SectionRecorder->Record(CurrentSequenceTime);
		}
	}
}

bool UActorRecording::StopRecording(ULevelSequence* CurrentSequence)
{
	FString ActorName;
	if(CurrentSequence)
	{
		UMovieScene* MovieScene = CurrentSequence->GetMovieScene();
		check(MovieScene);

		FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(Guid);
		if(Spawnable)
		{
			ActorName = Spawnable->GetName();
		}
	}

	FScopedSlowTask SlowTask((float)SectionRecorders.Num() + 1.0f, FText::Format(NSLOCTEXT("SequenceRecorder", "ProcessingActor", "Processing Actor {0}"), FText::FromString(ActorName)));

	// stop property recorders
	for(auto& SectionRecorder : SectionRecorders)
	{
		SlowTask.EnterProgressFrame();

		SectionRecorder->FinalizeSection();
	}

	SlowTask.EnterProgressFrame();

	SectionRecorders.Empty();

	return true;
}

bool UActorRecording::IsRecording() const
{
	return ActorToRecord.IsValid() && SectionRecorders.Num() > 0;
}

static FName FindParentComponentOwnerClassName(USceneComponent* SceneComponent, UBlueprint* Blueprint)
{
	if(SceneComponent->GetAttachParent())
	{
		FName AttachName = SceneComponent->GetAttachParent()->GetFName();

		// see if we can find this component in the BP inheritance hierarchy
		while(Blueprint)
		{
			if(Blueprint->SimpleConstructionScript->FindSCSNode(AttachName) != nullptr)
			{
				return Blueprint->GetFName();
			}

			Blueprint = Cast<UBlueprint>(Blueprint->GeneratedClass->GetSuperClass()->ClassGeneratedBy);
		}
	}

	return NAME_None;
}

void UActorRecording::StartRecordingNewComponents(ULevelSequence* CurrentSequence, float CurrentSequenceTime)
{
	if (ActorToRecord.IsValid())
	{
		// find the new component(s)
		TArray<USceneComponent*> NewComponents;
		TArray<USceneComponent*> SceneComponents;
		GetSceneComponents(SceneComponents);
		for(USceneComponent* SceneComponent : SceneComponents)
		{
			if(ValidComponent(SceneComponent))
			{
				TWeakObjectPtr<USceneComponent> WeakSceneComponent(SceneComponent);
				int32 FoundIndex = TrackedComponents.Find(WeakSceneComponent);
				if(FoundIndex == INDEX_NONE)
				{
					// new component!
					NewComponents.Add(SceneComponent);
				}
			}
		}

		UMovieScene* MovieScene = CurrentSequence->GetMovieScene();
		check(MovieScene);

		FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(Guid);
		check(Spawnable);

		AActor* ObjectTemplate = CastChecked<AActor>(Spawnable->GetObjectTemplate());

		for(USceneComponent* SceneComponent : NewComponents)
		{
			// new component, so we need to add this to our BP if it didn't come from SCS
			FName NewName;
			if(SceneComponent->CreationMethod != EComponentCreationMethod::SimpleConstructionScript)
			{
				// Give this component a unique name within its parent
				NewName = *FString::Printf(TEXT("Dynamic%s"), *SceneComponent->GetFName().GetPlainNameString());
				NewName.SetNumber(1);
				while (FindObjectFast<UObject>(ObjectTemplate, NewName))
				{
					NewName.SetNumber(NewName.GetNumber() + 1);
				}

				USceneComponent* TemplateRoot = ObjectTemplate->GetRootComponent();
				USceneComponent* AttachToComponent = TemplateRoot;

				// look for a similar attach parent in the current structure
				if(SceneComponent->GetAttachParent() != nullptr)
				{
					FName AttachName = SceneComponent->GetAttachParent()->GetFName();

					TInlineComponentArray<USceneComponent*> AllChildren;
					ObjectTemplate->GetComponents(AllChildren);

					for (USceneComponent* Child : AllChildren)
					{
						if (Child->GetFName() == AttachName)
						{
							AttachToComponent = Child;
							break;
						}
					}
				}

				USceneComponent* NewTemplateComponent = DuplicateObject<USceneComponent>(SceneComponent, ObjectTemplate, NewName);
				NewTemplateComponent->AttachToComponent(AttachToComponent, FAttachmentTransformRules::KeepRelativeTransform, SceneComponent->GetAttachSocketName());

				ObjectTemplate->AddInstanceComponent(NewTemplateComponent);
			}
			else
			{
				NewName = SceneComponent->GetFName();
			}

			StartRecordingComponentProperties(NewName, SceneComponent, ActorToRecord.Get(), CurrentSequence, CurrentSequenceTime);

			bNewComponentAddedWhileRecording = true;
		}
		
		SyncTrackedComponents();
	}
}
