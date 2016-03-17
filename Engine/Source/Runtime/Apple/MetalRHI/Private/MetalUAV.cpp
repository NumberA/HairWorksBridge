// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.


#include "MetalRHIPrivate.h"
#include "ShaderCache.h"

FMetalShaderResourceView::~FMetalShaderResourceView()
{
	if(TextureView)
	{
		if(TextureView->Texture)
		{
			[TextureView->Texture release];
			TextureView->Texture = nil;
			
			[TextureView->StencilTexture release];
			TextureView->StencilTexture = nil;
			
			[TextureView->MSAATexture release];
			TextureView->MSAATexture = nil;
		}
		delete TextureView;
		TextureView = nullptr;
	}
	FShaderCache::RemoveSRV(this);
	SourceVertexBuffer = NULL;
	SourceTexture = NULL;
}

void FMetalUnorderedAccessView::Set(FMetalContext* Context, uint32 ResourceIndex)
{
	// figure out which one of the resources we need to set
	FMetalStructuredBuffer* StructuredBuffer = SourceStructuredBuffer.GetReference();
	FMetalVertexBuffer* VertexBuffer = SourceVertexBuffer.GetReference();
	FRHITexture* Texture = SourceTexture.GetReference();
	if (StructuredBuffer)
	{
		Context->GetCommandEncoder().SetShaderBuffer(SF_Compute, StructuredBuffer->Buffer, 0, ResourceIndex);
	}
	else if (VertexBuffer)
	{
		Context->GetCommandEncoder().SetShaderBuffer(SF_Compute, VertexBuffer->Buffer, 0, ResourceIndex);
	}
	else if (Texture)
	{
		FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(Texture);
		if (Surface != nullptr)
		{
			Context->GetCommandEncoder().SetShaderTexture(SF_Compute, Surface->Texture, ResourceIndex);
		}
		else
		{
			Context->GetCommandEncoder().SetShaderTexture(SF_Compute, nil, ResourceIndex);
		}
	}

}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FStructuredBufferRHIParamRef StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer)
{
	return GDynamicRHI->RHICreateUnorderedAccessView(StructuredBuffer, bUseUAVCounter, bAppendBuffer);
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FTextureRHIParamRef Texture, uint32 MipLevel)
{
	return GDynamicRHI->RHICreateUnorderedAccessView(Texture, MipLevel);
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FVertexBufferRHIParamRef VertexBuffer, uint8 Format)
{
	return GDynamicRHI->RHICreateUnorderedAccessView(VertexBuffer, Format);
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView(FStructuredBufferRHIParamRef StructuredBufferRHI, bool bUseUAVCounter, bool bAppendBuffer)
{
	FMetalStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);

	// create the UAV buffer to point to the structured buffer's memory
	FMetalUnorderedAccessView* UAV = new FMetalUnorderedAccessView;
	UAV->SourceStructuredBuffer = StructuredBuffer;

	return UAV;
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView(FTextureRHIParamRef TextureRHI, uint32 MipLevel)
{
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(TextureRHI);

	// create the UAV buffer to point to the structured buffer's memory
	FMetalUnorderedAccessView* UAV = new FMetalUnorderedAccessView;
	UAV->SourceTexture = (FRHITexture*)TextureRHI;

	return UAV;
}

FUnorderedAccessViewRHIRef FMetalDynamicRHI::RHICreateUnorderedAccessView(FVertexBufferRHIParamRef VertexBufferRHI, uint8 Format)
{
	FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	// create the UAV buffer to point to the structured buffer's memory
	FMetalUnorderedAccessView* UAV = new FMetalUnorderedAccessView;
	UAV->SourceVertexBuffer = VertexBuffer;

	return UAV;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FTexture2DRHIParamRef Texture2DRHI, uint8 MipLevel)
{
#if PLATFORM_MAC
	FMetalTexture2D* Texture = ResourceCast(Texture2DRHI);
	id<MTLTexture> Tex = Texture->Surface.Texture;
	if (!(Tex.usage & MTLTextureUsagePixelFormatView))
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return GDynamicRHI->RHICreateShaderResourceView(Texture2DRHI, MipLevel);
	}
	else
#endif
	{
		return GDynamicRHI->RHICreateShaderResourceView(Texture2DRHI, MipLevel);
	}
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FTexture2DRHIParamRef Texture2DRHI, uint8 MipLevel, uint8 NumMipLevels, uint8 Format)
{
#if PLATFORM_MAC
	FMetalTexture2D* Texture = ResourceCast(Texture2DRHI);
	id<MTLTexture> Tex = Texture->Surface.Texture;
	if (!(Tex.usage & MTLTextureUsagePixelFormatView))
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return GDynamicRHI->RHICreateShaderResourceView(Texture2DRHI, MipLevel, NumMipLevels, Format);
	}
	else
#endif
	{
		return GDynamicRHI->RHICreateShaderResourceView(Texture2DRHI, MipLevel, NumMipLevels, Format);
	}
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FTexture3DRHIParamRef Texture3DRHI, uint8 MipLevel)
{
#if PLATFORM_MAC
	FMetalTexture3D* Texture = ResourceCast(Texture3DRHI);
	id<MTLTexture> Tex = Texture->Surface.Texture;
	if (!(Tex.usage & MTLTextureUsagePixelFormatView))
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return GDynamicRHI->RHICreateShaderResourceView(Texture3DRHI, MipLevel);
	}
	else
#endif
	{
		return GDynamicRHI->RHICreateShaderResourceView(Texture3DRHI, MipLevel);
	}
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FTexture2DArrayRHIParamRef Texture2DArrayRHI, uint8 MipLevel)
{
#if PLATFORM_MAC
	FMetalTexture2DArray* Texture = ResourceCast(Texture2DArrayRHI);
	id<MTLTexture> Tex = Texture->Surface.Texture;
	if (!(Tex.usage & MTLTextureUsagePixelFormatView))
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return GDynamicRHI->RHICreateShaderResourceView(Texture2DArrayRHI, MipLevel);
	}
	else
#endif
	{
		return GDynamicRHI->RHICreateShaderResourceView(Texture2DArrayRHI, MipLevel);
	}
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FTextureCubeRHIParamRef TextureCubeRHI, uint8 MipLevel)
{
#if PLATFORM_MAC
	FMetalTextureCube* Texture = ResourceCast(TextureCubeRHI);
	id<MTLTexture> Tex = Texture->Surface.Texture;
	if (!(Tex.usage & MTLTextureUsagePixelFormatView))
	{
		FScopedRHIThreadStaller StallRHIThread(RHICmdList);
		return GDynamicRHI->RHICreateShaderResourceView(TextureCubeRHI, MipLevel);
	}
	else
#endif
	{
		return GDynamicRHI->RHICreateShaderResourceView(TextureCubeRHI, MipLevel);
	}
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FVertexBufferRHIParamRef VertexBuffer, uint32 Stride, uint8 Format)
{
	return GDynamicRHI->RHICreateShaderResourceView(VertexBuffer, Stride, Format);
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FStructuredBufferRHIParamRef StructuredBuffer)
{
	UE_LOG(LogRHI, Fatal,TEXT("Metal RHI doesn't support RHICreateShaderResourceView with FStructuredBufferRHIParamRef yet!"));
	return GDynamicRHI->RHICreateShaderResourceView(StructuredBuffer);
}


FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FStructuredBufferRHIParamRef StructuredBufferRHI)
{
	FMetalStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);

	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->TextureView = nullptr;
	UE_LOG(LogRHI, Fatal,TEXT("Metal RHI doesn't support RHICreateShaderResourceView with FStructuredBufferRHIParamRef yet!"));
	return SRV;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FVertexBufferRHIParamRef VertexBufferRHI, uint32 Stride, uint8 Format)
{
	FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceVertexBuffer = VertexBuffer;
	SRV->TextureView = nullptr;
	
	FShaderCache::LogSRV(SRV, VertexBufferRHI, Stride, Format);
	
	return SRV;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FTexture2DRHIParamRef Texture2DRHI, uint8 MipLevel)
{
    FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceTexture = (FRHITexture*)Texture2DRHI;
	
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(Texture2DRHI);
	SRV->TextureView = Surface ? new FMetalSurface(*Surface, NSMakeRange(MipLevel, 1)) : nullptr;
	
	SRV->MipLevel = MipLevel;
	SRV->NumMips = 1;
	SRV->Format = PF_Unknown;
	
	FShaderCache::LogSRV(SRV, Texture2DRHI, MipLevel, SRV->NumMips, SRV->Format);
	
	return SRV;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FTexture2DRHIParamRef Texture2DRHI, uint8 MipLevel, uint8 NumMipLevels, uint8 Format)
{
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceTexture = (FRHITexture*)Texture2DRHI;
	
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(Texture2DRHI);
	SRV->TextureView = Surface ? new FMetalSurface(*Surface, NSMakeRange(MipLevel, NumMipLevels), (EPixelFormat)Format) : nullptr;
	
	SRV->MipLevel = MipLevel;
	SRV->NumMips = NumMipLevels;
	SRV->Format = Format;
	
	FShaderCache::LogSRV(SRV, Texture2DRHI, MipLevel, NumMipLevels, Format);
	return SRV;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FTexture3DRHIParamRef Texture3DRHI, uint8 MipLevel)
{
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceTexture = (FRHITexture*)Texture3DRHI;
	
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(Texture3DRHI);
	SRV->TextureView = Surface ? new FMetalSurface(*Surface, NSMakeRange(MipLevel, 1)) : nullptr;
	
	SRV->MipLevel = MipLevel;
	SRV->NumMips = 1;
	SRV->Format = PF_Unknown;
	
	FShaderCache::LogSRV(SRV, Texture3DRHI, MipLevel, SRV->NumMips, SRV->Format);
	return SRV;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FTexture2DArrayRHIParamRef Texture2DArrayRHI, uint8 MipLevel)
{
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceTexture = (FRHITexture*)Texture2DArrayRHI;
	
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(Texture2DArrayRHI);
	SRV->TextureView = Surface ? new FMetalSurface(*Surface, NSMakeRange(MipLevel, 1)) : nullptr;
	
	SRV->MipLevel = MipLevel;
	SRV->NumMips = 1;
	SRV->Format = PF_Unknown;
	
	FShaderCache::LogSRV(SRV, Texture2DArrayRHI, MipLevel, SRV->NumMips, SRV->Format);
	return SRV;
}

FShaderResourceViewRHIRef FMetalDynamicRHI::RHICreateShaderResourceView(FTextureCubeRHIParamRef TextureCubeRHI, uint8 MipLevel)
{
	FMetalShaderResourceView* SRV = new FMetalShaderResourceView;
	SRV->SourceTexture = (FRHITexture*)TextureCubeRHI;
	
	FMetalSurface* Surface = GetMetalSurfaceFromRHITexture(TextureCubeRHI);
	SRV->TextureView = Surface ? new FMetalSurface(*Surface, NSMakeRange(MipLevel, 1)) : nullptr;
	
	SRV->MipLevel = MipLevel;
	SRV->NumMips = 1;
	SRV->Format = PF_Unknown;
	
	FShaderCache::LogSRV(SRV, TextureCubeRHI, MipLevel, SRV->NumMips, SRV->Format);
	return SRV;
}

void FMetalRHICommandContext::RHIClearUAV(FUnorderedAccessViewRHIParamRef UnorderedAccessViewRHI, const uint32* Values)
{
	FMetalUnorderedAccessView* UnorderedAccessView = ResourceCast(UnorderedAccessViewRHI);
	if (UnorderedAccessView->SourceStructuredBuffer)
	{
	}
	else if (UnorderedAccessView->SourceTexture)
	{
	}
	else
	{
		check(UnorderedAccessView->SourceVertexBuffer);
		
		// Fill the buffer via a blit encoder - I hope that is sufficient.
		id<MTLBlitCommandEncoder> Blitter = Context->GetBlitContext();
		[Blitter fillBuffer:UnorderedAccessView->SourceVertexBuffer->Buffer range:NSMakeRange(0, UnorderedAccessView->SourceVertexBuffer->GetSize()) value:Values[0]];
		
		// If there are problems you may need to add calls to restore the render command encoder at this point
		// but we don't generally want to do that.
	}
}
