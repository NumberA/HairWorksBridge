// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * This is the WinRT version of a critical section. It uses an aggregate
 * CRITICAL_SECTION to implement its locking.
 */
class FWinRTCriticalSection
{
	/**
	 * The WinRT specific critical section
	 */
	CRITICAL_SECTION CriticalSection;

public:

	/**
	 * Constructor that initializes the aggregated critical section
	 */
	FORCEINLINE FWinRTCriticalSection()
	{
#if USING_CODE_ANALYSIS
	MSVC_PRAGMA( warning( suppress : 28125 ) )	// warning C28125: The function 'InitializeCriticalSection' must be called from within a try/except block:  The requirement might be conditional.
#endif	// USING_CODE_ANALYSIS
		InitializeCriticalSectionEx(&CriticalSection, 4000, 0);
	}

	/**
	 * Destructor cleaning up the critical section
	 */
	FORCEINLINE ~FWinRTCriticalSection()
	{
		DeleteCriticalSection(&CriticalSection);
	}

	/**
	 * Locks the critical section
	 */
	FORCEINLINE void Lock()
	{
		// Spin first before entering critical section, causing ring-0 transition and context switch.
		if( TryEnterCriticalSection(&CriticalSection) == 0 )
		{
			EnterCriticalSection(&CriticalSection);
		}
	}

	/**
	 * Releases the lock on the critical seciton
	 */
	FORCEINLINE void Unlock()
	{
		LeaveCriticalSection(&CriticalSection);
	}

private:
	FWinRTCriticalSection(const FWinRTCriticalSection&);
	FWinRTCriticalSection& operator=(const FWinRTCriticalSection&);
};
