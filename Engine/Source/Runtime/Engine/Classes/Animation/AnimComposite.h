// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/**
 * Abstract base class of animation made of multiple sequences.
 *
 */

#pragma once
#include "AnimCompositeBase.h"
#include "AnimComposite.generated.h"

UCLASS(config=Engine, hidecategories=UObject, MinimalAPI, BlueprintType)
class UAnimComposite : public UAnimCompositeBase
{
	GENERATED_UCLASS_BODY()

public:
	/** Serializable data that stores section/anim pairing **/
	UPROPERTY()
	struct FAnimTrack AnimationTrack;

#if WITH_EDITORONLY_DATA
	/** Preview Base pose for additive BlendSpace **/
	UPROPERTY(EditAnywhere, Category=AdditiveSettings)
	UAnimSequence* PreviewBasePose;
#endif // WITH_EDITORONLY_DATA

	//~ Begin UAnimSequenceBase Interface
	ENGINE_API virtual void HandleAssetPlayerTickedInternal(FAnimAssetTickContext &Context, const float PreviousTime, const float MoveDelta, const FAnimTickRecord &Instance, struct FAnimNotifyQueue& NotifyQueue) const override;
	virtual void GetAnimationPose(FCompactPose& OutPose, FBlendedCurve& OutCurve, const FAnimExtractContext& ExtractionContext) const override;	
	//~ End UAnimSequenceBase Interface
	//~ Begin UAnimSequence Interface
#if WITH_EDITOR
	virtual bool GetAllAnimationSequencesReferred(TArray<UAnimSequence*>& AnimationSequences) override;
	virtual void ReplaceReferredAnimations(const TMap<UAnimSequence*, UAnimSequence*>& ReplacementMap) override;
#endif
	//~ End UAnimSequence Interface
};

