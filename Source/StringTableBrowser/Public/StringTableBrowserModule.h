// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"
#include "PropertyEditorDelegates.h"
#include "StringTableBrowserTypes.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/TimerHandle.h"

DECLARE_LOG_CATEGORY_EXTERN(LogStringTableBrowser, Log, All);

/**
 * Bump this constant whenever the disk-cache JSON schema changes.
 * A mismatch between the stored version and this value causes the cache
 * to be discarded and fully rebuilt on the next editor launch.
 */
static constexpr int32 GStringTableBrowserCacheVersion = 2;

/**
 * FStringTableBrowserModule
 *
 * Core module of the String Table Browser plugin. Responsible for:
 *   - Building and maintaining a two-layer in-memory cache of all string table entries.
 *   - Persisting the cache to disk (JSON) for instant startup on subsequent launches.
 *   - Listening to Asset Registry events for incremental cache updates.
 *   - Registering the editor tab, menu entry, and Details panel customization.
 *
 * Thread safety: GroupedCache and FlatCache are guarded by CacheLock. All delegate
 * broadcasts are marshalled to the game thread before firing.
 */
class FStringTableBrowserModule : public IModuleInterface
{
public:

	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	// -------------------------------------------------------------------------
	// Static Helpers
	// -------------------------------------------------------------------------
	static FStringTableBrowserModule* GetModulePtr()
	{
		return FModuleManager::GetModulePtr<FStringTableBrowserModule>("StringTableBrowser");
	}
	
	// -------------------------------------------------------------------------
	// Tab and menu registration (called from StartupModule)
	// -------------------------------------------------------------------------

	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);
	void PluginButtonClicked();
	void RegisterMenus();

	// -------------------------------------------------------------------------
	// Cache API
	// -------------------------------------------------------------------------

	/**
	 * Returns a thread-safe snapshot copy of the flat entry cache.
	 * Prefer this in Slate widgets — it acquires and releases CacheLock
	 * internally so the caller never holds the lock during iteration.
	 */
	TArray<TSharedPtr<FStringTableBrowserEntry>> GetCachedEntriesCopy() const
	{
		TArray<TSharedPtr<FStringTableBrowserEntry>> Snapshot;
		
		{
			FScopeLock Lock(&CacheLock);
			Snapshot = FlatCache;
		}
		
		return Snapshot;
	}

	/**
	 * Discards the current cache, re-scans all loaded UStringTable assets,
	 * writes the result to disk, and broadcasts OnCacheUpdated.
	 */
	void ForceRebuildCache();

	// -------------------------------------------------------------------------
	// Delegate
	// -------------------------------------------------------------------------

	/** Broadcast on the game thread whenever the cache is rebuilt or updated. */
	DECLARE_MULTICAST_DELEGATE(FOnStringTableCacheUpdated);
	FOnStringTableCacheUpdated OnCacheUpdated;

private:

	// -------------------------------------------------------------------------
	// Disk cache
	// -------------------------------------------------------------------------

	/** Attempts to load the JSON cache from disk. Returns false on failure or version mismatch. */
	bool LoadCacheFromDisk();

	/** Serialises the current GroupedCache snapshot to disk as versioned JSON. */
	void SaveCacheToDisk();
	
	/** Sets a dirty flag and adds a time offset to write the cache.*/
	void ScheduleDiskCacheSave();

	void OnForceLoadComplete();
	void RebuildCacheFromLoadedAssets(const TArray<FAssetData>& AssetDataList);

	/** Returns the absolute path to the JSON cache file under the project's Saved directory. */
	FString GetCacheFilePath() const;

	// -------------------------------------------------------------------------
	// Asset Registry event handlers
	// -------------------------------------------------------------------------

	void OnAssetRegistryFilesLoaded();
	void OnAssetAdded(const struct FAssetData& AssetData);
	void OnAssetRemoved(const struct FAssetData& AssetData);
	void OnAssetUpdated(const struct FAssetData& AssetData);
	void OnPackageSaved( const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext ObjectSaveContext);

	// -------------------------------------------------------------------------
	// Internal cache helpers
	// IMPORTANT: All three functions below require CacheLock to be held by the caller.
	// -------------------------------------------------------------------------

	/** Enumerates all source strings in Table and inserts them into GroupedCache. */
	void CacheSingleStringTable(const struct FAssetData& AssetData, const UStringTable* Table);

	/** Removes all entries for the given package from GroupedCache. */
	void RemoveStringTableFromCache(const FName& PackageName);

	/** Flattens GroupedCache into FlatCache. Call after any GroupedCache mutation. */
	void RebuildFlatCache();

	/**
	 * Broadcasts OnCacheUpdated, marshalling to the game thread if called
	 * from a background thread (Asset Registry callbacks may arrive off-thread).
	 */
	void BroadcastCacheUpdated();

	// -------------------------------------------------------------------------
	// Data
	// -------------------------------------------------------------------------

	/**
	 * Primary cache, keyed by asset PackageName.
	 * Grouped storage allows a single table to be added, removed, or updated
	 * without rebuilding the entire dataset.
	 */
	TMap<FName, TArray<TSharedPtr<FStringTableBrowserEntry>>> GroupedCache;

	/**
	 * Flattened view of GroupedCache consumed directly by SListView.
	 * Rebuilt from GroupedCache after every mutation.
	 */
	TArray<TSharedPtr<FStringTableBrowserEntry>> FlatCache;

	/** Protects GroupedCache and FlatCache against concurrent Asset Registry callbacks. */
	mutable FCriticalSection CacheLock;

	TSharedPtr<class FUICommandList> PluginCommands;

	/** Handle for the PostEngineInit delegate used to register the Details customization. */
	FDelegateHandle PostEngineInitHandle;

	FDelegateHandle GlobalRowExtensionHandle;
	
	bool bDiskCacheDirty = false;
	FTimerHandle DiskSaveTimerHandle;

	TArray<FAssetData> PendingRebuildAssetList;
	TSharedPtr<FStreamableHandle> ActiveStreamableHandle;
};
