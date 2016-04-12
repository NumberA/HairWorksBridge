﻿// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IntelISPCTexComp : ModuleRules
{
	public IntelISPCTexComp(TargetInfo Target)
	{
		Type = ModuleType.External;

		string libraryPath = UEBuildConfiguration.UEThirdPartySourceDirectory + "IntelISPCTexComp/ispc_texcomp/";
		PublicIncludePaths.Add(libraryPath);

		bool bUseDebugBuild = false;

		if ( (Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Win32) )
		{
			string platformName = (Target.Platform == UnrealTargetPlatform.Win64) ? "Win64" : "Win32";
			string configName = bUseDebugBuild ? "Debug" : "Release";
			PublicLibraryPaths.Add(libraryPath + "lib/" + platformName + "-" + configName);

			PublicAdditionalLibraries.Add("ispc_texcomp.lib");
			PublicDelayLoadDLLs.Add("ispc_texcomp.dll");

            RuntimeDependencies.Add(new RuntimeDependency("$(EngineDir)/Binaries/ThirdParty/IntelISPCTexComp/" + platformName + "-" + configName + "/ispc_texcomp.dll"));
        }
    }
}
