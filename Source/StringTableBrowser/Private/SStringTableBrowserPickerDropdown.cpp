// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#include "SStringTableBrowserPickerDropdown.h"

#include "StringTableBrowserHelpers.h"
#include "StringTableBrowserModule.h"
#include "StringTableBrowserTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "StringTablePickerDropdown"

// -------------------------------------------------------------------------
// SPickerRow — private row widget for the picker dropdown
// -------------------------------------------------------------------------

class SPickerRow : public SMultiColumnTableRow<TSharedPtr<FStringTableBrowserEntry>>
{
public:

	SLATE_BEGIN_ARGS(SPickerRow) {}
		SLATE_ARGUMENT(TSharedPtr<FStringTableBrowserEntry>, Item)
		SLATE_EVENT(FOnClicked, OnApplyClicked)
		SLATE_EVENT(FOnClicked, OnCopyKeyClicked)
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		const TSharedRef<STableViewBase>& InOwnerTable
	)
	{
		Item = InArgs._Item;
		OnApplyClicked = InArgs._OnApplyClicked;
		OnCopyKeyClicked = InArgs._OnCopyKeyClicked;

		SMultiColumnTableRow<TSharedPtr<FStringTableBrowserEntry>>::Construct(
			FSuperRowType::FArguments(), InOwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == StringTableBrowserColumns::Key)
		{
			return SNew(SBox).Padding(4.0f)
				[ SNew(STextBlock).Text(FText::FromString(Item->Key)) ];
		}

		if (ColumnName == StringTableBrowserColumns::Value)
		{
			return SNew(SBox).Padding(4.0f)
				[ SNew(STextBlock).Text(FText::FromString(Item->Value)) ];
		}

		if (ColumnName == StringTableBrowserColumns::Source)
		{
			return SNew(SBox).Padding(4.0f)
				[ SNew(STextBlock).Text(FText::FromName(Item->TableId)) ];
		}

		if (ColumnName == StringTableBrowserColumns::Actions)
		{
			return SNew(SHorizontalBox)
			// Apply buton
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("ApplyBtnTooltip",
					"Bind the FText property to this entry as a string table reference.\n"
					"Equivalent to LOCTABLE(\"TablePath\", \"Key\")."))
				.OnClicked(OnApplyClicked)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(StringTableBrowserIcons::Check))
					.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
				]
			]
			// Copy button 
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("CopyKeyBtnTooltip",
					"Copy the full LOCTABLE() reference to the clipboard."))
				.OnClicked(OnCopyKeyClicked)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ContentPadding(FMargin(4.0f, 2.0f))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(StringTableBrowserIcons::Copy))
					.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
				]
			];
		}

		return SNullWidget::NullWidget;
	}

private:

	TSharedPtr<FStringTableBrowserEntry> Item;
	FOnClicked OnApplyClicked;
	FOnClicked OnCopyKeyClicked;
};

// -------------------------------------------------------------------------
// SStringTableBrowserPickerDropdown
// -------------------------------------------------------------------------

void SStringTableBrowserPickerDropdown::Construct(const FArguments& InArgs)
{
	OnEntryPicked = InArgs._OnEntryPicked;
	OnSearchTextChangedDelegate = InArgs._OnSearchTextChanged;

	// Scope defaults: values only, matching the main viewer's default
	Filter.bSearchValues = true;
	Filter.bSearchKeys = false;
	Filter.bSearchTables = false;
	
	// ---- Header row ---------------------------------------------------------

	TSharedRef<SHeaderRow> HeaderRow = SNew(SHeaderRow)
		+ SHeaderRow::Column(StringTableBrowserColumns::Key)
		  .DefaultLabel(LOCTEXT("KeyCol", "Key"))
		  .FillWidth(0.28f)
		+ SHeaderRow::Column(StringTableBrowserColumns::Value)
		  .DefaultLabel(LOCTEXT("ValCol", "Value"))
		  .FillWidth(0.42f)
		+ SHeaderRow::Column(StringTableBrowserColumns::Source)
		  .DefaultLabel(LOCTEXT("SourceCol", "Table"))
		  .FillWidth(0.20f)
		+ SHeaderRow::Column(StringTableBrowserColumns::Actions)
		  .DefaultLabel(LOCTEXT("ActionsCol", "Actions"))
		  .FixedWidth(60.0f);

	// ---- Widget tree --------------------------------------------------------

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(700.0f)
		.HeightOverride(380.0f)
		[
			SNew(SVerticalBox)

			// Search box
			+ SVerticalBox::Slot()
			  .AutoHeight()
			  .Padding(6.0f, 6.0f, 6.0f, 2.0f)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Search string tables..."))
				.OnTextChanged(this, &SStringTableBrowserPickerDropdown::OnSearchTextChangedInternal)
			]

			// Match mode toggles
			+ SVerticalBox::Slot()
			  .AutoHeight()
			  .Padding(6.0f, 2.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					FStringTableBrowserHelpers::MakeFilterCheckBox(
						LOCTEXT("MatchCase", "Match Case"),
						LOCTEXT("MatchCaseTooltip", "When enabled, the search is case-sensitive."),
						false,
						[this](bool b) { Filter.bMatchCase = b; })
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					FStringTableBrowserHelpers::MakeFilterCheckBox(
						LOCTEXT("WholeWord", "Whole Word"),
						LOCTEXT("WholeWordTooltip",
							"When enabled, only complete words are matched.\n"
							"For example, \"table\" will not match \"StringTable\"."),
						false,
						[this](bool b) { Filter.bWholeWord = b; })
				]

				+ SHorizontalBox::Slot().AutoWidth()
				[
					FStringTableBrowserHelpers::MakeFilterCheckBox(
						LOCTEXT("Regex", "Regex"),
						LOCTEXT("RegexTooltip",
							"Treat the search input as a regular expression (ICU syntax)."),
						false,
						[this](bool b) { Filter.bRegex = b; })
				]
			]

			// Scope toggles
			+ SVerticalBox::Slot()
			  .AutoHeight()
			  .Padding(6.0f, 0.0f, 6.0f, 4.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SearchIn", "Search in:"))
					.ToolTipText(LOCTEXT("SearchInTooltip",
						"Choose which fields are included when matching.\n"
						"If none are selected, all entries are shown."))
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					FStringTableBrowserHelpers::MakeFilterCheckBox(
						LOCTEXT("ScopeKeys", "Keys"),
						LOCTEXT("ScopeKeysTooltip",
							"Include the entry Key in the search.\n"
							"The key is the unique identifier used to reference a string in code."),
						false,
						[this](bool b) { Filter.bSearchKeys = b; })
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					FStringTableBrowserHelpers::MakeFilterCheckBox(
						LOCTEXT("ScopeValues", "Values"),
						LOCTEXT("ScopeValuesTooltip",
							"Include the entry Value in the search.\n"
							"The value is the human-readable string displayed in game."),
						true, // on by default
						[this](bool b) { Filter.bSearchValues = b; })
				]

				+ SHorizontalBox::Slot().AutoWidth()
				[
					FStringTableBrowserHelpers::MakeFilterCheckBox(
						LOCTEXT("ScopeTable", "String Tables"),
						LOCTEXT("ScopeTableTooltip",
							"Include the source String Table name in the search."),
						false,
						[this](bool b) { Filter.bSearchTables = b; })
				]
			]

			// Divider
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSeparator).Orientation(Orient_Horizontal)
			]

			// Results list
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				[
					SAssignNew(ListView, SListView<TSharedPtr<FStringTableBrowserEntry>>)
					.ListItemsSource(&FilteredEntries)
					.OnGenerateRow(this, &SStringTableBrowserPickerDropdown::GenerateRow)
					.HeaderRow(HeaderRow)
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return Filter.SearchText.IsEmpty()
							? LOCTEXT("EmptySearch",  "Type to search all string table entries.")
							: LOCTEXT("NoResults",    "No entries match your search.");
					})
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Visibility_Lambda([this]()
					{
						return FilteredEntries.IsEmpty()
							? EVisibility::HitTestInvisible
							: EVisibility::Collapsed;
					})
				]
			]

			// Footer — hint text + Open Viewer shortcut
			+ SVerticalBox::Slot()
			  .AutoHeight()
			  .Padding(6.0f, 4.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Hint",
						"Type to search. Click \u2713 to bind the FText property to the selected entry."))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("OpenBrowserTooltip",
						"Open the String Table Browser panel for advanced browsing and searching."))
					.ContentPadding(FMargin(4.0f, 2.0f))
					.OnClicked_Lambda([]()
					{
						FGlobalTabmanager::Get()->TryInvokeTab(FName("StringTableBrowser"));
						FSlateApplication::Get().DismissAllMenus();
						return FReply::Handled();
					})
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush(StringTableBrowserIcons::OpenBrowserWindow))
							.DesiredSizeOverride(FVector2D(14.0f, 14.0f))
						]

						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("OpenBrowserBtn", "Open String Table Browser"))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
				]
			]
		]
	];

	// Subscribe to cache updates so the results list stays fresh while open
	if (auto* const Module = FStringTableBrowserModule::GetModulePtr())
	{
		Module->OnCacheUpdated.AddSP(this, &SStringTableBrowserPickerDropdown::OnCacheUpdated);
	}

	// Pre-populate the search box if an initial value was provided
	if (!InArgs._InitialSearchText.IsEmpty())
	{
		Filter.SearchText = InArgs._InitialSearchText;
		Filter.Compile();
		ApplyFilter();

		if (SearchBox.IsValid())
		{
			SearchBox->SetText(FText::FromString(InArgs._InitialSearchText));
		}
	}
}

SStringTableBrowserPickerDropdown::~SStringTableBrowserPickerDropdown()
{
	if (auto* const Module = FStringTableBrowserModule::GetModulePtr())
	{
		Module->OnCacheUpdated.RemoveAll(this);
	}
}

// -------------------------------------------------------------------------
// Public
// -------------------------------------------------------------------------

void SStringTableBrowserPickerDropdown::FocusSearchBox()
{
	if (SearchBox.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(SearchBox, EFocusCause::SetDirectly);
	}
}

// -------------------------------------------------------------------------
// Row generation
// -------------------------------------------------------------------------

TSharedRef<ITableRow> SStringTableBrowserPickerDropdown::GenerateRow(
	TSharedPtr<FStringTableBrowserEntry> Item,
	const TSharedRef<STableViewBase>& OwnerTable
)
{
	return SNew(SPickerRow, OwnerTable)
		.Item(Item)
		.OnApplyClicked(this, &SStringTableBrowserPickerDropdown::OnApplyClicked, Item)
		.OnCopyKeyClicked(this,  &SStringTableBrowserPickerDropdown::OnCopyKeyClicked, Item);
}

// -------------------------------------------------------------------------
// Event handlers
// -------------------------------------------------------------------------

void SStringTableBrowserPickerDropdown::OnSearchTextChangedInternal(const FText& InText)
{
	Filter.SearchText = InText.ToString();
	Filter.Compile();
	ApplyFilter();

	if (OnSearchTextChangedDelegate.IsBound())
	{
		OnSearchTextChangedDelegate.Execute(Filter.SearchText);
	}
}

void SStringTableBrowserPickerDropdown::OnCacheUpdated()
{
	ApplyFilter();
}

FReply SStringTableBrowserPickerDropdown::OnApplyClicked(TSharedPtr<FStringTableBrowserEntry> Item) const
{
	if (Item.IsValid() && OnEntryPicked.IsBound())
	{
		// Derive the table ID from the full asset path so it matches what
		// FStringTableRegistry uses as its key for FText::FromStringTable().
		const FName TableIdFromPath(*Item->AssetPath.GetAssetPathString());
		OnEntryPicked.Execute(TableIdFromPath, Item->Key);
	}

	FSlateApplication::Get().DismissAllMenus();
	return FReply::Handled();
}

FReply SStringTableBrowserPickerDropdown::OnCopyKeyClicked(TSharedPtr<FStringTableBrowserEntry> Item) const
{
	FStringTableBrowserHelpers::CopyStringTableEntry(Item);
	return FReply::Handled();
}

// -------------------------------------------------------------------------
// Filter
// -------------------------------------------------------------------------

void SStringTableBrowserPickerDropdown::ApplyFilter()
{
	FilteredEntries.Empty();

	// Empty search intentionally shows nothing — the list is populated only
	// once the user has typed something, keeping the dropdown uncluttered on open.
	if (Filter.SearchText.IsEmpty())
	{
		if (ListView.IsValid())
		{
			ListView->RequestListRefresh();
		}
		return;
	}

	if (const auto* const Module = FStringTableBrowserModule::GetModulePtr())
	{
		for (const TSharedPtr<FStringTableBrowserEntry>& Entry : Module->GetCachedEntriesCopy())
		{
			if (Filter.PassesFilter(Entry))
			{
				FilteredEntries.Add(Entry);
			}
		}
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
