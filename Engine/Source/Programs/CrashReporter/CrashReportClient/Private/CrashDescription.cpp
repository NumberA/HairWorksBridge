// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "CrashReportClientApp.h"
#include "GenericPlatformCrashContext.h"

#include "EngineVersion.h"
#include "XmlFile.h"
#include "CrashDescription.h"
#include "CrashReportAnalytics.h"
#include "CrashReportUtil.h"
#include "Runtime/Analytics/Analytics/Public/Analytics.h"
#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"
#include "EngineBuildSettings.h"

// #YRX_Crash: 2015-07-23 Move crashes from C:\Users\[USER]\AppData\Local\Microsoft\Windows\WER\ReportQueue to C:\Users\[USER]\AppData\Local\CrashReportClient\Saved

/*-----------------------------------------------------------------------------
	FCrashProperty
-----------------------------------------------------------------------------*/

FCrashProperty::FCrashProperty( const FString& InMainCategory, const FString& InSecondCategory, FPrimaryCrashProperties* InOwner ) 
: Owner( InOwner )
, CachedValue( TEXT("") )
, MainCategory( InMainCategory )
, SecondCategory( InSecondCategory )
, bSet( false )
{

}

FCrashProperty& FCrashProperty::operator=(const FString& NewValue)
{
	bSet = true;
	CachedValue = NewValue;
	Owner->SetCrashProperty( MainCategory, SecondCategory, CachedValue );
	return *this;
}

FCrashProperty& FCrashProperty::operator=(const TCHAR* NewValue)
{
	bSet = true;
	CachedValue = NewValue;
	Owner->SetCrashProperty( MainCategory, SecondCategory, CachedValue );
	return *this;
}


FCrashProperty& FCrashProperty::operator=(const TArray<FString>& NewValue)
{
	bSet = true;
	CachedValue = Owner->EncodeArrayStringAsXMLString( NewValue );
	Owner->SetCrashProperty( MainCategory, SecondCategory, CachedValue );
	return *this;
}


FCrashProperty& FCrashProperty::operator=(const bool NewValue)
{
	bSet = true;
	CachedValue = NewValue ? TEXT( "1" ) : TEXT( "0" );
	Owner->SetCrashProperty( MainCategory, SecondCategory, CachedValue );
	return *this;
}

FCrashProperty& FCrashProperty::operator=(const int64 NewValue)
{
	bSet = true;
	CachedValue = LexicalConversion::ToString( NewValue );
	Owner->SetCrashProperty( MainCategory, SecondCategory, CachedValue );
	return *this;
}

const FString& FCrashProperty::AsString() const
{
	if (!bSet)
	{
		Owner->GetCrashProperty( CachedValue, MainCategory, SecondCategory );
		bSet = true;
	}
	return CachedValue;
}


bool FCrashProperty::AsBool() const
{
	return AsString().ToBool();
}

int64 FCrashProperty::AsInt64() const
{
	int64 Value = 0;
	TTypeFromString<int64>::FromString( Value, *AsString() );
	return Value;
}

/*-----------------------------------------------------------------------------
	FPrimaryCrashProperties
-----------------------------------------------------------------------------*/

FPrimaryCrashProperties* FPrimaryCrashProperties::Singleton = nullptr;

FPrimaryCrashProperties::FPrimaryCrashProperties()
	// At this moment only these properties can be changed by the crash report client.
	: PlatformFullName( FGenericCrashContext::RuntimePropertiesTag, TEXT( "PlatformFullName" ), this )
	, CommandLine( FGenericCrashContext::RuntimePropertiesTag, TEXT( "CommandLine" ), this )
	, UserName( FGenericCrashContext::RuntimePropertiesTag, TEXT("UserName"), this )
	, MachineId( FGenericCrashContext::RuntimePropertiesTag, TEXT( "MachineId" ), this )
	, EpicAccountId( FGenericCrashContext::RuntimePropertiesTag, TEXT( "EpicAccountId" ), this )
	// Multiline properties
	, CallStack( FGenericCrashContext::RuntimePropertiesTag, TEXT( "CallStack" ), this )
	, SourceContext( FGenericCrashContext::RuntimePropertiesTag, TEXT( "SourceContext" ), this )
	, Modules( FGenericCrashContext::RuntimePropertiesTag, TEXT( "Modules" ), this )
	, UserDescription( FGenericCrashContext::RuntimePropertiesTag, TEXT( "UserDescription" ), this )
	, ErrorMessage( FGenericCrashContext::RuntimePropertiesTag, TEXT( "ErrorMessage" ), this )
	, FullCrashDumpLocation( FGenericCrashContext::RuntimePropertiesTag, TEXT( "FullCrashDumpLocation" ), this )
	, TimeOfCrash( FGenericCrashContext::RuntimePropertiesTag, TEXT( "TimeOfCrash" ), this )
	, bAllowToBeContacted( FGenericCrashContext::RuntimePropertiesTag, TEXT( "bAllowToBeContacted" ), this )
	, XmlFile( nullptr )
{
	CrashVersion = ECrashDescVersions::VER_1_NewCrashFormat;
	CrashDumpMode = ECrashDumpMode::Default;
	bHasMiniDumpFile = false;
	bHasLogFile = false;
	bHasPrimaryData = false;
}

void FPrimaryCrashProperties::Shutdown()
{
	delete Get();
}

void FPrimaryCrashProperties::UpdateIDs()
{
	const bool bAddPersonalData = FCrashReportClientConfig::Get().GetAllowToBeContacted() || FEngineBuildSettings::IsInternalBuild();
	bAllowToBeContacted = bAddPersonalData;
	if (bAddPersonalData)
	{
		// The Epic ID can be looked up from this ID.
		EpicAccountId = FPlatformMisc::GetEpicAccountId();
	}
	else
	{
		EpicAccountId = FString();
	}

	// Add real user name only if log files were allowed since the user name is in the log file and the user consented to sending this information.
	const bool bSendUserName = FCrashReportClientConfig::Get().GetSendLogFile() || FEngineBuildSettings::IsInternalBuild();
	if (bSendUserName)
	{
		// Remove periods from user names to match AutoReporter user names
		// The name prefix is read by CrashRepository.AddNewCrash in the website code
		UserName = FString( FPlatformProcess::UserName() ).Replace( TEXT( "." ), TEXT( "" ) );
	}
	else
	{
		UserName = FString();
	}

	MachineId = FPlatformMisc::GetMachineId().ToString( EGuidFormats::Digits );
}

void FPrimaryCrashProperties::ReadXML( const FString& CrashContextFilepath  )
{
	XmlFilepath = CrashContextFilepath;
	XmlFile = new FXmlFile( XmlFilepath );
	TimeOfCrash = FDateTime::UtcNow().GetTicks();
	UpdateIDs();
}

void FPrimaryCrashProperties::SetCrashGUID( const FString& Filepath )
{
	FString CrashDirectory = FPaths::GetPath( Filepath );
	FPaths::NormalizeDirectoryName( CrashDirectory );
	// Grab the last component...
	CrashGUID = FPaths::GetCleanFilename( CrashDirectory );
}

FString FPrimaryCrashProperties::EncodeArrayStringAsXMLString( const TArray<FString>& ArrayString ) const
{
	const FString Encoded = FString::Join( ArrayString, TEXT("\n") );
	return Encoded;
}

void FPrimaryCrashProperties::SendAnalytics()
{
	// Connect the crash report client analytics provider.
	FCrashReportAnalytics::Initialize();

	IAnalyticsProvider& Analytics = FCrashReportAnalytics::GetProvider();

	TArray<FAnalyticsEventAttribute> CrashAttributes;

	CrashAttributes.Add( FAnalyticsEventAttribute( TEXT( "bHasPrimaryData" ), bHasPrimaryData ) );
	CrashAttributes.Add( FAnalyticsEventAttribute( TEXT( "CrashVersion" ), (int32)CrashVersion ) );
	CrashAttributes.Add( FAnalyticsEventAttribute( TEXT( "CrashGUID" ), CrashGUID ) );

	//	AppID = GameName
	CrashAttributes.Add( FAnalyticsEventAttribute( TEXT( "GameName" ), GameName ) );

	//	AppVersion = EngineVersion
	CrashAttributes.Add( FAnalyticsEventAttribute( TEXT( "EngineVersion" ), EngineVersion.ToString() ) );

	// @see UpdateIDs()
	CrashAttributes.Add( FAnalyticsEventAttribute( TEXT( "MachineID" ), MachineId.AsString() ) );
	CrashAttributes.Add( FAnalyticsEventAttribute( TEXT( "UserName" ), UserName.AsString() ) );
	CrashAttributes.Add( FAnalyticsEventAttribute( TEXT( "EpicAccountId" ), EpicAccountId.AsString() ) );

	CrashAttributes.Add( FAnalyticsEventAttribute( TEXT( "Platform" ), PlatformFullName.AsString() ) );
	CrashAttributes.Add( FAnalyticsEventAttribute( TEXT( "TimeOfCrash" ), TimeOfCrash.AsString() ) );
	CrashAttributes.Add( FAnalyticsEventAttribute( TEXT( "EngineMode" ), EngineMode ) );
	CrashAttributes.Add( FAnalyticsEventAttribute( TEXT( "AppDefaultLocale" ), AppDefaultLocale ) );

	Analytics.RecordEvent( TEXT( "CrashReportClient.ReportCrash" ), CrashAttributes );

	// Shutdown analytics.
	FCrashReportAnalytics::Shutdown();
}

void FPrimaryCrashProperties::Save()
{
	XmlFile->Save( XmlFilepath );
}

/*-----------------------------------------------------------------------------
	FCrashContextReader
-----------------------------------------------------------------------------*/

FCrashContext::FCrashContext( const FString& CrashContextFilepath )
{
	ReadXML( CrashContextFilepath );

	const bool bIsValid = XmlFile->IsValid();
	if (bIsValid)
	{
		RestartCommandLine = CommandLine.AsString();

		// Setup properties required for the analytics.
		GetCrashProperty( CrashVersion, FGenericCrashContext::RuntimePropertiesTag, TEXT( "CrashVersion" ) );
		GetCrashProperty( CrashGUID, FGenericCrashContext::RuntimePropertiesTag, TEXT( "CrashGUID" ) );
		GetCrashProperty( CrashDumpMode, FGenericCrashContext::RuntimePropertiesTag, TEXT( "CrashDumpMode" ) );
		GetCrashProperty( GameName, FGenericCrashContext::RuntimePropertiesTag, TEXT( "GameName" ) );
		GetCrashProperty( EngineVersion, FGenericCrashContext::RuntimePropertiesTag, TEXT( "EngineVersion" ) );

		GetCrashProperty( BaseDir, FGenericCrashContext::RuntimePropertiesTag, TEXT( "BaseDir" ) );
		FString Misc_OSVersionMajor;
		GetCrashProperty( Misc_OSVersionMajor, FGenericCrashContext::RuntimePropertiesTag, TEXT( "Misc.OSVersionMajor" ) );
		FString Misc_OSVersionMinor;
		GetCrashProperty( Misc_OSVersionMinor, FGenericCrashContext::RuntimePropertiesTag, TEXT( "Misc.OSVersionMinor" ) );

		bool Misc_Is64bitOperatingSystem = false;
		GetCrashProperty( Misc_Is64bitOperatingSystem, FGenericCrashContext::RuntimePropertiesTag, TEXT( "Misc.Is64bitOperatingSystem" ) );

		// Extract the Platform component.
		TArray<FString> SubDirs;
		BaseDir.ParseIntoArray( SubDirs, TEXT( "/" ), true );
		const int SubDirsNum = SubDirs.Num();
		const FString PlatformName = SubDirsNum > 0 ? SubDirs[SubDirsNum - 1] : TEXT( "" );
		if (Misc_OSVersionMajor.Len() > 0)
		{
			PlatformFullName = FString::Printf( TEXT( "%s [%s %s %s]" ), *PlatformName, *Misc_OSVersionMajor, *Misc_OSVersionMinor, Misc_Is64bitOperatingSystem ? TEXT( "64b" ) : TEXT( "32b" ) );
		}
		else
		{
			PlatformFullName = PlatformName;
		}

		GetCrashProperty( EngineMode, FGenericCrashContext::RuntimePropertiesTag, TEXT( "EngineMode" ) );
		GetCrashProperty( AppDefaultLocale, FGenericCrashContext::RuntimePropertiesTag, TEXT( "AppDefaultLocale" ) );

		if (CrashDumpMode == ECrashDumpMode::FullDump)
		{
			// Set the full dump crash location when we have a full dump.
			const FString LocationForBranch = FCrashReportClientConfig::Get().GetFullCrashDumpLocationForBranch( EngineVersion.GetBranch() );
			if (!LocationForBranch.IsEmpty())
			{
				FullCrashDumpLocation = LocationForBranch / CrashGUID + TEXT("_") + EngineVersion.ToString();
			}
		}

		bHasPrimaryData = true;
	}
}

/*-----------------------------------------------------------------------------
	FCrashDescription
-----------------------------------------------------------------------------*/

FCrashWERContext::FCrashWERContext( const FString& WERXMLFilepath )
	: FPrimaryCrashProperties()
{
	ReadXML( WERXMLFilepath );
	CrashGUID = FPaths::GetCleanFilename( FPaths::GetPath( WERXMLFilepath ) );

	const bool bIsValid = XmlFile->IsValid();
	if (bIsValid)
	{
		FString BuildVersion;
		FString BranchName;
		uint32 BuiltFromCL = 0;
		int EngineVersionComponents = 0;

		GetCrashProperty( GameName, TEXT( "ProblemSignatures" ), TEXT( "Parameter0" ) );

		GetCrashProperty( BuildVersion, TEXT( "ProblemSignatures" ), TEXT( "Parameter1" ) );
		if (!BuildVersion.IsEmpty())
		{
			EngineVersionComponents++;
		}

		FString Parameter8Value;
		GetCrashProperty( Parameter8Value, TEXT( "ProblemSignatures" ), TEXT( "Parameter8" ) );
		if (!Parameter8Value.IsEmpty())
		{
			TArray<FString> ParsedParameters8;
			Parameter8Value.ParseIntoArray( ParsedParameters8, TEXT( "!" ), false );

			if (ParsedParameters8.Num() > 1)
			{
				CommandLine = FGenericCrashContext::UnescapeXMLString( ParsedParameters8[1] );
				CrashDumpMode = CommandLine.AsString().Contains( TEXT( "-fullcrashdump" ) ) ? ECrashDumpMode::FullDump : ECrashDumpMode::Default;
			}

			if (ParsedParameters8.Num() > 2)
			{
				ErrorMessage = ParsedParameters8[2];
			}
		}

		RestartCommandLine = CommandLine.AsString();

		FString Parameter9Value;
		GetCrashProperty( Parameter9Value, TEXT( "ProblemSignatures" ), TEXT( "Parameter9" ) );
		if (!Parameter9Value.IsEmpty())
		{
			TArray<FString> ParsedParameters9;
			Parameter9Value.ParseIntoArray( ParsedParameters9, TEXT( "!" ), false );

			if (ParsedParameters9.Num() > 0)
			{
				BranchName = ParsedParameters9[0].Replace( TEXT( "+" ), TEXT( "/" ) );

				const FString DepotRoot = TEXT( "//depot/" );
				if (BranchName.StartsWith( DepotRoot ))
				{
					BranchName = BranchName.Mid( DepotRoot.Len() );
				}
				EngineVersionComponents++;
			}

			if (ParsedParameters9.Num() > 1)
			{
				const FString BaseDirectory = ParsedParameters9[1];

				TArray<FString> SubDirs;
				BaseDirectory.ParseIntoArray( SubDirs, TEXT( "/" ), true );
				const int SubDirsNum = SubDirs.Num();
				const FString PlatformName = SubDirsNum > 0 ? SubDirs[SubDirsNum - 1] : TEXT( "" );

				FString Product;
				GetCrashProperty( Product, TEXT( "OSVersionInformation" ), TEXT( "Product" ) );
				if (Product.Len() > 0)
				{
					PlatformFullName = FString::Printf( TEXT( "%s [%s]" ), *PlatformName, *Product );
				}
				else
				{
					PlatformFullName = PlatformName;
				}
			}

			if (ParsedParameters9.Num() > 2)
			{
				EngineMode = ParsedParameters9[2];
			}

			if (ParsedParameters9.Num() > 3)
			{
				TTypeFromString<uint32>::FromString( BuiltFromCL, *ParsedParameters9[3] );
				EngineVersionComponents++;
			}
		}

		// We have all three components of the engine version, so initialize it.
		if (EngineVersionComponents == 3)
		{
			InitializeEngineVersion( BuildVersion, BranchName, BuiltFromCL );
		}

		bHasPrimaryData = true;
	}
}

void FCrashWERContext::InitializeEngineVersion( const FString& BuildVersion, const FString& BranchName, uint32 BuiltFromCL )
{
	uint16 Major = 0;
	uint16 Minor = 0;
	uint16 Patch = 0;
 
	TArray<FString> ParsedBuildVersion;
	BuildVersion.ParseIntoArray( ParsedBuildVersion, TEXT( "." ), false );
 
	if (ParsedBuildVersion.Num() >= 3)
	{
		TTypeFromString<uint16>::FromString( Patch, *ParsedBuildVersion[2] );
	}
 
	if (ParsedBuildVersion.Num() >= 2)
	{
		TTypeFromString<uint16>::FromString( Minor, *ParsedBuildVersion[1] );
	}
 
	if (ParsedBuildVersion.Num() >= 1)
	{
		TTypeFromString<uint16>::FromString( Major, *ParsedBuildVersion[0] );
	}
 
	EngineVersion = FEngineVersion( Major, Minor, Patch, BuiltFromCL, BranchName );
}
