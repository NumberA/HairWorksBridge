// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"

#include "RetainerBox.h"
#include "SRetainerWidget.h"

#define LOCTEXT_NAMESPACE "UMG"

static FName DefaultTextureParameterName("Texture");

/////////////////////////////////////////////////////
// URetainerBox

URetainerBox::URetainerBox(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Visibility = ESlateVisibility::Visible;
	Phase = 0;
	PhaseCount = 1;
	TextureParameter = DefaultTextureParameterName;
}

UMaterialInstanceDynamic* URetainerBox::GetEffectMaterial() const
{
	if ( MyRetainerWidget.IsValid() )
	{
		return MyRetainerWidget->GetEffectMaterial();
	}

	return nullptr;
}

void URetainerBox::SetEffectMaterial(UMaterialInterface* InEffectMaterial)
{
	EffectMaterial = InEffectMaterial;
	if ( MyRetainerWidget.IsValid() )
	{
		MyRetainerWidget->SetEffectMaterial(EffectMaterial);
	}
}

void URetainerBox::SetTextureParameter(FName InTextureParameter)
{
	TextureParameter = InTextureParameter;
	if ( MyRetainerWidget.IsValid() )
	{
		MyRetainerWidget->SetTextureParameter(TextureParameter);
	}
}

void URetainerBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyRetainerWidget.Reset();
}

TSharedRef<SWidget> URetainerBox::RebuildWidget()
{
	MyRetainerWidget =
		SNew(SRetainerWidget)
		.Phase(Phase)
		.PhaseCount(PhaseCount)
#if STATS
		.StatId( FName( *FString::Printf(TEXT("%s [%s]"), *GetFName().ToString(), *GetClass()->GetName() ) ) )
#endif//STATS
		;

	MyRetainerWidget->SetRetainedRendering(IsDesignTime() ? false : true);

	if ( GetChildrenCount() > 0 )
	{
		MyRetainerWidget->SetContent(GetContentSlot()->Content ? GetContentSlot()->Content->TakeWidget() : SNullWidget::NullWidget);
	}
	
	return BuildDesignTimeWidget(MyRetainerWidget.ToSharedRef());
}

void URetainerBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	MyRetainerWidget->SetEffectMaterial(EffectMaterial);
	MyRetainerWidget->SetTextureParameter(TextureParameter);
}

void URetainerBox::OnSlotAdded(UPanelSlot* Slot)
{
	// Add the child to the live slot if it already exists
	if ( MyRetainerWidget.IsValid() )
	{
		MyRetainerWidget->SetContent(Slot->Content ? Slot->Content->TakeWidget() : SNullWidget::NullWidget);
	}
}

void URetainerBox::OnSlotRemoved(UPanelSlot* Slot)
{
	// Remove the widget from the live slot if it exists.
	if ( MyRetainerWidget.IsValid() )
	{
		MyRetainerWidget->SetContent(SNullWidget::NullWidget);
	}
}

#if WITH_EDITOR

const FSlateBrush* URetainerBox::GetEditorIcon()
{
	return FUMGStyle::Get().GetBrush("Widget.MenuAnchor");
}

const FText URetainerBox::GetPaletteCategory()
{
	return LOCTEXT("Optimization", "Optimization");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
