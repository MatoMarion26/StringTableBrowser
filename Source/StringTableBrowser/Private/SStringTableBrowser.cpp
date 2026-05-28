// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#include "SStringTableBrowser.h"

#include "AssetManagerEditorModule.h"
#include "Editor.h"
#include "StringTableBrowserHelpers.h"
#include "StringTableBrowserSettings.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "StringTableBrowser"

// -------------------------------------------------------------------------
// SStringTableBrowserRow — private row widget for the main browser
// -------------------------------------------------------------------------

class SStringTableBrowserRow : public SMultiColumnTableRow<TSharedPtr<FStringTableBrowserEntry>>
{
public:

	SLATE_BEGIN_ARGS(SStringTableBrowserRow) {}
		SLATE_ARGUMENT(TSharedPtr<FStringTableBrowserEntry>, Item)
		SLATE_EVENT(FOnClicked, OnEditClicked)
		SLATE_EVENT(FOnClicked, OnCopyKeyClicked)
		SLATE_EVENT(FOnClicked, OnViewReferencesClicked)
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		const TSharedRef<STableViewBase>& InOwnerTableView
	)
	{
		Item = InArgs._Item;
		OnEditClicked = InArgs._OnEditClicked;
		OnCopyKeyClicked = InArgs._OnCopyKeyClicked;
		OnViewReferencesClicked = InArgs._OnViewReferencesClicked;

		SMultiColumnTableRow<TSharedPtr<FStringTableBrowserEntry>>::Construct(
			FSuperRowType::FArguments(), InOwnerTableView
		);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == StringTableBrowserColumns::Key)
		{
			return SNew(SBox).Padding(5.0f)
				[ SNew(STextBlock).Text(FText::FromString(Item->Key)) ];
		}

		if (ColumnName == StringTableBrowserColumns::Value)
		{
			return SNew(SBox).Padding(5.0f)
				[ SNew(STextBlock).Text(FText::FromString(Item->Value)) ];
		}

		if (ColumnName == StringTableBrowserColumns::Source)
		{
			return SNew(SBox).Padding(5.0f)
				[ SNew(STextBlock).Text(FText::FromName(Item->TableId)) ];
		}

		if (ColumnName == StringTableBrowserColumns::Actions)
		{
			return SNew(SBox).Padding(2.0f)
			[
				SNew(SHorizontalBox)

				// Edit Table — opens the asset editor for the source string table
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 2.0f, 0.0f)
				[
					FStringTableBrowserHelpers::MakeIconButton(
						OnEditClicked,
						StringTableBrowserIcons::Edit,
						LOCTEXT("EditBtnTooltip", "Open this String Table in the asset editor.")
					)
				]

				// Copy Key — copies the LOCTABLE() reference to the clipboard
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 2.0f, 0.0f)
				[
					FStringTableBrowserHelpers::MakeIconButton(
						OnCopyKeyClicked,
						StringTableBrowserIcons::Copy,
						LOCTEXT("CopyKeyBtnTooltip","Copy the full LOCTABLE() reference to the clipboard.")
					)
				]

				// View References — opens Unreal's native Reference Viewer for the source asset
				+ SHorizontalBox::Slot().AutoWidth()
				[
					FStringTableBrowserHelpers::MakeIconButton(
						OnViewReferencesClicked,
						StringTableBrowserIcons::FindReferences,
						LOCTEXT("ViewRefsBtnTooltip",
							"Open the Reference Viewer for the source String Table asset.\n"
							"Shows all assets that reference or are referenced by this table."
						)
					)
				]
			];
		}

		return SNullWidget::NullWidget;
	}

private:

	TSharedPtr<FStringTableBrowserEntry> Item;
	FOnClicked OnEditClicked;
	FOnClicked OnCopyKeyClicked;
	FOnClicked OnViewReferencesClicked;
};

// -------------------------------------------------------------------------
// SStringTableBrowser
// -------------------------------------------------------------------------

void SStringTableBrowser::Construct(const FArguments& InArgs)
{
	CurrentSortColumn = StringTableBrowserColumns::Source;
	CurrentSortMode = EColumnSortMode::Ascending;

	// ---- Header row ---------------------------------------------------------

	TSharedRef<SHeaderRow> HeaderRow = SNew(SHeaderRow)

		+ SHeaderRow::Column(StringTableBrowserColumns::Key)
		  .DefaultLabel(LOCTEXT("KeyCol", "Key"))
		  .FillWidth(0.25f)
		  .SortMode(this, &SStringTableBrowser::GetColumnSortMode, StringTableBrowserColumns::Key)
		  .OnSort(this, &SStringTableBrowser::OnSortColumnHeader)

		+ SHeaderRow::Column(StringTableBrowserColumns::Value)
		  .DefaultLabel(LOCTEXT("ValCol", "Value"))
		  .FillWidth(0.4f)
		  .SortMode(this, &SStringTableBrowser::GetColumnSortMode, StringTableBrowserColumns::Value)
		  .OnSort(this, &SStringTableBrowser::OnSortColumnHeader)

		+ SHeaderRow::Column(StringTableBrowserColumns::Source)
		  .DefaultLabel(LOCTEXT("SourceCol", "Source String Table"))
		  .FillWidth(0.25f)
		  .SortMode(this, &SStringTableBrowser::GetColumnSortMode, StringTableBrowserColumns::Source)
		  .OnSort(this, &SStringTableBrowser::OnSortColumnHeader)

		+ SHeaderRow::Column(StringTableBrowserColumns::Actions)
		  .DefaultLabel(LOCTEXT("ActionCol", "Actions"))
		  .FixedWidth(110.0f);

	// ---- Widget tree --------------------------------------------------------

	ChildSlot
	[
		SNew(SVerticalBox)

		// Row 1 — search box + match mode toggles + cache rebuild button
		+ SVerticalBox::Slot()
		  .AutoHeight()
		  .Padding(5.0f, 5.0f, 5.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 10.0f, 0.0f)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Search..."))
				.OnTextChanged(this, &SStringTableBrowser::OnSearchTextChanged)
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f)
			[
				FStringTableBrowserHelpers::MakeFilterCheckBox(
					LOCTEXT("MatchCase", "Match Case"),
					LOCTEXT("MatchCaseTooltip",
						"When enabled, the search is case-sensitive.\n"
						"For example, \"hello\" will not match \"Hello\"."),
					false,
					[this](bool b)
					{
						Filter.bMatchCase = b;
						ApplyFilterAndSort();
					})
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f)
			[
				FStringTableBrowserHelpers::MakeFilterCheckBox(
					LOCTEXT("WholeWord", "Whole Word"),
					LOCTEXT("WholeWordTooltip",
						"When enabled, only complete words are matched.\n"
						"For example, \"table\" will not match \"StringTable\"."),
					false,
					[this](bool b)
					{
						Filter.bWholeWord = b;
						ApplyFilterAndSort();
					})
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f)
			[
				FStringTableBrowserHelpers::MakeFilterCheckBox(
					LOCTEXT("Regex", "Regex"),
					LOCTEXT("RegexTooltip",
						"When enabled, the search input is treated as a regular expression (ICU syntax).\n"
						"For example: \"^Hello\" matches entries that start with \"Hello\"."),
					false,
					[this](bool b)
						{
							Filter.bRegex = b;
							ApplyFilterAndSort();
						}
					)
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Force Load String Tables"))
				.ToolTipText(LOCTEXT("RefreshTooltip",
						"Discard the current cache, load and re-scan all String Table assets in the project.\n"
						"Use this after syncing changes from version control."
					)
				)
				.OnClicked_Lambda(
					[]()
					{
						if (auto* const Module = FStringTableBrowserModule::GetModulePtr())
						{
							Module->ForceRebuildCache();
						}
						return FReply::Handled();
					}
				)
			]
		]

		// Row 2 — search scope toggles
		+ SVerticalBox::Slot()
		  .AutoHeight()
		  .Padding(5.0f, 0.0f, 5.0f, 5.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SearchIn", "Search in:"))
				.ToolTipText(LOCTEXT("SearchInTooltip",
					"Choose which fields are included when matching your search term.\n"
					"If none are selected, all entries are shown."))
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f)
			[
				FStringTableBrowserHelpers::MakeFilterCheckBox(
					LOCTEXT("ScopeKeys", "Keys"),
					LOCTEXT("ScopeKeysTooltip",
						"Include the entry Key in the search.\n"
						"The key is the unique identifier used to reference a string in code,\n"
						"e.g. \"MAIN_MENU_TITLE\"."),
					false,
					[this](bool b)
					{
						Filter.bSearchKeys = b;
						ApplyFilterAndSort();
					})
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f)
			[
				FStringTableBrowserHelpers::MakeFilterCheckBox(
					LOCTEXT("ScopeValues", "Values"),
					LOCTEXT("ScopeValuesTooltip",
						"Include the entry Value in the search.\n"
						"The value is the human-readable string displayed in game,\n"
						"e.g. \"Start Game\"."),
					true, // on by default
					[this](bool b)
					{
						Filter.bSearchValues = b;
						ApplyFilterAndSort();
					})
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f)
			[
				FStringTableBrowserHelpers::MakeFilterCheckBox(
					LOCTEXT("ScopeTable", "String Tables"),
					LOCTEXT("ScopeTableTooltip",
						"Include the source String Table name in the search.\n"
						"Useful for finding all entries from a specific table, e.g. \"UI_Strings\"."),
					false,
					[this](bool b)
					{
						Filter.bSearchTables = b;
						ApplyFilterAndSort();
					})
			]
		]

		// Results list view
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SOverlay)

			// The list view — always present, hidden content when empty
			+ SOverlay::Slot()
			[
				SAssignNew(ListView, SListView<TSharedPtr<FStringTableBrowserEntry>>)
				.ListItemsSource(&FilteredEntries)
				.OnGenerateRow(this, &SStringTableBrowser::GenerateRow)
				.HeaderRow(HeaderRow)
			]

			// Empty state message — visible only when the list has no results
			// and the user has typed a search term
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoResults", "No entries match your search."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility_Lambda([this]()
				{
					return (!Filter.SearchText.IsEmpty() && FilteredEntries.IsEmpty())
						? EVisibility::HitTestInvisible
						: EVisibility::Collapsed;
				})
			]
		]
	];

	// Subscribe to cache updates so the results list stays fresh while open
	if (auto* const Module = FStringTableBrowserModule::GetModulePtr())
	{
		Module->OnCacheUpdated.AddSP(this, &SStringTableBrowser::UpdateFromCache);
	}

	UpdateFromCache();
}

SStringTableBrowser::~SStringTableBrowser()
{
	if (auto* const Module = FStringTableBrowserModule::GetModulePtr())
	{
		Module->OnCacheUpdated.RemoveAll(this);
	}
}

// -------------------------------------------------------------------------
// Row generation
// -------------------------------------------------------------------------

TSharedRef<ITableRow> SStringTableBrowser::GenerateRow(
	TSharedPtr<FStringTableBrowserEntry> Item,
	const TSharedRef<STableViewBase>& OwnerTable
)
{
	return SNew(SStringTableBrowserRow, OwnerTable)
		.Item(Item)
		.OnEditClicked(this, &SStringTableBrowser::OnEditStringTableClicked,  Item->AssetPath)
		.OnCopyKeyClicked(this, &SStringTableBrowser::OnCopyKeyClicked, Item)
		.OnViewReferencesClicked(this, &SStringTableBrowser::OnViewReferencesClicked, Item->AssetPath);
}

// -------------------------------------------------------------------------
// Data management
// -------------------------------------------------------------------------

void SStringTableBrowser::UpdateFromCache()
{
	ApplyFilterAndSort();
}

void SStringTableBrowser::ApplyFilterAndSort()
{
	FilteredEntries.Reset();

	auto* const Module = FStringTableBrowserModule::GetModulePtr();
	if (Module == nullptr)
	{
		return;
	}
	
	TArray<TSharedPtr<FStringTableBrowserEntry>> AllEntries = Module->GetCachedEntriesCopy();

	if (Filter.SearchText.IsEmpty())
	{
		// Empty search shows all entries in the main browser
		FilteredEntries = MoveTemp(AllEntries);
	}
	else
	{
		for (const TSharedPtr<FStringTableBrowserEntry>& Entry : AllEntries)
		{
			if (Filter.PassesFilter(Entry))
			{
				FilteredEntries.Add(Entry);
			}
		}
	}

	SortData();
}

// -------------------------------------------------------------------------
// Debounced search
// -------------------------------------------------------------------------

void SStringTableBrowser::OnSearchTextChanged(const FText& InFilterText)
{
	Filter.SearchText = InFilterText.ToString();
	Filter.Compile();

	// Cancel any in-flight debounce timer and start a fresh one.
	// UnRegisterActiveTimer is safe to call with a stale/expired handle.
	if (TSharedPtr<FActiveTimerHandle> ExistingTimer = SearchDebounceTimerHandle.Pin())
	{
		UnRegisterActiveTimer(ExistingTimer.ToSharedRef());
	}

	// RegisterActiveTimer schedules OnSearchDebounceTimer to fire once after
	// SearchDebounceDelay seconds. The returned handle lets us cancel it if the
	// user types again before the interval elapses.
	SearchDebounceTimerHandle = RegisterActiveTimer(
		UStringTableBrowserSettings::Get()->SearchDebounceDelay,
		FWidgetActiveTimerDelegate::CreateSP(this, &SStringTableBrowser::OnSearchDebounceTimer)
	);
}

EActiveTimerReturnType SStringTableBrowser::OnSearchDebounceTimer(
	double /*InCurrentTime*/,
	float  /*InDeltaTime*/)
{
	ApplyFilterAndSort();

	// Return Stop so Slate removes this timer automatically after it fires.
	// No explicit unregister needed — the handle becomes stale on its own.
	return EActiveTimerReturnType::Stop;
}

// -------------------------------------------------------------------------
// Sorting
// -------------------------------------------------------------------------

void SStringTableBrowser::SortData()
{
	FilteredEntries.Sort(
		[this](const TSharedPtr<FStringTableBrowserEntry>& A, const TSharedPtr<FStringTableBrowserEntry>& B)
		{
			int32 Result = 0;

			if (CurrentSortColumn == StringTableBrowserColumns::Key)
				Result = A->Key.Compare(B->Key);
			else if (CurrentSortColumn == StringTableBrowserColumns::Value)
				Result = A->Value.Compare(B->Value);
			else if (CurrentSortColumn == StringTableBrowserColumns::Source)
				Result = A->TableId.ToString().Compare(B->TableId.ToString());

			return CurrentSortMode == EColumnSortMode::Ascending ? (Result < 0) : (Result > 0);
		}
	);

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SStringTableBrowser::OnSortColumnHeader(
	EColumnSortPriority::Type /*SortPriority*/,
	const FName& ColumnId,
	EColumnSortMode::Type NewSortMode
)
{
	CurrentSortColumn = ColumnId;
	CurrentSortMode = NewSortMode;
	ApplyFilterAndSort();
}

EColumnSortMode::Type SStringTableBrowser::GetColumnSortMode(const FName ColumnId) const
{
	return CurrentSortColumn == ColumnId ? CurrentSortMode : EColumnSortMode::None;
}

// -------------------------------------------------------------------------
// UI callbacks
// -------------------------------------------------------------------------

FReply SStringTableBrowser::OnCopyKeyClicked(TSharedPtr<FStringTableBrowserEntry> Item) const
{
	FStringTableBrowserHelpers::CopyStringTableEntry(Item);
	return FReply::Handled();
}

FReply SStringTableBrowser::OnEditStringTableClicked(FSoftObjectPath AssetPath)
{
	FStringTableBrowserHelpers::OpenStringTableAsset(AssetPath);
	return FReply::Handled();
}

FReply SStringTableBrowser::OnViewReferencesClicked(FSoftObjectPath AssetPath) const
{
	// Resolve the package name from the asset path and open Unreal's native
	// Reference Viewer. This shows the full asset dependency graph for the
	// source string table — useful for finding where a table is used.
	const FName PackageName(*FPackageName::ObjectPathToPackageName(AssetPath.ToString()));

	if (IAssetManagerEditorModule::IsAvailable())
	{
		IAssetManagerEditorModule& AssetManagerEditorModule =
			IAssetManagerEditorModule::Get();

		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Add(FAssetIdentifier(PackageName));
		AssetManagerEditorModule.OpenReferenceViewerUI(AssetIdentifiers);
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
