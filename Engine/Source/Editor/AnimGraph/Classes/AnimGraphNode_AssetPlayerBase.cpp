// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphPrivatePCH.h"
#include "AnimGraphNode_AssetPlayerBase.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_RotationOffsetBlendSpace.h"
#include "AnimGraphNode_BlendSpacePlayer.h"

bool IsAimOffsetBlendSpace(const UClass* BlendSpaceClass)
{
	return  BlendSpaceClass->IsChildOf(UAimOffsetBlendSpace::StaticClass()) ||
		BlendSpaceClass->IsChildOf(UAimOffsetBlendSpace1D::StaticClass());
}

UClass* GetNodeClassForAsset(const UClass* AssetClass)
{
	if (AssetClass->IsChildOf(UAnimSequence::StaticClass()))
	{
		return UAnimGraphNode_SequencePlayer::StaticClass();
	}
	else if (AssetClass->IsChildOf(UBlendSpaceBase::StaticClass()))
	{
		if (IsAimOffsetBlendSpace(AssetClass))
		{
			return UAnimGraphNode_RotationOffsetBlendSpace::StaticClass();
		}
		else
		{
			return UAnimGraphNode_BlendSpacePlayer::StaticClass();
		}
	}
	else if (AssetClass->IsChildOf(UAnimComposite::StaticClass()))
	{
		return UAnimGraphNode_SequencePlayer::StaticClass();
	}
	return nullptr;
}

