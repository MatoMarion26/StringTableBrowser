// Copyright (c) 2026 Your Name. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StringTableBrowserModule.h"
#include "StringTableSearchFilter.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

/**
 * SStringTableBrowser
 *
 * The main editor panel widget. Displays all string table entries from the
 * module's cache in a sortable, filterable list view.
 *
 * Opened via Tools → String Table Browser. Can be docked anywhere in the layout.
 *
 * Search is debounced — the filter runs 150ms after the user stops typing rather
 * than on every keystroke, keeping the UI responsive on large datasets.
 */
class SStringTableBrowser : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SStringTableBrowser) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SStringTableBrowser() override;

private:

	// -------------------------------------------------------------------------
	// Row generation
	// -------------------------------------------------------------------------

	TSharedRef<ITableRow> GenerateRow(
		TSharedPtr<FStringTableBrowserEntry> Item,
		const TSharedRef<STableViewBase>&    OwnerTable);

	// -------------------------------------------------------------------------
	// Data management
	// -------------------------------------------------------------------------

	/** Called by the OnCacheUpdated delegate. Re-runs filter and sort. */
	void UpdateFromCache();

	/** Fetches a cache snapshot, applies the current filter, then sorts. */
	void ApplyFilterAndSort();

	// -------------------------------------------------------------------------
	// Debounced search
	// -------------------------------------------------------------------------

	/**
	 * Called on every keystroke. Updates the filter state and starts (or restarts)
	 * the debounce timer. ApplyFilterAndSort() is not called directly here.
	 */
	void OnSearchTextChanged(const FText& InFilterText);

	/**
	 * Active timer callback fired after the debounce interval elapses.
	 * Runs the actual filter and returns Stop so the timer removes itself.
	 */
	EActiveTimerReturnType OnSearchDebounceTimer(double InCurrentTime, float InDeltaTime);

	// -------------------------------------------------------------------------
	// Sorting
	// -------------------------------------------------------------------------

	void OnSortColumnHeader(
		EColumnSortPriority::Type SortPriority,
		const FName&              ColumnId,
		EColumnSortMode::Type     NewSortMode);

	EColumnSortMode::Type GetColumnSortMode(FName ColumnId) const;

	void SortData();

	// -------------------------------------------------------------------------
	// UI callbacks
	// -------------------------------------------------------------------------

	FReply OnEditStringTableClicked(FSoftObjectPath AssetPath);
	FReply OnCopyKeyClicked(TSharedPtr<FStringTableBrowserEntry> Item);

	/**
	 * Opens Unreal's native Reference Viewer for the source string table asset,
	 * showing all assets that reference or are referenced by it.
	 */
	FReply OnViewReferencesClicked(FSoftObjectPath AssetPath);

	// -------------------------------------------------------------------------
	// Data
	// -------------------------------------------------------------------------

	/** Entries currently visible in the list after filtering and sorting. */
	TArray<TSharedPtr<FStringTableBrowserEntry>> FilteredEntries;

	TSharedPtr<SListView<TSharedPtr<FStringTableBrowserEntry>>> ListView;

	/** Encapsulates all search state and pattern compilation. */
	FStringTableSearchFilter Filter;

	FName CurrentSortColumn;
	EColumnSortMode::Type CurrentSortMode = EColumnSortMode::Ascending;

	/**
	 * Handle to the active debounce timer, if any.
	 * Kept so we can cancel and restart the timer when the user types again
	 * before the previous interval has elapsed.
	 */
	TWeakPtr<FActiveTimerHandle> SearchDebounceTimerHandle;

	/** Debounce interval in seconds. Filter runs this long after the last keystroke. */
	static constexpr float SearchDebounceDelay = 0.15f;
};
