// Copyright (c) 2026 Mato Marion. All Rights Reserved.

using UnrealBuildTool;

public class StringTableBrowser : ModuleRules
{
	public StringTableBrowser(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"DeveloperSettings",
			"InputCore",
			"Projects"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Engine core
			"ApplicationCore",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",

			// Editor framework
			"EditorFramework",
			"LevelEditor",
			"UnrealEd",

			// Asset Registry — scanning and listening to string table assets
			"AssetRegistry",

			// Details panel customization
			"DetailCustomizations",
			"PropertyEditor",

			// IAssetManagerEditorModule — opens the native Reference Viewer
			"AssetManagerEditor",

			// Workspace menu (tab registration)
			"WorkspaceMenuStructure",

			// JSON serialization for the disk cache
			"Json",
			"JsonUtilities",

			// AsyncTask / ENamedThreads (game-thread marshalling)
			"RenderCore",
			"ToolMenus"
		});

		// This module is editor-only and must never be included in shipping builds.
		if (Target.bBuildEditor == false)
		{
			System.Console.Error.WriteLine(
				"StringTableBrowser is an Editor-only plugin and should not be compiled for non-editor targets.");
		}
	}
}
