// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ForwardShadingRenderer.cpp: Scene rendering code for the ES2 feature level.
=============================================================================*/

#include "RendererPrivate.h"
#include "Engine.h"
#include "ScenePrivate.h"
#include "FXSystem.h"
#include "PostProcessing.h"
#include "SceneFilterRendering.h"
#include "PostProcessMobile.h"
#include "SceneUtils.h"
#include "PostProcessUpscale.h"
#include "PostProcessCompositeEditorPrimitives.h"

uint32 GetShadowQuality();


FForwardShadingSceneRenderer::FForwardShadingSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer)
	:	FSceneRenderer(InViewFamily, HitProxyConsumer)
{
	bModulatedShadowsInUse = false;
	bCSMShadowsInUse = false;
}

/**
 * Initialize scene's views.
 * Check visibility, sort translucent items, etc.
 */
void FForwardShadingSceneRenderer::InitViews(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_DRAW_EVENT(RHICmdList, InitViews);

	SCOPE_CYCLE_COUNTER(STAT_InitViewsTime);

	FILCUpdatePrimTaskData ILCTaskData;
	PreVisibilityFrameSetup(RHICmdList);
	ComputeViewVisibility(RHICmdList);
	PostVisibilityFrameSetup(ILCTaskData);

	bool bDynamicShadows = ViewFamily.EngineShowFlags.DynamicShadows && GetShadowQuality() > 0;

	if (bDynamicShadows && !IsSimpleDynamicLightingEnabled())
	{
		// Setup dynamic shadows.
		InitDynamicShadows(RHICmdList);		
	}

	// if we kicked off ILC update via task, wait and finalize.
	if (ILCTaskData.TaskRef.IsValid())
	{
		Scene->IndirectLightingCache.FinalizeCacheUpdates(Scene, *this, ILCTaskData);
	}

	// initialize per-view uniform buffer.  Pass in shadow info as necessary.
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>* DirectionalLightShadowInfo = nullptr;

		FViewInfo& ViewInfo = Views[ViewIndex];
		FScene* Scene = (FScene*)ViewInfo.Family->Scene;
		if (bDynamicShadows && Scene->SimpleDirectionalLight)
		{
			int32 LightId = Scene->SimpleDirectionalLight->Id;
			if (VisibleLightInfos.IsValidIndex(LightId))
			{
				const FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightId];
				if (VisibleLightInfo.AllProjectedShadows.Num() > 0)
				{
					DirectionalLightShadowInfo = &VisibleLightInfo.AllProjectedShadows;
				}
			}
		}

		// Initialize the view's RHI resources.
		Views[ViewIndex].InitRHIResources(DirectionalLightShadowInfo);
	}

	// Now that the indirect lighting cache is updated, we can update the primitive precomputed lighting buffers.
	UpdatePrimitivePrecomputedLightingBuffers();
	
	OnStartFrame();
}

/** 
* Renders the view family. 
*/
void FForwardShadingSceneRenderer::Render(FRHICommandListImmediate& RHICmdList)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FForwardShadingSceneRenderer_Render);

	if(!ViewFamily.EngineShowFlags.Rendering)
	{
		return;
	}

	auto FeatureLevel = ViewFamily.GetFeatureLevel();

	// Initialize global system textures (pass-through if already initialized).
	GSystemTextures.InitializeTextures(RHICmdList, FeatureLevel);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	// Allocate the maximum scene render target space for the current view family.
	SceneContext.Allocate(RHICmdList, ViewFamily);

	//make sure all the targets we're going to use will be safely writable.
	GRenderTargetPool.TransitionTargetsWritable(RHICmdList);

	// Find the visible primitives.
	InitViews(RHICmdList);
	
	// Notify the FX system that the scene is about to be rendered.
	if (Scene->FXSystem)
	{
		Scene->FXSystem->PreRender(RHICmdList, NULL);
	}

	GRenderTargetPool.VisualizeTexture.OnStartFrame(Views[0]);

	RenderShadowDepthMaps(RHICmdList);

	// Dynamic vertex and index buffers need to be committed before rendering.
	FGlobalDynamicVertexBuffer::Get().Commit();
	FGlobalDynamicIndexBuffer::Get().Commit();

	// This might eventually be a problem with multiple views.
	// Using only view 0 to check to do on-chip transform of alpha.
	FViewInfo& View = Views[0];

	const bool bGammaSpace = !IsMobileHDR();
	const bool bRequiresUpscale = !ViewFamily.bUseSeparateRenderTarget && ((uint32)ViewFamily.RenderTarget->GetSizeXY().X > ViewFamily.FamilySizeX || (uint32)ViewFamily.RenderTarget->GetSizeXY().Y > ViewFamily.FamilySizeY);
	// ES2 requires that the back buffer and depth match dimensions.
	// For the most part this is not the case when using scene captures. Thus scene captures always render to scene color target.
	const bool bRenderToScene = bRequiresUpscale || FSceneRenderer::ShouldCompositeEditorPrimitives(View) || View.bIsSceneCapture;

	if (bGammaSpace && !bRenderToScene)
	{
		SetRenderTarget(RHICmdList, ViewFamily.RenderTarget->GetRenderTargetTexture(), SceneContext.GetSceneDepthTexture(), ESimpleRenderTargetMode::EClearColorAndDepth);
	}
	else
	{
		// Begin rendering to scene color
		SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EClearColorAndDepth);
	}

	if (GIsEditor)
	{
		RHICmdList.Clear(true, Views[0].BackgroundColor, false, (float)ERHIZBuffer::FarPlane, false, 0, FIntRect());
	}

	RenderForwardShadingBasePass(RHICmdList);

	// Make a copy of the scene depth if the current hardware doesn't support reading and writing to the same depth buffer
	ConditionalResolveSceneDepth(RHICmdList);
	
	if (ViewFamily.EngineShowFlags.Decals)
	{
		RenderDecals(RHICmdList);
	}

	// Notify the FX system that opaque primitives have been rendered.
	if (Scene->FXSystem)
	{
		Scene->FXSystem->PostRenderOpaque(RHICmdList);
	}

	RenderModulatedShadowProjections(RHICmdList);

	// Draw translucency.
	if (ViewFamily.EngineShowFlags.Translucency)
	{
		SCOPE_CYCLE_COUNTER(STAT_TranslucencyDrawTime);

		// Note: Forward pass has no SeparateTranslucency, so refraction effect order with Transluency is different.
		// Having the distortion applied between two different translucency passes would make it consistent with the deferred pass.
		// This is not done yet.

		if (GetRefractionQuality(ViewFamily) > 0)
		{
			// to apply refraction effect by distorting the scene color
			RenderDistortionES2(RHICmdList);
		}
		RenderTranslucency(RHICmdList);
	}

	static const auto CVarMobileMSAA = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileMSAA"));
	bool bOnChipSunMask =
		GSupportsRenderTargetFormat_PF_FloatRGBA &&
		GSupportsShaderFramebufferFetch &&
		ViewFamily.EngineShowFlags.PostProcessing &&
		((View.bLightShaftUse) || (View.FinalPostProcessSettings.DepthOfFieldScale > 0.0) ||
		((ViewFamily.GetShaderPlatform() == SP_METAL) && (CVarMobileMSAA ? CVarMobileMSAA->GetValueOnAnyThread() > 1 : false))
		);

	if (!bGammaSpace && bOnChipSunMask)
	{
		// Convert alpha from depth to circle of confusion with sunshaft intensity.
		// This is done before resolve on hardware with framebuffer fetch.
		// This will break when PrePostSourceViewportSize is not full size.
		FIntPoint PrePostSourceViewportSize = SceneContext.GetBufferSizeXY();

		FMemMark Mark(FMemStack::Get());
		FRenderingCompositePassContext CompositeContext(RHICmdList, View);

		FRenderingCompositePass* PostProcessSunMask = CompositeContext.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessSunMaskES2(PrePostSourceViewportSize, true));
		CompositeContext.Process(PostProcessSunMask, TEXT("OnChipAlphaTransform"));
	}

	if (!bGammaSpace || bRenderToScene)
	{
		// Resolve the scene color for post processing.
		SceneContext.ResolveSceneColor(RHICmdList, FResolveRect(0, 0, ViewFamily.FamilySizeX, ViewFamily.FamilySizeY));

		// Drop depth and stencil before post processing to avoid export.
		RHICmdList.DiscardRenderTargets(true, true, 0);
	}

	if (!bGammaSpace)
	{
		// Finish rendering for each view, or the full stereo buffer if enabled
		if (ViewFamily.bResolveScene)
		{
			{
				SCOPED_DRAW_EVENT(RHICmdList, PostProcessing);
				SCOPE_CYCLE_COUNTER(STAT_FinishRenderViewTargetTime);
				for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
				{
					SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);
					GPostProcessing.ProcessES2(RHICmdList, Views[ViewIndex], bOnChipSunMask);
				}
			}
		}
	}
	else if (bRenderToScene)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			BasicPostProcess(RHICmdList, Views[ViewIndex], bRequiresUpscale, FSceneRenderer::ShouldCompositeEditorPrimitives(Views[ViewIndex]));
		}
	}
	RenderFinish(RHICmdList);
}

// Perform simple upscale and/or editor primitive composite if the fully-featured post process is not in use.
void FForwardShadingSceneRenderer::BasicPostProcess(FRHICommandListImmediate& RHICmdList, FViewInfo &View, bool bDoUpscale, bool bDoEditorPrimitives)
{
	FRenderingCompositePassContext CompositeContext(RHICmdList, View);
	FPostprocessContext Context(RHICmdList, CompositeContext.Graph, View);

	const bool bBlitRequired = !bDoUpscale && !bDoEditorPrimitives;

	if (bDoUpscale || bBlitRequired)
	{	// blit from sceneRT to view family target, simple bilinear if upscaling otherwise point filtered.
		uint32 UpscaleQuality = bDoUpscale ? 1 : 0;
		FRenderingCompositePass* Node = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessUpscale(UpscaleQuality));

		Node->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
		Node->SetInput(ePId_Input1, FRenderingCompositeOutputRef(Context.FinalOutput));

		Context.FinalOutput = FRenderingCompositeOutputRef(Node);
	}

#if WITH_EDITOR
	// Composite editor primitives if we had any to draw and compositing is enabled
	if (bDoEditorPrimitives)
	{
		FRenderingCompositePass* EditorCompNode = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessCompositeEditorPrimitives(false));
		EditorCompNode->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
		//Node->SetInput(ePId_Input1, FRenderingCompositeOutputRef(Context.SceneDepth));
		Context.FinalOutput = FRenderingCompositeOutputRef(EditorCompNode);
	}
#endif

	// currently created on the heap each frame but View.Family->RenderTarget could keep this object and all would be cleaner
	TRefCountPtr<IPooledRenderTarget> Temp;
	FSceneRenderTargetItem Item;
	Item.TargetableTexture = (FTextureRHIRef&)View.Family->RenderTarget->GetRenderTargetTexture();
	Item.ShaderResourceTexture = (FTextureRHIRef&)View.Family->RenderTarget->GetRenderTargetTexture();

	FPooledRenderTargetDesc Desc;

	Desc.Extent = View.Family->RenderTarget->GetSizeXY();
	// todo: this should come from View.Family->RenderTarget
	Desc.Format = PF_B8G8R8A8;
	Desc.NumMips = 1;

	GRenderTargetPool.CreateUntrackedElement(Desc, Temp, Item);

	Context.FinalOutput.GetOutput()->PooledRenderTarget = Temp;
	Context.FinalOutput.GetOutput()->RenderTargetDesc = Desc;

	CompositeContext.Process(Context.FinalOutput.GetPass(), TEXT("ES2BasicPostProcess"));
}

void FForwardShadingSceneRenderer::ConditionalResolveSceneDepth(FRHICommandListImmediate& RHICmdList)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	
	SceneContext.ResolveSceneDepthToAuxiliaryTexture(RHICmdList);

#if !PLATFORM_HTML5
	auto ShaderPlatform = ViewFamily.GetShaderPlatform();

	if (IsMobileHDR() 
		&& IsMobilePlatform(ShaderPlatform) 
		&& !IsPCPlatform(ShaderPlatform)) // exclude mobile emulation on PC
	{
		bool bSceneDepthInAlpha = (SceneContext.GetSceneColor()->GetDesc().Format == PF_FloatRGBA);
		bool bOnChipDepthFetch = (GSupportsShaderDepthStencilFetch || (bSceneDepthInAlpha && GSupportsShaderFramebufferFetch));
		
		if (!bOnChipDepthFetch)
		{
			// Only these features require depth texture
			bool bDecals = ViewFamily.EngineShowFlags.Decals && Scene->Decals.Num();
			bool bModulatedShadows = ViewFamily.EngineShowFlags.DynamicShadows && GetShadowQuality() > 0 && bModulatedShadowsInUse;

			if (bDecals || bModulatedShadows)
			{
				// Switch target to force hardware flush current depth to texture
				FTextureRHIRef DummySceneColor = GSystemTextures.BlackDummy->GetRenderTargetItem().TargetableTexture;
				FTextureRHIRef DummyDepthTarget = GSystemTextures.DepthDummy->GetRenderTargetItem().TargetableTexture;
				SetRenderTarget(RHICmdList, DummySceneColor, DummyDepthTarget, ESimpleRenderTargetMode::EUninitializedColorClearDepth, FExclusiveDepthStencil::DepthWrite_StencilWrite);
				RHICmdList.DiscardRenderTargets(true, true, 0);
			}
		}
	}
#endif //!PLATFORM_HTML5
}