// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "AnimNode_SequenceEvaluator.generated.h"

// Evaluates a point in an anim sequence, using a specific time input rather than advancing time internally.
// Typically the playback position of the animation for this node will represent something other than time, like jump height.
// This node will not trigger any notifies present in the associated sequence.
USTRUCT()
struct ANIMGRAPHRUNTIME_API FAnimNode_SequenceEvaluator : public FAnimNode_AssetPlayerBase
{
	GENERATED_USTRUCT_BODY()
public:
	// The animation sequence asset to evaluate
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	UAnimSequenceBase* Sequence;

	// The time at which to evaluate the associated sequence
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinShownByDefault))
	mutable float ExplicitTime;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	bool bShouldLoopWhenInSyncGroup;

public:	
	FAnimNode_SequenceEvaluator()
		: ExplicitTime(0.0f)
	{
	}

	// FAnimNode_Base interface
	virtual void Initialize(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones(const FAnimationCacheBonesContext& Context) override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate(FPoseContext& Output) override;
	virtual void OverrideAsset(UAnimationAsset* NewAsset) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// FAnimNode_AssetPlayerBase Interface
	virtual float GetAccumulatedTime() {return ExplicitTime;}
	virtual void SetAccumulatedTime(const float& NewTime) {ExplicitTime = NewTime;}
	virtual UAnimationAsset* GetAnimAsset() {return Sequence;}
	// End of FAnimNode_AssetPlayerBase Interface
};
