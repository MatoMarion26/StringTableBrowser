// Copyright Epic Games, Inc. All Rights Reserved.

#include "StringTableBrowserCommands.h"

#define LOCTEXT_NAMESPACE "FStringTableBrowserModule"

void FStringTableBrowserCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "StringTableBrowser", "Execute StringTableBrowser action", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
