// Copyright (c) 2026 Mato Marion. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "PropertyEditorDelegates.h"

class IDetailCustomization;

class FTextStringTableBrowserDetailCustomization : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance();

	//~ IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization

	/**
	 * Bound to FPropertyEditorModule::GetGlobalRowExtensionDelegate().
	 * Only adds a button when the setting is ExtensionBar.
	 */
	static void OnGeneratePropertyRowExtension(
		const FOnGenerateGlobalRowExtensionArgs& InArgs,
		TArray<FPropertyRowExtensionButton>& OutExtensionButtons
	);

};

