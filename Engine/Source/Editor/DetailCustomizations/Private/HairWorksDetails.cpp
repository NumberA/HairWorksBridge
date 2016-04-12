// @third party code - BEGIN HairWorks
#include "DetailCustomizationsPrivatePCH.h"
#include "Engine/HairWorksMaterial.h"
#include "Engine/HairWorksAsset.h"
#include "Components/HairWorksComponent.h"
#include "HairWorksDetails.h"

TSharedRef<IDetailCustomization> FHairWorksMaterialDetails::MakeInstance()
{
	return MakeShareable(new FHairWorksMaterialDetails);
}

void FHairWorksMaterialDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Build category tree.
	struct FCategory
	{
		FName Name;
		TArray<FName> Properties;
		TArray<TSharedRef<FCategory>> Categories;
	};

	FCategory TopCategory;

	for(TFieldIterator<UProperty> PropIt(UHairWorksMaterial::StaticClass()); PropIt; ++PropIt)
	{
		// Get category path
		auto& Property = **PropIt;

		auto& CategoryPath = Property.GetMetaData("Category");
		TArray<FString> ParsedNames;
		CategoryPath.ParseIntoArray(ParsedNames, TEXT("|"), true);

		// Find or create category path.
		auto* Category = &TopCategory;
		for(auto& CategoryName : ParsedNames)
		{
			auto* SubCategory = Category->Categories.FindByPredicate([&](const TSharedRef<FCategory>& Category){return Category->Name == *CategoryName; });
			if(SubCategory != nullptr)
			{
				Category = &SubCategory->Get();
			}
			else
			{
				TSharedRef<FCategory> NewCategory(new FCategory);
				NewCategory->Name = *CategoryName;

				Category->Categories.Add(NewCategory);

				Category = &NewCategory.Get();
			}
		}

		// Add property.
		Category->Properties.Add(*Property.GetNameCPP());
	}

	// Collect selected hair materials
	TArray<TWeakObjectPtr<UObject>> CurrentObjects;
	DetailBuilder.GetObjectsBeingCustomized(CurrentObjects);

	TSharedRef<TArray<TWeakObjectPtr<UHairWorksMaterial>>> HairMaterials(new TArray<TWeakObjectPtr<UHairWorksMaterial>>);
	for(auto& ObjectPtr : CurrentObjects)
	{
		auto* HairMaterial = Cast<UHairWorksMaterial>(ObjectPtr.Get());
		if(HairMaterial != nullptr)
			HairMaterials->Add(HairMaterial);
	}

	// Reset handler
	static auto GetDefaultHairMaterial = [](const UHairWorksMaterial& HairMaterial)->UHairWorksMaterial&
	{
		// Use asset's hair material as default object if possible.
		if(auto* HairWorksComponent = Cast<UHairWorksComponent>(HairMaterial.GetOuter()))
		{
			if(HairWorksComponent->HairInstance.Hair != nullptr)
				return *HairWorksComponent->HairInstance.Hair->HairMaterial;
		}

		return *UHairWorksMaterial::StaticClass()->GetDefaultObject<UHairWorksMaterial>();
	};

	auto IsResetVisible = [](TSharedRef<IPropertyHandle> PropertyHandle, TSharedRef<TArray<TWeakObjectPtr<UHairWorksMaterial>>> SelectedObjects)
	{
		if(!PropertyHandle->IsValidHandle())
			return false;

		for(auto& HairMaterial : *SelectedObjects)
		{
			if(!HairMaterial.IsValid())
				continue;

			auto& DefaultHairMaterial = GetDefaultHairMaterial(*HairMaterial);

			auto& Property = *PropertyHandle->GetProperty();
			if(!Property.Identical_InContainer(HairMaterial.Get(), &DefaultHairMaterial))
				return true;
		}

		return false;
	};

	auto ResetProperty = [](TSharedRef<IPropertyHandle> PropertyHandle, TSharedRef<TArray<TWeakObjectPtr<UHairWorksMaterial>>> SelectedObjects)
	{
		if(!PropertyHandle->IsValidHandle())
			return;

		for(auto& HairMaterial : *SelectedObjects)
		{
			if(!HairMaterial.IsValid())
				continue;

			auto& DefaultHairMaterial = GetDefaultHairMaterial(*HairMaterial);

			auto& Property = *PropertyHandle->GetProperty();
			Property.CopyCompleteValue_InContainer(HairMaterial.Get(), &DefaultHairMaterial);
		}
	};

	// Conditional edit handler
	auto IsEditable = [](TSharedRef<TArray<TWeakObjectPtr<UHairWorksMaterial>>> SelectedObjects)
	{
		for(auto& HairMaterial : *SelectedObjects)
		{
			if(!HairMaterial.IsValid())
				continue;

			if(auto* HairWorksComponent = Cast<UHairWorksComponent>(HairMaterial->GetOuter()))
			{
				if(!HairWorksComponent->HairInstance.bOverride)
					return false;
			}
		}

		return true;
	};

	// To Bind handles
	auto AddPropertyHandler = [&](IDetailPropertyRow& DetailProperty)
	{
		DetailProperty.OverrideResetToDefault(FResetToDefaultOverride::Create(
			FIsResetToDefaultVisible::CreateStatic(IsResetVisible, HairMaterials),
			FResetToDefaultHandler::CreateStatic(ResetProperty, HairMaterials)
			));

		DetailProperty.EditCondition(
			TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateStatic(IsEditable, HairMaterials)),
			nullptr
			);
	};

	// Build property widgets
	for(auto& Category : TopCategory.Categories)
	{
		// Add category
		auto& CategoryBuilder = DetailBuilder.EditCategory(Category->Name, FText::GetEmpty(), ECategoryPriority::Uncommon);

		// Add properties
		for(auto& PropertyName : Category->Properties)
		{
			auto& DetailProperty = CategoryBuilder.AddProperty(DetailBuilder.GetProperty(PropertyName));
			AddPropertyHandler(DetailProperty);
		}

		// Add groups
		for(auto& Group : Category->Categories)
		{
			auto& DetailGroup = CategoryBuilder.AddGroup(Group->Name, FText::FromName(Group->Name));

			// Add properties
			for(auto& PropertyName : Group->Properties)
			{
				const auto& PropertyHandle = DetailBuilder.GetProperty(PropertyName);

				auto& DetailProperty = DetailGroup.AddPropertyRow(PropertyHandle);
				AddPropertyHandler(DetailProperty);

				// Spacial case for pins.
				if(PropertyHandle->GetProperty()->GetNameCPP() == "Pins")
				{
					// Pin array should not be reseted
					DetailProperty.OverrideResetToDefault(FResetToDefaultOverride::Hide());

					// Pins should be edited only in assets.
					auto* HairMaterialNotInAsset = HairMaterials->FindByPredicate([](const auto& HairMaterial)
					{
						return !HairMaterial->GetOuter()->IsA<UHairWorksAsset>();
					});

					if(HairMaterialNotInAsset != nullptr)
					{
						DetailProperty.IsEnabled(false);
						break;
					}

					// In asset, pin should not be reseted. Because pin bone name should not be reseted
					uint32 ChildNum = 0;
					PropertyHandle->GetNumChildren(ChildNum);
					for(uint32 Index = 0; Index < ChildNum; ++Index)
					{
						const auto& PinHandle = PropertyHandle->GetChildHandle(Index);
						DetailGroup.AddPropertyRow(PinHandle.ToSharedRef()).OverrideResetToDefault(FResetToDefaultOverride::Hide());
					}
				}
			}
		}
	}
}
// @third party code - END HairWorks
