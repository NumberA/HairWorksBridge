// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalConstantBuffer.cpp: Metal Constant buffer implementation.
=============================================================================*/

#include "MetalRHIPrivate.h"
#include "MetalProfiler.h"

#define NUM_POOL_BUCKETS 29

static const uint32 RequestedUniformBufferSizeBuckets[NUM_POOL_BUCKETS] =
{
	256, 512, 768, 1024,		// 256-byte increments
	1280, 1536, 1792, 2048,		// 256-byte increments
	2560, 3072, 3584, 4096,		// 512-byte increments
	5120, 6144, 7168, 8192,		// 1024-byte increments
	10240, 12288, 14336, 16384,	// 2048-byte increments
	20480, 24576, 28672, 32768,	// 4096-byte increments
	40960, 49152, 57344, 65536,	// 8192-byte increments

	// 65536 is current max uniform buffer size for Mac OS X.

	0xFFFFFFFF
};

// Maps desired size buckets to alignment actually 
static TArray<uint32> UniformBufferSizeBuckets;

static uint32 GetUBPoolSize()
{
	return 512 * 1024;
}

// Convert bucket sizes to be compatible with present device
static void RemapBuckets()
{
	const uint32 Alignment = 256;

	for (int32 Count = 0; Count < NUM_POOL_BUCKETS; Count++)
	{
		uint32 AlignedSize = ((RequestedUniformBufferSizeBuckets[Count] + Alignment - 1) / Alignment ) * Alignment;
		if (!UniformBufferSizeBuckets.Contains(AlignedSize))
		{
			UniformBufferSizeBuckets.Push(AlignedSize);
		}
	}
}

static uint32 GetPoolBucketIndex(uint32 NumBytes)
{
	if (UniformBufferSizeBuckets.Num() == 0)
	{
		RemapBuckets();
	}

	unsigned long Lower = 0;
	unsigned long Upper = UniformBufferSizeBuckets.Num();

	do
	{
		unsigned long Middle = (Upper + Lower) >> 1;
		if (NumBytes <= UniformBufferSizeBuckets[Middle - 1])
		{
			Upper = Middle;
		}
		else
		{
			Lower = Middle;
		}
	}
	while (Upper - Lower > 1);

	check(NumBytes <= UniformBufferSizeBuckets[Lower]);
	check((Lower == 0) || (NumBytes > UniformBufferSizeBuckets[Lower - 1]));

	return Lower;
}


// Describes a uniform buffer in the free pool.
struct FPooledUniformBuffer
{
	id<MTLBuffer> Buffer;
	uint32 CreatedSize;
	uint32 FrameFreed;
	uint32 Offset;
};

// Pool of free uniform buffers, indexed by bucket for constant size search time.
TArray<FPooledUniformBuffer> UniformBufferPool[NUM_POOL_BUCKETS];

// Uniform buffers that have been freed more recently than NumSafeFrames ago.
TArray<FPooledUniformBuffer> SafeUniformBufferPools[NUM_SAFE_FRAMES][NUM_POOL_BUCKETS];

static FCriticalSection GMutex;

// Does per-frame global updating for the uniform buffer pool.
void InitFrame_UniformBufferPoolCleanup()
{
	check(IsInRenderingThread() || IsInRHIThread());

	SCOPE_CYCLE_COUNTER(STAT_MetalUniformBufferCleanupTime);
	
	if(GUseRHIThread)
	{
		GMutex.Lock();
	}

	// Index of the bucket that is now old enough to be reused
	const int32 SafeFrameIndex = GFrameNumberRenderThread % NUM_SAFE_FRAMES;

	// Merge the bucket into the free pool array
	for (int32 BucketIndex = 0; BucketIndex < NUM_POOL_BUCKETS; BucketIndex++)
	{
		UniformBufferPool[BucketIndex].Append(SafeUniformBufferPools[SafeFrameIndex][BucketIndex]);
		SafeUniformBufferPools[SafeFrameIndex][BucketIndex].Reset();
	}
	
	if(GUseRHIThread)
	{
		GMutex.Unlock();
	}
}

struct TUBPoolBuffer
{
	id<MTLBuffer> Buffer;
	uint32 ConsumedSpace;
	uint32 AllocatedSpace;
};

TArray<TUBPoolBuffer> UBPool;

void AddNewlyFreedBufferToUniformBufferPool(id<MTLBuffer> Buffer, uint32 Offset, uint32 Size)
{
	check(Buffer);

	if(GUseRHIThread)
	{
		GMutex.Lock();
	}
	
	FPooledUniformBuffer NewEntry;
	NewEntry.Buffer = Buffer;
	NewEntry.FrameFreed = GFrameNumberRenderThread;
	NewEntry.CreatedSize = Size;
	NewEntry.Offset = Offset;

	// Add to this frame's array of free uniform buffers
	const int32 SafeFrameIndex = (GFrameNumberRenderThread - 1) % NUM_SAFE_FRAMES;
	const uint32 BucketIndex = GetPoolBucketIndex(Size);

	SafeUniformBufferPools[SafeFrameIndex][BucketIndex].Add(NewEntry);
	INC_DWORD_STAT(STAT_MetalNumFreeUniformBuffers);
	INC_MEMORY_STAT_BY(STAT_MetalFreeUniformBufferMemory, Buffer.length);
	
	if(GUseRHIThread)
	{
		GMutex.Unlock();
	}
}


id<MTLBuffer> SuballocateUB(uint32 Size, uint32& OutOffset)
{
	check(Size <= GetUBPoolSize());

	// Find space in previously allocated pool buffers
	for ( int32 Buffer = 0; Buffer < UBPool.Num(); Buffer++)
	{
		TUBPoolBuffer &Pool = UBPool[Buffer];
		if ( Size < (Pool.AllocatedSpace - Pool.ConsumedSpace))
		{
			OutOffset = Pool.ConsumedSpace;
			Pool.ConsumedSpace += Size;
			return Pool.Buffer;
		}
	}

	// No space was found to use, create a new Pool buffer
	uint32 TotalSize = GetUBPoolSize();
	//NSLog(@"New Metal Buffer Size %d", TotalSize);
	id<MTLBuffer> Buffer = [GetMetalDeviceContext().GetDevice() newBufferWithLength:TotalSize options:BUFFER_CACHE_MODE];
	TRACK_OBJECT(Buffer);

	OutOffset = 0;

	TUBPoolBuffer Pool;
	Pool.Buffer = Buffer;
	Pool.ConsumedSpace = Size;
	Pool.AllocatedSpace = GetUBPoolSize();
	UBPool.Push(Pool);

	return Buffer;
}


FMetalUniformBuffer::FMetalUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage)
	: FRHIUniformBuffer(Layout)
	, Buffer(nil)
	, Offset(0)
	, Size(Layout.ConstantBufferSize)
	, LastCachedFrame(INDEX_NONE)
{
	if (Layout.ConstantBufferSize > 0)
	{
		if(Layout.ConstantBufferSize <= 65536)
		{
			// for single use buffers, allocate from the ring buffer to avoid thrashing memory
			if (Usage == UniformBuffer_SingleDraw && !GUseRHIThread) // @todo Make this properly RHIThread safe.
			{
				// use a bit of the ring buffer
				Offset = GetMetalDeviceContext().AllocateFromRingBuffer(Layout.ConstantBufferSize);
				Buffer = GetMetalDeviceContext().GetRingBuffer();
			}
			else
			{
				// Find the appropriate bucket based on size
				if(GUseRHIThread)
				{
					GMutex.Lock();
				}

				const uint32 BucketIndex = GetPoolBucketIndex(Layout.ConstantBufferSize);
				TArray<FPooledUniformBuffer>& PoolBucket = UniformBufferPool[BucketIndex];
				if (PoolBucket.Num() > 0)
				{
					// Reuse the last entry in this size bucket
					FPooledUniformBuffer FreeBufferEntry = PoolBucket.Pop();
					DEC_DWORD_STAT(STAT_MetalNumFreeUniformBuffers);
					DEC_MEMORY_STAT_BY(STAT_MetalFreeUniformBufferMemory, FreeBufferEntry.CreatedSize);

					// reuse the one
					Buffer = FreeBufferEntry.Buffer;
					Offset = FreeBufferEntry.Offset;
				}
				else
				{
					// Nothing usable was found in the free pool, create a new uniform buffer (full size, not NumBytes)
					uint32 BufferSize = UniformBufferSizeBuckets[BucketIndex];
					Buffer = SuballocateUB(BufferSize, Offset);
				}
				
				if(GUseRHIThread)
				{
					GMutex.Unlock();
				}
			}
		}
		else
		{
			UE_LOG(LogMetal, Fatal, TEXT("Trying to allocated a uniform layout of size %d that is greater than the maximum permitted 64k."), Size);
		}

		// copy the contents
		FMemory::Memcpy(((uint8*)[Buffer contents]) + Offset, Contents, Layout.ConstantBufferSize);
	}

	// set up an SRT-style uniform buffer
	if (Layout.Resources.Num())
	{
		int32 NumResources = Layout.Resources.Num();
		FRHIResource** InResources = (FRHIResource**)((uint8*)Contents + Layout.ResourceOffset);
		ResourceTable.Empty(NumResources);
		ResourceTable.AddZeroed(NumResources);
		for (int32 i = 0; i < NumResources; ++i)
		{
			check(InResources[i]);
			ResourceTable[i] = InResources[i];
		}
		RawResourceTable.Empty(NumResources);
		RawResourceTable.AddZeroed(NumResources);
	}
}

FMetalUniformBuffer::~FMetalUniformBuffer()
{
	// don't need to free the ring buffer!
	if (GIsRHIInitialized && Buffer != nil && Buffer != GetMetalDeviceContext().GetRingBuffer())
	{
		check(Size <= 65536);
		AddNewlyFreedBufferToUniformBufferPool(Buffer, Offset, Size);
	}
}

void FMetalUniformBuffer::CacheResourcesInternal()
{
	const FRHIUniformBufferLayout& Layout = GetLayout();
	int32 NumResources = Layout.Resources.Num();
	const uint8* RESTRICT ResourceTypes = Layout.Resources.GetData();
	const TRefCountPtr<FRHIResource>* RESTRICT Resources = ResourceTable.GetData();
	void** RESTRICT RawResources = RawResourceTable.GetData();
	float CurrentTime = FApp::GetCurrentTime();

	// todo: Immutable resources, i.e. not textures, can be safely cached across frames.
	// Texture streaming makes textures complicated :)
	for (int32 i = 0; i < NumResources; ++i)
	{
		switch (ResourceTypes[i])
		{
			case UBMT_SRV:
				{
					NOT_SUPPORTED("FMetalUniformBuffer::CacheResourcesInternal UBMT_SRV");
					
					FMetalShaderResourceView* SRV = (FMetalShaderResourceView*)Resources[i].GetReference();
					if (IsValidRef(SRV->SourceTexture))
					{
						FMetalSurface* Surface = SRV->TextureView;
						RawResources[i] = Surface;
					}
					else
					{
						RawResources[i] = &SRV->SourceVertexBuffer->Buffer;
					}
				}
				break;

			case UBMT_TEXTURE:
				{
					FRHITexture* TextureRHI = (FRHITexture*)Resources[i].GetReference();
					TextureRHI->SetLastRenderTime(CurrentTime);
					RawResources[i] = TextureRHI;
				}
				break;

			case UBMT_UAV:
				NOT_SUPPORTED("FMetalUniformBuffer::CacheResourcesInternal UBMT_UAV");
				RawResources[i] = 0;
				break;

			case UBMT_SAMPLER:
				RawResources[i] = (FMetalSamplerState*)Resources[i].GetReference();
				break;

			default:
				check(0);
				break;
		}
	}
}



FUniformBufferRHIRef FMetalDynamicRHI::RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage)
{
	check(IsInRenderingThread() || IsInRHIThread());
	return new FMetalUniformBuffer(Contents, Layout, Usage);
}
