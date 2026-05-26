// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StringTableSearchFilter.h"
#include "StringTableBrowserModule.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

/**
 * Delegate fired when the user clicks Apply on a result row.
 * TableId — the string table's namespace identifier (FName of the asset name).
 * Key — the entry key within that table.
 */
DECLARE_DELEGATE_TwoParams(FOnStringTableEntryPicked, FName /*TableId*/, FString /*Key*/);

/**
 * SStringTablePickerDropdown
 *
 * Compact search-and-pick widget displayed as a dropdown anchored below the
 * search-icon button injected by FTextStringTableDetailCustomization into
 * every FText property row in the Details panel.
 *
 * The results list starts empty and populates as the user types. Clicking
 * the Apply button on a result row fires OnEntryPicked, which the
 * customization uses to bind the FText property via FText::FromStringTable().
 *
 * Also accepts an optional InitialSearchText and OnSearchTextChanged delegate
 * so the owner can pre-populate the search box and persist the last term.
 */
class SStringTableBrowserPickerDropdown : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SStringTableBrowserPickerDropdown) {}
		SLATE_EVENT(FOnStringTableEntryPicked, OnEntryPicked)
		SLATE_ARGUMENT(FString, InitialSearchText)
		SLATE_EVENT(TDelegate<void(const FString&)>, OnSearchTextChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SStringTableBrowserPickerDropdown() override;

	/** Moves keyboard focus into the search box. Call immediately after PushMenu(). */
	void FocusSearchBox();

private:

	// -------------------------------------------------------------------------
	// Row generation
	// -------------------------------------------------------------------------

	TSharedRef<ITableRow> GenerateRow(
		TSharedPtr<FStringTableBrowserEntry> Item,
		const TSharedRef<STableViewBase>& OwnerTable
	);

	// -------------------------------------------------------------------------
	// Event handlers
	// -------------------------------------------------------------------------

	void OnSearchTextChangedInternal(const FText& InText);
	void OnCacheUpdated();
	FReply OnApplyClicked(TSharedPtr<FStringTableBrowserEntry> Item) const;
	FReply OnCopyKeyClicked(TSharedPtr<FStringTableBrowserEntry> Item) const;
	FReply OnEditClicked(FSoftObjectPath AssetPath) const;

	// -------------------------------------------------------------------------
	// Filter
	// -------------------------------------------------------------------------

	void ApplyFilter();

	// -------------------------------------------------------------------------
	// Data
	// -------------------------------------------------------------------------

	TArray<TSharedPtr<FStringTableBrowserEntry>> FilteredEntries;
	TSharedPtr<SListView<TSharedPtr<FStringTableBrowserEntry>>> ListView;
	TSharedPtr<SSearchBox> SearchBox;

	FStringTableSearchFilter Filter;
	FOnStringTableEntryPicked OnEntryPicked;
	TDelegate<void(const FString&)> OnSearchTextChangedDelegate;
};
