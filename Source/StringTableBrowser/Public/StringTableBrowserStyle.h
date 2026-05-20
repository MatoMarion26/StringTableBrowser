#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

class FStringTableBrowserStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static const ISlateStyle& Get();
	static FName GetStyleSetName();

private:
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};