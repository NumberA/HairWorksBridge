// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "EnginePrivate.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimInstanceProxy.h"

/////////////////////////////////////////////////////
// FAnimationBaseContext

// @todo: remove after deprecation
FAnimationBaseContext::FAnimationBaseContext(UAnimInstance* InAnimInstance)
	: AnimInstanceProxy(&InAnimInstance->GetProxyOnAnyThread<FAnimInstanceProxy>())
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	AnimInstance = InAnimInstance;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FAnimationBaseContext::FAnimationBaseContext(FAnimInstanceProxy* InAnimInstanceProxy)
	: AnimInstanceProxy(InAnimInstanceProxy)
{
	// @todo: remove after deprecation
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	AnimInstance = CastChecked<UAnimInstance>(AnimInstanceProxy->GetAnimInstanceObject());
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FAnimationBaseContext::FAnimationBaseContext(const FAnimationBaseContext& InContext)
{
	// @todo: remove after deprecation
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	AnimInstance = InContext.AnimInstance;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	AnimInstanceProxy = InContext.AnimInstanceProxy;
}

UAnimBlueprintGeneratedClass* FAnimationBaseContext::GetAnimBlueprintClass() const
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return AnimInstanceProxy->GetAnimBlueprintClass();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

IAnimClassInterface* FAnimationBaseContext::GetAnimClass() const
{
	return AnimInstanceProxy ? AnimInstanceProxy->GetAnimClassInterface() : nullptr;
}

#if WITH_EDITORONLY_DATA
UAnimBlueprint* FAnimationBaseContext::GetAnimBlueprint() const
{
	return AnimInstanceProxy ? AnimInstanceProxy->GetAnimBlueprint() : nullptr;
}
#endif //WITH_EDITORONLY_DATA

/////////////////////////////////////////////////////
// FPoseContext

void FPoseContext::Initialize(FAnimInstanceProxy* InAnimInstanceProxy)
{
	checkSlow(AnimInstanceProxy && AnimInstanceProxy->GetRequiredBones().IsValid());
	Pose.SetBoneContainer(&AnimInstanceProxy->GetRequiredBones());
	Curve.InitFrom(AnimInstanceProxy->GetSkelMeshComponent()->GetCachedAnimCurveMappingNameUids());
}

/////////////////////////////////////////////////////
// FComponentSpacePoseContext

void FComponentSpacePoseContext::ResetToRefPose()
{
	checkSlow(AnimInstanceProxy && AnimInstanceProxy->GetRequiredBones().IsValid());
	Pose.InitPose(&AnimInstanceProxy->GetRequiredBones());
	Curve.InitFrom(AnimInstanceProxy->GetSkelMeshComponent()->GetCachedAnimCurveMappingNameUids());
}

/////////////////////////////////////////////////////
// FAnimNode_Base

void FAnimNode_Base::Initialize(const FAnimationInitializeContext& Context)
{
	EvaluateGraphExposedInputs.Initialize(this, Context.AnimInstanceProxy->GetAnimInstanceObject());
}

bool FAnimNode_Base::IsLODEnabled(FAnimInstanceProxy* AnimInstanceProxy, int32 InLODThreshold)
{
	return (InLODThreshold < 0 || AnimInstanceProxy->GetSkelMeshComponent()->PredictedLODLevel <= InLODThreshold);
}

/////////////////////////////////////////////////////
// FPoseLinkBase

void FPoseLinkBase::AttemptRelink(const FAnimationBaseContext& Context)
{
	// Do the linkage
	if ((LinkedNode == NULL) && (LinkID != INDEX_NONE))
	{
		IAnimClassInterface* AnimBlueprintClass = Context.GetAnimClass();
		check(AnimBlueprintClass);

		// adding ensure. We had a crash on here
		if ( ensure(AnimBlueprintClass->GetAnimNodeProperties().IsValidIndex(LinkID)) )
		{
			UProperty* LinkedProperty = AnimBlueprintClass->GetAnimNodeProperties()[LinkID];
			void* LinkedNodePtr = LinkedProperty->ContainerPtrToValuePtr<void>(Context.AnimInstanceProxy->GetAnimInstanceObject());
			LinkedNode = (FAnimNode_Base*)LinkedNodePtr;
		}
	}
}

void FPoseLinkBase::Initialize(const FAnimationInitializeContext& Context)
{
#if DO_CHECK
	checkf(!bProcessed, TEXT("Initialize already in progress, circular link for AnimInstance [%s] Blueprint [%s]"), \
		*Context.AnimInstanceProxy->GetAnimInstanceName(), *GetFullNameSafe(IAnimClassInterface::GetActualAnimClass(Context.AnimInstanceProxy->GetAnimClassInterface())));
	TGuardValue<bool> CircularGuard(bProcessed, true);
#endif

	AttemptRelink(Context);

#if ENABLE_ANIMGRAPH_TRAVERSAL_DEBUG
	InitializationCounter.SynchronizeWith(Context.AnimInstanceProxy->GetInitializationCounter());
#endif

	// Do standard initialization
	if (LinkedNode != NULL)
	{
		LinkedNode->Initialize(Context);
	}
}

void FPoseLinkBase::CacheBones(const FAnimationCacheBonesContext& Context) 
{
#if DO_CHECK
	checkf( !bProcessed, TEXT( "CacheBones already in progress, circular link for AnimInstance [%s] Blueprint [%s]" ), \
		*Context.AnimInstanceProxy->GetAnimInstanceName(), *GetFullNameSafe(IAnimClassInterface::GetActualAnimClass(Context.AnimInstanceProxy->GetAnimClassInterface())));
	TGuardValue<bool> CircularGuard(bProcessed, true);
#endif

#if ENABLE_ANIMGRAPH_TRAVERSAL_DEBUG
	CachedBonesCounter.SynchronizeWith(Context.AnimInstanceProxy->GetCachedBonesCounter());
#endif

	if (LinkedNode != NULL)
	{
		LinkedNode->CacheBones(Context);
	}
}

void FPoseLinkBase::Update(const FAnimationUpdateContext& Context)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FPoseLinkBase_Update);
#if DO_CHECK
	checkf( !bProcessed, TEXT( "Update already in progress, circular link for AnimInstance [%s] Blueprint [%s]" ), \
		*Context.AnimInstanceProxy->GetAnimInstanceName(), *GetFullNameSafe(IAnimClassInterface::GetActualAnimClass(Context.AnimInstanceProxy->GetAnimClassInterface())));
	TGuardValue<bool> CircularGuard(bProcessed, true);
#endif

#if WITH_EDITOR
	if (GIsEditor)
	{
		if (LinkedNode == NULL)
		{
			//@TODO: Should only do this when playing back
			AttemptRelink(Context);
		}

		// Record the node line activation
		if (LinkedNode != NULL)
		{
			if (Context.AnimInstanceProxy->IsBeingDebugged())
			{
				Context.AnimInstanceProxy->RecordNodeVisit(LinkID, SourceLinkID, Context.GetFinalBlendWeight());
			}
		}
	}
#endif

#if ENABLE_ANIMGRAPH_TRAVERSAL_DEBUG
	checkf(InitializationCounter.IsSynchronizedWith(Context.AnimInstanceProxy->GetInitializationCounter()), TEXT("Calling Update without initialization!"));
	checkf(!UpdateCounter.IsSynchronizedWith(Context.AnimInstanceProxy->GetUpdateCounter()), TEXT("Already called Update for this node!"));
	UpdateCounter.SynchronizeWith(Context.AnimInstanceProxy->GetUpdateCounter());
#endif

	if (LinkedNode != NULL)
	{
		LinkedNode->Update(Context);
	}
}

void FPoseLinkBase::GatherDebugData(FNodeDebugData& DebugData)
{
	if(LinkedNode != NULL)
	{
		LinkedNode->GatherDebugData(DebugData);
	}
}

/////////////////////////////////////////////////////
// FPoseLink

void FPoseLink::Evaluate(FPoseContext& Output)
{
#if DO_CHECK
	checkf( !bProcessed, TEXT( "Evaluate already in progress, circular link for AnimInstance [%s] Blueprint [%s]" ), \
		*Output.AnimInstanceProxy->GetAnimInstanceName(), *GetFullNameSafe(IAnimClassInterface::GetActualAnimClass(Output.AnimInstanceProxy->GetAnimClassInterface())));
	TGuardValue<bool> CircularGuard(bProcessed, true);
#endif

#if WITH_EDITOR
	if ((LinkedNode == NULL) && GIsEditor)
	{
		//@TODO: Should only do this when playing back
		AttemptRelink(Output);
	}
#endif

#if ENABLE_ANIMGRAPH_TRAVERSAL_DEBUG
	checkf(InitializationCounter.IsSynchronizedWith(Output.AnimInstanceProxy->GetInitializationCounter()), TEXT("Calling Evaluate without initialization!"));
	checkf(CachedBonesCounter.IsSynchronizedWith(Output.AnimInstanceProxy->GetCachedBonesCounter()), TEXT("Calling Evaluate without CachedBones!"));
	checkf(UpdateCounter.IsSynchronizedWith(Output.AnimInstanceProxy->GetUpdateCounter()), TEXT("Calling Evaluate without Update for this node!"));
	checkf(!EvaluationCounter.IsSynchronizedWith(Output.AnimInstanceProxy->GetEvaluationCounter()), TEXT("Already called Evaluate for this node!"));
	EvaluationCounter.SynchronizeWith(Output.AnimInstanceProxy->GetEvaluationCounter());
#endif

	if (LinkedNode != NULL)
	{
#if ENABLE_ANIMNODE_POSE_DEBUG
		CurrentPose.ResetToIdentity();
#endif
		LinkedNode->Evaluate(Output);
#if ENABLE_ANIMNODE_POSE_DEBUG
		CurrentPose = Output.Pose;
#endif
	}
	else
	{
		//@TODO: Warning here?
		Output.ResetToRefPose();
	}

	// Detect non valid output
	checkSlow( !Output.ContainsNaN() );
	checkSlow( Output.IsNormalized() );
}

/////////////////////////////////////////////////////
// FComponentSpacePoseLink

void FComponentSpacePoseLink::EvaluateComponentSpace(FComponentSpacePoseContext& Output)
{
#if DO_CHECK
	checkf( !bProcessed, TEXT( "EvaluateComponentSpace already in progress, circular link for AnimInstance [%s] Blueprint [%s]" ), \
		*Output.AnimInstanceProxy->GetAnimInstanceName(), *GetFullNameSafe(IAnimClassInterface::GetActualAnimClass(Output.AnimInstanceProxy->GetAnimClassInterface())));
	TGuardValue<bool> CircularGuard(bProcessed, true);
#endif

#if ENABLE_ANIMGRAPH_TRAVERSAL_DEBUG
	checkf(InitializationCounter.IsSynchronizedWith(Output.AnimInstanceProxy->GetInitializationCounter()), TEXT("Calling EvaluateComponentSpace without initialization!"));
	checkf(CachedBonesCounter.IsSynchronizedWith(Output.AnimInstanceProxy->GetCachedBonesCounter()), TEXT("Calling EvaluateComponentSpace without CachedBones!"));
	checkf(UpdateCounter.IsSynchronizedWith(Output.AnimInstanceProxy->GetUpdateCounter()), TEXT("Calling EvaluateComponentSpace without Update for this node!"));
	checkf(!EvaluationCounter.IsSynchronizedWith(Output.AnimInstanceProxy->GetEvaluationCounter()), TEXT("Already called EvaluateComponentSpace for this node!"));
	EvaluationCounter.SynchronizeWith(Output.AnimInstanceProxy->GetEvaluationCounter());
#endif

	if (LinkedNode != NULL)
	{
		LinkedNode->EvaluateComponentSpace(Output);
	}
	else
	{
		//@TODO: Warning here?
		Output.ResetToRefPose();
	}

	// Detect non valid output
	checkSlow( !Output.ContainsNaN() );
	checkSlow( Output.IsNormalized() );
}

/////////////////////////////////////////////////////
// FComponentSpacePoseContext

bool FComponentSpacePoseContext::ContainsNaN() const
{
	return Pose.GetPose().ContainsNaN();
}

bool FComponentSpacePoseContext::IsNormalized() const
{
	return Pose.GetPose().IsNormalized();
}

/////////////////////////////////////////////////////
// FNodeDebugData

void FNodeDebugData::AddDebugItem(FString DebugData, bool bPoseSource)
{
	check(NodeChain.Num() == 0 || NodeChain.Last().ChildNodeChain.Num() == 0); //Cannot add to this chain once we have branched

	NodeChain.Add( DebugItem(DebugData, bPoseSource) );
}

FNodeDebugData& FNodeDebugData::BranchFlow(float BranchWeight, FString InNodeDescription)
{
	DebugItem& LatestItem = NodeChain.Last();
	new (LatestItem.ChildNodeChain) FNodeDebugData(AnimInstance, BranchWeight*AbsoluteWeight, InNodeDescription);
	return LatestItem.ChildNodeChain.Last();
}

void FNodeDebugData::GetFlattenedDebugData(TArray<FFlattenedDebugData>& FlattenedDebugData, int32 Indent, int32& ChainID)
{
	int32 CurrChainID = ChainID;
	for(DebugItem& Item : NodeChain)
	{
		FlattenedDebugData.Add( FFlattenedDebugData(Item.DebugData, AbsoluteWeight, Indent, CurrChainID, Item.bPoseSource) );
		bool bMultiBranch = Item.ChildNodeChain.Num() > 1;
		int32 ChildIndent = bMultiBranch ? Indent + 1 : Indent;
		for(FNodeDebugData& Child : Item.ChildNodeChain)
		{
			if(bMultiBranch)
			{
				// If we only have one branch we treat it as the same really
				// as we may have only changed active status
				++ChainID;
			}
			Child.GetFlattenedDebugData(FlattenedDebugData, ChildIndent, ChainID);
		}
	}
}

void FExposedValueCopyRecord::PostSerialize(const FArchive& Ar)
{
	// backwards compatibility: check value of deprecated source property and patch up property name
	if(SourceProperty_DEPRECATED && SourcePropertyName == NAME_None)
	{
		SourcePropertyName = SourceProperty_DEPRECATED->GetFName();
	}
}

void FExposedValueHandler::Initialize(FAnimNode_Base* AnimNode, UObject* AnimInstanceObject) 
{
	if(bInitialized)
	{
		return;
	}

	if (BoundFunction != NAME_None)
	{
		// we cant call FindFunction on anything but the game thread as it accesses a shared map in the object's class
		check(IsInGameThread());
		Function = AnimInstanceObject->FindFunction(BoundFunction);
		check(Function);
	}
	else
	{
		Function = NULL;
	}

	// initialize copy records
	for(auto& CopyRecord : CopyRecords)
	{
		CopyRecord.SourceProperty_DEPRECATED = nullptr;

		UProperty* SourceProperty = AnimInstanceObject->GetClass()->FindPropertyByName(CopyRecord.SourcePropertyName);
		check(SourceProperty);
		if(UArrayProperty* SourceArrayProperty = Cast<UArrayProperty>(SourceProperty))
		{
			// the compiler should not be generating any code that calls down this path at the moment - it is untested
			check(false);
		//	FScriptArrayHelper ArrayHelper(SourceArrayProperty, SourceProperty->ContainerPtrToValuePtr<uint8>(AnimInstanceObject));
		//	check(ArrayHelper.IsValidIndex(CopyRecord.SourceArrayIndex));
		//	CopyRecord.Source = ArrayHelper.GetRawPtr(CopyRecord.SourceArrayIndex);
		//	CopyRecord.Size = ArrayHelper.Num() * SourceArrayProperty->Inner->GetSize();
		}
		else
		{
			if(CopyRecord.SourceSubPropertyName != NAME_None)
			{
				void* Source = SourceProperty->ContainerPtrToValuePtr<uint8>(AnimInstanceObject, 0);
				UStructProperty* SourceStructProperty = CastChecked<UStructProperty>(SourceProperty);
				UProperty* SourceStructSubProperty = SourceStructProperty->Struct->FindPropertyByName(CopyRecord.SourceSubPropertyName);
				CopyRecord.Source = SourceStructSubProperty->ContainerPtrToValuePtr<uint8>(Source, CopyRecord.SourceArrayIndex);
				CopyRecord.Size = SourceStructSubProperty->GetSize();
				CopyRecord.SourceProperty_DEPRECATED = Cast<UBoolProperty>(SourceStructSubProperty);
				if (CopyRecord.SourceProperty_DEPRECATED)
				{
					CopyRecord.Source = Source;
				}
			}
			else
			{
				CopyRecord.Source = SourceProperty->ContainerPtrToValuePtr<uint8>(AnimInstanceObject, CopyRecord.SourceArrayIndex);
				CopyRecord.Size = SourceProperty->GetSize();
				CopyRecord.SourceProperty_DEPRECATED = Cast<UBoolProperty>(SourceProperty);
				if (CopyRecord.SourceProperty_DEPRECATED)
				{
					CopyRecord.Source = AnimInstanceObject;
				}
			}
		}

		if(UArrayProperty* DestArrayProperty = Cast<UArrayProperty>(CopyRecord.DestProperty))
		{
			FScriptArrayHelper ArrayHelper(DestArrayProperty, CopyRecord.DestProperty->ContainerPtrToValuePtr<uint8>(AnimNode));
			check(ArrayHelper.IsValidIndex(CopyRecord.DestArrayIndex));
			CopyRecord.Dest = ArrayHelper.GetRawPtr(CopyRecord.DestArrayIndex);
			if (CopyRecord.DestProperty->IsA<UBoolProperty>())
			{
				CopyRecord.Dest = AnimNode;
			}
		}
		else
		{
			CopyRecord.Dest = CopyRecord.DestProperty->ContainerPtrToValuePtr<uint8>(AnimNode, CopyRecord.DestArrayIndex);
			if (CopyRecord.DestProperty->IsA<UBoolProperty>())
			{
				CopyRecord.Dest = AnimNode;
			}
		}
	}

	bInitialized = true;
}

void FExposedValueHandler::Execute(const FAnimationBaseContext& Context) const
{
	if (Function != nullptr)
	{
		Context.AnimInstanceProxy->GetAnimInstanceObject()->ProcessEvent(Function, NULL);
	}

	for(const FExposedValueCopyRecord& CopyRecord : CopyRecords)
	{
		// if any of these checks fail then it's likely that Initialize has not been called.
		// has new anim node type been added that doesnt call the base class Initialize()?
		checkSlow(CopyRecord.Dest != nullptr);
		checkSlow(CopyRecord.Source != nullptr);
		checkSlow(CopyRecord.Size != 0);
		
		switch(CopyRecord.PostCopyOperation)
		{
		case EPostCopyOperation::None:
			{
				
				if (CopyRecord.SourceProperty_DEPRECATED != nullptr)
				{
					UBoolProperty* DestBoolProperty = CastChecked<UBoolProperty>(CopyRecord.DestProperty);
					bool bValue = Cast<UBoolProperty>(CopyRecord.SourceProperty_DEPRECATED)->GetPropertyValue_InContainer(CopyRecord.Source);
					Cast<UBoolProperty>(DestBoolProperty)->SetPropertyValue_InContainer(CopyRecord.Dest, bValue, CopyRecord.DestArrayIndex);
				}
				else
				{
					FMemory::Memcpy(CopyRecord.Dest, CopyRecord.Source, CopyRecord.Size);
				}
			}
			break;
		case EPostCopyOperation::LogicalNegateBool:
			{
				UBoolProperty* DestBoolProperty = Cast<UBoolProperty>(CopyRecord.DestProperty);
				check(CopyRecord.SourceProperty_DEPRECATED != nullptr && DestBoolProperty != nullptr);

				bool bValue = Cast<UBoolProperty>(CopyRecord.SourceProperty_DEPRECATED)->GetPropertyValue_InContainer(CopyRecord.Source);
				Cast<UBoolProperty>(DestBoolProperty)->SetPropertyValue_InContainer(CopyRecord.Dest, !bValue, CopyRecord.DestArrayIndex);
			}
			break;
		}
	}
}