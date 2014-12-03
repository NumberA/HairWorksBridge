// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "EnvQueryTypes.h"
#include "DebugRenderSceneProxy.h"
#include "VisualLoggerExtension.h"
#include "EnvQueryDebugHelpers.generated.h"


#if  USE_EQS_DEBUGGER || ENABLE_VISUAL_LOG
namespace EQSDebug
{
	struct FItemData
	{
		FString Desc;
		int32 ItemIdx;
		float TotalScore;

		TArray<float> TestValues;
		TArray<float> TestScores;
	};

	struct FTestData
	{
		FString ShortName;
		FString Detailed;
	};

	// struct filled while collecting data (to store additional debug data needed to display per rendered item)
	struct FDebugHelper
	{
		FDebugHelper() : Location(FVector::ZeroVector), Radius(0), FailedTestIndex(INDEX_NONE){}
		FDebugHelper(FVector Loc, float R) : Location(Loc), Radius(R), FailedTestIndex(INDEX_NONE) {}
		FDebugHelper(FVector Loc, float R, const FString& Desc) : Location(Loc), Radius(R), FailedTestIndex(INDEX_NONE), AdditionalInformation(Desc) {}

		FVector Location;
		float Radius;
		int32 FailedTestIndex;
		float FailedScore;
		FString AdditionalInformation;
	};

	struct FQueryData
	{
		TArray<FItemData> Items;
		TArray<FTestData> Tests;
		TArray<FDebugRenderSceneProxy::FSphere> SolidSpheres;
		TArray<FDebugRenderSceneProxy::FText3d> Texts;
		TArray<FDebugHelper> RenderDebugHelpers;
		TArray<FString>	Options;
		int32 UsedOption;
		int32 NumValidItems;
		int32 Id;
		FString Name;
		float Timestamp;

		void Reset()
		{
			UsedOption = 0;
			Options.Reset();
			NumValidItems = 0;
			Id = INDEX_NONE;
			Name.Empty();
			Items.Reset();
			Tests.Reset();
			SolidSpheres.Reset();
			Texts.Reset();
			Timestamp = 0;
			RenderDebugHelpers.Reset();
		}
	};
}

FORCEINLINE
FArchive& operator<<(FArchive& Ar, FDebugRenderSceneProxy::FSphere& Data)
{
	Ar << Data.Radius;
	Ar << Data.Location;
	Ar << Data.Color;
	return Ar;
}

FORCEINLINE
FArchive& operator<<(FArchive& Ar, FDebugRenderSceneProxy::FText3d& Data)
{
	Ar << Data.Text;
	Ar << Data.Location;
	Ar << Data.Color;
	return Ar;
}

FORCEINLINE
FArchive& operator<<(FArchive& Ar, EQSDebug::FItemData& Data)
{
	Ar << Data.Desc;
	Ar << Data.ItemIdx;
	Ar << Data.TotalScore;
	Ar << Data.TestValues;
	Ar << Data.TestScores;
	return Ar;
}

FORCEINLINE
FArchive& operator<<(FArchive& Ar, EQSDebug::FTestData& Data)
{
	Ar << Data.ShortName;
	Ar << Data.Detailed;
	return Ar;
}

FORCEINLINE
FArchive& operator<<(FArchive& Ar, EQSDebug::FDebugHelper& Data)
{
	Ar << Data.Location;
	Ar << Data.Radius;
	Ar << Data.AdditionalInformation;
	Ar << Data.FailedTestIndex;
	return Ar;
}

FORCEINLINE
FArchive& operator<<(FArchive& Ar, EQSDebug::FQueryData& Data)
{
	Ar << Data.Items;
	Ar << Data.Tests;
	Ar << Data.SolidSpheres;
	Ar << Data.Texts;
	Ar << Data.NumValidItems;
	Ar << Data.Id;
	Ar << Data.Name;
	Ar << Data.Timestamp;
	Ar << Data.RenderDebugHelpers;
	Ar << Data.Options;
	Ar << Data.UsedOption;
	return Ar;
}

#endif //USE_EQS_DEBUGGER || ENABLE_VISUAL_LOG

#if ENABLE_VISUAL_LOG && USE_EQS_DEBUGGER
#	define UE_VLOG_EQS(Query, Category, Verbosity)  UEnvQueryDebugHelpers::LogQuery(Query, Category, ELogVerbosity::Verbosity);
#else
#	define UE_VLOG_EQS(Query, CategoryName, Verbosity)
#endif //ENABLE_VISUAL_LOG && USE_EQS_DEBUGGER

UCLASS(Abstract, CustomConstructor)
class AIMODULE_API UEnvQueryDebugHelpers : public UObject
{
	GENERATED_UCLASS_BODY()

	UEnvQueryDebugHelpers(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}
#if USE_EQS_DEBUGGER
	static void QueryToDebugData(struct FEnvQueryInstance& Query, EQSDebug::FQueryData& EQSLocalData);
	static void QueryToBlobArray(struct FEnvQueryInstance& Query, TArray<uint8>& BlobArray, bool bUseCompression = false);
	static void BlobArrayToDebugData(const TArray<uint8>& BlobArray, EQSDebug::FQueryData& EQSLocalData, bool bUseCompression = false);
#endif

#if ENABLE_VISUAL_LOG && USE_EQS_DEBUGGER
	static void LogQuery(struct FEnvQueryInstance& Query, const struct FLogCategoryBase& Category, ELogVerbosity::Type Verbosity);

private:
	static void LogQueryInternal(struct FEnvQueryInstance& Query, const struct FLogCategoryBase& Category, ELogVerbosity::Type Verbosity, float TimeSeconds, FVisualLogEntry *CurrentEntry);
#endif
};

#if ENABLE_VISUAL_LOG && USE_EQS_DEBUGGER
FORCEINLINE void UEnvQueryDebugHelpers::LogQuery(struct FEnvQueryInstance& Query, const struct FLogCategoryBase& Category, ELogVerbosity::Type Verbosity)
{
	UWorld *World = NULL;
	FVisualLogEntry *CurrentEntry = NULL;
	if (CheckVisualLogInputInternal(Query.Owner.Get(), Category, Verbosity, &World, &CurrentEntry) == false)
	{
		return;
	}

	LogQueryInternal(Query, Category, Verbosity, World->TimeSeconds, CurrentEntry);
}
#endif
