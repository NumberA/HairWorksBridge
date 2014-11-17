// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "BlueprintGraphPrivatePCH.h"
#include "BlueprintActionDatabase.h"
#include "EdGraphSchema_K2.h"       // for CanUserKismetCallFunction()
#include "KismetEditorUtilities.h"	// for IsClassABlueprintSkeleton(), IsClassABlueprintInterface(), GetBoundsForSelectedNodes(), etc.
#include "BlueprintNodeSpawner.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintPropertyNodeSpawner.h"
#include "BlueprintEventNodeSpawner.h"
#include "BlueprintComponentNodeSpawner.h"

// used below in FBlueprintNodeSpawnerFactory::MakeMacroNodeSpawner()
#include "K2Node_MacroInstance.h"
// used below in FBlueprintActionDatabaseImpl::AddClassEnumActions()
#include "K2Node_GetNumEnumEntries.h"
#include "K2Node_ForEachElementInEnum.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_CastByteToEnum.h"
#include "K2Node_SwitchEnum.h"
// used below in FBlueprintActionDatabaseImpl::AddClassStructActions()
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_SetFieldsInStruct.h"
// used below in FBlueprintNodeSpawnerFactory::MakeMessageNodeSpawner()
#include "K2Node_Message.h"
// used below in FBlueprintActionDatabaseImpl::AddClassPropertyActions()
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_ClearDelegate.h"
// used below in FBlueprintActionDatabaseImpl::AddClassCastActions()
#include "K2Node_DynamicCast.h"
#include "K2Node_ClassDynamicCast.h"
// used below in FBlueprintNodeSpawnerFactory::MakeCommentNodeSpawner()
#include "EdGraph/EdGraphNode_Comment.h"

/*******************************************************************************
 * FBlueprintNodeSpawnerFactory
 ******************************************************************************/

namespace FBlueprintNodeSpawnerFactory
{
	/**
	 * Constructs a UK2Node_MacroInstance spawner. Evolved from 
	 * FK2ActionMenuBuilder::AttachMacroGraphAction(). Sets up the spawner to 
	 * set spawned nodes with the supplied macro.
	 *
	 * @param  MacroGraph	The macro you want spawned nodes referencing.
	 * @return A new node-spawner, setup to spawn a UK2Node_MacroInstance.
	 */
	static UBlueprintNodeSpawner* MakeMacroNodeSpawner(UEdGraph* MacroGraph);

	/**
	 * A templatized method which constructs a node-spawner for various enum 
	 * node types (any node with a public UEnum* Enum field). Takes the 
	 * specified enum and applies it to the node post-spawn.
	 *
	 * @param  Enum					The enum you want set for the spawned node.
	 * @param  ExtraSetupCallback	A callback for any further post-spawn customization (other than setting the node's Enum field).
	 * @return A new node-spawner, setup to spawn some enum node (defined by the EnumNodeType template param).
	 */
	template<class EnumNodeType>
	static UBlueprintNodeSpawner* MakeEnumNodeSpawner(UEnum* Enum, void(*ExtraSetupCallback)(EnumNodeType*) = nullptr);

	/**
	 * A templatized method which constructs a node-spawner for various struct 
	 * node types (any node with a public UScriptStruct* StructType field). 
	 * Takes the specified struct and applies it to the node post-spawn.
	 *
	 * @param  Struct	The struct you want set for the spawned node.
	 * @return A new node-spawner, setup to spawn some struct node (defined by the StructNodeType template param).
	 */
	template<class StructNodeType>
	static UBlueprintNodeSpawner* MakeStructNodeSpawner(UScriptStruct* Struct);

	/**
	 * Constructs a UK2Node_Message spawner. Sets up the spawner to set 
	 * spawned nodes with the supplied function.
	 *
	 * @param  InterfaceFunction	The function you want spawned nodes referencing.
	 * @return A new node-spawner, setup to spawn a UK2Node_Message.
	 */
	static UBlueprintNodeSpawner* MakeMessageNodeSpawner(UFunction* InterfaceFunction);

	/**
	 * Constructs a UEdGraphNode_Comment spawner. Since UEdGraphNode_Comment is
	 * not a UK2Node then we can't have it create a spawner for itself (using 
	 * UK2Node's GetMenuActions() method).
	 *
	 * @TODO:  Fix it so comment nodes spawned this way will properly position 
	 *         themselves (FBlueprintActionMenuItem overrides positioning).
	 *
	 * @return A new node-spawner, setup to spawn a UEdGraphNode_Comment.
	 */
	static UBlueprintNodeSpawner* MakeCommentNodeSpawner();
};

//------------------------------------------------------------------------------
static UBlueprintNodeSpawner* FBlueprintNodeSpawnerFactory::MakeMacroNodeSpawner(UEdGraph* MacroGraph)
{
	check(MacroGraph != nullptr);
	check(MacroGraph->GetSchema()->GetGraphType(MacroGraph) == GT_Macro);

	UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(UK2Node_MacroInstance::StaticClass());
	check(NodeSpawner != nullptr);

	auto CustomizeMacroNodeLambda = [](UEdGraphNode* NewNode, bool bIsTemplateNode, TWeakObjectPtr<UEdGraph> MacroGraph)
	{
		UK2Node_MacroInstance* MacroNode = CastChecked<UK2Node_MacroInstance>(NewNode);
		if (MacroGraph.IsValid())
		{
			MacroNode->SetMacroGraph(MacroGraph.Get());
		}
	};

	TWeakObjectPtr<UEdGraph> GraphPtr = MacroGraph;
	NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(CustomizeMacroNodeLambda, GraphPtr);

	return NodeSpawner;
}

//------------------------------------------------------------------------------
template<class EnumNodeType>
static UBlueprintNodeSpawner* FBlueprintNodeSpawnerFactory::MakeEnumNodeSpawner(UEnum* Enum, void(*ExtraSetupCallback)(EnumNodeType*))
{
	UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(EnumNodeType::StaticClass());
	check(NodeSpawner != nullptr);

	DECLARE_DELEGATE_OneParam(FFurtherCustomizeNodeDelegate, EnumNodeType*);

	auto CustomizeEnumNodeLambda = [](UEdGraphNode* NewNode, bool bIsTemplateNode, TWeakObjectPtr<UEnum> EnumPtr, FFurtherCustomizeNodeDelegate FurtherCustomizeDelegate)
	{
		EnumNodeType* EnumNode = CastChecked<EnumNodeType>(NewNode);

		if (EnumPtr.IsValid())
		{
			EnumNode->Enum = EnumPtr.Get();
		}

		if (FurtherCustomizeDelegate.IsBound())
		{
			FurtherCustomizeDelegate.Execute(EnumNode);
		}
	};

	FFurtherCustomizeNodeDelegate FurtherCustomizeDelegate;
	if (ExtraSetupCallback != nullptr)
	{
		FurtherCustomizeDelegate = FFurtherCustomizeNodeDelegate::CreateStatic(ExtraSetupCallback);
	}

	TWeakObjectPtr<UEnum> EnumPtr = Enum;
	NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(CustomizeEnumNodeLambda, EnumPtr, FurtherCustomizeDelegate);

	return NodeSpawner;
}

//------------------------------------------------------------------------------
template<class StructNodeType>
static UBlueprintNodeSpawner* FBlueprintNodeSpawnerFactory::MakeStructNodeSpawner(UScriptStruct* Struct)
{
	UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(StructNodeType::StaticClass());
	check(NodeSpawner != nullptr);

	auto CustomizeStructNodeLambda = [](UEdGraphNode* NewNode, bool bIsTemplateNode, TWeakObjectPtr<UScriptStruct> StructPtr)
	{
		StructNodeType* StructNode = CastChecked<StructNodeType>(NewNode);
		if (StructPtr.IsValid())
		{
			StructNode->StructType = StructPtr.Get();
		}
	};

	TWeakObjectPtr<UScriptStruct> StructPtr = Struct;
	NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(CustomizeStructNodeLambda, StructPtr);

	return NodeSpawner;
}

//------------------------------------------------------------------------------
static UBlueprintNodeSpawner* FBlueprintNodeSpawnerFactory::MakeMessageNodeSpawner(UFunction* InterfaceFunction)
{
	check(InterfaceFunction != nullptr);
	check(FKismetEditorUtilities::IsClassABlueprintInterface(CastChecked<UClass>(InterfaceFunction->GetOuter())));

	UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(UK2Node_Message::StaticClass());
	check(NodeSpawner != nullptr);

	auto CustomizeMessageNodeLambda = [](UEdGraphNode* NewNode, bool bIsTemplateNode, TWeakObjectPtr<UFunction> FunctionPtr)
	{
		UK2Node_Message* MessageNode = CastChecked<UK2Node_Message>(NewNode);
		if (FunctionPtr.IsValid())
		{
			UFunction* Function = FunctionPtr.Get();
			MessageNode->FunctionReference.SetExternalMember(Function->GetFName(), Cast<UClass>(Function->GetOuter()));
		}
	};

	TWeakObjectPtr<UFunction> FunctionPtr = InterfaceFunction;
	NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(CustomizeMessageNodeLambda, FunctionPtr);

	return NodeSpawner;
}

//------------------------------------------------------------------------------
static UBlueprintNodeSpawner* FBlueprintNodeSpawnerFactory::MakeCommentNodeSpawner()
{
	UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(UEdGraphNode_Comment::StaticClass());
	check(NodeSpawner != nullptr);

	auto CustomizeMessageNodeLambda = [](UEdGraphNode* NewNode, bool bIsTemplateNode)
	{
		UEdGraphNode_Comment* CommentNode = CastChecked<UEdGraphNode_Comment>(NewNode);

		UEdGraph* OuterGraph = CommentNode->GetGraph();
		check(OuterGraph != nullptr);
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(OuterGraph);
		check(Blueprint != nullptr);

		FSlateRect Bounds;
		FKismetEditorUtilities::GetBoundsForSelectedNodes(Blueprint, Bounds, 50.0f);
		CommentNode->SetBounds(Bounds);
	};

	NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(CustomizeMessageNodeLambda);

	return NodeSpawner;
}

/*******************************************************************************
 * Static FBlueprintActionDatabase Helpers
 ******************************************************************************/

namespace FBlueprintActionDatabaseImpl
{
	/** 
	 * Wrapper around FBlueprintActionDatabase's FActionList. Manages the 
	 * RF_RootSet flag for the UBlueprintNodeSpawners (ensures they don't get 
	 * GC'd whilst in the database.
	 */
	struct FActionList
	{
	public:
		/** Clears the passed action-list and removes the RF_RootSet from all of its actions */
		FActionList(FBlueprintActionDatabase::FActionList& DatabaseIn)
			: ClassDatabase(DatabaseIn)
		{
			for (UBlueprintNodeSpawner* Action : ClassDatabase)
			{
				check(Action->GetOuter() == GetTransientPackage());
				Action->ClearFlags(RF_RootSet);
			}
			ClassDatabase.Empty();
		}

		/** 
		 * Passes the wrapped ClassDatabase this node-spawner to add, after 
		 * adding the RF_RootSet flag (to keep it from getting GC'd).
		 *
		 * @param  NodeSpawner	The action you want added to the database.
		 */
		void Add(UBlueprintNodeSpawner* NodeSpawner);

	private:
		/** */
		FBlueprintActionDatabase::FActionList& ClassDatabase;
	};

	/**
	 * Mimics UEdGraphSchema_K2::CanUserKismetAccessVariable(); however, this 
	 * omits the filtering that CanUserKismetAccessVariable() does (saves that 
	 * for later with FBlueprintActionFilter).
	 * 
	 * @param  Property		The property you want to check.
	 * @return True if the property can be seen from a blueprint.
	 */
	static bool IsPropertyBlueprintVisible(UProperty const* const Property);

	/**
	 * Loops over all of the class's functions and creates a node-spawners for
	 * any that are viable for blueprint use. Evolved from 
	 * FK2ActionMenuBuilder::GetFuncNodesForClass(), plus a series of other
	 * FK2ActionMenuBuilder methods (GetAllInterfaceMessageActions,
	 * GetEventsForBlueprint, etc). 
	 *
	 * Ideally, any node that is constructed from a UFunction should go in here 
	 * (so we only ever loop through the class's functions once). We handle
	 * UK2Node_CallFunction alongside UK2Node_Event.
	 *
	 * @param  Class			The class whose functions you want node-spawners for.
	 * @param  ActionListOut	The list you want populated with the new spawners.
	 */
	static void AddClassFunctionActions(UClass const* const Class, FActionList& ActionListOut);

	/**
	 * Loops over all of the class's properties and creates node-spawners for 
	 * any that are viable for blueprint use. Evolved from certain parts of 
	 * FK2ActionMenuBuilder::GetAllActionsForClass().
	 *
	 * @param  Class			The class whose properties you want node-spawners for.
	 * @param  ActionListOut	The list you want populated with the new spawners.
	 */
	static void AddClassPropertyActions(UClass const* const Class, FActionList& ActionListOut);

	/**
	 * Evolved from FClassDynamicCastHelper::GetClassDynamicCastNodes(). If the
	 * specified class is a viable blueprint variable type, then two cast nodes
	 * are added for it (UK2Node_DynamicCast, and UK2Node_ClassDynamicCast).
	 *
	 * @param  Class			The class who you want cast nodes for (they cast to this class).
	 * @param  ActionListOut	The list you want populated with the new spawners.
	 */
	static void AddClassCastActions(UClass* const Class, FActionList& ActionListOut);

	/**
	 * Evolved from K2ActionMenuBuilder's GetAddComponentClasses(). If the
	 * specified class is a component type (and can be spawned), then a 
	 * UK2Node_AddComponent spawner is created and added to ActionListOut.
	 *
	 * @param  Class			The class who you want a spawner for (the component class).
	 * @param  ActionListOut	The list you want populated with the new spawner.
	 */
	static void AddComponentClassActions(UClass* const Class, FActionList& ActionListOut);

	/**
	 * Loops over the class's enums and creates node-spawners for any that are 
	 * viable for blueprint use. Evolved from K2ActionMenuBuilder's 
	 * GetEnumUtilitiesNodes() (as well as snippets from GetSwitchMenuItems).
	 *
	 * NOTE: This only accounts for enums that belong to this specific class, 
	 *       not autonomous globally-scoped enums (ones with a UPackage outer). 
	 *       Those enums should instead be accounted for in the appropriate 
	 *       node's GetMenuActions(). For example, UK2Node_EnumLiteral should
	 *       add UK2Node_EnumLiteral spawners for any global enums.
	 *
	 * @param  Class			The class whose enums you want node-spawners for.
	 * @param  ActionListOut	The list you want populated with new spawners.
	 */
	static void AddClassEnumActions(UClass const* const Class, FActionList& ActionListOut);

	/**
	 * Loops over the class's structs and creates node-spawners for any that are 
	 * viable for blueprint use. Evolved from 
	 * FK2ActionMenuBuilder::GetStructActions().
	 *
	 * NOTE: This only captures structs that belong to this specific class, 
	 *       not autonomous globally-scoped ones (those with a UPackage outer). 
	 *       Those struct should instead be accounted for in the appropriate 
	 *       node's GetMenuActions(). For example, UK2Node_MakeStruct should
	 *       add UK2Node_MakeStruct spawners for any global structs.
	 *
	 * @param  Class			The class whose struct you want node-spawners for.
	 * @param  ActionListOut	The list you want populated with new spawners.
	 */
	static void AddClassStructActions(UClass const* const Class, FActionList& ActionListOut);

	/**
	 * If the associated class is a blueprint generated class, then this will
	 * loop over the blueprint's graphs and create any node-spawners associated
	 * with those graphs (like UK2Node_MacroInstance spawners for macro graphs).
	 *
	 * @param  Class			The class which you want graph associated node-spawners for.
	 * @param  ActionListOut	The list you want populated with new spawners.
	 */
	static void AddBlueprintGraphActions(UClass const* const Class, FActionList& ActionListOut);

	/**
	 * Emulates UEdGraphSchema::GetGraphContextActions(). If the supplied class  
	 * is a node type, then it will query the node's CDO for any actions it 
	 * wishes to add. This helps us keep the code in this file paired down, and 
	 * makes it easily extensible for new node types. At the high level, this is 
	 * for node types that aren't associated with an other class other, 
	 * "autonomous" nodes that don't have any other way of being listed (this is
	 * where enum/struct nodes missed by AddClassEnumActions()/AddClassStructActions()
	 * would be added).
	 * 
	 * @param  Class			The class which you want node-spawners for.
	 * @param  ActionListOut	The list you want populated with new spawners.
	 */
	static void AddAutonomousNodeActions(UClass* const Class, FActionList& ActionListOut);
}

//------------------------------------------------------------------------------
void FBlueprintActionDatabaseImpl::FActionList::Add(UBlueprintNodeSpawner* NodeSpawner)
{
	check(NodeSpawner != nullptr);
	check(NodeSpawner->GetOuter() == GetTransientPackage());
	// since this spawner's outer is the transient package, we want to mark it
	// root so that it doesn't get GC'd (we have to be careful and remove this 
	// flag when we refresh).
	NodeSpawner->SetFlags(RF_RootSet);

	ClassDatabase.Add(NodeSpawner);
}

//------------------------------------------------------------------------------
static bool FBlueprintActionDatabaseImpl::IsPropertyBlueprintVisible(UProperty const* const Property)
{
	bool const bIsAccessible = Property->HasAllPropertyFlags(CPF_BlueprintVisible);

	bool const bIsDelegate = Property->IsA(UMulticastDelegateProperty::StaticClass());
	bool const bIsAssignableOrCallable = Property->HasAnyPropertyFlags(CPF_BlueprintAssignable | CPF_BlueprintCallable);

	return !Property->HasAnyPropertyFlags(CPF_Parm) && (bIsAccessible || (bIsDelegate && bIsAssignableOrCallable));
}

//------------------------------------------------------------------------------
static void FBlueprintActionDatabaseImpl::AddClassFunctionActions(UClass const* const Class, FActionList& ActionListOut)
{
	using namespace FBlueprintNodeSpawnerFactory; // for MakeMessageNodeSpawner()
	check(Class != nullptr);

	// loop over all the functions in the specified class; exclude-super because 
	// we can always get the super functions by looking up that class separately 
	for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIteratorFlags::ExcludeSuper); FunctionIt; ++FunctionIt)
	{
		UFunction* Function = *FunctionIt;

		if (UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function))
		{
			if (UBlueprintEventNodeSpawner* NodeSpawner = UBlueprintEventNodeSpawner::Create(Function))
			{
				ActionListOut.Add(NodeSpawner);
			}
		}
		
		if (UEdGraphSchema_K2::CanUserKismetCallFunction(Function))
		{
			if (UBlueprintFunctionNodeSpawner* NodeSpawner = UBlueprintFunctionNodeSpawner::Create(Function))
			{
				ActionListOut.Add(NodeSpawner);
			}

			if (FKismetEditorUtilities::IsClassABlueprintInterface(Class))
			{
				ActionListOut.Add(MakeMessageNodeSpawner(Function));
			}
		}
	}
}

//------------------------------------------------------------------------------
static void FBlueprintActionDatabaseImpl::AddClassPropertyActions(UClass const* const Class, FActionList& ActionListOut)
{
	using namespace FBlueprintNodeSpawnerFactory; // for MakeDelegateNodeSpawner()
	
	// loop over all the properties in the specified class; exclude-super because 
	// we can always get the super properties by looking up that class separately 
	for (TFieldIterator<UProperty> PropertyIt(Class, EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt)
	{
		UProperty* Property = *PropertyIt;
		if (!IsPropertyBlueprintVisible(Property))
		{
			continue;
		}

 		bool bIsDelegate = Property->IsA(UMulticastDelegateProperty::StaticClass());
 		if (bIsDelegate)
 		{
			UMulticastDelegateProperty* DelegateProperty = CastChecked<UMulticastDelegateProperty>(Property);
			if (DelegateProperty->HasAnyPropertyFlags(CPF_BlueprintAssignable))
			{
				UBlueprintNodeSpawner* AddSpawner = UBlueprintPropertyNodeSpawner::Create<UK2Node_AddDelegate>(DelegateProperty);
				ActionListOut.Add(AddSpawner);
				
				// @TODO: account for: GetEventDispatcherNodesForClass() - FEdGraphSchemaAction_K2AssignDelegate
			}
			
			if (DelegateProperty->HasAnyPropertyFlags(CPF_BlueprintCallable))
			{
				UBlueprintNodeSpawner* CallSpawner = UBlueprintPropertyNodeSpawner::Create<UK2Node_CallDelegate>(DelegateProperty);
				ActionListOut.Add(CallSpawner);
			}
			
			UBlueprintNodeSpawner* RemoveSpawner = UBlueprintPropertyNodeSpawner::Create<UK2Node_RemoveDelegate>(DelegateProperty);
			ActionListOut.Add(RemoveSpawner);
			UBlueprintNodeSpawner* ClearSpawner  = UBlueprintPropertyNodeSpawner::Create<UK2Node_ClearDelegate>(DelegateProperty);
			ActionListOut.Add(ClearSpawner);

			// @TODO: AddBoundEventActionsForClass()
			//   UK2Node_ComponentBoundEvent 
			//   UK2Node_ActorBoundEvent
 		}
		else
		{
			
			UBlueprintPropertyNodeSpawner* GetterSpawner = UBlueprintPropertyNodeSpawner::Create<UK2Node_VariableGet>(Property);
			ActionListOut.Add(GetterSpawner);
			UBlueprintPropertyNodeSpawner* SetterSpawner = UBlueprintPropertyNodeSpawner::Create<UK2Node_VariableSet>(Property);
			ActionListOut.Add(SetterSpawner);
		}
	}

	// @TODO: if blueprint class, loop over function graphs and get local variables
}

//------------------------------------------------------------------------------
static void FBlueprintActionDatabaseImpl::AddClassCastActions(UClass* const Class, FActionList& ActionListOut)
{
	UEdGraphSchema_K2 const* K2Schema = GetDefault<UEdGraphSchema_K2>();
	bool bIsCastPermitted  = UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Class);

	if (bIsCastPermitted)
	{
		auto CustomizeCastNodeLambda = [](UEdGraphNode* NewNode, bool bIsTemplateNode, UClass* TargetType)
		{
			UK2Node_DynamicCast* CastNode = CastChecked<UK2Node_DynamicCast>(NewNode);
			CastNode->TargetType = TargetType;
		};

		UBlueprintNodeSpawner* CastObjNodeSpawner = UBlueprintNodeSpawner::Create(UK2Node_DynamicCast::StaticClass());
		CastObjNodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(CustomizeCastNodeLambda, Class);
		ActionListOut.Add(CastObjNodeSpawner);

		UBlueprintNodeSpawner* CastClassNodeSpawner = UBlueprintNodeSpawner::Create(UK2Node_ClassDynamicCast::StaticClass());
		CastClassNodeSpawner->CustomizeNodeDelegate = CastObjNodeSpawner->CustomizeNodeDelegate;
		ActionListOut.Add(CastClassNodeSpawner);
	}
}

//------------------------------------------------------------------------------
static void FBlueprintActionDatabaseImpl::AddComponentClassActions(UClass* const Class, FActionList& ActionListOut)
{
	bool bIsSpawnable = !Class->HasAnyClassFlags(CLASS_Abstract) && Class->HasMetaData(FBlueprintMetadata::MD_BlueprintSpawnableComponent);

	if (bIsSpawnable && Class->IsChildOf(UActorComponent::StaticClass()))
	{
		if (UBlueprintComponentNodeSpawner* NodeSpawner = UBlueprintComponentNodeSpawner::Create(Class))
		{
			ActionListOut.Add(NodeSpawner);
		}
	}
}

//------------------------------------------------------------------------------
static void FBlueprintActionDatabaseImpl::AddClassEnumActions(UClass const* const Class, FActionList& ActionListOut)
{
	using namespace FBlueprintNodeSpawnerFactory; // for MakeEnumNodeSpawner()

	for (TFieldIterator<UEnum> EnumIt(Class, EFieldIteratorFlags::ExcludeSuper); EnumIt; ++EnumIt)
	{
		UEnum* Enum = *EnumIt;
		if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Enum))
		{
			continue;
		}

		ActionListOut.Add(MakeEnumNodeSpawner<UK2Node_GetNumEnumEntries>(Enum));
		ActionListOut.Add(MakeEnumNodeSpawner<UK2Node_ForEachElementInEnum>(Enum));
		ActionListOut.Add(MakeEnumNodeSpawner<UK2Node_EnumLiteral>(Enum));

		auto SetupEnumByteCastLambda = [](UK2Node_CastByteToEnum* NewNode)
		{
			NewNode->bSafe = true;
		};
		ActionListOut.Add(MakeEnumNodeSpawner<UK2Node_CastByteToEnum>(Enum, SetupEnumByteCastLambda));


		auto SetupEnumSwitchLambda = [](UK2Node_SwitchEnum* NewNode)
		{
			// the node's 'Enum' field should have already been setup generically by MakeEnumNodeSpawner()
			check(NewNode->Enum != nullptr);
			NewNode->SetEnum(NewNode->Enum); // SetEnum() does more setup work than just setting the field
		};
		ActionListOut.Add(MakeEnumNodeSpawner<UK2Node_SwitchEnum>(Enum, SetupEnumSwitchLambda));
	}

	// @TODO: what about enum assets? how do we detect newly added ones and refresh those?
	//    FEditorDelegates::LoadSelectedAssetsIfNeeded.Broadcast();
}

//------------------------------------------------------------------------------
static void FBlueprintActionDatabaseImpl::AddClassStructActions(UClass const* const Class, FActionList& ActionListOut)
{
	using namespace FBlueprintNodeSpawnerFactory; // for MakeStructNodeSpawner()

	for (TFieldIterator<UScriptStruct> StructIt(Class, EFieldIteratorFlags::ExcludeSuper); StructIt; ++StructIt)
	{
		UScriptStruct* Struct = (*StructIt);
		if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Struct))
		{
			continue;
		}

		if (UK2Node_BreakStruct::CanBeBroken(Struct))
		{
			ActionListOut.Add(MakeStructNodeSpawner<UK2Node_BreakStruct>(Struct));
		}

		if (UK2Node_MakeStruct::CanBeMade(Struct))
		{
			ActionListOut.Add(MakeStructNodeSpawner<UK2Node_MakeStruct>(Struct));
			ActionListOut.Add(MakeStructNodeSpawner<UK2Node_SetFieldsInStruct>(Struct));
		}
	}

	// @TODO: what about struct assets? how do we detect newly added ones and refresh those?
	//    FEditorDelegates::LoadSelectedAssetsIfNeeded.Broadcast();
}

//------------------------------------------------------------------------------
static void FBlueprintActionDatabaseImpl::AddBlueprintGraphActions(UClass const* const Class, FActionList& ActionListOut)
{
	using namespace FBlueprintNodeSpawnerFactory; // for MakeMacroNodeSpawner()

	if (UBlueprint const* const Blueprint = Cast<UBlueprint const>(Class->ClassGeneratedBy))
	{
		for (auto GraphIt = Blueprint->MacroGraphs.CreateConstIterator(); GraphIt; ++GraphIt)
		{
			ActionListOut.Add(MakeMacroNodeSpawner(*GraphIt));
		}

		// @TODO: local variables
// 		for (auto GraphIt = Blueprint->FunctionGraphs.CreateConstIterator(); GraphIt; ++GraphIt)
// 		{
// 			UEdGraph* FunctionGraph = (*GraphIt);
// 
// 			TArray<UK2Node_FunctionEntry*> GraphEntryNodes;
// 			FunctionGraph->GetNodesOfClass<UK2Node_FunctionEntry>(GraphEntryNodes);
// 
// 			for (UK2Node_FunctionEntry* FunctionEntry : GraphEntryNodes)
// 			{
// 				for (FBPVariableDescription const& LocalVar : FunctionEntry->LocalVariables)
// 				{
// 					ActionListOut.Add(MakeLocalVarNodeSpawner(LocalVar, VT_Getter));
// 				}
// 			}
// 		}
	}	
}

//------------------------------------------------------------------------------
static void FBlueprintActionDatabaseImpl::AddAutonomousNodeActions(UClass* const Class, FActionList& ActionListOut)
{
	using namespace FBlueprintNodeSpawnerFactory; // for MakeCommentNodeSpawner()

	if (Class->IsChildOf(UK2Node::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract))
	{
		UK2Node const* const NodeCDO = Class->GetDefaultObject<UK2Node>();
		check(NodeCDO != nullptr);

		FBlueprintActionDatabase::FActionList NodeActionList;
		NodeCDO->GetMenuActions(NodeActionList);

		for (UBlueprintNodeSpawner* Spawner : NodeActionList)
		{
			ActionListOut.Add(Spawner);
		}
	}
	// unfortunately, UEdGraphNode_Comment is not a UK2Node and therefore cannot
	// leverage UK2Node's GetMenuActions() function, so here we HACK it in
	//
	// @TODO: DO NOT follow this example! Do as I say, not as I do! If we need
	//        to support other nodes in a similar way, then we should come up
	//        with a better (more generalized) solution.
	else if (Class == UEdGraphNode_Comment::StaticClass())
	{
		ActionListOut.Add(MakeCommentNodeSpawner());
	}
}

/*******************************************************************************
 * FBlueprintActionDatabase
 ******************************************************************************/

//------------------------------------------------------------------------------
FBlueprintActionDatabase& FBlueprintActionDatabase::Get()
{
	static FBlueprintActionDatabase DatabaseInst;
	if (DatabaseInst.ClassActions.Num() == 0)
	{
		// prime the database the first time we access it
		DatabaseInst.RefreshAll();
	}

	return DatabaseInst;
}

//------------------------------------------------------------------------------
void FBlueprintActionDatabase::RefreshAll()
{
	ClassActions.Empty();
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* const Class = (*ClassIt);
		RefreshClassActions(Class);
	}
}

//------------------------------------------------------------------------------
void FBlueprintActionDatabase::RefreshClassActions(UClass* const Class)
{
	using namespace FBlueprintActionDatabaseImpl;

	check(Class != nullptr);
	bool const bIsSkelClass   = FKismetEditorUtilities::IsClassABlueprintSkeleton(Class);
	bool const bIsReinstClass = Class->HasAnyClassFlags(CLASS_NewerVersionExists);
	
	// make sure this class is not an intermediate that was part of some
	// blueprint compile (if it is a SKEL or REINST class, then don't bother)
	if (!bIsSkelClass && !bIsReinstClass)
	{
		FBlueprintActionDatabaseImpl::FActionList ClassActionList(ClassActions.FindOrAdd(Class));
		
		// class field actions (nodes that represent and perform actions on
		// specific fields of the class... functions, properties, etc.)
		{
			AddClassFunctionActions(Class, ClassActionList);
			AddClassPropertyActions(Class, ClassActionList);
			AddClassEnumActions(Class, ClassActionList);
			AddClassStructActions(Class, ClassActionList);
		}

		AddClassCastActions(Class, ClassActionList);
		AddBlueprintGraphActions(Class, ClassActionList);
		AddComponentClassActions(Class, ClassActionList);
		
		// accounts for the "autonomous" standalone nodes that can't be strongly
		// associated with a particular class (besides the node's class)...
		// think things like: comment nodes, custom events, the self node, etc.
		//
		// also should catch any actions dealing with global UFields (like
		// global structs, enums, etc.; elements that wouldn't be caught
		// normally when sifting through fields on all known classes)
		AddAutonomousNodeActions(Class, ClassActionList);
	}

	// @TODO: account for all K2ActionMenuBuilder mathods...
	//   GetLiteralsFromActorSelection() - UK2Node_Literal
	//   GetAnimNotifyMenuItems()
	//   GetMatineeControllers() - UK2Node_MatineeController
	//   GetEventDispatcherNodesForClass()
	//   GetBoundEventsFromActorSelection() - handle with filter
	//   GetFunctionCallsOnSelectedActors() - handle with filter
	//   GetAddComponentActionsUsingSelectedAssets() 
	//   GetFunctionCallsOnSelectedComponents() - handle with filter
	//   GetBoundEventsFromComponentSelection() - handle with filter 
	//   GetPinAllowedMacros()
	//   GetFuncNodesWithPinType()
	//   GetVariableGettersSettersForClass()
}

//------------------------------------------------------------------------------
FBlueprintActionDatabase::FClassActionMap const& FBlueprintActionDatabase::GetAllActions()
{
	// if this is the first time that we're querying for actions, generate the
	// list before returning it
	if (ClassActions.Num() == 0)
	{
		RefreshAll();
	}
	return ClassActions;
}