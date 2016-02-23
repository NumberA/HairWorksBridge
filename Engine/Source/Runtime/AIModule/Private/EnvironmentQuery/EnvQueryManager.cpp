// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "AIModulePrivate.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvironmentQuery/EQSTestingPawn.h"
#include "EnvironmentQuery/EnvQueryDebugHelpers.h"
#include "EnvironmentQuery/EnvQueryInstanceBlueprintWrapper.h"

#if WITH_EDITOR
#include "UnrealEd.h"
#include "Engine/Brush.h"
#include "EngineUtils.h"

extern UNREALED_API UEditorEngine* GEditor;
#endif // WITH_EDITOR

DEFINE_LOG_CATEGORY(LogEQS);

DEFINE_STAT(STAT_AI_EQS_Tick);
DEFINE_STAT(STAT_AI_EQS_TickWork);
DEFINE_STAT(STAT_AI_EQS_TickNotifies);
DEFINE_STAT(STAT_AI_EQS_TickQueryRemovals);
DEFINE_STAT(STAT_AI_EQS_LoadTime);
DEFINE_STAT(STAT_AI_EQS_ExecuteOneStep);
DEFINE_STAT(STAT_AI_EQS_GeneratorTime);
DEFINE_STAT(STAT_AI_EQS_TestTime);
DEFINE_STAT(STAT_AI_EQS_NumInstances);
DEFINE_STAT(STAT_AI_EQS_NumItems);
DEFINE_STAT(STAT_AI_EQS_InstanceMemory);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	bool UEnvQueryManager::bAllowEQSTimeSlicing = true;
#endif

//////////////////////////////////////////////////////////////////////////
// FEnvQueryRequest

FEnvQueryRequest& FEnvQueryRequest::SetNamedParams(const TArray<FEnvNamedValue>& Params)
{
	for (int32 ParamIndex = 0; ParamIndex < Params.Num(); ParamIndex++)
	{
		NamedParams.Add(Params[ParamIndex].ParamName, Params[ParamIndex].Value);
	}

	return *this;
}

int32 FEnvQueryRequest::Execute(EEnvQueryRunMode::Type RunMode, FQueryFinishedSignature const& FinishDelegate)
{
	if (Owner == NULL)
	{
		Owner = FinishDelegate.GetUObject();
		if (Owner == NULL)
		{
			UE_LOG(LogEQS, Warning, TEXT("Unknown owner of request: %s"), *GetNameSafe(QueryTemplate));
			return INDEX_NONE;
		}
	}

	if (World == NULL)
	{
		World = GEngine->GetWorldFromContextObject(Owner);
		if (World == NULL)
		{
			UE_LOG(LogEQS, Warning, TEXT("Unable to access world with owner: %s"), *GetNameSafe(Owner));
			return INDEX_NONE;
		}
	}

	UEnvQueryManager* EnvQueryManager = UEnvQueryManager::GetCurrent(World);
	if (EnvQueryManager == NULL)
	{
		UE_LOG(LogEQS, Warning, TEXT("Missing EQS manager!"));
		return INDEX_NONE;
	}

	return EnvQueryManager->RunQuery(*this, RunMode, FinishDelegate);
}


//////////////////////////////////////////////////////////////////////////
// UEnvQueryManager

TArray<TSubclassOf<UEnvQueryItemType> > UEnvQueryManager::RegisteredItemTypes;

UEnvQueryManager::UEnvQueryManager(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	NextQueryID = 0;
}

UWorld* UEnvQueryManager::GetWorld() const
{
	return Cast<UWorld>(GetOuter());
}

void UEnvQueryManager::FinishDestroy()
{
	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	Super::FinishDestroy();
}

UEnvQueryManager* UEnvQueryManager::GetCurrent(UWorld* World)
{
	UAISystem* AISys = UAISystem::GetCurrentSafe(World);
	return AISys ? AISys->GetEnvironmentQueryManager() : NULL;
}

UEnvQueryManager* UEnvQueryManager::GetCurrent(const UObject* WorldContextObject)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, false);
	UAISystem* AISys = UAISystem::GetCurrentSafe(World);
	return AISys ? AISys->GetEnvironmentQueryManager() : NULL;
}

#if USE_EQS_DEBUGGER
void UEnvQueryManager::NotifyAssetUpdate(UEnvQuery* Query)
{
#if WITH_EDITOR
	if (GEditor == NULL)
	{
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		UEnvQueryManager* EQS = UEnvQueryManager::GetCurrent(World);
		if (EQS)
		{
			EQS->InstanceCache.Reset();
		}

		// was as follows, but got broken with changes to actor iterator (FActorIteratorBase::SpawnedActorArray)
		// for (TActorIterator<AEQSTestingPawn> It(World); It; ++It)
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AEQSTestingPawn* EQSPawn = Cast<AEQSTestingPawn>(*It);
			if (EQSPawn == NULL)
			{
				continue;
			}

			if (EQSPawn->QueryTemplate == Query || Query == NULL)
			{
				EQSPawn->RunEQSQuery();
			}
		}
	}
#endif //WITH_EDITOR
}
#endif // USE_EQS_DEBUGGER

TStatId UEnvQueryManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEnvQueryManager, STATGROUP_Tickables);
}

int32 UEnvQueryManager::RunQuery(const FEnvQueryRequest& Request, EEnvQueryRunMode::Type RunMode, FQueryFinishedSignature const& FinishDelegate)
{
	TSharedPtr<FEnvQueryInstance> QueryInstance = PrepareQueryInstance(Request, RunMode);
	return RunQuery(QueryInstance, FinishDelegate);
}

int32 UEnvQueryManager::RunQuery(TSharedPtr<FEnvQueryInstance> QueryInstance, FQueryFinishedSignature const& FinishDelegate)
{
	if (QueryInstance.IsValid() == false)
	{
		return INDEX_NONE;
	}

	QueryInstance->FinishDelegate = FinishDelegate;
	RunningQueries.Add(QueryInstance);

	return QueryInstance->QueryID;
}

TSharedPtr<FEnvQueryResult> UEnvQueryManager::RunInstantQuery(const FEnvQueryRequest& Request, EEnvQueryRunMode::Type RunMode)
{
	TSharedPtr<FEnvQueryInstance> QueryInstance = PrepareQueryInstance(Request, RunMode);
	if (!QueryInstance.IsValid())
	{
		return NULL;
	}

	RegisterExternalQuery(QueryInstance);
	while (QueryInstance->IsFinished() == false)
	{
		QueryInstance->ExecuteOneStep((double)FLT_MAX);
	}

	UnregisterExternalQuery(QueryInstance);

	UE_VLOG_EQS(*QueryInstance.Get(), LogEQS, All);

#if USE_EQS_DEBUGGER
	EQSDebugger.StoreQuery(GetWorld(), QueryInstance);
#endif // USE_EQS_DEBUGGER

	return QueryInstance;
}

void UEnvQueryManager::RemoveAllQueriesByQuerier(const UObject& Querier, bool bExecuteFinishDelegate)
{
	for (int32 QueryIndex = RunningQueries.Num() - 1; QueryIndex >= 0; --QueryIndex)
	{
		const TSharedPtr<FEnvQueryInstance>& QueryInstance = RunningQueries[QueryIndex];
		if (QueryInstance.IsValid() == false || QueryInstance->Owner.IsValid() == false || QueryInstance->Owner.Get() == &Querier)
		{
			if (bExecuteFinishDelegate && QueryInstance->IsFinished() == false)
			{
				QueryInstance->MarkAsAborted();
				QueryInstance->FinishDelegate.ExecuteIfBound(QueryInstance);
			}
			RunningQueries.RemoveAt(QueryIndex, 1, /*bAllowShrinking=*/false);
		}
	}
}

TSharedPtr<FEnvQueryInstance> UEnvQueryManager::PrepareQueryInstance(const FEnvQueryRequest& Request, EEnvQueryRunMode::Type RunMode)
{
	TSharedPtr<FEnvQueryInstance> QueryInstance = CreateQueryInstance(Request.QueryTemplate, RunMode);
	if (!QueryInstance.IsValid())
	{
		return NULL;
	}

	QueryInstance->World = Cast<UWorld>(GetOuter());
	QueryInstance->Owner = Request.Owner;

	DEC_MEMORY_STAT_BY(STAT_AI_EQS_InstanceMemory, QueryInstance->NamedParams.GetAllocatedSize());

	// @TODO: interface for providing default named params (like custom ranges in AI)
	QueryInstance->NamedParams = Request.NamedParams;

	INC_MEMORY_STAT_BY(STAT_AI_EQS_InstanceMemory, QueryInstance->NamedParams.GetAllocatedSize());

	QueryInstance->QueryID = NextQueryID++;

	return QueryInstance;
}

bool UEnvQueryManager::AbortQuery(int32 RequestID)
{
	for (int32 QueryIndex = 0; QueryIndex < RunningQueries.Num(); QueryIndex++)
	{
		TSharedPtr<FEnvQueryInstance>& QueryInstance = RunningQueries[QueryIndex];
		if (QueryInstance->QueryID == RequestID &&
			QueryInstance->IsFinished() == false)
		{
			QueryInstance->MarkAsAborted();
			QueryInstance->FinishDelegate.ExecuteIfBound(QueryInstance);
			
			RunningQueries.RemoveAt(QueryIndex);
			return true;
		}
	}

	return false;
}

void UEnvQueryManager::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_EQS_Tick);
	SET_DWORD_STAT(STAT_AI_EQS_NumInstances, RunningQueries.Num());
	// @TODO: threads?

	const double ExecutionTimeWarningSeconds = 0.25;
	const double MaxAllowedSeconds = 0.010;
	double TimeLeft = MaxAllowedSeconds;
	int32 FinishedQueriesCount = 0;
		
	TArray<TSharedPtr<FEnvQueryInstance> > RunningQueriesCopy = RunningQueries;

	{
		SCOPE_CYCLE_COUNTER(STAT_AI_EQS_TickWork);
		while (TimeLeft > 0.0 && RunningQueriesCopy.Num() > 0)
		{
			bool LoggedExecutionTimeWarning = false;

			for (int32 Index = 0; Index < RunningQueriesCopy.Num() && TimeLeft > 0.0; Index++)
			{
				const double StartTime = FPlatformTime::Seconds();
				double QuerierHandlingDuration = 0.;

				TSharedPtr<FEnvQueryInstance>& QueryInstance = RunningQueriesCopy[Index];

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				if (!bAllowEQSTimeSlicing)
				{
					// Passing in -1 causes QueryInstance to set its Deadline to -1, which in turn causes it to 
					// never fail based on time input.  (In fact, it's odd that we use FLT_MAX in RunInstantQuery(),
					// since that could simply use -1. as well.)  Note: "-1." to explicitly specify that it's a double.
					QueryInstance->ExecuteOneStep(-1.);
				}
				else
#endif
				{
					QueryInstance->ExecuteOneStep(TimeLeft);
				}

				if (QueryInstance->IsFinished())
				{
					// Always log that we executed total execution time at the end of the query.
					if (QueryInstance->GetTotalExecutionTime() > ExecutionTimeWarningSeconds)
					{
						UE_LOG(LogEQS, Warning, TEXT("Finished query %s over execution time warning. %s"), *QueryInstance->QueryName, *QueryInstance->GetExecutionTimeDescription());
					}

					// Now, handle the response to the query finishing, but calculate the time from that to remove from
					// the time spent for time-slicing purposes, because that's NOT the EQS manager doing work.
					{
						SCOPE_CYCLE_COUNTER(STAT_AI_EQS_TickNotifies);
						double QuerierHandlingStartTime = FPlatformTime::Seconds();
	
						UE_VLOG_EQS(*QueryInstance.Get(), LogEQS, All);

#if USE_EQS_DEBUGGER
						EQSDebugger.StoreQuery(GetWorld(), QueryInstance);
#endif // USE_EQS_DEBUGGER

						QueryInstance->FinishDelegate.ExecuteIfBound(QueryInstance);

						QuerierHandlingDuration = FPlatformTime::Seconds() - QuerierHandlingStartTime;
					}

					RunningQueriesCopy.RemoveAt(Index, 1, /*bAllowShrinking=*/false);
					Index--;
					++FinishedQueriesCount;
					LoggedExecutionTimeWarning = false;
				}

				if (!QueryInstance->HasLoggedTimeLimitWarning() && (QueryInstance->GetTotalExecutionTime() > ExecutionTimeWarningSeconds))
				{
					UE_LOG(LogEQS, Warning, TEXT("Query %s over execution time warning. %s"), *QueryInstance->QueryName, *QueryInstance->GetExecutionTimeDescription());
					QueryInstance->SetHasLoggedTimeLimitWarning();
				}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				if (bAllowEQSTimeSlicing) // if Time slicing is enabled...
#endif
				{	// Don't include the querier handling as part of the total time spent by EQS for time-slicing purposes.
					TimeLeft -= ((FPlatformTime::Seconds() - StartTime) - QuerierHandlingDuration);
				}
			}
		}
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_AI_EQS_TickQueryRemovals);
		for (int32 Index = RunningQueries.Num() - 1; Index >= 0 && FinishedQueriesCount > 0; --Index)
		{
			TSharedPtr<FEnvQueryInstance>& QueryInstance = RunningQueries[Index];

			if (QueryInstance->IsFinished())
			{
				RunningQueries.RemoveAt(Index, 1, /*bAllowShrinking=*/false);

				--FinishedQueriesCount;
			}
		}
	}
}

void UEnvQueryManager::OnWorldCleanup()
{
	if (RunningQueries.Num() > 0)
	{
		// @todo investigate if this is even needed. We should be fine with just removing all queries
		TArray<TSharedPtr<FEnvQueryInstance> > RunningQueriesCopy = RunningQueries;
		RunningQueries.Reset();

		for (int32 Index = 0; Index < RunningQueriesCopy.Num(); Index++)
		{
			TSharedPtr<FEnvQueryInstance>& QueryInstance = RunningQueriesCopy[Index];
			if (QueryInstance->IsFinished() == false)
			{
				QueryInstance->MarkAsFailed();
				QueryInstance->FinishDelegate.ExecuteIfBound(QueryInstance);
			}
		}
	}

	GCShieldedWrappers.Reset();
}

void UEnvQueryManager::RegisterExternalQuery(TSharedPtr<FEnvQueryInstance> QueryInstance)
{
	if (QueryInstance.IsValid())
	{
		ExternalQueries.Add(QueryInstance->QueryID, QueryInstance);
	}
}

void UEnvQueryManager::UnregisterExternalQuery(TSharedPtr<FEnvQueryInstance> QueryInstance)
{
	if (QueryInstance.IsValid())
	{
		ExternalQueries.Remove(QueryInstance->QueryID);
	}
}

namespace EnvQueryTestSort
{
	struct FAllMatching
	{
		FORCEINLINE bool operator()(const UEnvQueryTest& TestA, const UEnvQueryTest& TestB) const
		{
			// cheaper tests go first
			if (TestB.Cost > TestA.Cost)
			{
				return true;
			}

			// conditions go first
			const bool bConditionA = (TestA.TestPurpose != EEnvTestPurpose::Score); // Is Test A Filtering?
			const bool bConditionB = (TestB.TestPurpose != EEnvTestPurpose::Score); // Is Test B Filtering?
			if (bConditionA && !bConditionB)
			{
				return true;
			}

			// keep connection order (sort stability)
			return (TestB.TestOrder > TestA.TestOrder);
		}
	};

	struct FSingleResult
	{
		FSingleResult(EEnvTestCost::Type InHighestCost) : HighestCost(InHighestCost) {}

		FORCEINLINE bool operator()(const UEnvQueryTest& TestA, const UEnvQueryTest& TestB) const
		{
			// cheaper tests go first
			if (TestB.Cost > TestA.Cost)
			{
				return true;
			}

			const bool bConditionA = (TestA.TestPurpose != EEnvTestPurpose::Score); // Is Test A Filtering?
			const bool bConditionB = (TestB.TestPurpose != EEnvTestPurpose::Score); // Is Test B Filtering?
			if (TestA.Cost == HighestCost)
			{
				// highest cost: weights go first, conditions later (first match will return result)
				if (!bConditionA && bConditionB)
				{
					return true;
				}
			}
			else
			{
				// lower costs: conditions go first to reduce amount of items
				if (bConditionA && !bConditionB)
				{
					return true;
				}
			}

			// keep connection order (sort stability)
			return (TestB.TestOrder > TestA.TestOrder);
		}

	protected:
		TEnumAsByte<EEnvTestCost::Type> HighestCost;
	};
}

UEnvQuery* UEnvQueryManager::FindQueryTemplate(const FString& QueryName) const
{
	for (int32 InstanceIndex = 0; InstanceIndex < InstanceCache.Num(); InstanceIndex++)
	{
		const UEnvQuery* QueryTemplate = InstanceCache[InstanceIndex].Template;

		if (QueryTemplate != NULL && QueryTemplate->GetName() == QueryName)
		{
			return const_cast<UEnvQuery*>(QueryTemplate);
		}
	}

	for (FObjectIterator It(UEnvQuery::StaticClass()); It; ++It)
	{
		if (It->GetName() == QueryName)
		{
			return Cast<UEnvQuery>(*It);
		}
	}

	return NULL;
}

TSharedPtr<FEnvQueryInstance> UEnvQueryManager::CreateQueryInstance(const UEnvQuery* Template, EEnvQueryRunMode::Type RunMode)
{
	if (Template == nullptr || Template->Options.Num() == 0)
	{
		UE_CLOG(Template != nullptr && Template->Options.Num() == 0, LogEQS, Warning, TEXT("Query [%s] doesn't have any valid options!"), *Template->GetName());
		return nullptr;
	}

	// try to find entry in cache
	FEnvQueryInstance* InstanceTemplate = NULL;
	for (int32 InstanceIndex = 0; InstanceIndex < InstanceCache.Num(); InstanceIndex++)
	{
		if (InstanceCache[InstanceIndex].Template->GetQueryName() == Template->GetQueryName()
			&& InstanceCache[InstanceIndex].Instance.Mode == RunMode)
		{
			InstanceTemplate = &InstanceCache[InstanceIndex].Instance;
			break;
		}
	}

	// and create one if can't be found
	if (InstanceTemplate == NULL)
	{
		SCOPE_CYCLE_COUNTER(STAT_AI_EQS_LoadTime);
		
		// duplicate template in manager's world for BP based nodes
		UEnvQuery* LocalTemplate = (UEnvQuery*)StaticDuplicateObject(Template, this, TEXT("None"));

		{
			// memory stat tracking: temporary variable will exist only inside this section
			FEnvQueryInstanceCache NewCacheEntry;
			NewCacheEntry.Template = LocalTemplate;
			NewCacheEntry.Instance.QueryName = LocalTemplate->GetQueryName().ToString();
			NewCacheEntry.Instance.Mode = RunMode;

			const int32 Idx = InstanceCache.Add(NewCacheEntry);
			InstanceTemplate = &InstanceCache[Idx].Instance;
		}

		// NOTE: We must iterate over this from 0->Num because we are copying the options from the template into the
		// instance, and order matters!  Since we also may need to remove invalid or null options, we must decrement
		// the iteration pointer when doing so to avoid problems.
		for (int32 OptionIndex = 0; OptionIndex < LocalTemplate->Options.Num(); ++OptionIndex)
		{
			UEnvQueryOption* MyOption = LocalTemplate->Options[OptionIndex];
			if (MyOption == nullptr ||
				MyOption->Generator == nullptr ||
				MyOption->Generator->ItemType == nullptr)
			{
				UE_LOG(LogEQS, Error, TEXT("Trying to spawn a query with broken Template (generator:%s itemType:%s): %s, option %d"),
					MyOption ? (MyOption->Generator ? TEXT("ok") : TEXT("MISSING")) : TEXT("N/A"),
					(MyOption && MyOption->Generator) ? (MyOption->Generator->ItemType ? TEXT("ok") : TEXT("MISSING")) : TEXT("N/A"),
					*GetNameSafe(LocalTemplate), OptionIndex);

				LocalTemplate->Options.RemoveAt(OptionIndex, 1, false);
				--OptionIndex; // See note at top of for loop.  We cannot iterate backwards here.
				continue;
			}

			UEnvQueryOption* LocalOption = (UEnvQueryOption*)StaticDuplicateObject(MyOption, this);
			UEnvQueryGenerator* LocalGenerator = (UEnvQueryGenerator*)StaticDuplicateObject(MyOption->Generator, this);
			LocalTemplate->Options[OptionIndex] = LocalOption;
			LocalOption->Generator = LocalGenerator;

			EEnvTestCost::Type HighestCost(EEnvTestCost::Low);
			TArray<UEnvQueryTest*> SortedTests = MyOption->Tests;
			TSubclassOf<UEnvQueryItemType> GeneratedType = MyOption->Generator->ItemType;
			for (int32 TestIndex = SortedTests.Num() - 1; TestIndex >= 0; TestIndex--)
			{
				UEnvQueryTest* TestOb = SortedTests[TestIndex];
				if (TestOb == NULL || !TestOb->IsSupportedItem(GeneratedType))
				{
					UE_LOG(LogEQS, Warning, TEXT("Query [%s] can't use test [%s] in option %d [%s], removing it"),
						*GetNameSafe(LocalTemplate), *GetNameSafe(TestOb), OptionIndex, *MyOption->Generator->OptionName);

					SortedTests.RemoveAt(TestIndex, 1, false);
				}
				else if (HighestCost < TestOb->Cost)
				{
					HighestCost = TestOb->Cost;
				}
			}

			if (SortedTests.Num() == 0)
			{
				UE_LOG(LogEQS, Warning, TEXT("Query [%s] doesn't have any tests in option %d [%s]"),
					*GetNameSafe(LocalTemplate), OptionIndex, *MyOption->Generator->OptionName);

				LocalTemplate->Options.RemoveAt(OptionIndex, 1, false);
				--OptionIndex; // See note at top of for loop.  We cannot iterate backwards here.
				continue;
			}

			LocalOption->Tests.Reset(SortedTests.Num());
			for (int32 TestIdx = 0; TestIdx < SortedTests.Num(); TestIdx++)
			{
				UEnvQueryTest* LocalTest = (UEnvQueryTest*)StaticDuplicateObject(SortedTests[TestIdx], this);
				LocalOption->Tests.Add(LocalTest);
			}

			// use locally referenced duplicates
			SortedTests = LocalOption->Tests;

			if (SortedTests.Num() && LocalGenerator->bAutoSortTests)
			{
				switch (RunMode)
				{
				case EEnvQueryRunMode::SingleResult:
					SortedTests.Sort(EnvQueryTestSort::FSingleResult(HighestCost));
					break;

				case EEnvQueryRunMode::RandomBest5Pct:
				case EEnvQueryRunMode::RandomBest25Pct:
				case EEnvQueryRunMode::AllMatching:
					SortedTests.Sort(EnvQueryTestSort::FAllMatching());
					break;

				default:
					{
						UEnum* RunModeEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EEnvQueryRunMode"));
						UE_LOG(LogEQS, Warning, TEXT("Query [%s] can't be sorted for RunMode: %d [%s]"),
							*GetNameSafe(LocalTemplate), (int32)RunMode, RunModeEnum ? *RunModeEnum->GetEnumName(RunMode) : TEXT("??"));
					}
				}
			}

			CreateOptionInstance(LocalOption, SortedTests, *InstanceTemplate);
		}
	}

	if (InstanceTemplate->Options.Num() == 0)
	{
		return nullptr;
	}

	// create new instance
	TSharedPtr<FEnvQueryInstance> NewInstance(new FEnvQueryInstance(*InstanceTemplate));
	return NewInstance;
}

void UEnvQueryManager::CreateOptionInstance(UEnvQueryOption* OptionTemplate, const TArray<UEnvQueryTest*>& SortedTests, FEnvQueryInstance& Instance)
{
	FEnvQueryOptionInstance OptionInstance;
	OptionInstance.Generator = OptionTemplate->Generator;
	OptionInstance.ItemType = OptionTemplate->Generator->ItemType;

	OptionInstance.Tests.AddZeroed(SortedTests.Num());
	for (int32 TestIndex = 0; TestIndex < SortedTests.Num(); TestIndex++)
	{
		UEnvQueryTest* TestOb = SortedTests[TestIndex];
		OptionInstance.Tests[TestIndex] = TestOb;
	}

	DEC_MEMORY_STAT_BY(STAT_AI_EQS_InstanceMemory, Instance.Options.GetAllocatedSize());

	const int32 AddedIdx = Instance.Options.Add(OptionInstance);

	INC_MEMORY_STAT_BY(STAT_AI_EQS_InstanceMemory, Instance.Options.GetAllocatedSize() + Instance.Options[AddedIdx].GetAllocatedSize());
}

UEnvQueryContext* UEnvQueryManager::PrepareLocalContext(TSubclassOf<UEnvQueryContext> ContextClass)
{
	UEnvQueryContext* LocalContext = LocalContextMap.FindRef(ContextClass->GetFName());
	if (LocalContext == NULL)
	{
		LocalContext = (UEnvQueryContext*)StaticDuplicateObject(ContextClass.GetDefaultObject(), this);
		LocalContexts.Add(LocalContext);
		LocalContextMap.Add(ContextClass->GetFName(), LocalContext);
	}

	return LocalContext;
}

float UEnvQueryManager::FindNamedParam(int32 QueryId, FName ParamName) const
{
	float ParamValue = 0.0f;

	const TWeakPtr<FEnvQueryInstance>* QueryInstancePtr = ExternalQueries.Find(QueryId);
	if (QueryInstancePtr)
	{
		TSharedPtr<FEnvQueryInstance> QueryInstance = (*QueryInstancePtr).Pin();
		if (QueryInstance.IsValid())
		{
			ParamValue = QueryInstance->NamedParams.FindRef(ParamName);
		}
	}
	else
	{
		for (int32 QueryIndex = 0; QueryIndex < RunningQueries.Num(); QueryIndex++)
		{
			const TSharedPtr<FEnvQueryInstance>& QueryInstance = RunningQueries[QueryIndex];
			if (QueryInstance->QueryID == QueryId)
			{
				ParamValue = QueryInstance->NamedParams.FindRef(ParamName);
				break;
			}
		}
	}

	return ParamValue;
}

//----------------------------------------------------------------------//
// BP functions and related functionality 
//----------------------------------------------------------------------//
UEnvQueryInstanceBlueprintWrapper* UEnvQueryManager::RunEQSQuery(UObject* WorldContext, UEnvQuery* QueryTemplate, UObject* Querier, TEnumAsByte<EEnvQueryRunMode::Type> RunMode, TSubclassOf<UEnvQueryInstanceBlueprintWrapper> WrapperClass)
{ 
	if (QueryTemplate == nullptr || Querier == nullptr)
	{
		return nullptr;
	}

	UEnvQueryManager* EQSManager = GetCurrent(WorldContext);
	UEnvQueryInstanceBlueprintWrapper* QueryInstanceWrapper = nullptr;

	if (EQSManager)
	{
		bool bValidQuerier = true;

		// convert controller-owners to pawns, unless specifically configured not to do so
		if (GET_AI_CONFIG_VAR(bAllowControllersAsEQSQuerier) == false && Cast<AController>(Querier))
		{
			AController* Controller = Cast<AController>(Querier);
			if (Controller->GetPawn())
			{
				Querier = Controller->GetPawn();
			}
			else
			{
				UE_VLOG(Controller, LogEQS, Error, TEXT("Trying to run EQS query while not having a pawn! Aborting."));
				bValidQuerier = false;
			}
		}

		if (bValidQuerier)
		{
			QueryInstanceWrapper = NewObject<UEnvQueryInstanceBlueprintWrapper>(EQSManager, (UClass*)(WrapperClass) ? (UClass*)WrapperClass : UEnvQueryInstanceBlueprintWrapper::StaticClass());
			check(QueryInstanceWrapper);

			FEnvQueryRequest QueryRequest(QueryTemplate, Querier);
			// @todo named params still missing support
			//QueryRequest.SetNamedParams(QueryParams);
			QueryInstanceWrapper->RunQuery(RunMode, QueryRequest);
		}
	}
	
	return QueryInstanceWrapper;
}

void UEnvQueryManager::RegisterActiveWrapper(UEnvQueryInstanceBlueprintWrapper& Wrapper)
{
	GCShieldedWrappers.AddUnique(&Wrapper);
}

void UEnvQueryManager::UnregisterActiveWrapper(UEnvQueryInstanceBlueprintWrapper& Wrapper)
{
	GCShieldedWrappers.RemoveSingleSwap(&Wrapper, /*bAllowShrinking=*/false);
}

//----------------------------------------------------------------------//
// Exec functions (i.e. console commands)
//----------------------------------------------------------------------//
void UEnvQueryManager::SetAllowTimeSlicing(bool bAllowTimeSlicing)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	bAllowEQSTimeSlicing = bAllowTimeSlicing;

	UE_LOG(LogEQS, Log, TEXT("Set allow time slicing to %s."),
			bAllowEQSTimeSlicing ? TEXT("true") : TEXT("false"));
#else
	UE_LOG(LogEQS, Log, TEXT("Time slicing cannot be disabled in Test or Shipping builds.  SetAllowTimeSlicing does nothing."));
#endif
}

//----------------------------------------------------------------------//
// FEQSDebugger
//----------------------------------------------------------------------//
#if USE_EQS_DEBUGGER

void FEQSDebugger::StoreQuery(UWorld* InWorld, TSharedPtr<FEnvQueryInstance>& Query)
{
	StoredQueries.Remove(NULL);
	if (!Query.IsValid())
	{
		return;
	}
	TArray<FEnvQueryInfo>& AllQueries = StoredQueries.FindOrAdd(Query->Owner.Get());
		
	bool bFoundQuery = false;
	for (auto It = AllQueries.CreateIterator(); It; ++It)
	{
		auto& CurrentQuery = (*It);
		if (CurrentQuery.Instance.IsValid() && Query->QueryName == CurrentQuery.Instance->QueryName)
		{
			CurrentQuery.Instance = Query;
			CurrentQuery.Timestamp = InWorld->GetTimeSeconds();
			bFoundQuery = true;
			break;
		}
	}
	if (!bFoundQuery)
	{
		FEnvQueryInfo Info;
		Info.Instance = Query;
		Info.Timestamp = InWorld->GetTimeSeconds();
		AllQueries.AddUnique(Info);
	}
}

TArray<FEQSDebugger::FEnvQueryInfo>&  FEQSDebugger::GetAllQueriesForOwner(const UObject* Owner)
{
	TArray<FEnvQueryInfo>& AllQueries = StoredQueries.FindOrAdd(Owner);
	return AllQueries;
}

#endif // USE_EQS_DEBUGGER
