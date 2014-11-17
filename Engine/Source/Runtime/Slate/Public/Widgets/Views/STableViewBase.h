// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IScrollableWidget.h"

class SListPanel;
class SHeaderRow;

enum class EAllowOverscroll
{
	Yes,
	No
};

/**
 * Contains ListView functionality that does not depend on the type of data being observed by the ListView.
 */
class SLATE_API STableViewBase : public SCompoundWidget, public IScrollableWidget
{
public:

	/** Create the child widgets that comprise the list */
	void ConstructChildren( const TAttribute<float>& InItemWidth, const TAttribute<float>& InItemHeight, const TSharedPtr<SHeaderRow>& InColumnHeaders, const TSharedPtr<SScrollBar>& InScrollBar  );

	/** Sets the item height */
	void SetItemHeight(TAttribute<float> Height);

	/** Sets the item width */
	void SetItemWidth(TAttribute<float> Width);

	/**
	 * Invoked by the scrollbar when the user scrolls.
	 *
	 * @param InScrollOffsetFraction  The location to which the user scrolled as a fraction (between 0 and 1) of total height of the content.
	 */
	void ScrollBar_OnUserScrolled( float InScrollOffsetFraction );

	// SWidget interface
	virtual void OnKeyboardFocusLost( const FKeyboardFocusEvent& InKeyboardFocusEvent ) override;
	virtual void OnMouseCaptureLost() override;
	virtual bool SupportsKeyboardFocus() const override;
	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;
	virtual FReply OnPreviewMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;
	virtual FReply OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;
	virtual FReply OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent ) override;
	virtual FReply OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;
	virtual void OnMouseLeave( const FPointerEvent& MouseEvent ) override;
	virtual FReply OnMouseMove( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;
	virtual FReply OnMouseWheel( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;
	virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyboardEvent& InKeyboardEvent ) override;
	virtual FCursorReply OnCursorQuery( const FGeometry& MyGeometry, const FPointerEvent& CursorEvent ) const override;
	virtual FReply OnTouchStarted( const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent ) override;
	virtual FReply OnTouchMoved( const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent ) override;
	virtual FReply OnTouchEnded( const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent ) override;
	// End of SWidget interface

	/** @return The number of Widgets we currently have generated. */
	int32 GetNumGeneratedChildren() const;

	TSharedPtr<SHeaderRow> GetHeaderRow() const;

	/** @return Returns true if the user is currently interactively scrolling the view by holding
		        the right mouse button and dragging. */
	bool IsRightClickScrolling() const;

	/** @return Returns true if the user is currently interactively scrolling the view by holding
		        either mouse button and dragging. */
	bool IsUserScrolling() const;

	/**
	 * Mark the list as dirty, so that it will regenerate its widgets on next tick.
	 */
	void RequestListRefresh();

	/** Return true if there is currently a refresh pending, false otherwise */
	bool IsPendingRefresh() const;

	// IScrollableWidget interface
	virtual FVector2D GetScrollDistance() override;
	virtual FVector2D GetScrollDistanceRemaining() override;
	virtual TSharedRef<class SWidget> GetScrollWidget() override;
	// End of IScrollableWidget interface

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Is this list backing a tree or just a standalone list */
	const ETableViewMode::Type TableViewMode;

protected:

	STableViewBase( ETableViewMode::Type InTableViewMode );
	
	/**
	 * Scroll the list view by some number of screen units.
	 *
	 * @param MyGeometry      The geometry of the ListView at the time
	 * @param ScrollByAmount  The amount to scroll by in Slate Screen Units.
	 * @param AllowOverscroll Should we allow scrolling past the beginning/end of the list?
	 *
	 * @return The amount actually scrolled in items
	 */
	virtual float ScrollBy( const FGeometry& MyGeometry, float ScrollByAmount, EAllowOverscroll AllowOverscroll );

	/**
	 * Scroll the view to an offset
	 *
	 * @param InScrollOffset  Offset into the total list length to scroll down.
	 *
	 * @return The amount actually scrolled
	 */
	virtual float ScrollTo( float InScrollOffset );

	/** Insert WidgetToInsert at the top of the view. */
	void InsertWidget( const TSharedRef<ITableRow> & WidgetToInset );

	/** Add a WidgetToAppend to the bottom of the view. */
	void AppendWidget( const TSharedRef<ITableRow>& WidgetToAppend );
	
	/**
	 * Remove all the widgets from the view.
	 */
	void ClearWidgets();

	/**
	 * Get the uniform item width.
	 */
	float GetItemWidth() const;

	/**
	 * Get the uniform item height that is enforced by ListViews.
	 */
	float GetItemHeight() const;

	/** @return the number of items that can fit on the screen */
	virtual float GetNumLiveWidgets() const;

	/**
	 * Get the number of items that can fit in the view horizontally before creating a new row.
	 * Default is 1, but may be more in subclasses (like STileView)
	 */
	virtual int32 GetNumItemsWide() const;

	/**
	 * Opens a context menu as the result of a right click if OnContextMenuOpening is bound and we are not right click scrolling.
	 */
	virtual void OnRightMouseButtonUp(const FVector2D& SummonLocation);
	
	/**
	 * Get the scroll rate in items that best approximates a constant physical scroll rate.
	 */
	float GetScrollRateInItems() const;

	/**
	 * Remove any items that are no longer in the list from the selection set.
	 */
	virtual void UpdateSelectionSet() = 0;


	/** Information about the outcome of the WidgetRegeneratePass */
	struct SLATE_API FReGenerateResults
	{
		FReGenerateResults( double InNewScrollOffset, double InHeightGenerated, double InItemsOnScreen, bool AtEndOfList )
		: NewScrollOffset( InNewScrollOffset )
		, HeightOfGeneratedItems( InHeightGenerated )
		, ExactNumRowsOnScreen( InItemsOnScreen )
		, bGeneratedPastLastItem( AtEndOfList )
		{

		}

		/** The scroll offset that we actually use might not be what the user asked for */
		double NewScrollOffset;

		/** The total height of the widgets that we have generated to represent the visible subset of the items*/
		double HeightOfGeneratedItems;

		/** How many rows are fitting on the screen, including fractions */
		double ExactNumRowsOnScreen;

		/** True when we have generated  */
		bool bGeneratedPastLastItem;
	};
	/**
	 * Update generate Widgets for Items as needed and clean up any Widgets that are no longer needed.
	 * Re-arrange the visible widget order as necessary.
	 */
	virtual FReGenerateResults ReGenerateItems( const FGeometry& MyGeometry ) = 0;

	/** @return how many items there are in the TArray being observed */
	virtual int32 GetNumItemsBeingObserved() const = 0;

	/**
	 * If there is a pending request to scroll an item into view, do so.
	 * 
	 * @param ListViewGeometry  The geometry of the listView; can be useful for centering the item.
	 */
	virtual void ScrollIntoView(const FGeometry& ListViewGeometry) = 0;

	/**
	 * Called when an item has entered the visible geometry to check to see if the ItemScrolledIntoView delegate should be fired.
	 */
	virtual void NotifyItemScrolledIntoView() = 0;

	/** The panel which holds the visible widgets in this list */
	TSharedPtr< SListPanel > ItemsPanel;
	/** The scroll bar widget */
	TSharedPtr< SScrollBar > ScrollBar;

	/** Scroll offset from the beginning of the list in items */
	double ScrollOffset;

	/** Did the user start an interaction in this list? */
	bool bStartedTouchInteraction;

	/** How much we scrolled while the rmb has been held */
	float AmountScrolledWhileRightMouseDown;

	/** Information about the widgets we generated during the last regenerate pass */
	FReGenerateResults LastGenerateResults;

	/** Last time we scrolled, did we end up at the end of the list. */
	bool bWasAtEndOfList;

	/** What the list's geometry was the last time a refresh occurred. */
	FGeometry PanelGeometryLastTick;

	/** Delegate to invoke when the context menu should be opening. If it is NULL, a context menu will not be summoned. */
	FOnContextMenuOpening OnContextMenuOpening;

	/** The selection mode that this tree/list is in. Note that it is up to the generated ITableRows to respect this setting. */
	TAttribute<ESelectionMode::Type> SelectionMode;

	/** Column headers that describe which columns this list shows */
	TSharedPtr< SHeaderRow > HeaderRow;

	/** Helper object to manage inertial scrolling */
	FInertialScrollManager InertialScrollManager;

	/**	The current position of the software cursor */
	FVector2D SoftwareCursorPosition;

	/**	Whether the software cursor should be drawn in the viewport */
	bool bShowSoftwareCursor;

	struct SLATE_API FOverscroll
	{
	public:

		FOverscroll();

		/** @return The Amount actually scrolled */
		float ScrollBy( float Delta );

		/** How far the user scrolled above/below the beginning/end of the list. */
		float GetOverscroll() const;

		/** Ticks the overscroll manager so it can animate. */
		void UpdateOverscroll( float InDeltaTime );

		/**
		 * Should ScrollDelta be applied to overscroll or to regular item scrolling.
		 *
		 * @param bIsAtStartOfList  Are we at the very beginning of the list (i.e. showing the first item at the top of the view)?
		 * @param bIsAtEndOfList    Are we showing the last item on the screen completely?
		 * @param ScrollDelta       How much the user is trying to scroll in Slate Units.
		 *
		 * @return true if the user's scrolling should be applied toward overscroll.
		 */
		bool ShouldApplyOverscroll( const bool bIsAtStartOfList, const bool bIsAtEndOfList, const float ScrollDelta ) const;

	private:
		/** How much we've over-scrolled above/below the beginning/end of the list. */
		float OverscrollAmount;
	} Overscroll;

private:
	
	/** When true, a refresh should occur the next tick */
	bool bItemsNeedRefresh;
};