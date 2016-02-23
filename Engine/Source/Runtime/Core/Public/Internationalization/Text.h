﻿// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Containers/UnrealString.h"
#include "Internationalization/CulturePointer.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"
#include "ITextData.h"
#include "TextLocalizationManager.h"
#include "Optional.h"
#include "UniquePtr.h"

struct FTimespan;
struct FDateTime;

class FTextHistory;

#define ENABLE_TEXT_ERROR_CHECKING_RESULTS (UE_BUILD_DEBUG | UE_BUILD_DEVELOPMENT | UE_BUILD_TEST )


//DECLARE_CYCLE_STAT_EXTERN( TEXT("Format Text"), STAT_TextFormat, STATGROUP_Text, );

namespace ETextFlag
{
	enum Type
	{
		Transient = (1 << 0),
		CultureInvariant = (1 << 1),
		ConvertedProperty = (1 << 2),
		Immutable = (1 << 3),
		InitializedFromString = (1<<4),  // this ftext was initialized using FromString
	};
}

namespace ETextComparisonLevel
{
	enum Type
	{
		Default,	// Locale-specific Default
		Primary,	// Base
		Secondary,	// Accent
		Tertiary,	// Case
		Quaternary,	// Punctuation
		Quinary		// Identical
	};
}

namespace EDateTimeStyle
{
	enum Type
	{
		Default,
		Short,
		Medium,
		Long,
		Full
		// Add new enum types at the end only! They are serialized by index.
	};
}

namespace EFormatArgumentType
{
	enum Type
	{
		Int,
		UInt,
		Float,
		Double,
		Text
		// Add new enum types at the end only! They are serialized by index.
	};
}

class FFormatArgumentValue;
typedef TMap<FString, FFormatArgumentValue> FFormatNamedArguments;
typedef TArray<FFormatArgumentValue> FFormatOrderedArguments;

/** Redeclared in KismetTextLibrary for meta-data extraction purposes, be sure to update there as well */
enum ERoundingMode
{
	/** Rounds to the nearest place, equidistant ties go to the value which is closest to an even value: 1.5 becomes 2, 0.5 becomes 0 */
	HalfToEven,
	/** Rounds to nearest place, equidistant ties go to the value which is further from zero: -0.5 becomes -1.0, 0.5 becomes 1.0 */
	HalfFromZero,
	/** Rounds to nearest place, equidistant ties go to the value which is closer to zero: -0.5 becomes 0, 0.5 becomes 0. */
	HalfToZero,
	/** Rounds to the value which is further from zero, "larger" in absolute value: 0.1 becomes 1, -0.1 becomes -1 */
	FromZero,
	/** Rounds to the value which is closer to zero, "smaller" in absolute value: 0.1 becomes 0, -0.1 becomes 0 */
	ToZero,
	/** Rounds to the value which is more negative: 0.1 becomes 0, -0.1 becomes -1 */
	ToNegativeInfinity,
	/** Rounds to the value which is more positive: 0.1 becomes 1, -0.1 becomes 0 */
	ToPositiveInfinity,


	// Add new enum types at the end only! They are serialized by index.
};

struct CORE_API FNumberFormattingOptions
{
	FNumberFormattingOptions();

	bool UseGrouping;
	FNumberFormattingOptions& SetUseGrouping( bool InValue ){ UseGrouping = InValue; return *this; }

	ERoundingMode RoundingMode;
	FNumberFormattingOptions& SetRoundingMode( ERoundingMode InValue ){ RoundingMode = InValue; return *this; }

	int32 MinimumIntegralDigits;
	FNumberFormattingOptions& SetMinimumIntegralDigits( int32 InValue ){ MinimumIntegralDigits = InValue; return *this; }

	int32 MaximumIntegralDigits;
	FNumberFormattingOptions& SetMaximumIntegralDigits( int32 InValue ){ MaximumIntegralDigits = InValue; return *this; }

	int32 MinimumFractionalDigits;
	FNumberFormattingOptions& SetMinimumFractionalDigits( int32 InValue ){ MinimumFractionalDigits = InValue; return *this; }

	int32 MaximumFractionalDigits;
	FNumberFormattingOptions& SetMaximumFractionalDigits( int32 InValue ){ MaximumFractionalDigits = InValue; return *this; }

	friend FArchive& operator<<(FArchive& Ar, FNumberFormattingOptions& Value);

	/** Get the hash code to use for the given formatting options */
	friend uint32 GetTypeHash( const FNumberFormattingOptions& Key );

	/** Check to see if our formatting options match the other formatting options */
	bool IsIdentical( const FNumberFormattingOptions& Other ) const;

	/** Get the default number formatting options with grouping enabled */
	static const FNumberFormattingOptions& DefaultWithGrouping();

	/** Get the default number formatting options with grouping disabled */
	static const FNumberFormattingOptions& DefaultNoGrouping();
};

class FCulture;

class CORE_API FText
{
public:

	static const FText& GetEmpty()
	{
		// This is initialized inside this function as we need to be able to control the initialization order of the empty FText instance
		// If this were a file-scope static, we can end up with other statics trying to construct an empty FText before our empty FText has itself been constructed
		static const FText StaticEmptyText = FText(FText::EInitToEmptyString::Value);
		return StaticEmptyText;
	}

public:

	FText();
	FText( const FText& Source );
	FText(FText&& Source);

	FText& operator=(const FText& Source);
	FText& operator=(FText&& Source);

	/**
	 * Generate an FText that represents the passed number in the current culture
	 */
	static FText AsNumber(float Val,	const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsNumber(double Val,	const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsNumber(int8 Val,		const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsNumber(int16 Val,	const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsNumber(int32 Val,	const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsNumber(int64 Val,	const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsNumber(uint8 Val,	const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsNumber(uint16 Val,	const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsNumber(uint32 Val,	const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsNumber(uint64 Val,	const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsNumber(long Val,		const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);

	/**
	 * Generate an FText that represents the passed number as currency in the current culture
	 */
	static FText AsCurrency(float Val,  const FString& CurrencyCode = FString(), const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsCurrency(double Val, const FString& CurrencyCode = FString(), const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsCurrency(int8 Val,   const FString& CurrencyCode = FString(), const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsCurrency(int16 Val,  const FString& CurrencyCode = FString(), const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsCurrency(int32 Val,  const FString& CurrencyCode = FString(), const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsCurrency(int64 Val,  const FString& CurrencyCode = FString(), const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsCurrency(uint8 Val,  const FString& CurrencyCode = FString(), const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsCurrency(uint16 Val, const FString& CurrencyCode = FString(), const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsCurrency(uint32 Val, const FString& CurrencyCode = FString(), const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsCurrency(uint64 Val, const FString& CurrencyCode = FString(), const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsCurrency(long Val,   const FString& CurrencyCode = FString(), const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);

	/**
	 * Generate an FText that represents the passed number as a percentage in the current culture
	 */
	static FText AsPercent(float Val,	const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	static FText AsPercent(double Val,	const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);

	/**
	 * Generate an FText that represents the passed number as a date and/or time in the current culture
	 */
	static FText AsDate(const FDateTime& DateTime, const EDateTimeStyle::Type DateStyle = EDateTimeStyle::Default, const FString& TimeZone = TEXT(""), const FCulturePtr& TargetCulture = NULL);
	static FText AsDateTime(const FDateTime& DateTime, const EDateTimeStyle::Type DateStyle = EDateTimeStyle::Default, const EDateTimeStyle::Type TimeStyle = EDateTimeStyle::Default, const FString& TimeZone = TEXT(""), const FCulturePtr& TargetCulture = NULL);
	static FText AsTime(const FDateTime& DateTime, const EDateTimeStyle::Type TimeStyle = EDateTimeStyle::Default, const FString& TimeZone = TEXT(""), const FCulturePtr& TargetCulture = NULL);
	static FText AsTimespan(const FTimespan& Timespan, const FCulturePtr& TargetCulture = NULL);

	/**
	 * Gets the time zone string that represents a non-specific, zero offset, culture invariant time zone.
	 */
	static FString GetInvariantTimeZone();

	/**
	 * Generate an FText that represents the passed number as a memory size in the current culture
	 */
	static FText AsMemory(uint64 NumBytes, const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);

	/**
	 * Attempts to find an existing FText using the representation found in the loc tables for the specified namespace and key
	 * @return true if OutText was properly set; otherwise false and OutText will be untouched
	 */
	static bool FindText( const FString& Namespace, const FString& Key, FText& OutText, const FString* const SourceString = nullptr );

	/**
	 * Generate an FText representing the pass name
	 */
	static FText FromName( const FName& Val);
	
	/**
	 * Generate an FText representing the passed in string
	 */
	static FText FromString( FString String );

	/**
	 * Generate a culture invariant FText representing the passed in string
	 */
	static FText AsCultureInvariant( FString String );

	/**
	 * Generate a culture invariant FText representing the passed in FText
	 */
	static FText AsCultureInvariant( FText Text );

	const FString& ToString() const;

	/** Deep build of the source string for this FText, climbing the history hierarchy */
	FString BuildSourceString() const;

	bool IsNumeric() const;

	int32 CompareTo( const FText& Other, const ETextComparisonLevel::Type ComparisonLevel = ETextComparisonLevel::Default ) const;
	int32 CompareToCaseIgnored( const FText& Other ) const;

	bool EqualTo( const FText& Other, const ETextComparisonLevel::Type ComparisonLevel = ETextComparisonLevel::Default ) const;
	bool EqualToCaseIgnored( const FText& Other ) const;

	/**
	 * Check to see if this FText is identical to the other FText
	 * 
	 * Note:	This doesn't compare the text, but only checks that the internal string pointers have the same target (which makes it very fast!)
	 *			If you actually want to perform a lexical comparison, then you need to use EqualTo instead
	 */
	bool IdenticalTo( const FText& Other ) const;

	/** Trace the history of this Text until we find the base Texts it was comprised from */
	void GetSourceTextsFromFormatHistory(TArray<FText>& OutSourceTexts) const;

	class CORE_API FSortPredicate
	{
	public:
		FSortPredicate(const ETextComparisonLevel::Type ComparisonLevel = ETextComparisonLevel::Default);

		bool operator()(const FText& A, const FText& B) const;

	private:
#if UE_ENABLE_ICU
		class FSortPredicateImplementation;
		TSharedRef<FSortPredicateImplementation> Implementation;
#endif
	};

	bool IsEmpty() const;

	bool IsEmptyOrWhitespace() const;

	/**
	 * Removes whitespace characters from the front of the string.
	 */
	static FText TrimPreceding( const FText& );

	/**
	 * Removes trailing whitespace characters
	 */
	static FText TrimTrailing( const FText& );

	/**
	 * Does both of the above without needing to create an additional FText in the interim.
	 */
	static FText TrimPrecedingAndTrailing( const FText& );

	/**
	 * Check to see if the given character is considered whitespace by the current culture
	 */
	static bool IsWhitespace( const TCHAR Char );

	/** Checks to see if the given character is considered a letter */
	static bool IsLetter( const TCHAR Char );

	static void GetFormatPatternParameters(const FText& Pattern, TArray<FString>& ParameterNames);

	static FText Format(FText Pattern, FFormatNamedArguments Arguments);
	static FText Format(FText Pattern, FFormatOrderedArguments Arguments);
	static FText Format(FText Pattern, TArray< struct FFormatArgumentData > InArguments);

	static FText Format(FText Fmt, FText v1);
	static FText Format(FText Fmt, FText v1, FText v2);
	static FText Format(FText Fmt, FText v1, FText v2, FText v3);
	static FText Format(FText Fmt, FText v1, FText v2, FText v3, FText v4);

#if PLATFORM_COMPILER_HAS_VARIADIC_TEMPLATES
	/**
	 * FormatNamed allows you to pass name <-> value pairs to the function to format automatically
	 *
	 * @usage FText::FormatNamed( FText::FromString( TEXT( "{PlayerName} is really cool" ) ), TEXT( "PlayerName" ), FText::FromString( TEXT( "Awesomegirl" ) ) );
	 *
	 * @param Fmt the format to create from
	 * @param Args a variadic list of FString to Value (must be even numbered)
	 * @return a formatted FText
	 */
	template < typename... TArguments >
	static FText FormatNamed( FText Fmt, TArguments&&... Args );

	/**
	 * FormatOrdered allows you to pass a variadic list of types to use for formatting in order desired
	 *
	 * @param Fmt the format to create from
	 * @param Args a variadic list of values in order of desired formatting
	 * @return a formatted FText
	 */
	template < typename... TArguments >
	static FText FormatOrdered( FText Fmt, TArguments&&... Args );
#endif // PLATFORM_COMPILER_HAS_VARIADIC_TEMPLATES

	static void SetEnableErrorCheckingResults(bool bEnable){bEnableErrorCheckingResults=bEnable;}
	static bool GetEnableErrorCheckingResults(){return bEnableErrorCheckingResults;}

	static void SetSuppressWarnings(bool bSuppress){ bSuppressWarnings = bSuppress; }
	static bool GetSuppressWarnings(){ return bSuppressWarnings; }

	bool IsTransient() const;
	bool IsCultureInvariant() const;

	bool ShouldGatherForLocalization() const;

private:

	/** Special constructor used to create StaticEmptyText without also allocating a history object */
	enum class EInitToEmptyString : uint8 { Value };
	explicit FText( EInitToEmptyString );

	explicit FText( TSharedRef<ITextData, ESPMode::ThreadSafe> InTextData );

	explicit FText( FString InSourceString );

	FText( FString InSourceString, const FString& InNamespace, const FString& InKey, uint32 InFlags=0 );

	friend CORE_API FArchive& operator<<( FArchive& Ar, FText& Value );

#if WITH_EDITOR
	/**
	 * Constructs a new FText with the SourceString of the specified text but with the specified namespace and key
	 */
	static FText ChangeKey( FString Namespace, FString Key, const FText& Text );
#endif

	/**
	 * Generate an FText for a string formatted numerically.
	 */
	static FText CreateNumericalText(TSharedRef<ITextData, ESPMode::ThreadSafe> InTextData);

	/**
	 * Generate an FText for a string formatted from a date/time.
	 */
	static FText CreateChronologicalText(TSharedRef<ITextData, ESPMode::ThreadSafe> InTextData);

	/** Returns the source string of the FText */
	const FString& GetSourceString() const;

	/** Rebuilds the FText under the current culture if needed */
	void Rebuild() const;

	static FText FormatInternal(FText Pattern, FFormatNamedArguments Arguments, bool bInRebuildText, bool bInRebuildAsSource);
	static FText FormatInternal(FText Pattern, FFormatOrderedArguments Arguments, bool bInRebuildText, bool bInRebuildAsSource);
	static FText FormatInternal(FText Pattern, TArray< struct FFormatArgumentData > InArguments, bool bInRebuildText, bool bInRebuildAsSource);

private:
	template<typename T1, typename T2>
	static FText AsNumberTemplate(T1 Val, const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	template<typename T1, typename T2>
	static FText AsCurrencyTemplate(T1 Val, const FString& CurrencyCode, const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);
	template<typename T1, typename T2>
	static FText AsPercentTemplate(T1 Val, const FNumberFormattingOptions* const Options = NULL, const FCulturePtr& TargetCulture = NULL);

private:
	/** The internal shared data for this FText */
	TSharedRef<ITextData, ESPMode::ThreadSafe> TextData;

	/** Flags with various information on what sort of FText this is */
	uint32 Flags;

	static bool bEnableErrorCheckingResults;
	static bool bSuppressWarnings;

public:
	//Some error text formats
	static const FText UnusedArgumentsError;
	static const FText CommentStartError;
	static const FText TooFewArgsErrorFormat;
	static const FText VeryLargeArgumentNumberErrorText;
	static const FText NoArgIndexError;
	static const FText NoClosingBraceError;
	static const FText OpenBraceInBlock;
	static const FText UnEscapedCloseBraceOutsideOfArgumentBlock;
	static const FText SerializationFailureError;

	friend class FTextCache;
	friend class FTextFormatHelper;
	friend class FTextSnapshot;
	friend class FTextInspector;
	friend class UStruct;
	friend class UGatherTextFromAssetsCommandlet;
	friend class UEdGraphSchema;
	friend class UEdGraphSchema_K2;
	friend class FTextHistory_NamedFormat;
	friend class FTextHistory_ArgumentDataFormat;
	friend class FTextHistory_OrderedFormat;
	friend class FScopedTextIdentityPreserver;
};

class CORE_API FFormatArgumentValue
{
public:
	FFormatArgumentValue()
		: Type(EFormatArgumentType::Int)
	{
		IntValue = 0;
	}

	FFormatArgumentValue(const int32 Value)
		: Type(EFormatArgumentType::Int)
	{
		IntValue = Value;
	}

	FFormatArgumentValue(const uint32 Value)
		: Type(EFormatArgumentType::UInt)
	{
		UIntValue = Value;
	}

	FFormatArgumentValue(const int64 Value)
		: Type(EFormatArgumentType::Int)
	{
		IntValue = Value;
	}

	FFormatArgumentValue(const uint64 Value)
		: Type(EFormatArgumentType::UInt)
	{
		UIntValue = Value;
	}

	FFormatArgumentValue(const float Value)
		: Type(EFormatArgumentType::Float)
	{
		FloatValue = Value;
	}

	FFormatArgumentValue(const double Value)
		: Type(EFormatArgumentType::Double)
	{
		DoubleValue = Value;
	}

	FFormatArgumentValue(FText Value)
		: Type(EFormatArgumentType::Text)
		, TextValue(MoveTemp(Value))
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FFormatArgumentValue& Value);

	FORCEINLINE EFormatArgumentType::Type GetType() const
	{
		return Type;
	}

	FORCEINLINE int64 GetIntValue() const
	{
		check(Type == EFormatArgumentType::Int);
		return IntValue;
	}

	FORCEINLINE uint64 GetUIntValue() const
	{
		check(Type == EFormatArgumentType::UInt);
		return UIntValue;
	}

	FORCEINLINE float GetFloatValue() const
	{
		check(Type == EFormatArgumentType::Float);
		return FloatValue;
	}

	FORCEINLINE double GetDoubleValue() const
	{
		check(Type == EFormatArgumentType::Double);
		return DoubleValue;
	}

	FORCEINLINE const FText& GetTextValue() const
	{
		check(Type == EFormatArgumentType::Text);
		return TextValue.GetValue();
	}

private:
	EFormatArgumentType::Type Type;
	union
	{
		int64 IntValue;
		uint64 UIntValue;
		float FloatValue;
		double DoubleValue;
	};
	TOptional<FText> TextValue;
};

/**
 * Used to pass argument/value pairs into FText::Format.
 * The UHT struct is located here: Engine\Source\Runtime\Engine\Classes\Kismet\KismetTextLibrary.h
 */
struct FFormatArgumentData
{
	FString ArgumentName;
	FText ArgumentValue;

	friend inline FArchive& operator<<( FArchive& Ar, FFormatArgumentData& Value )
	{
		Ar << Value.ArgumentName;
		Ar << Value.ArgumentValue;
		return Ar;
	}
};

#if PLATFORM_COMPILER_HAS_VARIADIC_TEMPLATES
namespace TextFormatUtil
{

template < typename TName, typename TValue >
void FormatNamed( OUT FFormatNamedArguments& Result, TName&& Name, TValue&& Value )
{
	Result.Emplace( Forward< TName >( Name ), Forward< TValue >( Value ) );
}

template < typename TName, typename TValue, typename... TArguments >
void FormatNamed( OUT FFormatNamedArguments& Result, TName&& Name, TValue&& Value, TArguments&&... Args )
{
	FormatNamed( Result, Forward< TName >( Name ), Forward< TValue >( Value ) );
	FormatNamed( Result, Forward< TArguments >( Args )... );
}

template < typename TValue >
void FormatOrdered( OUT FFormatOrderedArguments& Result, TValue&& Value )
{
	Result.Emplace( Forward< TValue >( Value ) );
}

template < typename TValue, typename... TArguments >
void FormatOrdered( OUT FFormatOrderedArguments& Result, TValue&& Value, TArguments&&... Args )
{
	FormatOrdered( Result, Forward< TValue >( Value ) );
	FormatOrdered( Result, Forward< TArguments >( Args )... );
}

} // namespace TextFormatUtil

template < typename... TArguments >
FText FText::FormatNamed( FText Fmt, TArguments&&... Args )
{
	static_assert( sizeof...( TArguments ) % 2 == 0, "FormatNamed requires an even number of Name <-> Value pairs" );

	FFormatNamedArguments FormatArguments;
	TextFormatUtil::FormatNamed( FormatArguments, Forward< TArguments >( Args )... );
	return FormatInternal( MoveTemp( Fmt ), MoveTemp( FormatArguments ), false, false );
}

template < typename... TArguments >
FText FText::FormatOrdered( FText Fmt, TArguments&&... Args )
{
	FFormatOrderedArguments FormatArguments;
	TextFormatUtil::FormatOrdered( FormatArguments, Forward< TArguments >( Args )... );
	return FormatInternal( MoveTemp( Fmt ), MoveTemp( FormatArguments ), false, false );
}
#endif // PLATFORM_COMPILER_HAS_VARIADIC_TEMPLATES

/** A snapshot of an FText at a point in time that can be used to detect changes in the FText, including live-culture changes */
class CORE_API FTextSnapshot
{
public:
	FTextSnapshot();

	explicit FTextSnapshot(const FText& InText);

	/** Check to see whether the given text is identical to the text this snapshot was made from */
	bool IdenticalTo(const FText& InText) const;

	/** Check to see whether the display string of the given text is identical to the display string this snapshot was made from */
	bool IsDisplayStringEqualTo(const FText& InText) const;

private:
	/** A pointer to the text data for the FText that we took a snapshot of (used for an efficient pointer compare) */
	TSharedPtr<ITextData, ESPMode::ThreadSafe> TextDataPtr;

	/** Revision index of the history of the FText we took a snapshot of, or INDEX_NONE if there was no history */
	int32 HistoryRevision;

	/** Flags with various information on what sort of FText we took a snapshot of */
	uint32 Flags;
};

class CORE_API FTextInspector
{
private:
	FTextInspector() {}
	~FTextInspector() {}

public:
	static bool ShouldGatherForLocalization(const FText& Text);
	static TOptional<FString> GetNamespace(const FText& Text);
	static TOptional<FString> GetKey(const FText& Text);
	static const FString* GetSourceString(const FText& Text);
	static const FString& GetDisplayString(const FText& Text);
	static const FTextDisplayStringRef GetSharedDisplayString(const FText& Text);
	static uint32 GetFlags(const FText& Text);
};

class CORE_API FTextBuilder
{
public:

	FTextBuilder()
		: IndentCount(0)
	{

	}

	void Indent()
	{
		++IndentCount;
	}

	void Unindent()
	{
		--IndentCount;
	}

	void AppendLine()
	{
		if (!Report.IsEmpty())
		{
			Report += LINE_TERMINATOR;
		}

		for (int32 Index = 0; Index < IndentCount; Index++)
		{
			Report += TEXT("    ");
		}
	}

	void AppendLine(const FText& Text)
	{
		AppendLine();
		Report += Text.ToString();
	}

	void AppendLine(const FString& String)
	{
		AppendLine();
		Report += String;
	}

	void AppendLine(const FName& Name)
	{
		AppendLine();
		Report += Name.ToString();
	}

	void AppendLineFormat(const FText& Pattern, const FFormatNamedArguments& Arguments)
	{
		AppendLine(FText::Format(Pattern, Arguments));
	}

	void AppendLineFormat(const FText& Pattern, const FFormatOrderedArguments& Arguments)
	{
		AppendLine(FText::Format(Pattern, Arguments));
	}

	void AppendLineFormat(const FText& Pattern, const TArray< struct FFormatArgumentData > InArguments)
	{
		AppendLine(FText::Format(Pattern, InArguments));
	}

	void AppendLineFormat(const FText& Fmt, const FText& v1)
	{
		AppendLine(FText::Format(Fmt, v1));
	}

	void AppendLineFormat(const FText& Fmt, const FText& v1, const FText& v2)
	{
		AppendLine(FText::Format(Fmt, v1, v2));
	}

	void AppendLineFormat(const FText& Fmt, const FText& v1, const FText& v2, const FText& v3)
	{
		AppendLine(FText::Format(Fmt, v1, v2, v3));
	}

	void AppendLineFormat(const FText& Fmt, const FText& v1, const FText& v2, const FText& v3, const FText& v4)
	{
		AppendLine(FText::Format(Fmt, v1, v2, v3, v4));
	}

	void Clear()
	{
		Report.Empty();
	}

	FText ToText() const
	{
		return FText::FromString(Report);
	}

private:
	FString Report;
	int32 IndentCount;
};

class CORE_API FScopedTextIdentityPreserver
{
public:
	FScopedTextIdentityPreserver(FText& InTextToPersist);
	~FScopedTextIdentityPreserver();

private:
	FText& TextToPersist;
	bool HadFoundNamespaceAndKey;
	FString Namespace;
	FString Key;
	uint32 Flags;
};

/**
 * Unicode Bidirectional text support 
 * http://www.unicode.org/reports/tr9/
 */
namespace TextBiDi
{
	/** Lists the potential reading directions for text */
	enum class ETextDirection : uint8
	{
		/** Contains only LTR text - requires simple LTR layout */
		LeftToRight,
		/** Contains only RTL text - requires simple RTL layout */
		RightToLeft,
		/** Contains both LTR and RTL text - requires more complex layout using multiple runs of text */
		Mixed,
	};

	/** A single complex layout entry. Defines the starting position, length, and reading direction for a sub-section of text */
	struct FTextDirectionInfo
	{
		int32 StartIndex;
		int32 Length;
		ETextDirection TextDirection;
	};

	/** Defines the interface for a re-usable BiDi object */
	class CORE_API ITextBiDi
	{
	public:
		virtual ~ITextBiDi() {}

		/** See TextBiDi::ComputeTextDirection */
		virtual ETextDirection ComputeTextDirection(const FText& InText) = 0;
		virtual ETextDirection ComputeTextDirection(const FString& InString) = 0;
		virtual ETextDirection ComputeTextDirection(const TCHAR* InString, const int32 InStringStartIndex, const int32 InStringLen) = 0;

		/** See TextBiDi::ComputeTextDirection */
		virtual ETextDirection ComputeTextDirection(const FText& InText, const ETextDirection InBaseDirection, TArray<FTextDirectionInfo>& OutTextDirectionInfo) = 0;
		virtual ETextDirection ComputeTextDirection(const FString& InString, const ETextDirection InBaseDirection, TArray<FTextDirectionInfo>& OutTextDirectionInfo) = 0;
		virtual ETextDirection ComputeTextDirection(const TCHAR* InString, const int32 InStringStartIndex, const int32 InStringLen, const ETextDirection InBaseDirection, TArray<FTextDirectionInfo>& OutTextDirectionInfo) = 0;

		/** See TextBiDi::ComputeBaseDirection */
		virtual ETextDirection ComputeBaseDirection(const FText& InText) = 0;
		virtual ETextDirection ComputeBaseDirection(const FString& InString) = 0;
		virtual ETextDirection ComputeBaseDirection(const TCHAR* InString, const int32 InStringStartIndex, const int32 InStringLen) = 0;
	};

	/**
	 * Create a re-usable BiDi object.
	 * This may yield better performance than the utility functions if you're performing a lot of BiDi requests, as this object can re-use allocated data between requests.
	 */
	CORE_API TUniquePtr<ITextBiDi> CreateTextBiDi();

	/**
	 * Utility function which will compute the reading direction of the given text.
	 * @note You may want to use the version that returns you the advanced layout data in the Mixed case.
	 * @return LeftToRight if all of the text is LTR, RightToLeft if all of the text is RTL, or Mixed if the text contains both LTR and RTL text.
	 */
	CORE_API ETextDirection ComputeTextDirection(const FText& InText);
	CORE_API ETextDirection ComputeTextDirection(const FString& InString);
	CORE_API ETextDirection ComputeTextDirection(const TCHAR* InString, const int32 InStringStartIndex, const int32 InStringLen);

	/**
	 * Utility function which will compute the reading direction of the given text, as well as populate any advanced layout data for the text.
	 * The base direction is the overall reading direction of the text (see ComputeBaseDirection). This will affect where some characters (such as brackets and quotes) are placed within the resultant FTextDirectionInfo data.
	 * @return LeftToRight if all of the text is LTR, RightToLeft if all of the text is RTL, or Mixed if the text contains both LTR and RTL text.
	 */
	CORE_API ETextDirection ComputeTextDirection(const FText& InText, const ETextDirection InBaseDirection, TArray<FTextDirectionInfo>& OutTextDirectionInfo);
	CORE_API ETextDirection ComputeTextDirection(const FString& InString, const ETextDirection InBaseDirection, TArray<FTextDirectionInfo>& OutTextDirectionInfo);
	CORE_API ETextDirection ComputeTextDirection(const TCHAR* InString, const int32 InStringStartIndex, const int32 InStringLen, const ETextDirection InBaseDirection, TArray<FTextDirectionInfo>& OutTextDirectionInfo);

	/**
	 * Utility function which will compute the base direction of the given text.
	 * This provides the text flow direction that should be used when combining bidirectional text runs together.
	 * @return RightToLeft if the first character in the string has a bidirectional character type of R or AL, otherwise LeftToRight.
	 */
	CORE_API ETextDirection ComputeBaseDirection(const FText& InText);
	CORE_API ETextDirection ComputeBaseDirection(const FString& InString);
	CORE_API ETextDirection ComputeBaseDirection(const TCHAR* InString, const int32 InStringStartIndex, const int32 InStringLen);

	/**
	 * Utility function which tests to see whether the given character is a bidirectional control character.
	 */
	CORE_API bool IsControlCharacter(const TCHAR InChar);
} // namespace TextBiDi

Expose_TNameOf(FText)
