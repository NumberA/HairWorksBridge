// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TranslucentRendering.cpp: Translucent rendering implementation.
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "ScreenRendering.h"
#include "SceneFilterRendering.h"
#include "LightPropagationVolume.h"
#include "SceneUtils.h"

static float GScreenTextureUVScale;

static void SetTranslucentRenderTargetAndState(FRHICommandList& RHICmdList, const FViewInfo& View, ETranslucencyPassType TranslucenyPassType, bool bFirstTimeThisFrame = false)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	bool bSetupTranslucentState = true;
	bool bNeedsClear = (&View == View.Family->Views[0]) && bFirstTimeThisFrame;

	if ((TranslucenyPassType == TPT_SeparateTransluceny) && SceneContext.IsSeparateTranslucencyActive(View))
	{
		bSetupTranslucentState = SceneContext.BeginRenderingSeparateTranslucency(RHICmdList, View, bNeedsClear);
	}
	else if (TranslucenyPassType == TPT_NonSeparateTransluceny)
	{
		SceneContext.BeginRenderingTranslucency(RHICmdList, View, bNeedsClear);
	}

	if (bSetupTranslucentState)
	{
		// Enable depth test, disable depth writes.
		RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
	}
}

static void FinishTranslucentRenderTarget(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, ETranslucencyPassType TranslucenyPassType)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	if ((TranslucenyPassType == TPT_SeparateTransluceny) && SceneContext.IsSeparateTranslucencyActive(View))
	{
		SceneContext.FinishRenderingSeparateTranslucency(RHICmdList, View);
	}
	else
	{
		SceneContext.FinishRenderingTranslucency(RHICmdList, View);
	}
}

const FProjectedShadowInfo* FDeferredShadingSceneRenderer::PrepareTranslucentShadowMap(FRHICommandList& RHICmdList, const FViewInfo& View, FPrimitiveSceneInfo* PrimitiveSceneInfo, ETranslucencyPassType TranslucenyPassType)
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

				for (int32 ShadowIndex = 0, Count = VisibleLightInfo->AllProjectedShadows.Num(); ShadowIndex < Count; ShadowIndex++)
				{
					FProjectedShadowInfo* CurrentShadowInfo = VisibleLightInfo->AllProjectedShadows[ShadowIndex];

					if (CurrentShadowInfo && CurrentShadowInfo->bTranslucentShadow && CurrentShadowInfo->GetParentSceneInfo() == PrimitiveSceneInfo)
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
			check(IsInRenderingThread());
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
				SetTranslucentRenderTargetAndState(RHICmdList, View, TranslucenyPassType);
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

	static bool ShouldCache(EShaderPlatform Platform) { return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4); }

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

	virtual bool Serialize(FArchive& Ar) override
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

void FTranslucencyDrawingPolicyFactory::CopySceneColor(FRHICommandList& RHICmdList, const FViewInfo& View, const FPrimitiveSceneProxy* PrimitiveSceneProxy)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	SCOPED_DRAW_EVENTF(RHICmdList, EventCopy, TEXT("CopySceneColor for %s %s"), *PrimitiveSceneProxy->GetOwnerName().ToString(), *PrimitiveSceneProxy->GetResourceName().ToString());
	RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());

	SceneContext.ResolveSceneColor(RHICmdList);

	SceneContext.BeginRenderingLightAttenuation(RHICmdList);
	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

	TShaderMapRef<FScreenVS> ScreenVertexShader(View.ShaderMap);
	TShaderMapRef<FCopySceneColorPS> PixelShader(View.ShaderMap);
	SetGlobalBoundShaderState(RHICmdList, View.GetFeatureLevel(), CopySceneColorBoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *ScreenVertexShader, *PixelShader);

	/// ?
	PixelShader->SetParameters(RHICmdList, View);

	DrawRectangle(
		RHICmdList,
		0, 0, 
		View.ViewRect.Width(), View.ViewRect.Height(),
		View.ViewRect.Min.X, View.ViewRect.Min.Y, 
		View.ViewRect.Width(), View.ViewRect.Height(),
		FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()),
		SceneContext.GetBufferSizeXY(),
		*ScreenVertexShader,
		EDRF_UseTriangleOptimization);

	SceneContext.FinishRenderingLightAttenuation(RHICmdList);
}

/** The parameters used to draw a translucent mesh. */
class FDrawTranslucentMeshAction
{
public:

	const FViewInfo& View;
	const FProjectedShadowInfo* TranslucentSelfShadow;
	FHitProxyId HitProxyId;
	bool bBackFace;
	FMeshDrawingRenderState DrawRenderState;
	bool bUseTranslucentSelfShadowing;
	float SeparateTranslucencyScreenTextureScaleFactor;

	/** Initialization constructor. */
	FDrawTranslucentMeshAction(
		const FViewInfo& InView,
		bool bInBackFace,
		const FMeshDrawingRenderState& InDrawRenderState,
		FHitProxyId InHitProxyId,
		const FProjectedShadowInfo* InTranslucentSelfShadow,
		bool bInUseTranslucentSelfShadowing,
		float ScreenTextureUVScaleFactor
		) :
		View(InView),
		TranslucentSelfShadow(InTranslucentSelfShadow),
		HitProxyId(InHitProxyId),
		bBackFace(bInBackFace),
		DrawRenderState(InDrawRenderState),
		bUseTranslucentSelfShadowing(bInUseTranslucentSelfShadowing),
		SeparateTranslucencyScreenTextureScaleFactor(ScreenTextureUVScaleFactor)
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
		const FScene* Scene = (const FScene*)View.Family->Scene;
		return View.Family->EngineShowFlags.IndirectLightingCache && Scene && Scene->PrecomputedLightVolumes.Num() > 0;
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
			Parameters.FeatureLevel,
			LightMapPolicy,
			Parameters.BlendMode,
			// Translucent meshes need scene render targets set as textures
			ESceneRenderTargetsMode::SetTextures,
			bIsLitMaterial && Scene && Scene->SkyLight && !Scene->SkyLight->bHasStaticLighting,
			Scene && Scene->HasAtmosphericFog() && View.Family->EngineShowFlags.AtmosphericFog && View.Family->EngineShowFlags.Fog,
			View.Family->EngineShowFlags.ShaderComplexity,
			Parameters.bAllowFog,
			false,
			false,
			View.Family->GetQuadOverdrawMode()
			);
		RHICmdList.BuildAndSetLocalBoundShaderState(DrawingPolicy.GetBoundShaderStateInput(View.GetFeatureLevel()));
		DrawingPolicy.SetSharedState(RHICmdList, &View, typename TBasePassDrawingPolicy<LightMapPolicyType>::ContextDataType(), SeparateTranslucencyScreenTextureScaleFactor);

		int32 BatchElementIndex = 0;
		uint64 BatchElementMask = Parameters.BatchElementMask;
		do
		{
			if(BatchElementMask & 1)
			{
				TDrawEvent<FRHICommandList> MeshEvent;
				BeginMeshDrawEvent(RHICmdList, Parameters.PrimitiveSceneProxy, Parameters.Mesh, MeshEvent);

				DrawingPolicy.SetMeshRenderState(
					RHICmdList, 
					View,
					Parameters.PrimitiveSceneProxy,
					Parameters.Mesh,
					BatchElementIndex,
					bBackFace,
					DrawRenderState,
					typename TBasePassDrawingPolicy<LightMapPolicyType>::ElementDataType(LightMapElementData),
					typename TBasePassDrawingPolicy<LightMapPolicyType>::ContextDataType()
					);
				DrawingPolicy.DrawMesh(RHICmdList, Parameters.Mesh,BatchElementIndex);
			}

			BatchElementMask >>= 1;
			BatchElementIndex++;
		} while(BatchElementMask);
	}
};

static void CopySceneColorAndRestore(FRHICommandList& RHICmdList, const FViewInfo& View, const FPrimitiveSceneProxy* PrimitiveSceneProxy)
{
	check(IsInRenderingThread());
	FTranslucencyDrawingPolicyFactory::CopySceneColor(RHICmdList, View, PrimitiveSceneProxy);
	// Restore state
	SetTranslucentRenderTargetAndState(RHICmdList, View, TPT_NonSeparateTransluceny);
}

class FCopySceneColorAndRestoreRenderThreadTask
{
	FRHICommandList& RHICmdList;
	const FViewInfo& View;
	const FPrimitiveSceneProxy* PrimitiveSceneProxy;
public:

	FCopySceneColorAndRestoreRenderThreadTask(FRHICommandList& InRHICmdList, const FViewInfo& InView, const FPrimitiveSceneProxy* InPrimitiveSceneProxy)
		: RHICmdList(InRHICmdList)
		, View(InView)
		, PrimitiveSceneProxy(InPrimitiveSceneProxy)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FCopySceneColorAndRestoreRenderThreadTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::RenderThread_Local;
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		CopySceneColorAndRestore(RHICmdList, View, PrimitiveSceneProxy);
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
	const FMeshDrawingRenderState& DrawRenderState,
	bool bPreFog,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	FHitProxyId HitProxyId,
	bool bSeparateTranslucencyEnabled
	)
{
	bool bDirty = false;
	const auto FeatureLevel = View.GetFeatureLevel();

	// Determine the mesh's material and blend mode.
	const FMaterial* Material = Mesh.MaterialRenderProxy->GetMaterial(FeatureLevel);
	const EBlendMode BlendMode = Material->GetBlendMode();

	// Only render translucent materials
	if(IsTranslucentBlendMode(BlendMode))
	{
		bool bCurrentlyRenderingSeparateTranslucency = Material->IsSeparateTranslucencyEnabled() == (DrawingContext.TranslucenyPassType == TPT_SeparateTransluceny);
		// if we are in relevant pass
		if (bCurrentlyRenderingSeparateTranslucency || bSeparateTranslucencyEnabled == false)
		{
			if (Material->RequiresSceneColorCopy_RenderThread())
			{
				if (DrawingContext.bSceneColorCopyIsUpToDate == false)
				{
					if (!RHICmdList.Bypass() && !IsInActualRenderingThread() && !IsInGameThread())
					{
						FRHICommandList* CmdList = new FRHICommandList;
						CmdList->CopyRenderThreadContexts(RHICmdList);
						FGraphEventRef RenderThreadCompletionEvent = TGraphTask<FCopySceneColorAndRestoreRenderThreadTask>::CreateTask().ConstructAndDispatchWhenReady(*CmdList, View, PrimitiveSceneProxy);
						RHICmdList.QueueRenderThreadCommandListSubmit(RenderThreadCompletionEvent, CmdList);
					}
					else
					{
						// otherwise, just do it now. We don't want to defer in this case because that can interfere with render target visualization (a debugging tool).
						CopySceneColorAndRestore(RHICmdList, View, PrimitiveSceneProxy);
					}
					// todo: this optimization is currently broken
					DrawingContext.bSceneColorCopyIsUpToDate = (DrawingContext.TranslucenyPassType == TPT_SeparateTransluceny);
				}
			}

			const bool bDisableDepthTest = Material->ShouldDisableDepthTest();
			const bool bEnableResponsiveAA = Material->ShouldEnableResponsiveAA();
			// editor compositing not supported on translucent materials currently
			const bool bEditorCompositeDepthTest = false;

			// if this draw is coming postAA then there is probably no depth buffer (it's canvas) and bEnableResponsiveAA wont' do anything anyway.
			if (bEnableResponsiveAA && !DrawingContext.bPostAA)
			{
				if( bDisableDepthTest )
				{
					RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false,CF_Always,true,CF_Always,SO_Keep,SO_Keep,SO_Replace>::GetRHI(), 1);
				}
				else
				{
					RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false,CF_DepthNearOrEqual,true,CF_Always,SO_Keep,SO_Keep,SO_Replace>::GetRHI(), 1);
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
					DrawRenderState,
					HitProxyId,
					DrawingContext.TranslucentSelfShadow,
					PrimitiveSceneProxy && PrimitiveSceneProxy->CastsVolumetricTranslucentShadow(),
					bCurrentlyRenderingSeparateTranslucency ? GScreenTextureUVScale : 1.0f
				)
			);

			if (bDisableDepthTest || bEnableResponsiveAA)
			{
				// Restore default depth state
				RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false,CF_DepthNearOrEqual>::GetRHI());
			}

			bDirty = true;
		}
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
	FHitProxyId HitProxyId,
	bool bSeparateTranslucencyEnabled
	)
{
	return DrawMesh(
		RHICmdList,
		View,
		DrawingContext,
		Mesh,
		Mesh.Elements.Num() == 1 ? 1 : (1 << Mesh.Elements.Num()) - 1,	// 1 bit set for each mesh element
		bBackFace,
		FMeshDrawingRenderState(Mesh.DitheredLODTransitionAlpha),
		bPreFog,
		PrimitiveSceneProxy,
		HitProxyId,
		bSeparateTranslucencyEnabled
		);
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
	FHitProxyId HitProxyId,
	bool bSeparateTranslucencyEnabled
	)
{
	const FMeshDrawingRenderState DrawRenderState(View.GetDitheredLODTransitionState(StaticMesh));
	return DrawMesh(
		RHICmdList,
		View,
		DrawingContext,
		StaticMesh,
		BatchElementMask,
		false,
		DrawRenderState,
		bPreFog,
		PrimitiveSceneProxy,
		HitProxyId,
		bSeparateTranslucencyEnabled
		);
}

/*-----------------------------------------------------------------------------
FTranslucentPrimSet
-----------------------------------------------------------------------------*/

void FTranslucentPrimSet::DrawAPrimitive(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	FDeferredShadingSceneRenderer& Renderer,
	ETranslucencyPassType TranslucenyPassType,
	int32 PrimIdx
	) const
{
	const TArray<FSortedPrim, SceneRenderingAllocator>& PhaseSortedPrimitives =
		(TranslucenyPassType == TPT_SeparateTransluceny) ? SortedSeparateTranslucencyPrims : SortedPrims;

	check(PrimIdx < PhaseSortedPrimitives.Num());

	FPrimitiveSceneInfo* PrimitiveSceneInfo = PhaseSortedPrimitives[PrimIdx].PrimitiveSceneInfo;
	int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
	const FPrimitiveViewRelevance& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveId];

	checkSlow(ViewRelevance.HasTranslucency());

	const FProjectedShadowInfo* TranslucentSelfShadow = Renderer.PrepareTranslucentShadowMap(RHICmdList, View, PrimitiveSceneInfo, TranslucenyPassType);

	RenderPrimitive(RHICmdList, View, PrimitiveSceneInfo, ViewRelevance, TranslucentSelfShadow, TranslucenyPassType);
}

class FVolumetricTranslucentShadowRenderThreadTask
{
	FRHICommandList& RHICmdList;
	const FTranslucentPrimSet &PrimSet;
	const FViewInfo& View;
	FDeferredShadingSceneRenderer& Renderer;
	ETranslucencyPassType TranslucenyPassType;
	int32 Index;

public:

	FORCEINLINE_DEBUGGABLE FVolumetricTranslucentShadowRenderThreadTask(FRHICommandList& InRHICmdList, const FTranslucentPrimSet& InPrimSet, const FViewInfo& InView, FDeferredShadingSceneRenderer& InRenderer, ETranslucencyPassType InTranslucenyPassType, int32 InIndex)
		: RHICmdList(InRHICmdList)
		, PrimSet(InPrimSet)
		, View(InView)
		, Renderer(InRenderer)
		, TranslucenyPassType(InTranslucenyPassType)
		, Index(InIndex)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FVolumetricTranslucentShadowRenderThreadTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::RenderThread_Local;
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		PrimSet.DrawAPrimitive(RHICmdList, View, Renderer, TranslucenyPassType, Index);
	}
};

void FTranslucentPrimSet::DrawPrimitivesParallel(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	FDeferredShadingSceneRenderer& Renderer,
	ETranslucencyPassType TranslucenyPassType,
	int32 FirstIndex, int32 LastIndex
	) const
{
	const TArray<FSortedPrim, SceneRenderingAllocator>& PhaseSortedPrimitives =
		(TranslucenyPassType == TPT_SeparateTransluceny) ? SortedSeparateTranslucencyPrims : SortedPrims;

	check(LastIndex < PhaseSortedPrimitives.Num());
	
	// Draw sorted scene prims
	for (int32 PrimIdx = FirstIndex; PrimIdx <= LastIndex; PrimIdx++)
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo = PhaseSortedPrimitives[PrimIdx].PrimitiveSceneInfo;
		int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
		const FPrimitiveViewRelevance& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveId];

		checkSlow(ViewRelevance.HasTranslucency());

		if (PrimitiveSceneInfo->Proxy && PrimitiveSceneInfo->Proxy->CastsVolumetricTranslucentShadow())
		{
			check(!IsInActualRenderingThread());
			// can't do this in parallel, defer
			FRHICommandList* CmdList = new FRHICommandList;
			CmdList->CopyRenderThreadContexts(RHICmdList);
			FGraphEventRef RenderThreadCompletionEvent = TGraphTask<FVolumetricTranslucentShadowRenderThreadTask>::CreateTask().ConstructAndDispatchWhenReady(*CmdList, *this, View, Renderer, TranslucenyPassType, PrimIdx);
			RHICmdList.QueueRenderThreadCommandListSubmit(RenderThreadCompletionEvent, CmdList);
		}
		else
		{
			RenderPrimitive(RHICmdList, View, PrimitiveSceneInfo, ViewRelevance, nullptr, TranslucenyPassType);
		}
	}
}

void FTranslucentPrimSet::DrawPrimitives(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FDeferredShadingSceneRenderer& Renderer,
	ETranslucencyPassType TranslucenyPassType
	) const
{
	const TArray<FSortedPrim,SceneRenderingAllocator>& PhaseSortedPrimitives =
		(TranslucenyPassType == TPT_SeparateTransluceny) ? SortedSeparateTranslucencyPrims : SortedPrims;

	// Draw sorted scene prims
	for( int32 PrimIdx = 0, Count = PhaseSortedPrimitives.Num(); PrimIdx < Count; PrimIdx++ )
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo = PhaseSortedPrimitives[PrimIdx].PrimitiveSceneInfo;
		int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
		const FPrimitiveViewRelevance& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveId];

		checkSlow(ViewRelevance.HasTranslucency());
			
		const FProjectedShadowInfo* TranslucentSelfShadow = Renderer.PrepareTranslucentShadowMap(RHICmdList, View, PrimitiveSceneInfo, TranslucenyPassType);

		RenderPrimitive(RHICmdList, View, PrimitiveSceneInfo, ViewRelevance, TranslucentSelfShadow, TranslucenyPassType);
	}

	View.SimpleElementCollector.DrawBatchedElements(RHICmdList, View, FTexture2DRHIRef(), EBlendModeFilter::Translucent);
}

void FTranslucentPrimSet::RenderPrimitive(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	FPrimitiveSceneInfo* PrimitiveSceneInfo,
	const FPrimitiveViewRelevance& ViewRelevance,
	const FProjectedShadowInfo* TranslucentSelfShadow,
	ETranslucencyPassType TranslucenyPassType) const
{
	checkSlow(ViewRelevance.HasTranslucency());
	auto FeatureLevel = View.GetFeatureLevel();

	if (ViewRelevance.bDrawRelevance)
	{
		FTranslucencyDrawingPolicyFactory::ContextType Context(TranslucentSelfShadow, TranslucenyPassType);

		// need to chec further down if we can skip rendering ST primitives, because we need to make sure they render in the normal translucency pass otherwise
		// getting the cvar here and passing it down to be more efficient		
		bool bSeparateTranslucencyPossible = (FSceneRenderTargets::CVarSetSeperateTranslucencyEnabled.GetValueOnRenderThread() != 0) && View.Family->EngineShowFlags.SeparateTranslucency && View.Family->EngineShowFlags.PostProcessing;


		//@todo parallelrendering - come up with a better way to filter these by primitive
		for (int32 MeshBatchIndex = 0, Count = View.DynamicMeshElements.Num(); MeshBatchIndex < Count; MeshBatchIndex++)
		{
			const FMeshBatchAndRelevance& MeshBatchAndRelevance = View.DynamicMeshElements[MeshBatchIndex];

			if (MeshBatchAndRelevance.PrimitiveSceneProxy == PrimitiveSceneInfo->Proxy)
			{
				const FMeshBatch& MeshBatch = *MeshBatchAndRelevance.Mesh;
				FTranslucencyDrawingPolicyFactory::DrawDynamicMesh(RHICmdList, View, Context, MeshBatch, false, false, MeshBatchAndRelevance.PrimitiveSceneProxy, MeshBatch.BatchHitProxyId, bSeparateTranslucencyPossible);
			}
		}

		// Render static scene prim
		if (ViewRelevance.bStaticRelevance)
		{
			// Render static meshes from static scene prim
			for (int32 StaticMeshIdx = 0, Count = PrimitiveSceneInfo->StaticMeshes.Num(); StaticMeshIdx < Count; StaticMeshIdx++)
			{
				FStaticMesh& StaticMesh = PrimitiveSceneInfo->StaticMeshes[StaticMeshIdx];
				bool bMaterialMatchesPass = (StaticMesh.MaterialRenderProxy->GetMaterial(FeatureLevel)->IsSeparateTranslucencyEnabled() == (TranslucenyPassType == TPT_SeparateTransluceny));
				bool bShouldRenderMesh = bMaterialMatchesPass || (!bSeparateTranslucencyPossible);

				if (View.StaticMeshVisibilityMap[StaticMesh.Id]
					// Only render static mesh elements using translucent materials
					&& StaticMesh.IsTranslucent(FeatureLevel)
					&& bShouldRenderMesh )
				{
					FTranslucencyDrawingPolicyFactory::DrawStaticMesh(
					RHICmdList,
					View,
					FTranslucencyDrawingPolicyFactory::ContextType(TranslucentSelfShadow, TranslucenyPassType),
					StaticMesh,
					StaticMesh.Elements.Num() == 1 ? 1 : View.StaticMeshBatchVisibility[StaticMesh.Id],
					false,
					PrimitiveSceneInfo->Proxy,
					StaticMesh.BatchHitProxyId,
					bSeparateTranslucencyPossible
					);
				}
			}
		}
	}
}

inline float CalculateTranslucentSortKey(FPrimitiveSceneInfo* PrimitiveSceneInfo, const FViewInfo& ViewInfo)
{
	float SortKey = 0.0f;
	if (ViewInfo.TranslucentSortPolicy == ETranslucentSortPolicy::SortByDistance)
	{
		//sort based on distance to the view position, view rotation is not a factor
		SortKey = (PrimitiveSceneInfo->Proxy->GetBounds().Origin - ViewInfo.ViewMatrices.ViewOrigin).Size();
		// UE4_TODO: also account for DPG in the sort key.
	}
	else if (ViewInfo.TranslucentSortPolicy == ETranslucentSortPolicy::SortAlongAxis)
	{
		// Sort based on enforced orthogonal distance
		const FVector CameraToObject = PrimitiveSceneInfo->Proxy->GetBounds().Origin - ViewInfo.ViewMatrices.ViewOrigin;
		SortKey = FVector::DotProduct(CameraToObject, ViewInfo.TranslucentSortAxis);
	}
	else
	{
		// Sort based on projected Z distance
		check(ViewInfo.TranslucentSortPolicy == ETranslucentSortPolicy::SortByProjectedZ);
		SortKey = ViewInfo.ViewMatrices.ViewMatrix.TransformPosition(PrimitiveSceneInfo->Proxy->GetBounds().Origin).Z;
	}

	return SortKey;
}

/**
* Add a new primitive to the list of sorted prims
* @param PrimitiveSceneInfo - primitive info to add. Origin of bounds is used for sort.
* @param ViewInfo - used to transform bounds to view space
*/
void FTranslucentPrimSet::AddScenePrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, const FViewInfo& ViewInfo, bool bUseNormalTranslucency, bool bUseSeparateTranslucency)
{
	const float SortKey = CalculateTranslucentSortKey(PrimitiveSceneInfo, ViewInfo);

	const auto FeatureLevel = ViewInfo.GetFeatureLevel();

	if(bUseSeparateTranslucency 
		&& FeatureLevel >= ERHIFeatureLevel::SM4)
	{
		// add to list of translucent prims that use scene color
		new(SortedSeparateTranslucencyPrims) FSortedPrim(PrimitiveSceneInfo,SortKey,PrimitiveSceneInfo->Proxy->GetTranslucencySortPriority());
	}

	if (bUseNormalTranslucency 
		// Force separate translucency to be rendered normally if the feature level does not support separate translucency
		|| (bUseSeparateTranslucency && FeatureLevel < ERHIFeatureLevel::SM4))
	{
		// add to list of translucent prims
		new(SortedPrims) FSortedPrim(PrimitiveSceneInfo,SortKey,PrimitiveSceneInfo->Proxy->GetTranslucencySortPriority());
	}
}

void FTranslucentPrimSet::AppendScenePrimitives(FSortedPrim* Normal, int32 NumNormal, FSortedPrim* Separate, int32 NumSeparate)
{
	SortedPrims.Append(Normal, NumNormal);
	SortedSeparateTranslucencyPrims.Append(Separate, NumSeparate);
}

void FTranslucentPrimSet::PlaceScenePrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, const FViewInfo& ViewInfo, bool bUseNormalTranslucency, bool bUseSeparateTranslucency, void *NormalPlace, int32& NormalNum, void* SeparatePlace, int32& SeparateNum)
{
	const float SortKey = CalculateTranslucentSortKey(PrimitiveSceneInfo, ViewInfo);
	const auto FeatureLevel = ViewInfo.GetFeatureLevel();
	int32 CVarEnabled = FSceneRenderTargets::CVarSetSeperateTranslucencyEnabled.GetValueOnRenderThread();

	bool bCanBeSeparate = CVarEnabled
		&& FeatureLevel >= ERHIFeatureLevel::SM4
		&& ViewInfo.Family->EngineShowFlags.PostProcessing 
		&& !ViewInfo.Family->EngineShowFlags.ShaderComplexity
		&& ViewInfo.Family->EngineShowFlags.SeparateTranslucency;

	// add to list of sepaate translucency prims 
	if (bUseSeparateTranslucency
		&& bCanBeSeparate
		)
	{
		new (SeparatePlace)FSortedPrim(PrimitiveSceneInfo, SortKey, PrimitiveSceneInfo->Proxy->GetTranslucencySortPriority());
		SeparateNum++;
	}

	// add to list of translucent prims
	else if (bUseNormalTranslucency
		|| !bCanBeSeparate
		)
	{
		new (NormalPlace)FSortedPrim(PrimitiveSceneInfo, SortKey, PrimitiveSceneInfo->Proxy->GetTranslucencySortPriority());
		NormalNum++;
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

class FDrawSortedTransAnyThreadTask
{
	FDeferredShadingSceneRenderer& Renderer;
	FRHICommandList& RHICmdList;
	const FViewInfo& View;
	ETranslucencyPassType TranslucenyPassType;

	const int32 FirstIndex;
	const int32 LastIndex;

public:

	FDrawSortedTransAnyThreadTask(
		FDeferredShadingSceneRenderer& InRenderer,
		FRHICommandList& InRHICmdList,
		const FViewInfo& InView,
		ETranslucencyPassType InTranslucenyPassType,
		int32 InFirstIndex,
		int32 InLastIndex
		)
		: Renderer(InRenderer)
		, RHICmdList(InRHICmdList)
		, View(InView)
		, TranslucenyPassType(InTranslucenyPassType)
		, FirstIndex(InFirstIndex)
		, LastIndex(InLastIndex)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FDrawSortedTransAnyThreadTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyThread;
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		View.TranslucentPrimSet.DrawPrimitivesParallel(RHICmdList, View, Renderer, TranslucenyPassType, FirstIndex, LastIndex);
		RHICmdList.HandleRTThreadTaskCompletion(MyCompletionGraphEvent);
	}
};

class FTranslucencyPassParallelCommandListSet : public FParallelCommandListSet
{
	ETranslucencyPassType TranslucenyPassType;
	bool bFirstTimeThisFrame;
public:
	FTranslucencyPassParallelCommandListSet(const FViewInfo& InView, FRHICommandListImmediate& InParentCmdList, bool bInParallelExecute, bool bInCreateSceneContext, ETranslucencyPassType InTranslucenyPassType)
		: FParallelCommandListSet(InView, InParentCmdList, bInParallelExecute, bInCreateSceneContext)
		, TranslucenyPassType(InTranslucenyPassType)
		, bFirstTimeThisFrame(true)
	{
		SetStateOnCommandList(ParentCmdList);
	}

	virtual ~FTranslucencyPassParallelCommandListSet()
	{
		Dispatch();
	}

	virtual void SetStateOnCommandList(FRHICommandList& CmdList) override
	{
		SetTranslucentRenderTargetAndState(CmdList, View, TranslucenyPassType, bFirstTimeThisFrame);
		bFirstTimeThisFrame = false;
	}
};

static TAutoConsoleVariable<int32> CVarRHICmdTranslucencyPassDeferredContexts(
	TEXT("r.RHICmdTranslucencyPassDeferredContexts"),
	1,
	TEXT("True to use deferred contexts to parallelize base pass command list execution."));

static TAutoConsoleVariable<int32> CVarRHICmdFlushRenderThreadTasksTranslucentPass(
	TEXT("r.RHICmdFlushRenderThreadTasksTranslucentPass"),
	0,
	TEXT("Wait for completion of parallel render thread tasks at the end of the translucent pass.  A more granular version of r.RHICmdFlushRenderThreadTasks. If either r.RHICmdFlushRenderThreadTasks or r.RHICmdFlushRenderThreadTasksTranslucentPass is > 0 we will flush."));

// this is a static because we let the async tasks neyond the function
static FTranslucencyDrawingPolicyFactory::ContextType GParallelTranslucencyContext;

void FDeferredShadingSceneRenderer::RenderTranslucencyParallel(FRHICommandListImmediate& RHICmdList)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SceneContext.AllocLightAttenuation(RHICmdList); // materials will attempt to get this texture before the deferred command to set it up executes
	check(IsInRenderingThread());


	GParallelTranslucencyContext.TranslucentSelfShadow = nullptr;
	GParallelTranslucencyContext.TranslucenyPassType = TPT_NonSeparateTransluceny;
	GParallelTranslucencyContext.bSceneColorCopyIsUpToDate = false;
	FScopedCommandListWaitForTasks Flusher(CVarRHICmdFlushRenderThreadTasksTranslucentPass.GetValueOnRenderThread() > 0 || CVarRHICmdFlushRenderThreadTasks.GetValueOnRenderThread() > 0, RHICmdList);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);

		const FViewInfo& View = Views[ViewIndex];
		{
			if (SceneContext.IsSeparateTranslucencyActive(View))
			{
				// we need to allocate this now so it ends up in the snapshot
				static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.SeparateTranslucencyScreenPercentage"));
				float Scale = CVar->GetValueOnRenderThread() / 100.0f;
				FIntPoint ScaledSize(SceneContext.GetBufferSizeXY().X * Scale, SceneContext.GetBufferSizeXY().Y * Scale );
				SceneContext.GetSeparateTranslucency(RHICmdList, ScaledSize);

				if (Scale<1.0f)
				{
					SceneContext.GetSeparateTranslucencyDepth(RHICmdList, SceneContext.GetBufferSizeXY());
					DownsampleDepthSurface(RHICmdList, SceneContext.GetSeparateTranslucencyDepthSurface(), View, Scale, 1.0f);
				}
			}
			FTranslucencyPassParallelCommandListSet ParallelCommandListSet(View, RHICmdList, 
				CVarRHICmdTranslucencyPassDeferredContexts.GetValueOnRenderThread() > 0, 
				CVarRHICmdFlushRenderThreadTasksTranslucentPass.GetValueOnRenderThread() == 0  && CVarRHICmdFlushRenderThreadTasks.GetValueOnRenderThread() == 0, 
				TPT_NonSeparateTransluceny);

			{
				int32 NumPrims = View.TranslucentPrimSet.NumPrims() - View.TranslucentPrimSet.NumSeparateTranslucencyPrims();
				int32 EffectiveThreads = FMath::Min<int32>(FMath::DivideAndRoundUp(NumPrims, ParallelCommandListSet.MinDrawsPerCommandList), ParallelCommandListSet.Width);

				int32 Start = 0;
				if (EffectiveThreads)
				{

					int32 NumPer = NumPrims / EffectiveThreads;
					int32 Extra = NumPrims - NumPer * EffectiveThreads;


					for (int32 ThreadIndex = 0; ThreadIndex < EffectiveThreads; ThreadIndex++)
					{
						int32 Last = Start + (NumPer - 1) + (ThreadIndex < Extra);
						check(Last >= Start);

						{
							FRHICommandList* CmdList = ParallelCommandListSet.NewParallelCommandList();

							FGraphEventRef AnyThreadCompletionEvent = TGraphTask<FDrawSortedTransAnyThreadTask>::CreateTask(ParallelCommandListSet.GetPrereqs(), ENamedThreads::RenderThread)
								.ConstructAndDispatchWhenReady(*this, *CmdList, View, TPT_NonSeparateTransluceny, Start, Last);

							ParallelCommandListSet.AddParallelCommandList(CmdList, AnyThreadCompletionEvent);
						}
						Start = Last + 1;
					}
				}
			}
			// Draw the view's mesh elements with the translucent drawing policy.
			DrawViewElementsParallel<FTranslucencyDrawingPolicyFactory>(GParallelTranslucencyContext, SDPG_World, false, ParallelCommandListSet);
			// Draw the view's mesh elements with the translucent drawing policy.
			DrawViewElementsParallel<FTranslucencyDrawingPolicyFactory>(GParallelTranslucencyContext, SDPG_Foreground, false, ParallelCommandListSet);

		}
		FinishTranslucentRenderTarget(RHICmdList, View, TPT_NonSeparateTransluceny);

#if 0 // unsupported visualization in the parallel case
		const FSceneViewState* ViewState = (const FSceneViewState*)View.State;
		if (ViewState && View.Family->EngineShowFlags.VisualizeLPV)
		{
			FLightPropagationVolume* LightPropagationVolume = ViewState->GetLightPropagationVolume();

			if (LightPropagationVolume)
			{
				LightPropagationVolume->Visualise(RHICmdList, View);
			}
		}
#endif
		{
			{
				// always call BeginRenderingSeparateTranslucency() even if there are no primitives to we keep the RT allocated
				FTranslucencyPassParallelCommandListSet ParallelCommandListSet(View, 
					RHICmdList, 
					CVarRHICmdTranslucencyPassDeferredContexts.GetValueOnRenderThread() > 0, 
					CVarRHICmdFlushRenderThreadTasksTranslucentPass.GetValueOnRenderThread() == 0 && CVarRHICmdFlushRenderThreadTasks.GetValueOnRenderThread() == 0, 
					TPT_SeparateTransluceny);

				// Draw only translucent prims that are in the SeparateTranslucency pass
				if (View.TranslucentPrimSet.NumSeparateTranslucencyPrims() > 0)
				{
					int32 NumPrims = View.TranslucentPrimSet.NumSeparateTranslucencyPrims();
					int32 EffectiveThreads = FMath::Min<int32>(FMath::DivideAndRoundUp(NumPrims, ParallelCommandListSet.MinDrawsPerCommandList), ParallelCommandListSet.Width);

					int32 Start = 0;
					check(EffectiveThreads);
					{
						int32 NumPer = NumPrims / EffectiveThreads;
						int32 Extra = NumPrims - NumPer * EffectiveThreads;


						for (int32 ThreadIndex = 0; ThreadIndex < EffectiveThreads; ThreadIndex++)
						{
							int32 Last = Start + (NumPer - 1) + (ThreadIndex < Extra);
							check(Last >= Start);

							{
								FRHICommandList* CmdList = ParallelCommandListSet.NewParallelCommandList();

								FGraphEventRef AnyThreadCompletionEvent = TGraphTask<FDrawSortedTransAnyThreadTask>::CreateTask(ParallelCommandListSet.GetPrereqs(), ENamedThreads::RenderThread)
									.ConstructAndDispatchWhenReady(*this, *CmdList, View, TPT_SeparateTransluceny, Start, Last);

								ParallelCommandListSet.AddParallelCommandList(CmdList, AnyThreadCompletionEvent);
							}
							Start = Last + 1;
						}
					}
				}
			}
			SceneContext.FinishRenderingSeparateTranslucency(RHICmdList, View);
		}
	}
}

static TAutoConsoleVariable<int32> CVarParallelTranslucency(
	TEXT("r.ParallelTranslucency"),
	1,
	TEXT("Toggles parallel translucency rendering. Parallel rendering must be enabled for this to have an effect."),
	ECVF_RenderThreadSafe
	);

void FDeferredShadingSceneRenderer::DrawAllTranslucencyPasses(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, ETranslucencyPassType TranslucenyPassType)
{
	// Draw translucent prims
	View.TranslucentPrimSet.DrawPrimitives(RHICmdList, View, *this, TranslucenyPassType);

	FTranslucencyDrawingPolicyFactory::ContextType Context(0, TranslucenyPassType);

	// editor and debug rendering
	DrawViewElements<FTranslucencyDrawingPolicyFactory>(RHICmdList, View, Context, SDPG_World, false);
	DrawViewElements<FTranslucencyDrawingPolicyFactory>(RHICmdList, View, Context, SDPG_Foreground, false);
}

void FDeferredShadingSceneRenderer::RenderTranslucency(FRHICommandListImmediate& RHICmdList)
{
	if (ShouldRenderTranslucency())
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.SeparateTranslucencyScreenPercentage"));
		GScreenTextureUVScale = 100.0f / CVar->GetValueOnRenderThread();

		SCOPED_DRAW_EVENT(RHICmdList, Translucency);

		if (GRHICommandList.UseParallelAlgorithms() && CVarParallelTranslucency.GetValueOnRenderThread())
		{
			RenderTranslucencyParallel(RHICmdList);
			return;
		}

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);

			const FViewInfo& View = Views[ViewIndex];

			// non separate translucency
			{
				bool bFirstTimeThisFrame = (ViewIndex == 0);
				SetTranslucentRenderTargetAndState(RHICmdList, View, TPT_NonSeparateTransluceny, bFirstTimeThisFrame);

				DrawAllTranslucencyPasses(RHICmdList, View, TPT_NonSeparateTransluceny);

				const FSceneViewState* ViewState = (const FSceneViewState*)View.State;

				if (ViewState && View.Family->EngineShowFlags.VisualizeLPV)
				{
					FLightPropagationVolume* LightPropagationVolume = ViewState->GetLightPropagationVolume(View.GetFeatureLevel());

					if (LightPropagationVolume)
					{
						LightPropagationVolume->Visualise(RHICmdList, View);
					}
				}

				FinishTranslucentRenderTarget(RHICmdList, View, TPT_NonSeparateTransluceny);
			}
			
			// separate translucency
			{
				FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
				if (SceneContext.IsSeparateTranslucencyActive(View))
				{
					// always call BeginRenderingSeparateTranslucency() even if there are no primitives to we keep the RT allocated
					static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.SeparateTranslucencyScreenPercentage"));
					float Scale = CVar->GetValueOnRenderThread() / 100.0f;
					if (Scale < 1.0f)
					{
						SceneContext.GetSeparateTranslucencyDepth(RHICmdList, SceneContext.GetBufferSizeXY());
						DownsampleDepthSurface(RHICmdList, SceneContext.GetSeparateTranslucencyDepthSurface(), View, Scale, 1.0f);
					}
					bool bFirstTimeThisFrame = (ViewIndex == 0);
					bool bSetupTranslucency = SceneContext.BeginRenderingSeparateTranslucency(RHICmdList, View, bFirstTimeThisFrame);

					const TIndirectArray<FMeshBatch>& WorldList = View.ViewMeshElements;
					const TIndirectArray<FMeshBatch>& ForegroundList = View.TopViewMeshElements;

					bool bRenderSeparateTranslucency = View.TranslucentPrimSet.NumSeparateTranslucencyPrims() > 0 || WorldList.Num() || ForegroundList.Num();

					// Draw only translucent prims that are in the SeparateTranslucency pass
					if (bRenderSeparateTranslucency)
					{
						if (bSetupTranslucency)
						{
							RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
						}
					
						DrawAllTranslucencyPasses(RHICmdList, View, TPT_SeparateTransluceny);
					}

					SceneContext.FinishRenderingSeparateTranslucency(RHICmdList, View);
				}
			}
		}
	}
}
