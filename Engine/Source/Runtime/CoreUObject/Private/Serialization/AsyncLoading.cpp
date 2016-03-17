// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnAsyncLoading.cpp: Unreal async loading code.
=============================================================================*/

#include "CoreUObjectPrivate.h"
#include "Serialization/AsyncLoading.h"
#include "Serialization/DeferredMessageLog.h"
#include "Serialization/AsyncPackage.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/LinkerManager.h"
#include "Serialization/AsyncLoadingThread.h"
#include "ExclusiveLoadPackageTimeTracker.h"
#include "AssetRegistryInterface.h"
#include "BlueprintSupport.h"


/*-----------------------------------------------------------------------------
	Async loading stats.
-----------------------------------------------------------------------------*/

DECLARE_MEMORY_STAT(TEXT("Streaming Memory Used"),STAT_StreamingAllocSize,STATGROUP_Memory);

DECLARE_STATS_GROUP_VERBOSE(TEXT("Async Load"), STATGROUP_AsyncLoad, STATCAT_Advanced);

DECLARE_CYCLE_STAT(TEXT("Tick AsyncPackage"),STAT_FAsyncPackage_Tick,STATGROUP_AsyncLoad);
DECLARE_FLOAT_ACCUMULATOR_STAT(TEXT("Tick AsyncPackage Time"), STAT_FAsyncPackage_TickTime, STATGROUP_AsyncLoad);

DECLARE_CYCLE_STAT(TEXT("CreateLinker AsyncPackage"),STAT_FAsyncPackage_CreateLinker,STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("FinishLinker AsyncPackage"),STAT_FAsyncPackage_FinishLinker,STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("LoadImports AsyncPackage"),STAT_FAsyncPackage_LoadImports,STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("CreateImports AsyncPackage"),STAT_FAsyncPackage_CreateImports,STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("FinishTextureAllocations AsyncPackage"),STAT_FAsyncPackage_FinishTextureAllocations,STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("CreateExports AsyncPackage"),STAT_FAsyncPackage_CreateExports,STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("FreeReferencedImports AsyncPackage"), STAT_FAsyncPackage_FreeReferencedImports, STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("Precache ArchiveAsync"), STAT_FArchiveAsync_Precache, STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("PreLoadObjects AsyncPackage"),STAT_FAsyncPackage_PreLoadObjects,STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("PostLoadObjects AsyncPackage"),STAT_FAsyncPackage_PostLoadObjects,STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("FinishObjects AsyncPackage"),STAT_FAsyncPackage_FinishObjects,STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("CreateAsyncPackagesFromQueue"), STAT_FAsyncPackage_CreateAsyncPackagesFromQueue, STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("ProcessAsyncLoading AsyncLoadingThread"), STAT_FAsyncLoadingThread_ProcessAsyncLoading, STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("Async Loading Time"),STAT_AsyncLoadingTime,STATGROUP_AsyncLoad);
DECLARE_CYCLE_STAT(TEXT("Async Loading Time Detailed"), STAT_AsyncLoadingTimeDetailed, STATGROUP_AsyncLoad);

DECLARE_STATS_GROUP(TEXT("Async Load Game Thread"), STATGROUP_AsyncLoadGameThread, STATCAT_Advanced);

DECLARE_CYCLE_STAT(TEXT("PostLoadObjects GT"), STAT_FAsyncPackage_PostLoadObjectsGameThread, STATGROUP_AsyncLoadGameThread);
DECLARE_CYCLE_STAT(TEXT("TickAsyncLoading GT"), STAT_FAsyncPackage_TickAsyncLoadingGameThread, STATGROUP_AsyncLoadGameThread);
DECLARE_CYCLE_STAT(TEXT("Flush Async Loading GT"), STAT_FAsyncPackage_FlushAsyncLoadingGameThread, STATGROUP_AsyncLoadGameThread);

DECLARE_FLOAT_ACCUMULATOR_STAT( TEXT( "Async loading block time" ), STAT_AsyncIO_AsyncLoadingBlockingTime, STATGROUP_AsyncIO );
DECLARE_FLOAT_ACCUMULATOR_STAT( TEXT( "Async package precache wait time" ), STAT_AsyncIO_AsyncPackagePrecacheWaitTime, STATGROUP_AsyncIO );

/** Returns true if we're inside a FGCScopeLock */
bool IsGarbageCollectionLocked();

/** Global request ID counter */
static FThreadSafeCounter GPackageRequestID;

/** 
 * Keeps a reference to all objects created during async load until streaming has finished 
 *
 * ASSUMPTION: AddObject can't be called while GC is running and we don't want to lock when calling AddReferencedObjects
 */
class FAsyncObjectsReferencer : FGCObject
{
	/** Private constructor */
	FAsyncObjectsReferencer() {}

	/** List of referenced objects */
	TSet<UObject*> ReferencedObjects;
#if THREADSAFE_UOBJECTS
	/** Critical section for referenced objects list */
	FCriticalSection ReferencedObjectsCritical;
#endif

public:
#if !UE_BUILD_SHIPPING
	bool Contains(UObject* InObj)
	{
#if THREADSAFE_UOBJECTS
		FScopeLock ReferencedObjectsLock(&ReferencedObjectsCritical);
#endif
		return ReferencedObjects.Contains(InObj);
	}
#endif

	/** Returns the one and only instance of this object */
	static FAsyncObjectsReferencer& Get();
	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		// Note we don't lock here as we're guaranteed that AddObject can only be called from
		// within FGCScopeGuard scope where GC does not run
		Collector.AllowEliminatingReferences(false);
		{
			Collector.AddReferencedObjects(ReferencedObjects);
		}
		Collector.AllowEliminatingReferences(true);
	}
	/** 
	 * Adds an object to be referenced 
	 * The assumption here is that this can only happen from inside of FGCScopeGuard (@see IsGarbageCollectionLocked()) where we're sure GC is not currently running,
	 * unless we're on the game thread where atm GC can run simultaneously with async loading.
	 */
	FORCEINLINE void AddObject(UObject* InObject)
	{
		if (InObject)
		{
			UE_CLOG(!IsInGameThread() && !IsGarbageCollectionLocked(), LogStreaming, Fatal, TEXT("Trying to add an object %s to FAsyncObjectsReferencer outside of a FGCScopeLock."), *InObject->GetFullName());
			{
#if THREADSAFE_UOBJECTS
				// Still want to lock as AddObject may be called on the game thread and async loading thread,
				// but in any case it may not happen when GC runs.
				FScopeLock ReferencedObjectsLock(&ReferencedObjectsCritical);
#else
				check(IsInGameThread());
#endif
				if (!ReferencedObjects.Contains(InObject))
				{
					ReferencedObjects.Add(InObject);
				}
			}
			InObject->ThisThreadAtomicallyClearedRFUnreachable();
		}
	}
	/** Removes all objects from the list and clears async loading flags */
	void EmptyReferencedObjects()
	{
		check(IsInGameThread());
#if THREADSAFE_UOBJECTS
		FScopeLock ReferencedObjectsLock(&ReferencedObjectsCritical);
#endif
		const EInternalObjectFlags AsyncFlags = EInternalObjectFlags::Async | EInternalObjectFlags::AsyncLoading;
		for (UObject* Obj : ReferencedObjects)
		{
			check(Obj);
			Obj->AtomicallyClearInternalFlags(AsyncFlags);
			check(!Obj->HasAnyInternalFlags(AsyncFlags))
		}
		ReferencedObjects.Reset();
	}
	/** Removes all referenced objects and markes them for GC */
	void EmptyReferencedObjectsAndCancelLoading()
	{
		const EObjectFlags LoadFlags = RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects;
		const EInternalObjectFlags AsyncFlags = EInternalObjectFlags::Async | EInternalObjectFlags::AsyncLoading;

#if THREADSAFE_UOBJECTS
		FScopeLock ReferencedObjectsLock(&ReferencedObjectsCritical);
#endif

		// All of the referenced objects have been created by async loading code and may be in an invalid state so mark them for GC
		for (auto Object : ReferencedObjects)
		{
			Object->ClearInternalFlags(AsyncFlags);
			if (Object->HasAnyFlags(LoadFlags))
			{
				Object->AtomicallyClearFlags(LoadFlags);
				Object->MarkPendingKill();
			}
			check(!Object->HasAnyInternalFlags(AsyncFlags) && !Object->HasAnyFlags(LoadFlags));
		}
		ReferencedObjects.Reset();
	}

#if !UE_BUILD_SHIPPING
	/** Verifies that no object exists that has either EInternalObjectFlags::AsyncLoading and EInternalObjectFlags::Async set and is NOT being referenced by FAsyncObjectsReferencer */
	FORCENOINLINE void VerifyAssumptions()
	{
		const EInternalObjectFlags AsyncFlags = EInternalObjectFlags::Async | EInternalObjectFlags::AsyncLoading;
		for (FRawObjectIterator It; It; ++It)
		{
			FUObjectItem* ObjItem = *It;
			checkSlow(ObjItem);
			UObject* Object = static_cast<UObject*>(ObjItem->Object);
			if (Object->HasAnyInternalFlags(AsyncFlags))
			{
				if (!Contains(Object))
				{
					UE_LOG(LogStreaming, Error, TEXT("%s has AsyncLoading|Async set but is not referenced by FAsyncObjectsReferencer"), *Object->GetPathName());
				}
			}
		}
	}
#endif
};
FAsyncObjectsReferencer& FAsyncObjectsReferencer::Get()
{
	static FAsyncObjectsReferencer Singleton;
	return Singleton;
}

#if !UE_BUILD_SHIPPING
class FAsyncLoadingExec : private FSelfRegisteringExec
{
public:

	FAsyncLoadingExec()
	{}

	/** Console commands **/
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override
	{
		if (FParse::Command(&Cmd, TEXT("VerifyAsyncLoadAssumptions")))
		{
			if (!IsAsyncLoading())
			{
				FAsyncObjectsReferencer::Get().VerifyAssumptions();
			}
			else
			{
				Ar.Logf(TEXT("Unable to verify async loading assumptions while streaming."));
			}
			return true;
		}
		return false;
	}
};
static TAutoPtr<FAsyncLoadingExec> GAsyncLoadingExec;
#endif

static int32 GAsyncLoadingThreadEnabled;
static FAutoConsoleVariableRef CVarAsyncLoadingThreadEnabledg(
	TEXT("s.AsyncLoadingThreadEnabled"),
	GAsyncLoadingThreadEnabled,
	TEXT("Placeholder console variable, currently not used in runtime."),
	ECVF_Default
	);


static int32 GWarnIfTimeLimitExceeded = 0;
static FAutoConsoleVariableRef CVarWarnIfTimeLimitExceeded(
	TEXT("s.WarnIfTimeLimitExceeded"),
	GWarnIfTimeLimitExceeded,
	TEXT("Enables log warning if time limit for time-sliced package streaming has been exceeded."),
	ECVF_Default
	);

static float GTimeLimitExceededMultiplier = 1.5f;
static FAutoConsoleVariableRef CVarTimeLimitExceededMultiplier(
	TEXT("s.TimeLimitExceededMultiplier"),
	GTimeLimitExceededMultiplier,
	TEXT("Multiplier for time limit exceeded warning time threshold."),
	ECVF_Default
	);

static float GTimeLimitExceededMinTime = 0.005f;
static FAutoConsoleVariableRef CVarTimeLimitExceededMinTime(
	TEXT("s.TimeLimitExceededMinTime"),
	GTimeLimitExceededMinTime,
	TEXT("Minimum time the time limit exceeded warning will be triggered by."),
	ECVF_Default
	);

static int32 GPreloadPackageDependencies = 0;
static FAutoConsoleVariableRef CVarPreloadPackageDependencies(
	TEXT("s.PreloadPackageDependencies"),
	GPreloadPackageDependencies,
	TEXT("Enables preloading of package dependencies based on data from the asset registry\n") \
	TEXT("0 - Do not preload dependencies. Can cause more seeks but uses less memory [default].\n") \
	TEXT("1 - Preload package dependencies. Faster but requires asset registry data to be loaded into memory\n"),
	ECVF_Default
	);

static FORCEINLINE bool IsTimeLimitExceeded(double InTickStartTime, bool bUseTimeLimit, float InTimeLimit, const TCHAR* InLastTypeOfWorkPerformed = nullptr, UObject* InLastObjectWorkWasPerformedOn = nullptr)
{
	bool bTimeLimitExceeded = false;
	if (bUseTimeLimit)
	{
		double CurrentTime = FPlatformTime::Seconds();
		bTimeLimitExceeded = CurrentTime - InTickStartTime > InTimeLimit;

		// Log single operations that take longer than time limit (but only in cooked builds)
		if (GWarnIfTimeLimitExceeded && 
			(CurrentTime - InTickStartTime) > GTimeLimitExceededMinTime &&
			(CurrentTime - InTickStartTime) > (GTimeLimitExceededMultiplier * InTimeLimit))
		{			
			UE_LOG(LogStreaming, Warning, TEXT("IsTimeLimitExceeded: %s %s took (less than) %5.2f ms"),
				InLastTypeOfWorkPerformed ? InLastTypeOfWorkPerformed : TEXT("unknown"),
				InLastObjectWorkWasPerformedOn ? *InLastObjectWorkWasPerformedOn->GetFullName() : TEXT("nullptr"),
				(CurrentTime - InTickStartTime) * 1000);
		}
	}
	return bTimeLimitExceeded;
}

uint32 FAsyncLoadingThread::AsyncLoadingThreadID = 0;

#if LOOKING_FOR_PERF_ISSUES
FThreadSafeCounter FAsyncLoadingThread::BlockingCycles = 0;
#endif

FAsyncLoadingThread& FAsyncLoadingThread::Get()
{
	static FAsyncLoadingThread GAsyncLoader;
	return GAsyncLoader;
}

/** Just like TGuardValue for FAsyncLoadingThread::bIsInAsyncLoadingTick but only works for the game thread */
struct FAsyncLoadingTickScope
{
	bool bWasInTick;
	FAsyncLoadingTickScope()
		: bWasInTick(false)
	{
		if (IsInGameThread())
		{
			FAsyncLoadingThread& AsyncLoadingThread = FAsyncLoadingThread::Get();
			bWasInTick = AsyncLoadingThread.GetIsInAsyncLoadingTick();
			AsyncLoadingThread.SetIsInAsyncLoadingTick(true);
		}
	}
	~FAsyncLoadingTickScope()
	{
		if (IsInGameThread())
		{
			FAsyncLoadingThread::Get().SetIsInAsyncLoadingTick(bWasInTick);
		}
	}
};

void FAsyncLoadingThread::InitializeAsyncThread()
{
	AsyncThreadReady.Increment();
}

void FAsyncLoadingThread::CancelAsyncLoadingInternal()
{
	{
		// Packages we haven't yet started processing.
#if THREADSAFE_UOBJECTS
		FScopeLock QueueLock(&QueueCritical);
#endif
		for (FAsyncPackageDesc* PackageDesc : QueuedPackages)
		{
			delete PackageDesc;
		}
		QueuedPackages.Reset();
	}

	{
		// Packages we started processing, need to be canceled.
		// Accessed only in async thread, no need to protect region.
		for (FAsyncPackage* AsyncPackage : AsyncPackages)
		{
			AsyncPackage->Cancel();
			delete AsyncPackage;
		}

		AsyncPackages.Reset();
	}

	{
		// Packages that are already loaded. May be halfway through PostLoad
#if THREADSAFE_UOBJECTS
		FScopeLock LoadedLock(&LoadedPackagesCritical);
#endif
		for (FAsyncPackage* LoadedPackage : LoadedPackages)
		{
			LoadedPackage->Cancel();
			delete LoadedPackage;
		}
		LoadedPackages.Reset();
	}
	{
#if THREADSAFE_UOBJECTS
		FScopeLock LoadedLock(&LoadedPackagesToProcessCritical);
#endif
		for (FAsyncPackage* LoadedPackage : LoadedPackagesToProcess)
		{
			LoadedPackage->Cancel();
			delete LoadedPackage;
		}
		LoadedPackagesToProcess.Reset();
	}

	AsyncLoadingCounter.Reset();
	AsyncPackagesCounter.Reset();
	QueuedPackagesCounter.Reset();

	FUObjectThreadContext::Get().ObjLoaded.Empty();
	{
		FGCScopeGuard GCGuard;
		FAsyncObjectsReferencer::Get().EmptyReferencedObjectsAndCancelLoading();
	}

	// Notify everyone streaming is canceled.
	CancelLoadingEvent->Trigger();
}

void FAsyncLoadingThread::QueuePackage(const FAsyncPackageDesc& Package)
{
	{
#if THREADSAFE_UOBJECTS
		FScopeLock QueueLock(&QueueCritical);
#endif
		QueuedPackagesCounter.Increment();
		QueuedPackages.Add(new FAsyncPackageDesc(Package));
	}
	QueuedRequestsEvent->Trigger();
}

FAsyncPackage* FAsyncLoadingThread::FindExistingPackageAndAddCompletionCallback(FAsyncPackageDesc* PackageRequest, TArray<FAsyncPackage*>& PackageList)
{
	checkSlow(IsInAsyncLoadThread());
	FAsyncPackage* Result = nullptr;
	const int32 ExistingPackageIndex = FindPackageByName(PackageList, PackageRequest->Name);
	if (ExistingPackageIndex != INDEX_NONE)
	{
		Result = PackageList[ExistingPackageIndex];
		if (PackageRequest->PackageLoadedDelegate.IsBound())
		{
			const bool bInternalCallback = false;
			Result->AddCompletionCallback(PackageRequest->PackageLoadedDelegate, bInternalCallback);
			Result->AddRequestID(PackageRequest->RequestID);
		}
		const int32 QueuedPackagesCount = QueuedPackagesCounter.Decrement();
		check(QueuedPackagesCount >= 0);
	}
	return Result;
}

void FAsyncLoadingThread::UpdateExistingPackagePriorities(FAsyncPackage* InPackage, TAsyncLoadPriority InNewPriority, TSet<FName>& InDependencyTracker, IAssetRegistryInterface* InAssetRegistry)
{
	InDependencyTracker.Add(InPackage->GetPackageName());

	if (InNewPriority > InPackage->GetPriority())
	{
		AsyncPackages.Remove(InPackage);
		InPackage->SetPriority(InNewPriority);

		// Reduce loading counters ready for InsertPackage to increment them again
		AsyncLoadingCounter.Decrement();
		AsyncPackagesCounter.Decrement();

		InsertPackage(InPackage, InAssetRegistry != nullptr ? EAsyncPackageInsertMode::InsertAfterMatchingPriorities : EAsyncPackageInsertMode::InsertBeforeMatchingPriorities);
	}

	if (InAssetRegistry)
	{
		TArray<FName> Dependencies;
		InAssetRegistry->GetDependencies(InPackage->GetPackageName(), Dependencies, EAssetRegistryDependencyType::Hard);

		for (FName DependencyName : Dependencies)
		{
			if (!InDependencyTracker.Contains(DependencyName))
			{
				int32 PackageIndex = FindPackageByName(AsyncPackages, DependencyName);

				if (PackageIndex >= 0)
				{
					FAsyncPackage* DependencyPackage = AsyncPackages[PackageIndex];
					UpdateExistingPackagePriorities(DependencyPackage, InNewPriority, InDependencyTracker, InAssetRegistry);
				}
			}
		}
	}
}

void FAsyncLoadingThread::ProcessAsyncPackageRequest(FAsyncPackageDesc* InRequest, FAsyncPackage* InRootPackage, TSet<FName>& InDependencyTracker, IAssetRegistryInterface* InAssetRegistry)
{
	check(!InDependencyTracker.Contains(InRequest->Name));
	InDependencyTracker.Add(InRequest->Name);

	FAsyncPackage* Package = FindExistingPackageAndAddCompletionCallback(InRequest, AsyncPackages);

	if (Package)
	{
		// The package is already sitting in the queue. Make sure the its priority, and the priority of all its
		// dependencies is at least as high as the priority of this request
		UpdateExistingPackagePriorities(Package, InRequest->Priority, InDependencyTracker, InAssetRegistry);
	}
	else
	{
		// [BLOCKING] LoadedPackages are accessed on the main thread too, so lock to be able to add a completion callback
#if THREADSAFE_UOBJECTS
		FScopeLock LoadedLock(&LoadedPackagesCritical);
#endif
		Package = FindExistingPackageAndAddCompletionCallback(InRequest, LoadedPackages);
	}

	if (!Package)
	{
		// [BLOCKING] LoadedPackagesToProcess are modified on the main thread, so lock to be able to add a completion callback
#if THREADSAFE_UOBJECTS
		FScopeLock LoadedLock(&LoadedPackagesToProcessCritical);
#endif
		Package = FindExistingPackageAndAddCompletionCallback(InRequest, LoadedPackagesToProcess);
	}

	if (!Package)
	{
		// New package that needs to be loaded or a package has already been loaded long time ago
		Package = new FAsyncPackage(*InRequest);
		if (InRequest->PackageLoadedDelegate.IsBound())
		{
			const bool bInternalCallback = false;
			Package->AddCompletionCallback(InRequest->PackageLoadedDelegate, bInternalCallback);
		}
		Package->SetDependencyRootPackage(InRootPackage);

#if !WITH_EDITOR
		if (InAssetRegistry)
			{
				TArray<FName> Dependencies;
			InAssetRegistry->GetDependencies(Package->GetPackageName(), Dependencies, EAssetRegistryDependencyType::Hard);

				if (InRootPackage == nullptr)
				{
					InRootPackage = Package;
				}

				for (auto DependencyName : Dependencies)
				{
				if (!InDependencyTracker.Contains(DependencyName) && FindObjectFast<UPackage>(nullptr, DependencyName, false, false ) == nullptr)
						{
							QueuedPackagesCounter.Increment();
							const int32 RequestID = GPackageRequestID.Increment();
							FAsyncLoadingThread::Get().AddPendingRequest(RequestID);
							FAsyncPackageDesc DependencyPackageRequest(RequestID, DependencyName, NAME_None, FGuid(), FLoadPackageAsyncDelegate(), InRequest->PackageFlags, INDEX_NONE, InRequest->Priority);
					ProcessAsyncPackageRequest(&DependencyPackageRequest, InRootPackage, InDependencyTracker, InAssetRegistry);
				}
			}
		}
#endif
		// Add to queue according to priority.
		InsertPackage(Package, InAssetRegistry != nullptr ? EAsyncPackageInsertMode::InsertAfterMatchingPriorities : EAsyncPackageInsertMode::InsertBeforeMatchingPriorities);

		// For all other cases this is handled in FindExistingPackageAndAddCompletionCallback
		const int32 QueuedPackagesCount = QueuedPackagesCounter.Decrement();
		check(QueuedPackagesCount >= 0);
	}
}

int32 FAsyncLoadingThread::CreateAsyncPackagesFromQueue()
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_CreateAsyncPackagesFromQueue);

	FAsyncLoadingTickScope InAsyncLoadingTick;

	int32 NumCreated = 0;
	checkSlow(IsInAsyncLoadThread());

	TArray<FAsyncPackageDesc*> QueueCopy;
	{
#if THREADSAFE_UOBJECTS
		FScopeLock QueueLock(&QueueCritical);
#endif
		QueueCopy = QueuedPackages;
		QueuedPackages.Reset();
	}

	if (QueueCopy.Num() > 0)
	{
		IAssetRegistryInterface* AssetRegistry = nullptr;
		
		if (GPreloadPackageDependencies && IsPlatformFileCompatibleWithDependencyPreloading())
		{
			AssetRegistry = IAssetRegistryInterface::GetPtr();
		}
		
		double Timer = 0;
		{
			SCOPE_SECONDS_COUNTER(Timer);
			for (FAsyncPackageDesc* PackageRequest : QueueCopy)
			{
				DependencyTracker.Reset();
				ProcessAsyncPackageRequest(PackageRequest, nullptr, DependencyTracker, AssetRegistry);
				delete PackageRequest;
			}
		}
		UE_LOG(LogStreaming, Verbose, TEXT("Async package requests inserted in %fms"), Timer * 1000.0);
	}

	NumCreated = QueueCopy.Num();

	return NumCreated;
}

void FAsyncLoadingThread::InsertPackage(FAsyncPackage* Package, EAsyncPackageInsertMode InsertMode)
{
	checkSlow(IsInAsyncLoadThread());

	// Incremented on the Async Thread, decremented on the game thread
	AsyncLoadingCounter.Increment();

	// Incemented and decremented on the AsyncThread
	AsyncPackagesCounter.Increment();

	{
#if THREADSAFE_UOBJECTS
		FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
#endif
		int32 InsertIndex = -1;

		switch (InsertMode)
		{
		case EAsyncPackageInsertMode::InsertAfterMatchingPriorities:
			{
				InsertIndex = AsyncPackages.IndexOfByPredicate([Package](const FAsyncPackage* Element)
				{
					return Element->GetPriority() < Package->GetPriority();
				});

				break;
			}

		case EAsyncPackageInsertMode::InsertBeforeMatchingPriorities:
			{
		// Insert new package keeping descending priority order in AsyncPackages
				InsertIndex = AsyncPackages.IndexOfByPredicate([Package](const FAsyncPackage* Element)
		{
			return Element->GetPriority() <= Package->GetPriority();
		});

				break;
			}
		};

		InsertIndex = InsertIndex == INDEX_NONE ? AsyncPackages.Num() : InsertIndex;

		AsyncPackages.InsertUninitialized(InsertIndex);
		AsyncPackages[InsertIndex] = Package;
	}
}

EAsyncPackageState::Type FAsyncLoadingThread::ProcessAsyncLoading(int32& OutPackagesProcessed, bool bUseTimeLimit /*= false*/, bool bUseFullTimeLimit /*= false*/, float TimeLimit /*= 0.0f*/)
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncLoadingThread_ProcessAsyncLoading);
	
	EAsyncPackageState::Type LoadingState = EAsyncPackageState::Complete;
	OutPackagesProcessed = 0;

	// We need to loop as the function has to handle finish loading everything given no time limit
	// like e.g. when called from FlushAsyncLoading.
	for (int32 PackageIndex = 0; LoadingState != EAsyncPackageState::TimeOut && PackageIndex < AsyncPackages.Num(); ++PackageIndex)
	{
		OutPackagesProcessed++;

		// Package to be loaded.
		FAsyncPackage* Package = AsyncPackages[PackageIndex];

		if (Package->HasFinishedLoading() == false)
		{
			// Package tick returns EAsyncPackageState::Complete on completion.
			// We only tick packages that have not yet been loaded.
			LoadingState = Package->Tick(bUseTimeLimit, bUseFullTimeLimit, TimeLimit);
		}
		else
		{
			// This package has finished loading but some other package is still holding
			// a reference to it because it has this package in its dependency list.
			LoadingState = EAsyncPackageState::Complete;
		}
		bool bPackageFullyLoaded = false;
		if (LoadingState == EAsyncPackageState::Complete)
		{
			// We're done, at least on this thread, so we can remove the package now.
			AddToLoadedPackages(Package);
			{
#if THREADSAFE_UOBJECTS
				FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
#endif
				AsyncPackages.RemoveAt(PackageIndex);
			}
					
			// Need to process this index again as we just removed an item
			PackageIndex--;
			bPackageFullyLoaded = true;
		}
		else if (!bUseTimeLimit && !FPlatformProcess::SupportsMultithreading())
		{
			// Tick async loading when multithreading is disabled.
			FIOSystem::Get().TickSingleThreaded();
		}

		// Check if there's any new packages in the queue.
		CreateAsyncPackagesFromQueue();

		if (bPackageFullyLoaded)
		{
			AsyncPackagesCounter.Decrement();
		}
	}

	return LoadingState;
}


EAsyncPackageState::Type FAsyncLoadingThread::ProcessLoadedPackages(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit, int32 WaitForRequestID /*= INDEX_NONE*/)
{
	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;

	double TickStartTime = FPlatformTime::Seconds();
	{
#if THREADSAFE_UOBJECTS
		FScopeLock LoadedPackagesLock(&LoadedPackagesCritical);
		FScopeLock LoadedPackagesToProcessLock(&LoadedPackagesToProcessCritical);
#endif
		LoadedPackagesToProcess.Append(LoadedPackages);
		LoadedPackages.Reset();
	}
		
	for (int32 PackageIndex = 0; PackageIndex < LoadedPackagesToProcess.Num() && !IsAsyncLoadingSuspended(); ++PackageIndex)
	{
		if (PackageIndex % 20 == 0 && IsTimeLimitExceeded(TickStartTime, bUseTimeLimit, TimeLimit, TEXT("ProcessLoadedPackages")))
		{
			break;
		}

		FAsyncPackage* Package = LoadedPackagesToProcess[PackageIndex];
		if (Package->GetDependencyRefCount() == 0)
		{
			Result = Package->PostLoadDeferredObjects(TickStartTime, bUseTimeLimit, TimeLimit);
			if (Result == EAsyncPackageState::Complete)
			{
				// Remove the package from the list before we trigger the callbacks, 
				// this is to ensure we can re-enter FlushAsyncLoading from any of the callbacks
				{
					FScopeLock LoadedLock(&LoadedPackagesToProcessCritical);					
					LoadedPackagesToProcess.RemoveAt(PackageIndex--);
					
					if (FPlatformProperties::RequiresCookedData())
					{
						// Emulates ResetLoaders on the package linker's linkerroot.
						Package->ResetLoader();
					}
					else
					{
						// Detach linker in mutex scope to make sure that if something requests this package
						// before it's been deleted does not try to associate the new async package with the old linker
						// while this async package is still bound to it.
						Package->DetachLinker();
					}
					
				}

				// Incremented on the Async Thread, now decrement as we're done with this package				
				const int32 NewAsyncLoadingCounterValue = AsyncLoadingCounter.Decrement();
				UE_CLOG(NewAsyncLoadingCounterValue < 0, LogStreaming, Fatal, TEXT("AsyncLoadingCounter is negative, this means we loaded more packages then requested so there must be a bug in async loading code."));

				// Call external callbacks
				const bool bInternalCallbacks = false;
				const EAsyncLoadingResult::Type LoadingResult = Package->HasLoadFailed() ? EAsyncLoadingResult::Failed : EAsyncLoadingResult::Succeeded;
				Package->CallCompletionCallbacks(bInternalCallbacks, LoadingResult);

				// We don't need the package anymore
				delete Package;

				if (WaitForRequestID != INDEX_NONE && !ContainsRequestID(WaitForRequestID))
				{
					// The only package we care about has finished loading, so we're good to exit
					break;
				}
			}
			else
			{
				break;
			}
		}
		else
		{
			Result = EAsyncPackageState::PendingImports;
			// Break immediately, we want to keep the order of processing when packages get here
			break;
		}
	}

	return Result;
}



EAsyncPackageState::Type FAsyncLoadingThread::TickAsyncLoading(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit, int32 WaitForRequestID /*= INDEX_NONE*/)
{
	const bool bLoadingSuspended = IsAsyncLoadingSuspended();
	const bool bIsMultithreaded = FAsyncLoadingThread::IsMultithreaded();
	EAsyncPackageState::Type Result = bLoadingSuspended ? EAsyncPackageState::PendingImports : EAsyncPackageState::Complete;

	if (!bLoadingSuspended)
	{
		double TickStartTime = FPlatformTime::Seconds();
		double TimeLimitUsedForProcessLoaded = 0;

		{
			Result = ProcessLoadedPackages(bUseTimeLimit, bUseFullTimeLimit, TimeLimit, WaitForRequestID);
			TimeLimitUsedForProcessLoaded = FPlatformTime::Seconds() - TickStartTime;
		}

		if (!bIsMultithreaded && Result != EAsyncPackageState::TimeOut && !IsTimeLimitExceeded(TickStartTime, bUseTimeLimit, TimeLimit, TEXT("Pre-TickAsyncThread")))
		{
			double RemainingTimeLimit = FMath::Max(0.0, TimeLimit - TimeLimitUsedForProcessLoaded);
			Result = TickAsyncThread(bUseTimeLimit, bUseFullTimeLimit, RemainingTimeLimit);			
		}

		if (Result != EAsyncPackageState::TimeOut && !IsTimeLimitExceeded(TickStartTime, bUseTimeLimit, TimeLimit, TEXT("Pre-EmptyReferencedObjects")))
		{
#if THREADSAFE_UOBJECTS
			FScopeLock QueueLock(&QueueCritical);
			FScopeLock LoadedLock(&LoadedPackagesCritical);
#endif
			if (AsyncPackagesCounter.GetValue() == 0 && LoadedPackagesToProcess.Num() == 0)
			{
				FDeferredMessageLog::Flush();
				FAsyncObjectsReferencer::Get().EmptyReferencedObjects();
			}
		}
	}

	return Result;
}

FAsyncLoadingThread::FAsyncLoadingThread()
{
#if !UE_BUILD_SHIPPING
	GAsyncLoadingExec = new FAsyncLoadingExec();
#endif
	QueuedRequestsEvent = FPlatformProcess::GetSynchEventFromPool();
	CancelLoadingEvent = FPlatformProcess::GetSynchEventFromPool();
	ThreadSuspendedEvent = FPlatformProcess::GetSynchEventFromPool();
	ThreadResumedEvent = FPlatformProcess::GetSynchEventFromPool();
	if (FAsyncLoadingThread::IsMultithreaded())
	{
		UE_LOG(LogStreaming, Log, TEXT("Async loading is multithreaded."));
		Thread = FRunnableThread::Create(this, TEXT("FAsyncLoadingThread"), 0, TPri_Normal);
	}
	else
	{
		UE_LOG(LogStreaming, Log, TEXT("Async loading is time-sliced."));
		Thread = nullptr;
		Init();
	}
	bIsInAsyncLoadingTick = false;
}

FAsyncLoadingThread::~FAsyncLoadingThread()
{
	delete Thread;
	Thread = nullptr;
	FPlatformProcess::ReturnSynchEventToPool(QueuedRequestsEvent);
	QueuedRequestsEvent = nullptr;
	FPlatformProcess::ReturnSynchEventToPool(CancelLoadingEvent);
	CancelLoadingEvent = nullptr;
	FPlatformProcess::ReturnSynchEventToPool(ThreadSuspendedEvent);
	ThreadSuspendedEvent = nullptr;
	FPlatformProcess::ReturnSynchEventToPool(ThreadResumedEvent);
	ThreadResumedEvent = nullptr;	
}

bool FAsyncLoadingThread::Init()
{
	return true;
}

uint32 FAsyncLoadingThread::Run()
{
	AsyncLoadingThreadID = FPlatformTLS::GetCurrentThreadId();

	bool bWasSuspendedLastFrame = false;
	while (StopTaskCounter.GetValue() == 0)
	{
		if (IsLoadingSuspended.GetValue() == 0)
		{
			if (bWasSuspendedLastFrame)
			{
				bWasSuspendedLastFrame = false;
				ThreadResumedEvent->Trigger();
			}			
			TickAsyncThread(false, true, 0.0f);
		}
		else if (!bWasSuspendedLastFrame)
		{
			bWasSuspendedLastFrame = true;
			ThreadSuspendedEvent->Trigger();			
		}
		else
		{
			FPlatformProcess::SleepNoStats(0.001f);
		}		
	}
	return 0;
}

EAsyncPackageState::Type  FAsyncLoadingThread::TickAsyncThread(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit)
{
	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;
	if (!bShouldCancelLoading)
	{
		int32 ProcessedRequests = 0;
		if (AsyncThreadReady.GetValue())
		{
			CreateAsyncPackagesFromQueue();
			Result = ProcessAsyncLoading(ProcessedRequests, bUseTimeLimit, bUseFullTimeLimit, TimeLimit);
		}
		if (ProcessedRequests == 0 && IsMultithreaded())
		{
			const bool bIgnoreThreadIdleStats = true;
			QueuedRequestsEvent->Wait(30, bIgnoreThreadIdleStats);
		}
	}
	else
	{
		// Blocks main thread
		CancelAsyncLoadingInternal();
		bShouldCancelLoading = false;
	}

#if LOOKING_FOR_PERF_ISSUES
	// Update stats
	SET_FLOAT_STAT( STAT_AsyncIO_AsyncLoadingBlockingTime, FPlatformTime::ToSeconds( BlockingCycles.GetValue() ) );
	BlockingCycles.Set( 0 );
#endif

	return Result;
}

void FAsyncLoadingThread::Stop()
{
	StopTaskCounter.Increment();
}

void FAsyncLoadingThread::CancelAsyncLoading()
{
	checkSlow(IsInGameThread());	

	bShouldCancelLoading = true;
	if (IsMultithreaded())
	{
		CancelLoadingEvent->Wait();
	}
	else
	{
		// This will immediately cancel async loading without waiting for packages to finish loading
		FlushAsyncLoading();
		// It's possible we haven't been async loading at all in which case the above call would not reset bShouldCancelLoading
		bShouldCancelLoading = false;
	}
}

void FAsyncLoadingThread::SuspendLoading()
{
	check(IsInGameThread());
	const int32 SuspendCount = IsLoadingSuspended.Increment();
	if (IsMultithreaded() && SuspendCount == 1)
	{
		ThreadSuspendedEvent->Wait();
	}
}

void FAsyncLoadingThread::ResumeLoading()
{
	check(IsInGameThread());
	const int32 SuspendCount = IsLoadingSuspended.Decrement();
	UE_CLOG(SuspendCount < 0, LogStreaming, Fatal, TEXT("ResumeAsyncLoadingThread: Async loading was resumed more times than it was suspended."));
	if (IsMultithreaded() && SuspendCount == 0)
	{
		ThreadResumedEvent->Wait();
	}
}

float FAsyncLoadingThread::GetAsyncLoadPercentage(const FName& PackageName)
{
	float LoadPercentage = -1.0f;
	{
#if THREADSAFE_UOBJECTS
		FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
#endif
		const int32 PackageIndex = FindPackageByName(AsyncPackages, PackageName);
		if (PackageIndex != INDEX_NONE)
		{
			LoadPercentage = AsyncPackages[PackageIndex]->GetLoadPercentage();
		}
	}
	if (LoadPercentage < 0.0f)
	{
#if THREADSAFE_UOBJECTS
		FScopeLock LockLoadedPackages(&LoadedPackagesCritical);
#endif
		const int32 PackageIndex = FindPackageByName(LoadedPackages, PackageName);
		if (PackageIndex != INDEX_NONE)
		{
			LoadPercentage = LoadedPackages[PackageIndex]->GetLoadPercentage();
		}
	}
	if (LoadPercentage < 0.0f)
	{
		checkSlow(IsInGameThread());
		// No lock required as we're in the game thread and LoadedPackagesToProcess are only modified on the game thread
		const int32 PackageIndex = FindPackageByName(LoadedPackagesToProcess, PackageName);
		if (PackageIndex != INDEX_NONE)
		{
			LoadPercentage = LoadedPackagesToProcess[PackageIndex]->GetLoadPercentage();
		}
	}

	return LoadPercentage;
}

/**
 * Call back into the async loading code to inform of the creation of a new object
 * @param Object		Object created
 * @param bSubObject	Object created as a sub-object of a loaded object
 */
void NotifyConstructedDuringAsyncLoading(UObject* Object, bool bSubObject)
{
	// Mark objects created during async loading process (e.g. from within PostLoad or CreateExport) as async loaded so they 
	// cannot be found. This requires also keeping track of them so we can remove the async loading flag later one when we 
	// finished routing PostLoad to all objects.
	if (!bSubObject)
	{
		Object->SetInternalFlags(EInternalObjectFlags::AsyncLoading);
	}
	FAsyncObjectsReferencer::Get().AddObject(Object);
}

/*-----------------------------------------------------------------------------
	FAsyncPackage implementation.
-----------------------------------------------------------------------------*/


int32 FAsyncPackage::PreLoadIndex = 0;
int32 FAsyncPackage::PostLoadIndex = 0;

/**
* Constructor
*/
FAsyncPackage::FAsyncPackage(const FAsyncPackageDesc& InDesc)
: Desc(InDesc)
, Linker(nullptr)
, LinkerRoot(nullptr)
, DependencyRootPackage(nullptr)
, DependencyRefCount(0)
, LoadImportIndex(0)
, ImportIndex(0)
, ExportIndex(0)
, DeferredPostLoadIndex(0)
, TimeLimit(FLT_MAX)
, bUseTimeLimit(false)
, bUseFullTimeLimit(false)
, bTimeLimitExceeded(false)
, bLoadHasFailed(false)
, bLoadHasFinished(false)
, TickStartTime(0)
, LastObjectWorkWasPerformedOn(nullptr)
, LastTypeOfWorkPerformed(nullptr)
, LoadStartTime(0.0)
, LoadPercentage(0)
, AsyncLoadingThread(FAsyncLoadingThread::Get())
#if PERF_TRACK_DETAILED_ASYNC_STATS
, TickCount(0)
, TickLoopCount(0)
, CreateLinkerCount(0)
, FinishLinkerCount(0)
, CreateImportsCount(0)
, CreateExportsCount(0)
, PreLoadObjectsCount(0)
, PostLoadObjectsCount(0)
, FinishObjectsCount(0)
, TickTime(0.0)
, CreateLinkerTime(0.0)
, FinishLinkerTime(0.0)
, CreateImportsTime(0.0)
, CreateExportsTime(0.0)
, PreLoadObjectsTime(0.0)
, PostLoadObjectsTime(0.0)
, FinishObjectsTime(0.0)
#endif // PERF_TRACK_DETAILED_ASYNC_STATS
{
	AddRequestID(InDesc.RequestID);
}

FAsyncPackage::~FAsyncPackage()
{
	AsyncLoadingThread.RemovePendingRequests(RequestIDs);
	DetachLinker();
}

void FAsyncPackage::AddRequestID(int32 Id)
{
	if (Id > 0)
	{
		RequestIDs.Add(Id);
		AsyncLoadingThread.AddPendingRequest(Id);
	}
}

/**
 * @return Time load begun. This is NOT the time the load was requested in the case of other pending requests.
 */
double FAsyncPackage::GetLoadStartTime() const
{
	return LoadStartTime;
}

/**
 * Emulates ResetLoaders for the package's Linker objects, hence deleting it. 
 */
void FAsyncPackage::ResetLoader()
{
	// Reset loader.
	if (Linker)
	{
		check(Linker->AsyncRoot == this || Linker->AsyncRoot == nullptr);
		Linker->AsyncRoot = nullptr;
		Linker->Detach();
		FLinkerManager::Get().RemoveLinker(Linker);
		Linker = nullptr;
	}
}

void FAsyncPackage::DetachLinker()
{	
	if (Linker)
	{
		check(bLoadHasFinished || bLoadHasFailed);
		check(Linker->AsyncRoot == this || Linker->AsyncRoot == nullptr);
		Linker->AsyncRoot = nullptr;
		Linker = nullptr;
	}
}

/**
 * Returns whether time limit has been exceeded.
 *
 * @return true if time limit has been exceeded (and is used), false otherwise
 */
bool FAsyncPackage::IsTimeLimitExceeded()
{
	return AsyncLoadingThread.IsAsyncLoadingSuspended() || ::IsTimeLimitExceeded(TickStartTime, bUseTimeLimit, TimeLimit, LastTypeOfWorkPerformed, LastObjectWorkWasPerformedOn);
}

/**
 * Gives up time slice if time limit is enabled.
 *
 * @return true if time slice can be given up, false otherwise
 */
bool FAsyncPackage::GiveUpTimeSlice()
{
	static const bool bPlatformIsSingleThreaded = !FPlatformProcess::SupportsMultithreading();
	if (bPlatformIsSingleThreaded)
	{
		FIOSystem::Get().TickSingleThreaded();
	}

	if (bUseTimeLimit && !bUseFullTimeLimit)
	{
		bTimeLimitExceeded = true;
	}
	return bTimeLimitExceeded;
}

/**
 * Begin async loading process. Simulates parts of BeginLoad.
 *
 * Objects created during BeginAsyncLoad and EndAsyncLoad will have EInternalObjectFlags::AsyncLoading set
 */
void FAsyncPackage::BeginAsyncLoad()
{
	if (IsInGameThread())
	{
		FAsyncLoadingThread::Get().SetIsInAsyncLoadingTick(true);
	}

	// this won't do much during async loading except increase the load count which causes IsLoading to return true
	BeginLoad();
}

/**
 * End async loading process. Simulates parts of EndLoad(). FinishObjects 
 * simulates some further parts once we're fully done loading the package.
 */
void FAsyncPackage::EndAsyncLoad()
{
	check(IsAsyncLoading());

	// this won't do much during async loading except decrease the load count which causes IsLoading to return false
	EndLoad();

	if (IsInGameThread())
	{
		FAsyncLoadingThread::Get().SetIsInAsyncLoadingTick(false);
	}

	if (!bLoadHasFailed)
	{
		// Mark the package as loaded, if we succeeded
		LinkerRoot->SetFlags(RF_WasLoaded);
	}
}

/**
 * Ticks the async loading code.
 *
 * @param	InbUseTimeLimit		Whether to use a time limit
 * @param	InbUseFullTimeLimit	If true use the entire time limit, even if you have to block on IO
 * @param	InOutTimeLimit			Soft limit to time this function may take
 *
 * @return	true if package has finished loading, false otherwise
 */

EAsyncPackageState::Type FAsyncPackage::Tick(bool InbUseTimeLimit, bool InbUseFullTimeLimit, float& InOutTimeLimit)
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_Tick);

	// Whether we should execute the next step.
	EAsyncPackageState::Type LoadingState = EAsyncPackageState::Complete;

	check(LastObjectWorkWasPerformedOn == nullptr);
	check(LastTypeOfWorkPerformed == nullptr);

	// Set up tick relevant variables.
	bUseTimeLimit = InbUseTimeLimit;
	bUseFullTimeLimit = InbUseFullTimeLimit;
	bTimeLimitExceeded = false;
	TimeLimit = InOutTimeLimit;
	TickStartTime = FPlatformTime::Seconds();

	// Keep track of time when we start loading.
	if (LoadStartTime == 0.0)
	{
		LoadStartTime = TickStartTime;

		// If we are a dependency of another package, we need to tell that package when its first dependent started loading,
		// otherwise because that package loads last it'll not include the entire load time of all its dependencies
		if (DependencyRootPackage)
		{
			// Only the first dependent needs to register the start time
			if (DependencyRootPackage->GetLoadStartTime() == 0.0)
			{
				DependencyRootPackage->LoadStartTime = TickStartTime;
			}
		}
	}

	// Make sure we finish our work if there's no time limit. The loop is required as PostLoad
	// might cause more objects to be loaded in which case we need to Preload them again.
	do
	{
		// Reset value to true at beginning of loop.
		LoadingState = EAsyncPackageState::Complete;

		// Begin async loading, simulates BeginLoad
		BeginAsyncLoad();

		// We have begun loading a package that we know the name of. Let the package time tracker know.
		FExclusiveLoadPackageTimeTracker::PushLoadPackage(Desc.NameToLoad);

		// Create raw linker. Needs to be async created via ticking before it can be used.
		if (LoadingState == EAsyncPackageState::Complete)
		{
			LoadingState = CreateLinker();
		}

		// Async create linker.
		if (LoadingState == EAsyncPackageState::Complete)
		{
			LoadingState = FinishLinker();
		}

		// Load imports from linker import table asynchronously.
		if (LoadingState == EAsyncPackageState::Complete)
		{
			LoadingState = LoadImports();
		}

		// Create imports from linker import table.
		if (LoadingState == EAsyncPackageState::Complete)
		{
			LoadingState = CreateImports();
		}

		// Finish all async texture allocations.
		if (LoadingState == EAsyncPackageState::Complete)
		{
			LoadingState = FinishTextureAllocations();
		}

		// Create exports from linker export table and also preload them.
		if (LoadingState == EAsyncPackageState::Complete)
		{
			LoadingState = CreateExports();
		}

		// Call Preload on the linker for all loaded objects which causes actual serialization.
		if (LoadingState == EAsyncPackageState::Complete)
		{
			LoadingState = PreLoadObjects();
		}

		// Call PostLoad on objects, this could cause new objects to be loaded that require
		// another iteration of the PreLoad loop.
		if (LoadingState == EAsyncPackageState::Complete)
		{
			LoadingState = PostLoadObjects();
		}

		// We are done loading the package for now. Whether it is done or not, let the package time tracker know.
		FExclusiveLoadPackageTimeTracker::PopLoadPackage(Linker ? Linker->LinkerRoot : nullptr);

		// End async loading, simulates EndLoad
		EndAsyncLoad();

		// Finish objects (removing EInternalObjectFlags::AsyncLoading, dissociate imports and forced exports, 
		// call completion callback, ...
		// If the load has failed, perform completion callbacks and then quit
		if (LoadingState == EAsyncPackageState::Complete || bLoadHasFailed)
		{
			LoadingState = FinishObjects();
		}
	} while (!IsTimeLimitExceeded() && LoadingState == EAsyncPackageState::TimeOut);

	check(bUseTimeLimit || LoadingState != EAsyncPackageState::TimeOut || AsyncLoadingThread.IsAsyncLoadingSuspended());

	// We can't have a reference to a UObject.
	LastObjectWorkWasPerformedOn = nullptr;
	// Reset type of work performed.
	LastTypeOfWorkPerformed = nullptr;
	// Mark this package as loaded if everything completed.
	bLoadHasFinished = (LoadingState == EAsyncPackageState::Complete);
	// Subtract the time it took to load this package from the global limit.
	InOutTimeLimit = (float)FMath::Max(0.0, InOutTimeLimit - (FPlatformTime::Seconds() - TickStartTime));

	// true means that we're done loading this package.
	return LoadingState;
}

/**
 * Create linker async. Linker is not finalized at this point.
 *
 * @return true
 */
EAsyncPackageState::Type FAsyncPackage::CreateLinker()
{
	if (Linker == nullptr)
	{
		SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_CreateLinker);

		LastObjectWorkWasPerformedOn	= nullptr;
		LastTypeOfWorkPerformed			= TEXT("creating Linker");

		// Try to find existing package or create it if not already present.
		UPackage* Package = nullptr;
		{
			FGCScopeGuard GCGuard;
			Package = CreatePackage(nullptr, *Desc.Name.ToString());
			FAsyncObjectsReferencer::Get().AddObject(Package);
			LinkerRoot = Package;
		}
		FScopeCycleCounterUObject ConstructorScope(Package, GET_STATID(STAT_FAsyncPackage_CreateLinker));

		// Set package specific data 
		Package->SetPackageFlags(Desc.PackageFlags);
#if WITH_EDITOR
		Package->PIEInstanceID = Desc.PIEInstanceID;
#endif

		// Always store package filename we loading from
		Package->FileName = Desc.NameToLoad;
#if WITH_EDITORONLY_DATA
		// Assume all packages loaded through async loading are required by runtime
		Package->SetLoadedByEditorPropertiesOnly(false);
#endif

		// if the linker already exists, we don't need to lookup the file (it may have been pre-created with
		// a different filename)
		Linker = FLinkerLoad::FindExistingLinkerForPackage(Package);

		if (!Linker)
		{
			FString PackageFileName;
			if (Desc.NameToLoad == NAME_None || 
				(!GetConvertedDynamicPackageNameToTypeName().Contains(Desc.Name) &&
				 !FPackageName::DoesPackageExist(Desc.NameToLoad.ToString(), Desc.Guid.IsValid() ? &Desc.Guid : nullptr, &PackageFileName)))
			{
				UE_LOG(LogStreaming, Error, TEXT("Couldn't find file for package %s requested by async loading code."), *Desc.Name.ToString());
				bLoadHasFailed = true;
				return EAsyncPackageState::TimeOut;
			}

			// Create raw async linker, requiring to be ticked till finished creating.
			uint32 LinkerFlags = LOAD_None;
			if (FApp::IsGame() && !GIsEditor)
			{
				LinkerFlags |= (LOAD_SeekFree | LOAD_NoVerify);
			}
#if WITH_EDITOR
			else if ((Desc.PackageFlags & PKG_PlayInEditor) != 0)
			{
				LinkerFlags |= LOAD_PackageForPIE;
			}
#endif
			Linker = FLinkerLoad::CreateLinkerAsync(Package, *PackageFileName, LinkerFlags);
		}

		// Associate this async package with the linker
		check(Linker->AsyncRoot == nullptr || Linker->AsyncRoot == this);
		Linker->AsyncRoot = this;

		UE_LOG(LogStreaming, Verbose, TEXT("FAsyncPackage::CreateLinker for %s finished."), *Desc.NameToLoad.ToString());
	}
	return EAsyncPackageState::Complete;
}

/**
 * Finalizes linker creation till time limit is exceeded.
 *
 * @return true if linker is finished being created, false otherwise
 */
EAsyncPackageState::Type FAsyncPackage::FinishLinker()
{
	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;
	if (Linker && !Linker->HasFinishedInitialization())
	{
		SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_FinishLinker);
		LastObjectWorkWasPerformedOn	= LinkerRoot;
		LastTypeOfWorkPerformed			= TEXT("ticking linker");
	
		const float RemainingTimeLimit = TimeLimit - (float)(FPlatformTime::Seconds() - TickStartTime);

		// Operation still pending if Tick returns false
		FLinkerLoad::ELinkerStatus LinkerResult = Linker->Tick(RemainingTimeLimit, bUseTimeLimit, bUseFullTimeLimit);
		if (LinkerResult != FLinkerLoad::LINKER_Loaded)
		{
			// Give up remainder of timeslice if there is one to give up.
			GiveUpTimeSlice();
			Result = EAsyncPackageState::TimeOut;
			if (LinkerResult == FLinkerLoad::LINKER_Failed)
			{
				// If linker failed we exit with EAsyncPackageState::TimeOut to skip all the remaining steps.
				// The error will be handled as bLoadHasFailed will be true.
				bLoadHasFailed = true;
			}
		}
	}

	return Result;
}

/**
 * Find a package by name.
 * 
 * @param Dependencies package list.
 * @param PackageName long package name.
 * @return Index into the array if the package was found, otherwise INDEX_NONE
 */
FORCEINLINE int32 ContainsDependencyPackage(TArray<FAsyncPackage*>& Dependencies, const FName& PackageName)
{
	for (int32 Index = 0; Index < Dependencies.Num(); ++Index)
	{
		if (Dependencies[Index]->GetPackageName() == PackageName)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}


/**
	* Adds a package to the list of pending import packages.
	*
	* @param PendingImport Name of the package imported either directly or by one of the imported packages
	*/
void FAsyncPackage::AddImportDependency(int32 CurrentPackageIndex, const FName& PendingImport)
{
	FAsyncPackage* PackageToStream = nullptr;
	int32 ExistingAsyncPackageIndex = FAsyncLoadingThread::Get().FindAsyncPackage(PendingImport);
	if (ExistingAsyncPackageIndex == INDEX_NONE)
	{
		const FAsyncPackageDesc Info(INDEX_NONE, PendingImport);
		PackageToStream = new FAsyncPackage(Info);

		// If priority of the dependency is not set, inherit from parent.
		if (PackageToStream->Desc.Priority == 0)
		{
			PackageToStream->Desc.Priority = Desc.Priority;
		}
		FAsyncLoadingThread::Get().InsertPackage(PackageToStream);
	}
	else
	{
		PackageToStream = FAsyncLoadingThread::Get().GetPackage(ExistingAsyncPackageIndex);
	}
	
	if (!PackageToStream->HasFinishedLoading() && 
		!PackageToStream->bLoadHasFailed)
	{
		const bool bInternalCallback = true;
		PackageToStream->AddCompletionCallback(FLoadPackageAsyncDelegate::CreateRaw(this, &FAsyncPackage::ImportFullyLoadedCallback), bInternalCallback);
		PackageToStream->DependencyRefCount.Increment();
		PendingImportedPackages.Add(PackageToStream);
	}
	else
	{
		PackageToStream->DependencyRefCount.Increment();
		ReferencedImports.Add(PackageToStream);
	}
}

/**
 * Adds a unique package to the list of packages to wait for until their linkers have been created.
 *
 * @param PendingImport Package imported either directly or by one of the imported packages
 */
bool FAsyncPackage::AddUniqueLinkerDependencyPackage(int32 CurrentPackageIndex, FAsyncPackage& PendingImport)
{
	if (ContainsDependencyPackage(PendingImportedPackages, PendingImport.GetPackageName()) == INDEX_NONE)
	{
		FLinkerLoad* PendingImportLinker = PendingImport.Linker;
		if (PendingImportLinker == nullptr || !PendingImportLinker->HasFinishedInitialization())
		{
			AddImportDependency(CurrentPackageIndex, PendingImport.GetPackageName());
			UE_LOG(LogStreaming, Verbose, TEXT("  Adding linker dependency %s"), *PendingImport.GetPackageName().ToString());
		}
		else if (this != &PendingImport)
		{
			return false;
		}
	}
	return true;
}

/**
 * Adds dependency tree to the list if packages to wait for until their linkers have been created.
 *
 * @param ImportedPackage Package imported either directly or by one of the imported packages
 */
void FAsyncPackage::AddDependencyTree(int32 CurrentPackageIndex, FAsyncPackage& ImportedPackage, TSet<FAsyncPackage*>& SearchedPackages)
{
	if (SearchedPackages.Contains(&ImportedPackage))
	{
		// we've already searched this package
		return;
	}
	for (int32 Index = 0; Index < ImportedPackage.PendingImportedPackages.Num(); ++Index)
	{
		FAsyncPackage& PendingImport = *ImportedPackage.PendingImportedPackages[Index];
		if (!AddUniqueLinkerDependencyPackage(CurrentPackageIndex, PendingImport))
		{
			AddDependencyTree(CurrentPackageIndex, PendingImport, SearchedPackages);
		}
	}
	// Mark this package as searched
	SearchedPackages.Add(&ImportedPackage);
}

/** 
 * Load imports till time limit is exceeded.
 *
 * @return true if we finished load all imports, false otherwise
 */
EAsyncPackageState::Type FAsyncPackage::LoadImports()
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_LoadImports);
	LastObjectWorkWasPerformedOn	= LinkerRoot;
	LastTypeOfWorkPerformed			= TEXT("loading imports");
	
	// Index of this package in the async queue.
	const int32 AsyncQueueIndex = FAsyncLoadingThread::Get().FindAsyncPackage(GetPackageName());

	// GC can't run in here
	FGCScopeGuard GCGuard;

	// Create imports.
	while (LoadImportIndex < Linker->ImportMap.Num() && !IsTimeLimitExceeded())
	{
		// Get the package for this import
		const FObjectImport* Import = &Linker->ImportMap[LoadImportIndex++];

		while (Import->OuterIndex.IsImport())
		{
			Import = &Linker->Imp(Import->OuterIndex);
		}
		check(Import->OuterIndex.IsNull());

		// @todo: why do we need this? some UFunctions have null outer in the linker.
		if (Import->ClassName != NAME_Package)
		{
			continue;
		}
			

		// Don't try to import a package that is in an import table that we know is an invalid entry
		if (FLinkerLoad::KnownMissingPackages.Contains(Import->ObjectName))
		{
			continue;
		}

		// Our import package name is the import name
		const FName ImportPackageFName(Import->ObjectName);

		// Handle circular dependencies - try to find existing packages.
		UPackage* ExistingPackage = dynamic_cast<UPackage*>(StaticFindObjectFast(UPackage::StaticClass(), nullptr, ImportPackageFName, true));
		if (ExistingPackage && !ExistingPackage->HasAnyPackageFlags(PKG_CompiledIn) && !ExistingPackage->bHasBeenFullyLoaded)//!ExistingPackage->HasAnyFlags(RF_WasLoaded))
		{
			// The import package already exists. Check if it's currently being streamed as well. If so, make sure
			// we add all dependencies that don't yet have linkers created otherwise we risk that if the current package
			// doesn't depend on any other packages that have not yet started streaming, creating imports is going
			// to load packages blocking the main thread.
			int32 PendingAsyncPackageIndex = FAsyncLoadingThread::Get().FindAsyncPackage(ImportPackageFName);
			if (PendingAsyncPackageIndex != INDEX_NONE)
			{
				FAsyncPackage& PendingPackage = *FAsyncLoadingThread::Get().GetPackage(PendingAsyncPackageIndex);
				FLinkerLoad* PendingPackageLinker = PendingPackage.Linker;
				if (PendingPackageLinker == nullptr || !PendingPackageLinker->HasFinishedInitialization())
				{
					// Add this import to the dependency list.
					AddUniqueLinkerDependencyPackage(AsyncQueueIndex, PendingPackage);
				}
				else
				{
					UE_LOG(LogStreaming, Verbose, TEXT("FAsyncPackage::LoadImports for %s: Linker exists for %s"), *Desc.NameToLoad.ToString(), *ImportPackageFName.ToString());
					// Only keep a reference to this package so that its linker doesn't go away too soon
					PendingPackage.DependencyRefCount.Increment();
					ReferencedImports.Add(&PendingPackage);
					// Check if we need to add its dependencies too.
					TSet<FAsyncPackage*> SearchedPackages;
					AddDependencyTree(AsyncQueueIndex, PendingPackage, SearchedPackages);
				}
			}
		}

		if (!ExistingPackage && ContainsDependencyPackage(PendingImportedPackages, ImportPackageFName) == INDEX_NONE)
		{
			const FString ImportPackageName(Import->ObjectName.ToString());
			// The package doesn't exist and this import is not in the dependency list so add it now.
			if (!FPackageName::IsShortPackageName(ImportPackageName))
			{
				UE_LOG(LogStreaming, Verbose, TEXT("FAsyncPackage::LoadImports for %s: Loading %s"), *Desc.NameToLoad.ToString(), *ImportPackageName);
				AddImportDependency(AsyncQueueIndex, ImportPackageFName);
			}
			else
			{
				// This usually means there's a reference to a script package from another project
				UE_LOG(LogStreaming, Warning, TEXT("FAsyncPackage::LoadImports for %s: Short package name in imports list: %s"), *Desc.NameToLoad.ToString(), *ImportPackageName);
			}
		}

		UpdateLoadPercentage();
	}
			
	
	if (PendingImportedPackages.Num())
	{
		GiveUpTimeSlice();
		return EAsyncPackageState::PendingImports;
	}
	return LoadImportIndex == Linker->ImportMap.Num() ? EAsyncPackageState::Complete : EAsyncPackageState::TimeOut;
}

/**
 * Function called when pending import package has been fully loaded.
 */
void FAsyncPackage::ImportFullyLoadedCallback(const FName& InPackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result)
{
	if (Result != EAsyncLoadingResult::Canceled)
	{
		UE_LOG(LogStreaming, Verbose, TEXT("FAsyncPackage::LoadImports for %s: Loaded %s"), *Desc.NameToLoad.ToString(), *InPackageName.ToString());
		int32 Index = ContainsDependencyPackage(PendingImportedPackages, InPackageName);
		check(Index != INDEX_NONE);
		// Keep a reference to this package so that its linker doesn't go away too soon
		ReferencedImports.Add(PendingImportedPackages[Index]);
		PendingImportedPackages.RemoveAt(Index);
	}
}

/** 
 * Create imports till time limit is exceeded.
 *
 * @return true if we finished creating all imports, false otherwise
 */
EAsyncPackageState::Type FAsyncPackage::CreateImports()
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_CreateImports);

	// GC can't run in here
	FGCScopeGuard GCGuard;

	// Create imports.
	while( ImportIndex < Linker->ImportMap.Num() && !IsTimeLimitExceeded() )
	{
 		UObject* Object	= Linker->CreateImport( ImportIndex++ );
		LastObjectWorkWasPerformedOn	= Object;
		LastTypeOfWorkPerformed			= TEXT("creating imports for");

		// Make sure this object is not claimed by GC if it's triggered while streaming.
		FAsyncObjectsReferencer::Get().AddObject(Object);
	}

	return ImportIndex == Linker->ImportMap.Num() ? EAsyncPackageState::Complete : EAsyncPackageState::TimeOut;
}

/**
 * Checks if all async texture allocations for this package have been completed.
 *
 * @return true if all texture allocations have been completed, false otherwise
 */
EAsyncPackageState::Type FAsyncPackage::FinishTextureAllocations()
{
	//@TODO: Cancel allocations if they take too long.
#if WITH_ENGINE
	bool bHasCompleted = Linker->Summary.TextureAllocations.HasCompleted();
	if ( !bHasCompleted )
	{
		SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_FinishTextureAllocations);
		if ( bUseTimeLimit && !bUseFullTimeLimit)
		{
			// Try again next Tick instead.
			GiveUpTimeSlice();
		}
		else
		{
			// Need to finish right now. Cancel async allocations that haven't finished yet.
			// Those will be allocated immediately by UTexture2D during serialization instead.
			Linker->Summary.TextureAllocations.CancelRemainingAllocations( false );
			bHasCompleted = true;
		}
	}
	return bHasCompleted ? EAsyncPackageState::Complete : EAsyncPackageState::TimeOut;
#else
	return EAsyncPackageState::Complete;
#endif		// WITH_ENGINE
}

/**
 * Create exports till time limit is exceeded.
 *
 * @return true if we finished creating and preloading all exports, false otherwise.
 */
EAsyncPackageState::Type FAsyncPackage::CreateExports()
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_CreateExports);

	// GC can't run in here
	FGCScopeGuard GCGuard;

	// Create exports.
	while( ExportIndex < Linker->ExportMap.Num() && !IsTimeLimitExceeded() )
	{
		const FObjectExport& Export = Linker->ExportMap[ExportIndex];
		
		// Precache data and see whether it's already finished.

		// We have sufficient data in the cache so we can load.
		if( Linker->Precache( Export.SerialOffset, Export.SerialSize ) )
		{
			// Create the object...
			UObject* Object	= Linker->CreateExport( ExportIndex++ );
			// ... and preload it.
			if( Object )
			{				
				// This will cause the object to be serialized. We do this here for all objects and
				// not just UClass and template objects, for which this is required in order to ensure
				// seek free loading, to be able introduce async file I/O.
				Linker->Preload( Object );
			}

			LastObjectWorkWasPerformedOn	= Object;
			LastTypeOfWorkPerformed = TEXT("creating exports for");
				
			UpdateLoadPercentage();
		}
		// Data isn't ready yet. Give up remainder of time slice if we're not using a time limit.
		else if (GiveUpTimeSlice())
		{
			INC_FLOAT_STAT_BY(STAT_AsyncIO_AsyncPackagePrecacheWaitTime, (float)FApp::GetDeltaTime());
			return EAsyncPackageState::TimeOut;
		}
	}
	
	// We no longer need the referenced packages.
	FreeReferencedImports();

	return ExportIndex == Linker->ExportMap.Num() ? EAsyncPackageState::Complete : EAsyncPackageState::TimeOut;
}

/**
 * Removes references to any imported packages.
 */
void FAsyncPackage::FreeReferencedImports()
{	
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_FreeReferencedImports);	

	for (int32 ReferenceIndex = 0; ReferenceIndex < ReferencedImports.Num(); ++ReferenceIndex)
	{
		FAsyncPackage& Ref = *ReferencedImports[ReferenceIndex];
		Ref.DependencyRefCount.Decrement();
		UE_LOG(LogStreaming, Verbose, TEXT("FAsyncPackage::FreeReferencedImports for %s: Releasing %s (%d)"), *Desc.NameToLoad.ToString(), *Ref.GetPackageName().ToString(), Ref.GetDependencyRefCount());
		check(Ref.DependencyRefCount.GetValue() >= 0);
	}
	ReferencedImports.Empty();
}

/**
 * Preloads aka serializes all loaded objects.
 *
 * @return true if we finished serializing all loaded objects, false otherwise.
 */
EAsyncPackageState::Type FAsyncPackage::PreLoadObjects()
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_PreLoadObjects);

	// GC can't run in here
	FGCScopeGuard GCGuard;

	auto& ObjLoaded = FUObjectThreadContext::Get().ObjLoaded;
	// Preload (aka serialize) the objects.
	while (PreLoadIndex < ObjLoaded.Num() && !IsTimeLimitExceeded())
	{
		//@todo async: make this part async as well.
		UObject* Object = ObjLoaded[ PreLoadIndex++ ];
		if( Object && Object->GetLinker() )
		{
			Object->GetLinker()->Preload( Object );
			LastObjectWorkWasPerformedOn = Object;
			LastTypeOfWorkPerformed = TEXT("preloading");
		}
	}

	return PreLoadIndex == ObjLoaded.Num() ? EAsyncPackageState::Complete : EAsyncPackageState::TimeOut;
}

/**
 * Route PostLoad to all loaded objects. This might load further objects!
 *
 * @return true if we finished calling PostLoad on all loaded objects and no new ones were created, false otherwise
 */
EAsyncPackageState::Type FAsyncPackage::PostLoadObjects()
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_PostLoadObjects);
	
	// GC can't run in here
	FGCScopeGuard GCGuard;

	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;
	TGuardValue<bool> GuardIsRoutingPostLoad(FUObjectThreadContext::Get().IsRoutingPostLoad, true);

	auto& ObjLoaded = FUObjectThreadContext::Get().ObjLoaded;
	// PostLoad objects.
	while (PostLoadIndex < ObjLoaded.Num() && PostLoadIndex < PreLoadIndex && !IsTimeLimitExceeded())
	{
		UObject* Object = ObjLoaded[PostLoadIndex++];
		check(Object);
		if (!FAsyncLoadingThread::Get().IsMultithreaded() || Object->IsPostLoadThreadSafe())
		{
			FScopeCycleCounterUObject ConstructorScope(Object, GET_STATID(STAT_FAsyncPackage_PostLoadObjects));

			Object->ConditionalPostLoad();

			LastObjectWorkWasPerformedOn = Object;
			LastTypeOfWorkPerformed = TEXT("postloading_async");
		}
		else
		{
			DeferredPostLoadObjects.Add(Object);
		}
		// All object must be finalized on the game thread
		DeferredFinalizeObjects.Add(Object);
		// Make sure all objects in DeferredFinalizeObjects are referenced too
		FAsyncObjectsReferencer::Get().AddObject(Object);
	}

	// New objects might have been loaded during PostLoad.
	Result = (PreLoadIndex == ObjLoaded.Num()) && (PostLoadIndex == ObjLoaded.Num()) ? EAsyncPackageState::Complete : EAsyncPackageState::TimeOut;

	return Result;
}

void CreateClustersFromPackage(FLinkerLoad* PackageLinker);

EAsyncPackageState::Type FAsyncPackage::PostLoadDeferredObjects(double InTickStartTime, bool bInUseTimeLimit, float& InOutTimeLimit)
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_PostLoadObjectsGameThread);

	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;
	TGuardValue<bool> GuardIsRoutingPostLoad(FUObjectThreadContext::Get().IsRoutingPostLoad, true);
	FAsyncLoadingTickScope InAsyncLoadingTick;

	LastObjectWorkWasPerformedOn = nullptr;
	LastTypeOfWorkPerformed = TEXT("postloading_gamethread");

	TArray<UObject*>& ObjLoadedInPostLoad = FUObjectThreadContext::Get().ObjLoaded;
	TArray<UObject*> ObjLoadedInPostLoadLocal;

	while (DeferredPostLoadIndex < DeferredPostLoadObjects.Num() && 
		!AsyncLoadingThread.IsAsyncLoadingSuspended() &&
		!::IsTimeLimitExceeded(InTickStartTime, bInUseTimeLimit, InOutTimeLimit, LastTypeOfWorkPerformed, LastObjectWorkWasPerformedOn))
	{
		UObject* Object = DeferredPostLoadObjects[DeferredPostLoadIndex++];
		check(Object);

		FScopeCycleCounterUObject ConstructorScope(Object, GET_STATID(STAT_FAsyncPackage_PostLoadObjectsGameThread));

		Object->ConditionalPostLoad();

		if (ObjLoadedInPostLoad.Num())
		{
			// If there were any LoadObject calls inside of PostLoad, we need to pre-load those objects here. 
			// There's no going back to the async tick loop from here.
			UE_LOG(LogStreaming, Warning, TEXT("Detected %d objects loaded in PostLoad while streaming, this may cause hitches as we're blocking async loading to pre-load them."), ObjLoadedInPostLoad.Num());
			
			// Copy to local array because ObjLoadedInPostLoad can change while we're iterating over it
			ObjLoadedInPostLoadLocal.Append(ObjLoadedInPostLoad);
			ObjLoadedInPostLoad.Reset();

			while (ObjLoadedInPostLoadLocal.Num())
			{
				// Make sure all objects loaded in PostLoad get post-loaded too
				DeferredPostLoadObjects.Append(ObjLoadedInPostLoadLocal);

				// Preload (aka serialize) the objects loaded in PostLoad.
				for (UObject* PreLoadObject : ObjLoadedInPostLoadLocal)
				{
					if (PreLoadObject && PreLoadObject->GetLinker())
					{
						PreLoadObject->GetLinker()->Preload(PreLoadObject);
					}
				}

				// Other objects could've been loaded while we were preloading, continue until we've processed all of them.
				ObjLoadedInPostLoadLocal.Reset();
				ObjLoadedInPostLoadLocal.Append(ObjLoadedInPostLoad);
				ObjLoadedInPostLoad.Reset();
			}			
		}

		LastObjectWorkWasPerformedOn = Object;		

		UpdateLoadPercentage();
	}

	// New objects might have been loaded during PostLoad.
	Result = (DeferredPostLoadIndex == DeferredPostLoadObjects.Num()) ? EAsyncPackageState::Complete : EAsyncPackageState::TimeOut;
	if (Result == EAsyncPackageState::Complete)
	{
		// Clear async loading flags (we still want RF_Async, but EInternalObjectFlags::AsyncLoading can be cleared)
		for (UObject* Object : DeferredFinalizeObjects)
		{
			Object->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
		}

		// Mark package as having been fully loaded and update load time.
		if (LinkerRoot && !bLoadHasFailed)
		{
			LinkerRoot->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
			LinkerRoot->MarkAsFullyLoaded();			
			LinkerRoot->SetLoadTime(FPlatformTime::Seconds() - LoadStartTime);

			if (Linker)
			{
				CreateClustersFromPackage(Linker);

				// give a hint to the IO system that we are done with this file for now
				FIOSystem::Get().HintDoneWithFile(Linker->Filename);
			}
		}
	}

	return Result;
}

/**
 * Finish up objects and state, which means clearing the EInternalObjectFlags::AsyncLoading flag on newly created ones
 *
 * @return true
 */
EAsyncPackageState::Type FAsyncPackage::FinishObjects()
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_FinishObjects);
	LastObjectWorkWasPerformedOn	= nullptr;
	LastTypeOfWorkPerformed			= TEXT("finishing all objects");
		
	auto& LoadingGlobals = FUObjectThreadContext::Get();

	EAsyncLoadingResult::Type LoadingResult;
	if (!bLoadHasFailed)
	{
		LoadingGlobals.ObjLoaded.Empty();
		LoadingResult = EAsyncLoadingResult::Succeeded;
	}
	else
	{
		// Cleanup objects from this package only
		for (int32 ObjectIndex = LoadingGlobals.ObjLoaded.Num() - 1; ObjectIndex >= 0; --ObjectIndex)
		{
			UObject* Object = LoadingGlobals.ObjLoaded[ObjectIndex];
			if (Object->GetOutermost()->GetFName() == Desc.Name)
			{
				Object->ClearFlags(RF_NeedPostLoad | RF_NeedLoad | RF_NeedPostLoadSubobjects);
				Object->MarkPendingKill();
				LoadingGlobals.ObjLoaded.RemoveAt(ObjectIndex);
			}
		}
		LoadingResult = EAsyncLoadingResult::Failed;
	}

	// Simulate what EndLoad does.
	DissociateImportsAndForcedExports(); //@todo: this should be avoidable
	PreLoadIndex = 0;
	PostLoadIndex = 0;
	
	// If we successfully loaded
	if (!bLoadHasFailed && Linker)
	{
	#if WITH_ENGINE
		// Cancel all texture allocations that haven't been claimed yet.
		Linker->Summary.TextureAllocations.CancelRemainingAllocations( true );
	#endif		// WITH_ENGINE
	}

	{
		const bool bInternalCallbacks = true;
		CallCompletionCallbacks(bInternalCallbacks, LoadingResult);
	}

	return EAsyncPackageState::Complete;
}

void FAsyncPackage::CallCompletionCallbacks(bool bInternal, EAsyncLoadingResult::Type LoadingResult)
{
	UPackage* LoadedPackage = (!bLoadHasFailed) ? LinkerRoot : nullptr;
	for (auto& CompletionCallback : CompletionCallbacks)
	{
		if (CompletionCallback.bIsInternal == bInternal)
		{
			CompletionCallback.Callback.ExecuteIfBound(Desc.Name, LoadedPackage, LoadingResult);
		}
	}
}

void FAsyncPackage::Cancel()
{
	// Call any completion callbacks specified.
	const EAsyncLoadingResult::Type Result = EAsyncLoadingResult::Canceled;
	for (int32 CallbackIndex = 0; CallbackIndex < CompletionCallbacks.Num(); CallbackIndex++)
	{
		CompletionCallbacks[CallbackIndex].Callback.ExecuteIfBound(Desc.Name, nullptr, Result);
	}
	if (LinkerRoot)
	{
		if (Linker)
		{
			// give a hint to the IO system that we are done with this file for now
			FIOSystem::Get().HintDoneWithFile(Linker->Filename);
			Linker->FlushCache();
		}
		LinkerRoot->ClearFlags(RF_WasLoaded);
		LinkerRoot->bHasBeenFullyLoaded = false;
		LinkerRoot->Rename(*MakeUniqueObjectName(GetTransientPackage(), UPackage::StaticClass()).ToString(), nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_ForceNoResetLoaders | REN_NonTransactional);
		DetachLinker();
	}
	PreLoadIndex = 0;
}

void FAsyncPackage::AddCompletionCallback(const FLoadPackageAsyncDelegate& Callback, bool bInternal)
{
	// This is to ensure that there is no one trying to subscribe to a already loaded package
	//check(!bLoadHasFinished && !bLoadHasFailed);
	CompletionCallbacks.Add(FCompletionCallback(bInternal, Callback));
}

void FAsyncPackage::UpdateLoadPercentage()
{
	// PostLoadCount is just an estimate to prevent packages to go to 100% too quickly
	// We may never reach 100% this way, but it's better than spending most of the load package time at 100%
	float NewLoadPercentage = 0.0f;
	if (Linker)
	{
		const int32 PostLoadCount = FMath::Max(DeferredPostLoadObjects.Num(), Linker->ImportMap.Num());
		NewLoadPercentage = 100.f * (LoadImportIndex + ExportIndex + DeferredPostLoadIndex) / (Linker->ExportMap.Num() + Linker->ImportMap.Num() + PostLoadCount);		
	}
	else if (DeferredPostLoadObjects.Num() > 0)
	{
		NewLoadPercentage = static_cast<float>(DeferredPostLoadIndex) / DeferredPostLoadObjects.Num();
	}
	// It's also possible that we got so many objects to PostLoad that LoadPercantage will actually drop
	LoadPercentage = FMath::Max(NewLoadPercentage, LoadPercentage);
}

int32 LoadPackageAsync(const FString& InName, const FGuid* InGuid /*= nullptr*/, const TCHAR* InPackageToLoadFrom /*= nullptr*/, FLoadPackageAsyncDelegate InCompletionDelegate /*= FLoadPackageAsyncDelegate()*/, EPackageFlags InPackageFlags /*= PKG_None*/, int32 InPIEInstanceID /*= INDEX_NONE*/, int32 InPackagePriority /*= 0*/)
{
#if !WITH_EDITOR
	if (GPreloadPackageDependencies)
	{
		// If dependency preloading is enabled, we need to force the asset registry module to be loaded on the game thread
		// as it will potentiall be used on the async loading thread, which isn't allowed to load modules.
		// We could do this at init time, but doing it here allows us to not load the module at all if preloading is
		// disabled.
		IAssetRegistryInterface::GetPtr();
	}
#endif

	// The comments clearly state that it should be a package name but we also handle it being a filename as this function is not perf critical
	// and LoadPackage handles having a filename being passed in as well.
	FString PackageName;
	if (FPackageName::IsValidLongPackageName(InName, /*bIncludeReadOnlyRoots*/true))
	{
		PackageName = InName;
	}
	// PackageName got populated by the conditional function
	else if (!(FPackageName::IsPackageFilename(InName) && FPackageName::TryConvertFilenameToLongPackageName(PackageName, PackageName)))
	{
		// PackageName will get populated by the conditional function
		FString ClassName;
		if (!FPackageName::ParseExportTextPath(PackageName, &ClassName, &PackageName))
		{
			UE_LOG(LogStreaming, Fatal, TEXT("LoadPackageAsync failed to begin to load a package because the supplied package name was neither a valid long package name nor a filename of a map within a content folder: '%s'"), *PackageName);
		}
	}

	FString PackageNameToLoad(InPackageToLoadFrom);
	if (PackageNameToLoad.IsEmpty())
	{
		PackageNameToLoad = PackageName;
	}
	// Make sure long package name is passed to FAsyncPackage so that it doesn't attempt to 
	// create a package with short name.
	if (FPackageName::IsShortPackageName(PackageNameToLoad))
	{
		UE_LOG(LogStreaming, Fatal, TEXT("Async loading code requires long package names (%s)."), *PackageNameToLoad);
	}

	// Generate new request ID and add it immediately to the global request list (it needs to be there before we exit
	// this function, otherwise it would be added when the packages are being processed on the async thread).
	const int32 RequestID = GPackageRequestID.Increment();
	FAsyncLoadingThread::Get().AddPendingRequest(RequestID);
	// Add new package request
	FAsyncPackageDesc PackageDesc(RequestID, *PackageName, *PackageNameToLoad, InGuid ? *InGuid : FGuid(), InCompletionDelegate, InPackageFlags, InPIEInstanceID, InPackagePriority);
	FAsyncLoadingThread::Get().QueuePackage(PackageDesc);

	return RequestID;
}

int32 LoadPackageAsync(const FString& PackageName, FLoadPackageAsyncDelegate CompletionDelegate, int32 InPackagePriority /*= 0*/, EPackageFlags InPackageFlags /*= PKG_None*/)
{
	const FGuid* Guid = nullptr;
	const TCHAR* PackageToLoadFrom = nullptr;
	return LoadPackageAsync(PackageName, Guid, PackageToLoadFrom, CompletionDelegate, InPackageFlags, -1, InPackagePriority );
}

int32 LoadPackageAsync(const FString& InName, const FGuid* InGuid, FName InType /* Unused, deprecated */, const TCHAR* InPackageToLoadFrom /*= nullptr*/, FLoadPackageAsyncDelegate InCompletionDelegate /*= FLoadPackageAsyncDelegate()*/, EPackageFlags InPackageFlags /*= PKG_None*/, int32 InPIEInstanceID /*= INDEX_NONE*/, int32 InPackagePriority /*= 0*/)
{
	return LoadPackageAsync(InName, InGuid, InPackageToLoadFrom, InCompletionDelegate, InPackageFlags, InPIEInstanceID, InPackagePriority);
}

void CancelAsyncLoading()
{
	// Cancelling async loading while loading is suspend will result in infinite stall
	UE_CLOG(FAsyncLoadingThread::Get().IsAsyncLoadingSuspended(), LogStreaming, Fatal, TEXT("Cannot Cancel Async Loading while async loading is suspended."));

	FAsyncLoadingThread::Get().CancelAsyncLoading();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, false);
}

float GetAsyncLoadPercentage(const FName& PackageName)
{
	return FAsyncLoadingThread::Get().GetAsyncLoadPercentage(PackageName);
}

void InitAsyncThread()
{
	FAsyncLoadingThread::Get().InitializeAsyncThread();
}

bool IsInAsyncLoadingThreadCoreUObjectInternal()
{
	return FAsyncLoadingThread::Get().IsInAsyncLoadThread();
}

void FlushAsyncLoading(int32 PackageID /* = INDEX_NONE */)
{
	if (IsAsyncLoading())
	{
		FAsyncLoadingThread& AsyncThread = FAsyncLoadingThread::Get();
		// Flushing async loading while loading is suspend will result in infinite stall
		UE_CLOG(AsyncThread.IsAsyncLoadingSuspended(), LogStreaming, Fatal, TEXT("Cannot Flush Async Loading while async loading is suspended."));

		SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_FlushAsyncLoadingGameThread);

		if (PackageID != INDEX_NONE && !AsyncThread.ContainsRequestID(PackageID))
		{
			return;
		}

		// Disallow low priority requests like texture streaming while we are flushing streaming
		// in order to avoid excessive seeking.
		FIOSystem::Get().SetMinPriority( AIOP_Normal );

		// Flush async loaders by not using a time limit. Needed for e.g. garbage collection.
		UE_LOG(LogStreaming, Log,  TEXT("Flushing async loaders.") );
		{
			SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_TickAsyncLoadingGameThread);
			while (IsAsyncLoading())
			{
				EAsyncPackageState::Type Result = AsyncThread.TickAsyncLoading(false, false, 0, PackageID);
				if (PackageID != INDEX_NONE && Result == EAsyncPackageState::Complete)
				{
					break;
				}

				if (AsyncThread.IsMultithreaded())
				{
					FPlatformProcess::SleepNoStats(0.0001f);
				}
			}
		}

		check(PackageID != INDEX_NONE || !IsAsyncLoading());

		// Reset min priority again.
		FIOSystem::Get().SetMinPriority( AIOP_MIN );
	}
}

void FlushAsyncLoading(FName ExcludeType)
{
	FlushAsyncLoading();
}

int32 GetNumAsyncPackages()
{
	return FAsyncLoadingThread::Get().GetAsyncPackagesCount();
}

EAsyncPackageState::Type ProcessAsyncLoading(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit)
{
	SCOPE_CYCLE_COUNTER(STAT_AsyncLoadingTime);

	{
		SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_TickAsyncLoadingGameThread);
		FAsyncLoadingThread::Get().TickAsyncLoading(bUseTimeLimit, bUseFullTimeLimit, TimeLimit);
	}

	return IsAsyncLoading() ? EAsyncPackageState::TimeOut : EAsyncPackageState::Complete;
}

bool IsAsyncLoadingCoreUObjectInternal()
{
	// GIsInitialLoad guards the async loading thread from being created too early
	return !GIsInitialLoad && FAsyncLoadingThread::Get().IsAsyncLoadingPackages();
}

void SuspendAsyncLoadingInternal()
{
	FAsyncLoadingThread::Get().SuspendLoading();
}

void ResumeAsyncLoadingInternal()
{
	FAsyncLoadingThread::Get().ResumeLoading();
}

/*----------------------------------------------------------------------------
	FArchiveAsync.
----------------------------------------------------------------------------*/

/**
 * Constructor, initializing all member variables.
 */
FArchiveAsync::FArchiveAsync( const TCHAR* InFileName )
	: FileName(InFileName)
	, FileSize(INDEX_NONE)
	, UncompressedFileSize(INDEX_NONE)
	, BulkDataAreaSize(0)
	, CurrentPos(0)
	, CompressedChunks(nullptr)
	, CurrentChunkIndex(0)
	, CompressionFlags(COMPRESS_None)
	, PlatformIsSinglethreaded(false)
{
	/** Cache FPlatformProcess::SupportsMultithreading() value as it shows up too often in profiles */
	PlatformIsSinglethreaded = !FPlatformProcess::SupportsMultithreading();

	ArIsLoading		= true;
	ArIsPersistent	= true;

	PrecacheStartPos[CURRENT]	= 0;
	PrecacheEndPos[CURRENT]		= 0;
	PrecacheBuffer[CURRENT]		= nullptr;

	PrecacheStartPos[NEXT]		= 0;
	PrecacheEndPos[NEXT]		= 0;
	PrecacheBuffer[NEXT]		= nullptr;

	// Relies on default constructor initializing to 0.
	check( PrecacheReadStatus[CURRENT].GetValue() == 0 );
	check( PrecacheReadStatus[NEXT].GetValue() == 0 );

	// Cache file size.
	FileSize = IFileManager::Get().FileSize( *FileName );
	// Check whether file existed.
	if( FileSize >= 0 )
	{
		// No error.
		ArIsError	= false;

		// Retrieved uncompressed file size.
		UncompressedFileSize = INDEX_NONE;

		// Package wasn't compressed so use regular file size.
		if( UncompressedFileSize == INDEX_NONE )
		{
			UncompressedFileSize = FileSize;
		}
	}
	else
	{
		// Couldn't open the file.
		ArIsError	= true;
	}
}

/**
 * Flushes cache and frees internal data.
 */
void FArchiveAsync::FlushCache()
{
	// Wait on all outstanding requests.
	if (PrecacheReadStatus[CURRENT].GetValue() || PrecacheReadStatus[NEXT].GetValue())
	{
		SCOPE_CYCLE_COUNTER(STAT_Sleep);
#if !( PLATFORM_WINDOWS && defined(__clang__) )	// @todo clang: Clang r231657 on Windows has bugs with inlining DLL imported functions
		FThreadIdleStats::FScopeIdle Scope;
#endif
		do
		{
			SHUTDOWN_IF_EXIT_REQUESTED;
			FPlatformProcess::SleepNoStats(0.0f);
		} while (PrecacheReadStatus[CURRENT].GetValue() || PrecacheReadStatus[NEXT].GetValue());
	}

	uint32 Delta = 0;

	// Invalidate any precached data and free memory for current buffer.
	Delta += PrecacheEndPos[CURRENT] - PrecacheStartPos[CURRENT];
	FMemory::Free( PrecacheBuffer[CURRENT] );
	PrecacheBuffer[CURRENT]		= nullptr;
	PrecacheStartPos[CURRENT]	= 0;
	PrecacheEndPos[CURRENT]		= 0;
	
	// Invalidate any precached data and free memory for next buffer.
	FMemory::Free( PrecacheBuffer[NEXT] );
	PrecacheBuffer[NEXT]		= nullptr;
	PrecacheStartPos[NEXT]		= 0;
	PrecacheEndPos[NEXT]		= 0;

	Delta += PrecacheEndPos[NEXT] - PrecacheStartPos[NEXT];
	DEC_DWORD_STAT_BY(STAT_StreamingAllocSize, Delta);
}

/**
 * Virtual destructor cleaning up internal file reader.
 */
FArchiveAsync::~FArchiveAsync()
{
	// Invalidate any precached data and free memory.
	FlushCache();
}

/**
 * Close archive and return whether there has been an error.
 *
 * @return	true if there were NO errors, false otherwise
 */
bool FArchiveAsync::Close()
{
	// Invalidate any precached data and free memory.
	FlushCache();
	// Return true if there were NO errors, false otherwise.
	return !ArIsError;
}

/**
 * Sets mapping from offsets/ sizes that are going to be used for seeking and serialization to what
 * is actually stored on disk. If the archive supports dealing with compression in this way it is 
 * going to return true.
 *
 * @param	InCompressedChunks	Pointer to array containing information about [un]compressed chunks
 * @param	InCompressionFlags	Flags determining compression format associated with mapping
 *
 * @return true if archive supports translating offsets & uncompressing on read, false otherwise
 */
bool FArchiveAsync::SetCompressionMap( TArray<FCompressedChunk>* InCompressedChunks, ECompressionFlags InCompressionFlags )
{
	// Set chunks. A value of nullptr means to use direct reads again.
	CompressedChunks	= InCompressedChunks;
	CompressionFlags	= InCompressionFlags;
	CurrentChunkIndex	= 0;
	// Invalidate any precached data and free memory.
	FlushCache();

	// verify some assumptions
	check(UncompressedFileSize == FileSize);
	check(CompressedChunks->Num() > 0);

	// update the uncompressed filesize (which is the end of the uncompressed last chunk)
	FCompressedChunk& LastChunk = (*CompressedChunks)[CompressedChunks->Num() - 1];
	UncompressedFileSize = LastChunk.UncompressedOffset + LastChunk.UncompressedSize;

	BulkDataAreaSize = FileSize - (LastChunk.CompressedOffset + LastChunk.CompressedSize);

	// We support translation as requested.
	return true;
}

/**
 * Swaps current and next buffer. Relies on calling code to ensure that there are no outstanding
 * async read operations into the buffers.
 */
void FArchiveAsync::BufferSwitcheroo()
{
	check( PrecacheReadStatus[CURRENT].GetValue() == 0 );
	check( PrecacheReadStatus[NEXT].GetValue() == 0 );

	// Switcheroo.
	DEC_DWORD_STAT_BY(STAT_StreamingAllocSize, PrecacheEndPos[CURRENT] - PrecacheStartPos[CURRENT]);
	FMemory::Free( PrecacheBuffer[CURRENT] );
	PrecacheBuffer[CURRENT]		= PrecacheBuffer[NEXT];
	PrecacheStartPos[CURRENT]	= PrecacheStartPos[NEXT];
	PrecacheEndPos[CURRENT]		= PrecacheEndPos[NEXT];

	// Next buffer is unused/ free.
	PrecacheBuffer[NEXT]		= nullptr;
	PrecacheStartPos[NEXT]		= 0;
	PrecacheEndPos[NEXT]		= 0;
}

/**
 * Whether the current precache buffer contains the passed in request.
 *
 * @param	RequestOffset	Offset in bytes from start of file
 * @param	RequestSize		Size in bytes requested
 *
 * @return true if buffer contains request, false othwerise
 */
bool FArchiveAsync::PrecacheBufferContainsRequest( int64 RequestOffset, int64 RequestSize )
{
	// true if request is part of precached buffer.
	if( (RequestOffset >= PrecacheStartPos[CURRENT]) 
	&&  (RequestOffset+RequestSize <= PrecacheEndPos[CURRENT]) )
	{
		return true;
	}
	// false if it doesn't fit 100%.
	else
	{
		return false;
	}
}

/**
 * Finds and returns the compressed chunk index associated with the passed in offset.
 *
 * @param	RequestOffset	Offset in file to find associated chunk index for
 *
 * @return Index into CompressedChunks array matching this offset
 */
int32 FArchiveAsync::FindCompressedChunkIndex( int64 RequestOffset )
{
	// Find base start point and size. @todo optimization: avoid full iteration
	CurrentChunkIndex = 0;
	while( CurrentChunkIndex < CompressedChunks->Num() )
	{
		const FCompressedChunk& Chunk = (*CompressedChunks)[CurrentChunkIndex];
		// Check whether request offset is encompassed by this chunk.
		if( Chunk.UncompressedOffset <= RequestOffset 
		&&  Chunk.UncompressedOffset + Chunk.UncompressedSize > RequestOffset )
		{
			break;
		}
		CurrentChunkIndex++;
	}
	check( CurrentChunkIndex < CompressedChunks->Num() );
	return CurrentChunkIndex;
}

/**
 * Precaches compressed chunk of passed in index using buffer at passed in index.
 *
 * @param	ChunkIndex	Index of compressed chunk
 * @param	BufferIndex	Index of buffer to precache into	
 */
void FArchiveAsync::PrecacheCompressedChunk( int64 ChunkIndex, int64 BufferIndex )
{
	// Compressed chunk to request.
	FCompressedChunk ChunkToRead = (*CompressedChunks)[ChunkIndex];

	// Update start and end position...
	{
		DEC_DWORD_STAT_BY(STAT_StreamingAllocSize, PrecacheEndPos[BufferIndex] - PrecacheStartPos[BufferIndex]);
	}
	PrecacheStartPos[BufferIndex]	= ChunkToRead.UncompressedOffset;
	PrecacheEndPos[BufferIndex]		= ChunkToRead.UncompressedOffset + ChunkToRead.UncompressedSize;

	// In theory we could use FMemory::Realloc if it had a way to signal that we don't want to copy
	// the data (implicit realloc behavior).
	FMemory::Free( PrecacheBuffer[BufferIndex] );
	PrecacheBuffer[BufferIndex]		= (uint8*) FMemory::Malloc( PrecacheEndPos[BufferIndex] - PrecacheStartPos[BufferIndex] );
	{
		INC_DWORD_STAT_BY(STAT_StreamingAllocSize, PrecacheEndPos[BufferIndex] - PrecacheStartPos[BufferIndex]);
	}

	// Increment read status, request load and make sure that request was possible (e.g. filename was valid).
	check( PrecacheReadStatus[BufferIndex].GetValue() == 0 );
	PrecacheReadStatus[BufferIndex].Increment();
	uint64 RequestId = FIOSystem::Get().LoadCompressedData( 
							FileName, 
							ChunkToRead.CompressedOffset, 
							ChunkToRead.CompressedSize, 
							ChunkToRead.UncompressedSize, 
							PrecacheBuffer[BufferIndex], 
							CompressionFlags, 
							&PrecacheReadStatus[BufferIndex],
							AIOP_Normal);
	check(RequestId);
}

/**
 * Hint the archive that the region starting at passed in offset and spanning the passed in size
 * is going to be read soon and should be precached.
 *
 * The function returns whether the precache operation has completed or not which is an important
 * hint for code knowing that it deals with potential async I/O. The archive is free to either not 
 * implement this function or only partially precache so it is required that given sufficient time
 * the function will return true. Archives not based on async I/O should always return true.
 *
 * This function will not change the current archive position.
 *
 * @param	RequestOffset	Offset at which to begin precaching.
 * @param	RequestSize		Number of bytes to precache
 * @return	false if precache operation is still pending, true otherwise
 */
bool FArchiveAsync::Precache( int64 RequestOffset, int64 RequestSize )
{
	SCOPE_CYCLE_COUNTER(STAT_FArchiveAsync_Precache);

	// Check whether we're currently waiting for a read request to finish.
	bool bFinishedReadingCurrent	= PrecacheReadStatus[CURRENT].GetValue()==0 ? true : false;
	bool bFinishedReadingNext		= PrecacheReadStatus[NEXT].GetValue()==0 ? true : false;

	// Return read status if the current request fits entirely in the precached region.
	if( PrecacheBufferContainsRequest( RequestOffset, RequestSize ) )
	{
		if (!bFinishedReadingCurrent && PlatformIsSinglethreaded)
		{
			// Tick async loading when multithreading is disabled.
			FIOSystem::Get().TickSingleThreaded();
		}
		return bFinishedReadingCurrent;
	}
	// We're not fitting into the precached region and we have a current read request outstanding
	// so wait till we're done with that. This can happen if we're skipping over large blocks in
	// the file because the object has been found in memory.
	// @todo async: implement cancelation
	else if( !bFinishedReadingCurrent )
	{
		return false;
	}
	// We're still in the middle of fulfilling the next read request so wait till that is done.
	else if( !bFinishedReadingNext )
	{
		return false;
	}
	// We need to make a new read request.
	else
	{
		// Compressed read. The passed in offset and size were requests into the uncompressed file and
		// need to be translated via the CompressedChunks map first.
		if( CompressedChunks && RequestOffset < UncompressedFileSize)
		{
			// Switch to next buffer.
			BufferSwitcheroo();

			// Check whether region is precached after switcheroo.
			bool	bIsRequestCached	= PrecacheBufferContainsRequest( RequestOffset, RequestSize );
			// Find chunk associated with request.
			int32		RequestChunkIndex	= FindCompressedChunkIndex( RequestOffset );

			// Precache chunk if it isn't already.
			if( !bIsRequestCached )
			{
				PrecacheCompressedChunk( RequestChunkIndex, CURRENT );
			}

			// Precache next chunk if there is one.
			if( RequestChunkIndex + 1 < CompressedChunks->Num() )
			{
				PrecacheCompressedChunk( RequestChunkIndex + 1, NEXT );
			}
		}
		// Regular read.
		else
		{
			// Request generic async IO system.
			{
				DEC_DWORD_STAT_BY(STAT_StreamingAllocSize, PrecacheEndPos[CURRENT] - PrecacheStartPos[CURRENT]);
			}
			PrecacheStartPos[CURRENT]	= RequestOffset;
			// We always request at least a few KByte to be read/ precached to avoid going to disk for
			// a lot of little reads.
			static int64 MinimumReadSize = FIOSystem::Get().MinimumReadSize();
			checkSlow(MinimumReadSize >= 2048 && MinimumReadSize <= 1024 * 1024); // not a hard limit, but we should be loading at least a reasonable amount of data
			PrecacheEndPos[CURRENT]		= RequestOffset + FMath::Max( RequestSize, MinimumReadSize );
			// Ensure that we're not trying to read beyond EOF.
			PrecacheEndPos[CURRENT]		= FMath::Min( PrecacheEndPos[CURRENT], FileSize );
			// In theory we could use FMemory::Realloc if it had a way to signal that we don't want to copy
			// the data (implicit realloc behavior).
			FMemory::Free( PrecacheBuffer[CURRENT] );

			PrecacheBuffer[CURRENT]		= (uint8*) FMemory::Malloc( PrecacheEndPos[CURRENT] - PrecacheStartPos[CURRENT] );
			{
				INC_DWORD_STAT_BY(STAT_StreamingAllocSize, PrecacheEndPos[CURRENT] - PrecacheStartPos[CURRENT]);
			}

			// Increment read status, request load and make sure that request was possible (e.g. filename was valid).
			PrecacheReadStatus[CURRENT].Increment();
			uint64 RequestId = FIOSystem::Get().LoadData( 
									FileName, 
									PrecacheStartPos[CURRENT], 
									PrecacheEndPos[CURRENT] - PrecacheStartPos[CURRENT], 
									PrecacheBuffer[CURRENT], 
									&PrecacheReadStatus[CURRENT],
									AIOP_Normal);
			check(RequestId);
		}

		return false;
	}
}

/**
 * Serializes data from archive.
 *
 * @param	Data	Pointer to serialize to
 * @param	Count	Number of bytes to read
 */
void FArchiveAsync::Serialize(void* Data, int64 Count)
{
	// Ensure we aren't reading beyond the end of the file
	checkf( CurrentPos + Count <= TotalSize(), TEXT("Seeked past end of file %s (%lld / %lld)"), *FileName, CurrentPos + Count, TotalSize() );

#if LOOKING_FOR_PERF_ISSUES
	uint32 StartCycles = 0;
	bool	bIOBlocked	= false;
#endif

	// Make sure serialization request fits entirely in already precached region.
	if( !PrecacheBufferContainsRequest( CurrentPos, Count ) )
	{
		DECLARE_SCOPE_CYCLE_COUNTER( TEXT( "FArchiveAsync::Serialize.PrecacheBufferContainsRequest" ), STAT_ArchiveAsync_Serialize_PrecacheBufferContainsRequest, STATGROUP_AsyncLoad );

#if LOOKING_FOR_PERF_ISSUES
		// Keep track of time we started to block.
		StartCycles = FPlatformTime::Cycles();
		bIOBlocked = true;
#endif

		// Busy wait for region to be precached.
		if (!Precache(CurrentPos, Count))
		{
			SCOPE_CYCLE_COUNTER(STAT_Sleep);
#if !( PLATFORM_WINDOWS && defined(__clang__) )	// @todo clang: Clang r231657 on Windows has bugs with inlining DLL imported functions
			FThreadIdleStats::FScopeIdle Scope;
#endif
			do
			{
				SHUTDOWN_IF_EXIT_REQUESTED;
				if (PlatformIsSinglethreaded)
				{
					FIOSystem::Get().TickSingleThreaded();
				}
				FPlatformProcess::SleepNoStats(0.0f);
			} while (!Precache(CurrentPos, Count));
		}

		// There shouldn't be any outstanding read requests for the main buffer at this point.
		check( PrecacheReadStatus[CURRENT].GetValue() == 0 );
	}
	
	// Make sure to wait till read request has finished before progressing. This can happen if PreCache interface
	// is not being used for serialization.
	if (PrecacheReadStatus[CURRENT].GetValue())
	{
		SCOPE_CYCLE_COUNTER(STAT_Sleep);
#if !( PLATFORM_WINDOWS && defined(__clang__) )	// @todo clang: Clang r231657 on Windows has bugs with inlining DLL imported functions
		FThreadIdleStats::FScopeIdle Scope;
#endif
		do
		{
			SHUTDOWN_IF_EXIT_REQUESTED;
#if LOOKING_FOR_PERF_ISSUES
			// Only update StartTime if we haven't already started blocking I/O above.
			if (!bIOBlocked)
			{
				// Keep track of time we started to block.
				StartCycles = FPlatformTime::Cycles();
				bIOBlocked = true;
			}
#endif
			if (PlatformIsSinglethreaded)
			{
				FIOSystem::Get().TickSingleThreaded();
			}
			FPlatformProcess::SleepNoStats(0.0f);
		} while (PrecacheReadStatus[CURRENT].GetValue());
	}

	// Update stats if we were blocked.
#if LOOKING_FOR_PERF_ISSUES
	if( bIOBlocked )
	{
		const int32 BlockingCycles = int32(FPlatformTime::Cycles() - StartCycles);
		FAsyncLoadingThread::BlockingCycles.Add( BlockingCycles );

		UE_LOG(LogStreaming, Warning, TEXT("FArchiveAsync::Serialize: %5.2fms blocking on read from '%s' (Offset: %lld, Size: %lld)"), 
			FPlatformTime::ToMilliseconds(BlockingCycles), 
			*FileName, 
			CurrentPos, 
			Count );
	}
#endif

	// Copy memory to destination.
	FMemory::Memcpy( Data, PrecacheBuffer[CURRENT] + (CurrentPos - PrecacheStartPos[CURRENT]), Count );
	// Serialization implicitly increases position in file.
	CurrentPos += Count;
}

/**
 * Returns the current position in the archive as offset in bytes from the beginning.
 *
 * @return	Current position in the archive (offset in bytes from the beginning)
 */
int64 FArchiveAsync::Tell()
{
	return CurrentPos;
}

/**
 * Returns the total size of the archive in bytes.
 *
 * @return total size of the archive in bytes
 */
int64 FArchiveAsync::TotalSize()
{
	return UncompressedFileSize + BulkDataAreaSize;
}

/**
 * Sets the current position.
 *
 * @param InPos	New position (as offset from beginning in bytes)
 */
void FArchiveAsync::Seek( int64 InPos )
{
	check( InPos >= 0 && InPos <= TotalSize() );
	CurrentPos = InPos;
}


/*----------------------------------------------------------------------------
	End.
----------------------------------------------------------------------------*/

