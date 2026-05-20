#include "StringTableBrowserStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

TSharedPtr<FSlateStyleSet> FStringTableBrowserStyle::StyleInstance = nullptr;

void FStringTableBrowserStyle::Initialize()
{
	if (StyleInstance.IsValid()) return;

	StyleInstance = MakeShared<FSlateStyleSet>(GetStyleSetName());

	// Point to the plugin's Resources/ folder
	const FString PluginDir = IPluginManager::Get().FindPlugin("StringTableBrowser")->GetBaseDir();
	StyleInstance->SetContentRoot(PluginDir / TEXT("Resources"));

	// Register the icon — IMAGE_BRUSH resolves relative to ContentRoot
	//StyleInstance->Set("StringTableBrowser.Icon16", new IMAGE_BRUSH("Icon16", FVector2D(16.f, 16.f)));
	//StyleInstance->Set("StringTableBrowser.Icon40", new IMAGE_BRUSH("Icon40", FVector2D(40.f, 40.f)));

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