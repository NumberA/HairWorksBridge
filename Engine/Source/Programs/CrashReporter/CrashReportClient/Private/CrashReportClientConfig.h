// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core.h"

/**
*  Holds FullCrashDump properties from the config.
*
*	FullCrashDump_0_Branch=UE4
*	FullCrashDump_0_Location=\\epicgames.net\root\Builds\UE4
*	FullCrashDump_1_Branch=...
*	...
*/
struct FFullCrashDumpEntry
{
	/** Initialization constructor. */
	FFullCrashDumpEntry( const FString& InBranchName, const FString& InLocation, const bool bInExactMatch )
		: BranchName( InBranchName )
		, Location( InLocation )
		, bExactMatch( bInExactMatch )
	{}


	/** Partial branch name. */
	const FString BranchName;

	/** Location where the full crash dump will be copied. Usually a network share. */
	const FString Location;

	/**
	*	Branch=UE4 means exact match
	*	Branch=UE4* means contain match
	*/
	const bool bExactMatch;
};

/** Holds basic configuration for the crash report client. */
struct FCrashReportClientConfig
{
	/** Accesses the singleton. */
	static FCrashReportClientConfig& Get()
	{
		static FCrashReportClientConfig Instance;
		return Instance;
	}

	/** Initialization constructor. */
	FCrashReportClientConfig();

	FString GetReceiverAddress() const
	{
		return CrashReportReceiverIP;
	}

	FString GetDataRouterURL() const
	{
		return DataRouterUrl;
	}

	const FString& GetDiagnosticsFilename() const
	{
		return DiagnosticsFilename;
	}

	const bool& GetAllowToBeContacted() const
	{
		return bAllowToBeContacted;
	}

	const bool& GetSendLogFile() const
	{
		return bSendLogFile;
	}

	const bool& GetHideLogFilesOption() const
	{
		return bHideLogFilesOption;
	}

	void SetAllowToBeContacted( bool bNewValue );
	void SetSendLogFile( bool bNewValue );

	/**
	 * @return location for full crash dump for the specified branch.
	 */
	const FString GetFullCrashDumpLocationForBranch( const FString& BranchName ) const;

protected:
	/** Returns empty string if couldn't read */
	FString GetKey( const FString& KeyName );

	/** Reads FFullCrashDumpEntry config entries. */
	void ReadFullCrashDumpConfigurations();

	/** IP address of crash report receiver. */
	FString CrashReportReceiverIP;

	/** URL of Data Router service */
	FString DataRouterUrl;

	/** Filename to use when saving diagnostics report, if generated locally. */
	FString DiagnosticsFilename;

	/** Section for crash report client configuration. */
	FString SectionName;

	/** Configuration used for copying full dump crashes. */
	TArray<FFullCrashDumpEntry> FullCrashDumpConfigurations;

	/**
	*	Whether the user allowed us to be contacted.
	*	If true the following properties are retrieved from the system: UserName (for non-launcher build) and EpicAccountID.
	*	Otherwise they will be empty.
	*/
	bool bAllowToBeContacted;

	/** Whether the user allowed us to send the log file. */
	bool bSendLogFile;

	/** Whether the user is shown the option to enable/disable sending the log file. */
	bool bHideLogFilesOption;
};
