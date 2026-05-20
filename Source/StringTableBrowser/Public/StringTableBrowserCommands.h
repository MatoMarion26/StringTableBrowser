// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "StringTableBrowserStyle.h"

class FStringTableBrowserCommands : public TCommands<FStringTableBrowserCommands>
{
public:

	FStringTableBrowserCommands()
		: TCommands<FStringTableBrowserCommands>(TEXT("StringTableBrowser"), NSLOCTEXT("Contexts", "StringTableBrowser", "StringTableBrowser Plugin"), NAME_None, FStringTableBrowserStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};
