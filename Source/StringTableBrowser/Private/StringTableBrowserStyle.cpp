// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#include "StringTableBrowserStyle.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FStringTableBrowserStyle::StyleInstance = nullptr;

void FStringTableBrowserStyle::Initialize()
{
	if (StyleInstance.IsValid()) return;

	StyleInstance = MakeShared<FSlateStyleSet>(GetStyleSetName());

	// Point to the plugin's Resources/ folder
	const FString PluginDir = IPluginManager::Get().FindPlugin("StringTableBrowser")->GetBaseDir();
	StyleInstance->SetContentRoot(PluginDir / TEXT("Resources"));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
}

void FStringTableBrowserStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		StyleInstance.Reset();
	}
}

const ISlateStyle& FStringTableBrowserStyle::Get()
{
	return *StyleInstance;
}

FName FStringTableBrowserStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("StringTableBrowserStyle"));
	return StyleSetName;
}