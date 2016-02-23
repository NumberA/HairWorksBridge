﻿// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Linq;
using System.Reflection;
using AutomationTool;
using UnrealBuildTool;

[Help("Builds a plugin, and packages it for distribution")]
[Help("Plugin", "Specify the path to the descriptor file for the plugin that should be packaged")]
[Help("NoHostPlatform", "Prevent compiling for the editor platform on the host")]
[Help("TargetPlatforms", "Specify a list of target platforms to build, separated by '+' characters (eg. -TargetPlatforms=Win32+Win64). Default is all the Rocket target platforms.")]
[Help("Package", "The path which the build artifacts should be packaged to, ready for distribution.")]
class BuildPlugin : BuildCommand
{
	public override void ExecuteBuild()
	{
		// Get the plugin filename
		string PluginFileName = ParseParamValue("Plugin");
		if(PluginFileName == null)
		{
			throw new AutomationException("Plugin file name was not specified via the -plugin argument");
		}

		// Read the plugin
		PluginDescriptor Plugin = PluginDescriptor.FromFile(new FileReference(PluginFileName));

		// Clean the intermediate build directory
		string IntermediateBuildDirectory = Path.Combine(Path.GetDirectoryName(PluginFileName), "Intermediate", "Build");
		if(CommandUtils.DirectoryExists(IntermediateBuildDirectory))
		{
			CommandUtils.DeleteDirectory(IntermediateBuildDirectory);
		}

		// Get any additional arguments from the commandline
		string AdditionalArgs = "";

		// Build the host platforms
		List<string> ReceiptFileNames = new List<string>();
		UE4Build.BuildAgenda Agenda = new UE4Build.BuildAgenda();
		UnrealTargetPlatform HostPlatform = BuildHostPlatform.Current.Platform;
		if(!ParseParam("NoHostPlatform"))
		{
			AddPluginToAgenda(Agenda, PluginFileName, Plugin, "UE4Editor", TargetRules.TargetType.Editor, HostPlatform, UnrealTargetConfiguration.Development, ReceiptFileNames, AdditionalArgs);
		}

		// Add the game targets
		List<UnrealTargetPlatform> TargetPlatforms = Rocket.RocketBuild.GetTargetPlatforms(this, HostPlatform);
		foreach(UnrealTargetPlatform TargetPlatform in TargetPlatforms)
		{
			if(Rocket.RocketBuild.IsCodeTargetPlatform(HostPlatform, TargetPlatform))
			{
				AddPluginToAgenda(Agenda, PluginFileName, Plugin, "UE4Game", TargetRules.TargetType.Game, TargetPlatform, UnrealTargetConfiguration.Development, ReceiptFileNames, AdditionalArgs);
				AddPluginToAgenda(Agenda, PluginFileName, Plugin, "UE4Game", TargetRules.TargetType.Game, TargetPlatform, UnrealTargetConfiguration.Shipping, ReceiptFileNames, AdditionalArgs);
			}
		}

		// Build it
		UE4Build Build = new UE4Build(this);
		Build.Build(Agenda, InDeleteBuildProducts: true, InUpdateVersionFiles: false);

		// Package the plugin to the output folder
		string PackageDirectory = ParseParamValue("Package");
		if(PackageDirectory != null)
		{
			List<BuildProduct> BuildProducts = GetBuildProductsFromReceipts(ReceiptFileNames);
			PackagePlugin(PluginFileName, BuildProducts, PackageDirectory);
		}
	}

	static void AddPluginToAgenda(UE4Build.BuildAgenda Agenda, string PluginFileName, PluginDescriptor Plugin, string TargetName, TargetRules.TargetType TargetType, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration, List<string> ReceiptFileNames, string InAdditionalArgs)
	{
		// Find a list of modules that need to be built for this plugin
		List<string> ModuleNames = new List<string>();
		foreach(ModuleDescriptor Module in Plugin.Modules)
		{
			bool bBuildDeveloperTools = (TargetType == TargetRules.TargetType.Editor || TargetType == TargetRules.TargetType.Program);
			bool bBuildEditor = (TargetType == TargetRules.TargetType.Editor);
			if(Module.IsCompiledInConfiguration(Platform, TargetType, bBuildDeveloperTools, bBuildEditor))
			{
				ModuleNames.Add(Module.Name);
			}
		}

		// Add these modules to the build agenda
		if(ModuleNames.Count > 0)
		{
			string Arguments = String.Format("-plugin {0}", CommandUtils.MakePathSafeToUseWithCommandLine(PluginFileName));
			foreach(string ModuleName in ModuleNames)
			{
				Arguments += String.Format(" -module {0}", ModuleName);
			}

			string ReceiptFileName = TargetReceipt.GetDefaultPath(Path.GetDirectoryName(PluginFileName), TargetName, Platform, Configuration, "");
			Arguments += String.Format(" -receipt {0}", CommandUtils.MakePathSafeToUseWithCommandLine(ReceiptFileName));
			ReceiptFileNames.Add(ReceiptFileName);
			
			if(!String.IsNullOrEmpty(InAdditionalArgs))
			{
				Arguments += InAdditionalArgs;
			}

			Agenda.AddTarget(TargetName, Platform, Configuration, InAddArgs: Arguments);
		}
	}

	static List<BuildProduct> GetBuildProductsFromReceipts(List<string> ReceiptFileNames)
	{
		List<BuildProduct> BuildProducts = new List<BuildProduct>();
		foreach(string ReceiptFileName in ReceiptFileNames)
		{
			TargetReceipt Receipt;
			if(!TargetReceipt.TryRead(ReceiptFileName, out Receipt))
			{
				throw new AutomationException("Missing or invalid target receipt ({0})", ReceiptFileName);
			}
			BuildProducts.AddRange(Receipt.BuildProducts);
		}
		return BuildProducts;
	}

	static void PackagePlugin(string PluginFileName, List<BuildProduct> BuildProducts, string PackageDirectory)
	{
		// Clear the output directory
		CommandUtils.DeleteDirectoryContents(PackageDirectory);

		// Copy all the files to the output directory
		List<string> MatchingFileNames = FilterPluginFiles(PluginFileName, BuildProducts);
		foreach(string MatchingFileName in MatchingFileNames)
		{
			string SourceFileName = Path.Combine(Path.GetDirectoryName(PluginFileName), MatchingFileName);
			string TargetFileName = Path.Combine(PackageDirectory, MatchingFileName);
			CommandUtils.CopyFile(SourceFileName, TargetFileName);
			CommandUtils.SetFileAttributes(TargetFileName, ReadOnly: false);
		}

		// Get the output plugin filename
		string TargetPluginFileName = CommandUtils.MakeRerootedFilePath(Path.GetFullPath(PluginFileName), Path.GetDirectoryName(Path.GetFullPath(PluginFileName)), PackageDirectory);
		PluginDescriptor NewDescriptor = PluginDescriptor.FromFile(new FileReference(TargetPluginFileName));
		NewDescriptor.bEnabledByDefault = true;
		NewDescriptor.bInstalled = true;
		NewDescriptor.Save(TargetPluginFileName);
	}

	static List<string> FilterPluginFiles(string PluginFileName, List<BuildProduct> BuildProducts)
	{
		string PluginDirectory = Path.GetDirectoryName(PluginFileName);

		// Set up the default filter
		FileFilter Filter = new FileFilter();
		Filter.AddRuleForFile(PluginFileName, PluginDirectory, FileFilterType.Include);
		Filter.AddRuleForFiles(BuildProducts.Select(x => x.Path), PluginDirectory, FileFilterType.Include);
		Filter.Include("/Binaries/ThirdParty/...");
		Filter.Include("/Resources/...");
		Filter.Include("/Content/...");
		Filter.Include("/Intermediate/Build/.../Inc/...");
		Filter.Include("/Source/...");

		// Add custom rules for each platform
		string FilterFileName = Path.Combine(Path.GetDirectoryName(PluginFileName), "Config", "FilterPlugin.ini");
		if(File.Exists(FilterFileName))
		{
			Filter.ReadRulesFromFile(FilterFileName, "FilterPlugin");
		}

		// Apply the standard exclusion rules
		Filter.ExcludeConfidentialFolders();
		Filter.ExcludeConfidentialPlatforms();

		// Apply the filter to the plugin directory
		return Filter.ApplyToDirectory(PluginDirectory, true);
	}
}
