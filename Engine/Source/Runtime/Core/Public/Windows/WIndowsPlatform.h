// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once


/**
* Windows specific types
**/
struct FWindowsPlatformTypes : public FGenericPlatformTypes
{
#ifdef _WIN64
	typedef unsigned __int64	SIZE_T;
	typedef __int64				SSIZE_T;
#else
	typedef unsigned long		SIZE_T;
	typedef long				SSIZE_T;
#endif
};

typedef FWindowsPlatformTypes FPlatformTypes;

// Base defines, must define these for the platform, there are no defaults
#define PLATFORM_DESKTOP					1
#if defined( _WIN64 )
	#define PLATFORM_64BITS					1
#else
	#define PLATFORM_64BITS					0
#endif
#if defined( _MANAGED ) || defined( _M_CEE )
	#define PLATFORM_COMPILER_COMMON_LANGUAGE_RUNTIME_COMPILATION	1
#endif
#define PLATFORM_CAN_SUPPORT_EDITORONLY_DATA				1

// Base defines, defaults are commented out

#define PLATFORM_LITTLE_ENDIAN								1
#if defined(__clang__)
	// @todo clang: Clang compiler on Windows doesn't support SEH exception handling yet (__try/__except)
	#define PLATFORM_SEH_EXCEPTIONS_DISABLED				1

	// @todo clang: Clang compiler on Windows doesn't support C++ exception handling yet (try/throw/catch)
	#define PLATFORM_EXCEPTIONS_DISABLED					1
#endif

#define PLATFORM_SUPPORTS_PRAGMA_PACK						1
#if defined(__clang__)
	#define PLATFORM_ENABLE_VECTORINTRINSICS				0
#else
	#define PLATFORM_ENABLE_VECTORINTRINSICS				1
#endif
//#define PLATFORM_USE_LS_SPEC_FOR_WIDECHAR					1
//#define PLATFORM_USE_SYSTEM_VSWPRINTF						1
//#define PLATFORM_TCHAR_IS_4_BYTES							0
#define PLATFORM_HAS_BSD_TIME								0
#define PLATFORM_USE_PTHREADS								0
#define PLATFORM_MAX_FILEPATH_LENGTH						MAX_PATH
#define PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS			1
#define PLATFORM_USES_MICROSOFT_LIBC_FUNCTIONS				1
#define PLATFORM_SUPPORTS_TBB								1
#define PLATFORM_SUPPORTS_NAMED_PIPES						1
#if 0 //@todo: VS2015 supports defaulted functions but we have errors in some of our classes we need to fix up before we can enable it
	#define PLATFORM_COMPILER_HAS_DEFAULTED_FUNCTIONS		1
#else
	#define PLATFORM_COMPILER_HAS_DEFAULTED_FUNCTIONS		0
#endif
#if _MSC_VER >= 1800 || __clang__
	#define PLATFORM_COMPILER_HAS_VARIADIC_TEMPLATES		1
	#define PLATFORM_COMPILER_HAS_EXPLICIT_OPERATORS		1
	#define PLATFORM_COMPILER_HAS_DEFAULT_FUNCTION_TEMPLATE_ARGUMENTS	1
#else
	#define PLATFORM_COMPILER_HAS_VARIADIC_TEMPLATES		0
	#define PLATFORM_COMPILER_HAS_EXPLICIT_OPERATORS		0
	#define PLATFORM_COMPILER_HAS_DEFAULT_FUNCTION_TEMPLATE_ARGUMENTS	0
#endif
#define PLATFORM_COMPILER_HAS_TCHAR_WMAIN					1

#define PLATFORM_RHITHREAD_DEFAULT_BYPASS					1
#define PLATFORM_CAN_TOGGLE_RHITHREAD_IN_SHIPPING			1

#define PLATFORM_SUPPORTS_STACK_SYMBOLS						1

// Intrinsics for 128-bit atomics on Windows platform requires Windows 8 or higher (WINVER>0x0602)
// http://msdn.microsoft.com/en-us/library/windows/desktop/hh972640.aspx
#define PLATFORM_HAS_128BIT_ATOMICS							(!HACK_HEADER_GENERATOR && PLATFORM_64BITS && (WINVER >= 0x602))
#define PLATFORM_USES_ANSI_STRING_FOR_EXTERNAL_PROFILING	0

// Function type macros.
#define VARARGS     __cdecl														/* Functions with variable arguments */
#define CDECL	    __cdecl														/* Standard C function */
#define STDCALL		__stdcall													/* Standard calling convention */
#define FORCEINLINE __forceinline												/* Force code to be inline */
#define FORCENOINLINE __declspec(noinline)										/* Force code to NOT be inline */
#define FUNCTION_CHECK_RETURN(...) __declspec("SAL_checkReturn") __VA_ARGS__	/* Wrap a function signature in this to warn that callers should not ignore the return value. */

// Hints compiler that expression is true; generally restricted to comparisons against constants
#if !defined(__clang__)		// Clang doesn't support __assume (Microsoft specific)
	#define ASSUME(expr) __assume(expr)
#endif

#define DECLARE_UINT64(x)	x

// Optimization macros (uses __pragma to enable inside a #define).
#if !defined(__clang__)		// @todo clang: Clang doesn't appear to support optimization pragmas yet
	#define PRAGMA_DISABLE_OPTIMIZATION_ACTUAL __pragma(optimize("",off))
	#define PRAGMA_ENABLE_OPTIMIZATION_ACTUAL  __pragma(optimize("",on))
#endif

// Backwater of the spec. All compilers support this except microsoft, and they will soon
#if !defined(__clang__)		// Clang expects typename outside template
	#define TYPENAME_OUTSIDE_TEMPLATE
#endif

#pragma warning(disable : 4481) // nonstandard extension used: override specifier 'override'

#if defined(__clang__)
	#define CONSTEXPR constexpr
#else
	#define CONSTEXPR
#endif
#define ABSTRACT abstract

// Strings.
#define LINE_TERMINATOR TEXT("\r\n")
#define LINE_TERMINATOR_ANSI "\r\n"

// Alignment.
#if defined(__clang__)
	#define GCC_PACK(n) __attribute__((packed,aligned(n)))
	#define GCC_ALIGN(n) __attribute__((aligned(n)))
#else
	#define MS_ALIGN(n) __declspec(align(n))
#endif

// Pragmas
#define MSVC_PRAGMA(Pragma) __pragma(Pragma)

// Prefetch
#define PLATFORM_CACHE_LINE_SIZE	128

// DLL export and import definitions
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)

// disable this now as it is annoying for generic platform implementations
#pragma warning(disable : 4100) // unreferenced formal parameter


// Include code analysis features
#include "WindowsPlatformCodeAnalysis.h"
