// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "BlueprintCompilerCppBackendModulePrivatePCH.h"
#include "BlueprintCompilerCppBackendUtils.h"
#include "Editor/UnrealEd/Public/Kismet2/StructureEditorUtils.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/DynamicBlueprintBinding.h"

void FEmitDefaultValueHelper::OuterGenerate(FEmitterLocalContext& Context
	, const UProperty* Property
	, const FString& OuterPath
	, const uint8* DataContainer
	, const uint8* OptionalDefaultDataContainer
	, EPropertyAccessOperator AccessOperator
	, bool bAllowProtected)
{
	// Determine if the given property contains an instanced default subobject reference. We only get here if the values are not identical.
	auto IsInstancedSubobjectLambda = [&](int32 ArrayIndex) -> bool
	{
		if (auto ObjectProperty = Cast<UObjectProperty>(Property))
		{
			check(DataContainer);
			check(OptionalDefaultDataContainer);

			auto ObjectPropertyValue = ObjectProperty->GetObjectPropertyValue_InContainer(DataContainer, ArrayIndex);
			auto DefaultObjectPropertyValue = ObjectProperty->GetObjectPropertyValue_InContainer(OptionalDefaultDataContainer, ArrayIndex);
			if (ObjectPropertyValue && ObjectPropertyValue->IsDefaultSubobject() && DefaultObjectPropertyValue && DefaultObjectPropertyValue->IsDefaultSubobject() && ObjectPropertyValue->GetFName() == DefaultObjectPropertyValue->GetFName())
			{
				return true;
			}
		}

		return false;
	};

	if (Property->HasAnyPropertyFlags(CPF_EditorOnly | CPF_Transient))
	{
		UE_LOG(LogK2Compiler, Verbose, TEXT("FEmitDefaultValueHelper Skip EditorOnly or Transient property: %s"), *Property->GetPathName());
		return;
	}

	for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
	{
		if (!OptionalDefaultDataContainer
			|| (!Property->Identical_InContainer(DataContainer, OptionalDefaultDataContainer, ArrayIndex) && !IsInstancedSubobjectLambda(ArrayIndex)))
		{
			FString PathToMember;
			auto PropertyOwnerAsBPGC = Cast<UBlueprintGeneratedClass>(Property->GetOwnerClass());
			if (PropertyOwnerAsBPGC && !Context.Dependencies.WillClassBeConverted(PropertyOwnerAsBPGC))
			{
				ensure(EPropertyAccessOperator::None != AccessOperator);
				const FString OperatorStr = (EPropertyAccessOperator::Dot == AccessOperator) ? TEXT("&") : TEXT("");
				const FString ContainerStr = (EPropertyAccessOperator::None == AccessOperator) ? TEXT("this") : FString::Printf(TEXT("%s(%s)"), *OperatorStr, *OuterPath);

				PathToMember = FString::Printf(TEXT("FUnconvertedWrapper__%s(%s).GetRef__%s()"), *FEmitHelper::GetCppName(PropertyOwnerAsBPGC), *ContainerStr
					, *UnicodeToCPPIdentifier(Property->GetName(), false, nullptr));
			}
			else if (Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate) || (!bAllowProtected && Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierProtected)))
			{
				ensure(EPropertyAccessOperator::None != AccessOperator);
				const FString OperatorStr = (EPropertyAccessOperator::Dot == AccessOperator) ? TEXT("&") : TEXT("");
				const FString ContainerStr = (EPropertyAccessOperator::None == AccessOperator) ? TEXT("this") : OuterPath;
				const FString StaticArrayIndexArg = FString::Printf(TEXT(", %d"), ArrayIndex);
				const FString GetPtrStr = FEmitHelper::AccessInaccessibleProperty(Context, Property, ContainerStr, OperatorStr, StaticArrayIndexArg);
				PathToMember = Context.GenerateUniqueLocalName();
				Context.AddLine(FString::Printf(TEXT("auto& %s = %s;"), *PathToMember, *GetPtrStr));
			}
			else
			{
				const FString AccessOperatorStr = (EPropertyAccessOperator::None == AccessOperator) ? TEXT("")
					: ((EPropertyAccessOperator::Pointer == AccessOperator) ? TEXT("->") : TEXT("."));
				const bool bStaticArray = (Property->ArrayDim > 1);
				const FString ArrayPost = bStaticArray ? FString::Printf(TEXT("[%d]"), ArrayIndex) : TEXT("");
				PathToMember = FString::Printf(TEXT("%s%s%s%s"), *OuterPath, *AccessOperatorStr, *FEmitHelper::GetCppName(Property), *ArrayPost);
			}
			const uint8* ValuePtr = Property->ContainerPtrToValuePtr<uint8>(DataContainer, ArrayIndex);
			const uint8* DefaultValuePtr = OptionalDefaultDataContainer ? Property->ContainerPtrToValuePtr<uint8>(OptionalDefaultDataContainer, ArrayIndex) : nullptr;
			InnerGenerate(Context, Property, PathToMember, ValuePtr, DefaultValuePtr);
		}
	}
}

void FEmitDefaultValueHelper::GenerateGetDefaultValue(const UUserDefinedStruct* Struct, FEmitterLocalContext& Context)
{
	check(Struct);
	const FString StructName = FEmitHelper::GetCppName(Struct);
	Context.Header.AddLine(FString::Printf(TEXT("static %s GetDefaultValue()"), *StructName));
	Context.Header.AddLine(TEXT("{"));

	Context.Header.IncreaseIndent();
	Context.Header.AddLine(FString::Printf(TEXT("%s DefaultData__;"), *StructName));
	{
		TGuardValue<FCodeText*> OriginalDefaultTarget(Context.DefaultTarget, &Context.Header);
		FStructOnScope StructData(Struct);
		FStructureEditorUtils::Fill_MakeStructureDefaultValue(Struct, StructData.GetStructMemory());
		for (auto Property : TFieldRange<const UProperty>(Struct))
		{
			OuterGenerate(Context, Property, TEXT("DefaultData__"), StructData.GetStructMemory(), nullptr, EPropertyAccessOperator::Dot);
		}


	}
	Context.Header.AddLine(TEXT("return DefaultData__;"));
	Context.Header.DecreaseIndent();

	Context.Header.AddLine(TEXT("}"));
}

void FEmitDefaultValueHelper::InnerGenerate(FEmitterLocalContext& Context, const UProperty* Property, const FString& PathToMember, const uint8* ValuePtr, const uint8* DefaultValuePtr, bool bWithoutFirstConstructionLine)
{
	auto OneLineConstruction = [](FEmitterLocalContext& LocalContext, const UProperty* LocalProperty, const uint8* LocalValuePtr, FString& OutSingleLine, bool bGenerateEmptyStructConstructor) -> bool
	{
		bool bComplete = true;
		FString ValueStr = HandleSpecialTypes(LocalContext, LocalProperty, LocalValuePtr);
		if (ValueStr.IsEmpty())
		{
			ValueStr = LocalContext.ExportTextItem(LocalProperty, LocalValuePtr);
			auto StructProperty = Cast<const UStructProperty>(LocalProperty);
			if (ValueStr.IsEmpty() && StructProperty)
			{
				check(StructProperty->Struct);
				if (bGenerateEmptyStructConstructor)
				{
					ValueStr = FString::Printf(TEXT("%s{}"), *FEmitHelper::GetCppName(StructProperty->Struct)); //don;t override existing values
				}
				bComplete = false;
			}
			else if (ValueStr.IsEmpty())
			{
				UE_LOG(LogK2Compiler, Error, TEXT("FEmitDefaultValueHelper Cannot generate initilization: %s"), *LocalProperty->GetPathName());
			}
		}
		OutSingleLine += ValueStr;
		return bComplete;
	};

	auto StructProperty = Cast<const UStructProperty>(Property);
	check(!StructProperty || StructProperty->Struct);
	auto ArrayProperty = Cast<const UArrayProperty>(Property);
	check(!ArrayProperty || ArrayProperty->Inner);

	if (!bWithoutFirstConstructionLine)
	{
		FString ValueStr;
		const bool bComplete = OneLineConstruction(Context, Property, ValuePtr, ValueStr, false);
		if (!ValueStr.IsEmpty())
		{
			Context.AddLine(FString::Printf(TEXT("%s = %s;"), *PathToMember, *ValueStr));
		}
		// array initialization "array_var = TArray<..>()" is complete, but it still needs items.
		if (bComplete && !ArrayProperty)
		{
			return;
		}
	}

	if (StructProperty)
	{
		for (auto LocalProperty : TFieldRange<const UProperty>(StructProperty->Struct))
		{
			OuterGenerate(Context, LocalProperty, PathToMember, ValuePtr, DefaultValuePtr, EPropertyAccessOperator::Dot);
		}
	}
	
	if (ArrayProperty)
	{
		FScriptArrayHelper ScriptArrayHelper(ArrayProperty, ValuePtr);
		for (int32 Index = 0; Index < ScriptArrayHelper.Num(); ++Index)
		{
			const uint8* LocalValuePtr = ScriptArrayHelper.GetRawPtr(Index);

			FString ValueStr;
			const bool bComplete = OneLineConstruction(Context, ArrayProperty->Inner, LocalValuePtr, ValueStr, true);
			Context.AddLine(FString::Printf(TEXT("%s.Add(%s);"), *PathToMember, *ValueStr));
			if (!bComplete)
			{
				const FString LocalPathToMember = FString::Printf(TEXT("%s[%d]"), *PathToMember, Index);

				// There is no point in doing diff with a "clean" struct, since we don't know what is really set by the native constructor.
				InnerGenerate(Context, ArrayProperty->Inner, LocalPathToMember, LocalValuePtr, nullptr, true);
			}
		}
	}
}

FString FEmitDefaultValueHelper::HandleSpecialTypes(FEmitterLocalContext& Context, const UProperty* Property, const uint8* ValuePtr)
{
	//TODO: Use Path maps for Objects
	if (auto ObjectProperty = Cast<UObjectProperty>(Property))
	{
		UObject* Object = ObjectProperty->GetPropertyValue(ValuePtr);
		if (Object)
		{
			{
				UClass* ObjectClassToUse = Context.GetFirstNativeOrConvertedClass(ObjectProperty->PropertyClass);
				const FString MappedObject = Context.FindGloballyMappedObject(Object, ObjectClassToUse);
				if (!MappedObject.IsEmpty())
				{
					return MappedObject;
				}
			}

			const bool bCreatingSubObjectsOfClass = (Context.CurrentCodeType == FEmitterLocalContext::EGeneratedCodeType::SubobjectsOfClass);
			{
				auto BPGC = Context.GetCurrentlyGeneratedClass();
				auto CDO = BPGC ? BPGC->GetDefaultObject(false) : nullptr;
				if (BPGC && Object && CDO && Object->IsIn(BPGC) && !Object->IsIn(CDO) && bCreatingSubObjectsOfClass)
				{
					return HandleClassSubobject(Context, Object, FEmitterLocalContext::EClassSubobjectList::MiscConvertedSubobjects, true, true);
				}
			}

			if (!bCreatingSubObjectsOfClass && Property->HasAnyPropertyFlags(CPF_InstancedReference))
			{
				const FString CreateAsInstancedSubobject = HandleInstancedSubobject(Context, Object, Object->HasAnyFlags(RF_ArchetypeObject));
				if (!CreateAsInstancedSubobject.IsEmpty())
				{
					return CreateAsInstancedSubobject;
				}
			}
		}
		else if (ObjectProperty->HasMetaData(FBlueprintMetadata::MD_LatentCallbackTarget))
		{
			return TEXT("this");
		}
	}

	if (auto StructProperty = Cast<UStructProperty>(Property))
	{
		if (TBaseStructure<FTransform>::Get() == StructProperty->Struct)
		{
			check(ValuePtr);
			const FTransform* Transform = reinterpret_cast<const FTransform*>(ValuePtr);
			const auto Rotation = Transform->GetRotation();
			const auto Translation = Transform->GetTranslation();
			const auto Scale = Transform->GetScale3D();
			return FString::Printf(TEXT("FTransform(FQuat(%f, %f, %f, %f), FVector(%f, %f, %f), FVector(%f, %f, %f))")
				, Rotation.X, Rotation.Y, Rotation.Z, Rotation.W
				, Translation.X, Translation.Y, Translation.Z
				, Scale.X, Scale.Y, Scale.Z);
		}

		if (TBaseStructure<FVector>::Get() == StructProperty->Struct)
		{
			const FVector* Vector = reinterpret_cast<const FVector*>(ValuePtr);
			return FString::Printf(TEXT("FVector(%f, %f, %f)"), Vector->X, Vector->Y, Vector->Z);
		}

		if (TBaseStructure<FGuid>::Get() == StructProperty->Struct)
		{
			const FGuid* Guid = reinterpret_cast<const FGuid*>(ValuePtr);
			return FString::Printf(TEXT("FGuid(0x%08X, 0x%08X, 0x%08X, 0x%08X)"), Guid->A, Guid->B, Guid->C, Guid->D);
		}
	}
	return FString();
}

struct FNonativeComponentData
{
	FString NativeVariablePropertyName;
	UActorComponent* ComponentTemplate;
	UObject* ObjectToCompare;

	////
	FString ParentVariableName;
	bool bSetNativeCreationMethod;

	FNonativeComponentData()
		: ComponentTemplate(nullptr)
		, ObjectToCompare(nullptr)
		, bSetNativeCreationMethod(false)
	{
	}

	void EmitProperties(FEmitterLocalContext& Context)
	{
		ensure(!NativeVariablePropertyName.IsEmpty());
		if (bSetNativeCreationMethod)
		{
			Context.AddLine(FString::Printf(TEXT("%s->CreationMethod = EComponentCreationMethod::Native;"), *NativeVariablePropertyName));
		}

		if (!ParentVariableName.IsEmpty())
		{
			Context.AddLine(FString::Printf(TEXT("%s->AttachParent = %s;"), *NativeVariablePropertyName, *ParentVariableName));
		}

		UClass* ComponentClass = ComponentTemplate->GetClass();
		for (auto Property : TFieldRange<const UProperty>(ComponentClass))
		{
			FEmitDefaultValueHelper::OuterGenerate(Context, Property, NativeVariablePropertyName
				, reinterpret_cast<const uint8*>(ComponentTemplate)
				, reinterpret_cast<const uint8*>(ObjectToCompare)
				, FEmitDefaultValueHelper::EPropertyAccessOperator::Pointer);
		}
	}
};

FString FEmitDefaultValueHelper::HandleNonNativeComponent(FEmitterLocalContext& Context, const USCS_Node* Node, TSet<const UProperty*>& OutHandledProperties, TArray<FString>& NativeCreatedComponentProperties, const USCS_Node* ParentNode, TArray<FNonativeComponentData>& ComponenntsToInit)
{
	check(Node);
	check(Context.CurrentCodeType == FEmitterLocalContext::EGeneratedCodeType::CommonConstructor);

	FString NativeVariablePropertyName;
	UBlueprintGeneratedClass* BPGC = CastChecked<UBlueprintGeneratedClass>(Context.GetCurrentlyGeneratedClass());
	if (UActorComponent* ComponentTemplate = Node->GetActualComponentTemplate(BPGC))
	{
		const FString VariableCleanName = Node->VariableName.ToString();

		const UObjectProperty* VariableProperty = FindField<UObjectProperty>(BPGC, *VariableCleanName);
		if (VariableProperty)
		{
			NativeVariablePropertyName = FEmitHelper::GetCppName(VariableProperty);
			OutHandledProperties.Add(VariableProperty);
		}
		else
		{
			NativeVariablePropertyName = VariableCleanName;
		}

		Context.AddCommonSubObject_InConstructor(ComponentTemplate, NativeVariablePropertyName);

		if (ComponentTemplate->GetOuter() == BPGC)
		{
			FNonativeComponentData NonativeComponentData;
			NonativeComponentData.NativeVariablePropertyName = NativeVariablePropertyName;
			NonativeComponentData.ComponentTemplate = ComponentTemplate;

			UClass* ComponentClass = ComponentTemplate->GetClass();
			check(ComponentClass != nullptr);

			UObject* ObjectToCompare = ComponentClass->GetDefaultObject(false);

			if (ComponentTemplate->HasAnyFlags(RF_InheritableComponentTemplate))
			{
				ObjectToCompare = Node->GetActualComponentTemplate(Cast<UBlueprintGeneratedClass>(BPGC->GetSuperClass()));
			}
			else
			{
				Context.AddLine(FString::Printf(TEXT("%s%s = CreateDefaultSubobject<%s>(TEXT(\"%s\"));")
					, (VariableProperty == nullptr) ? TEXT("auto ") : TEXT("")
					, *NativeVariablePropertyName
					, *FEmitHelper::GetCppName(ComponentClass)
					, *VariableCleanName));

				NonativeComponentData.bSetNativeCreationMethod = true;
				NativeCreatedComponentProperties.Add(NativeVariablePropertyName);

				FString ParentVariableName;
				if (ParentNode)
				{
					const FString CleanParentVariableName = ParentNode->VariableName.ToString();
					const UObjectProperty* ParentVariableProperty = FindField<UObjectProperty>(BPGC, *CleanParentVariableName);
					ParentVariableName = ParentVariableProperty ? FEmitHelper::GetCppName(ParentVariableProperty) : CleanParentVariableName;
				}
				else if (USceneComponent* ParentComponentTemplate = Node->GetParentComponentTemplate(CastChecked<UBlueprint>(BPGC->ClassGeneratedBy)))
				{
					ParentVariableName = Context.FindGloballyMappedObject(ParentComponentTemplate, USceneComponent::StaticClass());
				}
				NonativeComponentData.ParentVariableName = ParentVariableName;
			}
			NonativeComponentData.ObjectToCompare = ObjectToCompare;
			ComponenntsToInit.Add(NonativeComponentData);
		}
	}

	// Recursively handle child nodes.
	for (auto ChildNode : Node->ChildNodes)
	{
		HandleNonNativeComponent(Context, ChildNode, OutHandledProperties, NativeCreatedComponentProperties, Node, ComponenntsToInit);
	}

	return NativeVariablePropertyName;
}

struct FDependenciesHelper
{
private:
	// Keep sync with FTypeSingletonCache::GenerateSingletonName
	static FString GenerateZConstructor(UField* Item)
	{
		FString Result;
		if (!ensure(Item))
		{
			return Result;
		}
		
		for (UObject* Outer = Item; Outer; Outer = Outer->GetOuter())
		{
			if (!Result.IsEmpty())
			{
				Result = TEXT("_") + Result;
			}

			if (Cast<UClass>(Outer) || Cast<UScriptStruct>(Outer))
			{
				FString OuterName = FEmitHelper::GetCppName(CastChecked<UField>(Outer), true);
				Result = OuterName + Result;

				// Structs can also have UPackage outer.
				if (Cast<UClass>(Outer) || Cast<UPackage>(Outer->GetOuter()))
				{
					break;
				}
			}
			else
			{
				Result = Outer->GetName() + Result;
			}
		}

		// Can't use long package names in function names.
		if (Result.StartsWith(TEXT("/Script/"), ESearchCase::CaseSensitive))
		{
			Result = FPackageName::GetShortName(Result);
		}

		const FString ClassString = Item->IsA<UClass>() ? TEXT("UClass") : TEXT("UScriptStruct");
		const FString PostFix = Item->IsA<UClass>() ? TEXT("_NoRegister") : TEXT("");
		return FString(TEXT("Z_Construct_")) + ClassString + TEXT("_") + Result + PostFix + TEXT("()");
	}
public:
	static void AddDependenciesInConstructor(FEmitterLocalContext& Context)
	{
		const bool bUseZConstructorInGeneratedCode = true;
		if (Context.Dependencies.ConvertedClasses.Num())
		{
			Context.AddLine(TEXT("// List of all referenced converted classes"));
		}
		for (auto LocStruct : Context.Dependencies.ConvertedClasses)
		{
			FString ClassConstructor;
			if (bUseZConstructorInGeneratedCode)
			{
				ClassConstructor = GenerateZConstructor(Context.Dependencies.FindOriginalClass(LocStruct));
				Context.AddLine(FString::Printf(TEXT("extern UClass* %s;"), *ClassConstructor));
			}
			else
			{
				ClassConstructor = FEmitHelper::GetCppName(LocStruct, true) + TEXT("::StaticClass()");
			}
			Context.AddLine(FString::Printf(TEXT("CastChecked<UDynamicClass>(GetClass())->ReferencedConvertedFields.Add(%s);"), *ClassConstructor));
		}

		if (Context.Dependencies.ConvertedStructs.Num())
		{
			Context.AddLine(TEXT("// List of all referenced converted structures"));
		}
		for (auto LocStruct : Context.Dependencies.ConvertedStructs)
		{
			FString StructConstructor;
			if (bUseZConstructorInGeneratedCode)
			{
				StructConstructor = GenerateZConstructor(LocStruct);
				Context.AddLine(FString::Printf(TEXT("extern UScriptStruct* %s;"), *StructConstructor));
			}
			else
			{
				StructConstructor = FEmitHelper::GetCppName(LocStruct, true) + TEXT("::StaticStruct()");
			}
			Context.AddLine(FString::Printf(TEXT("CastChecked<UDynamicClass>(GetClass())->ReferencedConvertedFields.Add(%s);"), *StructConstructor));
		}

		if (Context.Dependencies.Assets.Num())
		{
			Context.AddLine(TEXT("// List of all referenced assets"));
		}
		for (auto LocAsset : Context.Dependencies.Assets)
		{
			const FString AssetStr = Context.FindGloballyMappedObject(LocAsset, UObject::StaticClass(), true, false);
			Context.AddLine(FString::Printf(TEXT("CastChecked<UDynamicClass>(GetClass())->UsedAssets.Add(%s);"), *AssetStr));
		}
	}

	static void AddStaticFunctionsForDependencies(FEmitterLocalContext& Context)
	{
		auto SourceClass = Context.GetCurrentlyGeneratedClass();
		auto OriginalClass = Context.Dependencies.FindOriginalClass(SourceClass);
		const FString CppClassName = FEmitHelper::GetCppName(OriginalClass);
		

		// __StaticDependenciesAssets
		Context.AddLine(FString::Printf(TEXT("void %s::__StaticDependenciesAssets(TArray<FBlueprintDependencyData>& AssetsToLoad)"), *CppClassName));
		Context.AddLine(TEXT("{"));
		Context.IncreaseIndent();

		for (UObject* LocAsset : Context.Dependencies.Assets)
		{
			auto GetConvertedClass = [&](const UClass* AssetType) -> const UClass*
			{
				const UBlueprintGeneratedClass* BPGC = Cast<const UBlueprintGeneratedClass>(AssetType);
				if (AssetType->IsChildOf<UUserDefinedEnum>())
				{
					return UEnum::StaticClass();
				}
				else if (AssetType->IsChildOf<UUserDefinedStruct>())
				{
					return UScriptStruct::StaticClass();
				}
				else if (BPGC && Context.Dependencies.WillClassBeConverted(BPGC))
				{
					return UDynamicClass::StaticClass();
				}
				return AssetType;
			};
			const UClass* ClassToUse = GetConvertedClass(LocAsset->GetClass());
			Context.AddLine(FString::Printf(TEXT("AssetsToLoad.Add({FName(TEXT(\"%s\")), FName(TEXT(\"%s\")), FName(TEXT(\"%s\")), FName(TEXT(\"%s\"))});")
				, *LocAsset->GetOutermost()->GetPathName()
				, *LocAsset->GetName()
				, *ClassToUse->GetOutermost()->GetPathName()
				, *ClassToUse->GetName()));
		}

		Context.DecreaseIndent();
		Context.AddLine(TEXT("}"));

		// Register Helper Struct
		const FString RegisterHelperName = FString::Printf(TEXT("FRegisterHelper__%s"), *CppClassName);
		Context.AddLine(FString::Printf(TEXT("struct %s"), *RegisterHelperName));
		Context.AddLine(TEXT("{"));
		Context.IncreaseIndent();

		Context.AddLine(FString::Printf(TEXT("%s()"), *RegisterHelperName));
		Context.AddLine(TEXT("{"));
		Context.IncreaseIndent();

		Context.AddLine(FString::Printf(
			TEXT("FConvertedBlueprintsDependencies::Get().RegisterClass(TEXT(\"%s\"), &%s::__StaticDependenciesAssets);")
			, *OriginalClass->GetOutermost()->GetPathName()
			, *CppClassName));

		Context.DecreaseIndent();
		Context.AddLine(TEXT("}"));

		Context.AddLine(FString::Printf(TEXT("static %s Instance;"), *RegisterHelperName));

		Context.DecreaseIndent();
		Context.AddLine(TEXT("};"));

		Context.AddLine(FString::Printf(TEXT("%s %s::Instance;"), *RegisterHelperName, *RegisterHelperName));
	}
};

void FEmitDefaultValueHelper::GenerateConstructor(FEmitterLocalContext& Context)
{
	auto BPGC = CastChecked<UBlueprintGeneratedClass>(Context.GetCurrentlyGeneratedClass());
	const FString CppClassName = FEmitHelper::GetCppName(BPGC);

	UClass* SuperClass = BPGC->GetSuperClass();
	const bool bSuperHasOnlyDefaultConstructor = SuperClass && SuperClass->HasMetaData(TEXT("OnlyDefaultConstructorDeclared"));
	Context.AddLine(FString::Printf(TEXT("%s::%s(const FObjectInitializer& ObjectInitializer) : Super(%s)")
		, *CppClassName
		, *CppClassName
		, bSuperHasOnlyDefaultConstructor ? TEXT("") : TEXT("ObjectInitializer")));
	Context.AddLine(TEXT("{"));
	Context.IncreaseIndent();

	// When CDO is created create all subobjects owned by the class
	{
		TArray<UActorComponent*> ActorComponentTempatesOwnedByClass = BPGC->ComponentTemplates;
		// Gather all CT from SCS and IH, the remaining ones are generated for class..
		if (auto SCS = BPGC->SimpleConstructionScript)
		{
			for (auto Node : SCS->GetAllNodes())
			{
				ActorComponentTempatesOwnedByClass.RemoveSwap(Node->ComponentTemplate);
			}
		}
		if (auto IH = BPGC->GetInheritableComponentHandler())
		{
			TArray<UActorComponent*> AllTemplates;
			IH->GetAllTemplates(AllTemplates);
			ActorComponentTempatesOwnedByClass.RemoveAllSwap([&](UActorComponent* Component) -> bool
			{
				return AllTemplates.Contains(Component);
			});
		}

		Context.AddLine(FString::Printf(TEXT("if(HasAnyFlags(RF_ClassDefaultObject) && (%s::StaticClass() == GetClass()))"), *CppClassName));
		Context.AddLine(TEXT("{"));
		Context.IncreaseIndent();
		Context.AddLine(TEXT("ensure(0 == CastChecked<UDynamicClass>(GetClass())->MiscConvertedSubobjects.Num());"));
		Context.AddLine(TEXT("ensure(0 == CastChecked<UDynamicClass>(GetClass())->ReferencedConvertedFields.Num());"));
		Context.AddLine(TEXT("ensure(0 == CastChecked<UDynamicClass>(GetClass())->UsedAssets.Num());"));
		Context.AddLine(TEXT("ensure(0 == CastChecked<UDynamicClass>(GetClass())->DynamicBindingObjects.Num());"));
		Context.AddLine(TEXT("ensure(0 == CastChecked<UDynamicClass>(GetClass())->ComponentTemplates.Num());"));
		Context.AddLine(TEXT("ensure(0 == CastChecked<UDynamicClass>(GetClass())->Timelines.Num());"));
		Context.AddLine(TEXT("ensure(nullptr == CastChecked<UDynamicClass>(GetClass())->AnimClassImplementation);"));
		Context.CurrentCodeType = FEmitterLocalContext::EGeneratedCodeType::SubobjectsOfClass;

		FDependenciesHelper::AddDependenciesInConstructor(Context);

		auto CreateAndInitializeClassSubobjects = [&](bool bCreate, bool bInitilize)
		{
			for (auto ComponentTemplate : ActorComponentTempatesOwnedByClass)
			{
				if (ComponentTemplate)
				{
					HandleClassSubobject(Context, ComponentTemplate, FEmitterLocalContext::EClassSubobjectList::ComponentTemplates, bCreate, bInitilize);
				}
			}

			for (auto TimelineTemplate : BPGC->Timelines)
			{
				if (TimelineTemplate)
				{
					HandleClassSubobject(Context, TimelineTemplate, FEmitterLocalContext::EClassSubobjectList::Timelines, bCreate, bInitilize);
				}
			}

			for (auto DynamicBindingObject : BPGC->DynamicBindingObjects)
			{
				if (DynamicBindingObject)
				{
					HandleClassSubobject(Context, DynamicBindingObject, FEmitterLocalContext::EClassSubobjectList::DynamicBindingObjects, bCreate, bInitilize);
				}
			}
			FBackendHelperUMG::CreateClassSubobjects(Context, bCreate, bInitilize);
		};
		CreateAndInitializeClassSubobjects(true, false);
		CreateAndInitializeClassSubobjects(false, true);

		FBackendHelperAnim::CreateAnimClassData(Context);

		Context.DecreaseIndent();
		Context.AddLine(TEXT("}"));
	}

	// Components that must be fixed after serialization
	TArray<FString> NativeCreatedComponentProperties;

	{
		Context.CurrentCodeType = FEmitterLocalContext::EGeneratedCodeType::CommonConstructor;
		// Let's have an easy access to generated class subobjects
		Context.AddLine(TEXT("{")); // no shadow variables
		Context.IncreaseIndent();

		UObject* CDO = BPGC->GetDefaultObject(false);

		UObject* ParentCDO = BPGC->GetSuperClass()->GetDefaultObject(false);
		check(CDO && ParentCDO);
		Context.AddLine(TEXT(""));

		FString NativeRootComponentFallback;
		TSet<const UProperty*> HandledProperties;

		// Generate ctor init code for native class default subobjects that are always instanced (e.g. components).
		// @TODO (pkavan) - We can probably make this faster by generating code to index through the DSO array instead (i.e. in place of HandleInstancedSubobject which will generate a lookup call per DSO).
		TArray<UObject*> NativeDefaultObjectSubobjects;
		BPGC->GetDefaultObjectSubobjects(NativeDefaultObjectSubobjects);
		for (auto DSO : NativeDefaultObjectSubobjects)
		{
			if (DSO && DSO->GetClass()->HasAnyClassFlags(CLASS_DefaultToInstanced))
			{
				// Determine if this is an editor-only subobject.
				bool bIsEditorOnlySubobject = false;
				if (const UActorComponent* ActorComponent = Cast<UActorComponent>(DSO))
				{
					bIsEditorOnlySubobject = ActorComponent->IsEditorOnly();
				}

				// Skip ctor code gen for editor-only subobjects, since they won't be used by the runtime. Any dependencies on editor-only subobjects will be handled later (see HandleInstancedSubobject).
				if (!bIsEditorOnlySubobject)
				{
					const FString VariableName = HandleInstancedSubobject(Context, DSO, false, true);

					// Keep track of which component can be used as a root, in case it's not explicitly set.
					if (NativeRootComponentFallback.IsEmpty())
					{
						USceneComponent* SceneComponent = Cast<USceneComponent>(DSO);
						if (SceneComponent && !SceneComponent->AttachParent && SceneComponent->CreationMethod == EComponentCreationMethod::Native)
						{
							NativeRootComponentFallback = VariableName;
						}
					}
				}
			}
		}

		// Check for a valid RootComponent property value; mark it as handled if already set in the defaults.
		bool bNeedsRootComponentAssignment = false;
		static const FName RootComponentPropertyName(TEXT("RootComponent"));
		const UObjectProperty* RootComponentProperty = FindField<UObjectProperty>(BPGC, RootComponentPropertyName);
		if (RootComponentProperty)
		{
			if (RootComponentProperty->GetObjectPropertyValue_InContainer(CDO))
			{
				HandledProperties.Add(RootComponentProperty);
			}
			else if (!NativeRootComponentFallback.IsEmpty())
			{
				Context.AddLine(FString::Printf(TEXT("RootComponent = %s;"), *NativeRootComponentFallback));
				HandledProperties.Add(RootComponentProperty);
			}
			else
			{
				bNeedsRootComponentAssignment = true;
			}
		}

		// Generate ctor init code for the SCS node hierarchy (i.e. non-native components). SCS nodes may have dependencies on native DSOs, but not vice-versa.
		TArray<const UBlueprintGeneratedClass*> BPGCStack;
		const bool bErrorFree = UBlueprintGeneratedClass::GetGeneratedClassesHierarchy(BPGC, BPGCStack);
		if (bErrorFree)
		{
			TArray<FNonativeComponentData> ComponentsToInit;

			// Start at the base of the hierarchy so that dependencies are handled first.
			for (int32 i = BPGCStack.Num() - 1; i >= 0; --i)
			{
				if (BPGCStack[i]->SimpleConstructionScript)
				{
					for (auto Node : BPGCStack[i]->SimpleConstructionScript->GetRootNodes())
					{
						if (Node)
						{
							const FString NativeVariablePropertyName = HandleNonNativeComponent(Context, Node, HandledProperties, NativeCreatedComponentProperties, nullptr, ComponentsToInit);

							if (bNeedsRootComponentAssignment && Node->ComponentTemplate && Node->ComponentTemplate->IsA<USceneComponent>() && !NativeVariablePropertyName.IsEmpty())
							{
								// Only emit the explicit root component assignment statement if we're looking at the child BPGC that we're generating ctor code
								// for. In all other cases, the root component will already be set up by a chained parent ctor call, so we avoid stomping it here.
								if (i == 0)
								{
									Context.AddLine(FString::Printf(TEXT("RootComponent = %s;"), *NativeVariablePropertyName));
									HandledProperties.Add(RootComponentProperty);
								}

								bNeedsRootComponentAssignment = false;
							}
						}
					}
				}
			}

			for (auto& ComponentToInit : ComponentsToInit)
			{
				ComponentToInit.EmitProperties(Context);
			}
		}

		// Generate ctor init code for generated Blueprint class property values that may differ from parent class defaults (or that otherwise belong to the generated Blueprint class).
		for (auto Property : TFieldRange<const UProperty>(BPGC))
		{
			const bool bNewProperty = Property->GetOwnerStruct() == BPGC;
			const bool bIsAccessible = bNewProperty || !Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate);
			if (bIsAccessible && !HandledProperties.Contains(Property))
			{
				OuterGenerate(Context, Property, TEXT(""), reinterpret_cast<const uint8*>(CDO), bNewProperty ? nullptr : reinterpret_cast<const uint8*>(ParentCDO), EPropertyAccessOperator::None, true);
			}
		}

		Context.DecreaseIndent();
		Context.AddLine(TEXT("}"));
	}
	Context.DecreaseIndent();
	Context.AddLine(TEXT("}"));
	Context.CurrentCodeType = FEmitterLocalContext::EGeneratedCodeType::Regular;

	{
		Context.AddLine(FString::Printf(TEXT("void %s::PostLoadSubobjects(FObjectInstancingGraph* OuterInstanceGraph)"), *CppClassName));
		Context.AddLine(TEXT("{"));
		Context.IncreaseIndent();
		Context.AddLine(TEXT("Super::PostLoadSubobjects(OuterInstanceGraph);"));
		
		for (auto& ComponentToFix : NativeCreatedComponentProperties)
		{
			Context.AddLine(FString::Printf(TEXT("if(ensure(%s))"), *ComponentToFix));
			Context.AddLine(TEXT("{"));
			Context.IncreaseIndent();
			Context.AddLine(FString::Printf(TEXT("%s->CreationMethod = EComponentCreationMethod::Native;"), *ComponentToFix));
			Context.DecreaseIndent();
			Context.AddLine(TEXT("}"));
		}
		Context.DecreaseIndent();
		Context.AddLine(TEXT("}"));
	}

	FDependenciesHelper::AddStaticFunctionsForDependencies(Context);

	FBackendHelperUMG::EmitWidgetInitializationFunctions(Context);
}

FString FEmitDefaultValueHelper::HandleClassSubobject(FEmitterLocalContext& Context, UObject* Object, FEmitterLocalContext::EClassSubobjectList ListOfSubobjectsType, bool bCreate, bool bInitilize)
{
	ensure(Context.CurrentCodeType == FEmitterLocalContext::EGeneratedCodeType::SubobjectsOfClass);

	FString LocalNativeName;
	if (bCreate)
	{
		FString OuterStr = Context.FindGloballyMappedObject(Object->GetOuter());
		if (OuterStr.IsEmpty())
		{
			OuterStr = HandleClassSubobject(Context, Object->GetOuter(), ListOfSubobjectsType, bCreate, bInitilize);
			if (OuterStr.IsEmpty())
			{
				return FString();
			}
			const FString AlreadyCreatedObject = Context.FindGloballyMappedObject(Object);
			if (!AlreadyCreatedObject.IsEmpty())
			{
				return AlreadyCreatedObject;
			}
		}

		const bool AddAsSubobjectOfClass = Object->GetOuter() == Context.GetCurrentlyGeneratedClass();
		LocalNativeName = Context.GenerateUniqueLocalName();
		Context.AddClassSubObject_InConstructor(Object, LocalNativeName);
		UClass* ObjectClass = Object->GetClass();
		Context.AddLine(FString::Printf(
			TEXT("auto %s = NewObject<%s>(%s, TEXT(\"%s\"));")
			, *LocalNativeName
			, *FEmitHelper::GetCppName(ObjectClass)
			, *OuterStr
			, *Object->GetName()));
		if (AddAsSubobjectOfClass)
		{
			Context.RegisterClassSubobject(Object, ListOfSubobjectsType);
			Context.AddLine(FString::Printf(TEXT("CastChecked<UDynamicClass>(GetClass())->%s.Add(%s);")
				, Context.ClassSubobjectListName(ListOfSubobjectsType)
				, *LocalNativeName));
		}
	}

	if (bInitilize)
	{
		if (LocalNativeName.IsEmpty())
		{
			LocalNativeName = Context.FindGloballyMappedObject(Object);
		}
		ensure(!LocalNativeName.IsEmpty());
		auto CDO = Object->GetClass()->GetDefaultObject(false);
		for (auto Property : TFieldRange<const UProperty>(Object->GetClass()))
		{
			OuterGenerate(Context, Property, LocalNativeName
				, reinterpret_cast<const uint8*>(Object)
				, reinterpret_cast<const uint8*>(CDO)
				, EPropertyAccessOperator::Pointer);
		}
	}
	return LocalNativeName;
}

FString FEmitDefaultValueHelper::HandleInstancedSubobject(FEmitterLocalContext& Context, UObject* Object, bool bCreateInstance, bool bSkipEditorOnlyCheck)
{
	check(Object);

	// Make sure we don't emit initialization code for the same object more than once.
	FString LocalNativeName = Context.FindGloballyMappedObject(Object);
	if (!LocalNativeName.IsEmpty())
	{
		return LocalNativeName;
	}
	else
	{
		LocalNativeName = Context.GenerateUniqueLocalName();
	}

	if (Context.CurrentCodeType == FEmitterLocalContext::EGeneratedCodeType::SubobjectsOfClass)
	{
		Context.AddClassSubObject_InConstructor(Object, LocalNativeName);
	}
	else if (Context.CurrentCodeType == FEmitterLocalContext::EGeneratedCodeType::CommonConstructor)
	{
		Context.AddCommonSubObject_InConstructor(Object, LocalNativeName);
	}

	UClass* ObjectClass = Object->GetClass();

	// Determine if this is an editor-only subobject. When handling as a dependency, we'll create a "dummy" object in its place (below).
	bool bIsEditorOnlySubobject = false;
	if (!bSkipEditorOnlyCheck)
	{
		if (UActorComponent* ActorComponent = Cast<UActorComponent>(Object))
		{
			bIsEditorOnlySubobject = ActorComponent->IsEditorOnly();
			if (bIsEditorOnlySubobject)
			{
				// Replace the potentially editor-only class with a base actor/scene component class that's available to the runtime. We'll create a "dummy" object of this type to stand in for the editor-only subobject below.
				ObjectClass = ObjectClass->IsChildOf<USceneComponent>() ? USceneComponent::StaticClass() : UActorComponent::StaticClass();
			}
		}
	}

	auto BPGC = Context.GetCurrentlyGeneratedClass();
	auto CDO = BPGC ? BPGC->GetDefaultObject(false) : nullptr;
	if (!bIsEditorOnlySubobject && ensure(CDO) && (CDO == Object->GetOuter()))
	{
		if (bCreateInstance)
		{
			Context.AddLine(FString::Printf(TEXT("auto %s = CreateDefaultSubobject<%s>(TEXT(\"%s\"));")
				, *LocalNativeName, *FEmitHelper::GetCppName(ObjectClass), *Object->GetName()));
		}
		else
		{
			Context.AddLine(FString::Printf(TEXT("auto %s = CastChecked<%s>(GetDefaultSubobjectByName(TEXT(\"%s\")));")
				, *LocalNativeName, *FEmitHelper::GetCppName(ObjectClass), *Object->GetName()));
		}

		const UObject* ObjectArchetype = Object->GetArchetype();
		for (auto Property : TFieldRange<const UProperty>(ObjectClass))
		{
			OuterGenerate(Context, Property, LocalNativeName
				, reinterpret_cast<const uint8*>(Object)
				, reinterpret_cast<const uint8*>(ObjectArchetype)
				, EPropertyAccessOperator::Pointer);
		}
	}
	else
	{
		const FString OuterStr = Context.FindGloballyMappedObject(Object);
		if (OuterStr.IsEmpty())
		{
			ensure(false);
			return FString();
		}
		Context.AddLine(FString::Printf(TEXT("auto %s = NewObject<%s>(%s, TEXT(\"%s\"));")
			, *LocalNativeName
			, *FEmitHelper::GetCppName(ObjectClass)
			, *OuterStr
			, *Object->GetName()));
	}

	return LocalNativeName;
}