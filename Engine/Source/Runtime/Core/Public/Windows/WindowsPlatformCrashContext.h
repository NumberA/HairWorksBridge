// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformCrashContext.h"

#include "AllowWindowsPlatformTypes.h"
#include "DbgHelp.h"
#include "HideWindowsPlatformTypes.h"

struct CORE_API FWindowsPlatformCrashContext : public FGenericCrashContext
{
	/** Platform specific constants. */
	enum EConstants
	{
		UE4_MINIDUMP_CRASHCONTEXT = LastReservedStream + 1,
	};

	virtual void AddPlatformSpecificProperties() override
	{
		AddCrashProperty( TEXT( "PlatformIsRunningWindows" ), 1 );
	}
};

typedef FWindowsPlatformCrashContext FPlatformCrashContext;