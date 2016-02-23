// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneRendering.cpp: Scene rendering.
=============================================================================*/

#include "RendererPrivate.h"
#include "Engine.h"
#include "ScenePrivate.h"
#include "ScreenRendering.h"
#include "SceneFilterRendering.h"
#include "VisualizeTexture.h"
#include "PostProcessEyeAdaptation.h"
#include "CompositionLighting.h"
#include "FXSystem.h"
#include "SceneViewExtension.h"
#include "PostProcessBusyWait.h"
#include "PostProcessCircleDOF.h"
#include "SceneUtils.h"
#include "LightGrid.h"

/*-----------------------------------------------------------------------------
	Globals
-----------------------------------------------------------------------------*/

extern ENGINE_API FLightMap2D* GDebugSelectedLightmap;
extern ENGINE_API UPrimitiveComponent* GDebugSelectedComponent;

TGlobalResource<FLightGridVertexBuffer> GLightGridVertexBuffer;

/**
 * Console variable controlling whether or not occlusion queries are allowed.
 */
static TAutoConsoleVariable<int32> CVarAllowOcclusionQueries(
	TEXT("r.AllowOcclusionQueries"),
	1,
	TEXT("If zero, occlusion queries will not be used to cull primitives."),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarDemosaicVposOffset(
	TEXT("r.DemosaicVposOffset"),
	0.0f,
	TEXT("This offset is added to the rasterized position used for demosaic in the ES2 tonemapping shader. It exists to workaround driver bugs on some Android devices that have a half-pixel offset."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarForwardLighting(
	TEXT("r.ForwardLighting"),
	0,
	TEXT("Experimental dynamic forward lighting for translucency. Can be the base for opaque forward lighting which will allow\n")
	TEXT("more lighting models or rendering without a GBuffer.\n")
	TEXT("The current implementation is limited to 32 lights, coarse 2d culling (on CPU), no shadows and simple shading (no area lights, phong).\n")
	TEXT("Enabled with the new TranslucencyLightingMode SurfacePerPixelLighting\n")
	TEXT("0: off (default)\n")
	TEXT("1: on (some CPU setup cost on GPU and extra per pixel cost)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRefractionQuality(
	TEXT("r.RefractionQuality"),
	2,
	TEXT("Defines the distorion/refraction quality which allows to adjust for quality or performance.\n")
	TEXT("<=0: off (fastest)\n")
	TEXT("  1: low quality (not yet implemented)\n")
	TEXT("  2: normal quality (default)\n")
	TEXT("  3: high quality (e.g. color fringe, not yet implemented)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarInstancedStereo(
	TEXT("vr.InstancedStereo"),
	0,
	TEXT("0 to disable instanced stereo, 1 to enable."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
static TAutoConsoleVariable<float> CVarGeneralPurposeTweak(
	TEXT("r.GeneralPurposeTweak"),
	1.0f,
	TEXT("Useful for low level shader development to get quick iteration time without having to change any c++ code.\n")
	TEXT("Value maps to View.GeneralPurposeTweak inside the shaders.\n")
	TEXT("Example usage: Multiplier on some value to tweak, toggle to switch between different algorithms (Default: 1.0)\n")
	TEXT("DON'T USE THIS FOR ANYTHING THAT IS CHECKED IN. Compiled out in SHIPPING to make cheating a bit harder."),
	ECVF_RenderThreadSafe);

// should be changed to BaseColor and Metallic, since some time now UE4 is not using DiffuseColor and SpecularColor any more
static TAutoConsoleVariable<float> CVarDiffuseColorMin(
	TEXT("r.DiffuseColor.Min"),
	0.0f,
	TEXT("Allows quick material test by remapping the diffuse color at 1 to a new value (0..1), Only for non shipping built!\n")
	TEXT("1: (default)"),
	ECVF_Cheat | ECVF_RenderThreadSafe
	);
static TAutoConsoleVariable<float> CVarDiffuseColorMax(
	TEXT("r.DiffuseColor.Max"),
	1.0f,
	TEXT("Allows quick material test by remapping the diffuse color at 1 to a new value (0..1), Only for non shipping built!\n")
	TEXT("1: (default)"),
	ECVF_Cheat | ECVF_RenderThreadSafe
	);
static TAutoConsoleVariable<float> CVarRoughnessMin(
	TEXT("r.Roughness.Min"),
	0.0f,
	TEXT("Allows quick material test by remapping the roughness at 0 to a new value (0..1), Only for non shipping built!\n")
	TEXT("0: (default)"),
	ECVF_Cheat | ECVF_RenderThreadSafe
	);
static TAutoConsoleVariable<float> CVarRoughnessMax(
	TEXT("r.Roughness.Max"),
	1.0f,
	TEXT("Allows quick material test by remapping the roughness at 1 to a new value (0..1), Only for non shipping built!\n")
	TEXT("1: (default)"),
	ECVF_Cheat | ECVF_RenderThreadSafe
	);
#endif

/**
 * Console variable controlling the maximum number of shadow cascades to render with.
 *   DO NOT READ ON THE RENDERING THREAD. Use FSceneView::MaxShadowCascades.
 */
static TAutoConsoleVariable<int32> CVarMaxShadowCascades(
	TEXT("r.Shadow.CSM.MaxCascades"),
	10,
	TEXT("The maximum number of cascades with which to render dynamic directional light shadows."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarTessellationAdaptivePixelsPerTriangle(
	TEXT("r.TessellationAdaptivePixelsPerTriangle"),
	48.0f,
	TEXT("Global tessellation factor multiplier"),
	ECVF_RenderThreadSafe);

/*-----------------------------------------------------------------------------
	FParallelCommandListSet
-----------------------------------------------------------------------------*/


static TAutoConsoleVariable<int32> CVarRHICmdSpewParallelListBalance(
	TEXT("r.RHICmdSpewParallelListBalance"),
	0,
	TEXT("For debugging, spews the size of the parallel command lists. This stalls and otherwise wrecks performance.\n")
	TEXT(" 0: off (default)\n")
	TEXT(" 1: enabled (default)"));

static TAutoConsoleVariable<int32> CVarRHICmdBalanceParallelLists(
	TEXT("r.RHICmdBalanceParallelLists"),
	1,
	TEXT("Allows to enable a preprocess of the drawlists to try to balance the load equally among the command lists.\n")
	TEXT(" 0: off \n")
	TEXT(" 1: enabled")
	TEXT(" 2: experiemental, uses previous frame results (does not do anything in split screen etc)"));

static TAutoConsoleVariable<int32> CVarRHICmdMinCmdlistForParallelSubmit(
	TEXT("r.RHICmdMinCmdlistForParallelSubmit"),
	2,
	TEXT("Minimum number of parallel translate command lists to submit. If there are fewer than this number, they just run on the RHI thread and immediate context."));

static TAutoConsoleVariable<int32> CVarRHICmdMinDrawsPerParallelCmdList(
	TEXT("r.RHICmdMinDrawsPerParallelCmdList"),
	32,
	TEXT("The minimum number of draws per cmdlist. If the total number of draws is less than this, then no parallel work will be done at all. This can't always be honored or done correctly. More effective with RHICmdBalanceParallelLists."));

FParallelCommandListSet::FParallelCommandListSet(const FViewInfo& InView, FRHICommandListImmediate& InParentCmdList, bool bInParallelExecute, bool bInCreateSceneContext)
	: View(InView)
	, ParentCmdList(InParentCmdList)
	, Snapshot(nullptr)
	, NumAlloc(0)
	, bParallelExecute(GRHISupportsParallelRHIExecute && bInParallelExecute)
	, bCreateSceneContext(bInCreateSceneContext)
{
	Width = CVarRHICmdWidth.GetValueOnRenderThread();
	MinDrawsPerCommandList = CVarRHICmdMinDrawsPerParallelCmdList.GetValueOnRenderThread();
	bSpewBalance = !!CVarRHICmdSpewParallelListBalance.GetValueOnRenderThread();
	int32 IntBalance = CVarRHICmdBalanceParallelLists.GetValueOnRenderThread();
	bBalanceCommands = !!IntBalance;
	bBalanceCommandsWithLastFrame = IntBalance > 1;
	CommandLists.Reserve(Width * 8);
	Events.Reserve(Width * 8);
	NumDrawsIfKnown.Reserve(Width * 8);
}

FRHICommandList* FParallelCommandListSet::AllocCommandList()
{
	NumAlloc++;
	return new FRHICommandList;
}

void FParallelCommandListSet::Dispatch()
{
	check(IsInRenderingThread() && FMemStack::Get().GetNumMarks() == 1); // we do not want this popped before the end of the scene and it better be the scene allocator
	check(CommandLists.Num() == Events.Num());
	check(CommandLists.Num() == NumAlloc);
	if (bSpewBalance)
	{
		// finish them all
		for (auto& Event : Events)
		{
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Event, ENamedThreads::RenderThread_Local);
		}
		// spew sizes
		int32 Index = 0;
		for (auto CmdList : CommandLists)
		{
			UE_LOG(LogTemp, Display, TEXT("CmdList %2d/%2d  : %8dKB"), Index, CommandLists.Num(), (CmdList->GetUsedMemory() + 1023) / 1024);
			Index++;
		}
	}
	bool bActuallyDoParallelTranslate = bParallelExecute && CommandLists.Num() >= CVarRHICmdMinCmdlistForParallelSubmit.GetValueOnRenderThread();
	if (bActuallyDoParallelTranslate)
	{
		int32 Total = 0;
		bool bIndeterminate = false;
		for (int32 Count : NumDrawsIfKnown)
		{
			if (Count < 0)
			{
				bIndeterminate = true;
				break; // can't determine how many are in this one; assume we should run parallel translate
			}
			Total += Count;
		}
		if (!bIndeterminate && Total < MinDrawsPerCommandList)
		{
			UE_CLOG(bSpewBalance, LogTemp, Display, TEXT("Disabling parallel translate because the number of draws is known to be small."));
			bActuallyDoParallelTranslate = false;
		}
	}

	if (bActuallyDoParallelTranslate)
	{
		UE_CLOG(bSpewBalance, LogTemp, Display, TEXT("%d cmdlists for parallel translate"), CommandLists.Num());
		check(GRHISupportsParallelRHIExecute);
		NumAlloc -= CommandLists.Num();
		ParentCmdList.QueueParallelAsyncCommandListSubmit(&Events[0], &CommandLists[0], &NumDrawsIfKnown[0], CommandLists.Num(), (MinDrawsPerCommandList * 4) / 3, bSpewBalance);
		SetStateOnCommandList(ParentCmdList);
	}
	else
	{
		UE_CLOG(bSpewBalance, LogTemp, Display, TEXT("%d cmdlists (no parallel translate desired)"), CommandLists.Num());
		for (int32 Index = 0; Index < CommandLists.Num(); Index++)
		{
			ParentCmdList.QueueAsyncCommandListSubmit(Events[Index], CommandLists[Index]);
			NumAlloc--;
		}
	}
	CommandLists.Reset();
	Snapshot = nullptr;
	Events.Reset();
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FParallelCommandListSet_Dispatch_ServiceLocalQueue);
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::RenderThread_Local);
}

FParallelCommandListSet::~FParallelCommandListSet()
{
	check(IsInRenderingThread() && FMemStack::Get().GetNumMarks() == 1); // we do not want this popped before the end of the scene and it better be the scene allocator
	checkf(CommandLists.Num() == 0, TEXT("Derived class of FParallelCommandListSet did not call Dispatch in virtual destructor"));
	checkf(NumAlloc == 0, TEXT("Derived class of FParallelCommandListSet did not call Dispatch in virtual destructor"));
}

FRHICommandList* FParallelCommandListSet::NewParallelCommandList()
{
	FRHICommandList* Result = AllocCommandList();
	SetStateOnCommandList(*Result); 
	if (bCreateSceneContext)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(ParentCmdList);
		check(&SceneContext == &FSceneRenderTargets::Get_FrameConstantsOnly()); // the immediate should not have an overridden context
		if (!Snapshot)
		{
			Snapshot = SceneContext.CreateSnapshot(View);
		}
		Snapshot->SetSnapshotOnCmdList(*Result);
		check(&SceneContext != &FSceneRenderTargets::Get(*Result)); // the new commandlist should have a snapshot
	}
	return Result;
}

void FParallelCommandListSet::AddParallelCommandList(FRHICommandList* CmdList, FGraphEventRef& CompletionEvent, int32 InNumDrawsIfKnown)
{
	check(IsInRenderingThread() && FMemStack::Get().GetNumMarks() == 1); // we do not want this popped before the end of the scene and it better be the scene allocator
	check(CommandLists.Num() == Events.Num());
	CommandLists.Add(CmdList);
	Events.Add(CompletionEvent);
	NumDrawsIfKnown.Add(InNumDrawsIfKnown);
}



/*-----------------------------------------------------------------------------
	FViewInfo
-----------------------------------------------------------------------------*/

/** 
 * Initialization constructor. Passes all parameters to FSceneView constructor
 */
FViewInfo::FViewInfo(const FSceneViewInitOptions& InitOptions)
	:	FSceneView(InitOptions)
	,	IndividualOcclusionQueries((FSceneViewState*)InitOptions.SceneViewStateInterface, 1)
	,	GroupedOcclusionQueries((FSceneViewState*)InitOptions.SceneViewStateInterface, FOcclusionQueryBatcher::OccludedPrimitiveQueryBatchSize)
{
	Init();
}

/** 
 * Initialization constructor. 
 * @param InView - copy to init with
 */
FViewInfo::FViewInfo(const FSceneView* InView)
	:	FSceneView(*InView)
	,	IndividualOcclusionQueries((FSceneViewState*)InView->State,1)
	,	GroupedOcclusionQueries((FSceneViewState*)InView->State,FOcclusionQueryBatcher::OccludedPrimitiveQueryBatchSize)
	,	CustomVisibilityQuery(nullptr)
{
	Init();
}

void FViewInfo::Init()
{
	bHasTranslucentViewMeshElements = 0;
	bPrevTransformsReset = false;
	bIgnoreExistingQueries = false;
	bDisableQuerySubmissions = false;
	bDisableDistanceBasedFadeTransitions = false;	
	ShadingModelMaskInView = 0;
	NumVisibleStaticMeshElements = 0;
	PrecomputedVisibilityData = 0;
	bSceneHasDecals = 0;

	bIsViewInfo = true;
	PrevViewProjMatrix.SetIdentity();
	PrevViewRotationProjMatrix.SetIdentity();
	
	bUsesGlobalDistanceField = false;
	bUsesLightingChannels = false;

	ExponentialFogParameters = FVector4(0,1,1,0);
	ExponentialFogColor = FVector::ZeroVector;
	FogMaxOpacity = 1;

	bUseDirectionalInscattering = false;
	DirectionalInscatteringExponent = 0;
	DirectionalInscatteringStartDistance = 0;
	InscatteringLightDirection = FVector(0);
	DirectionalInscatteringColor = FLinearColor(ForceInit);

	for (int32 CascadeIndex = 0; CascadeIndex < TVC_MAX; CascadeIndex++)
	{
		TranslucencyLightingVolumeMin[CascadeIndex] = FVector(0);
		TranslucencyVolumeVoxelSize[CascadeIndex] = 0;
		TranslucencyLightingVolumeSize[CascadeIndex] = FVector(0);
	}

	MaxShadowCascades = FMath::Clamp<int32>(CVarMaxShadowCascades.GetValueOnAnyThread(), 1, 10);

	ShaderMap = GetGlobalShaderMap(FeatureLevel);

	ViewState = (FSceneViewState*)State;
	bIsSnapshot = false;

	bAllowStencilDither = false;
}

FViewInfo::~FViewInfo()
{
	for(int32 ResourceIndex = 0;ResourceIndex < DynamicResources.Num();ResourceIndex++)
	{
		DynamicResources[ResourceIndex]->ReleasePrimitiveResource();
	}
	if (CustomVisibilityQuery)
	{
		CustomVisibilityQuery->Release();
	}
}

void FViewInfo::SetupSkyIrradianceEnvironmentMapConstants(FVector4* OutSkyIrradianceEnvironmentMap) const
{
	FScene* Scene = (FScene*)Family->Scene;

	if (Scene 
		&& Scene->SkyLight 
		// Skylights with static lighting already had their diffuse contribution baked into lightmaps
		&& !Scene->SkyLight->bHasStaticLighting
		&& Family->EngineShowFlags.SkyLighting)
	{
		const FSHVectorRGB3& SkyIrradiance = Scene->SkyLight->IrradianceEnvironmentMap;

		const float SqrtPI = FMath::Sqrt(PI);
		const float Coefficient0 = 1.0f / (2 * SqrtPI);
		const float Coefficient1 = FMath::Sqrt(3) / (3 * SqrtPI);
		const float Coefficient2 = FMath::Sqrt(15) / (8 * SqrtPI);
		const float Coefficient3 = FMath::Sqrt(5) / (16 * SqrtPI);
		const float Coefficient4 = .5f * Coefficient2;

		// Pack the SH coefficients in a way that makes applying the lighting use the least shader instructions
		// This has the diffuse convolution coefficients baked in
		// See "Stupid Spherical Harmonics (SH) Tricks"
		OutSkyIrradianceEnvironmentMap[0].X = -Coefficient1 * SkyIrradiance.R.V[3];
		OutSkyIrradianceEnvironmentMap[0].Y = -Coefficient1 * SkyIrradiance.R.V[1];
		OutSkyIrradianceEnvironmentMap[0].Z = Coefficient1 * SkyIrradiance.R.V[2];
		OutSkyIrradianceEnvironmentMap[0].W = Coefficient0 * SkyIrradiance.R.V[0] - Coefficient3 * SkyIrradiance.R.V[6];

		OutSkyIrradianceEnvironmentMap[1].X = -Coefficient1 * SkyIrradiance.G.V[3];
		OutSkyIrradianceEnvironmentMap[1].Y = -Coefficient1 * SkyIrradiance.G.V[1];
		OutSkyIrradianceEnvironmentMap[1].Z = Coefficient1 * SkyIrradiance.G.V[2];
		OutSkyIrradianceEnvironmentMap[1].W = Coefficient0 * SkyIrradiance.G.V[0] - Coefficient3 * SkyIrradiance.G.V[6];

		OutSkyIrradianceEnvironmentMap[2].X = -Coefficient1 * SkyIrradiance.B.V[3];
		OutSkyIrradianceEnvironmentMap[2].Y = -Coefficient1 * SkyIrradiance.B.V[1];
		OutSkyIrradianceEnvironmentMap[2].Z = Coefficient1 * SkyIrradiance.B.V[2];
		OutSkyIrradianceEnvironmentMap[2].W = Coefficient0 * SkyIrradiance.B.V[0] - Coefficient3 * SkyIrradiance.B.V[6];

		OutSkyIrradianceEnvironmentMap[3].X = Coefficient2 * SkyIrradiance.R.V[4];
		OutSkyIrradianceEnvironmentMap[3].Y = -Coefficient2 * SkyIrradiance.R.V[5];
		OutSkyIrradianceEnvironmentMap[3].Z = 3 * Coefficient3 * SkyIrradiance.R.V[6];
		OutSkyIrradianceEnvironmentMap[3].W = -Coefficient2 * SkyIrradiance.R.V[7];

		OutSkyIrradianceEnvironmentMap[4].X = Coefficient2 * SkyIrradiance.G.V[4];
		OutSkyIrradianceEnvironmentMap[4].Y = -Coefficient2 * SkyIrradiance.G.V[5];
		OutSkyIrradianceEnvironmentMap[4].Z = 3 * Coefficient3 * SkyIrradiance.G.V[6];
		OutSkyIrradianceEnvironmentMap[4].W = -Coefficient2 * SkyIrradiance.G.V[7];

		OutSkyIrradianceEnvironmentMap[5].X = Coefficient2 * SkyIrradiance.B.V[4];
		OutSkyIrradianceEnvironmentMap[5].Y = -Coefficient2 * SkyIrradiance.B.V[5];
		OutSkyIrradianceEnvironmentMap[5].Z = 3 * Coefficient3 * SkyIrradiance.B.V[6];
		OutSkyIrradianceEnvironmentMap[5].W = -Coefficient2 * SkyIrradiance.B.V[7];

		OutSkyIrradianceEnvironmentMap[6].X = Coefficient4 * SkyIrradiance.R.V[8];
		OutSkyIrradianceEnvironmentMap[6].Y = Coefficient4 * SkyIrradiance.G.V[8];
		OutSkyIrradianceEnvironmentMap[6].Z = Coefficient4 * SkyIrradiance.B.V[8];
		OutSkyIrradianceEnvironmentMap[6].W = 1;
	}
	else
	{
		FMemory::Memzero(OutSkyIrradianceEnvironmentMap, sizeof(FVector4) * 7);
	}
}

/** Creates the view's uniform buffers given a set of view transforms. */
void FViewInfo::CreateUniformBuffer(
	TUniformBufferRef<FViewUniformShaderParameters>& OutViewUniformBuffer,
	TUniformBufferRef<FFrameUniformShaderParameters>& OutFrameUniformBuffer,
	FRHICommandList& RHICmdList,
	const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>* DirectionalLightShadowInfo,	
	const FMatrix& EffectiveTranslatedViewMatrix, 
	const FMatrix& EffectiveViewToTranslatedWorld, 
	FBox* OutTranslucentCascadeBoundsArray, 
	int32 NumTranslucentCascades) const
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	check(Family);
	check(!DirectionalLightShadowInfo || DirectionalLightShadowInfo->Num() > 0);

	// Calculate the vector used by shaders to convert clip space coordinates to texture space.
	const FIntPoint BufferSize = SceneContext.GetBufferSizeXY();
	const float InvBufferSizeX = 1.0f / BufferSize.X;
	const float InvBufferSizeY = 1.0f / BufferSize.Y;
	// to bring NDC (-1..1, 1..-1) into 0..1 UV for BufferSize textures
	const FVector4 ScreenPositionScaleBias(
		ViewRect.Width() * InvBufferSizeX / +2.0f,
		ViewRect.Height() * InvBufferSizeY / (-2.0f * GProjectionSignY),
		(ViewRect.Height() / 2.0f + ViewRect.Min.Y) * InvBufferSizeY,
		(ViewRect.Width() / 2.0f + ViewRect.Min.X) * InvBufferSizeX
		);
	
	FVector4 LocalDiffuseOverrideParameter = DiffuseOverrideParameter;
	FVector2D LocalRoughnessOverrideParameter = RoughnessOverrideParameter;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		// assuming we have no color in the multipliers
		float MinValue = LocalDiffuseOverrideParameter.X;
		float MaxValue = MinValue + LocalDiffuseOverrideParameter.W;

		float NewMinValue = FMath::Max(MinValue, CVarDiffuseColorMin.GetValueOnRenderThread());
		float NewMaxValue = FMath::Min(MaxValue, CVarDiffuseColorMax.GetValueOnRenderThread());

		LocalDiffuseOverrideParameter.X = LocalDiffuseOverrideParameter.Y = LocalDiffuseOverrideParameter.Z = NewMinValue;
		LocalDiffuseOverrideParameter.W = NewMaxValue - NewMinValue;
	}
	{
		float MinValue = LocalRoughnessOverrideParameter.X;
		float MaxValue = MinValue + LocalRoughnessOverrideParameter.Y;

		float NewMinValue = FMath::Max(MinValue, CVarRoughnessMin.GetValueOnRenderThread());
		float NewMaxValue = FMath::Min(MaxValue, CVarRoughnessMax.GetValueOnRenderThread());

		LocalRoughnessOverrideParameter.X = NewMinValue;
		LocalRoughnessOverrideParameter.Y = NewMaxValue - NewMinValue;
	}

#endif

	const bool bIsUnlitView = !Family->EngineShowFlags.Lighting;

	// Create the view's uniform buffer.
	// TODO: We should use a view and previous view uniform buffer to avoid code duplication and keep consistency
	FViewUniformShaderParameters ViewUniformShaderParameters;
	FFrameUniformShaderParameters FrameUniformShaderParameters;

	ViewUniformShaderParameters.TranslatedWorldToClip = ViewMatrices.TranslatedViewProjectionMatrix;
	ViewUniformShaderParameters.WorldToClip = ViewProjectionMatrix;
	ViewUniformShaderParameters.TranslatedWorldToView = EffectiveTranslatedViewMatrix;
	ViewUniformShaderParameters.ViewToTranslatedWorld = EffectiveViewToTranslatedWorld;
	ViewUniformShaderParameters.TranslatedWorldToCameraView = ViewMatrices.TranslatedViewMatrix;
	ViewUniformShaderParameters.CameraViewToTranslatedWorld = ViewUniformShaderParameters.TranslatedWorldToCameraView.Inverse();
	ViewUniformShaderParameters.ViewToClip = ViewMatrices.ProjMatrix;
	ViewUniformShaderParameters.ClipToView = ViewMatrices.GetInvProjMatrix();
	ViewUniformShaderParameters.ClipToTranslatedWorld = ViewMatrices.InvTranslatedViewProjectionMatrix;
	ViewUniformShaderParameters.ViewForward = EffectiveTranslatedViewMatrix.GetColumn(2);
	ViewUniformShaderParameters.ViewUp = EffectiveTranslatedViewMatrix.GetColumn(1);
	ViewUniformShaderParameters.ViewRight = EffectiveTranslatedViewMatrix.GetColumn(0);
	ViewUniformShaderParameters.InvDeviceZToWorldZTransform = InvDeviceZToWorldZTransform;
	ViewUniformShaderParameters.WorldViewOrigin = EffectiveViewToTranslatedWorld.TransformPosition(FVector(0)) - ViewMatrices.PreViewTranslation;
	ViewUniformShaderParameters.WorldCameraOrigin = ViewMatrices.ViewOrigin;
	ViewUniformShaderParameters.TranslatedWorldCameraOrigin = ViewMatrices.ViewOrigin + ViewMatrices.PreViewTranslation;
	ViewUniformShaderParameters.PreViewTranslation = ViewMatrices.PreViewTranslation;
	ViewUniformShaderParameters.PrevProjection = PrevViewMatrices.ProjMatrix;
	ViewUniformShaderParameters.PrevViewProj = PrevViewProjMatrix;
	ViewUniformShaderParameters.PrevViewRotationProj = PrevViewRotationProjMatrix;
	ViewUniformShaderParameters.PrevViewToClip = PrevViewMatrices.ProjMatrix;
	ViewUniformShaderParameters.PrevClipToView = PrevViewMatrices.GetInvProjMatrix();
	ViewUniformShaderParameters.PrevTranslatedWorldToClip = PrevViewMatrices.TranslatedViewProjectionMatrix;
	// EffectiveTranslatedViewMatrix != ViewMatrices.TranslatedViewMatrix in the shadow pass
	// and we don't have EffectiveTranslatedViewMatrix for the previous frame to set up PrevTranslatedWorldToView
	// but that is fine to set up PrevTranslatedWorldToView as same as PrevTranslatedWorldToCameraView
	// since the shadow pass doesn't require previous frame computation.
	ViewUniformShaderParameters.PrevTranslatedWorldToView = PrevViewMatrices.TranslatedViewMatrix;
	ViewUniformShaderParameters.PrevViewToTranslatedWorld = ViewUniformShaderParameters.PrevTranslatedWorldToView.Inverse();
	ViewUniformShaderParameters.PrevTranslatedWorldToCameraView = PrevViewMatrices.TranslatedViewMatrix;
	ViewUniformShaderParameters.PrevCameraViewToTranslatedWorld = ViewUniformShaderParameters.PrevTranslatedWorldToCameraView.Inverse();
	ViewUniformShaderParameters.PrevWorldCameraOrigin = PrevViewMatrices.ViewOrigin;
	// previous view world origin is going to be needed only in the base pass or shadow pass
	// therefore is same as previous camera world origin.
	ViewUniformShaderParameters.PrevWorldViewOrigin = ViewUniformShaderParameters.PrevWorldCameraOrigin;
	ViewUniformShaderParameters.PrevPreViewTranslation = PrevViewMatrices.PreViewTranslation;
	// can be optimized
	ViewUniformShaderParameters.PrevInvViewProj = PrevViewProjMatrix.Inverse();
	ViewUniformShaderParameters.ScreenPositionScaleBias = ScreenPositionScaleBias;

	FrameUniformShaderParameters.FieldOfViewWideAngles = 2.f * ViewMatrices.GetHalfFieldOfViewPerAxis();
	FrameUniformShaderParameters.PrevFieldOfViewWideAngles = 2.f * PrevViewMatrices.GetHalfFieldOfViewPerAxis();
	FrameUniformShaderParameters.ViewRectMin = FVector4(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, 0.0f);
	FrameUniformShaderParameters.ViewSizeAndInvSize = FVector4(ViewRect.Width(), ViewRect.Height(), 1.0f / float(ViewRect.Width()), 1.0f / float(ViewRect.Height()));
	FrameUniformShaderParameters.BufferSizeAndInvSize = FVector4(BufferSize.X, BufferSize.Y, InvBufferSizeX, InvBufferSizeY);
	FrameUniformShaderParameters.DiffuseOverrideParameter = LocalDiffuseOverrideParameter;
	FrameUniformShaderParameters.SpecularOverrideParameter = SpecularOverrideParameter;
	FrameUniformShaderParameters.NormalOverrideParameter = NormalOverrideParameter;
	FrameUniformShaderParameters.RoughnessOverrideParameter = LocalRoughnessOverrideParameter;
	FrameUniformShaderParameters.PrevFrameGameTime = Family->CurrentWorldTime - Family->DeltaWorldTime;
	FrameUniformShaderParameters.PrevFrameRealTime = Family->CurrentRealTime - Family->DeltaWorldTime;
	FrameUniformShaderParameters.WorldCameraMovementSinceLastFrame = ViewMatrices.ViewOrigin - PrevViewMatrices.ViewOrigin;
	FrameUniformShaderParameters.CullingSign = bReverseCulling ? -1.0f : 1.0f;
	FrameUniformShaderParameters.NearPlane = GNearClippingPlane;

	{
		// setup a matrix to transform float4(SvPosition.xyz,1) directly to TranslatedWorld (quality, performance as we don't need to convert or use interpolator)

		//	new_xy = (xy - ViewRectMin.xy) * ViewSizeAndInvSize.zw * float2(2,-2) + float2(-1, 1);

		//  transformed into one MAD:  new_xy = xy * ViewSizeAndInvSize.zw * float2(2,-2)      +       (-ViewRectMin.xy) * ViewSizeAndInvSize.zw * float2(2,-2) + float2(-1, 1);

		float Mx = 2.0f * FrameUniformShaderParameters.ViewSizeAndInvSize.Z;
		float My = -2.0f * FrameUniformShaderParameters.ViewSizeAndInvSize.W;
		float Ax = -1.0f - 2.0f * ViewRect.Min.X * FrameUniformShaderParameters.ViewSizeAndInvSize.Z;
		float Ay = 1.0f + 2.0f * ViewRect.Min.Y * FrameUniformShaderParameters.ViewSizeAndInvSize.W;

		// http://stackoverflow.com/questions/9010546/java-transformation-matrix-operations

		ViewUniformShaderParameters.SVPositionToTranslatedWorld = 
			FMatrix(FPlane(Mx,   0,  0,   0),
					FPlane( 0,  My,  0,   0),
					FPlane( 0,   0,  1,   0),
					FPlane(Ax,  Ay,  0,   1)) * ViewMatrices.InvTranslatedViewProjectionMatrix;
	}

	ViewUniformShaderParameters.ScreenToWorld = FMatrix(
		FPlane(1,0,0,0),
		FPlane(0,1,0,0),
		FPlane(0,0,ProjectionMatrixUnadjustedForRHI.M[2][2],1),
		FPlane(0,0,ProjectionMatrixUnadjustedForRHI.M[3][2],0))
		* InvViewProjectionMatrix;

	ViewUniformShaderParameters.ScreenToTranslatedWorld = FMatrix(
		FPlane(1,0,0,0),
		FPlane(0,1,0,0),
		FPlane(0,0,ProjectionMatrixUnadjustedForRHI.M[2][2],1),
		FPlane(0,0,ProjectionMatrixUnadjustedForRHI.M[3][2],0))
		* ViewMatrices.InvTranslatedViewProjectionMatrix;

	ViewUniformShaderParameters.PrevScreenToTranslatedWorld = FMatrix(
		FPlane(1,0,0,0),
		FPlane(0,1,0,0),
		FPlane(0,0,ProjectionMatrixUnadjustedForRHI.M[2][2],1),
		FPlane(0,0,ProjectionMatrixUnadjustedForRHI.M[3][2],0))
		* PrevViewMatrices.InvTranslatedViewProjectionMatrix;

	FVector DeltaTranslation = PrevViewMatrices.PreViewTranslation - ViewMatrices.PreViewTranslation;
	FMatrix InvViewProj = ViewMatrices.GetInvProjNoAAMatrix() * ViewMatrices.TranslatedViewMatrix.GetTransposed();
	FMatrix PrevViewProj = FTranslationMatrix( DeltaTranslation ) * PrevViewMatrices.TranslatedViewMatrix * PrevViewMatrices.GetProjNoAAMatrix();

	ViewUniformShaderParameters.ClipToPrevClip = InvViewProj * PrevViewProj;

	// is getting clamped in the shader to a value larger than 0 (we don't want the triangles to disappear)
	FrameUniformShaderParameters.AdaptiveTessellationFactor = 0.0f;

	if(Family->EngineShowFlags.Tessellation)
	{
		// CVar setting is pixels/tri which is nice and intuitive.  But we want pixels/tessellated edge.  So use a heuristic.
		float TessellationAdaptivePixelsPerEdge = FMath::Sqrt(2.f * CVarTessellationAdaptivePixelsPerTriangle.GetValueOnRenderThread());

		FrameUniformShaderParameters.AdaptiveTessellationFactor = 0.5f * ViewMatrices.ProjMatrix.M[1][1] * float(ViewRect.Height()) / TessellationAdaptivePixelsPerEdge;
	}

	//white texture should act like a shadowmap cleared to the farplane.
	FrameUniformShaderParameters.DirectionalLightShadowTexture = GWhiteTexture->TextureRHI;
	FrameUniformShaderParameters.DirectionalLightShadowSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	if (Family->Scene)
	{
		FScene* Scene = (FScene*)Family->Scene;

		if (Scene->SimpleDirectionalLight)
		{			
			FrameUniformShaderParameters.DirectionalLightColor = Scene->SimpleDirectionalLight->Proxy->GetColor() / PI;
			FrameUniformShaderParameters.DirectionalLightDirection = -Scene->SimpleDirectionalLight->Proxy->GetDirection();

			static_assert(MAX_FORWARD_SHADOWCASCADES <= 4, "more than 4 cascades not supported by the shader and uniform buffer");
			if (DirectionalLightShadowInfo)
			{
				{
					FProjectedShadowInfo& ShadowInfo = *((*DirectionalLightShadowInfo)[0]);
					FIntPoint ShadowBufferResolution = ShadowInfo.GetShadowBufferResolution();
					FVector4 ShadowBufferSizeValue((float)ShadowBufferResolution.X, (float)ShadowBufferResolution.Y, 1.0f / (float)ShadowBufferResolution.X, 1.0f / (float)ShadowBufferResolution.Y);

					FrameUniformShaderParameters.DirectionalLightShadowTexture = SceneContext.GetShadowDepthZTexture();
					FrameUniformShaderParameters.DirectionalLightShadowTransition = 1.0f / ShadowInfo.ComputeTransitionSize();
					FrameUniformShaderParameters.DirectionalLightShadowSize = ShadowBufferSizeValue;
				}

				int32 NumShadowsToCopy = FMath::Min(DirectionalLightShadowInfo->Num(), MAX_FORWARD_SHADOWCASCADES);				
				for (int32 i = 0; i < NumShadowsToCopy; ++i)
				{
					const FProjectedShadowInfo& ShadowInfo = *(*DirectionalLightShadowInfo)[i];
					FrameUniformShaderParameters.DirectionalLightScreenToShadow[i] = ShadowInfo.GetScreenToShadowMatrix(*this);
					FrameUniformShaderParameters.DirectionalLightShadowDistances[i] = ShadowInfo.CascadeSettings.SplitFar;
				}

				for (int32 i = NumShadowsToCopy; i < MAX_FORWARD_SHADOWCASCADES; ++i)
				{
					FrameUniformShaderParameters.DirectionalLightScreenToShadow[i].SetIdentity();
					FrameUniformShaderParameters.DirectionalLightShadowDistances[i] = 0.0f;
				}
			}
			else
			{
				FrameUniformShaderParameters.DirectionalLightShadowTransition = 0.0f;
				FrameUniformShaderParameters.DirectionalLightShadowSize = FVector::ZeroVector;
				for (int32 i = 0; i < MAX_FORWARD_SHADOWCASCADES; ++i)
				{
					FrameUniformShaderParameters.DirectionalLightScreenToShadow[i].SetIdentity();
					FrameUniformShaderParameters.DirectionalLightShadowDistances[i] = 0.0f;
				}			
			}			 
		}
		else
		{
			FrameUniformShaderParameters.DirectionalLightColor = FLinearColor::Black;
			FrameUniformShaderParameters.DirectionalLightDirection = FVector::ZeroVector;
		}
		
		FrameUniformShaderParameters.UpperSkyColor = Scene->UpperDynamicSkylightColor;
		FrameUniformShaderParameters.LowerSkyColor = Scene->LowerDynamicSkylightColor;

		// Atmospheric fog parameters
		if (ShouldRenderAtmosphere(*Family) && Scene->AtmosphericFog)
		{
			FrameUniformShaderParameters.AtmosphericFogSunPower = Scene->AtmosphericFog->SunMultiplier;
			FrameUniformShaderParameters.AtmosphericFogPower = Scene->AtmosphericFog->FogMultiplier;
			FrameUniformShaderParameters.AtmosphericFogDensityScale = Scene->AtmosphericFog->InvDensityMultiplier;
			FrameUniformShaderParameters.AtmosphericFogDensityOffset = Scene->AtmosphericFog->DensityOffset;
			FrameUniformShaderParameters.AtmosphericFogGroundOffset = Scene->AtmosphericFog->GroundOffset;
			FrameUniformShaderParameters.AtmosphericFogDistanceScale = Scene->AtmosphericFog->DistanceScale;
			FrameUniformShaderParameters.AtmosphericFogAltitudeScale = Scene->AtmosphericFog->AltitudeScale;
			FrameUniformShaderParameters.AtmosphericFogHeightScaleRayleigh = Scene->AtmosphericFog->RHeight;
			FrameUniformShaderParameters.AtmosphericFogStartDistance = Scene->AtmosphericFog->StartDistance;
			FrameUniformShaderParameters.AtmosphericFogDistanceOffset = Scene->AtmosphericFog->DistanceOffset;
			FrameUniformShaderParameters.AtmosphericFogSunDiscScale = Scene->AtmosphericFog->SunDiscScale;
			FrameUniformShaderParameters.AtmosphericFogSunColor = Scene->SunLight ? Scene->SunLight->Proxy->GetColor() : Scene->AtmosphericFog->DefaultSunColor;
			FrameUniformShaderParameters.AtmosphericFogSunDirection = Scene->SunLight ? -Scene->SunLight->Proxy->GetDirection() : -Scene->AtmosphericFog->DefaultSunDirection;
			FrameUniformShaderParameters.AtmosphericFogRenderMask = Scene->AtmosphericFog->RenderFlag & (EAtmosphereRenderFlag::E_DisableGroundScattering | EAtmosphereRenderFlag::E_DisableSunDisk);
			FrameUniformShaderParameters.AtmosphericFogInscatterAltitudeSampleNum = Scene->AtmosphericFog->InscatterAltitudeSampleNum;
		}
		else
		{
			FrameUniformShaderParameters.AtmosphericFogSunPower = 0.f;
			FrameUniformShaderParameters.AtmosphericFogPower = 0.f;
			FrameUniformShaderParameters.AtmosphericFogDensityScale = 0.f;
			FrameUniformShaderParameters.AtmosphericFogDensityOffset = 0.f;
			FrameUniformShaderParameters.AtmosphericFogGroundOffset = 0.f;
			FrameUniformShaderParameters.AtmosphericFogDistanceScale = 0.f;
			FrameUniformShaderParameters.AtmosphericFogAltitudeScale = 0.f;
			FrameUniformShaderParameters.AtmosphericFogHeightScaleRayleigh = 0.f;
			FrameUniformShaderParameters.AtmosphericFogStartDistance = 0.f;
			FrameUniformShaderParameters.AtmosphericFogDistanceOffset = 0.f;
			FrameUniformShaderParameters.AtmosphericFogSunDiscScale = 1.f;
			FrameUniformShaderParameters.AtmosphericFogSunColor = FLinearColor::Black;
			FrameUniformShaderParameters.AtmosphericFogSunDirection = FVector::ZeroVector;
			FrameUniformShaderParameters.AtmosphericFogRenderMask = EAtmosphereRenderFlag::E_EnableAll;
			FrameUniformShaderParameters.AtmosphericFogInscatterAltitudeSampleNum = 0;
		}
	}
	else
	{
		FrameUniformShaderParameters.DirectionalLightDirection = FVector::ZeroVector;
		FrameUniformShaderParameters.DirectionalLightColor = FLinearColor::Black;
		FrameUniformShaderParameters.UpperSkyColor = FLinearColor::Black;
		FrameUniformShaderParameters.LowerSkyColor = FLinearColor::Black;

		// Atmospheric fog parameters
		FrameUniformShaderParameters.AtmosphericFogSunPower = 0.f;
		FrameUniformShaderParameters.AtmosphericFogPower = 0.f;
		FrameUniformShaderParameters.AtmosphericFogDensityScale = 0.f;
		FrameUniformShaderParameters.AtmosphericFogDensityOffset = 0.f;
		FrameUniformShaderParameters.AtmosphericFogGroundOffset = 0.f;
		FrameUniformShaderParameters.AtmosphericFogDistanceScale = 0.f;
		FrameUniformShaderParameters.AtmosphericFogAltitudeScale = 0.f;
		FrameUniformShaderParameters.AtmosphericFogHeightScaleRayleigh =  0.f;
		FrameUniformShaderParameters.AtmosphericFogStartDistance = 0.f;
		FrameUniformShaderParameters.AtmosphericFogDistanceOffset = 0.f;
		FrameUniformShaderParameters.AtmosphericFogSunDiscScale = 1.f;
		FrameUniformShaderParameters.AtmosphericFogSunColor = FLinearColor::Black;
		FrameUniformShaderParameters.AtmosphericFogSunDirection = FVector::ZeroVector;
		FrameUniformShaderParameters.AtmosphericFogRenderMask = EAtmosphereRenderFlag::E_EnableAll;
		FrameUniformShaderParameters.AtmosphericFogInscatterAltitudeSampleNum = 0;
	}

	FrameUniformShaderParameters.UnlitViewmodeMask = bIsUnlitView ? 1 : 0;
	FrameUniformShaderParameters.OutOfBoundsMask = Family->EngineShowFlags.VisualizeOutOfBoundsPixels ? 1 : 0;

	FrameUniformShaderParameters.GameTime = Family->CurrentWorldTime;
	FrameUniformShaderParameters.RealTime = Family->CurrentRealTime;
	FrameUniformShaderParameters.Random = FMath::Rand();
	FrameUniformShaderParameters.FrameNumber = Family->FrameNumber;

	// Lets not use Lightmaps if we don't allow static lighting, shall we?
	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowStaticLighting"));
	static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DiffuseFromCaptures"));
	const bool bUseLightmaps = (AllowStaticLightingVar->GetInt() == 1) && (CVar->GetInt() == 0);

	FrameUniformShaderParameters.CameraCut = bCameraCut ? 1 : 0;
	FrameUniformShaderParameters.UseLightmaps = bUseLightmaps ? 1 : 0;

	if(State)
	{
		// safe to cast on the renderer side
		FrameUniformShaderParameters.TemporalAAParams = FVector4(
			ViewState->GetCurrentTemporalAASampleIndex(), 
			ViewState->GetCurrentTemporalAASampleCount(),
			TemporalJitterPixelsX,
			TemporalJitterPixelsY);
	}
	else
	{
		FrameUniformShaderParameters.TemporalAAParams = FVector4(0, 1, 0, 0);
	}

	{
		// If rendering in stereo, the right eye uses the left eye's translucency lighting volume.
		const FViewInfo* PrimaryView = this;
		if (StereoPass == eSSP_RIGHT_EYE)
		{
			int32 ViewIndex = Family->Views.Find(this);
			if (Family->Views.IsValidIndex(ViewIndex) && Family->Views.IsValidIndex(ViewIndex - 1))
			{
				const FSceneView* LeftEyeView = Family->Views[ViewIndex - 1];
				if (LeftEyeView->bIsViewInfo && LeftEyeView->StereoPass == eSSP_LEFT_EYE)
				{
					PrimaryView = static_cast<const FViewInfo*>(LeftEyeView);
				}
			}
		}
		PrimaryView->CalcTranslucencyLightingVolumeBounds(OutTranslucentCascadeBoundsArray, NumTranslucentCascades);
	}

	for (int32 CascadeIndex = 0; CascadeIndex < NumTranslucentCascades; CascadeIndex++)
	{
		const float VolumeVoxelSize = (OutTranslucentCascadeBoundsArray[CascadeIndex].Max.X - OutTranslucentCascadeBoundsArray[CascadeIndex].Min.X) / GTranslucencyLightingVolumeDim;
		const FVector VolumeSize = OutTranslucentCascadeBoundsArray[CascadeIndex].Max - OutTranslucentCascadeBoundsArray[CascadeIndex].Min;
		FrameUniformShaderParameters.TranslucencyLightingVolumeMin[CascadeIndex] = FVector4(OutTranslucentCascadeBoundsArray[CascadeIndex].Min, 1.0f / GTranslucencyLightingVolumeDim);
		FrameUniformShaderParameters.TranslucencyLightingVolumeInvSize[CascadeIndex] = FVector4(FVector(1.0f) / VolumeSize, VolumeVoxelSize);
	}
	
	FrameUniformShaderParameters.RenderTargetSize = BufferSize;
	// The exposure scale is just a scalar but needs to be a float4 to workaround a driver bug on IOS.
	// After 4.2 we can put the workaround in the cross compiler.
	float ExposureScale = FRCPassPostProcessEyeAdaptation::ComputeExposureScaleValue( *this );
	FrameUniformShaderParameters.ExposureScale = FVector4(ExposureScale, ExposureScale, ExposureScale, 1.0f);
	FrameUniformShaderParameters.DepthOfFieldFocalDistance = FinalPostProcessSettings.DepthOfFieldFocalDistance;
	FrameUniformShaderParameters.DepthOfFieldFocalRegion = FinalPostProcessSettings.DepthOfFieldFocalRegion;
	// clamped to avoid div by 0 in shader
	FrameUniformShaderParameters.DepthOfFieldNearTransitionRegion = FMath::Max(0.01f, FinalPostProcessSettings.DepthOfFieldNearTransitionRegion);
	// clamped to avoid div by 0 in shader
	FrameUniformShaderParameters.DepthOfFieldFarTransitionRegion = FMath::Max(0.01f, FinalPostProcessSettings.DepthOfFieldFarTransitionRegion);
	FrameUniformShaderParameters.DepthOfFieldScale = FinalPostProcessSettings.DepthOfFieldScale;
	FrameUniformShaderParameters.DepthOfFieldFocalLength = 50.0f;
	FrameUniformShaderParameters.MotionBlurNormalizedToPixel = FinalPostProcessSettings.MotionBlurMax * ViewRect.Width() / 100.0f;

	{
		// This is the CVar default
		float Value = 1.0f;

		// Compiled out in SHIPPING to make cheating a bit harder.
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		Value = CVarGeneralPurposeTweak.GetValueOnRenderThread();
#endif

		FrameUniformShaderParameters.GeneralPurposeTweak = Value;
	}

	FrameUniformShaderParameters.DemosaicVposOffset = 0.0f;
	{
		FrameUniformShaderParameters.DemosaicVposOffset = CVarDemosaicVposOffset.GetValueOnRenderThread();
	}

	FrameUniformShaderParameters.IndirectLightingColorScale = FVector(FinalPostProcessSettings.IndirectLightingColor.R * FinalPostProcessSettings.IndirectLightingIntensity,
		FinalPostProcessSettings.IndirectLightingColor.G * FinalPostProcessSettings.IndirectLightingIntensity,
		FinalPostProcessSettings.IndirectLightingColor.B * FinalPostProcessSettings.IndirectLightingIntensity);

	FrameUniformShaderParameters.AmbientCubemapTint = FinalPostProcessSettings.AmbientCubemapTint;
	FrameUniformShaderParameters.AmbientCubemapIntensity = FinalPostProcessSettings.AmbientCubemapIntensity;

	{
		// Enables HDR encoding mode selection without recompile of all PC shaders during ES2 emulation.
		FrameUniformShaderParameters.HDR32bppEncodingMode = 0;
		if (IsMobileHDR32bpp())
		{
			FrameUniformShaderParameters.HDR32bppEncodingMode = IsMobileHDRMosaic() ? 1.0f : 2.0f;
		}
	}
	
	FVector2D OneScenePixelUVSize = FVector2D(1.0f / BufferSize.X, 1.0f / BufferSize.Y);
	FVector4 SceneTexMinMax(	((float)ViewRect.Min.X / BufferSize.X), 
								((float)ViewRect.Min.Y / BufferSize.Y), 
								(((float)ViewRect.Max.X / BufferSize.X) - OneScenePixelUVSize.X) , 
								(((float)ViewRect.Max.Y / BufferSize.Y) - OneScenePixelUVSize.Y) );
	FrameUniformShaderParameters.SceneTextureMinMax = SceneTexMinMax;
	FrameUniformShaderParameters.CircleDOFParams = CircleDofHalfCoc(*this);

	FScene* Scene = (FScene*)Family->Scene;
	ERHIFeatureLevel::Type FeatureLevel = Scene == nullptr ? GMaxRHIFeatureLevel : Scene->GetFeatureLevel();

	if (Scene && Scene->SkyLight)
	{
		FSkyLightSceneProxy* SkyLight = Scene->SkyLight;

		FrameUniformShaderParameters.SkyLightColor = SkyLight->LightColor;

		bool bApplyPrecomputedBentNormalShadowing = 
			SkyLight->bCastShadows 
			&& SkyLight->bWantsStaticShadowing
			&& SkyLight->bPrecomputedLightingIsValid;

		FrameUniformShaderParameters.SkyLightParameters = bApplyPrecomputedBentNormalShadowing ? 1 : 0;
	}
	else
	{
		FrameUniformShaderParameters.SkyLightColor = FLinearColor::Black;
		FrameUniformShaderParameters.SkyLightParameters = 0;
	}

	// Make sure there's no padding since we're going to cast to FVector4*
	checkSlow(sizeof(FrameUniformShaderParameters.SkyIrradianceEnvironmentMap) == sizeof(FVector4)* 7);
	SetupSkyIrradianceEnvironmentMapConstants((FVector4*)&FrameUniformShaderParameters.SkyIrradianceEnvironmentMap);

	FrameUniformShaderParameters.MobilePreviewMode =
		(GIsEditor &&
		(FeatureLevel == ERHIFeatureLevel::ES2 || FeatureLevel == ERHIFeatureLevel::ES3_1) &&
		GMaxRHIFeatureLevel > ERHIFeatureLevel::ES3_1) ? 1.0f : 0.0f;

	// Padding between the left and right eye may be introduced by an HMD, which instanced stereo needs to account for.
	if (Family != nullptr && StereoPass == eSSP_LEFT_EYE)
	{
		check(Family->Views.Num() == 2);
		const float FamilySizeX = static_cast<float>(Family->FamilySizeX);
		const float EyePaddingSize = static_cast<float>(Family->Views[1]->ViewRect.Min.X - ViewRect.Max.X);
		FrameUniformShaderParameters.HMDEyePaddingOffset = (FamilySizeX - EyePaddingSize) / FamilySizeX;
	}
	else
	{
		FrameUniformShaderParameters.HMDEyePaddingOffset = 1.0f;
	}

	OutViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(ViewUniformShaderParameters, UniformBuffer_SingleFrame);
	OutFrameUniformBuffer = TUniformBufferRef<FFrameUniformShaderParameters>::CreateUniformBufferImmediate(FrameUniformShaderParameters, UniformBuffer_SingleFrame);
}

void FViewInfo::CreateForwardLightDataUniformBuffer(FForwardLightData &OutForwardLightData) const
{
	uint32 LightIndex = 0;
	// todo: we should do this only once before rendering, not late after GBuffer pass

	FScene* Scene = (FScene*)(Family->Scene);

	// Reflection override skips direct specular because it tends to be blindingly bright with a perfectly smooth surface
	if(Scene && !Family->EngineShowFlags.ReflectionOverride)
	{
		// we test after adding each light so we need at least space for one element
		check(GMaxNumForwardLights > 0);

		// Build a list of visible lights.
		for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
		{
			const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
			const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

			if(!LightSceneInfoCompact.Color.IsAlmostBlack()
				// Only render lights with dynamic lighting or unbuilt static lights
				&& (!LightSceneInfo->Proxy->HasStaticLighting() || !LightSceneInfo->IsPrecomputedLightingValid()))
			{
				const ELightComponentType LightType = (const ELightComponentType)LightSceneInfoCompact.LightType;

				FVector NormalizedLightDirection;
				FVector2D SpotAngles;
				float SourceRadius;
				float SourceLength;
				float MinRoughness;

				// Get the light parameters
				LightSceneInfo->Proxy->GetParameters(
					OutForwardLightData.LightPositionAndInvRadius[LightIndex],
					OutForwardLightData.LightColorAndFalloffExponent[LightIndex],
					NormalizedLightDirection,
					SpotAngles,
					SourceRadius,
					SourceLength,
					MinRoughness);

				// Check if the light is visible in this view.
				if(!LightSceneInfo->ShouldRenderLight(*this))
				{
					continue;
				}

				FVector4 BoundingSphereVector = *(FVector4*)&LightSceneInfoCompact.BoundingSphereVector;

				float InvRadius = 1.0f / BoundingSphereVector.W;

				OutForwardLightData.LightPositionAndInvRadius[LightIndex] = FVector4(FVector(BoundingSphereVector), InvRadius);

				// SpotlightMaskAndMinRoughness, >0:Spotlight, MinRoughness = abs();
				{
					float W = FMath::Max(0.0001f, MinRoughness) * ((LightType == LightType_Spot) ? 1 : -1);

					OutForwardLightData.LightDirectionAndSpotlightMaskAndMinRoughness[LightIndex] = FVector4(NormalizedLightDirection, W);
					OutForwardLightData.SpotAnglesAndSourceRadiusAndDir[LightIndex] = FVector4(SpotAngles.X, SpotAngles.Y, SourceRadius, LightType == LightType_Directional);
				}

				if(LightSceneInfo->Proxy->IsInverseSquared())
				{
					// Correction for lumen units
					OutForwardLightData.LightColorAndFalloffExponent[LightIndex].X *= 16.0f;
					OutForwardLightData.LightColorAndFalloffExponent[LightIndex].Y *= 16.0f;
					OutForwardLightData.LightColorAndFalloffExponent[LightIndex].Z *= 16.0f;
					OutForwardLightData.LightColorAndFalloffExponent[LightIndex].W = 0;
				}

				{
					// SpotlightMaskAndMinRoughness, >0:Spotlight, MinRoughness = abs();
					float W = FMath::Max(0.0001f, MinRoughness) * ((LightSceneInfo->Proxy->GetLightType() == LightType_Spot) ? 1 : -1);

					OutForwardLightData.LightDirectionAndSpotlightMaskAndMinRoughness[LightIndex] = FVector4(NormalizedLightDirection, W);
				}

				// we want to add one light
				++LightIndex;

				if(LightIndex >= GMaxNumForwardLights)
				{
					// we cannot handle more lights this way
					break;
				}
			}
		}
	}

	OutForwardLightData.LightCount = LightIndex;
}

void FViewInfo::CreateLightGrid()
{
	// This constant affects performance, power of two makes sense, too small costs more CPU and upload data, too large culls less efficiently.
	const int32 TileSize = 16;

	uint32 TileCountX = FMath::DivideAndRoundUp(ViewRect.Width(), TileSize);
	uint32 TileCountY = FMath::DivideAndRoundUp(ViewRect.Height(), TileSize);

	//@todo - creating a new uniform buffer is expensive, only do this when the vertex factory needs an accurate view matrix (particle sprites)
	FForwardLightData LocalForwardLightData;

	CreateForwardLightDataUniformBuffer(LocalForwardLightData);

	LocalForwardLightData.TileSize = TileSize;
	LocalForwardLightData.TileCountX = TileCountX;
	LocalForwardLightData.InvTileSize = 1.0f / TileSize;

	ForwardLightData = TUniformBufferRef<FForwardLightData>::CreateUniformBufferImmediate(LocalForwardLightData, UniformBuffer_SingleFrame);
	// todo: temp data should be released when possible (after lighting)

	{
		GLightGridVertexBuffer.CPUData.SetNumUninitialized(TileCountX * TileCountY);

		FMemory::Memset(&GLightGridVertexBuffer.CPUData[0], 0, TileCountX * TileCountY * sizeof(uint32));

		for(uint32 i = 0, Count = LocalForwardLightData.LightCount; i < Count; ++i)
		{
			if(Count > 31)
			{
				// todo: currently we don't support more
				break;
			}

			const FVector4 PosAndRadius = LocalForwardLightData.LightPositionAndInvRadius[i];

			FIntRect LocalPixelRect = ViewRect;
			if(FMath::ComputeProjectedSphereScissorRect(LocalPixelRect, FVector(PosAndRadius), 1.0f / PosAndRadius.W, ViewMatrices.ViewOrigin, ViewMatrices.ViewMatrix, ViewMatrices.ProjMatrix) > 0)
			{
				FIntRect LocalTileRect = FIntRect::DivideAndRoundUp(LocalPixelRect - ViewRect.Min, TileSize);

				for(int32 y = LocalTileRect.Min.Y; y < LocalTileRect.Max.Y; ++y)
				{
					for(int32 x = LocalTileRect.Min.X; x < LocalTileRect.Max.X; ++x)
					{
						uint32& Tile = GLightGridVertexBuffer.CPUData[x + y * TileCountX];

						Tile |= 1 << i;
					}
				}
			}
		}

		GLightGridVertexBuffer.UpdateGPUFromCPUData();
	}
}

void FViewInfo::InitRHIResources(const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>* DirectionalLightShadowInfo)
{
	FBox VolumeBounds[TVC_MAX];

	/** The view transform, starting from world-space points translated by -ViewOrigin. */
	FMatrix TranslatedViewMatrix = FTranslationMatrix(-ViewMatrices.PreViewTranslation) * ViewMatrices.ViewMatrix;

	check(IsInRenderingThread());
	CreateUniformBuffer(
		ViewUniformBuffer, 
		FrameUniformBuffer, 
		FRHICommandListExecutor::GetImmediateCommandList(),
		DirectionalLightShadowInfo,
		TranslatedViewMatrix,
		InvViewMatrix * FTranslationMatrix(ViewMatrices.PreViewTranslation),
		VolumeBounds,
		TVC_MAX);

	for (int32 CascadeIndex = 0; CascadeIndex < TVC_MAX; CascadeIndex++)
	{
		TranslucencyLightingVolumeMin[CascadeIndex] = VolumeBounds[CascadeIndex].Min;
		TranslucencyVolumeVoxelSize[CascadeIndex] = (VolumeBounds[CascadeIndex].Max.X - VolumeBounds[CascadeIndex].Min.X) / GTranslucencyLightingVolumeDim;
		TranslucencyLightingVolumeSize[CascadeIndex] = VolumeBounds[CascadeIndex].Max - VolumeBounds[CascadeIndex].Min;
	}

	// Initialize the dynamic resources used by the view's FViewElementDrawer.
	for(int32 ResourceIndex = 0;ResourceIndex < DynamicResources.Num();ResourceIndex++)
	{
		DynamicResources[ResourceIndex]->InitPrimitiveResource();
	}

	if(CVarForwardLighting.GetValueOnRenderThread())
	{
		CreateLightGrid();
	}
	else
	{
		FForwardLightData LocalForwardLightData;
		FMemory::Memzero(LocalForwardLightData);
		ForwardLightData = TUniformBufferRef<FForwardLightData>::CreateUniformBufferImmediate(LocalForwardLightData, UniformBuffer_SingleFrame);
	}
}

// These are not real view infos, just dumb memory blocks
static TArray<FViewInfo*> ViewInfoSnapshots;
// these are never freed, even at program shutdown
static TArray<FViewInfo*> FreeViewInfoSnapshots;

FViewInfo* FViewInfo::CreateSnapshot() const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FViewInfo_CreateSnapshot);

	check(IsInRenderingThread()); // we do not want this popped before the end of the scene and it better be the scene allocator
	FViewInfo* Result;
	if (FreeViewInfoSnapshots.Num())
	{
		Result = FreeViewInfoSnapshots.Pop(false);
	}
	else
	{
		Result = (FViewInfo*)FMemory::Malloc(sizeof(FViewInfo), ALIGNOF(FViewInfo));
	}
	FMemory::Memcpy(*Result, *this);

	// we want these to start null without a reference count, since we clear a ref later
	TUniformBufferRef<FViewUniformShaderParameters> NullViewUniformBuffer;
	TUniformBufferRef<FFrameUniformShaderParameters> NullFrameUniformBuffer;
	FMemory::Memcpy(Result->ViewUniformBuffer, NullViewUniformBuffer); 
	FMemory::Memcpy(Result->FrameUniformBuffer, NullFrameUniformBuffer);
	Result->bIsSnapshot = true;
	ViewInfoSnapshots.Add(Result);
	return Result;
}

void FViewInfo::DestroyAllSnapshots()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FViewInfo_DestroyAllSnapshots);

	check(IsInRenderingThread());
	// we will only keep double the number actually used, plus a few
	int32 NumToRemove = FreeViewInfoSnapshots.Num() - (ViewInfoSnapshots.Num() + 2);
	if (NumToRemove > 0)
	{
		for (int32 Index = 0; Index < NumToRemove; Index++)
		{
			FMemory::Free(FreeViewInfoSnapshots[Index]);
		}
		FreeViewInfoSnapshots.RemoveAt(0, NumToRemove, false);
	}
	for (FViewInfo* Snapshot : ViewInfoSnapshots)
	{
		Snapshot->ViewUniformBuffer.SafeRelease();
		Snapshot->FrameUniformBuffer.SafeRelease();
		FreeViewInfoSnapshots.Add(Snapshot);
	}
	ViewInfoSnapshots.Reset();
}

FSceneViewState* FViewInfo::GetEffectiveViewState() const
{
	FSceneViewState* EffectiveViewState = ViewState;

	// When rendering in stereo we want to use the same exposure for both eyes.
	if (StereoPass == eSSP_RIGHT_EYE)
	{
		int32 ViewIndex = Family->Views.Find(this);
		if (Family->Views.IsValidIndex(ViewIndex))
		{
			// The left eye is always added before the right eye.
			ViewIndex = ViewIndex - 1;
			if (Family->Views.IsValidIndex(ViewIndex))
			{
				const FSceneView* PrimaryView = Family->Views[ViewIndex];
				if (PrimaryView->StereoPass == eSSP_LEFT_EYE)
				{
					EffectiveViewState = (FSceneViewState*)PrimaryView->State;
				}
			}
		}
	}

	return EffectiveViewState;
}

IPooledRenderTarget* FViewInfo::GetEyeAdaptation(FRHICommandList& RHICmdList) const
{
	return GetEyeAdaptationRT(RHICmdList);
}

IPooledRenderTarget* FViewInfo::GetEyeAdaptationRT(FRHICommandList& RHICmdList) const
{
	FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	IPooledRenderTarget* result = NULL;
	if (EffectiveViewState)
	{
		result = EffectiveViewState->GetCurrentEyeAdaptationRT(RHICmdList);
	}
	return result;
}

IPooledRenderTarget* FViewInfo::GetLastEyeAdaptationRT(FRHICommandList& RHICmdList) const
{
	FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	IPooledRenderTarget* result = NULL;
	if (EffectiveViewState)
	{
		result = EffectiveViewState->GetLastEyeAdaptationRT(RHICmdList);
	}
	return result;
}

void FViewInfo::SwapEyeAdaptationRTs()
{
	FSceneViewState* EffectiveViewState = GetEffectiveViewState();
	if (EffectiveViewState)
	{
		EffectiveViewState->SwapEyeAdaptationRTs();
	}
}

bool FViewInfo::HasValidEyeAdaptation() const
{
	FSceneViewState* EffectiveViewState = GetEffectiveViewState();	

	if (EffectiveViewState)
	{
		return EffectiveViewState->HasValidEyeAdaptation();
	}
	return false;
}

void FViewInfo::SetValidEyeAdaptation()
{
	FSceneViewState* EffectiveViewState = GetEffectiveViewState();	

	if (EffectiveViewState)
	{
		EffectiveViewState->SetValidEyeAdaptation();
	}
}

/*-----------------------------------------------------------------------------
	FSceneRenderer
-----------------------------------------------------------------------------*/

FSceneRenderer::FSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer)
:	Scene(InViewFamily->Scene ? InViewFamily->Scene->GetRenderScene() : NULL)
,	ViewFamily(*InViewFamily)
,	bUsedPrecomputedVisibility(false)
{
	check(Scene != NULL);

	check(IsInGameThread());
	ViewFamily.FrameNumber = GFrameNumber;

	// Copy the individual views.
	bool bAnyViewIsLocked = false;
	Views.Empty(InViewFamily->Views.Num());
	for(int32 ViewIndex = 0;ViewIndex < InViewFamily->Views.Num();ViewIndex++)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		for(int32 ViewIndex2 = 0;ViewIndex2 < InViewFamily->Views.Num();ViewIndex2++)
		{
			if (ViewIndex != ViewIndex2 && InViewFamily->Views[ViewIndex]->State != NULL)
			{
				// Verify that each view has a unique view state, as the occlusion query mechanism depends on it.
				check(InViewFamily->Views[ViewIndex]->State != InViewFamily->Views[ViewIndex2]->State);
			}
		}
#endif
		// Construct a FViewInfo with the FSceneView properties.
		FViewInfo* ViewInfo = new(Views) FViewInfo(InViewFamily->Views[ViewIndex]);
		ViewFamily.Views[ViewIndex] = ViewInfo;
		ViewInfo->Family = &ViewFamily;
		bAnyViewIsLocked |= ViewInfo->bIsLocked;

#if WITH_EDITOR
		// Should we allow the user to select translucent primitives?
		ViewInfo->bAllowTranslucentPrimitivesInHitProxy =
			GEngine->AllowSelectTranslucent() ||		// User preference enabled?
			!ViewInfo->IsPerspectiveProjection();		// Is orthographic view?
#endif

		// Batch the view's elements for later rendering.
		if(ViewInfo->Drawer)
		{
			FViewElementPDI ViewElementPDI(ViewInfo,HitProxyConsumer);
			ViewInfo->Drawer->Draw(ViewInfo,&ViewElementPDI);
		}
	}

	// If any viewpoint has been locked, set time to zero to avoid time-based
	// rendering differences in materials.
	if (bAnyViewIsLocked)
	{
		ViewFamily.CurrentRealTime = 0.0f;
		ViewFamily.CurrentWorldTime = 0.0f;
	}
	
	if(HitProxyConsumer)
	{
		// Set the hit proxies show flag.
		ViewFamily.EngineShowFlags.SetHitProxies(1);
	}

	// launch custom visibility queries for views
	if (GCustomCullingImpl)
	{
		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{
			FViewInfo& ViewInfo = Views[ViewIndex];
			ViewInfo.CustomVisibilityQuery = GCustomCullingImpl->CreateQuery(ViewInfo);
		}
	}

	ViewFamily.ComputeFamilySize();

	// copy off the requests
	// (I apologize for the const_cast, but didn't seem worth refactoring just for the freezerendering command)
	bHasRequestedToggleFreeze = const_cast<FRenderTarget*>(InViewFamily->RenderTarget)->HasToggleFreezeCommand();

	FeatureLevel = Scene->GetFeatureLevel();
}

bool FSceneRenderer::DoOcclusionQueries(ERHIFeatureLevel::Type InFeatureLevel) const
{
	return !IsMobilePlatform(GShaderPlatformForFeatureLevel[InFeatureLevel])
		&& CVarAllowOcclusionQueries.GetValueOnRenderThread() != 0;
}

FSceneRenderer::~FSceneRenderer()
{
	if(Scene)
	{
		// Destruct the projected shadow infos.
		for(TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights);LightIt;++LightIt)
		{
			if( VisibleLightInfos.IsValidIndex(LightIt.GetIndex()) )
			{
				FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightIt.GetIndex()];
				for(int32 ShadowIndex = 0;ShadowIndex < VisibleLightInfo.MemStackProjectedShadows.Num();ShadowIndex++)
				{
					// FProjectedShadowInfo's in MemStackProjectedShadows were allocated on the rendering thread mem stack, 
					// Their memory will be freed when the stack is freed with no destructor call, so invoke the destructor explicitly
					VisibleLightInfo.MemStackProjectedShadows[ShadowIndex]->~FProjectedShadowInfo();
				}
			}
		}
	}
}

/** 
* Finishes the view family rendering.
*/
void FSceneRenderer::RenderFinish(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_DRAW_EVENT(RHICmdList, RenderFinish);

	if(FRCPassPostProcessBusyWait::IsPassRequired())
	{
		// mostly view independent but to be safe we use the first view
		FViewInfo& View = Views[0];

		FMemMark Mark(FMemStack::Get());
		FRenderingCompositePassContext CompositeContext(RHICmdList, View);

		FRenderingCompositeOutputRef BusyWait;
		{
			// for debugging purpose, can be controlled by console variable
			FRenderingCompositePass* Node = CompositeContext.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessBusyWait());
			BusyWait = FRenderingCompositeOutputRef(Node);
		}		
		
		if(BusyWait.IsValid())
		{
			CompositeContext.Process(BusyWait.GetPass(), TEXT("RenderFinish"));
		}
	}
	
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		bool bShowPrecomputedVisibilityWarning = false;
		static const auto* CVarPrecomputedVisibilityWarning = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.PrecomputedVisibilityWarning"));
		if (CVarPrecomputedVisibilityWarning && CVarPrecomputedVisibilityWarning->GetValueOnRenderThread() == 1)
		{
			bShowPrecomputedVisibilityWarning = !bUsedPrecomputedVisibility;
		}

		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{	
			FViewInfo& View = Views[ViewIndex];
			if (!View.bIsReflectionCapture && !View.bIsSceneCapture )
			{
				SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);
				// display a message saying we're frozen
				FSceneViewState* ViewState = (FSceneViewState*)View.State;
				bool bViewParentOrFrozen = ViewState && (ViewState->HasViewParent() || ViewState->bIsFrozen);
				bool bLocked = View.bIsLocked;
				if (bViewParentOrFrozen || bShowPrecomputedVisibilityWarning || bLocked)
				{
					// this is a helper class for FCanvas to be able to get screen size
					class FRenderTargetTemp : public FRenderTarget
					{
					public:
						FViewInfo& View;

						FRenderTargetTemp(FViewInfo& InView) : View(InView)
						{
						}
						virtual FIntPoint GetSizeXY() const
						{
							return View.ViewRect.Size();
						};
						virtual const FTexture2DRHIRef& GetRenderTargetTexture() const
						{
							return View.Family->RenderTarget->GetRenderTargetTexture();
						}
					} TempRenderTarget(View);

					// create a temporary FCanvas object with the temp render target
					// so it can get the screen size
					int32 Y = 130;
					FCanvas Canvas(&TempRenderTarget, NULL, View.Family->CurrentRealTime, View.Family->CurrentWorldTime, View.Family->DeltaWorldTime, FeatureLevel);
					if (bViewParentOrFrozen)
					{
						const FText StateText =
							ViewState->bIsFrozen ?
							NSLOCTEXT("SceneRendering", "RenderingFrozen", "Rendering frozen...")
							:
							NSLOCTEXT("SceneRendering", "OcclusionChild", "Occlusion Child");
						Canvas.DrawShadowedText(10, Y, StateText, GetStatsFont(), FLinearColor(0.8, 1.0, 0.2, 1.0));
						Y += 14;
					}
					if (bShowPrecomputedVisibilityWarning)
					{
						const FText Message = NSLOCTEXT("Renderer", "NoPrecomputedVisibility", "NO PRECOMPUTED VISIBILITY");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(1.0, 0.05, 0.05, 1.0));
						Y += 14;
					}
					if (bLocked)
					{
						const FText Message = NSLOCTEXT("Renderer", "ViewLocked", "VIEW LOCKED");
						Canvas.DrawShadowedText(10, Y, Message, GetStatsFont(), FLinearColor(0.8, 1.0, 0.2, 1.0));
						Y += 14;
					}
					Canvas.Flush_RenderThread(RHICmdList);
				}
			}
		}
	}
	
#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	// Save the post-occlusion visibility stats for the frame and freezing info
	for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		INC_DWORD_STAT_BY(STAT_VisibleStaticMeshElements,View.NumVisibleStaticMeshElements);
		INC_DWORD_STAT_BY(STAT_VisibleDynamicPrimitives,View.VisibleDynamicPrimitives.Num());

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		// update freezing info
		FSceneViewState* ViewState = (FSceneViewState*)View.State;
		if (ViewState)
		{
			// if we're finished freezing, now we are frozen
			if (ViewState->bIsFreezing)
			{
				ViewState->bIsFreezing = false;
				ViewState->bIsFrozen = true;
			}

			// handle freeze toggle request
			if (bHasRequestedToggleFreeze)
			{
				// do we want to start freezing?
				if (!ViewState->bIsFrozen)
				{
					ViewState->bIsFrozen = false;
					ViewState->bIsFreezing = true;
					ViewState->FrozenPrimitives.Empty();
				}
				// or stop?
				else
				{
					ViewState->bIsFrozen = false;
					ViewState->bIsFreezing = false;
					ViewState->FrozenPrimitives.Empty();
				}
			}
		}
#endif
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// clear the commands
	bHasRequestedToggleFreeze = false;

	if(ViewFamily.EngineShowFlags.OnScreenDebug)
	{
		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];

			if(!View.IsPerspectiveProjection())
			{
				continue;
			}

			GRenderTargetPool.PresentContent(RHICmdList, View);
		}
	}

#endif

	// To prevent keeping persistent references to single frame buffers, clear any such reference at this point.
	ClearPrimitiveSingleFramePrecomputedLightingBuffers();

	// Notify the RHI we are done rendering a scene.
	RHICmdList.EndScene();
}

FSceneRenderer* FSceneRenderer::CreateSceneRenderer(const FSceneViewFamily* InViewFamily, FHitProxyConsumer* HitProxyConsumer)
{
	bool bUseDeferred = InViewFamily->Scene->ShouldUseDeferredRenderer();
	if (bUseDeferred)
	{
		return new FDeferredShadingSceneRenderer(InViewFamily, HitProxyConsumer);
	}
	else
	{
		return new FForwardShadingSceneRenderer(InViewFamily, HitProxyConsumer);
	}
}

void FSceneRenderer::RenderCustomDepthPass(FRHICommandListImmediate& RHICmdList)
{
	if(FeatureLevel < ERHIFeatureLevel::SM4)
	{
		// not yet supported on lower end platforms
		return;
	}

	// do we have primitives in this pass?
	bool bPrimitives = false;

	if(!Scene->World || (Scene->World->WorldType != EWorldType::Preview && Scene->World->WorldType != EWorldType::Inactive))
	{
		for(int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FViewInfo& View = Views[ViewIndex];
			if(View.CustomDepthSet.NumPrims())
			{
				bPrimitives = true;
				break;
			}
		}
	}

	// Render CustomDepth
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	if (SceneContext.BeginRenderingCustomDepth(RHICmdList, bPrimitives))
	{
		SCOPED_DRAW_EVENT(RHICmdList, CustomDepth);

		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{
			SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);

			FViewInfo& View = Views[ViewIndex];

			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
			
			// seems this is set each draw call anyway
			RHICmdList.SetRasterizerState(TStaticRasterizerState<>::GetRHI());
			RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());
			
			const bool bWriteCustomStencilValues = SceneContext.IsCustomDepthPassWritingStencil();

			if (!bWriteCustomStencilValues)
			{
				RHICmdList.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
			}

			View.CustomDepthSet.DrawPrims(RHICmdList, View, bWriteCustomStencilValues);
		}

		// resolve using the current ResolveParams 
		SceneContext.FinishRenderingCustomDepth(RHICmdList);
	}
}

void FSceneRenderer::OnStartFrame()
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get_Todo_PassContext();

	GRenderTargetPool.VisualizeTexture.OnStartFrame(Views[0]);
	CompositionGraph_OnStartFrame();
	SceneContext.bScreenSpaceAOIsValid = false;
	SceneContext.bCustomDepthIsValid = false;

	for(int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{	
		FSceneView& View = Views[ViewIndex];
		FSceneViewStateInterface* State = View.State;

		if(State)
		{
			State->OnStartFrame(View, ViewFamily);
		}
	}
}

bool FSceneRenderer::ShouldCompositeEditorPrimitives(const FViewInfo& View)
{
	// If the show flag is enabled
	if (!View.Family->EngineShowFlags.CompositeEditorPrimitives)
	{
		return false;
	}

	if (View.Family->EngineShowFlags.VisualizeHDR ||
		View.Family->EngineShowFlags.ShaderComplexity)
	{
		// certain visualize modes get obstructed too much
		return false;
	}

	if (GIsEditor && View.Family->EngineShowFlags.Wireframe)
	{
		// In Editor we want wire frame view modes to be in MSAA
		return true;
	}

	// Any elements that needed compositing were drawn then compositing should be done
	if (View.ViewMeshElements.Num() || View.TopViewMeshElements.Num() || View.BatchedViewElements.HasPrimsToDraw() || View.TopBatchedViewElements.HasPrimsToDraw() || View.VisibleEditorPrimitives.Num()
		// @third party code - BEGIN HairWorks
		|| View.VisibleHairs.Num()
		// @third party code - END HairWorks
		)
	{
		return true;
	}

	return false;
}

void FSceneRenderer::WaitForTasksClearSnapshotsAndDeleteSceneRenderer(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer)
{
	// we are about to destroy things that are being used for async tasks, so we wait here for them.
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_DeleteSceneRenderer_WaitForTasks);
		RHICmdList.ImmediateFlush(EImmediateFlushType::WaitForOutstandingTasksOnly);
	}
	FViewInfo::DestroyAllSnapshots(); // this destroys viewinfo snapshots
	FSceneRenderTargets::Get(RHICmdList).DestroyAllSnapshots(); // this will destroy the render target snapshots
	static const IConsoleVariable* AsyncDispatch	= IConsoleManager::Get().FindConsoleVariable(TEXT("r.RHICmdAsyncRHIThreadDispatch"));

	if (AsyncDispatch->GetInt() == 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_DeleteSceneRenderer_Dispatch);
		RHICmdList.ImmediateFlush(EImmediateFlushType::WaitForDispatchToRHIThread); // we want to make sure this all gets to the rhi thread this frame and doesn't hang around
	}
	// Delete the scene renderer.
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_DeleteSceneRenderer);
		delete SceneRenderer;
	}
}


void FSceneRenderer::UpdatePrimitivePrecomputedLightingBuffers()
{
	// Use a bit array to prevent primitives from being updated more than once.
	FSceneBitArray UpdatedPrimitiveMap;
	UpdatedPrimitiveMap.Init(false, Scene->Primitives.Num());

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{		
		FViewInfo& View = Views[ViewIndex];

		for (int32 Index = 0; Index < View.DirtyPrecomputedLightingBufferPrimitives.Num(); ++Index)
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = View.DirtyPrecomputedLightingBufferPrimitives[Index];

			FBitReference bInserted = UpdatedPrimitiveMap[PrimitiveSceneInfo->GetIndex()];
			if (!bInserted)
			{
				PrimitiveSceneInfo->UpdatePrecomputedLightingBuffer();
				bInserted = true;
			}
			else
			{
				// This will prevent clearing it twice.
				View.DirtyPrecomputedLightingBufferPrimitives[Index] = nullptr;
			}
		}
	}
}

void FSceneRenderer::ClearPrimitiveSingleFramePrecomputedLightingBuffers()
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{		
		FViewInfo& View = Views[ViewIndex];

		for (int32 Index = 0; Index < View.DirtyPrecomputedLightingBufferPrimitives.Num(); ++Index)
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = View.DirtyPrecomputedLightingBufferPrimitives[Index];
	
			if (PrimitiveSceneInfo) // Could be null if it was a duplicate.
			{
				PrimitiveSceneInfo->ClearPrecomputedLightingBuffer(true);
			}
		}
	}
}

/*-----------------------------------------------------------------------------
	FRendererModule::BeginRenderingViewFamily
-----------------------------------------------------------------------------*/

/**
 * Helper function performing actual work in render thread.
 *
 * @param SceneRenderer	Scene renderer to use for rendering.
 */
static void RenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer)
{
	FMemMark MemStackMark(FMemStack::Get());

	// update any resources that needed a deferred update
	FDeferredUpdateResource::UpdateResources(RHICmdList);

	for( int ViewExt = 0; ViewExt < SceneRenderer->ViewFamily.ViewExtensions.Num(); ViewExt++ )
	{
		SceneRenderer->ViewFamily.ViewExtensions[ViewExt]->PreRenderViewFamily_RenderThread(RHICmdList, SceneRenderer->ViewFamily);
		for( int ViewIndex = 0; ViewIndex < SceneRenderer->ViewFamily.Views.Num(); ViewIndex++ )
		{
			SceneRenderer->ViewFamily.ViewExtensions[ViewExt]->PreRenderView_RenderThread(RHICmdList, SceneRenderer->Views[ViewIndex]);
		}
	}

	if(SceneRenderer->ViewFamily.EngineShowFlags.OnScreenDebug)
	{
		GRenderTargetPool.SetEventRecordingActive(true);
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_TotalSceneRenderingTime);
		
		if(SceneRenderer->ViewFamily.EngineShowFlags.HitProxies)
		{
			// Render the scene's hit proxies.
			SceneRenderer->RenderHitProxies(RHICmdList);
		}
		else
		{
			// Render the scene.
			SceneRenderer->Render(RHICmdList);
		}

#if STATS
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderViewFamily_RenderThread_MemStats);

			// Update scene memory stats that couldn't be tracked continuously
			SET_MEMORY_STAT(STAT_StaticDrawListMemory, FStaticMeshDrawListBase::TotalBytesUsed);
			SET_MEMORY_STAT(STAT_RenderingSceneMemory, SceneRenderer->Scene->GetSizeBytes());

			SIZE_T ViewStateMemory = 0;
			for (int32 ViewIndex = 0; ViewIndex < SceneRenderer->Views.Num(); ViewIndex++)
			{
				if (SceneRenderer->Views[ViewIndex].State)
				{
					ViewStateMemory += SceneRenderer->Views[ViewIndex].State->GetSizeBytes();
				}
			}
			SET_MEMORY_STAT(STAT_ViewStateMemory, ViewStateMemory);
			SET_MEMORY_STAT(STAT_RenderingMemStackMemory, FMemStack::Get().GetByteCount());
			SET_MEMORY_STAT(STAT_LightInteractionMemory, FLightPrimitiveInteraction::GetMemoryPoolSize());
		}
#endif

		GRenderTargetPool.SetEventRecordingActive(false);

		FSceneRenderer::WaitForTasksClearSnapshotsAndDeleteSceneRenderer(RHICmdList, SceneRenderer);

	}

#if STATS
	QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderViewFamily_RenderThread_RHIGetGPUFrameCycles);
	if (FPlatformProperties::SupportsWindowedMode() == false)
	{
		/** Update STATS with the total GPU time taken to render the last frame. */
		SET_CYCLE_COUNTER(STAT_TotalGPUFrameTime, RHIGetGPUFrameCycles());
	}
#endif
}

void FRendererModule::CreateAndInitSingleView(FRHICommandListImmediate& RHICmdList, class FSceneViewFamily* ViewFamily, const struct FSceneViewInitOptions* ViewInitOptions)
{
	// Create and add the new view
	FViewInfo* NewView = new FViewInfo(*ViewInitOptions);
	ViewFamily->Views.Add(NewView);
	SetRenderTarget(RHICmdList, ViewFamily->RenderTarget->GetRenderTargetTexture(), nullptr, ESimpleRenderTargetMode::EClearColorExistingDepth);
	FViewInfo* View = (FViewInfo*)ViewFamily->Views[0];
	View->InitRHIResources(nullptr);
}

void FRendererModule::BeginRenderingViewFamily(FCanvas* Canvas,FSceneViewFamily* ViewFamily)
{
	UWorld* World = nullptr; 
	check(ViewFamily->Scene);

	FScene* const Scene = ViewFamily->Scene->GetRenderScene();
	if (Scene)
	{
		World = Scene->GetWorld();
		if (World)
		{
			//guarantee that all render proxies are up to date before kicking off a BeginRenderViewFamily.
			World->SendAllEndOfFrameUpdates();
		}
	}

	// Flush the canvas first.
	Canvas->Flush_GameThread();

	// Increment FrameNumber before render the scene. Wrapping around is no problem.
	// This is the only spot we change GFrameNumber, other places can only read.
	++GFrameNumber;

	// this is passes to the render thread, better access that than GFrameNumberRenderThread
	ViewFamily->FrameNumber = GFrameNumber;

	for (int ViewExt = 0; ViewExt < ViewFamily->ViewExtensions.Num(); ViewExt++)
	{
		ViewFamily->ViewExtensions[ViewExt]->BeginRenderViewFamily(*ViewFamily);
	}
	
	if (Scene)
	{		
		// Set the world's "needs full lighting rebuild" flag if the scene has any uncached static lighting interactions.
		if(World)
		{
			// Note: reading NumUncachedStaticLightingInteractions on the game thread here which is written to by the rendering thread
			// This is reliable because the RT uses interlocked mechanisms to update it
			World->SetMapNeedsLightingFullyRebuilt(Scene->NumUncachedStaticLightingInteractions);
		}
	
		// Construct the scene renderer.  This copies the view family attributes into its own structures.
		FSceneRenderer* SceneRenderer = FSceneRenderer::CreateSceneRenderer(ViewFamily, Canvas->GetHitProxyConsumer());

		ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
			FDrawSceneCommand,
			FSceneRenderer*,SceneRenderer,SceneRenderer,
		{
			RenderViewFamily_RenderThread(RHICmdList, SceneRenderer);
			FlushPendingDeleteRHIResources_RenderThread();
		});
	}
}

void FRendererModule::UpdateMapNeedsLightingFullyRebuiltState(UWorld* World)
{
	World->SetMapNeedsLightingFullyRebuilt(World->Scene->GetRenderScene()->NumUncachedStaticLightingInteractions);
}

void FRendererModule::DrawRectangle(
		FRHICommandList& RHICmdList,
		float X,
		float Y,
		float SizeX,
		float SizeY,
		float U,
		float V,
		float SizeU,
		float SizeV,
		FIntPoint TargetSize,
		FIntPoint TextureSize,
		class FShader* VertexShader,
		EDrawRectangleFlags Flags
		)
{
	::DrawRectangle( RHICmdList, X, Y, SizeX, SizeY, U, V, SizeU, SizeV, TargetSize, TextureSize, VertexShader, Flags );
}


TGlobalResource<FFilterVertexDeclaration>& FRendererModule::GetFilterVertexDeclaration()
{
	return GFilterVertexDeclaration;
}

void FRendererModule::RegisterPostOpaqueRenderDelegate(const FPostOpaqueRenderDelegate& InPostOpaqueRenderDelegate)
{
	this->PostOpaqueRenderDelegate = InPostOpaqueRenderDelegate;
}

void FRendererModule::RegisterOverlayRenderDelegate(const FPostOpaqueRenderDelegate& InOverlayRenderDelegate)
{
	this->OverlayRenderDelegate = InOverlayRenderDelegate;
}

void FRendererModule::RenderPostOpaqueExtensions(const FSceneView& View, FRHICommandListImmediate& RHICmdList, FSceneRenderTargets& SceneContext)
{
	check(IsInRenderingThread());
	FPostOpaqueRenderParameters RenderParameters;
	RenderParameters.ViewMatrix = View.ViewMatrices.ViewMatrix;
	RenderParameters.ProjMatrix = View.ViewMatrices.ProjMatrix;
	RenderParameters.DepthTexture = SceneContext.GetSceneDepthSurface()->GetTexture2D();
	RenderParameters.SmallDepthTexture = SceneContext.GetSmallDepthSurface()->GetTexture2D();

	RenderParameters.ViewportRect = View.ViewRect;
	RenderParameters.RHICmdList = &RHICmdList;

	RenderParameters.Uid = (void*)(&View);
	PostOpaqueRenderDelegate.ExecuteIfBound(RenderParameters);
}

void FRendererModule::RenderOverlayExtensions(const FSceneView& View, FRHICommandListImmediate& RHICmdList, FSceneRenderTargets& SceneContext)
{
	check(IsInRenderingThread());
	FPostOpaqueRenderParameters RenderParameters;
	RenderParameters.ViewMatrix = View.ViewMatrices.ViewMatrix;
	RenderParameters.ProjMatrix = View.ViewMatrices.ProjMatrix;
	RenderParameters.DepthTexture = SceneContext.GetSceneDepthSurface()->GetTexture2D();
	RenderParameters.SmallDepthTexture = SceneContext.GetSmallDepthSurface()->GetTexture2D();

	RenderParameters.ViewportRect = View.ViewRect;
	RenderParameters.RHICmdList = &RHICmdList;

	RenderParameters.Uid=(void*)(&View);
	OverlayRenderDelegate.ExecuteIfBound(RenderParameters);
}

bool IsMobileHDR()
{
	static auto* MobileHDRCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
	return MobileHDRCvar->GetValueOnAnyThread() == 1;
}

bool IsMobileHDR32bpp()
{
	static auto* MobileHDR32bppModeCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR32bppMode"));
	return IsMobileHDR() && (GSupportsRenderTargetFormat_PF_FloatRGBA == false || MobileHDR32bppModeCvar->GetValueOnRenderThread() != 0);
}

bool IsMobileHDRMosaic()
{
	if (!IsMobileHDR32bpp())
		return false;

	static auto* MobileHDR32bppMode = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR32bppMode"));
	switch (MobileHDR32bppMode->GetValueOnRenderThread())
	{
		case 1:
			return true;
		case 2:
			return false;
		default:
			return !(GSupportsHDR32bppEncodeModeIntrinsic && GSupportsShaderFramebufferFetch);
	}
}
