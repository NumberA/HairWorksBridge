// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "CorePrivatePCH.h"

#include "TextHistory.h"
#include "PropertyPortFlags.h"

///////////////////////////////////////
// FTextHistory

/** Base class for all FText history types */

FTextHistory::FTextHistory()
	: Revision(FTextLocalizationManager::Get().GetTextRevision())
{
}

FTextHistory::FTextHistory(FTextHistory&& Other)
	: Revision(MoveTemp(Other.Revision))
{
}

FTextHistory& FTextHistory::operator=(FTextHistory&& Other)
{
	if (this != &Other)
	{
		Revision = Other.Revision;
	}
	return *this;
}

bool FTextHistory::IsOutOfDate() const
{
	return Revision < FTextLocalizationManager::Get().GetTextRevision();
}

const FString* FTextHistory::GetSourceString() const
{
	return NULL;
}

void FTextHistory::GetSourceTextsFromFormatHistory(FText Text, TArray<FText>& OutSourceTexts) const
{
	// If we get here, we have no more source so this must be the base text
	OutSourceTexts.Add(Text);
}

void FTextHistory::SerializeForDisplayString(FArchive& Ar, FTextDisplayStringPtr& InOutDisplayString)
{
	if(Ar.IsLoading())
	{
		// We will definitely need to do a rebuild later
		Revision = INDEX_NONE;

		//When duplicating, the CDO is used as the template, then values for the instance are assigned.
		//If we don't duplicate the string, the CDO and the instance are both pointing at the same thing.
		//This would result in all subsequently duplicated objects stamping over formerly duplicated ones.
		InOutDisplayString = MakeShareable(new FString());
	}
}

void FTextHistory::Rebuild(TSharedRef< FString, ESPMode::ThreadSafe > InDisplayString)
{
	const bool bIsOutOfDate = IsOutOfDate();
	if(bIsOutOfDate)
	{
		// FTextHistory_Base will never report being able to rebuild its text, but we need to keep the history 
		// revision in sync with the head culture so that FTextSnapshot::IdenticalTo still works correctly
		Revision = FTextLocalizationManager::Get().GetTextRevision();

		const bool bCanRebuildText = CanRebuildText();
		if(bCanRebuildText)
		{
			InDisplayString.Get() = FTextInspector::GetDisplayString(ToText(false));
		}
	}
}

///////////////////////////////////////
// FTextHistory_Base

FTextHistory_Base::FTextHistory_Base(FString InSourceString)
	: SourceString(MoveTemp(InSourceString))
{
}

FTextHistory_Base::FTextHistory_Base(FTextHistory_Base&& Other)
	: FTextHistory(MoveTemp(Other))
	, SourceString(MoveTemp(Other.SourceString))
{
}

FTextHistory_Base& FTextHistory_Base::operator=(FTextHistory_Base&& Other)
{
	FTextHistory::operator=(MoveTemp(Other));
	if (this != &Other)
	{
		SourceString = MoveTemp(Other.SourceString);
	}
	return *this;
}

FText FTextHistory_Base::ToText(bool bInAsSource) const
{
	if(bInAsSource)
	{
		return FText::FromString(SourceString);
	}

	// This should never be called except to build as source
	check(0);

	return FText::GetEmpty();
}

const FString* FTextHistory_Base::GetSourceString() const
{
	return &SourceString;
}

void FTextHistory_Base::Serialize( FArchive& Ar )
{
	// If I serialize out the Namespace and Key HERE, then we can load it up.
	if(Ar.IsSaving())
	{
		int8 HistoryType = (int8)ETextHistoryType::Base;
		Ar << HistoryType;
	}
}

void FTextHistory_Base::SerializeForDisplayString(FArchive& Ar, FTextDisplayStringPtr& InOutDisplayString)
{
	if(Ar.IsLoading())
	{
		// We will definitely need to do a rebuild later
		Revision = INDEX_NONE;

		FString Namespace;
		FString Key;

		Ar << Namespace;
		Ar << Key;
		Ar << SourceString;

		// Using the deserialized namespace and key, find the DisplayString.
		InOutDisplayString = FTextLocalizationManager::Get().GetDisplayString(Namespace, Key, &SourceString);
	}
	else if(Ar.IsSaving())
	{
		check(InOutDisplayString.IsValid());

		FString Namespace;
		FString Key;

		const bool FoundNamespaceAndKey = FTextLocalizationManager::Get().FindNamespaceAndKeyFromDisplayString(InOutDisplayString.ToSharedRef(), Namespace, Key);

		// If this has no namespace or key, attempt to give it a GUID for a key and register it.
		if (!FoundNamespaceAndKey && GIsEditor && (Ar.IsPersistent() && !Ar.HasAnyPortFlags(PPF_Duplicate)))
		{
			Key = FGuid::NewGuid().ToString();
			if (!FTextLocalizationManager::Get().AddDisplayString(InOutDisplayString.ToSharedRef(), Namespace, Key))
			{
				// Could not add display string, reset namespace and key.
				Namespace.Empty();
				Key.Empty();
			}
		}

		// Serialize the Namespace
		Ar << Namespace;

		// Serialize the Key
		Ar << Key;

		// Serialize the SourceString
		Ar << SourceString;
	}
}

///////////////////////////////////////
// FTextHistory_NamedFormat

FTextHistory_NamedFormat::FTextHistory_NamedFormat(FText InSourceText, FFormatNamedArguments InArguments)
	: SourceText(MoveTemp(InSourceText))
	, Arguments(MoveTemp(InArguments))
{
}

FTextHistory_NamedFormat::FTextHistory_NamedFormat(FTextHistory_NamedFormat&& Other)
	: FTextHistory(MoveTemp(Other))
	, SourceText(MoveTemp(Other.SourceText))
	, Arguments(MoveTemp(Other.Arguments))
{
}

FTextHistory_NamedFormat& FTextHistory_NamedFormat::operator=(FTextHistory_NamedFormat&& Other)
{
	FTextHistory::operator=(MoveTemp(Other));
	if (this != &Other)
	{
		SourceText = MoveTemp(Other.SourceText);
		Arguments = MoveTemp(Other.Arguments);
	}
	return *this;
}

FText FTextHistory_NamedFormat::ToText(bool bInAsSource) const
{
	return FText::FormatInternal(SourceText, Arguments, true, bInAsSource);
}

void FTextHistory_NamedFormat::Serialize( FArchive& Ar )
{
	if(Ar.IsSaving())
	{
		int8 HistoryType = (int8)ETextHistoryType::NamedFormat;
		Ar << HistoryType;
	}

	Ar << SourceText;
	Ar << Arguments;
}

void FTextHistory_NamedFormat::GetSourceTextsFromFormatHistory(FText, TArray<FText>& OutSourceTexts) const
{
	// Search the formatting text itself for source text
	SourceText.GetSourceTextsFromFormatHistory(OutSourceTexts);

	for (auto It = Arguments.CreateConstIterator(); It; ++It)
	{
		const FFormatArgumentValue& ArgumentValue = It.Value();
		if (ArgumentValue.GetType() == EFormatArgumentType::Text)
		{
			// Search any text arguments for source text
			const FText& TextValue = ArgumentValue.GetTextValue();
			TextValue.TextData->GetTextHistory().GetSourceTextsFromFormatHistory(TextValue, OutSourceTexts);
		}
	}
}

///////////////////////////////////////
// FTextHistory_OrderedFormat

FTextHistory_OrderedFormat::FTextHistory_OrderedFormat(FText InSourceText, FFormatOrderedArguments InArguments)
	: SourceText(MoveTemp(InSourceText))
	, Arguments(MoveTemp(InArguments))
{
}

FTextHistory_OrderedFormat::FTextHistory_OrderedFormat(FTextHistory_OrderedFormat&& Other)
	: FTextHistory(MoveTemp(Other))
	, SourceText(MoveTemp(Other.SourceText))
	, Arguments(MoveTemp(Other.Arguments))
{
}

FTextHistory_OrderedFormat& FTextHistory_OrderedFormat::operator=(FTextHistory_OrderedFormat&& Other)
{
	FTextHistory::operator=(MoveTemp(Other));
	if (this != &Other)
	{
		SourceText = MoveTemp(Other.SourceText);
		Arguments = MoveTemp(Other.Arguments);
	}
	return *this;
}

FText FTextHistory_OrderedFormat::ToText(bool bInAsSource) const
{
	return FText::FormatInternal(SourceText, Arguments, true, bInAsSource);
}

void FTextHistory_OrderedFormat::Serialize( FArchive& Ar )
{
	if(Ar.IsSaving())
	{
		int8 HistoryType = (int8)ETextHistoryType::OrderedFormat;
		Ar << HistoryType;
	}

	Ar << SourceText;
	Ar << Arguments;
}

void FTextHistory_OrderedFormat::GetSourceTextsFromFormatHistory(FText, TArray<FText>& OutSourceTexts) const
{
	// Search the formatting text itself for source text
	SourceText.GetSourceTextsFromFormatHistory(OutSourceTexts);

	for (auto It = Arguments.CreateConstIterator(); It; ++It)
	{
		const FFormatArgumentValue& ArgumentValue = *It;
		if (ArgumentValue.GetType() == EFormatArgumentType::Text)
		{
			// Search any text arguments for source text
			const FText& TextValue = ArgumentValue.GetTextValue();
			TextValue.TextData->GetTextHistory().GetSourceTextsFromFormatHistory(TextValue, OutSourceTexts);
		}
	}
}

///////////////////////////////////////
// FTextHistory_ArgumentDataFormat

FTextHistory_ArgumentDataFormat::FTextHistory_ArgumentDataFormat(FText InSourceText, TArray<FFormatArgumentData> InArguments)
	: SourceText(MoveTemp(InSourceText))
	, Arguments(MoveTemp(InArguments))
{
}

FTextHistory_ArgumentDataFormat::FTextHistory_ArgumentDataFormat(FTextHistory_ArgumentDataFormat&& Other)
	: FTextHistory(MoveTemp(Other))
	, SourceText(MoveTemp(Other.SourceText))
	, Arguments(MoveTemp(Other.Arguments))
{
}

FTextHistory_ArgumentDataFormat& FTextHistory_ArgumentDataFormat::operator=(FTextHistory_ArgumentDataFormat&& Other)
{
	FTextHistory::operator=(MoveTemp(Other));
	if (this != &Other)
	{
		SourceText = MoveTemp(Other.SourceText);
		Arguments = MoveTemp(Other.Arguments);
	}
	return *this;
}

FText FTextHistory_ArgumentDataFormat::ToText(bool bInAsSource) const
{
	return FText::FormatInternal(SourceText, Arguments, true, bInAsSource);
}

void FTextHistory_ArgumentDataFormat::Serialize( FArchive& Ar )
{
	if(Ar.IsSaving())
	{
		int8 HistoryType = (int8)ETextHistoryType::ArgumentFormat;
		Ar << HistoryType;
	}

	Ar << SourceText;
	Ar << Arguments;
}

void FTextHistory_ArgumentDataFormat::GetSourceTextsFromFormatHistory(FText, TArray<FText>& OutBaseTexts) const
{
	// Search the formatting text itself for source text
	SourceText.GetSourceTextsFromFormatHistory(OutBaseTexts);

	for (int32 x = 0; x < Arguments.Num(); ++x)
	{
		// Search any text arguments for source text
		const FText& TextValue = Arguments[x].ArgumentValue;
		TextValue.TextData->GetTextHistory().GetSourceTextsFromFormatHistory(TextValue, OutBaseTexts);
	}
}

///////////////////////////////////////
// FTextHistory_FormatNumber

FTextHistory_FormatNumber::FTextHistory_FormatNumber(FFormatArgumentValue InSourceValue, const FNumberFormattingOptions* const InFormatOptions, FCulturePtr InTargetCulture)
	: SourceValue(MoveTemp(InSourceValue))
	, FormatOptions()
	, TargetCulture(MoveTemp(InTargetCulture))
{
	if(InFormatOptions)
	{
		FormatOptions = *InFormatOptions;
	}
}

FTextHistory_FormatNumber::FTextHistory_FormatNumber(FTextHistory_FormatNumber&& Other)
	: FTextHistory(MoveTemp(Other))
	, SourceValue(MoveTemp(Other.SourceValue))
	, FormatOptions(MoveTemp(Other.FormatOptions))
	, TargetCulture(MoveTemp(Other.TargetCulture))
{
}

FTextHistory_FormatNumber& FTextHistory_FormatNumber::operator=(FTextHistory_FormatNumber&& Other)
{
	FTextHistory::operator=(MoveTemp(Other));
	if (this != &Other)
	{
		SourceValue = MoveTemp(Other.SourceValue);
		FormatOptions = MoveTemp(Other.FormatOptions);
		TargetCulture = MoveTemp(Other.TargetCulture);
	}
	return *this;
}

void FTextHistory_FormatNumber::Serialize(FArchive& Ar)
{
	Ar << SourceValue;

	bool bHasFormatOptions = FormatOptions.IsSet();
	Ar << bHasFormatOptions;

	if(Ar.IsLoading())
	{
		if(bHasFormatOptions)
		{
			FormatOptions = FNumberFormattingOptions();
		}
		else
		{
			FormatOptions.Reset();
		}
	}
	if(bHasFormatOptions)
	{
		check(FormatOptions.IsSet());
		Ar << FormatOptions.GetValue();
	}

	if(Ar.IsSaving())
	{
		FString CultureName = TargetCulture.IsValid()? TargetCulture->GetName() : FString();
		Ar << CultureName;
	}
	else if(Ar.IsLoading())
	{
		FString CultureName;
		Ar << CultureName;

		if(!CultureName.IsEmpty())
		{
			TargetCulture = FInternationalization::Get().GetCulture(CultureName);
		}
	}
}

///////////////////////////////////////
// FTextHistory_AsNumber

FTextHistory_AsNumber::FTextHistory_AsNumber(FFormatArgumentValue InSourceValue, const FNumberFormattingOptions* const InFormatOptions, FCulturePtr InTargetCulture)
	: FTextHistory_FormatNumber(MoveTemp(InSourceValue), InFormatOptions, MoveTemp(InTargetCulture))
{
}

FTextHistory_AsNumber::FTextHistory_AsNumber(FTextHistory_AsNumber&& Other)
	: FTextHistory_FormatNumber(MoveTemp(Other))
{
}

FTextHistory_AsNumber& FTextHistory_AsNumber::operator=(FTextHistory_AsNumber&& Other)
{
	FTextHistory_FormatNumber::operator=(MoveTemp(Other));
	return *this;
}

FText FTextHistory_AsNumber::ToText(bool bInAsSource) const
{
	TSharedPtr< FCulture, ESPMode::ThreadSafe > CurrentCulture = bInAsSource? FInternationalization::Get().GetInvariantCulture() : TargetCulture;

	const FNumberFormattingOptions* FormatOptionsPtr = (FormatOptions.IsSet()) ? &FormatOptions.GetValue() : nullptr;
	switch(SourceValue.GetType())
	{
	case EFormatArgumentType::UInt:
		{
			return FText::AsNumber(SourceValue.GetUIntValue(), FormatOptionsPtr, CurrentCulture);
		}
	case EFormatArgumentType::Int:
		{
			return FText::AsNumber(SourceValue.GetIntValue(), FormatOptionsPtr, CurrentCulture);
		}
	case EFormatArgumentType::Float:
		{
			return FText::AsNumber(SourceValue.GetFloatValue(), FormatOptionsPtr, CurrentCulture);
		}
	case EFormatArgumentType::Double:
		{
			return FText::AsNumber(SourceValue.GetDoubleValue(), FormatOptionsPtr, CurrentCulture);
		}
	default:
		{
			// Should never reach this point
			check(0);
		}
	}
	return FText();
}

void FTextHistory_AsNumber::Serialize( FArchive& Ar )
{
	if(Ar.IsSaving())
	{
		int8 HistoryType = (int8)ETextHistoryType::AsNumber;
		Ar << HistoryType;
	}

	FTextHistory_FormatNumber::Serialize(Ar);
}

///////////////////////////////////////
// FTextHistory_AsPercent

FTextHistory_AsPercent::FTextHistory_AsPercent(FFormatArgumentValue InSourceValue, const FNumberFormattingOptions* const InFormatOptions, FCulturePtr InTargetCulture)
	: FTextHistory_FormatNumber(MoveTemp(InSourceValue), InFormatOptions, MoveTemp(InTargetCulture))
{
}

FTextHistory_AsPercent::FTextHistory_AsPercent(FTextHistory_AsPercent&& Other)
	: FTextHistory_FormatNumber(MoveTemp(Other))
{
}

FTextHistory_AsPercent& FTextHistory_AsPercent::operator=(FTextHistory_AsPercent&& Other)
{
	FTextHistory_FormatNumber::operator=(MoveTemp(Other));
	return *this;
}

FText FTextHistory_AsPercent::ToText(bool bInAsSource) const
{
	TSharedPtr< FCulture, ESPMode::ThreadSafe > CurrentCulture = bInAsSource? FInternationalization::Get().GetInvariantCulture() : TargetCulture;

	const FNumberFormattingOptions* FormatOptionsPtr = (FormatOptions.IsSet()) ? &FormatOptions.GetValue() : nullptr;
	switch(SourceValue.GetType())
	{
	case EFormatArgumentType::Float:
		{
			return FText::AsPercent(SourceValue.GetFloatValue(), FormatOptionsPtr, CurrentCulture);
		}
	case EFormatArgumentType::Double:
		{
			return FText::AsPercent(SourceValue.GetDoubleValue(), FormatOptionsPtr, CurrentCulture);
		}
	default:
		{
			// Should never reach this point
			check(0);
		}
	}
	return FText();
}

void FTextHistory_AsPercent::Serialize( FArchive& Ar )
{
	if(Ar.IsSaving())
	{
		int8 HistoryType = (int8)ETextHistoryType::AsPercent;
		Ar << HistoryType;
	}

	FTextHistory_FormatNumber::Serialize(Ar);
}

///////////////////////////////////////
// FTextHistory_AsCurrency

FTextHistory_AsCurrency::FTextHistory_AsCurrency(FFormatArgumentValue InSourceValue, FString InCurrencyCode, const FNumberFormattingOptions* const InFormatOptions, FCulturePtr InTargetCulture)
	: FTextHistory_FormatNumber(MoveTemp(InSourceValue), InFormatOptions, MoveTemp(InTargetCulture))
	, CurrencyCode(MoveTemp(InCurrencyCode))
{
}

FTextHistory_AsCurrency::FTextHistory_AsCurrency(FTextHistory_AsCurrency&& Other)
	: FTextHistory_FormatNumber(MoveTemp(Other))
	, CurrencyCode(MoveTemp(Other.CurrencyCode))
{
}

FTextHistory_AsCurrency& FTextHistory_AsCurrency::operator=(FTextHistory_AsCurrency&& Other)
{
	FTextHistory_FormatNumber::operator=(MoveTemp(Other));
	if (this != &Other)
	{
		CurrencyCode = MoveTemp(Other.CurrencyCode);
	}
	return *this;
}

FText FTextHistory_AsCurrency::ToText(bool bInAsSource) const
{
	TSharedPtr< FCulture, ESPMode::ThreadSafe > CurrentCulture = bInAsSource? FInternationalization::Get().GetInvariantCulture() : TargetCulture;

	const FNumberFormattingOptions* FormatOptionsPtr = (FormatOptions.IsSet()) ? &FormatOptions.GetValue() : nullptr;
	switch(SourceValue.GetType())
	{
	case EFormatArgumentType::UInt:
		{
			return FText::AsCurrency(SourceValue.GetUIntValue(), CurrencyCode, FormatOptionsPtr, CurrentCulture);
		}
	case EFormatArgumentType::Int:
		{
			return FText::AsCurrency(SourceValue.GetIntValue(), CurrencyCode, FormatOptionsPtr, CurrentCulture);
		}
	case EFormatArgumentType::Float:
		{
			return FText::AsCurrency(SourceValue.GetFloatValue(), CurrencyCode, FormatOptionsPtr, CurrentCulture);
		}
	case EFormatArgumentType::Double:
		{
			return FText::AsCurrency(SourceValue.GetDoubleValue(), CurrencyCode, FormatOptionsPtr, CurrentCulture);
		}
	default:
		{
			// Should never reach this point
			check(0);
		}
	}
	return FText();
}

void FTextHistory_AsCurrency::Serialize( FArchive& Ar )
{
	if(Ar.IsSaving())
	{
		int8 HistoryType = (int8)ETextHistoryType::AsCurrency;
		Ar << HistoryType;
	}

	if (Ar.UE4Ver() >= VER_UE4_ADDED_CURRENCY_CODE_TO_FTEXT)
	{
		Ar << CurrencyCode;
	}

	FTextHistory_FormatNumber::Serialize(Ar);
}

///////////////////////////////////////
// FTextHistory_AsDate

FTextHistory_AsDate::FTextHistory_AsDate(FDateTime InSourceDateTime, const EDateTimeStyle::Type InDateStyle, FString InTimeZone, FCulturePtr InTargetCulture)
	: SourceDateTime(MoveTemp(InSourceDateTime))
	, DateStyle(InDateStyle)
	, TimeZone(MoveTemp(InTimeZone))
	, TargetCulture(MoveTemp(InTargetCulture))
{
}

FTextHistory_AsDate::FTextHistory_AsDate(FTextHistory_AsDate&& Other)
	: FTextHistory(MoveTemp(Other))
	, SourceDateTime(MoveTemp(Other.SourceDateTime))
	, DateStyle(Other.DateStyle)
	, TimeZone(MoveTemp(Other.TimeZone))
	, TargetCulture(MoveTemp(Other.TargetCulture))
{
}

FTextHistory_AsDate& FTextHistory_AsDate::operator=(FTextHistory_AsDate&& Other)
{
	FTextHistory::operator=(MoveTemp(Other));
	if (this != &Other)
	{
		SourceDateTime = MoveTemp(Other.SourceDateTime);
		DateStyle = Other.DateStyle;
		TimeZone = MoveTemp(Other.TimeZone);
		TargetCulture = MoveTemp(Other.TargetCulture);
	}
	return *this;
}

void FTextHistory_AsDate::Serialize(FArchive& Ar)
{
	if(Ar.IsSaving())
	{
		int8 HistoryType = (int8)ETextHistoryType::AsDate;
		Ar << HistoryType;
	}

	Ar << SourceDateTime;

	int8 DateStyleInt8 = (int8)DateStyle;
	Ar << DateStyleInt8;
	DateStyle = (EDateTimeStyle::Type)DateStyleInt8;

	if( Ar.UE4Ver() >= VER_UE4_FTEXT_HISTORY_DATE_TIMEZONE )
	{
		Ar << TimeZone;
	}

	if(Ar.IsSaving())
	{
		FString CultureName = TargetCulture.IsValid()? TargetCulture->GetName() : FString();
		Ar << CultureName;
	}
	else if(Ar.IsLoading())
	{
		FString CultureName;
		Ar << CultureName;

		if(!CultureName.IsEmpty())
		{
			TargetCulture = FInternationalization::Get().GetCulture(CultureName);
		}
	}
}

FText FTextHistory_AsDate::ToText(bool bInAsSource) const
{
	TSharedPtr< FCulture, ESPMode::ThreadSafe > CurrentCulture = bInAsSource? FInternationalization::Get().GetInvariantCulture() : TargetCulture;

	return FText::AsDate(SourceDateTime, DateStyle, TimeZone, CurrentCulture);
}
///////////////////////////////////////
// FTextHistory_AsTime

FTextHistory_AsTime::FTextHistory_AsTime(FDateTime InSourceDateTime, const EDateTimeStyle::Type InTimeStyle, FString InTimeZone, FCulturePtr InTargetCulture)
	: SourceDateTime(MoveTemp(InSourceDateTime))
	, TimeStyle(InTimeStyle)
	, TimeZone(MoveTemp(InTimeZone))
	, TargetCulture(MoveTemp(InTargetCulture))
{
}

FTextHistory_AsTime::FTextHistory_AsTime(FTextHistory_AsTime&& Other)
	: FTextHistory(MoveTemp(Other))
	, SourceDateTime(MoveTemp(Other.SourceDateTime))
	, TimeStyle(Other.TimeStyle)
	, TimeZone(MoveTemp(Other.TimeZone))
	, TargetCulture(MoveTemp(Other.TargetCulture))
{
}

FTextHistory_AsTime& FTextHistory_AsTime::operator=(FTextHistory_AsTime&& Other)
{
	FTextHistory::operator=(MoveTemp(Other));
	if (this != &Other)
	{
		SourceDateTime = MoveTemp(Other.SourceDateTime);
		TimeStyle = Other.TimeStyle;
		TimeZone = MoveTemp(Other.TimeZone);
		TargetCulture = MoveTemp(Other.TargetCulture);
	}
	return *this;
}

void FTextHistory_AsTime::Serialize(FArchive& Ar)
{
	if(Ar.IsSaving())
	{
		int8 HistoryType = (int8)ETextHistoryType::AsTime;
		Ar << HistoryType;
	}

	Ar << SourceDateTime;

	int8 TimeStyleInt8 = (int8)TimeStyle;
	Ar << TimeStyleInt8;
	TimeStyle = (EDateTimeStyle::Type)TimeStyleInt8;

	Ar << TimeZone;

	if(Ar.IsSaving())
	{
		FString CultureName = TargetCulture.IsValid()? TargetCulture->GetName() : FString();
		Ar << CultureName;
	}
	else if(Ar.IsLoading())
	{
		FString CultureName;
		Ar << CultureName;

		if(!CultureName.IsEmpty())
		{
			TargetCulture = FInternationalization::Get().GetCulture(CultureName);
		}
	}
}

FText FTextHistory_AsTime::ToText(bool bInAsSource) const
{
	TSharedPtr< FCulture, ESPMode::ThreadSafe > CurrentCulture = bInAsSource? FInternationalization::Get().GetInvariantCulture() : TargetCulture;
	
	return FText::AsTime(SourceDateTime, TimeStyle, TimeZone, TargetCulture);
}

///////////////////////////////////////
// FTextHistory_AsDateTime

FTextHistory_AsDateTime::FTextHistory_AsDateTime(FDateTime InSourceDateTime, const EDateTimeStyle::Type InDateStyle, const EDateTimeStyle::Type InTimeStyle, FString InTimeZone, FCulturePtr InTargetCulture)
	: SourceDateTime(MoveTemp(InSourceDateTime))
	, DateStyle(InDateStyle)
	, TimeStyle(InTimeStyle)
	, TimeZone(MoveTemp(InTimeZone))
	, TargetCulture(MoveTemp(InTargetCulture))
{
}

FTextHistory_AsDateTime::FTextHistory_AsDateTime(FTextHistory_AsDateTime&& Other)
	: FTextHistory(MoveTemp(Other))
	, SourceDateTime(MoveTemp(Other.SourceDateTime))
	, DateStyle(Other.DateStyle)
	, TimeStyle(Other.TimeStyle)
	, TimeZone(MoveTemp(Other.TimeZone))
	, TargetCulture(MoveTemp(Other.TargetCulture))
{
}

FTextHistory_AsDateTime& FTextHistory_AsDateTime::operator=(FTextHistory_AsDateTime&& Other)
{
	FTextHistory::operator=(MoveTemp(Other));
	if (this != &Other)
	{
		SourceDateTime = MoveTemp(Other.SourceDateTime);
		DateStyle = Other.DateStyle;
		TimeStyle = Other.TimeStyle;
		TimeZone = MoveTemp(Other.TimeZone);
		TargetCulture = MoveTemp(Other.TargetCulture);
	}
	return *this;
}

void FTextHistory_AsDateTime::Serialize(FArchive& Ar)
{
	if(Ar.IsSaving())
	{
		int8 HistoryType = (int8)ETextHistoryType::AsDateTime;
		Ar << HistoryType;
	}

	Ar << SourceDateTime;

	int8 DateStyleInt8 = (int8)DateStyle;
	Ar << DateStyleInt8;
	DateStyle = (EDateTimeStyle::Type)DateStyleInt8;

	int8 TimeStyleInt8 = (int8)TimeStyle;
	Ar << TimeStyleInt8;
	TimeStyle = (EDateTimeStyle::Type)TimeStyleInt8;

	Ar << TimeZone;

	if(Ar.IsSaving())
	{
		FString CultureName = TargetCulture.IsValid()? TargetCulture->GetName() : FString();
		Ar << CultureName;
	}
	else if(Ar.IsLoading())
	{
		FString CultureName;
		Ar << CultureName;

		if(!CultureName.IsEmpty())
		{
			TargetCulture = FInternationalization::Get().GetCulture(CultureName);
		}
	}
}

FText FTextHistory_AsDateTime::ToText(bool bInAsSource) const
{
	TSharedPtr< FCulture, ESPMode::ThreadSafe > CurrentCulture = bInAsSource? FInternationalization::Get().GetInvariantCulture() : TargetCulture;
	
	return FText::AsDateTime(SourceDateTime, DateStyle, TimeStyle, TimeZone, TargetCulture);
}
