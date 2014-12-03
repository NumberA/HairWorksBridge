// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGEditorPrivatePCH.h"
#include "IUMGDesigner.h"
#include "ObjectEditorUtils.h"
#include "STransformHandle.h"
#include "WidgetReference.h"
#include "Widget.h"
#include "Components/PanelSlot.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// STransformHandle

void STransformHandle::Construct(const FArguments& InArgs, IUMGDesigner* InDesigner, ETransformDirection::Type InTransformDirection)
{
	TransformDirection = InTransformDirection;
	Designer = InDesigner;

	Action = ETransformAction::None;

	DragDirection = ComputeDragDirection(InTransformDirection);
	DragOrigin = ComputeOrigin(InTransformDirection);

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(10)
		.HeightOverride(10)
		.Visibility(this, &STransformHandle::GetHandleVisibility)
		[
			SNew(SImage)
			.Image(FEditorStyle::Get().GetBrush("CurveEd.CurveKey"))
		]
	];
}

EVisibility STransformHandle::GetHandleVisibility() const
{
	ETransformMode::Type TransformMode = Designer->GetTransformMode();

	// Only show the handles for visible elements in the designer.
	FWidgetReference SelectedWidget = Designer->GetSelectedWidget();
	if ( SelectedWidget.IsValid() )
	{
		if ( !SelectedWidget.GetTemplate()->bHiddenInDesigner )
		{
			switch ( TransformMode )
			{
			case ETransformMode::Layout:
			{
				if ( UPanelSlot* TemplateSlot = SelectedWidget.GetTemplate()->Slot )
				{
					if ( TemplateSlot->CanResize(DragDirection) )
					{
						return EVisibility::Visible;
					}
				}
				break;
			}
			case ETransformMode::Render:
				return EVisibility::Visible;
			}
		}
	}

	return EVisibility::Collapsed;
}

FReply STransformHandle::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
	{
		Action = ComputeActionAtLocation(MyGeometry, MouseEvent);

		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Unhandled();
}

FReply STransformHandle::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( HasMouseCapture() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
	{
		const bool bRequiresRecompile = false;
		Designer->MarkDesignModifed(bRequiresRecompile);

		Action = ETransformAction::None;
		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

FReply STransformHandle::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( Action != ETransformAction::None )
	{
		FWidgetReference SelectedWidget = Designer->GetSelectedWidget();

		UWidget* Template = SelectedWidget.GetTemplate();
		UWidget* Preview = SelectedWidget.GetPreview();

		ETransformMode::Type TransformMode = Designer->GetTransformMode();
		if ( TransformMode == ETransformMode::Layout )
		{
			//FGeometry MyGeometry = Designer->GetWidgetGeometry(SelectedWidget);
			//FGeometry ParentGeometry = Designer->GetWidgetParentGeometry(SelectedWidget);

			//FVector2D LocalPosition = MyGeometry.Position;
			//FVector2D LocalDragPosition = ParentGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

			//FVector2D NewSize = LocalDragPosition - LocalPosition;

			FVector2D TranslateAmount = MouseEvent.GetCursorDelta() * ( 1.0f / Designer->GetPreviewScale() );

			if ( Preview->Slot )
			{
				Preview->Slot->Resize(DragDirection, TranslateAmount);
			}

			if ( Template->Slot )
			{
				Template->Slot->Resize(DragDirection, TranslateAmount);
			}
		}
		else if ( TransformMode == ETransformMode::Render )
		{
			FWidgetTransform RenderTransform = Preview->RenderTransform;

			//if ( Action == ETransformAction::Primary )
			//{
				FVector2D TranslateAmount = MouseEvent.GetCursorDelta() * ( 1.0f / Designer->GetPreviewScale() );

				if ( Preview->Slot )
				{
					Preview->Slot->Resize(DragDirection, TranslateAmount);
				}

				if ( Template->Slot )
				{
					Template->Slot->Resize(DragDirection, TranslateAmount);
				}
			//}
			//else
			//{
			//	FVector2D RotationDelta = MouseEvent.GetCursorDelta();
			//	RenderTransform.Angle += RotationDelta.Size();
			//}

			static const FName RenderTransformName(TEXT("RenderTransform"));

			FObjectEditorUtils::SetPropertyValue<UWidget, FWidgetTransform>(Preview, RenderTransformName, RenderTransform);
			FObjectEditorUtils::SetPropertyValue<UWidget, FWidgetTransform>(Template, RenderTransformName, RenderTransform);
		}
	}

	return FReply::Unhandled();
}

FCursorReply STransformHandle::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
{
	ETransformAction::Type CurrentAction = Action;
	if ( CurrentAction == ETransformAction::None )
	{
		CurrentAction = ComputeActionAtLocation(MyGeometry, MouseEvent);
	}

	ETransformMode::Type TransformMode = Designer->GetTransformMode();

	//if ( TransformMode == ETransformMode::Layout || CurrentAction == ETransformAction::Primary )
	//{
		switch ( TransformDirection )
		{
		case ETransformDirection::TopLeft:
		case ETransformDirection::BottomRight:
			return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
		case ETransformDirection::TopRight:
		case ETransformDirection::BottomLeft:
			return FCursorReply::Cursor(EMouseCursor::ResizeSouthWest);
		case ETransformDirection::TopCenter:
		case ETransformDirection::BottomCenter:
			return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
		case ETransformDirection::CenterLeft:
		case ETransformDirection::CenterRight:
			return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
		}
	//}
	//else
	//{
	//	return FCursorReply::Cursor(EMouseCursor::EyeDropper);
	//}

	return FCursorReply::Unhandled();
}

FVector2D STransformHandle::ComputeDragDirection(ETransformDirection::Type InTransformDirection) const
{
	switch ( InTransformDirection )
	{
	case ETransformDirection::TopLeft:
		return FVector2D(-1, -1);
	case ETransformDirection::TopCenter:
		return FVector2D(0, -1);
	case ETransformDirection::TopRight:
		return FVector2D(1, -1);

	case ETransformDirection::CenterLeft:
		return FVector2D(-1, 0);
	case ETransformDirection::CenterRight:
		return FVector2D(1, 0);

	case ETransformDirection::BottomLeft:
		return FVector2D(-1, 1);
	case ETransformDirection::BottomCenter:
		return FVector2D(0, 1);
	case ETransformDirection::BottomRight:
		return FVector2D(1, 1);
	}

	return FVector2D(0, 0);
}

FVector2D STransformHandle::ComputeOrigin(ETransformDirection::Type InTransformDirection) const
{
	FVector2D Size(10, 10);

	switch ( InTransformDirection )
	{
	case ETransformDirection::TopLeft:
		return Size * FVector2D(1, 1);
	case ETransformDirection::TopCenter:
		return Size * FVector2D(0.5, 1);
	case ETransformDirection::TopRight:
		return Size * FVector2D(0, 1);

	case ETransformDirection::CenterLeft:
		return Size * FVector2D(1, 0.5);
	case ETransformDirection::CenterRight:
		return Size * FVector2D(0, 0.5);

	case ETransformDirection::BottomLeft:
		return Size * FVector2D(1, 0);
	case ETransformDirection::BottomCenter:
		return Size * FVector2D(0.5, 0);
	case ETransformDirection::BottomRight:
		return Size * FVector2D(0, 0);
	}

	return FVector2D(0, 0);
}

ETransformAction::Type STransformHandle::ComputeActionAtLocation(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
{
	FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	FVector2D GrabOriginOffset = LocalPosition - DragOrigin;
	if ( GrabOriginOffset.Size() < 6 )
	{
		return ETransformAction::Primary;
	}
	else
	{
		return ETransformAction::Secondary;
	}
}

#undef LOCTEXT_NAMESPACE