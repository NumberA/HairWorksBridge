// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.


#include "EnginePrivate.h"
#include "SoundDefinitions.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundNodeLooping.h"
#include "Sound/SoundNodeWavePlayer.h"

/*-----------------------------------------------------------------------------
	USoundNodeLooping implementation.
-----------------------------------------------------------------------------*/
USoundNodeLooping::USoundNodeLooping(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, LoopCount(1)
	, bLoopIndefinitely(true)
{
}

void USoundNodeLooping::ParseNodes( FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash, FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams, TArray<FWaveInstance*>& WaveInstances )
{
	RETRIEVE_SOUNDNODE_PAYLOAD(sizeof(int32));
	DECLARE_SOUNDNODE_ELEMENT(int32, CurrentLoopCount);

	if (*RequiresInitialization)
	{
		CurrentLoopCount = 0;

		*RequiresInitialization = false;
	}

#if !(NO_LOGGING || UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (bLoopIndefinitely && !ActiveSound.bWarnedAboutOrphanedLooping && ActiveSound.GetAudioComponent() == nullptr)
	{
		UE_LOG(LogAudio, Warning, TEXT("Detected orphaned looping sound '%s'."), *ActiveSound.Sound->GetName());
		ActiveSound.bWarnedAboutOrphanedLooping = true;
	}
#endif

	FSoundParseParameters UpdatedParams = ParseParams;
	UpdatedParams.NotifyBufferFinishedHooks.AddNotify(this, NodeWaveInstanceHash);

	Super::ParseNodes( AudioDevice, NodeWaveInstanceHash, ActiveSound, UpdatedParams, WaveInstances );
}

bool USoundNodeLooping::NotifyWaveInstanceFinished( FWaveInstance* InWaveInstance )
{
	FActiveSound& ActiveSound = *InWaveInstance->ActiveSound;
	const UPTRINT NodeWaveInstanceHash = InWaveInstance->NotifyBufferFinishedHooks.GetHashForNode(this);
	RETRIEVE_SOUNDNODE_PAYLOAD(sizeof(int32));
	DECLARE_SOUNDNODE_ELEMENT(int32, CurrentLoopCount);
	check(*RequiresInitialization == 0);

	if (bLoopIndefinitely || ++CurrentLoopCount < LoopCount)
	{
		struct FNodeHashPairs
		{
			USoundNode* Node;
			UPTRINT NodeWaveInstanceHash;

			FNodeHashPairs(USoundNode* InNode, const UPTRINT InHash)
				: Node(InNode)
				, NodeWaveInstanceHash(InHash)
			{
			}
		};

		TArray<FNodeHashPairs> NodesToReset;

		for (int32 ChildNodeIndex = 0; ChildNodeIndex < ChildNodes.Num(); ++ChildNodeIndex)
		{
			USoundNode* ChildNode = ChildNodes[ChildNodeIndex];
			if (ChildNode)
			{
				NodesToReset.Add(FNodeHashPairs(ChildNode, GetNodeWaveInstanceHash(NodeWaveInstanceHash, ChildNode, ChildNodeIndex)));
			}
		}

		// GetAllNodes includes current node so we have to start at Index 1.
		for (int32 ResetNodeIndex = 0; ResetNodeIndex < NodesToReset.Num(); ++ResetNodeIndex)
		{
			const FNodeHashPairs& NodeHashPair = NodesToReset[ResetNodeIndex];

			// Reset all child nodes so they are initialized again.
			uint32* Offset = ActiveSound.SoundNodeOffsetMap.Find(NodeHashPair.NodeWaveInstanceHash);
			if (Offset)
			{
				bool* bRequiresInitialization = (bool*)&ActiveSound.SoundNodeData[*Offset];
				*bRequiresInitialization = true;
			}

			USoundNode* ResetNode = NodeHashPair.Node;

			if (ResetNode->ChildNodes.Num())
			{
				for (int32 ResetChildIndex = 0; ResetChildIndex < ResetNode->ChildNodes.Num(); ++ResetChildIndex)
				{
					USoundNode* ResetChildNode = ResetNode->ChildNodes[ResetChildIndex];
					if (ResetChildNode)
					{
						NodesToReset.Add(FNodeHashPairs(ResetChildNode, GetNodeWaveInstanceHash(NodeHashPair.NodeWaveInstanceHash, ResetChildNode, ResetChildIndex)));
					}
				}
			}
			else if (ResetNode->IsA<USoundNodeWavePlayer>())
			{
				FWaveInstance* WaveInstance = ActiveSound.FindWaveInstance(NodeHashPair.NodeWaveInstanceHash);
				if (WaveInstance)
				{
					WaveInstance->bAlreadyNotifiedHook = true;
					WaveInstance->bIsStarted = false;
					WaveInstance->bIsFinished = false;
				}
			}
		}

		// Reset wave instances that notified us of completion.
		InWaveInstance->bIsStarted = false;
		InWaveInstance->bIsFinished = false;

		return true;
	}

	return false;
}

float USoundNodeLooping::GetDuration()
{
	// Assume no duration (i.e. no input node)
	float Duration = 0.0f;

	// If we have any child nodes
	if (ChildNodes.Num() > 0)
	{
		// If we're told to loop indefinitely, then the duration will be "infinite"
		if (bLoopIndefinitely)
		{
			Duration = INDEFINITELY_LOOPING_DURATION;
		}
		else
		{
			// Looping nodes can only have one child node
			check(ChildNodes.Num() == 1);
			if (USoundNode* Child = ChildNodes[0])
			{
				// Duration will be the loop count times the child node duration
				Duration = LoopCount * Child->GetDuration();
			}
		}
	}

	return Duration;
}

int32 USoundNodeLooping::GetNumSounds(const UPTRINT NodeWaveInstanceHash, FActiveSound& ActiveSound) const
{
	// Number of sounds this node plays is essentially infinite if told to bLoopIndefinitely
	if (bLoopIndefinitely)
	{
		return (int32)INDEFINITELY_LOOPING_DURATION;
	}
	// Looping nodes count as 1 sound finishing since the looping node captures 
	// sounddone hooks except for the last one (when the loop count is reached)
	return 1;
}



