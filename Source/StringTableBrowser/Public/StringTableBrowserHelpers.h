// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Collection of helper static functions re-used across the plugin code.
 */

class SWidget;
struct FStringTableBrowserEntry;

class FStringTableBrowserHelpers
{
public:
	// -------------------------------------------------------------------------
	// MakeFilterCheckBox — shared helper for building labelled toggle checkboxes
	// -------------------------------------------------------------------------

	/**
	 * Creates a checkbox with a label and tooltip whose state is forwarded to
	 * the caller via a TFunction. Used for both mode and scope toggles.
	 */
	static TSharedRef<SWidget> MakeFilterCheckBox(
		const FText& Label,
		const FText& Tooltip,
		bool bInitiallyChecked,
		TFunction<void(bool)> OnChanged
	);
	
	// -------------------------------------------------------------------------
	// MakeIconButton — shared helper for building action icons
	// -------------------------------------------------------------------------

	/**
	 * Creates a button with an icon and tooltip whose state is forwarded to
	 * the caller via a TFunction. Used for actions on the browser and the picker.
	 */

	static TSharedRef<SWidget> MakeIconButton(
		const FOnClicked& OnClicked,
		const FName& BrushName,
		const FText& Tooltip
	);

	// -------------------------------------------------------------------------
	// CopyStringTableEntry — shared helper for copying a String Table Entry key
	// -------------------------------------------------------------------------

	/**
	 * Copies the TableId/Key pair to the Clipboard in a usable way to paste on FText properties.
	 */
	static void CopyStringTableEntry(TSharedPtr<FStringTableBrowserEntry> Item);

	// -------------------------------------------------------------------------
	// OpenStringTableAsset — shared helper for opening the provided String Table Asset
	// -------------------------------------------------------------------------

	/**
	 * Usses the provided Asset Path to try and open it with the Asset Manager.
	 */
	static void OpenStringTableAsset(const FSoftObjectPath& AssetPath);
};
