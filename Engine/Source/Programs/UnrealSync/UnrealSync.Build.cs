﻿// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealSync : ModuleRules
{
	public UnrealSync(TargetInfo Target)
	{
		PublicIncludePaths.Add("Runtime/Launch/Public");

		PrivateIncludePaths.Add("Runtime/Launch/Private");		// For LaunchEngineLoop.cpp include

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Projects",
				"Slate",
				"SlateCore",
				"StandaloneRenderer",
				"XmlParser",
				"JSON",
				"DesktopPlatform"
			}
		);
		
		PrivateDependencyModuleNames.Add("Projects");
	}
}
