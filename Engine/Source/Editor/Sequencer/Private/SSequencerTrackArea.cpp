// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "SequencerPrivatePCH.h"
#include "SSequencerTrackArea.h"
#include "SSequencerTrackLane.h"
#include "TimeSliderController.h"
#include "CommonMovieSceneTools.h"
#include "Sequencer.h"
#include "SSequencerTreeView.h"
#include "IKeyArea.h"
#include "ISequencerSection.h"
#include "SSequencerSection.h"
#include "ISequencerTrackEditor.h"

FTrackAreaSlot::FTrackAreaSlot(const TSharedPtr<SSequencerTrackLane>& InSlotContent)
{
	TrackLane = InSlotContent;
	
	HAlignment = HAlign_Fill;
	VAlignment = VAlign_Top;

	this->AttachWidget(
		SNew(SWeakWidget)
			.PossiblyNullContent(InSlotContent)
	);
}


float FTrackAreaSlot::GetVerticalOffset() const
{
	auto PinnedTrackLane = TrackLane.Pin();
	return PinnedTrackLane.IsValid() ? PinnedTrackLane->GetPhysicalPosition() : 0.f;
}


void SSequencerTrackArea::Construct(const FArguments& InArgs, TSharedRef<FSequencerTimeSliderController> InTimeSliderController, TSharedRef<FSequencer> InSequencer)
{
	Sequencer = InSequencer;
	TimeSliderController = InTimeSliderController;

	// Input stack in order or priority

	// Space for the edit tool
	InputStack.AddHandler(nullptr);
	// The time slider controller
	InputStack.AddHandler(TimeSliderController.Get());
}


void SSequencerTrackArea::SetTreeView(const TSharedPtr<SSequencerTreeView>& InTreeView)
{
	TreeView = InTreeView;
}


void SSequencerTrackArea::AddTrackSlot(const TSharedRef<FSequencerDisplayNode>& InNode, const TSharedPtr<SSequencerTrackLane>& InSlot)
{
	TrackSlots.Add(InNode, InSlot);
	Children.Add(new FTrackAreaSlot(InSlot));
}


TSharedPtr<SSequencerTrackLane> SSequencerTrackArea::FindTrackSlot(const TSharedRef<FSequencerDisplayNode>& InNode)
{
	return TrackSlots.FindRef(InNode).Pin();
}


void SSequencerTrackArea::OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const
{
	for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
	{
		const FTrackAreaSlot& CurChild = Children[ChildIndex];

		const EVisibility ChildVisibility = CurChild.GetWidget()->GetVisibility();
		if (!ArrangedChildren.Accepts(ChildVisibility))
		{
			continue;
		}

		const FMargin Padding(0, CurChild.GetVerticalOffset(), 0, 0);

		AlignmentArrangeResult XResult = AlignChild<Orient_Horizontal>(AllottedGeometry.Size.X, CurChild, Padding, 1.0f, false);
		AlignmentArrangeResult YResult = AlignChild<Orient_Vertical>(AllottedGeometry.Size.Y, CurChild, Padding, 1.0f, false);

		ArrangedChildren.AddWidget(ChildVisibility,
			AllottedGeometry.MakeChild(
				CurChild.GetWidget(),
				FVector2D(XResult.Offset,YResult.Offset),
				FVector2D(XResult.Size, YResult.Size)
			)
		);
	}
}


FVector2D SSequencerTrackArea::ComputeDesiredSize( float ) const
{
	FVector2D MaxSize(0,0);
	for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
	{
		const FTrackAreaSlot& CurChild = Children[ChildIndex];

		const EVisibility ChildVisibilty = CurChild.GetWidget()->GetVisibility();
		if (ChildVisibilty != EVisibility::Collapsed)
		{
			FVector2D ChildDesiredSize = CurChild.GetWidget()->GetDesiredSize();
			MaxSize.X = FMath::Max(MaxSize.X, ChildDesiredSize.X);
			MaxSize.Y = FMath::Max(MaxSize.Y, ChildDesiredSize.Y);
		}
	}

	return MaxSize;
}


FChildren* SSequencerTrackArea::GetChildren()
{
	return &Children;
}


int32 SSequencerTrackArea::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	if ( Sequencer.IsValid() )
	{
		// give track editors a chance to paint
		auto TrackEditors = Sequencer.Pin()->GetTrackEditors();

		for (const auto& TrackEditor : TrackEditors)
		{
			LayerId = TrackEditor->PaintTrackArea(Args, AllottedGeometry, MyClippingRect, OutDrawElements, LayerId + 1, InWidgetStyle);
		}

		// paint the child widgets
		FArrangedChildren ArrangedChildren(EVisibility::Visible);
		ArrangeChildren(AllottedGeometry, ArrangedChildren);

		const FPaintArgs NewArgs = Args.WithNewParent(this);

		for (int32 ChildIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex)
		{
			FArrangedWidget& CurWidget = ArrangedChildren[ChildIndex];
			FSlateRect ChildClipRect = MyClippingRect.IntersectionWith( CurWidget.Geometry.GetClippingRect() );
			const int32 ThisWidgetLayerId = CurWidget.Widget->Paint( NewArgs, CurWidget.Geometry, ChildClipRect, OutDrawElements, LayerId + 2, InWidgetStyle, ShouldBeEnabled( bParentEnabled ) );

			LayerId = FMath::Max(LayerId, ThisWidgetLayerId);
		}

		return Sequencer.Pin()->GetEditTool().OnPaint(AllottedGeometry, MyClippingRect, OutDrawElements, LayerId + 2);
	}
	return LayerId;
}


FReply SSequencerTrackArea::OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if ( Sequencer.IsValid() )
	{
		// Always ensure the edit tool is set up
		InputStack.SetHandlerAt(0, &Sequencer.Pin()->GetEditTool());
		return InputStack.HandleMouseButtonDown(*this, MyGeometry, MouseEvent);
	}
	return FReply::Unhandled();
}


FReply SSequencerTrackArea::OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if ( Sequencer.IsValid() )
	{
		FContextMenuSuppressor SuppressContextMenus(TimeSliderController.ToSharedRef());

		// Always ensure the edit tool is set up
		InputStack.SetHandlerAt(0, &Sequencer.Pin()->GetEditTool());
		return InputStack.HandleMouseButtonUp(*this, MyGeometry, MouseEvent);
	}
	return FReply::Unhandled();
}


FReply SSequencerTrackArea::OnMouseMove( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if ( Sequencer.IsValid() )
	{
		// Always ensure the edit tool is set up
		InputStack.SetHandlerAt(0, &Sequencer.Pin()->GetEditTool());

		FReply Reply = InputStack.HandleMouseMove(*this, MyGeometry, MouseEvent);

		// Handle right click scrolling on the track area, if the captured index is that of the time slider
		if (Reply.IsEventHandled() && InputStack.GetCapturedIndex() == 1)
		{
			if (MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton) && HasMouseCapture())
			{
				TreeView.Pin()->ScrollByDelta(-MouseEvent.GetCursorDelta().Y);
			}
		}

		return Reply;
	}
	return FReply::Unhandled();
}


FReply SSequencerTrackArea::OnMouseWheel( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if ( Sequencer.IsValid() )
	{
		// Always ensure the edit tool is set up
		InputStack.SetHandlerAt(0, &Sequencer.Pin()->GetEditTool());
		return InputStack.HandleMouseWheel(*this, MyGeometry, MouseEvent);
	}
	return FReply::Unhandled();
}


void SSequencerTrackArea::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( Sequencer.IsValid() )
	{
		Sequencer.Pin()->GetEditTool().OnMouseEnter(*this, MyGeometry, MouseEvent);
	}
}


void SSequencerTrackArea::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if ( Sequencer.IsValid() )
	{
		Sequencer.Pin()->GetEditTool().OnMouseLeave(*this, MouseEvent);
	}
}


void SSequencerTrackArea::OnMouseCaptureLost()
{
	if ( Sequencer.IsValid() )
	{
		Sequencer.Pin()->GetEditTool().OnMouseCaptureLost();
	}
}


FCursorReply SSequencerTrackArea::OnCursorQuery( const FGeometry& MyGeometry, const FPointerEvent& CursorEvent ) const
{
	if ( Sequencer.IsValid() )
	{
		if (CursorEvent.IsMouseButtonDown(EKeys::RightMouseButton) && HasMouseCapture())
		{
			return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
		}

		return Sequencer.Pin()->GetEditTool().OnCursorQuery(MyGeometry, CursorEvent);
	}
	return FCursorReply::Unhandled();
}


void SSequencerTrackArea::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	CachedGeometry = AllottedGeometry;

	if ( Sequencer.IsValid() )
	{
		Sequencer.Pin()->GetEditTool().Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	}

	FVector2D Size = AllottedGeometry.GetLocalSize();

	if (SizeLastFrame.IsSet() && Size.X != SizeLastFrame->X)
	{
		// Zoom by the difference in horizontal size
		const float Difference = Size.X - SizeLastFrame->X;
		TRange<float> OldRange = TimeSliderController->GetViewRange().GetAnimationTarget();

		TimeSliderController->SetViewRange(
			OldRange.GetLowerBoundValue(),
			OldRange.GetUpperBoundValue() + (Difference * OldRange.Size<float>() / SizeLastFrame->X),
			EViewRangeInterpolation::Immediate
		);
	}

	SizeLastFrame = Size;

	for (int32 Index = 0; Index < Children.Num();)
	{
		if (!StaticCastSharedRef<SWeakWidget>(Children[Index].GetWidget())->ChildWidgetIsValid())
		{
			Children.RemoveAt(Index);
		}
		else
		{
			++Index;
		}
	}
}
