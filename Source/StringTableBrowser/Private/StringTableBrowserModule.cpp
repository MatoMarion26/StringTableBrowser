// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#include "StringTableBrowserModule.h"

#include "SStringTableBrowser.h"
#include "FTextStringTableBrowserDetailCustomization.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PropertyEditorModule.h"
#include "StringTableBrowserStyle.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "StringTableBrowserModule"

// -------------------------------------------------------------------------
// Tab spawner
// -------------------------------------------------------------------------

TSharedRef<SDockTab> FStringTableBrowserModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SStringTableBrowser)
		];
}

void FStringTableBrowserModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FName("StringTableBrowser"));
}

void FStringTableBrowserModule::RegisterMenus()
{
	UToolMenus* ToolMenus = UToolMenus::Get();

	// Helper to avoid repeating the entry definition for each menu target
	auto RegisterInToolsMenu = [&](const FName& MenuName)
	{
		UToolMenu* ToolsMenu = ToolMenus->ExtendMenu(MenuName);
		FToolMenuSection& Section = ToolsMenu->FindOrAddSection("Tools");

		Section.AddMenuEntry(
			"OpenStringTableBrowser",
			LOCTEXT("OpenStringTableBrowser", "String Table Browser"),
			LOCTEXT("OpenStringTableBrowserTooltip", "Open the String Table Browser panel."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
		FUIAction(FExecuteAction::CreateRaw(this, &FStringTableBrowserModule::PluginButtonClicked)
			)
		);
	};

	// Level Editor Tools menu — the primary entry point
	RegisterInToolsMenu("LevelEditor.MainMenu.Tools");

	// String Table asset editor Tools menu
	RegisterInToolsMenu("AssetEditor.StringTableEditor.MainMenu.Tools");

	// Widget Blueprint editor Tools menu
	RegisterInToolsMenu("AssetEditor.WidgetBlueprintEditor.MainMenu.Tools");
}

// -------------------------------------------------------------------------
// Module lifecycle
// -------------------------------------------------------------------------

void FStringTableBrowserModule::StartupModule()
{
	// Use a single AssetRegistry reference throughout startup to avoid loading
	// the module more than once and to ensure all bindings share the same instance.
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	// Attempt to warm up from the disk cache first.
	// On failure (first run, schema change, or corruption) wait for the registry
	// to finish its initial scan before doing a full rebuild.
	if (!LoadCacheFromDisk())
	{
		if (AssetRegistryModule.Get().IsLoadingAssets())
		{
			AssetRegistryModule.Get().OnFilesLoaded().AddRaw(
				this, &FStringTableBrowserModule::OnAssetRegistryFilesLoaded);
		}
		else
		{
			ForceRebuildCache();
		}
	}

	// Subscribe to incremental Asset Registry events for live updates
	AssetRegistryModule.Get().OnAssetAdded().AddRaw(  this, &FStringTableBrowserModule::OnAssetAdded);
	AssetRegistryModule.Get().OnAssetRemoved().AddRaw( this, &FStringTableBrowserModule::OnAssetRemoved);
	AssetRegistryModule.Get().OnAssetUpdated().AddRaw( this, &FStringTableBrowserModule::OnAssetUpdated);

	FPropertyEditorModule& PropertyModule =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	// Extension bar — always bound, but internally checks the setting
	// before adding any buttons so only one path is active at a time.
	GlobalRowExtensionHandle =
		PropertyModule.GetGlobalRowExtensionDelegate().AddStatic(
			&FTextStringTableBrowserDetailCustomization::OnGeneratePropertyRowExtension);

	// Next-to-label customization — always registered, same internal gate.
	PropertyModule.RegisterCustomClassLayout(
		"Object",
		FOnGetDetailCustomizationInstance::CreateStatic(
			&FTextStringTableBrowserDetailCustomization::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();
	
	// Register the tab spawner
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		FName("StringTableBrowser"),
		FOnSpawnTab::CreateRaw(this, &FStringTableBrowserModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("StringTableBrowserTab", "String Table Browser"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"))
		.SetMenuType(ETabSpawnerMenuType::Hidden); // Hidden because we manage the menu entry ourselves

	// Register menus (deferred so the editor toolbar is ready)
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FStringTableBrowserModule::RegisterMenus));
}

void FStringTableBrowserModule::ShutdownModule()
{
	FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);

	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		IAssetRegistry& AssetRegistry =
			FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

		AssetRegistry.OnFilesLoaded().RemoveAll(this);
		AssetRegistry.OnAssetAdded().RemoveAll(this);
		AssetRegistry.OnAssetRemoved().RemoveAll(this);
		AssetRegistry.OnAssetUpdated().RemoveAll(this);
	}

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule =
			FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyModule.GetGlobalRowExtensionDelegate().Remove(GlobalRowExtensionHandle);
		PropertyModule.UnregisterCustomClassLayout("Object");
		PropertyModule.NotifyCustomizationModuleChanged();
	}
	
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::Get()->UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FName("StringTableBrowser"));
	FStringTableBrowserStyle::Shutdown(); 
}

// -------------------------------------------------------------------------
// Cache — disk persistence
// -------------------------------------------------------------------------

FString FStringTableBrowserModule::GetCacheFilePath() const
{
	return FPaths::ProjectSavedDir() / TEXT("StringTableBrowserCache.json");
}

bool FStringTableBrowserModule::LoadCacheFromDisk()
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *GetCacheFilePath()))
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return false;
	}

	// Reject caches written by an older or newer schema version
	int32 CachedVersion = 0;
	RootObject->TryGetNumberField(TEXT("Version"), CachedVersion);
	if (CachedVersion != GStringTableBrowserCacheVersion)
	{
		UE_LOG(LogTemp, Log,
			TEXT("StringTableBrowser: Cache version mismatch (stored=%d, expected=%d). Rebuilding."),
			CachedVersion, GStringTableBrowserCacheVersion);
		return false;
	}

	FScopeLock Lock(&CacheLock);
	GroupedCache.Empty();

	const TArray<TSharedPtr<FJsonValue>>* JsonTables = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("Tables"), JsonTables))
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& TableValue : *JsonTables)
	{
		const TSharedPtr<FJsonObject> TableObject = TableValue->AsObject();
		if (!TableObject.IsValid())
		{
			continue;
		}

		const FName  PackageName(TableObject->GetStringField(TEXT("PackageName")));
		const FName  TableId(TableObject->GetStringField(TEXT("TableId")));
		const FSoftObjectPath AssetPath(TableObject->GetStringField(TEXT("AssetPath")));

		TArray<TSharedPtr<FStringTableBrowserEntry>> Entries;

		const TArray<TSharedPtr<FJsonValue>>* JsonEntries = nullptr;
		if (TableObject->TryGetArrayField(TEXT("Entries"), JsonEntries))
		{
			for (const TSharedPtr<FJsonValue>& EntryValue : *JsonEntries)
			{
				const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
				if (!EntryObject.IsValid())
				{
					continue;
				}

				TSharedPtr<FStringTableBrowserEntry> Entry = MakeShared<FStringTableBrowserEntry>();
				Entry->TableId   = TableId;
				Entry->AssetPath = AssetPath;
				Entry->Key       = EntryObject->GetStringField(TEXT("Key"));
				Entry->Value     = EntryObject->GetStringField(TEXT("Value"));
				Entries.Add(Entry);
			}
		}

		GroupedCache.Add(PackageName, MoveTemp(Entries));
	}

	RebuildFlatCache();
	return true;
}

void FStringTableBrowserModule::SaveCacheToDisk()
{
	// Snapshot under lock, then do all I/O outside the lock to minimise contention
	TMap<FName, TArray<TSharedPtr<FStringTableBrowserEntry>>> CacheSnapshot;
	{
		FScopeLock Lock(&CacheLock);
		CacheSnapshot = GroupedCache;
	}

	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetNumberField(TEXT("Version"), GStringTableBrowserCacheVersion);

	TArray<TSharedPtr<FJsonValue>> JsonTables;
	for (const auto& Pair : CacheSnapshot)
	{
		TSharedPtr<FJsonObject> TableObject = MakeShared<FJsonObject>();
		TableObject->SetStringField(TEXT("PackageName"), Pair.Key.ToString());

		if (Pair.Value.Num() > 0)
		{
			TableObject->SetStringField(TEXT("TableId"),   Pair.Value[0]->TableId.ToString());
			TableObject->SetStringField(TEXT("AssetPath"), Pair.Value[0]->AssetPath.ToString());
		}

		TArray<TSharedPtr<FJsonValue>> JsonEntries;
		for (const TSharedPtr<FStringTableBrowserEntry>& Entry : Pair.Value)
		{
			TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
			EntryObject->SetStringField(TEXT("Key"),   Entry->Key);
			EntryObject->SetStringField(TEXT("Value"), Entry->Value);
			JsonEntries.Add(MakeShared<FJsonValueObject>(EntryObject));
		}

		TableObject->SetArrayField(TEXT("Entries"), JsonEntries);
		JsonTables.Add(MakeShared<FJsonValueObject>(TableObject));
	}

	RootObject->SetArrayField(TEXT("Tables"), JsonTables);

	FString OutputString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	if (FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
	{
		FFileHelper::SaveStringToFile(OutputString, *GetCacheFilePath());
	}
}

// -------------------------------------------------------------------------
// Cache — rebuild
// -------------------------------------------------------------------------

void FStringTableBrowserModule::ForceRebuildCache()
{
	{
		FScopeLock Lock(&CacheLock);
		GroupedCache.Empty();

		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

		TArray<FAssetData> AssetDataList;
		AssetRegistryModule.Get().GetAssetsByClass(
			UStringTable::StaticClass()->GetClassPathName(), AssetDataList);

		for (const FAssetData& AssetData : AssetDataList)
		{
			// Only cache assets that are already resident in memory.
			// Loading everything synchronously here would cause severe hitches on
			// large projects. Assets load lazily as they are opened by the user.
			UStringTable* Table = Cast<UStringTable>(
				FindObject<UStringTable>(nullptr, *AssetData.GetObjectPathString()));

			if (Table)
			{
				CacheSingleStringTable(AssetData, Table);
			}
		}

		RebuildFlatCache();
	}

	SaveCacheToDisk();
	BroadcastCacheUpdated();
}

// -------------------------------------------------------------------------
// Cache — Asset Registry event handlers
// -------------------------------------------------------------------------

void FStringTableBrowserModule::OnAssetRegistryFilesLoaded()
{
	// Self-remove immediately — this delegate must fire at most once
	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry")
			.Get().OnFilesLoaded().RemoveAll(this);
	}

	ForceRebuildCache();
}

void FStringTableBrowserModule::OnAssetAdded(const FAssetData& AssetData)
{
	// Do not process events that arrive during the initial editor scan
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	if (AssetRegistryModule.Get().IsLoadingAssets())
	{
		return;
	}

	if (AssetData.AssetClassPath != UStringTable::StaticClass()->GetClassPathName())
	{
		return;
	}

	UStringTable* Table = Cast<UStringTable>(
		FindObject<UStringTable>(nullptr, *AssetData.GetObjectPathString()));

	if (Table)
	{
		{
			FScopeLock Lock(&CacheLock);
			CacheSingleStringTable(AssetData, Table);
			RebuildFlatCache();
		}
		SaveCacheToDisk();
		BroadcastCacheUpdated();
	}
}

void FStringTableBrowserModule::OnAssetRemoved(const FAssetData& AssetData)
{
	if (AssetData.AssetClassPath != UStringTable::StaticClass()->GetClassPathName())
	{
		return;
	}

	{
		FScopeLock Lock(&CacheLock);
		RemoveStringTableFromCache(AssetData.PackageName);
		RebuildFlatCache();
	}

	SaveCacheToDisk();
	BroadcastCacheUpdated();
}

void FStringTableBrowserModule::OnAssetUpdated(const FAssetData& AssetData)
{
	if (AssetData.AssetClassPath != UStringTable::StaticClass()->GetClassPathName())
	{
		return;
	}

	UStringTable* Table = Cast<UStringTable>(
		FindObject<UStringTable>(nullptr, *AssetData.GetObjectPathString()));

	if (Table)
	{
		{
			FScopeLock Lock(&CacheLock);
			RemoveStringTableFromCache(AssetData.PackageName);
			CacheSingleStringTable(AssetData, Table);
			RebuildFlatCache();
		}
		SaveCacheToDisk();
		BroadcastCacheUpdated();
	}
}

void FStringTableBrowserModule::OnGenerateGlobalRowExtension(
	const FOnGenerateGlobalRowExtensionArgs& Args,
	TArray<FPropertyRowExtensionButton>& OutExtensionButtons
)
{
	if (Args.PropertyHandle.IsValid() && Args.PropertyHandle->GetProperty())
	{
		// Check if the property being drawn is an FText Property
		if (FTextProperty* TextProp = CastField<FTextProperty>(Args.PropertyHandle->GetProperty()))
		{
			FPropertyRowExtensionButton ExtensionButton;
            
			ExtensionButton.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Info");
			ExtensionButton.ToolTip = FText::FromString("Open the String Table Browser Dropdown.");
            
			// Define what happens when the user clicks the button
			ExtensionButton.UIAction = FUIAction(
				FExecuteAction::CreateLambda([PropHandle = Args.PropertyHandle]()
				{
					// Your logic goes here
					UE_LOG(LogTemp, Warning, TEXT("Global FText button clicked!"));
				})
			);
            
			// Add it to the row
			OutExtensionButtons.Add(ExtensionButton);
		}
	}
}

// -------------------------------------------------------------------------
// Cache — internal helpers (CacheLock must be held by the caller)
// -------------------------------------------------------------------------

void FStringTableBrowserModule::CacheSingleStringTable(
	const FAssetData& AssetData,
	UStringTable*     Table)
{
	TArray<TSharedPtr<FStringTableBrowserEntry>> TableEntries;

	Table->GetStringTable()->EnumerateSourceStrings(
		[&](const FString& InKey, const FString& InSourceString)
		{
			TSharedPtr<FStringTableBrowserEntry> Entry = MakeShared<FStringTableBrowserEntry>();
			Entry->TableId   = AssetData.AssetName;
			Entry->AssetPath = AssetData.ToSoftObjectPath();
			Entry->Key       = InKey;
			Entry->Value     = InSourceString;
			TableEntries.Add(Entry);
			return true; // continue enumeration
		});

	GroupedCache.Add(AssetData.PackageName, MoveTemp(TableEntries));
}

void FStringTableBrowserModule::RemoveStringTableFromCache(const FName& PackageName)
{
	GroupedCache.Remove(PackageName);
}

void FStringTableBrowserModule::RebuildFlatCache()
{
	FlatCache.Empty();
	for (const auto& Pair : GroupedCache)
	{
		FlatCache.Append(Pair.Value);
	}
}

void FStringTableBrowserModule::BroadcastCacheUpdated()
{
	if (IsInGameThread())
	{
		OnCacheUpdated.Broadcast();
	}
	else
	{
		// Asset Registry callbacks can arrive on background threads in some engine versions.
		// Slate is not thread-safe, so always marshal broadcasts to the game thread.
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			OnCacheUpdated.Broadcast();
		});
	}
}

IMPLEMENT_MODULE(FStringTableBrowserModule, StringTableBrowser)

#undef LOCTEXT_NAMESPACE
