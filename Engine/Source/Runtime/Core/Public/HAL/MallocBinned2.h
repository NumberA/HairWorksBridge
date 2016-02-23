// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Allocators/CachedOSPageAllocator.h"

#define BINNED2_MAX_CACHED_OS_FREES (64)
#if PLATFORM_64BITS
	#define BINNED2_MAX_CACHED_OS_FREES_BYTE_LIMIT (64*1024*1024)
#else
	#define BINNED2_MAX_CACHED_OS_FREES_BYTE_LIMIT (16*1024*1024)
#endif

#if STATS
#	if PLATFORM_64BITS
#		define BINNED2_STAT volatile int64
#	else
#		define BINNED2_STAT volatile int32
#	endif
#endif

//
// Optimized virtual memory allocator.
//
class FMallocBinned2 : public FMalloc
{
	struct Private;

private:

	// Counts.
	enum { POOL_COUNT = 42 };

	/** Maximum allocation for the pooled allocator */
	enum { EXTENDED_PAGE_POOL_ALLOCATION_COUNT = 2 };
	enum { MAX_POOLED_ALLOCATION_SIZE   = 32768+1 };

	// Forward declares.
	struct FFreeMem;
	struct FPoolTable;
	struct FPoolInfo;
	struct PoolHashBucket;

	/** Pool table. */
	struct FPoolTable
	{
		FPoolInfo*			FirstPool;
		FPoolInfo*			ExhaustedPool;
		uint32				BlockSize;
#if STATS
		/** Number of currently active pools */
		uint32				NumActivePools;

		/** Largest number of pools simultaneously active */
		uint32				MaxActivePools;

		/** Number of requests currently active */
		uint32				ActiveRequests;

		/** High watermark of requests simultaneously active */
		uint32				MaxActiveRequests;

		/** Minimum request size (in bytes) */
		uint32				MinRequest;

		/** Maximum request size (in bytes) */
		uint32				MaxRequest;

		/** Total number of requests ever */
		uint64				TotalRequests;

		/** Total waste from all allocs in this table */
		uint64				TotalWaste;
#endif
		FPoolTable()
			: FirstPool(nullptr)
			, ExhaustedPool(nullptr)
			, BlockSize(0)
#if STATS
			, NumActivePools(0)
			, MaxActivePools(0)
			, ActiveRequests(0)
			, MaxActiveRequests(0)
			, MinRequest(0)
			, MaxRequest(0)
			, TotalRequests(0)
			, TotalWaste(0)
#endif
		{

		}
	};

	const uint32 PageSize;

	/** Shift to get the reference from the indirect tables */
	const uint64 PoolBitShift;
	const uint64 IndirectPoolBlockSize;
	const uint64 IndirectPoolBitShift;

	// PageSize dependent constants
	const uint64 MaxHashBuckets;

	/** Shift required to get required hash table key. */
	const uint64 HashKeyShift;

	/** Used to mask off the bits that have been used to lookup the indirect table */
	const uint64 PoolMask;
	const uint64 BinnedSizeLimit;
	const uint64 BinnedOSTableIndex;

	// Variables.
	FPoolTable  PoolTable[POOL_COUNT];
	FPoolTable  PagePoolTable[EXTENDED_PAGE_POOL_ALLOCATION_COUNT];
	FPoolTable* MemSizeToPoolTable[MAX_POOLED_ALLOCATION_SIZE + EXTENDED_PAGE_POOL_ALLOCATION_COUNT];

	PoolHashBucket* HashBuckets;
	PoolHashBucket* HashBucketFreeList;

	TCachedOSPageAllocator<BINNED2_MAX_CACHED_OS_FREES, BINNED2_MAX_CACHED_OS_FREES_BYTE_LIMIT> CachedOSPageAllocator;

#if STATS
	struct FStats
	{
		BINNED2_STAT		OsCurrent;
		BINNED2_STAT		OsPeak;
		BINNED2_STAT		WasteCurrent;
		BINNED2_STAT		WastePeak;
		BINNED2_STAT		UsedCurrent;
		BINNED2_STAT		UsedPeak;
		BINNED2_STAT		CurrentAllocs;
		BINNED2_STAT		TotalAllocs;
		/** OsCurrent - WasteCurrent - UsedCurrent. */
		BINNED2_STAT		SlackCurrent;
		double				MemTime;

		FStats()
			: OsCurrent    (0)
			, OsPeak       (0)
			, WasteCurrent (0)
			, WastePeak    (0)
			, UsedCurrent  (0)
			, UsedPeak     (0)
			, CurrentAllocs(0)
			, TotalAllocs  (0)
			, SlackCurrent (0)
			, MemTime      (0.0)
		{
		}
	};

	FStats Stats;
#endif

public:
	// FMalloc interface.
	// InPageSize - First parameter is page size, all allocs from BinnedAllocFromOS() MUST be aligned to this size
	// AddressLimit - Second parameter is estimate of the range of addresses expected to be returns by BinnedAllocFromOS(). Binned
	// Malloc will adjust its internal structures to make lookups for memory allocations O(1) for this range. 
	// It is ok to go outside this range, lookups will just be a little slower
	FMallocBinned2(uint32 InPageSize, uint64 AddressLimit);

	virtual void InitializeStatsMetadata() override;

	virtual ~FMallocBinned2();

	/**
	 * Returns if the allocator is guaranteed to be thread-safe and therefore
	 * doesn't need a unnecessary thread-safety wrapper around it.
	 */
	virtual bool IsInternallyThreadSafe() const override;

	/** 
	 * Malloc
	 */
	virtual void* Malloc( SIZE_T Size, uint32 Alignment ) override;

	/** 
	 * Realloc
	 */
	virtual void* Realloc( void* Ptr, SIZE_T NewSize, uint32 Alignment ) override;

	/** 
	 * Free
	 */
	virtual void Free( void* Ptr ) override;

	/**
	 * If possible determine the size of the memory allocated at the given address
	 *
	 * @param Original - Pointer to memory we are checking the size of
	 * @param SizeOut - If possible, this value is set to the size of the passed in pointer
	 * @return true if succeeded
	 */
	virtual bool GetAllocationSize(void *Original, SIZE_T &SizeOut) override;

	/**
	 * Validates the allocator's heap
	 */
	virtual bool ValidateHeap() override;

	/** Called once per frame, gathers and sets all memory allocator statistics into the corresponding stats. MUST BE THREAD SAFE. */
	virtual void UpdateStats() override;

	/** Writes allocator stats from the last update into the specified destination. */
	virtual void GetAllocatorStats( FGenericMemoryStats& out_Stats ) override;

	/**
	 * Dumps allocator stats to an output device. Subclasses should override to add additional info
	 *
	 * @param Ar	[in] Output device
	 */
	virtual void DumpAllocatorStats( class FOutputDevice& Ar ) override;

	virtual const TCHAR* GetDescriptiveName() override;
};

#undef BINNED2_STAT
