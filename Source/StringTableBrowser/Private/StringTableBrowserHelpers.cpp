// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#include "StringTableBrowserHelpers.h"

#include "StringTableBrowserTypes.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"

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
