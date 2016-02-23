// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphRuntimePrivatePCH.h"
#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "Animation/AnimInstanceProxy.h"

/////////////////////////////////////////////////////
// FAnimSequenceEvaluatorNode

void FAnimNode_SequenceEvaluator::Initialize(const FAnimationInitializeContext& Context)
{
	FAnimNode_AssetPlayerBase::Initialize(Context);
}

void FAnimNode_SequenceEvaluator::CacheBones(const FAnimationCacheBonesContext& Context) 
{
}

void FAnimNode_SequenceEvaluator::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	EvaluateGraphExposedInputs.Execute(Context);

	if (Sequence)
	{
		// Clamp input to a valid position on this sequence's time line.
		ExplicitTime = FMath::Clamp(ExplicitTime, 0.f, Sequence->SequenceLength);

		if ((GroupIndex != INDEX_NONE) && (Context.AnimInstanceProxy->IsSkeletonCompatible(Sequence->GetSkeleton())))
		{
			InternalTimeAccumulator = FMath::Clamp(InternalTimeAccumulator, 0.f, Sequence->SequenceLength);
			float TimeJump = ExplicitTime - InternalTimeAccumulator;
			if (bShouldLoopWhenInSyncGroup)
			{
				if (FMath::Abs(TimeJump) > (Sequence->SequenceLength * 0.5f))
				{
					if (TimeJump > 0.f)
					{
						TimeJump -= Sequence->SequenceLength;
					}
					else
					{
						TimeJump += Sequence->SequenceLength;
					}
				}
			}

			const float DeltaTime = Context.GetDeltaTime();
			const float PlayRate = FMath::IsNearlyZero(DeltaTime) ? 0.f : (TimeJump / DeltaTime);
			CreateTickRecordForNode(Context, Sequence, bShouldLoopWhenInSyncGroup, PlayRate);
		}
		else
		{
			InternalTimeAccumulator = ExplicitTime;
		}
	}
}

void FAnimNode_SequenceEvaluator::Evaluate(FPoseContext& Output)
{
	if ((Sequence != NULL) && (Output.AnimInstanceProxy->IsSkeletonCompatible(Sequence->GetSkeleton())))
	{
		Sequence->GetAnimationPose(Output.Pose, Output.Curve, FAnimExtractContext(InternalTimeAccumulator, Output.AnimInstanceProxy->ShouldExtractRootMotion()));
	}
	else
	{
		Output.ResetToRefPose();
	}
}

void FAnimNode_SequenceEvaluator::OverrideAsset(UAnimationAsset* NewAsset)
{
	if(UAnimSequenceBase* NewSequence = Cast<UAnimSequenceBase>(NewAsset))
	{
		Sequence = NewSequence;
	}
}

void FAnimNode_SequenceEvaluator::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
	
	DebugLine += FString::Printf(TEXT("('%s' Play Time: %.3f)"), *GetNameSafe(Sequence), ExplicitTime);
	DebugData.AddDebugItem(DebugLine, true);
}