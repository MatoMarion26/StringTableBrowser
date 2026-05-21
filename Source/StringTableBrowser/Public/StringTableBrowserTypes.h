// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/SoftObjectPath.h"

// -------------------------------------------------------------------------
// CONSTANTS
// -------------------------------------------------------------------------

namespace StringTableBrowserColumns
{
	inline const FName Key = "Key";
	inline const FName Value = "Value";
	inline const FName Source = "Source";
	inline const FName Actions = "Actions";
}

namespace StringTableBrowserIcons
{
	inline const FName OpenBrowserSearch = "Icons.Search";
	inline const FName FindReferences = "Icons.Find";
	inline const FName Edit = "Icons.Edit";
	inline const FName Check = "Icons.Check";
	inline const FName Copy = "Icons.Clipboard";
	inline const FName OpenBrowserWindow = "Icons.OpenInExternalEditor";
}


// -------------------------------------------------------------------------
// STRUCTS
// -------------------------------------------------------------------------

/**
 * FStringTableBrowserEntry
 *
 * A single row of data displayed in the browser and picker.
 * AssetPath is stored as a soft reference so string table assets are not
 * kept loaded in memory just because they appear in the cache.
 */
struct FStringTableBrowserEntry
{
	/** Short asset name used as the string table namespace identifier. */
	FName TableId;

	/** Soft path to the owning UStringTable asset. Loaded on demand only. */
	FSoftObjectPath AssetPath;

	/** The localisation key, e.g. "MAIN_MENU_START". */
	FString Key;

	/** The source string value, e.g. "Start Game". */
	FString Value;
};