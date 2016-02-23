// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainerAllocationPolicies.h"
#include "HAL/Platform.h"
#include "Serialization/ArchiveBase.h"
#include "Templates/EnableIf.h"
#include "Templates/Sorting.h"
#include "Templates/UnrealTemplate.h"


#define DEBUG_HEAP 0

#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	#define TARRAY_RANGED_FOR_CHECKS 0
#else
	#define TARRAY_RANGED_FOR_CHECKS 1
#endif


/**
 * Generic iterator which can operate on types that expose the following:
 * - A type called ElementType representing the contained type.
 * - A method IndexType Num() const that returns the number of items in the container.
 * - A method bool IsValidIndex(IndexType index) which returns whether a given index is valid in the container.
 * - A method T& operator\[\](IndexType index) which returns a reference to a contained object by index.
 */
template< typename ContainerType, typename ElementType, typename IndexType>
class TIndexedContainerIterator
{
public:
	TIndexedContainerIterator(ContainerType& InContainer, IndexType StartIndex = 0)
		: Container(InContainer)
		, Index    (StartIndex)
	{
	}

	/** Advances iterator to the next element in the container. */
	TIndexedContainerIterator& operator++()
	{
		++Index;
		return *this;
	}
	TIndexedContainerIterator operator++(int)
	{
		TIndexedContainerIterator Tmp(*this);
		++Index;
		return Tmp;
	}

	/** Moves iterator to the previous element in the container. */
	TIndexedContainerIterator& operator--()
	{
		--Index;
		return *this;
	}
	TIndexedContainerIterator operator--(int)
	{
		TIndexedContainerIterator Tmp(*this);
		--Index;
		return Tmp;
	}

	/** iterator arithmetic support */
	TIndexedContainerIterator& operator+=(int32 Offset)
	{
		Index += Offset;
		return *this;
	}

	TIndexedContainerIterator operator+(int32 Offset) const
	{
		TIndexedContainerIterator Tmp(*this);
		return Tmp += Offset;
	}

	TIndexedContainerIterator& operator-=(int32 Offset)
	{
		return *this += -Offset;
	}

	TIndexedContainerIterator operator-(int32 Offset) const
	{
		TIndexedContainerIterator Tmp(*this);
		return Tmp -= Offset;
	}

	/** @name Element access */
	//@{
	ElementType& operator* () const
	{
		return Container[ Index ];
	}

	ElementType* operator-> () const
	{
		return &Container[ Index ];
	}
	//@}

	/** conversion to "bool" returning true if the iterator has not reached the last element. */
	FORCEINLINE_EXPLICIT_OPERATOR_BOOL() const
	{
		return Container.IsValidIndex(Index);
	}
	/** inverse of the "bool" operator */
	FORCEINLINE bool operator !() const 
	{
		return !(bool)*this;
	}

	/** Returns an index to the current element. */
	IndexType GetIndex() const
	{
		return Index;
	}

	/** Resets the iterator to the first element. */
	void Reset()
	{
		Index = 0;
	}

	FORCEINLINE friend bool operator==(const TIndexedContainerIterator& Lhs, const TIndexedContainerIterator& Rhs) { return &Lhs.Container == &Rhs.Container && Lhs.Index == Rhs.Index; }
	FORCEINLINE friend bool operator!=(const TIndexedContainerIterator& Lhs, const TIndexedContainerIterator& Rhs) { return &Lhs.Container != &Rhs.Container || Lhs.Index != Rhs.Index; }

private:
	ContainerType& Container;
	IndexType      Index;
};

/** operator + */
template <typename ContainerType, typename ElementType, typename IndexType>
FORCEINLINE TIndexedContainerIterator<ContainerType, ElementType, IndexType> operator+(int32 Offset, TIndexedContainerIterator<ContainerType, ElementType, IndexType> RHS)
{
	return RHS + Offset;
}

#if TARRAY_RANGED_FOR_CHECKS

	/**
	 * Pointer-like iterator type for ranged-for loops which checks that the
	 * container hasn't been resized during iteration.
	 */
	template <typename ElementType>
	struct TCheckedPointerIterator
	{
		// This iterator type only supports the minimal functionality needed to support
		// C++ ranged-for syntax.  For example, it does not provide post-increment ++ nor ==.
		//
		// We do add an operator-- to help FString implementation

		explicit TCheckedPointerIterator(const int32& InNum, ElementType* InPtr)
			: Ptr       (InPtr)
			, CurrentNum(InNum)
			, InitialNum(InNum)
		{
		}

		FORCEINLINE ElementType& operator*() const
		{
			return *Ptr;
		}

		FORCEINLINE TCheckedPointerIterator& operator++()
		{
			++Ptr;
			return *this;
		}

		FORCEINLINE TCheckedPointerIterator& operator--()
		{
			--Ptr;
			return *this;
		}

	private:
		ElementType* Ptr;
		const int32& CurrentNum;
		int32        InitialNum;

		friend bool operator!=(const TCheckedPointerIterator& Lhs, const TCheckedPointerIterator& Rhs)
		{
			// We only need to do the check in this operator, because no other operator will be
			// called until after this one returns.
			//
			// Also, we should only need to check one side of this comparison - if the other iterator isn't
			// even from the same array then the compiler has generated bad code.
			ensureMsgf(Lhs.CurrentNum == Lhs.InitialNum, TEXT("Array has changed during ranged-for iteration!"));
			return Lhs.Ptr != Rhs.Ptr;
		}
	};

#endif

template <typename ElementType, typename IteratorType>
struct TDereferencingIterator
{
	explicit TDereferencingIterator(IteratorType InIter)
		: Iter(InIter)
	{
	}

	FORCEINLINE ElementType& operator*() const
	{
		return *(ElementType*)*Iter;
	}

	FORCEINLINE TDereferencingIterator& operator++()
	{
		++Iter;
		return *this;
	}

private:
	IteratorType Iter;

	FORCEINLINE friend bool operator!=(const TDereferencingIterator& Lhs, const TDereferencingIterator& Rhs)
	{
		return Lhs.Iter != Rhs.Iter;
	}
};

/**
 * Base dynamic array.
 * An untyped data array; mirrors a TArray's members, but doesn't need an exact C++ type for its elements.
 **/
class FScriptArray : protected FHeapAllocator::ForAnyElementType
{
public:
	void* GetData()
	{
		return this->GetAllocation();
	}
	const void* GetData() const
	{
		return this->GetAllocation();
	}
	bool IsValidIndex( int32 i ) const
	{
		return i>=0 && i<ArrayNum;
	}
	FORCEINLINE int32 Num() const
	{
		checkSlow(ArrayNum>=0);
		checkSlow(ArrayMax>=ArrayNum);
		return ArrayNum;
	}
	void InsertZeroed( int32 Index, int32 Count, int32 NumBytesPerElement )
	{
		Insert( Index, Count, NumBytesPerElement );
		FMemory::Memzero( (uint8*)this->GetAllocation()+Index*NumBytesPerElement, Count*NumBytesPerElement );
	}
	void Insert( int32 Index, int32 Count, int32 NumBytesPerElement )
	{
		check(Count>=0);
		check(ArrayNum>=0);
		check(ArrayMax>=ArrayNum);
		check(Index>=0);
		check(Index<=ArrayNum);

		const int32 OldNum = ArrayNum;
		if( (ArrayNum+=Count)>ArrayMax )
		{
			ArrayMax = this->CalculateSlack( ArrayNum, ArrayMax, NumBytesPerElement );
			this->ResizeAllocation(OldNum,ArrayMax,NumBytesPerElement);
		}
		FMemory::Memmove
		(
			(uint8*)this->GetAllocation() + (Index+Count )*NumBytesPerElement,
			(uint8*)this->GetAllocation() + (Index       )*NumBytesPerElement,
			                                               (OldNum-Index)*NumBytesPerElement
		);
	}
	int32 Add( int32 Count, int32 NumBytesPerElement )
	{
		check(Count>=0);
		checkSlow(ArrayNum>=0);
		checkSlow(ArrayMax>=ArrayNum);

		const int32 OldNum = ArrayNum;
		if( (ArrayNum+=Count)>ArrayMax )
		{
			ArrayMax = this->CalculateSlack( ArrayNum, ArrayMax, NumBytesPerElement );
			this->ResizeAllocation(OldNum,ArrayMax,NumBytesPerElement);
		}

		return OldNum;
	}
	int32 AddZeroed( int32 Count, int32 NumBytesPerElement )
	{
		const int32 Index = Add( Count, NumBytesPerElement );
		FMemory::Memzero( (uint8*)this->GetAllocation()+Index*NumBytesPerElement, Count*NumBytesPerElement );
		return Index;
	}
	void Shrink( int32 NumBytesPerElement )
	{
		checkSlow(ArrayNum>=0);
		checkSlow(ArrayMax>=ArrayNum);
		if( ArrayMax != ArrayNum )
		{
			ArrayMax = ArrayNum;
			this->ResizeAllocation(ArrayNum,ArrayMax,NumBytesPerElement);
		}
	}
	void Empty( int32 Slack, int32 NumBytesPerElement )
	{
		checkSlow(Slack>=0);
		ArrayNum = 0;
		// only reallocate if we need to, I don't trust realloc to the same size to work
		if (ArrayMax != Slack)
		{
			ArrayMax = Slack;
			this->ResizeAllocation(0,ArrayMax,NumBytesPerElement);
		}
	}
	void SwapMemory(int32 A, int32 B, int32 NumBytesPerElement )
	{
		FMemory::Memswap(
			(uint8*)this->GetAllocation()+(NumBytesPerElement*A),
			(uint8*)this->GetAllocation()+(NumBytesPerElement*B),
			NumBytesPerElement
			);
	}
	FScriptArray()
	:   ArrayNum( 0 )
	,	ArrayMax( 0 )
	{
	}
	void CountBytes( FArchive& Ar, int32 NumBytesPerElement  )
	{
		Ar.CountBytes( ArrayNum*NumBytesPerElement, ArrayMax*NumBytesPerElement );
	}
	/**
	 * Returns the amount of slack in this array in elements.
	 */
	int32 GetSlack() const
	{
		return ArrayMax - ArrayNum;
	}
		
	void Remove( int32 Index, int32 Count, int32 NumBytesPerElement  )
	{
		checkSlow(Count >= 0);
		checkSlow(Index >= 0); 
		checkSlow(Index <= ArrayNum);
		checkSlow(Index + Count <= ArrayNum);

		// Skip memmove in the common case that there is nothing to move.
		int32 NumToMove = ArrayNum - Index - Count;
		if( NumToMove )
		{
			FMemory::Memmove
			(
				(uint8*)this->GetAllocation() + (Index      ) * NumBytesPerElement,
				(uint8*)this->GetAllocation() + (Index+Count) * NumBytesPerElement,
				NumToMove * NumBytesPerElement
			);
		}
		ArrayNum -= Count;
		
		const int32 NewArrayMax = this->CalculateSlack(ArrayNum,ArrayMax,NumBytesPerElement);
		if(NewArrayMax != ArrayMax)
		{
			ArrayMax = NewArrayMax;
			this->ResizeAllocation(ArrayNum,ArrayMax,NumBytesPerElement);
		}
		checkSlow(ArrayNum >= 0);
		checkSlow(ArrayMax >= ArrayNum);
	}

protected:

	FScriptArray( int32 InNum, int32 NumBytesPerElement  )
	:   ArrayNum( InNum )
	,	ArrayMax( InNum )

	{
		this->ResizeAllocation(0,ArrayMax,NumBytesPerElement);
	}
	int32	  ArrayNum;
	int32	  ArrayMax;

public:
	// These should really be private, because they shouldn't be called, but there's a bunch of code
	// that needs to be fixed first.
	FScriptArray(const FScriptArray&) { check(false); }
	void operator=(const FScriptArray&) { check(false); }
};

template<> struct TIsZeroConstructType<FScriptArray> { enum { Value = true }; };


/**
 * TReversePredicateWrapper class used by implicit heaps. 
 * This is similar to TDereferenceWrapper from Sorting.h except it reverses the comparison at the same time
 */
template <typename ElementType, typename PREDICATE_CLASS>
class TReversePredicateWrapper
{
	const PREDICATE_CLASS& Predicate;
public:
	TReversePredicateWrapper( const PREDICATE_CLASS& InPredicate )
		: Predicate( InPredicate )
	{}

	FORCEINLINE bool operator()( ElementType& A, ElementType& B ) const { return Predicate( B, A ); }
	FORCEINLINE bool operator()( const ElementType& A, const ElementType& B ) const { return Predicate( B, A ); }
};

/**
 * Partially specialized version of the above.
 */
template <typename ElementType, typename PREDICATE_CLASS>
class TReversePredicateWrapper<ElementType*, PREDICATE_CLASS>
{
	const PREDICATE_CLASS& Predicate;
public:
	TReversePredicateWrapper( const PREDICATE_CLASS& InPredicate )
		: Predicate( InPredicate )
	{}

	FORCEINLINE bool operator()( ElementType* A, ElementType* B ) const 
	{
		check( A != nullptr );
		check( B != nullptr );
		return Predicate( *B, *A ); 
	}
	FORCEINLINE bool operator()( const ElementType* A, const ElementType* B ) const 
	{
		check( A != nullptr );
		check( B != nullptr );
		return Predicate( *B, *A ); 
	}
};

namespace UE4Array_Private
{
	template <typename FromArrayType, typename ToArrayType>
	struct TCanMoveTArrayPointersBetweenArrayTypes
	{
		typedef typename FromArrayType::Allocator   FromAllocatorType;
		typedef typename ToArrayType  ::Allocator   ToAllocatorType;
		typedef typename FromArrayType::ElementType FromElementType;
		typedef typename ToArrayType  ::ElementType ToElementType;

		enum
		{
			Value =
				TAreTypesEqual<FromAllocatorType, ToAllocatorType>::Value && // Allocators must be equal
				TContainerTraits<FromArrayType>::MoveWillEmptyContainer &&   // A move must be allowed to leave the source array empty
				(
					TAreTypesEqual         <ToElementType, FromElementType>::Value || // The element type of the container must be the same, or...
					TIsBitwiseConstructible<ToElementType, FromElementType>::Value    // ... the element type of the source container must be bitwise constructible from the element type in the destination container
				)
		};
	};
}

/**
 * Templated dynamic array
 *
 * A dynamically sized array of typed elements.  Makes the assumption that your elements are relocate-able; 
 * i.e. that they can be transparently moved to new memory without a copy constructor.  The main implication 
 * is that pointers to elements in the TArray may be invalidated by adding or removing other elements to the array. 
 * Removal of elements is O(N) and invalidates the indices of subsequent elements.
 *
 * Caution: as noted below some methods are not safe for element types that require constructors.
 *
 **/
template<typename InElementType, typename InAllocator>
class TArray
{
	template <typename OtherInElementType, typename OtherAllocator>
	friend class TArray;

public:
	typedef InElementType ElementType;
	typedef InAllocator   Allocator;

	/**
	 * Constructor, initializes element number counters.
	 */
	TArray()
		: ArrayNum(0)
		, ArrayMax(0)
	{}

	/**
	 * Copy constructor with changed allocator. Use the common routine to perform the copy.
	 *
	 * @param Other The source array to copy.
	 */
	template <typename OtherElementType, typename OtherAllocator>
	explicit TArray(const TArray<OtherElementType, OtherAllocator>& Other)
	{
		CopyToEmpty(Other, 0, 0);
	}

	/**
	 * Copy constructor. Use the common routine to perform the copy.
	 *
	 * @param Other The source array to copy.
	 */
	TArray(const TArray& Other)
	{
		CopyToEmpty(Other, 0, 0);
	}

	/**
	 * Copy constructor. Use the common routine to perform the copy.
	 *
	 * @param Other The source array to copy.
	 * @param ExtraSlack Tells how much extra memory should be preallocated
	 *                   at the end of the array in the number of elements.
	 */
	TArray(const TArray& Other, int32 ExtraSlack)
	{
		CopyToEmpty(Other, 0, ExtraSlack);
	}

	/**
	 * Assignment operator. First deletes all currently contained elements
	 * and then copies from other array.
	 *
	 * Allocator changing version.
	 *
	 * @param Other The source array to assign from.
	 */
	template<typename OtherAllocator>
	TArray& operator=(const TArray<ElementType, OtherAllocator>& Other)
	{
		DestructItems(GetData(), ArrayNum);
		CopyToEmpty(Other, ArrayMax, 0);
		return *this;
	}

	/**
	 * Assignment operator. First deletes all currently contained elements
	 * and then copies from other array.
	 *
	 * @param Other The source array to assign from.
	 */
	TArray& operator=(const TArray& Other)
	{
		if (this != &Other)
		{
			DestructItems(GetData(), ArrayNum);
			CopyToEmpty(Other, ArrayMax, 0);
		}
		return *this;
	}

private:
	/**
	 * Moves or copies array. Depends on the array type traits.
	 *
	 * This override moves.
	 *
	 * @param ToArray Array to move into.
	 * @param FromArray Array to move from.
	 */
	template <typename FromArrayType, typename ToArrayType>
	static FORCEINLINE typename TEnableIf<UE4Array_Private::TCanMoveTArrayPointersBetweenArrayTypes<FromArrayType, ToArrayType>::Value>::Type MoveOrCopy(ToArrayType& ToArray, FromArrayType& FromArray, int32 PrevMax)
	{
		ToArray.AllocatorInstance.MoveToEmpty(FromArray.AllocatorInstance);

		ToArray  .ArrayNum = FromArray.ArrayNum;
		ToArray  .ArrayMax = FromArray.ArrayMax;
		FromArray.ArrayNum = 0;
		FromArray.ArrayMax = 0;
	}

	/**
	 * Moves or copies array. Depends on the array type traits.
	 *
	 * This override copies.
	 *
	 * @param ToArray Array to move into.
	 * @param FromArray Array to move from.
	 * @param ExtraSlack Tells how much extra memory should be preallocated
	 *                   at the end of the array in the number of elements.
	 */
	template <typename FromArrayType, typename ToArrayType>
	static FORCEINLINE typename TEnableIf<!UE4Array_Private::TCanMoveTArrayPointersBetweenArrayTypes<FromArrayType, ToArrayType>::Value>::Type MoveOrCopy(ToArrayType& ToArray, FromArrayType& FromArray, int32 PrevMax)
	{
		ToArray.CopyToEmpty(FromArray, PrevMax, 0);
	}

	/**
	 * Moves or copies array. Depends on the array type traits.
	 *
	 * This override moves.
	 *
	 * @param ToArray Array to move into.
	 * @param FromArray Array to move from.
	 * @param ExtraSlack Tells how much extra memory should be preallocated
	 *                   at the end of the array in the number of elements.
	 */
	template <typename FromArrayType, typename ToArrayType>
	static FORCEINLINE typename TEnableIf<UE4Array_Private::TCanMoveTArrayPointersBetweenArrayTypes<FromArrayType, ToArrayType>::Value>::Type MoveOrCopyWithSlack(ToArrayType& ToArray, FromArrayType& FromArray, int32 PrevMax, int32 ExtraSlack)
	{
		MoveOrCopy(ToArray, FromArray, PrevMax);

		ToArray.Reserve(ToArray.ArrayNum + ExtraSlack);
	}

	/**
	 * Moves or copies array. Depends on the array type traits.
	 *
	 * This override copies.
	 *
	 * @param ToArray Array to move into.
	 * @param FromArray Array to move from.
	 * @param ExtraSlack Tells how much extra memory should be preallocated
	 *                   at the end of the array in the number of elements.
	 */
	template <typename FromArrayType, typename ToArrayType>
	static FORCEINLINE typename TEnableIf<!UE4Array_Private::TCanMoveTArrayPointersBetweenArrayTypes<FromArrayType, ToArrayType>::Value>::Type MoveOrCopyWithSlack(ToArrayType& ToArray, FromArrayType& FromArray, int32 PrevMax, int32 ExtraSlack)
	{
		ToArray.CopyToEmpty(FromArray, PrevMax, ExtraSlack);
	}

public:
	/**
	 * Move constructor.
	 *
	 * @param Other Array to move from.
	 */
	FORCEINLINE TArray(TArray&& Other)
	{
		MoveOrCopy(*this, Other, 0);
	}

	/**
	 * Move constructor.
	 *
	 * @param Other Array to move from.
	 */
	template <typename OtherElementType, typename OtherAllocator>
	FORCEINLINE explicit TArray(TArray<OtherElementType, OtherAllocator>&& Other)
	{
		MoveOrCopy(*this, Other, 0);
	}

	/**
	 * Move constructor.
	 *
	 * @param Other Array to move from.
	 * @param ExtraSlack Tells how much extra memory should be preallocated
	 *                   at the end of the array in the number of elements.
	 */
	template <typename OtherElementType>
	TArray(TArray<OtherElementType, Allocator>&& Other, int32 ExtraSlack)
	{
		// We don't implement move semantics for general OtherAllocators, as there's no way
		// to tell if they're compatible with the current one.  Probably going to be a pretty
		// rare requirement anyway.

		MoveOrCopyWithSlack(*this, Other, 0, ExtraSlack);
	}

	/**
	 * Move assignment operator.
	 *
	 * @param Other Array to assign and move from.
	 */
	TArray& operator=(TArray&& Other)
	{
		if (this != &Other)
		{
			DestructItems(GetData(), ArrayNum);
			MoveOrCopy(*this, Other, ArrayMax);
		}
		return *this;
	}

	/**
	 * Destructor.
	 */
	~TArray()
	{
		DestructItems(GetData(), ArrayNum);

		#if defined(_MSC_VER) && !defined(__clang__)	// Relies on MSVC-specific lazy template instantiation to support arrays of incomplete types
			// ensure that DebugGet gets instantiated.
			//@todo it would be nice if we had a cleaner solution for DebugGet
			volatile const ElementType* Dummy = &DebugGet(0);
		#endif
	}

	/**
	 * Helper function for returning a typed pointer to the first array entry.
	 *
	 * @returns Pointer to first array entry or nullptr if ArrayMax == 0.
	 */
	DEPRECATED(4.6, "This function is deprecated as it does the same as GetData(). Please use GetData() instead.")
	FORCEINLINE ElementType* GetTypedData()
	{
		return (ElementType*)AllocatorInstance.GetAllocation();
	}

	/**
	 * Helper function for returning a typed pointer to the first array entry.
	 *
	 * @returns Pointer to first array entry or nullptr if ArrayMax == 0.
	 */
	FORCEINLINE ElementType* GetData()
	{
		return (ElementType*)AllocatorInstance.GetAllocation();
	}

	/**
	 * Helper function for returning a typed pointer to the first array entry.
	 *
	 * @returns Pointer to first array entry or nullptr if ArrayMax == 0.
	 */
	DEPRECATED(4.6, "This function is deprecated as it does the same as GetData(). Please use GetData() instead.")
	FORCEINLINE const ElementType* GetTypedData() const
	{
		return (const ElementType*)AllocatorInstance.GetAllocation();
	}

	/**
	 * Helper function for returning a typed pointer to the first array entry.
	 *
	 * @returns Pointer to first array entry or nullptr if ArrayMax == 0.
	 */
	FORCEINLINE const ElementType* GetData() const
	{
		return (const ElementType*)AllocatorInstance.GetAllocation();
	}

	/** 
	 * Helper function returning the size of the inner type.
	 *
	 * @returns Size in bytes of array type.
	 */
	FORCEINLINE uint32 GetTypeSize() const
	{
		return sizeof(ElementType);
	}

	/** 
	 * Helper function to return the amount of memory allocated by this
	 * container.
	 *
	 * @returns Number of bytes allocated by this container.
	 */
	FORCEINLINE uint32 GetAllocatedSize(void) const
	{
		return AllocatorInstance.GetAllocatedSize(ArrayMax, sizeof(ElementType));
	}

	/**
	 * Returns the amount of slack in this array in elements.
	 */
	int32 GetSlack() const
	{
		return ArrayMax - ArrayNum;
	}

	/**
	 * Checks array invariants: if array size is greater than zero and less
	 * than maximum.
	 */
	FORCEINLINE void CheckInvariants() const
	{
		checkSlow((ArrayNum >= 0) & (ArrayMax >= ArrayNum)); // & for one branch
	}

	/**
	 * Checks if index is in array range.
	 *
	 * @param Index Index to check.
	 */
	FORCEINLINE void RangeCheck(int32 Index) const
	{
		CheckInvariants();

		// Template property, branch will be optimized out
		if (Allocator::RequireRangeCheck)
		{
			checkf((Index >= 0) & (Index < ArrayNum),TEXT("Array index out of bounds: %i from an array of size %i"),Index,ArrayNum); // & for one branch
		}
	}

	/**
	 * Tests if index is valid, i.e. than or equal to zero, and less than the number of elements in the array.
	 *
	 * @param Index Index to test.
	 *
	 * @returns True if index is valid. False otherwise.
	 */
	FORCEINLINE bool IsValidIndex(int32 Index) const
	{
		return Index >= 0 && Index < ArrayNum;
	}

	/**
	 * Returns number of elements in array.
	 *
	 * @returns Number of elements in array.
	 */
	FORCEINLINE int32 Num() const
	{
		return ArrayNum;
	}

	/**
	 * Returns maximum number of elements in array.
	 *
	 * @returns Maximum number of elements in array.
	 */
	FORCEINLINE int32 Max() const
	{
		return ArrayMax;
	}

	/**
	 * Array bracket operator. Returns reference to element at give index.
	 *
	 * @returns Reference to indexed element.
	 */
	FORCEINLINE ElementType& operator[](int32 Index)
	{
		RangeCheck(Index);
		return GetData()[Index];
	}

	/**
	 * Array bracket operator. Returns reference to element at give index.
	 *
	 * Const version of the above.
	 *
	 * @returns Reference to indexed element.
	 */
	FORCEINLINE const ElementType& operator[](int32 Index) const
	{
		RangeCheck(Index);
		return GetData()[Index];
	}

	/**
	 * Pops element from the array.
	 *
	 * @param bAllowShrinking If this call allows shrinking of the array during
	 *                        element remove.
	 *
	 * @returns Popped element.
	 */
	ElementType Pop(bool bAllowShrinking = true)
	{
		RangeCheck(0);
		ElementType Result = MoveTemp(GetData()[ArrayNum - 1]);
		RemoveAt(ArrayNum - 1, 1, bAllowShrinking);
		return Result;
	}

	/**
	 * Pushes element into the array.
	 *
	 * @param Item Item to push.
	 */
	void Push(ElementType&& Item)
	{
		Add(MoveTemp(Item));
	}

	/**
	 * Pushes element into the array.
	 *
	 * Const ref version of the above.
	 *
	 * @param Item Item to push.
	 */
	void Push(const ElementType& Item)
	{
		Add(Item);
	}

	/**
	 * Returns the top element, i.e. the last one.
	 *
	 * @returns Reference to the top element.
	 */
	ElementType& Top()
	{
		return Last();
	}

	/**
	 * Returns the top element, i.e. the last one.
	 *
	 * Const version of the above.
	 *
	 * @returns Reference to the top element.
	 */
	const ElementType& Top() const
	{
		return Last();
	}

	/**
	 * Returns n-th last element from the array.
	 *
	 * @param IndexFromTheEnd (Optional) Index from the end of array.
	 *                        Default is 0.
	 *
	 * @returns Reference to n-th last element from the array.
	 */
	ElementType& Last(int32 IndexFromTheEnd = 0)
	{
		RangeCheck(ArrayNum - IndexFromTheEnd - 1);
		return GetData()[ArrayNum - IndexFromTheEnd - 1];
	}

	/**
	 * Returns n-th last element from the array.
	 *
	 * Const version of the above.
	 *
	 * @param IndexFromTheEnd (Optional) Index from the end of array.
	 *                        Default is 0.
	 *
	 * @returns Reference to n-th last element from the array.
	 */
	const ElementType& Last(int32 IndexFromTheEnd = 0) const
	{
		RangeCheck(ArrayNum - IndexFromTheEnd - 1);
		return GetData()[ArrayNum - IndexFromTheEnd - 1];
	}

	/**
	 * Shrinks the array's used memory to smallest possible to store elements
	 * currently in it.
	 */
	void Shrink()
	{
		CheckInvariants();
		if (ArrayMax != ArrayNum)
		{
			ArrayMax = ArrayNum;
			AllocatorInstance.ResizeAllocation(ArrayNum, ArrayMax, sizeof(ElementType));
		}
	}

	/**
	 * Finds element within the array.
	 *
	 * @param Item Item to look for.
	 * @param Index Output parameter. Found index.
	 *
	 * @returns True if found. False otherwise.
	 */
	FORCEINLINE bool Find(const ElementType& Item, int32& Index) const
	{
		Index = this->Find(Item);
		return Index != INDEX_NONE;
	}

	/**
	 * Finds element within the array.
	 *
	 * @param Item Item to look for.
	 *
	 * @returns Index of the found element. INDEX_NONE otherwise.
	 */
	int32 Find(const ElementType& Item) const
	{
		const ElementType* RESTRICT Start = GetData();
		for (const ElementType* RESTRICT Data = Start, *RESTRICT DataEnd = Data + ArrayNum; Data != DataEnd; ++Data)
		{
			if (*Data == Item)
			{
				return static_cast<int32>(Data - Start);
			}
		}
		return INDEX_NONE;
	}

	/**
	 * Finds element within the array starting from the end.
	 *
	 * @param Item Item to look for.
	 * @param Index Output parameter. Found index.
	 *
	 * @returns True if found. False otherwise.
	 */
	FORCEINLINE bool FindLast(const ElementType& Item, int32& Index) const
	{
		Index = this->FindLast(Item);
		return Index != INDEX_NONE;
	}

	/**
	 * Finds element within the array starting from the end.
	 *
	 * @param Item Item to look for.
	 *
	 * @returns Index of the found element. INDEX_NONE otherwise.
	 */
	int32 FindLast(const ElementType& Item) const
	{
		for (const ElementType* RESTRICT Start = GetData(), *RESTRICT Data = Start + ArrayNum; Data != Start; )
		{
			--Data;
			if (*Data == Item)
			{
				return static_cast<int32>(Data - Start);
			}
		}
		return INDEX_NONE;
	}

	/**
	 * Finds element within the array starting from StartIndex and going backwards. Uses predicate to match element.
	 *
	 * @param Pred Predicate taking array element and returns true if element matches search criteria, false otherwise.
	 * @param StartIndex Index of element from which to start searching.
	 *
	 * @returns Index of the found element. INDEX_NONE otherwise.
	 */
	template <typename Predicate>
	int32 FindLastByPredicate(Predicate Pred, int32 StartIndex) const
	{
		check(StartIndex >= 0 && StartIndex <= this->Num());
		for (const ElementType* RESTRICT Start = GetData(), *RESTRICT Data = Start + StartIndex; Data != Start; )
		{
			--Data;
			if (Pred(*Data))
			{
				return static_cast<int32>(Data - Start);
			}
		}
		return INDEX_NONE;
	}

	/**
	* Finds element within the array starting from the end. Uses predicate to match element.
	*
	* @param Pred Predicate taking array element and returns true if element matches search criteria, false otherwise.
	*
	* @returns Index of the found element. INDEX_NONE otherwise.
	*/
	template <typename Predicate>
	FORCEINLINE int32 FindLastByPredicate(Predicate Pred) const
	{
		return FindLastByPredicate(Pred, ArrayNum);
	}

	/**
	 * Finds element within the array that fulfills given predicate.
	 *
	 * @param MatchFunctorType A functor object with implemented
	 *                         bool Matches(const ElementType& Element) method
	 *                         that returns true if given element is the one we
	 *                         look for.
	 *
	 * @returns Index of the found element. INDEX_NONE otherwise.
	 */
	template<typename MatchFunctorType>
	DEPRECATED(4.6, "This function is deprecated as it does the same as IndexOfByPredicate(). Please use IndexOfByPredicate() instead.")
	int32 FindMatch(const MatchFunctorType& Matcher) const
	{
		const ElementType* const RESTRICT DataEnd = GetData() + ArrayNum;
		for (const ElementType* RESTRICT Data = GetData();
			Data < DataEnd;
			Data++
			)
		{
			if (Matcher.Matches(*Data))
			{
				return static_cast<int32>(Data - GetData());
			}
		}
		return INDEX_NONE;
	}

	/**
	 * Finds an item by key (assuming the ElementType overloads operator== for
	 * the comparison).
	 *
	 * @param Key The key to search by.
	 *
	 * @returns Index to the first matching element, or INDEX_NONE if none is
	 *          found.
	 */
	template <typename KeyType>
	int32 IndexOfByKey(const KeyType& Key) const
	{
		const ElementType* RESTRICT Start = GetData();
		for (const ElementType* RESTRICT Data = Start, *RESTRICT DataEnd = Start + ArrayNum; Data != DataEnd; ++Data)
		{
			if (*Data == Key)
			{
				return static_cast<int32>(Data - Start);
			}
		}
		return INDEX_NONE;
	}

	/**
	 * Finds an item by predicate.
	 *
	 * @param Pred The predicate to match.
	 *
	 * @returns Index to the first matching element, or INDEX_NONE if none is
	 *          found.
	 */
	template <typename Predicate>
	int32 IndexOfByPredicate(Predicate Pred) const
	{
		const ElementType* RESTRICT Start = GetData();
		for (const ElementType* RESTRICT Data = Start, *RESTRICT DataEnd = Start + ArrayNum; Data != DataEnd; ++Data)
		{
			if (Pred(*Data))
			{
				return static_cast<int32>(Data - Start);
			}
		}
		return INDEX_NONE;
	}

	/**
	 * Finds an item by key (assuming the ElementType overloads operator== for
	 * the comparison).
	 *
	 * @param Key The key to search by.
	 *
	 * @returns Pointer to the first matching element, or nullptr if none is found.
	 */
	template <typename KeyType>
	FORCEINLINE const ElementType* FindByKey(const KeyType& Key) const
	{
		return const_cast<TArray*>(this)->FindByKey(Key);
	}

	/**
	 * Finds an item by key (assuming the ElementType overloads operator== for
	 * the comparison). Time Complexity: O(n), starts iteration from the beginning so better performance if Key is in the front
	 *
	 * @param Key The key to search by.
	 *
	 * @returns Pointer to the first matching element, or nullptr if none is found.
	 */
	template <typename KeyType>
	ElementType* FindByKey(const KeyType& Key)
	{
		for (ElementType* RESTRICT Data = GetData(), *RESTRICT DataEnd = Data + ArrayNum; Data != DataEnd; ++Data)
		{
			if (*Data == Key)
			{
				return Data;
			}
		}

		return nullptr;
	}

	/**
	 * Finds an element which matches a predicate functor.
	 *
	 * @param Pred The functor to apply to each element.
	 *
	 * @returns Pointer to the first element for which the predicate returns
	 *          true, or nullptr if none is found.
	 */
	template <typename Predicate>
	FORCEINLINE const ElementType* FindByPredicate(Predicate Pred) const
	{
		return const_cast<TArray*>(this)->FindByPredicate(Pred);
	}

	/**
	 * Finds an element which matches a predicate functor.
	 *
	 * @param Pred The functor to apply to each element.
	 *
	 * @return Pointer to the first element for which the predicate returns
	 *         true, or nullptr if none is found.
	 */
	template <typename Predicate>
	ElementType* FindByPredicate(Predicate Pred)
	{
		for (ElementType* RESTRICT Data = GetData(), *RESTRICT DataEnd = Data + ArrayNum; Data != DataEnd; ++Data)
		{
			if (Pred(*Data))
			{
				return Data;
			}
		}

		return nullptr;
	}

	/**
	 * Filters the elements in the array based on a predicate functor.
	 *
	 * @param Pred The functor to apply to each element.
	 *
	 * @returns TArray with the same type as this object which contains
	 *          the subset of elements for which the functor returns true.
	 */
	template <typename Predicate>
	TArray<ElementType> FilterByPredicate(Predicate Pred) const
	{
		TArray<ElementType> FilterResults;
		for (const ElementType* RESTRICT Data = GetData(), *RESTRICT DataEnd = Data + ArrayNum; Data != DataEnd; ++Data)
		{
			if (Pred(*Data))
			{
				FilterResults.Add(*Data);
			}
		}
		return FilterResults;
	}

	/**
	 * Checks if this array contains the element.
	 *
	 * @returns	True if found. False otherwise.
	 */
	template <typename ComparisonType>
	bool Contains(const ComparisonType& Item) const
	{
		for (const ElementType* RESTRICT Data = GetData(), *RESTRICT DataEnd = Data + ArrayNum; Data != DataEnd; ++Data)
		{
			if (*Data == Item)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Checks if this array contains element for which the predicate is true.
	 *
	 * @param Predicate to use
	 *
	 * @returns	True if found. False otherwise.
	 */
	template <typename Predicate>
	FORCEINLINE bool ContainsByPredicate(Predicate Pred) const
	{
		return FindByPredicate(Pred) != nullptr;
	}

	/**
	 * Equality operator.
	 *
	 * @param OtherArray Array to compare.
	 *
	 * @returns True if this array is the same as OtherArray. False otherwise.
	 */
	bool operator==(const TArray& OtherArray) const
	{
		int32 Count = Num();

		return Count == OtherArray.Num() && CompareItems(GetData(), OtherArray.GetData(), Count);
	}

	/**
	 * Inequality operator.
	 *
	 * @param OtherArray Array to compare.
	 *
	 * @returns True if this array is NOT the same as OtherArray. False otherwise.
	 */
	bool operator!=(const TArray& OtherArray) const
	{
		return !(*this == OtherArray);
	}

	/**
	 * Serialization operator.
	 *
	 * @param Ar Archive to serialize the array with.
	 * @param A Array to serialize.
	 *
	 * @returns Passing the given archive.
	 */
	friend FArchive& operator<<(FArchive& Ar, TArray& A)
	{
		A.CountBytes(Ar);
		if (sizeof(ElementType) == 1)
		{
			// Serialize simple bytes which require no construction or destruction.
			Ar << A.ArrayNum;
			check(A.ArrayNum >= 0);
			if (Ar.IsLoading())
			{
				A.ArrayMax = A.ArrayNum;
				A.AllocatorInstance.ResizeAllocation(0, A.ArrayMax, sizeof(ElementType));
			}
			Ar.Serialize(A.GetData(), A.Num());
		}
		else if (Ar.IsLoading())
		{
			// Load array.
			int32 NewNum;
			Ar << NewNum;
			A.Empty(NewNum);
			for (int32 i = 0; i < NewNum; i++)
			{
				Ar << *::new(A)ElementType;
			}
		}
		else
		{
			// Save array.
			Ar << A.ArrayNum;
			for (int32 i = 0; i < A.ArrayNum; i++)
			{
				Ar << A[i];
			}
		}
		return Ar;
	}

	/**
	 * Bulk serialize array as a single memory blob when loading. Uses regular serialization code for saving
	 * and doesn't serialize at all otherwise (e.g. transient, garbage collection, ...).
	 * 
	 * Requirements:
	 *   - T's << operator needs to serialize ALL member variables in the SAME order they are layed out in memory.
	 *   - T's << operator can NOT perform any fixup operations. This limitation can be lifted by manually copying
	 *     the code after the BulkSerialize call.
	 *   - T can NOT contain any member variables requiring constructor calls or pointers
	 *   - sizeof(ElementType) must be equal to the sum of sizes of it's member variables.
	 *        - e.g. use pragma pack (push,1)/ (pop) to ensure alignment
	 *        - match up uint8/ WORDs so everything always end up being properly aligned
	 *   - Code can not rely on serialization of T if neither ArIsLoading nor ArIsSaving is true.
	 *   - Can only be called platforms that either have the same endianness as the one the content was saved with
	 *     or had the endian conversion occur in a cooking process like e.g. for consoles.
	 *
	 * Notes:
	 *   - it is safe to call BulkSerialize on TTransArrays
	 *
	 * IMPORTANT:
	 *   - This is Overridden in XeD3dResourceArray.h Please make certain changes are propogated accordingly
	 *
	 * @param Ar	FArchive to bulk serialize this TArray to/from
	 */
	void BulkSerialize(FArchive& Ar, bool bForcePerElementSerialization = false)
	{
		int32 ElementSize = sizeof(ElementType);
		// Serialize element size to detect mismatch across platforms.
		int32 SerializedElementSize = ElementSize;
		Ar << SerializedElementSize;

		if (bForcePerElementSerialization
			|| (Ar.IsSaving()			// if we are saving, we always do the ordinary serialize as a way to make sure it matches up with bulk serialization
			&& !Ar.IsCooking()			// but cooking and transacting is performance critical, so we skip that
			&& !Ar.IsTransacting())		
			|| Ar.IsByteSwapping()		// if we are byteswapping, we need to do that per-element
			)
		{
			Ar << *this;
		}
		else
		{
			CountBytes(Ar);
			if (Ar.IsLoading())
			{
				// Basic sanity checking to ensure that sizes match.
				checkf(SerializedElementSize == 0 || SerializedElementSize == ElementSize, TEXT("Expected %i, Got: %i"), ElementSize, SerializedElementSize);
				// Serialize the number of elements, block allocate the right amount of memory and deserialize
				// the data as a giant memory blob in a single call to Serialize. Please see the function header
				// for detailed documentation on limitations and implications.
				int32 NewArrayNum;
				Ar << NewArrayNum;
				Empty(NewArrayNum);
				AddUninitialized(NewArrayNum);
				Ar.Serialize(GetData(), NewArrayNum * SerializedElementSize);
			}
			else if (Ar.IsSaving())
			{
				int32 ArrayCount = Num();
				Ar << ArrayCount;
				Ar.Serialize(GetData(), ArrayCount * SerializedElementSize);
			}
		}
	}

	/**
	 * Count bytes needed to serialize this array.
	 *
	 * @param Ar Archive to count for.
	 */
	void CountBytes(FArchive& Ar)
	{
		Ar.CountBytes(ArrayNum*sizeof(ElementType), ArrayMax*sizeof(ElementType));
	}

	/**
	 * Adds a given number of uninitialized elements into the array.
	 *
	 * Caution, AddUninitialized() will create elements without calling
	 * the constructor and this is not appropriate for element types that
	 * require a constructor to function properly.
	 *
	 * @param Count Number of elements to add.
	 *
	 * @returns Number of elements in array before addition.
	 */
	int32 AddUninitialized(int32 Count = 1)
	{
		CheckInvariants();
		checkSlow(Count >= 0);

		const int32 OldNum = ArrayNum;
		if ((ArrayNum += Count) > ArrayMax)
		{
			ArrayMax = AllocatorInstance.CalculateSlack(ArrayNum, ArrayMax, sizeof(ElementType));
			AllocatorInstance.ResizeAllocation(OldNum, ArrayMax, sizeof(ElementType));
		}

		return OldNum;
	}

	/**
	 * Inserts a given number of uninitialized elements into the array at given
	 * location.
	 *
	 * Caution, InsertUninitialized() will create elements without calling the
	 * constructor and this is not appropriate for element types that require
	 * a constructor to function properly.
	 *
	 * @param Index Tells where to insert the new elements.
	 * @param Count Number of elements to add.
	 */
	void InsertUninitialized(int32 Index, int32 Count = 1)
	{
		CheckInvariants();
		checkSlow((Count >= 0) & (Index >= 0) & (Index <= ArrayNum));

		const int32 OldNum = ArrayNum;
		if ((ArrayNum += Count) > ArrayMax)
		{
			ArrayMax = AllocatorInstance.CalculateSlack(ArrayNum, ArrayMax, sizeof(ElementType));
			AllocatorInstance.ResizeAllocation(OldNum, ArrayMax, sizeof(ElementType));
		}
		ElementType* Data = GetData() + Index;
		RelocateConstructItems<ElementType>(Data + Count, Data, OldNum - Index);
	}

	/**
	 * Inserts a given number of zeroed elements into the array at given
	 * location.
	 *
	 * Caution, InsertZeroed() will create elements without calling the
	 * constructor and this is not appropriate for element types that require
	 * a constructor to function properly.
	 *
	 * @param Index Tells where to insert the new elements.
	 * @param Count Number of elements to add.
	 */
	void InsertZeroed(int32 Index, int32 Count = 1)
	{
		InsertUninitialized(Index, Count);
		FMemory::Memzero((uint8*)AllocatorInstance.GetAllocation() + Index*sizeof(ElementType), Count*sizeof(ElementType));
	}

	/**
	 * Inserts given elements into the array at given location.
	 *
	 * @param Items Array of elements to insert.
	 * @param InIndex Tells where to insert the new elements.
	 *
	 * @returns Location at which the insert was done.
	 */
	int32 Insert(const TArray<ElementType>& Items, const int32 InIndex)
	{
		check(this != &Items);
		InsertUninitialized(InIndex, Items.Num());
		int32 Index = InIndex;
		for (auto It = Items.CreateConstIterator(); It; ++It)
		{
			RangeCheck(Index);
			new(GetData() + Index++) ElementType(MoveTemp(*It));
		}
		return InIndex;
	}

	/**
	 * Inserts a raw array of elements at a particular index in the TArray.
	 *
	 * @param Ptr   A pointer to an array of elements to add.
	 * @param Count The number of elements to insert from Ptr.
	 * @param Index The index to insert the elements at.
	 *
	 * @return The index of the first element inserted.
	 */
	int32 Insert(const ElementType* Ptr, int32 Count, int32 Index)
	{
		check(Ptr != nullptr);

		InsertUninitialized(Index, Count);
		ConstructItems<ElementType>(GetData() + Index, Ptr, Count);

		return Index;
	}

	/**
	 * Checks that the specified address is not part of an element within the
	 * container. Used for implementations to check that reference arguments
	 * aren't going to be invalidated by possible reallocation.
	 *
	 * @param Addr The address to check.
	 */
	FORCEINLINE void CheckAddress(const ElementType* Addr) const
	{
		checkf(Addr < GetData() || Addr >= (GetData() + ArrayMax), TEXT("Attempting to add a container element (0x%08x) which already comes from the container (0x%08x, ArrayMax: %d)!"), Addr, GetData(), ArrayMax);
	}

	/**
	 * Inserts a given element into the array at given location. Move semantics
	 * version.
	 *
	 * @param Item The element to insert.
	 * @param Index Tells where to insert the new elements.
	 *
	 * @returns Location at which the insert was done.
	 */
	int32 Insert(ElementType&& Item, int32 Index)
	{
		CheckAddress(&Item);

		// construct a copy in place at Index (this new operator will insert at 
		// Index, then construct that memory with Item)
		InsertUninitialized(Index, 1);
		new(GetData() + Index) ElementType(MoveTemp(Item));
		return Index;
	}

	/**
	 * Inserts a given element into the array at given location.
	 *
	 * @param Item The element to insert.
	 * @param Index Tells where to insert the new elements.
	 *
	 * @returns Location at which the insert was done.
	 */
	int32 Insert(const ElementType& Item, int32 Index)
	{
		CheckAddress(&Item);

		// construct a copy in place at Index (this new operator will insert at 
		// Index, then construct that memory with Item)
		InsertUninitialized(Index, 1);
		new(GetData() + Index) ElementType(Item);
		return Index;
	}

	/**
	 * Removes an element (or elements) at given location optionally shrinking
	 * the array.
	 *
	 * @param Index Location in array of the element to remove.
	 * @param Count (Optional) Number of elements to remove. Default is 1.
	 * @param bAllowShrinking (Optional) Tells if this call can shrink array if
	 *                        suitable after remove. Default is true.
	 */
	void RemoveAt(int32 Index, int32 Count = 1, bool bAllowShrinking = true)
	{
		CheckInvariants();
		checkSlow((Count >= 0) & (Index >= 0) & (Index + Count <= ArrayNum));

		DestructItems(GetData() + Index, Count);

		// Skip memmove in the common case that there is nothing to move.
		int32 NumToMove = ArrayNum - Index - Count;
		if (NumToMove)
		{
			FMemory::Memmove
				(
				(uint8*)AllocatorInstance.GetAllocation() + (Index)* sizeof(ElementType),
				(uint8*)AllocatorInstance.GetAllocation() + (Index + Count) * sizeof(ElementType),
				NumToMove * sizeof(ElementType)
				);
		}
		ArrayNum -= Count;

		if (bAllowShrinking)
		{
			const int32 NewArrayMax = AllocatorInstance.CalculateSlack(ArrayNum, ArrayMax, sizeof(ElementType));
			if (NewArrayMax != ArrayMax)
			{
				ArrayMax = NewArrayMax;
				AllocatorInstance.ResizeAllocation(ArrayNum, ArrayMax, sizeof(ElementType));
			}
		}
	}

	/**
	 * Removes an element (or elements) at given location optionally shrinking
	 * the array.
	 *
	 * This version is much more efficient than RemoveAt (O(Count) instead of
	 * O(ArrayNum)), but does not preserve the order.
	 *
	 * @param Index Location in array of the element to remove.
	 * @param Count (Optional) Number of elements to remove. Default is 1.
	 * @param bAllowShrinking (Optional) Tells if this call can shrink array if
	 *                        suitable after remove. Default is true.
	 */
	void RemoveAtSwap(int32 Index, int32 Count = 1, bool bAllowShrinking = true)
	{
		CheckInvariants();
		checkSlow((Count >= 0) & (Index >= 0) & (Index + Count <= ArrayNum));

		DestructItems(GetData() + Index, Count);

		// Replace the elements in the hole created by the removal with elements from the end of the array, so the range of indices used by the array is contiguous.
		const int32 NumElementsInHole = Count;
		const int32 NumElementsAfterHole = ArrayNum - (Index + Count);
		const int32 NumElementsToMoveIntoHole = FMath::Min(NumElementsInHole, NumElementsAfterHole);
		if (NumElementsToMoveIntoHole)
		{
			FMemory::Memcpy(
				(uint8*)AllocatorInstance.GetAllocation() + (Index)* sizeof(ElementType),
				(uint8*)AllocatorInstance.GetAllocation() + (ArrayNum - NumElementsToMoveIntoHole) * sizeof(ElementType),
				NumElementsToMoveIntoHole * sizeof(ElementType)
				);
		}
		ArrayNum -= Count;

		if (bAllowShrinking)
		{
			const int32 NewArrayMax = AllocatorInstance.CalculateSlack(ArrayNum, ArrayMax, sizeof(ElementType));
			if (NewArrayMax != ArrayMax)
			{
				ArrayMax = NewArrayMax;
				AllocatorInstance.ResizeAllocation(ArrayNum, ArrayMax, sizeof(ElementType));
			}
		}
	}

	/**
	 * Same as empty, but doesn't change memory allocations, unless the new size is larger than
	 * the current array. It calls the destructors on held items if needed and then zeros the ArrayNum.
	 *
	 * @param NewSize The expected usage size after calling this function.
	 */
	void Reset(int32 NewSize = 0)
	{
		// If we have space to hold the excepted size, then don't reallocate
		if (NewSize <= ArrayMax)
		{
			DestructItems(GetData(), ArrayNum);
			ArrayNum = 0;
		}
		else
		{
			Empty(NewSize);
		}
	}

	/**
	 * Empties the array. It calls the destructors on held items if needed.
	 *
	 * @param Slack (Optional) The expected usage size after empty operation. Default is 0.
	 */
	void Empty(int32 Slack = 0)
	{
		DestructItems(GetData(), ArrayNum);

		checkSlow(Slack >= 0);
		ArrayNum = 0;
		// only reallocate if we need to, I don't trust realloc to the same size to work
		if (ArrayMax != Slack)
		{
			ArrayMax = Slack;
			AllocatorInstance.ResizeAllocation(0, ArrayMax, sizeof(ElementType));
		}
	}

	/**
	 * Resizes array to given number of elements.
	 *
	 * @param NewNum New size of the array.
	 * @param bAllowShrinking Tell if this function can shrink the memory in-use if suitable.
	 */
	void SetNum(int32 NewNum, bool bAllowShrinking = true)
	{
		if (NewNum > Num())
		{
			const int32 Diff = NewNum - ArrayNum;
			const int32 Index = AddUninitialized(Diff);
			DefaultConstructItems<ElementType>((uint8*)AllocatorInstance.GetAllocation() + Index * sizeof(ElementType), Diff);
		}
		else if (NewNum < Num())
		{
			RemoveAt(NewNum, Num() - NewNum, bAllowShrinking);
		}
	}

	/**
	 * Resizes array to given number of elements. New elements will be zeroed.
	 *
	 * @param NewNum New size of the array.
	 */
	void SetNumZeroed(int32 NewNum)
	{
		if (NewNum > Num())
		{
			AddZeroed(NewNum - Num());
		}
		else if (NewNum < Num())
		{
			RemoveAt(NewNum, Num() - NewNum);
		}
	}

	/**
	 * Resizes array to given number of elements. New elements will be uninitialized.
	 *
	 * @param NewNum New size of the array.
	 */
	void SetNumUninitialized(int32 NewNum)
	{
		if (NewNum > Num())
		{
			AddUninitialized(NewNum - Num());
		}
		else if (NewNum < Num())
		{
			RemoveAt(NewNum, Num() - NewNum);
		}
	}

	/**
	 * Does nothing except setting the new number of elements in the array. Does not destruct items, does not de-allocate memory.
	 * @param NewNum New number of elements in the array, must be <= the current number of elements in the array.
	 */
	void SetNumUnsafeInternal(int32 NewNum)
	{
		checkSlow(NewNum <= Num() && NewNum >= 0);
		ArrayNum = NewNum;
	}

	/**
	 * Appends the specified array to this array.
	 *
	 * Allocator changing version.
	 *
	 * @param Source The array to append.
	 */
	template <typename OtherElementType, typename OtherAllocator>
	FORCEINLINE void Append(const TArray<OtherElementType, OtherAllocator>& Source)
	{
		check((void*)this != (void*)&Source);

		int32 SourceCount = Source.Num();

		// Do nothing if the source is empty.
		if (!SourceCount)
		{
			return;
		}

		// Allocate memory for the new elements.
		Reserve(ArrayNum + SourceCount);

		ConstructItems<ElementType>(GetData() + ArrayNum, Source.GetData(), SourceCount);

		ArrayNum += SourceCount;
	}

	/**
	 * Appends the specified array to this array.
	 *
	 * @param Source The array to append.
	 */
	template <typename OtherElementType, typename OtherAllocator>
	FORCEINLINE void Append(TArray<OtherElementType, OtherAllocator>&& Source)
	{
		check((void*)this != (void*)&Source);

		int32 SourceCount = Source.Num();

		// Do nothing if the source is empty.
		if (!SourceCount)
		{
			return;
		}

		// Allocate memory for the new elements.
		Reserve(ArrayNum + SourceCount);

		RelocateConstructItems<ElementType>(GetData() + ArrayNum, Source.GetData(), SourceCount);
		Source.ArrayNum = 0;

		ArrayNum += SourceCount;
	}

	/**
	 * Adds a raw array of elements to the end of the TArray.
	 *
	 * @param Ptr   A pointer to an array of elements to add.
	 * @param Count The number of elements to insert from Ptr.
	 */
	void Append(const ElementType* Ptr, int32 Count)
	{
		check(Ptr != nullptr);

		int32 Pos = AddUninitialized(Count);
		ConstructItems<ElementType>(GetData() + Pos, Ptr, Count);
	}

	/**
	 * Appends the specified array to this array.
	 * Cannot append to self.
	 *
	 * Move semantics version.
	 *
	 * @param Other The array to append.
	 */
	FORCEINLINE TArray& operator+=(TArray&& Other)
	{
		Append(MoveTemp(Other));
		return *this;
	}

	/**
	 * Appends the specified array to this array.
	 * Cannot append to self.
	 *
	 * @param Other The array to append.
	 */
	FORCEINLINE TArray& operator+=(const TArray& Other)
	{
		Append(Other);
		return *this;
	}

	/**
	 * Adds a new item to the end of the array, possibly reallocating the whole array to fit.
	 *
	 * @param Item	The item to add
	 * @return		Index to the new item
	 */
	#if PLATFORM_COMPILER_HAS_VARIADIC_TEMPLATES

		template <typename... ArgsType>
		int32 Emplace(ArgsType&&... Args)
		{
			const int32 Index = AddUninitialized(1);
			new(GetData() + Index) ElementType(Forward<ArgsType>(Args)...);
			return Index;
		}

	#else

		int32 Emplace()
		{
			const int32 Index = AddUninitialized(1);
			new(GetData() + Index) ElementType();
			return Index;
		}

		template <typename Arg0Type>
		int32 Emplace(Arg0Type&& Arg0)
		{
			const int32 Index = AddUninitialized(1);
			new(GetData() + Index) ElementType(Forward<Arg0Type>(Arg0));
			return Index;
		}

		template <typename Arg0Type, typename Arg1Type>
		int32 Emplace(Arg0Type&& Arg0, Arg1Type&& Arg1)
		{
			const int32 Index = AddUninitialized(1);
			new(GetData() + Index) ElementType(Forward<Arg0Type>(Arg0), Forward<Arg1Type>(Arg1));
			return Index;
		}

		template <typename Arg0Type, typename Arg1Type, typename Arg2Type>
		int32 Emplace(Arg0Type&& Arg0, Arg1Type&& Arg1, Arg2Type&& Arg2)
		{
			const int32 Index = AddUninitialized(1);
			new(GetData() + Index) ElementType(Forward<Arg0Type>(Arg0), Forward<Arg1Type>(Arg1), Forward<Arg2Type>(Arg2));
			return Index;
		}

		template <typename Arg0Type, typename Arg1Type, typename Arg2Type, typename Arg3Type>
		int32 Emplace(Arg0Type&& Arg0, Arg1Type&& Arg1, Arg2Type&& Arg2, Arg3Type&& Arg3)
		{
			const int32 Index = AddUninitialized(1);
			new(GetData() + Index) ElementType(Forward<Arg0Type>(Arg0), Forward<Arg1Type>(Arg1), Forward<Arg2Type>(Arg2), Forward<Arg3Type>(Arg3));
			return Index;
		}

		template <typename Arg0Type, typename Arg1Type, typename Arg2Type, typename Arg3Type, typename Arg4Type>
		int32 Emplace(Arg0Type&& Arg0, Arg1Type&& Arg1, Arg2Type&& Arg2, Arg3Type&& Arg3, Arg4Type&& Arg4)
		{
			const int32 Index = AddUninitialized(1);
			new(GetData() + Index) ElementType(Forward<Arg0Type>(Arg0), Forward<Arg1Type>(Arg1), Forward<Arg2Type>(Arg2), Forward<Arg3Type>(Arg3), Forward<Arg4Type>(Arg4));
			return Index;
		}

		template <typename Arg0Type, typename Arg1Type, typename Arg2Type, typename Arg3Type, typename Arg4Type, typename Arg5Type>
		int32 Emplace(Arg0Type&& Arg0, Arg1Type&& Arg1, Arg2Type&& Arg2, Arg3Type&& Arg3, Arg4Type&& Arg4, Arg5Type&& Arg5)
		{
			const int32 Index = AddUninitialized(1);
			new(GetData() + Index) ElementType(Forward<Arg0Type>(Arg0), Forward<Arg1Type>(Arg1), Forward<Arg2Type>(Arg2), Forward<Arg3Type>(Arg3), Forward<Arg4Type>(Arg4), Forward<Arg5Type>(Arg5));
			return Index;
		}

#endif

	/**
	 * Adds a new item to the end of the array, possibly reallocating the whole array to fit.
	 *
	 * Move semantics version.
	 *
	 * @param Item	The item to add
	 * @return		Index to the new item
	 */
	FORCEINLINE int32 Add(ElementType&& Item) { CheckAddress(&Item); return Emplace(MoveTemp(Item)); }

	/**
	 * Adds a new item to the end of the array, possibly reallocating the whole array to fit.
	 *
	 * @param Item	The item to add
	 * @return		Index to the new item
	 */
	FORCEINLINE int32 Add(const ElementType& Item) { CheckAddress(&Item); return Emplace(Item); }

	/**
	 * Adds new items to the end of the array, possibly reallocating the whole
	 * array to fit. The new items will be zeroed.
	 *
	 * Caution, AddZeroed() will create elements without calling the
	 * constructor and this is not appropriate for element types that require
	 * a constructor to function properly.
	 *
	 * @param  Count  The number of new items to add.
	 *
	 * @return Index to the first of the new items.
	 */
	int32 AddZeroed(int32 Count = 1)
	{
		const int32 Index = AddUninitialized(Count);
		FMemory::Memzero((uint8*)AllocatorInstance.GetAllocation() + Index*sizeof(ElementType), Count*sizeof(ElementType));
		return Index;
	}

	/**
	 * Adds new items to the end of the array, possibly reallocating the whole
	 * array to fit. The new items will be default-constructed.
	 *
	 * @param  Count  The number of new items to add.
	 *
	 * @return Index to the first of the new items.
	 */
	int32 AddDefaulted(int32 Count = 1)
	{
		const int32 Index = AddUninitialized(Count);
		DefaultConstructItems<ElementType>((uint8*)AllocatorInstance.GetAllocation() + Index * sizeof(ElementType), Count);
		return Index;
	}

private:
	/**
	 * Adds unique element to array if it doesn't exist.
	 *
	 * @param Args Item to add.
	 *
	 * @returns Index of the element in the array.
	 */
	template <typename ArgsType>
	int32 AddUniqueImpl(ArgsType&& Args)
	{
		int32 Index;
		if (Find(Args, Index))
		{
			return Index;
		}

		return Add(Forward<ArgsType>(Args));
	}

public:
	/**
	 * Adds unique element to array if it doesn't exist.
	 *
	 * Move semantics version.
	 *
	 * @param Args Item to add.
	 *
	 * @returns Index of the element in the array.
	 */
	FORCEINLINE int32 AddUnique(ElementType&& Item) { return AddUniqueImpl(MoveTemp(Item)); }

	/**
	 * Adds unique element to array if it doesn't exist.
	 *
	 * @param Args Item to add.
	 *
	 * @returns Index of the element in the array.
	 */
	FORCEINLINE int32 AddUnique(const ElementType& Item) { return AddUniqueImpl(Item); }

	/**
	 * Reserves memory such that the array can contain at least Number elements.
	 *
	 * @param Number The number of elements that the array should be able to
	 *               contain after allocation.
	 */
	void Reserve(int32 Number)
	{
		if (Number > ArrayMax)
		{
			ArrayMax = Number;
			AllocatorInstance.ResizeAllocation(ArrayNum, ArrayMax, sizeof(ElementType));
		}
	}
	
	/**
	 * Sets the size of the array. The added elements will be uninitialized.
	 *
	 * @param Number The number of elements that the array should be able to
	 *               contain after allocation.
	 */
	DEPRECATED(4.8, "Init is deprecated - please use SetNumUninitialized(Number) instead.")
	void Init(int32 Number)
	{
		Empty(Number);
		AddUninitialized(Number);
	}

	/**
	 * Sets the size of the array, filling it with the given element.
	 *
	 * @param Element The element to fill array with.
	 * @param Number The number of elements that the array should be able to
	 *               contain after allocation.
	 */
	void Init(const ElementType& Element, int32 Number)
	{
		Empty(Number);
		for (int32 Index = 0; Index < Number; ++Index)
		{
			new(*this) ElementType(Element);
		}
	}

	/**
	 * Removes the first occurrence of the specified item in the array,
	 * maintaining order but not indices.
	 *
	 * @param Item The item to remove.
	 *
	 * @returns The number of items removed. For RemoveSingleItem, this is
	 *          always either 0 or 1.
	 */
	int32 RemoveSingle(const ElementType& Item)
	{
		int32 Index = Find(Item);
		if (Index == INDEX_NONE)
		{
			return 0;
		}

		auto* RemovePtr = GetData() + Index;

		// Destruct items that match the specified Item.
		DestructItems(RemovePtr, 1);
		const int32 NextIndex = Index + 1;
		RelocateConstructItems<ElementType>(RemovePtr, RemovePtr + 1, ArrayNum - (Index + 1));

		// Update the array count
		--ArrayNum;

		// Removed one item
		return 1;
	}

	/**
	 * Removes as many instances of Item as there are in the array, maintaining
	 * order but not indices.
	 *
	 * @param Item Item to remove from array.
	 *
	 * @returns Number of removed elements.
	 */
	int32 Remove(const ElementType& Item)
	{
		CheckAddress(&Item);

		// Element is non-const to preserve compatibility with existing code with a non-const operator==() member function
		return RemoveAll([&Item](ElementType& Element) { return Element == Item; });
	}


	/**
	 * Remove all instances that match the predicate, maintaining order but not indices
	 * Optimized to work with runs of matches/non-matches
	 *
	 * @param Predicate Predicate class instance
	 *
	 * @returns Number of removed elements.
	 */
	template <class PREDICATE_CLASS>
	int32 RemoveAll(const PREDICATE_CLASS& Predicate)
	{
		const int32 OriginalNum = ArrayNum;
		if (!OriginalNum)
		{
			return 0; // nothing to do, loop assumes one item so need to deal with this edge case here
		}

		int32 WriteIndex = 0;
		int32 ReadIndex = 0;
		bool NotMatch = !Predicate(GetData()[ReadIndex]); // use a ! to guarantee it can't be anything other than zero or one
		do
		{
			int32 RunStartIndex = ReadIndex++;
			while (ReadIndex < OriginalNum && NotMatch == !Predicate(GetData()[ReadIndex]))
			{
				ReadIndex++;
			}
			int32 RunLength = ReadIndex - RunStartIndex;
			checkSlow(RunLength > 0);
			if (NotMatch)
			{
				// this was a non-matching run, we need to move it
				if (WriteIndex != RunStartIndex)
				{
					FMemory::Memmove(&GetData()[WriteIndex], &GetData()[RunStartIndex], sizeof(ElementType)* RunLength);
				}
				WriteIndex += RunLength;
			}
			else
			{
				// this was a matching run, delete it
				DestructItems(GetData() + RunStartIndex, RunLength);
			}
			NotMatch = !NotMatch;
		} while (ReadIndex < OriginalNum);

		ArrayNum = WriteIndex;
		return OriginalNum - ArrayNum;
	}

	/**
	 * Remove all instances that match the predicate
	 *
	 * @param Predicate Predicate class instance
	 */
	template <class PREDICATE_CLASS>
	void RemoveAllSwap(const PREDICATE_CLASS& Predicate, bool bAllowShrinking = true)
	{
		for (int32 ItemIndex = 0; ItemIndex < Num();)
		{
			if (Predicate((*this)[ItemIndex]))
			{
				RemoveAtSwap(ItemIndex, 1, bAllowShrinking);
			}
			else
			{
				++ItemIndex;
			}
		}
	}

	/**
	 * Removes the first occurrence of the specified item in the array. This version is much more efficient
	 * O(Count) instead of O(ArrayNum), but does not preserve the order
	 *
	 * @param Item The item to remove
	 *
	 * @returns The number of items removed. For RemoveSingleItem, this is always either 0 or 1.
	 */
	int32 RemoveSingleSwap(const ElementType& Item, bool bAllowShrinking = true)
	{
		int32 Index = Find(Item);
		if (Index == INDEX_NONE)
		{
			return 0;
		}

		RemoveAtSwap(Index, 1, bAllowShrinking);

		// Removed one item
		return 1;
	}

	/**
	 * Removes item from the array.
	 *
	 * This version is much more efficient, because it uses RemoveAtSwap
	 * internally which is O(Count) instead of RemoveAt which is O(ArrayNum),
	 * but does not preserve the order.
	 *
	 * @returns Number of elements removed.
	 */
	int32 RemoveSwap(const ElementType& Item)
	{
		CheckAddress(&Item);

		const int32 OriginalNum = ArrayNum;
		for (int32 Index = 0; Index < ArrayNum; Index++)
		{
			if ((*this)[Index] == Item)
			{
				RemoveAtSwap(Index--);
			}
		}
		return OriginalNum - ArrayNum;
	}

	/**
	 * Element-wise array memory swap.
	 *
	 * @param FirstIndexToSwap Position of the first element to swap.
	 * @param SecondIndexToSwap Position of the second element to swap.
	 */
	void SwapMemory(int32 FirstIndexToSwap, int32 SecondIndexToSwap)
	{
		FMemory::Memswap(
			(uint8*)AllocatorInstance.GetAllocation() + (sizeof(ElementType)*FirstIndexToSwap),
			(uint8*)AllocatorInstance.GetAllocation() + (sizeof(ElementType)*SecondIndexToSwap),
			sizeof(ElementType)
			);
	}

	/**
	 * Element-wise array element swap.
	 *
	 * This version is doing more sanity checks than SwapMemory.
	 *
	 * @param FirstIndexToSwap Position of the first element to swap.
	 * @param SecondIndexToSwap Position of the second element to swap.
	 */
	void Swap(int32 FirstIndexToSwap, int32 SecondIndexToSwap)
	{
		check((FirstIndexToSwap >= 0) && (SecondIndexToSwap >= 0));
		check((ArrayNum > FirstIndexToSwap) && (ArrayNum > SecondIndexToSwap));
		if (FirstIndexToSwap != SecondIndexToSwap)
		{
			SwapMemory(FirstIndexToSwap, SecondIndexToSwap);
		}
	}

	/**
	 * Searches for the first entry of the specified type, will only work with
	 * TArray<UObject*>. Optionally return the item's index, and can specify
	 * the start index.
	 *
	 * @param Item (Optional output) If it's not null, then it will point to
	 *             the found element. Untouched if element hasn't been found.
	 * @param ItemIndex (Optional output) If it's not null, then it will be set
	 *             to the position of found element in the array. Untouched if
	 *             element hasn't been found.
	 * @param StartIndex (Optional) Index in array at which the function should
	 *             start to look for element.
	 *
	 * @returns True if element was found. False otherwise.
	 */
	template<typename SearchType>
	bool FindItemByClass(SearchType **Item = nullptr, int32 *ItemIndex = nullptr, int32 StartIndex = 0) const
	{
		UClass* SearchClass = SearchType::StaticClass();
		for (int32 Idx = StartIndex; Idx < ArrayNum; Idx++)
		{
			if ((*this)[Idx] != nullptr && (*this)[Idx]->IsA(SearchClass))
			{
				if (Item != nullptr)
				{
					*Item = (SearchType*)((*this)[Idx]);
				}
				if (ItemIndex != nullptr)
				{
					*ItemIndex = Idx;
				}
				return true;
			}
		}
		return false;
	}

	// Iterators
	typedef TIndexedContainerIterator<      TArray,       ElementType, int32> TIterator;
	typedef TIndexedContainerIterator<const TArray, const ElementType, int32> TConstIterator;

	/**
	 * Creates an iterator for the contents of this array
	 *
	 * @returns The iterator.
	 */
	TIterator CreateIterator()
	{
		return TIterator(*this);
	}

	/**
	 * Creates a const iterator for the contents of this array
	 *
	 * @returns The const iterator.
	 */
	TConstIterator CreateConstIterator() const
	{
		return TConstIterator(*this);
	}

	#if TARRAY_RANGED_FOR_CHECKS
		typedef TCheckedPointerIterator<      ElementType> RangedForIteratorType;
		typedef TCheckedPointerIterator<const ElementType> RangedForConstIteratorType;
	#else
		typedef       ElementType* RangedForIteratorType;
		typedef const ElementType* RangedForConstIteratorType;
	#endif

private:
	/**
	 * DO NOT USE DIRECTLY
	 * STL-like iterators to enable range-based for loop support.
	 */
	#if TARRAY_RANGED_FOR_CHECKS
		FORCEINLINE friend RangedForIteratorType      begin(      TArray& Array) { return RangedForIteratorType     (Array.ArrayNum, Array.GetData()); }
		FORCEINLINE friend RangedForConstIteratorType begin(const TArray& Array) { return RangedForConstIteratorType(Array.ArrayNum, Array.GetData()); }
		FORCEINLINE friend RangedForIteratorType      end  (      TArray& Array) { return RangedForIteratorType     (Array.ArrayNum, Array.GetData() + Array.Num()); }
		FORCEINLINE friend RangedForConstIteratorType end  (const TArray& Array) { return RangedForConstIteratorType(Array.ArrayNum, Array.GetData() + Array.Num()); }
	#else
		FORCEINLINE friend RangedForIteratorType      begin(      TArray& Array) { return Array.GetData(); }
		FORCEINLINE friend RangedForConstIteratorType begin(const TArray& Array) { return Array.GetData(); }
		FORCEINLINE friend RangedForIteratorType      end  (      TArray& Array) { return Array.GetData() + Array.Num(); }
		FORCEINLINE friend RangedForConstIteratorType end  (const TArray& Array) { return Array.GetData() + Array.Num(); }
	#endif

public:
	/**
	 * Sorts the array assuming < operator is defined for the item type.
	 */
	void Sort()
	{
		::Sort(GetData(), Num());
	}

	/**
	 * Sorts the array using user define predicate class.
	 *
	 * @param Predicate Predicate class instance.
	 */
	template <class PREDICATE_CLASS>
	void Sort(const PREDICATE_CLASS& Predicate)
	{
		::Sort(GetData(), Num(), Predicate);
	}

	/**
	 * Stable sorts the array assuming < operator is defined for the item type.
	 *
	 * Stable sort is slower than non-stable algorithm.
	 */
	void StableSort()
	{
		::StableSort(GetData(), Num());
	}

	/**
	 * Stable sorts the array using user defined predicate class.
	 *
	 * Stable sort is slower than non-stable algorithm.
	 *
	 * @param Predicate Predicate class instance
	 */
	template <class PREDICATE_CLASS>
	void StableSort(const PREDICATE_CLASS& Predicate)
	{
		::StableSort(GetData(), Num(), Predicate);
	}

#if defined(_MSC_VER) && !defined(__clang__)	// Relies on MSVC-specific lazy template instantiation to support arrays of incomplete types
private:
	/**
	 * Helper function that can be used inside the debuggers watch window to debug TArrays. E.g. "*Class->Defaults.DebugGet(5)". 
	 *
	 * @param Index Position to get.
	 * @returns Reference to the element at given position.
	 */
	FORCENOINLINE const ElementType& DebugGet(int32 Index) const
	{
		return GetData()[Index];
	}
#endif

private:

	/**
	 * Copies data from one array into this array. Uses the fast path if the
	 * data in question does not need a constructor.
	 *
	 * @param Source The source array to copy
	 * @param PrevMax The previous allocated size
	 * @param ExtraSlack Additional amount of memory to allocate at
	 *                   the end of the buffer. Counted in elements. Zero by
	 *                   default.
	 */
	template <typename OtherElementType, typename OtherAllocator>
	void CopyToEmpty(const TArray<OtherElementType, OtherAllocator>& Source, int32 PrevMax, int32 ExtraSlack)
	{
		check(ExtraSlack >= 0);

		int32 SourceCount = Source.Num();
		if (SourceCount + ExtraSlack != PrevMax)
		{
			AllocatorInstance.ResizeAllocation(0, SourceCount + ExtraSlack, sizeof(ElementType));
		}

		ConstructItems<ElementType>(GetData(), Source.GetData(), SourceCount);

		ArrayNum = SourceCount;
		ArrayMax = SourceCount + ExtraSlack;
	}

protected:

	typedef typename TChooseClass<
		Allocator::NeedsElementType,
		typename Allocator::template ForElementType<ElementType>,
		typename Allocator::ForAnyElementType
		>::Result ElementAllocatorType;

	ElementAllocatorType AllocatorInstance;
	int32	  ArrayNum;
	int32	  ArrayMax;

	/**
	 * Implicit heaps
	 */

public:

	/** 
	 * Builds an implicit heap from the array.
	 *
	 * @param Predicate Predicate class instance.
	 */
	template <class PREDICATE_CLASS>
	void Heapify(const PREDICATE_CLASS& Predicate)
	{
		TDereferenceWrapper< ElementType, PREDICATE_CLASS> PredicateWrapper(Predicate);
		for (int32 Index = HeapGetParentIndex(Num() - 1); Index >= 0; Index--)
		{
			SiftDown(Index, Num(), PredicateWrapper);
		}

#if DEBUG_HEAP
		VerifyHeap(PredicateWrapper);
#endif
	}

	/**
	 * Builds an implicit heap from the array. Assumes < operator is defined
	 * for the template type.
	 */
	void Heapify()
	{
		Heapify(TLess<ElementType>());
	}

	/** 
	 * Adds a new element to the heap.
	 *
	 * @param InIntem Item to be added.
	 * @param Predicate Predicate class instance.
	 *
	 * @return The index of the new element.
	 */
	template <class PREDICATE_CLASS>
	int32 HeapPush(const ElementType& InItem, const PREDICATE_CLASS& Predicate)
	{
		// Add at the end, then sift up
		Add(InItem);
		TDereferenceWrapper<ElementType, PREDICATE_CLASS> PredicateWrapper(Predicate);
		int32 Result = SiftUp(0, Num() - 1, PredicateWrapper);

#if DEBUG_HEAP
		VerifyHeap(PredicateWrapper);
#endif

		return Result;
	}

	/** 
	 * Adds a new element to the heap. Assumes < operator is defined for the
	 * template type.
	 *
	 * @param InIntem Item to be added.
	 *
	 * @return The index of the new element.
	 */
	int32 HeapPush(const ElementType& InItem)
	{
		return HeapPush(InItem, TLess<ElementType>());
	}

	/** 
	 * Removes the top element from the heap.
	 *
	 * @param OutItem The removed item.
	 * @param Predicate Predicate class instance.
	 */
	template <class PREDICATE_CLASS>
	void HeapPop(ElementType& OutItem, const PREDICATE_CLASS& Predicate, bool bAllowShrinking = true)
	{
		OutItem = (*this)[0];
		RemoveAtSwap(0, 1, bAllowShrinking);

		TDereferenceWrapper< ElementType, PREDICATE_CLASS> PredicateWrapper(Predicate);
		SiftDown(0, Num(), PredicateWrapper);

#if DEBUG_HEAP
		VerifyHeap(PredicateWrapper);
#endif
	}

	/** 
	 * Removes the top element from the heap. Assumes < operator is defined for
	 * the template type.
	 *
	 * @param OutItem The removed item.
	 * @param bAllowShrinking (Optional) Tells if this call can shrink the array allocation if suitable after the pop. Default is true.
	 */
	void HeapPop(ElementType& OutItem, bool bAllowShrinking = true)
	{
		HeapPop(OutItem, TLess<ElementType>(), bAllowShrinking);
	}

	/**
	 * Verifies the heap.
	 *
	 * @param Predicate Predicate class instance.
	 */
	template <class PREDICATE_CLASS>
	void VerifyHeap(const PREDICATE_CLASS& Predicate)
	{
		// Verify Predicate
		ElementType* Heap = GetData();
		for (int32 Index = 1; Index < Num(); Index++)
		{
			int32 ParentIndex = HeapGetParentIndex(Index);
			if (Predicate(Heap[Index], Heap[ParentIndex]))
			{
				check(false);
			}
		}
	}

	/** 
	 * Removes the top element from the heap.
	 *
	 * @param Predicate Predicate class instance.
	 * @param bAllowShrinking (Optional) Tells if this call can shrink the array allocation if suitable after the discard. Default is true.
	 */
	template <class PREDICATE_CLASS>
	void HeapPopDiscard(const PREDICATE_CLASS& Predicate, bool bAllowShrinking = true)
	{
		RemoveAtSwap(0, 1, bAllowShrinking);
		TDereferenceWrapper< ElementType, PREDICATE_CLASS> PredicateWrapper(Predicate);
		SiftDown(0, Num(), PredicateWrapper);

#if DEBUG_HEAP
		VerifyHeap(PredicateWrapper);
#endif
	}

	/** 
	 * Removes the top element from the heap. Assumes < operator is defined for
	 * the template type.
	 * @param bAllowShrinking (Optional) Tells if this call can shrink the array allocation if suitable after the discard. Default is true.
	 */
	void HeapPopDiscard(bool bAllowShrinking = true)
	{
		HeapPopDiscard(TLess<ElementType>(), bAllowShrinking);
	}

	/** 
	 * Returns the top element from the heap (does not remove the element).
	 *
	 * Const version.
	 *
	 * @returns The reference to the top element from the heap.
	 */
	const ElementType& HeapTop() const
	{
		return (*this)[0];
	}

	/** 
	 * Returns the top element from the heap (does not remove the element).
	 *
	 * @returns The reference to the top element from the heap.
	 */
	ElementType& HeapTop()
	{
		return (*this)[0];
	}

	/**
	 * Removes an element from the heap.
	 *
	 * @param Index Position at which to remove item.
	 * @param Predicate Predicate class instance.
	 * @param bAllowShrinking (Optional) Tells if this call can shrink the array allocation if suitable after the remove. Default is true.
	 */
	template <class PREDICATE_CLASS>
	void HeapRemoveAt(int32 Index, const PREDICATE_CLASS& Predicate, bool bAllowShrinking = true)
	{
		RemoveAtSwap(Index, 1, bAllowShrinking);

		TDereferenceWrapper< ElementType, PREDICATE_CLASS> PredicateWrapper(Predicate);
		SiftDown(Index, Num(), PredicateWrapper);
		SiftUp(0, FMath::Min(Index, Num() - 1), PredicateWrapper);

#if DEBUG_HEAP
		VerifyHeap(PredicateWrapper);
#endif
	}

	/**
	 * Removes an element from the heap. Assumes < operator is defined for the
	 * template type.
	 *
	 * @param Index Position at which to remove item.
	 * @param bAllowShrinking (Optional) Tells if this call can shrink the array allocation if suitable after the remove. Default is true.
	 */
	void HeapRemoveAt(int32 Index, bool bAllowShrinking = true)
	{
		HeapRemoveAt(Index, TLess< ElementType >(), bAllowShrinking);
	}

	/**
	 * Performs heap sort on the array.
	 *
	 * @param Predicate Predicate class instance.
	 */
	template <class PREDICATE_CLASS>
	void HeapSort(const PREDICATE_CLASS& Predicate)
	{
		TReversePredicateWrapper<ElementType, PREDICATE_CLASS> ReversePredicateWrapper(Predicate);
		Heapify(ReversePredicateWrapper);

		ElementType* Heap = GetData();
		for(int32 Index=Num()-1; Index>0; Index--)
		{
			Exchange(Heap[0], Heap[Index]);
			SiftDown(0, Index, ReversePredicateWrapper);
		}

#if DEBUG_HEAP
		TDereferenceWrapper<ElementType, PREDICATE_CLASS> PredicateWrapper(Predicate);

		// Verify Heap Property
		VerifyHeap(PredicateWrapper);

		// Also verify Array is properly sorted
		for(int32 Index=1; Index<Num(); Index++)
		{
			if (PredicateWrapper(Heap[Index], Heap[Index - 1]))
			{
				check(false);
			}
		}
#endif
	}

	/**
	 * Performs heap sort on the array. Assumes < operator is defined for the
	 * template type.
	 */
	void HeapSort()
	{
		HeapSort(TLess<ElementType>());
	}

private:

	/**
	 * Gets the index of the left child of node at Index.
	 *
	 * @param Index Node for which the left child index is to be returned.
	 *
	 * @returns Index of the left child.
	 */
	FORCEINLINE int32 HeapGetLeftChildIndex(int32 Index) const
	{
		return Index * 2 + 1;
	}

	/** 
	 * Checks if node located at Index is a leaf or not.
	 *
	 * @param Index Node index.
	 *
	 * @returns True if node is a leaf. False otherwise.
	 */
	FORCEINLINE bool HeapIsLeaf(int32 Index, int32 Count) const
	{
		return HeapGetLeftChildIndex(Index) >= Count;
	}

	/**
	 * Gets the parent index for node at Index.
	 *
	 * @param Index node index.
	 *
	 * @returns Parent index.
	 */
	FORCEINLINE int32 HeapGetParentIndex(int32 Index) const
	{
		return (Index - 1) / 2;
	}

private:
	/**
	 * Fixes a possible violation of order property between node at Index and a child.
	 *
	 * @param Index Node index.
	 * @param Count Size of the heap (to avoid using Num()).
	 * @param Predicate Predicate class instance.
	 */
	template <class PREDICATE_CLASS>
	FORCEINLINE void SiftDown(int32 Index, const int32 Count, const PREDICATE_CLASS& Predicate)
	{
		ElementType* Heap = GetData();
		while (!HeapIsLeaf(Index, Count))
		{
			const int32 LeftChildIndex = HeapGetLeftChildIndex(Index);
			const int32 RightChildIndex = LeftChildIndex + 1;

			int32 MinChildIndex = LeftChildIndex;
			if (RightChildIndex < Count)
			{
				MinChildIndex = Predicate(Heap[LeftChildIndex], Heap[RightChildIndex]) ? LeftChildIndex : RightChildIndex;
			}

			if (!Predicate(Heap[MinChildIndex], Heap[Index]))
			{
				break;
			}

			Exchange(Heap[Index], Heap[MinChildIndex]);
			Index = MinChildIndex;
		}
	}

	/**
	 * Fixes a possible violation of order property between node at NodeIndex and a parent.
	 *
	 * @param RootIndex How far to go up?
	 * @param NodeIndex Node index.
	 * @param Predicate Predicate class instance.
	 *
	 * @return The new index of the node that was at NodeIndex
	 */
	template <class PREDICATE_CLASS>
	FORCEINLINE int32 SiftUp(int32 RootIndex, int32 NodeIndex, const PREDICATE_CLASS& Predicate)
	{
		ElementType* Heap = GetData();
		while (NodeIndex > RootIndex)
		{
			int32 ParentIndex = HeapGetParentIndex(NodeIndex);
			if (!Predicate(Heap[NodeIndex], Heap[ParentIndex]))
			{
				break;
			}

			Exchange(Heap[NodeIndex], Heap[ParentIndex]);
			NodeIndex = ParentIndex;
		}

		return NodeIndex;
	}
};

template <typename InElementType, typename Allocator>
struct TIsZeroConstructType<TArray<InElementType, Allocator>>
{
	enum { Value = TAllocatorTraits<Allocator>::IsZeroConstruct };
};

template <typename InElementType, typename Allocator>
struct TContainerTraits<TArray<InElementType, Allocator> > : public TContainerTraitsBase<TArray<InElementType, Allocator> >
{
	enum { MoveWillEmptyContainer = TAllocatorTraits<Allocator>::SupportsMove };
};

/**
 * Traits class which determines whether or not a type is a TArray.
 */
template <typename T> struct TIsTArray { enum { Value = false }; };

template <typename InElementType, typename Allocator> struct TIsTArray<               TArray<InElementType, Allocator>> { enum { Value = true }; };
template <typename InElementType, typename Allocator> struct TIsTArray<const          TArray<InElementType, Allocator>> { enum { Value = true }; };
template <typename InElementType, typename Allocator> struct TIsTArray<      volatile TArray<InElementType, Allocator>> { enum { Value = true }; };
template <typename InElementType, typename Allocator> struct TIsTArray<const volatile TArray<InElementType, Allocator>> { enum { Value = true }; };

//
// Array operator news.
//
template <typename T,typename Allocator> void* operator new( size_t Size, TArray<T,Allocator>& Array )
{
	check(Size == sizeof(T));
	const int32 Index = Array.AddUninitialized(1);
	return &Array[Index];
}
template <typename T,typename Allocator> void* operator new( size_t Size, TArray<T,Allocator>& Array, int32 Index )
{
	check(Size == sizeof(T));
	Array.InsertUninitialized(Index,1);
	return &Array[Index];
}

/*-----------------------------------------------------------------------------
	MRU array.
-----------------------------------------------------------------------------*/

/**
 * Same as TArray except:
 * - Has an upper limit of the number of items it will store.
 * - Any item that is added to the array is moved to the top.
 */
template<typename T, typename Allocator = FDefaultAllocator>
class TMRUArray : public TArray<T, Allocator>
{
public:
	typedef TArray<T, Allocator> Super;

	/** The maximum number of items we can store in this array. */
	int32 MaxItems;

	/**
	 * Constructor.
	 */
	TMRUArray()
		: Super()
	{
		MaxItems = 0;
	}

#if PLATFORM_COMPILER_HAS_DEFAULTED_FUNCTIONS

	TMRUArray(TMRUArray&&) = default;
	TMRUArray(const TMRUArray&) = default;
	TMRUArray& operator=(TMRUArray&&) = default;
	TMRUArray& operator=(const TMRUArray&) = default;

#else

	/**
	 * Copy constructor.
	 *
	 * @param Other Other array to copy.
	 */
	FORCEINLINE TMRUArray(const TMRUArray& Other)
		: Super((const Super&)Other)
	{
	}

	/**
	 * Assign operator.
	 *
	 * @param Other Other array to assign with.
	 *
	 * @returns Reference to this object.
	 */
	FORCEINLINE TMRUArray& operator=(const TMRUArray& Other)
	{
		(Super&)*this = (const Super&)Other;
		return *this;
	}

	/**
	 * Move constructor.
	 *
	 * @param Other Other array to move.
	 */
	FORCEINLINE TMRUArray(TMRUArray&& Other)
		: Super((TMRUArray&&)Other)
	{
	}

	/**
	 * Move assign operator.
	 *
	 * @param Other Other array to assign with.
	 *
	 * @returns Reference to this object.
	 */
	FORCEINLINE TMRUArray& operator=(TMRUArray&& Other)
	{
		(Super&)*this = (Super&&)Other;
		return *this;
	}

#endif
	/**
	 * Adds item to the array. Makes sure that we don't add more than the
	 * limit.
	 *
	 * @param Item Item to add.
	 *
	 * @returns Always 0.
	 */
	int32 Add(const T& Item)
	{
		const int32 idx = Super::Add(Item);
		this->Swap(idx, 0);
		CullArray();
		return 0;
	}

	/**
	 * Adds a number of zeroed elements to the array. Makes sure that we don't
	 * add more than the limit.
	 *
	 * @param Count (Optional) A number of elements to add. Default is 0.
	 *
	 * @returns Always 0.
	 */
	int32 AddZeroed(int32 Count = 1)
	{
		const int32 idx = Super::AddZeroed(Count);
		this->Swap(idx, 0);
		CullArray();
		return 0;
	}

	/**
	 * Adds unique item to the array. Makes sure that we don't add more than
	 * the limit. If the item existed it will be removed before addition.
	 *
	 * @param Item Element to add.
	 *
	 * @returns Always 0.
	 */
	int32 AddUnique(const T& Item)
	{
		// Remove any existing copies of the item.
		this->Remove(Item);

		this->InsertUninitialized(0);
		(*this)[0] = Item;

		CullArray();

		return 0;
	}

	/**
	 * Makes sure that the array never gets beyond MaxItems in size.
	 */
	void CullArray()
	{
		// 0 = no limit
		if (!MaxItems)
		{
			return;
		}

		while (this->Num() > MaxItems)
		{
			this->RemoveAt(this->Num() - 1, 1);
		}
	}
};

template<typename T, typename Allocator>
struct TContainerTraits<TMRUArray<T, Allocator> > : public TContainerTraitsBase<TMRUArray<T, Allocator> >
{
	enum { MoveWillEmptyContainer = TContainerTraitsBase<typename TMRUArray<T, Allocator>::Super>::MoveWillEmptyContainer };
};

/*-----------------------------------------------------------------------------
	Indirect array.
	Same as a TArray above, but stores pointers to the elements, to allow
	resizing the array index without relocating the actual elements.
-----------------------------------------------------------------------------*/
template<typename T,typename Allocator = FDefaultAllocator>
class TIndirectArray
{
public:
	typedef T                        ElementType;
	typedef TArray<void*, Allocator> InternalArrayType;

	/**
	 * Default constructors.
	 */
#if PLATFORM_COMPILER_HAS_DEFAULTED_FUNCTIONS

	TIndirectArray() = default;
	TIndirectArray(TIndirectArray&&) = default;
	TIndirectArray& operator=(TIndirectArray&&) = default;

#else

	            TIndirectArray() {}
	FORCEINLINE TIndirectArray(TIndirectArray&& Other) : Array(MoveTemp(Other.Array)) {}
	FORCEINLINE TIndirectArray& operator=(TIndirectArray&& Other) { Array = MoveTemp(Other.Array); return *this; }

#endif

	/**
	 * Copy constructor.
	 *
	 * @param Other Other array to copy from.
	 */
	TIndirectArray(const TIndirectArray& Other)
	{
		for (auto& Item : Other)
		{
			Add(new T(Item));
		}
	}

	/**
	 * Assignment operator.
	 *
	 * @param Other Other array to assign with.
	 */
	TIndirectArray& operator=(const TIndirectArray& Other)
	{
		if (&Other != this)
		{
			Empty(Other.Num());
			for (auto& Item : Other)
			{
				Add(new T(Item));
			}
		}

		return *this;
	}

	/**
	 * Destructor.
	 */
	~TIndirectArray()
	{
		Empty();
	}

	/**
	 * Gets number of elements in array.
	 *
	 * @returns Number of elements in array.
	 */
	FORCEINLINE int32 Num() const
	{
		return Array.Num();
	}

	/**
	 * Helper function for returning a typed pointer to the first array entry.
	 *
	 * @returns Pointer to first array entry or nullptr if this->ArrayMax == 0.
	 */
	DEPRECATED(4.6, "This function is deprecated as it does the same as GetData(). Please use GetData() instead.")
	FORCEINLINE T** GetTypedData()
	{
		return (T**)Array.GetData();
	}

	/**
	 * Helper function for returning a typed pointer to the first array entry.
	 *
	 * @returns Pointer to first array entry or nullptr if this->ArrayMax == 0.
	 */
	FORCEINLINE T** GetData()
	{
		return (T**)Array.GetData();
	}

	/**
	 * Helper function for returning a typed pointer to the first array entry.
	 *
	 * @returns Pointer to first array entry or nullptr if this->ArrayMax == 0.
	 */
	DEPRECATED(4.6, "This function is deprecated as it does the same as GetData(). Please use GetData() instead.")
	FORCEINLINE const T** GetTypedData() const
	{
		return (const T**)Array.GetData();
	}

	/**
	 * Helper function for returning a typed pointer to the first array entry.
	 *
	 * @returns Pointer to first array entry or nullptr if this->ArrayMax == 0.
	 */
	FORCEINLINE const T** GetData() const
	{
		return (const T**)Array.GetData();
	}

	/** 
	 * Helper function returning the size of the inner type.
	 *
	 * @returns Size in bytes of array type.
	 */
	uint32 GetTypeSize() const
	{
		return sizeof(T*);
	}

	/**
	 * Bracket array access operator.
	 *
	 * @param Index Position of element to return.
	 *
	 * @returns Reference to element in array at given position.
	 */
	FORCEINLINE T& operator[](int32 Index)
	{
		return *(T*)Array[Index];
	}

	/**
	 * Bracket array access operator.
	 *
	 * Const version.
	 *
	 * @param Index Position of element to return.
	 *
	 * @returns Reference to element in array at given position.
	 */
	FORCEINLINE const T& operator[](int32 Index) const
	{
		return *(T*)Array[Index];
	}

	/**
	 * Returns n-th last element from the array.
	 *
	 * @param IndexFromTheEnd (Optional) Index from the end of array.
	 *                        Default is 0.
	 *
	 * @returns Reference to n-th last element from the array.
	 */
	FORCEINLINE ElementType& Last(int32 IndexFromTheEnd = 0)
	{
		return *(T*)Array.Last(IndexFromTheEnd);
	}

	/**
	 * Returns n-th last element from the array.
	 *
	 * Const version.
	 *
	 * @param IndexFromTheEnd (Optional) Index from the end of array.
	 *                        Default is 0.
	 *
	 * @returns Reference to n-th last element from the array.
	 */
	FORCEINLINE const ElementType& Last(int32 IndexFromTheEnd = 0) const
	{
		return *(T*)Array.Last(IndexFromTheEnd);
	}

	/**
	 * Shrinks the array's used memory to smallest possible to store elements
	 * currently in it.
	 */
	void Shrink()
	{
		Array.Shrink();
	}

	/**
	 * Resets the array to the new given size. It calls the destructors on held
	 * items.
	 *
	 * @param NewSize (Optional) The expected usage size after calling this
	 *                function. Default is 0.
	 */
	void Reset(int32 NewSize = 0)
	{
		DestructAndFreeItems();
		Array.Reset(NewSize);
	}

	/**
	 * Special serialize function passing the owning UObject along as required
	 * by FUnytpedBulkData serialization.
	 *
	 * @param	Ar		Archive to serialize with.
	 * @param	Owner	UObject this structure is serialized within.
	 */
	void Serialize(FArchive& Ar, UObject* Owner)
	{
		CountBytes(Ar);
		if (Ar.IsLoading())
		{
			// Load array.
			int32 NewNum;
			Ar << NewNum;
			Empty(NewNum);
			for (int32 Index = 0; Index < NewNum; Index++)
			{
				new(*this) T;
			}
			for (int32 Index = 0; Index < NewNum; Index++)
			{
				(*this)[Index].Serialize(Ar, Owner, Index);
			}
		}
		else
		{
			// Save array.
			int32 Num = Array.Num();
			Ar << Num;
			for (int32 Index = 0; Index < Num; Index++)
			{
				(*this)[Index].Serialize(Ar, Owner, Index);
			}
		}
	}

	/**
	 * Serialization operator for TIndirectArray.
	 *
	 * @param	Ar		Archive to serialize with.
	 * @param	A		Array to serialize.
	 *
	 * @returns Passing down serializing archive.
	 */
	friend FArchive& operator<<(FArchive& Ar, TIndirectArray& A)
	{
		A.CountBytes(Ar);
		if (Ar.IsLoading())
		{
			// Load array.
			int32 NewNum;
			Ar << NewNum;
			A.Empty(NewNum);
			for (int32 Index = 0; Index < NewNum; Index++)
			{
				Ar << *new(A)T;
			}
		}
		else
		{
			// Save array.
			int32 Num = A.Num();
			Ar << Num;
			for (int32 Index = 0; Index < Num; Index++)
			{
				Ar << A[Index];
			}
		}
		return Ar;
	}

	/**
	 * Count bytes needed to serialize this array.
	 *
	 * @param Ar Archive to count for.
	 */
	void CountBytes(FArchive& Ar)
	{
		Array.CountBytes(Ar);
	}

	/**
	 * Removes an element (or elements) at given location optionally shrinking
	 * the array.
	 *
	 * @param Index Location in array of the element to remove.
	 * @param Count (Optional) Number of elements to remove. Default is 1.
	 * @param bAllowShrinking (Optional) Tells if this call can shrink array if
	 *                        suitable after remove. Default is true.
	 */
	void RemoveAt(int32 Index, int32 Count = 1, bool bAllowShrinking = true)
	{
		check(Index >= 0);
		check(Index <= Array.Num());
		check(Index + Count <= Array.Num());
		T** Element = GetData() + Index;
		for (int32 ElementId = Count; ElementId; --ElementId)
		{
			// We need a typedef here because VC won't compile the destructor call below if T itself has a member called T
			typedef T IndirectArrayDestructElementType;

			(*Element)->IndirectArrayDestructElementType::~IndirectArrayDestructElementType();
			FMemory::Free(*Element);
			++Element;
		}
		Array.RemoveAt(Index, Count, bAllowShrinking);
	}

	/**
	 * Removes an element (or elements) at given location optionally shrinking
	 * the array.
	 *
	 * This version is much more efficient than RemoveAt (O(Count) instead of
	 * O(ArrayNum)), but does not preserve the order.
	 *
	 * @param Index Location in array of the element to remove.
	 * @param Count (Optional) Number of elements to remove. Default is 1.
	 * @param bAllowShrinking (Optional) Tells if this call can shrink array if
	 *                        suitable after remove. Default is true.
	 */
	void RemoveAtSwap(int32 Index, int32 Count = 1, bool bAllowShrinking = true)
	{
		check(Index >= 0);
		check(Index <= Array.Num());
		check(Index + Count <= Array.Num());
		T** Element = GetData() + Index;
		for (int32 ElementId = Count; ElementId; --ElementId)
		{
			// We need a typedef here because VC won't compile the destructor call below if T itself has a member called T
			typedef T IndirectArrayDestructElementType;

			(*Element)->IndirectArrayDestructElementType::~IndirectArrayDestructElementType();
			FMemory::Free(*Element);
			++Element;
		}
		Array.RemoveAtSwap(Index, Count, bAllowShrinking);
	}

	/**
	 * Element-wise array element swap.
	 *
	 * This version is doing more sanity checks than SwapMemory.
	 *
	 * @param FirstIndexToSwap Position of the first element to swap.
	 * @param SecondIndexToSwap Position of the second element to swap.
	 */
	void Swap(int32 FirstIndexToSwap, int32 SecondIndexToSwap)
	{
		Array.Swap(FirstIndexToSwap, SecondIndexToSwap);
	}

	/**
	 * Empties the array. It calls the destructors on held items.
	 *
	 * @param Slack (Optional) The expected usage size after empty operation.
	 *              Default is 0.
	 */
	void Empty(int32 Slack = 0)
	{
		DestructAndFreeItems();
		Array.Empty(Slack);
	}

	/**
	 * Adds a new item to the end of the array, possibly reallocating the
	 * whole array to fit.
	 *
	 * @param Item The item to add.
	 *
	 * @returns Index to the new item.
	 */
	FORCEINLINE int32 Add(T* Item)
	{
		return Array.Add(Item);
	}

	/**
	 * Adds a new item to the end of the array, possibly reallocating the
	 * whole array to fit.
	 *
	 * @param Item The item to add.
	 *
	 * @returns Index to the new item.
	 */
	DEPRECATED(4.6, "This function is deprecated as it does the same as Add(). Please use Add() instead.")
	FORCEINLINE int32 AddRawItem(T* Item)
	{
		return Array.Add(Item);
	}

	/**
	 * Inserts a given element into the array at given location.
	 *
	 * @param Item The element to insert.
	 * @param Index Tells where to insert the new elements.
	 */
	FORCEINLINE void Insert(T* Item, int32 Index)
	{
		Array.Insert(Item, Index);
	}

	/**
	 * Inserts a given element into the array at given location.
	 *
	 * @param Item The element to insert.
	 * @param Index Tells where to insert the new elements.
	 */
	DEPRECATED(4.6, "This function is deprecated as it does the same as Insert(). Please use Insert() instead.")
	FORCEINLINE void InsertRawItem(T* Item, int32 Index)
	{
		Array.Insert(Item, Index);
	}

	/**
	 * Reserves memory such that the array can contain at least Number elements.
	 *
	 * @param Number The number of elements that the array should be able to
	 *               contain after allocation.
	 */
	FORCEINLINE void Reserve(int32 Number)
	{
		Array.Reserve(Number);
	}

	/**
	 * Tests if index is valid, i.e. greater than zero and less than number of
	 * elements in array.
	 *
	 * @param Index Index to test.
	 *
	 * @returns True if index is valid. False otherwise.
	 */
	FORCEINLINE bool IsValidIndex(int32 Index) const
	{
		return Array.IsValidIndex(Index);
	}

	/** 
	 * Helper function to return the amount of memory allocated by this container 
	 *
	 * @returns Number of bytes allocated by this container.
	 */
	uint32 GetAllocatedSize() const
	{
		return Array.Max() * sizeof(T*) + Array.Num() * sizeof(T);
	}

	// Iterators
	typedef TIndexedContainerIterator<      TIndirectArray,       ElementType, int32> TIterator;
	typedef TIndexedContainerIterator<const TIndirectArray, const ElementType, int32> TConstIterator;

	/**
	 * Creates an iterator for the contents of this array.
	 *
	 * @returns The iterator.
	 */
	TIterator CreateIterator()
	{
		return TIterator(*this);
	}

	/**
	 * Creates a const iterator for the contents of this array.
	 *
	 * @returns The const iterator.
	 */
	TConstIterator CreateConstIterator() const
	{
		return TConstIterator(*this);
	}

private:
	/**
	 * Calls destructor and frees memory on every element in the array.
	 */
	void DestructAndFreeItems()
	{
		T** Element = GetData();
		for (int32 Index = Array.Num(); Index; --Index)
		{
			// We need a typedef here because VC won't compile the destructor call below if T itself has a member called T
			typedef T IndirectArrayDestructElementType;

			(*Element)->IndirectArrayDestructElementType::~IndirectArrayDestructElementType();
			FMemory::Free(*Element);
			++Element;
		}
	}

	/**
	 * DO NOT USE DIRECTLY
	 * STL-like iterators to enable range-based for loop support.
	 */
	FORCEINLINE friend TDereferencingIterator<      ElementType, typename InternalArrayType::RangedForIteratorType     > begin(      TIndirectArray& IndirectArray) { return TDereferencingIterator<      ElementType, typename InternalArrayType::RangedForIteratorType     >(begin(IndirectArray.Array)); }
	FORCEINLINE friend TDereferencingIterator<const ElementType, typename InternalArrayType::RangedForConstIteratorType> begin(const TIndirectArray& IndirectArray) { return TDereferencingIterator<const ElementType, typename InternalArrayType::RangedForConstIteratorType>(begin(IndirectArray.Array)); }
	FORCEINLINE friend TDereferencingIterator<      ElementType, typename InternalArrayType::RangedForIteratorType     > end  (      TIndirectArray& IndirectArray) { return TDereferencingIterator<      ElementType, typename InternalArrayType::RangedForIteratorType     >(end  (IndirectArray.Array)); }
	FORCEINLINE friend TDereferencingIterator<const ElementType, typename InternalArrayType::RangedForConstIteratorType> end  (const TIndirectArray& IndirectArray) { return TDereferencingIterator<const ElementType, typename InternalArrayType::RangedForConstIteratorType>(end  (IndirectArray.Array)); }

	InternalArrayType Array;
};

template<typename T, typename Allocator>
struct TContainerTraits<TIndirectArray<T, Allocator> > : public TContainerTraitsBase<TIndirectArray<T, Allocator> >
{
	enum { MoveWillEmptyContainer = TContainerTraitsBase<typename TIndirectArray<T, Allocator>::InternalArrayType>::MoveWillEmptyContainer };
};

template <typename T,typename Allocator> void* operator new( size_t Size, TIndirectArray<T,Allocator>& Array )
{
	check(Size == sizeof(T));
	const int32 Index = Array.Add((T*)FMemory::Malloc(Size));
	return &Array[Index];
}

template <typename T,typename Allocator> void* operator new( size_t Size, TIndirectArray<T,Allocator>& Array, int32 Index )
{
	check(Size == sizeof(T));
	Array.Insert((T*)FMemory::Malloc(Size), Index);
	return &Array[Index];
}

/*-----------------------------------------------------------------------------
	Transactional array.
-----------------------------------------------------------------------------*/

// NOTE: Right now, you can't use a custom allocation policy with transactional arrays. If
// you need to do it, you will have to fix up FTransaction::FObjectRecord to use the correct TArray<Allocator>.
template< typename T >
class TTransArray : public TArray<T>
{
public:
	typedef TArray<T> Super;

	// Constructors.
	explicit TTransArray( UObject* InOwner )
	:	Owner( InOwner )
	{
		checkSlow(Owner);
	}
	TTransArray( UObject* InOwner, const Super& Other )
	:	Super( Other )
	,	Owner( InOwner )
	{
		checkSlow(Owner);
	}

#if PLATFORM_COMPILER_HAS_DEFAULTED_FUNCTIONS

	TTransArray(TTransArray&&) = default;
	TTransArray(const TTransArray&) = default;
	TTransArray& operator=(TTransArray&&) = default;
	TTransArray& operator=(const TTransArray&) = default;

#else

	FORCEINLINE TTransArray(const TTransArray& Other)
		: Super((const Super&)Other)
		, Owner(Other.Owner)
	{
	}

	FORCEINLINE TTransArray& operator=(const TTransArray& Other)
	{
		(Super&)*this = (const Super&)Other;
		Owner         = Other.Owner;
		return *this;
	}

	FORCEINLINE TTransArray(TTransArray&& Other)
		: Super((TTransArray&&)Other)
		, Owner(Other.Owner)
	{
	}

	FORCEINLINE TTransArray& operator=(TTransArray&& Other)
	{
		(Super&)*this = (Super&&)Other;
		Owner         = Other.Owner;
		return *this;
	}

#endif

	// Add, Insert, Remove, Empty interface.
	int32 AddUninitialized( int32 Count=1 )
	{
		const int32 Index = Super::AddUninitialized( Count );
		if( GUndo )
		{
			GUndo->SaveArray( Owner, (FScriptArray*)this, Index, Count, 1, sizeof(T), DefaultConstructItem, SerializeItem, DestructItem );
		}
		return Index;
	}
	void InsertUninitialized( int32 Index, int32 Count=1 )
	{
		Super::InsertUninitialized( Index, Count );
		if( GUndo )
		{
			GUndo->SaveArray( Owner, (FScriptArray*)this, Index, Count, 1, sizeof(T), DefaultConstructItem, SerializeItem, DestructItem );
		}
	}
	void RemoveAt( int32 Index, int32 Count=1 )
	{
		if( GUndo )
		{
			GUndo->SaveArray( Owner, (FScriptArray*)this, Index, Count, -1, sizeof(T), DefaultConstructItem, SerializeItem, DestructItem );
		}
		Super::RemoveAt( Index, Count );
	}
	void Empty( int32 Slack=0 )
	{
		if( GUndo )
		{
			GUndo->SaveArray( Owner, (FScriptArray*)this, 0, this->ArrayNum, -1, sizeof(T), DefaultConstructItem, SerializeItem, DestructItem );
		}
		Super::Empty( Slack );
	}

	// Functions dependent on Add, Remove.
	void AssignButKeepOwner( const Super& Other )
	{
		(Super&)*this = Other;
	}

	int32 Add( const T& Item )
	{
		new(*this) T(Item);
		return this->Num() - 1;
	}
	int32 AddZeroed( int32 n=1 )
	{
		const int32 Index = AddUninitialized(n);
		FMemory::Memzero(this->GetData() + Index, n*sizeof(T));
		return Index;
	}
	int32 AddUnique( const T& Item )
	{
		for( int32 Index=0; Index<this->ArrayNum; Index++ )
		{
			if( (*this)[Index]==Item )
			{
				return Index;
			}
		}
		return Add( Item );
	}
	int32 Remove( const T& Item )
	{
		this->CheckAddress(&Item);

		const int32 OriginalNum=this->ArrayNum;
		for( int32 Index=0; Index<this->ArrayNum; Index++ )
		{
			if( (*this)[Index]==Item )
			{
				RemoveAt( Index-- );
			}
		}
		return OriginalNum - this->ArrayNum;
	}

	// TTransArray interface.
	UObject* GetOwner() const
	{
		return Owner;
	}
	void SetOwner( UObject* NewOwner )
	{
		Owner = NewOwner;
	}
	void ModifyItem( int32 Index )
	{
		if( GUndo )
		{
			GUndo->SaveArray( Owner, (FScriptArray*)this, Index, 1, 0, sizeof(T), DefaultConstructItem, SerializeItem, DestructItem );
		}
	}
	void ModifyAllItems()
	{
		if( GUndo )
		{
			GUndo->SaveArray( Owner, (FScriptArray*)this, 0, this->Num(), 0, sizeof(T), DefaultConstructItem, SerializeItem, DestructItem );
		}
	}
	friend FArchive& operator<<( FArchive& Ar, TTransArray& A )
	{
		Ar << A.Owner;
		Ar << (Super&)A;
		return Ar;
	}
protected:
	static void DefaultConstructItem( void* TPtr )
	{
		new (TPtr) T;
	}
	static void SerializeItem( FArchive& Ar, void* TPtr )
	{
		Ar << *(T*)TPtr;
	}
	static void DestructItem( void* TPtr )
	{
		((T*)TPtr)->~T();
	}
	UObject* Owner;
};

template<typename T>
struct TContainerTraits<TTransArray<T> > : public TContainerTraitsBase<TTransArray<T> >
{
	enum { MoveWillEmptyContainer = TContainerTraitsBase<typename TTransArray<T>::Super>::MoveWillEmptyContainer };
};

//
// Transactional array operator news.
//
template <typename T> void* operator new( size_t Size, TTransArray<T>& Array )
{
	check(Size == sizeof(T));
	const int32 Index = Array.AddUninitialized();
	return &Array[Index];
}
template <typename T> void* operator new( size_t Size, TTransArray<T>& Array, int32 Index )
{
	check(Size == sizeof(T));
	Array.Insert(Index);
	return &Array[Index];
}
