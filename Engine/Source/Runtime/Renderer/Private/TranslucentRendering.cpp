// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TranslucentRendering.cpp: Translucent rendering implementation.
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "ScreenRendering.h"
#include "SceneFilterRendering.h"
#include "LightPropagationVolume.h"

void SetTranslucentRenderTargetAndState(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, bool bSeparateTranslucencyPass)
{
	bool bSetupTranslucentState = true;

	if (bSeparateTranslucencyPass && GSceneRenderTargets.IsSeparateTranslucencyActive(View))
	{
		bSetupTranslucentState = GSceneRenderTargets.BeginRenderingSeparateTranslucency(RHICmdList, View, false);
	}
	else
	{
		GSceneRenderTargets.BeginRenderingTranslucency(RHICmdList, View);
	}

	if (bSetupTranslucentState)
	{
		// Enable depth test, disable depth writes.
		// Note, this is a reversed Z depth surface, using CF_GreaterEqual.
		RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_GreaterEqual>::GetRHI());
	}
}

const FProjectedShadowInfo* FDeferredShadingSceneRenderer::PrepareTranslucentShadowMap(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FPrimitiveSceneInfo* PrimitiveSceneInfo, bool bSeparateTranslucencyPass)
{
	const FVisibleLightInfo* VisibleLightInfo = NULL;
	FProjectedShadowInfo* TranslucentSelfShadow = NULL;

	// Find this primitive's self shadow if there is one
	if (PrimitiveSceneInfo->Proxy && PrimitiveSceneInfo->Proxy->CastsVolumetricTranslucentShadow())
	{			
		for (FLightPrimitiveInteraction* Interaction = PrimitiveSceneInfo->LightList;
			Interaction && !TranslucentSelfShadow;
			Interaction = Interaction->GetNextLight()
			)
		{
			const FLightSceneInfo* LightSceneInfo = Interaction->GetLight();

			if (LightSceneInfo->Proxy->GetLightType() == LightType_Directional
				// Only reuse cached shadows from the light which last used TranslucentSelfShadowLayout
				// This has the side effect of only allowing per-pixel self shadowing from one light
				&& LightSceneInfo->Id == CachedTranslucentSelfShadowLightId)
			{
				VisibleLightInfo = &VisibleLightInfos[LightSceneInfo->Id];
				FProjectedShadowInfo* ObjectShadow = NULL;

				for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfo->AllProjectedShadows.Num(); ShadowIndex++)
				{
					FProjectedShadowInfo* CurrentShadowInfo = VisibleLightInfo->AllProjectedShadows[ShadowIndex];

					if (CurrentShadowInfo && CurrentShadowInfo->bTranslucentShadow && CurrentShadowInfo->ParentSceneInfo == PrimitiveSceneInfo)
					{
						TranslucentSelfShadow = CurrentShadowInfo;
						break;
					}
				}
			}
		}

		// Allocate and render the shadow's depth map if needed
		if (TranslucentSelfShadow && !TranslucentSelfShadow->bAllocatedInTranslucentLayout)
		{
			bool bPossibleToAllocate = true;

			// Attempt to find space in the layout
			TranslucentSelfShadow->bAllocatedInTranslucentLayout = TranslucentSelfShadowLayout.AddElement(
				TranslucentSelfShadow->X,
				TranslucentSelfShadow->Y,
				TranslucentSelfShadow->ResolutionX + SHADOW_BORDER * 2,
				TranslucentSelfShadow->ResolutionY + SHADOW_BORDER * 2);

			// Free shadowmaps from this light until allocation succeeds
			while (!TranslucentSelfShadow->bAllocatedInTranslucentLayout && bPossibleToAllocate)
			{
				bPossibleToAllocate = false;

				for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfo->AllProjectedShadows.Num(); ShadowIndex++)
				{
					FProjectedShadowInfo* CurrentShadowInfo = VisibleLightInfo->AllProjectedShadows[ShadowIndex];

					if (CurrentShadowInfo->bTranslucentShadow && CurrentShadowInfo->bAllocatedInTranslucentLayout)
					{
						verify(TranslucentSelfShadowLayout.RemoveElement(
							CurrentShadowInfo->X,
							CurrentShadowInfo->Y,
							CurrentShadowInfo->ResolutionX + SHADOW_BORDER * 2,
							CurrentShadowInfo->ResolutionY + SHADOW_BORDER * 2));

						CurrentShadowInfo->bAllocatedInTranslucentLayout = false;

						bPossibleToAllocate = true;
						break;
					}
				}

				TranslucentSelfShadow->bAllocatedInTranslucentLayout = TranslucentSelfShadowLayout.AddElement(
					TranslucentSelfShadow->X,
					TranslucentSelfShadow->Y,
					TranslucentSelfShadow->ResolutionX + SHADOW_BORDER * 2,
					TranslucentSelfShadow->ResolutionY + SHADOW_BORDER * 2);
			}

			if (!bPossibleToAllocate)
			{
				// Failed to allocate space for the shadow depth map, so don't use the self shadow
				TranslucentSelfShadow = NULL;
			}
			else
			{
				check(TranslucentSelfShadow->bAllocatedInTranslucentLayout);

				// Render the translucency shadow map
				TranslucentSelfShadow->RenderTranslucencyDepths(RHICmdList, this);

				// Restore state
				SetTranslucentRenderTargetAndState(RHICmdList, View, bSeparateTranslucencyPass);
			}
		}
	}

	return TranslucentSelfShadow;
}

/** Pixel shader used to copy scene color into another texture so that materials can read from scene color with a node. */
class FCopySceneColorPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopySceneColorPS,Global);
public:

	static bool ShouldCache(EShaderPlatform Platform) { return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM3); }

	FCopySceneColorPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		SceneTextureParameters.Bind(Initializer.ParameterMap);
	}
	FCopySceneColorPS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		SceneTextureParameters.Set(RHICmdList, GetPixelShader(), View);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SceneTextureParameters;
		return bShaderHasOutdatedParameters;
	}

private:
	FSceneTextureShaderParameters SceneTextureParameters;
};

IMPLEMENT_SHADER_TYPE(,FCopySceneColorPS,TEXT("TranslucentLightingShaders"),TEXT("CopySceneColorMain"),SF_Pixel);

FGlobalBoundShaderState CopySceneColorBoundShaderState;

void FTranslucencyDrawingPolicyFactory::CopySceneColor(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FPrimitiveSceneProxy* PrimitiveSceneProxy)
{

	SCOPED_DRAW_EVENTF(EventCopy, DEC_SCENE_ITEMS, TEXT("CopySceneColor for %s %s"), *PrimitiveSceneProxy->GetOwnerName().ToString(), *PrimitiveSceneProxy->GetResourceName().ToString());
	RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());

	GSceneRenderTargets.ResolveSceneColor(RHICmdList);

	GSceneRenderTargets.BeginRenderingLightAttenuation(RHICmdList);
	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

	TShaderMapRef<FScreenVS> ScreenVertexShader(GetGlobalShaderMap());
	TShaderMapRef<FCopySceneColorPS> PixelShader(GetGlobalShaderMap());
	SetGlobalBoundShaderState(RHICmdList, CopySceneColorBoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *ScreenVertexShader, *PixelShader);

	/// ?
	PixelShader->SetParameters(RHICmdList, View);

	DrawRectangle(
		RHICmdList,
		0, 0, 
		View.ViewRect.Width(), View.ViewRect.Height(),
		View.ViewRect.Min.X, View.ViewRect.Min.Y, 
		View.ViewRect.Width(), View.ViewRect.Height(),
		FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()),
		GSceneRenderTargets.GetBufferSizeXY(),
		*ScreenVertexShader,
		EDRF_UseTriangleOptimization);

	GSceneRenderTargets.FinishRenderingLightAttenuation(RHICmdList);
}

/** The parameters used to draw a translucent mesh. */
class FDrawTranslucentMeshAction
{
public:

	const FViewInfo& View;
	bool bBackFace;
	FHitProxyId HitProxyId;
	const FProjectedShadowInfo* TranslucentSelfShadow;
	bool bUseTranslucentSelfShadowing;

	/** Initialization constructor. */
	FDrawTranslucentMeshAction(
		const FViewInfo& InView,
		bool bInBackFace,
		FHitProxyId InHitProxyId,
		const FProjectedShadowInfo* InTranslucentSelfShadow,
		bool bInUseTranslucentSelfShadowing
		):
		View(InView),
		bBackFace(bInBackFace),
		HitProxyId(InHitProxyId),
		TranslucentSelfShadow(InTranslucentSelfShadow),
		bUseTranslucentSelfShadowing(bInUseTranslucentSelfShadowing)
	{}

	bool UseTranslucentSelfShadowing() const 
	{ 
		return bUseTranslucentSelfShadowing;
	}

	const FProjectedShadowInfo* GetTranslucentSelfShadow() const
	{
		return TranslucentSelfShadow;
	}

	bool AllowIndirectLightingCache() const
	{
		return View.Family->EngineShowFlags.IndirectLightingCache;
	}

	bool AllowIndirectLightingCacheVolumeTexture() const
	{
		// This will force the cheaper single sample interpolated GI path
		return false;
	}

	/** Draws the translucent mesh with a specific light-map type, and fog volume type */
	template<typename LightMapPolicyType>
	void Process(
		FRHICommandList& RHICmdList, 
		const FProcessBasePassMeshParameters& Parameters,
		const LightMapPolicyType& LightMapPolicy,
		const typename LightMapPolicyType::ElementDataType& LightMapElementData
		) const
	{
		const bool bIsLitMaterial = Parameters.ShadingModel != MSM_Unlit;

		const FScene* Scene = Parameters.PrimitiveSceneProxy ? Parameters.PrimitiveSceneProxy->GetPrimitiveSceneInfo()->Scene : NULL;

		TBasePassDrawingPolicy<LightMapPolicyType> DrawingPolicy(
			Parameters.Mesh.VertexFactory,
			Parameters.Mesh.MaterialRenderProxy,
			*Parameters.Material,
			LightMapPolicy,
			Parameters.BlendMode,
			// Translucent meshes need scene render targets set as textures
			ESceneRenderTargetsMode::SetTextures,
			bIsLitMaterial && Scene && Scene->SkyLight && !Scene->SkyLight->bHasStaticLighting,
			Scene && Scene->HasAtmosphericFog() && View.Family->EngineShowFlags.Atmosphere,
			View.Family->EngineShowFlags.ShaderComplexity,
			Parameters.bAllowFog
			);
		RHICmdList.BuildAndSetLocalBoundShaderState(DrawingPolicy.GetBoundShaderStateInput(View.GetFeatureLevel()));
		DrawingPolicy.SetSharedState(RHICmdList, &View);

		int32 BatchElementIndex = 0;
		uint64 BatchElementMask = Parameters.BatchElementMask;
		do
		{
			if(BatchElementMask & 1)
			{
				DrawingPolicy.SetMeshRenderState(
					RHICmdList, 
					View,
					Parameters.PrimitiveSceneProxy,
					Parameters.Mesh,
					BatchElementIndex,
					bBackFace,
					typename TBasePassDrawingPolicy<LightMapPolicyType>::ElementDataType(LightMapElementData)
					);
				DrawingPolicy.DrawMesh(RHICmdList, Parameters.Mesh,BatchElementIndex);
			}

			BatchElementMask >>= 1;
			BatchElementIndex++;
		} while(BatchElementMask);
	}
};

/**
* Render a dynamic or static mesh using a translucent draw policy
* @return true if the mesh rendered
*/
bool FTranslucencyDrawingPolicyFactory::DrawMesh(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	ContextType DrawingContext,
	const FMeshBatch& Mesh,
	const uint64& BatchElementMask,
	bool bBackFace,
	bool bPreFog,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	FHitProxyId HitProxyId
	)
{
	bool bDirty = false;
	const auto FeatureLevel = View.GetFeatureLevel();

	// Determine the mesh's material and blend mode.
	const FMaterial* Material = Mesh.MaterialRenderProxy->GetMaterial(FeatureLevel);
	const EBlendMode BlendMode = Material->GetBlendMode();
	const EMaterialShadingModel ShadingModel = Material->GetShadingModel();

	// Only render translucent materials.
	if(IsTranslucentBlendMode(BlendMode))
	{
		if (Material->RequiresSceneColorCopy())
		{
			if (DrawingContext.bSceneColorCopyIsUpToDate == false)
			{
				//hack here to get the code checked in, but this needs to be fixed. It was not sensible to wind this all the way out of the callstack
				FRHICommandListImmediate& RHICmdListIm = FRHICommandListExecutor::GetImmediateCommandList();
				check(&RHICmdList == &RHICmdListIm);

				CopySceneColor(RHICmdListIm, View, PrimitiveSceneProxy);
				// Restore state
				SetTranslucentRenderTargetAndState(RHICmdListIm, View, false);

				// separate translucency is not updating scene color so we don't need to copy it multiple times.
				// optimization:
				// we should consider the same for non separate translucency (could cause artifacts but will be much faster)
				DrawingContext.bSceneColorCopyIsUpToDate = DrawingContext.bSeparateTranslucencyPass;
			}
		}

		const bool bDisableDepthTest = Material->ShouldDisableDepthTest();
		const bool bEnableResponsiveAA = Material->ShouldEnableResponsiveAA();
		// editor compositing not supported on translucent materials currently
		const bool bEditorCompositeDepthTest = false;

		if( bEnableResponsiveAA )
		{
			if( bDisableDepthTest )
			{
				RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false,CF_Always,true,CF_Always,SO_Keep,SO_Keep,SO_Replace>::GetRHI(), 1);
			}
			else
			{
				// Note, this is a reversed Z depth surface, using CF_GreaterEqual.	
				RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false,CF_GreaterEqual,true,CF_Always,SO_Keep,SO_Keep,SO_Replace>::GetRHI(), 1);
			}
		}
		else if( bDisableDepthTest )
		{
			RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false,CF_Always>::GetRHI());
		}

		ProcessBasePassMesh(
			RHICmdList, 
			FProcessBasePassMeshParameters(
				Mesh,
				BatchElementMask,
				Material,
				PrimitiveSceneProxy, 
				!bPreFog,
				bEditorCompositeDepthTest,
				ESceneRenderTargetsMode::SetTextures,
				FeatureLevel
			),
			FDrawTranslucentMeshAction(
				View,
				bBackFace,
				HitProxyId,
				DrawingContext.TranslucentSelfShadow,
				PrimitiveSceneProxy && PrimitiveSceneProxy->CastsVolumetricTranslucentShadow()
			)
		);

		if (bDisableDepthTest || bEnableResponsiveAA)
		{
			// Restore default depth state
			// Note, this is a reversed Z depth surface, using CF_GreaterEqual.	
			RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false,CF_GreaterEqual>::GetRHI());
		}

		bDirty = true;
	}
	return bDirty;
}


/**
 * Render a dynamic mesh using a translucent draw policy
 * @return true if the mesh rendered
 */
bool FTranslucencyDrawingPolicyFactory::DrawDynamicMesh(
	FRHICommandList& RHICmdList, 
	const FViewInfo& View,
	ContextType DrawingContext,
	const FMeshBatch& Mesh,
	bool bBackFace,
	bool bPreFog,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	FHitProxyId HitProxyId
	)
{
	return DrawMesh(
		RHICmdList,
		View,
		DrawingContext,
		Mesh,
		Mesh.Elements.Num()==1 ? 1 : (1<<Mesh.Elements.Num())-1,	// 1 bit set for each mesh element
		bBackFace,
		bPreFog,
		PrimitiveSceneProxy,
		HitProxyId);
}

/**
 * Render a static mesh using a translucent draw policy
 * @return true if the mesh rendered
 */
bool FTranslucencyDrawingPolicyFactory::DrawStaticMesh(
	FRHICommandList& RHICmdList, 
	const FViewInfo& View,
	ContextType DrawingContext,
	const FStaticMesh& StaticMesh,
	const uint64& BatchElementMask,
	bool bPreFog,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	FHitProxyId HitProxyId
	)
{
	return DrawMesh(
		RHICmdList,
		View,
		DrawingContext,
		StaticMesh,
		BatchElementMask,
		false,
		bPreFog,
		PrimitiveSceneProxy,
		HitProxyId
		);
}

/*-----------------------------------------------------------------------------
FTranslucentPrimSet
-----------------------------------------------------------------------------*/

/**
* Iterate over the sorted list of prims and draw them
* @param ViewInfo - current view used to draw items
* @param bSeparateTranslucencyPass
* @return true if anything was drawn
*/
void FTranslucentPrimSet::DrawPrimitives(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FDeferredShadingSceneRenderer& Renderer,
	bool bSeparateTranslucencyPass
	) const
{
	const TArray<FSortedPrim,SceneRenderingAllocator>* PhaseSortedPrimitivesPtr = NULL;

	// copy to reference for easier access
	const TArray<FSortedPrim,SceneRenderingAllocator>& PhaseSortedPrimitives =
		bSeparateTranslucencyPass ? SortedSeparateTranslucencyPrims : SortedPrims;

	if( PhaseSortedPrimitives.Num() )
	{
		// Draw sorted scene prims
		for( int32 PrimIdx=0; PrimIdx < PhaseSortedPrimitives.Num(); PrimIdx++ )
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = PhaseSortedPrimitives[PrimIdx].PrimitiveSceneInfo;
			int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
			const FPrimitiveViewRelevance& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveId];

			checkSlow(ViewRelevance.HasTranslucency());
			
			const FProjectedShadowInfo* TranslucentSelfShadow = Renderer.PrepareTranslucentShadowMap(RHICmdList, View, PrimitiveSceneInfo, bSeparateTranslucencyPass);

			RenderPrimitive(RHICmdList, View, PrimitiveSceneInfo, ViewRelevance, TranslucentSelfShadow, bSeparateTranslucencyPass);
		}
	}
}

void FTranslucentPrimSet::RenderPrimitive(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View, 
	FPrimitiveSceneInfo* PrimitiveSceneInfo, 
	const FPrimitiveViewRelevance& ViewRelevance, 
	const FProjectedShadowInfo* TranslucentSelfShadow,
	bool bSeparateTranslucencyPass) const
{
	checkSlow(ViewRelevance.HasTranslucency());
	auto FeatureLevel = View.GetFeatureLevel();

	if(ViewRelevance.bDrawRelevance)
	{
		// Render dynamic scene prim
		if( ViewRelevance.bDynamicRelevance )
		{
			TDynamicPrimitiveDrawer<FTranslucencyDrawingPolicyFactory> TranslucencyDrawer(
				RHICmdList,
				&View,
				FTranslucencyDrawingPolicyFactory::ContextType(TranslucentSelfShadow, bSeparateTranslucencyPass),
				false
				);

			TranslucencyDrawer.SetPrimitive(PrimitiveSceneInfo->Proxy);
			PrimitiveSceneInfo->Proxy->DrawDynamicElements(
				&TranslucencyDrawer,
				&View
				);
		}

		// Render static scene prim
		if( ViewRelevance.bStaticRelevance )
		{
			// Render static meshes from static scene prim
			for( int32 StaticMeshIdx=0; StaticMeshIdx < PrimitiveSceneInfo->StaticMeshes.Num(); StaticMeshIdx++ )
			{
				FStaticMesh& StaticMesh = PrimitiveSceneInfo->StaticMeshes[StaticMeshIdx];
				if (View.StaticMeshVisibilityMap[StaticMesh.Id]
					// Only render static mesh elements using translucent materials
					&& StaticMesh.IsTranslucent(FeatureLevel)
					&& (StaticMesh.MaterialRenderProxy->GetMaterial(FeatureLevel)->IsSeparateTranslucencyEnabled() == bSeparateTranslucencyPass))
				{
					FTranslucencyDrawingPolicyFactory::DrawStaticMesh(
						RHICmdList,
						View,
						FTranslucencyDrawingPolicyFactory::ContextType( TranslucentSelfShadow, bSeparateTranslucencyPass),
						StaticMesh,
						StaticMesh.Elements.Num() == 1 ? 1 : View.StaticMeshBatchVisibility[StaticMesh.Id],
						false,
						PrimitiveSceneInfo->Proxy,
						StaticMesh.HitProxyId
						);
				}
			}
		}
	}
}

/**
* Add a new primitive to the list of sorted prims
* @param PrimitiveSceneInfo - primitive info to add. Origin of bounds is used for sort.
* @param ViewInfo - used to transform bounds to view space
*/
void FTranslucentPrimSet::AddScenePrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, const FViewInfo& ViewInfo, bool bUseNormalTranslucency, bool bUseSeparateTranslucency)
{
	float SortKey=0.f;
	//sort based on distance to the view position, view rotation is not a factor
	SortKey = (PrimitiveSceneInfo->Proxy->GetBounds().Origin - ViewInfo.ViewMatrices.ViewOrigin).Size();
	// UE4_TODO: also account for DPG in the sort key.

	const auto FeatureLevel = ViewInfo.GetFeatureLevel();

	if(bUseSeparateTranslucency 
		&& FeatureLevel >= ERHIFeatureLevel::SM3)
	{
		// add to list of translucent prims that use scene color
		new(SortedSeparateTranslucencyPrims) FSortedPrim(PrimitiveSceneInfo,SortKey,PrimitiveSceneInfo->Proxy->GetTranslucencySortPriority());
	}

	if (bUseNormalTranslucency 
		// Force separate translucency to be rendered normally if the feature level does not support separate translucency
		|| (bUseSeparateTranslucency && FeatureLevel < ERHIFeatureLevel::SM3))
	{
		// add to list of translucent prims
		new(SortedPrims) FSortedPrim(PrimitiveSceneInfo,SortKey,PrimitiveSceneInfo->Proxy->GetTranslucencySortPriority());
	}
}

/**
* Sort any primitives that were added to the set back-to-front
*/
void FTranslucentPrimSet::SortPrimitives()
{
	// sort prims based on depth
	SortedPrims.Sort( FCompareFSortedPrim() );
	SortedSeparateTranslucencyPrims.Sort( FCompareFSortedPrim() );
}

bool FSceneRenderer::ShouldRenderTranslucency() const
{
	bool bRender = false;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (View.TranslucentPrimSet.NumPrims() > 0 || View.bHasTranslucentViewMeshElements || View.TranslucentPrimSet.NumSeparateTranslucencyPrims() > 0)
		{
			bRender = true;
			break;
		}
	}

	return bRender;
}

void FDeferredShadingSceneRenderer::RenderTranslucency(FRHICommandListImmediate& RHICmdList)
{
	if (ShouldRenderTranslucency())
	{

		SCOPED_DRAW_EVENT(Translucency, DEC_SCENE_ITEMS);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			SCOPED_CONDITIONAL_DRAW_EVENTF(EventView, Views.Num() > 1, DEC_SCENE_ITEMS, TEXT("View%d"), ViewIndex);

			const FViewInfo& View = Views[ViewIndex];

			SetTranslucentRenderTargetAndState(RHICmdList, View, false);

			// Draw only translucent prims that don't read from scene color
			View.TranslucentPrimSet.DrawPrimitives(RHICmdList, View, *this, false);
			// Draw the view's mesh elements with the translucent drawing policy.
			DrawViewElements<FTranslucencyDrawingPolicyFactory>(RHICmdList, View, FTranslucencyDrawingPolicyFactory::ContextType(), SDPG_World, false);
			// Draw the view's mesh elements with the translucent drawing policy.
			DrawViewElements<FTranslucencyDrawingPolicyFactory>(RHICmdList, View, FTranslucencyDrawingPolicyFactory::ContextType(), SDPG_Foreground, false);

			const FSceneViewState* ViewState = (const FSceneViewState*)View.State;

			if(ViewState && View.Family->EngineShowFlags.VisualizeLPV)
			{
				FLightPropagationVolume* LightPropagationVolume = ViewState->GetLightPropagationVolume();

				if (LightPropagationVolume)
				{
					LightPropagationVolume->Visualise(RHICmdList, View);
				}
			}
			
			{
				bool bRenderSeparateTranslucency = View.TranslucentPrimSet.NumSeparateTranslucencyPrims() > 0;

				// always call BeginRenderingSeparateTranslucency() even if there are no primitives to we keep the RT allocated
				bool bSetupTranslucency = GSceneRenderTargets.BeginRenderingSeparateTranslucency(RHICmdList, View, true);

				// Draw only translucent prims that are in the SeparateTranslucency pass
				if (bRenderSeparateTranslucency)
				{
					if (bSetupTranslucency)
					{
						RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_GreaterEqual>::GetRHI());
					}

					View.TranslucentPrimSet.DrawPrimitives(RHICmdList, View, *this, true);
				}

				GSceneRenderTargets.FinishRenderingSeparateTranslucency(RHICmdList, View);
			}
		}
	}
}
