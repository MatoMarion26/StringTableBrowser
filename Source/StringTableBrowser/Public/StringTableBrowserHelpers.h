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
	// CopyStringTableEntry — shared helper for copying a String Table Entry key
	// -------------------------------------------------------------------------

	/**
	 * Copies the TableId/Key pair to the Clipboard in a usable way to paste on FText properties.
	 */
	static void CopyStringTableEntry(TSharedPtr<FStringTableBrowserEntry> Item);
};
