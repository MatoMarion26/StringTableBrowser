// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#include "StringTableBrowserHelpers.h"

#include "SStringTableBrowserPickerDropdown.h"
#include "StringTableBrowserTypes.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FStringTableBrowserHelpers"

TSharedRef<SWidget> FStringTableBrowserHelpers::MakeFilterCheckBox(
	const FText& Label,
	const FText& Tooltip,
	bool bInitiallyChecked,
	TFunction<void(bool)> OnChanged
)
{
	return SNew(SCheckBox)
		.IsChecked(bInitiallyChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
		.ToolTipText(Tooltip)
		.OnCheckStateChanged_Lambda([OnChanged](ECheckBoxState NewState)
		{
			OnChanged(NewState == ECheckBoxState::Checked);
		})
		[ SNew(STextBlock).Text(Label) ];
}

TSharedRef<SWidget> FStringTableBrowserHelpers::MakeIconButton(
	const FOnClicked& OnClicked,
	const FName& BrushName,
	const FText& Tooltip
)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ToolTipText(Tooltip)
		.OnClicked(OnClicked)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.ContentPadding(FMargin(4.0f, 2.0f))
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush(BrushName))
			.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
		];
};

void FStringTableBrowserHelpers::CopyStringTableEntry(TSharedPtr<FStringTableBrowserEntry> Item)
{
	if (Item.IsValid())
	{
		// AssetPath is intentional here — FStringTableRegistry keys tables by their
		// full object path (/Game/Localization/UI_Strings.UI_Strings), which is what
		// LOCTABLE() and FText::FromStringTable resolve against at runtime.
		// TableId (the short asset name) would produce an unresolvable reference.
		const FString Reference = FString::Printf(
			TEXT("LOCTABLE(\"%s\", \"%s\")"),
			*Item->AssetPath.ToString(),
			*Item->Key);

		FPlatformApplicationMisc::ClipboardCopy(*Reference);
	}
}

void FStringTableBrowserHelpers::OpenStringTableAsset(const FSoftObjectPath& AssetPath)
{
	if (UObject* LoadedAsset = AssetPath.TryLoad())
	{
		if (UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			EditorSubsystem->OpenEditorForAsset(LoadedAsset);
		}
	}
}

void FStringTableBrowserHelpers::OpenPickerDropdown(
	TSharedPtr<IPropertyHandle> PropertyHandle,
	TSharedPtr<FString> LastSearchText
)
{
	if (LastSearchText->IsEmpty())
	{
		FText CurrentValue;
		PropertyHandle->GetValue(CurrentValue);
		*LastSearchText = CurrentValue.ToString();
	}

	TSharedRef<SStringTableBrowserPickerDropdown> Dropdown =
		SNew(SStringTableBrowserPickerDropdown)
		.InitialSearchText(*LastSearchText)
		.OnSearchTextChanged_Lambda([LastSearchText](const FString& NewText)
		{
			*LastSearchText = NewText;
		})
		.OnEntryPicked_Lambda([PropertyHandle](FName TableId, FString Key)
		{
			const FScopedTransaction Transaction(
				LOCTEXT("SetStringTableReference", "Set String Table Reference"));

			TArray<UObject*> OuterObjects;
			PropertyHandle->GetOuterObjects(OuterObjects);
			for (UObject* Object : OuterObjects)
			{
				if (Object) { Object->Modify(); }
			}

			const FText NewValue = FText::FromStringTable(TableId, Key);
			PropertyHandle->SetValue(NewValue, EPropertyValueSetFlags::DefaultFlags);
		});

	const FVector2D SpawnLocation =
		FSlateApplication::Get().GetCursorPos() + FVector2D(0.0f, 20.0f);

	FSlateApplication::Get().PushMenu(
		FSlateApplication::Get().GetActiveTopLevelWindow().ToSharedRef(),
		FWidgetPath(),
		Dropdown,
		SpawnLocation,
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));

	Dropdown->FocusSearchBox();
}

#undef LOCTEXT_NAMESPACE