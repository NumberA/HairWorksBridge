// @third party code - BEGIN HairWorks
#include "RendererPrivate.h"

#include "HairWorksSDK.h"

#ifndef _CPP
#define _CPP
#endif
#include <Nv/HairWorks/Shader/NvHairShaderCommonTypes.h>

#include "AllowWindowsPlatformTypes.h"
#include <Nv/Common/Platform/Dx11/NvCoDx11Handle.h>
#include "HideWindowsPlatformTypes.h"

#include "SceneUtils.h"
#include "ScreenRendering.h"
#include "SceneFilterRendering.h"
#include "AmbientCubemapParameters.h"
#include "HairWorksSceneProxy.h"
#include "HairWorksRenderer.h"

// UE4 uses shader class names, so we need put these classes in global name space to let compiler check name conflicts.
// Pixel shaders
class FHairWorksBaseShader
{
public:
	static bool ShouldCache(EShaderPlatform Platform)
	{
		return Platform == EShaderPlatform::SP_PCD3D_SM5;
	}
};

class FHairWorksBasePs: public FGlobalShader
{
protected:
	FHairWorksBasePs()
	{}

	FHairWorksBasePs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		HairConstantBuffer.Bind(Initializer.ParameterMap, TEXT("HairConstantBuffer"));

		TextureSampler.Bind(Initializer.ParameterMap, TEXT("TextureSampler"));

		RootColorTexture.Bind(Initializer.ParameterMap, TEXT("RootColorTexture"));
		TipColorTexture.Bind(Initializer.ParameterMap, TEXT("TipColorTexture"));
		SpecularColorTexture.Bind(Initializer.ParameterMap, TEXT("SpecularColorTexture"));
		StrandTexture.Bind(Initializer.ParameterMap, TEXT("StrandTexture"));

		NvHair_resourceFaceHairIndices.Bind(Initializer.ParameterMap, TEXT("NvHair_resourceFaceHairIndices"));
		NvHair_resourceTangents.Bind(Initializer.ParameterMap, TEXT("NvHair_resourceTangents"));
		NvHair_resourceNormals.Bind(Initializer.ParameterMap, TEXT("NvHair_resourceNormals"));
		NvHair_resourceMasterPositions.Bind(Initializer.ParameterMap, TEXT("NvHair_resourceMasterPositions"));
		NvHair_resourceMasterPrevPositions.Bind(Initializer.ParameterMap, TEXT("NvHair_resourceMasterPrevPositions"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const NvHair_ConstantBuffer& HairConstBuffer, const TArray<FTexture2DRHIRef>& HairTextures, ID3D11ShaderResourceView* HairSrvs[NvHair::ShaderResourceType::COUNT_OF])
	{
		FGlobalShader::SetParameters(RHICmdList, GetPixelShader(), View);

		SetShaderValue(RHICmdList, GetPixelShader(), this->HairConstantBuffer, HairConstBuffer);

		SetSamplerParameter(RHICmdList, GetPixelShader(), TextureSampler, TStaticSamplerState<>::GetRHI());

		SetTextureParameter(RHICmdList, GetPixelShader(), RootColorTexture, HairTextures[NvHair::ETextureType::ROOT_COLOR]);
		SetTextureParameter(RHICmdList, GetPixelShader(), TipColorTexture, HairTextures[NvHair::ETextureType::TIP_COLOR]);
		SetTextureParameter(RHICmdList, GetPixelShader(), SpecularColorTexture, HairTextures[NvHair::ETextureType::SPECULAR]);
		SetTextureParameter(RHICmdList, GetPixelShader(), StrandTexture, HairTextures[NvHair::ETextureType::STRAND]);

		auto BindSrv = [&](FShaderResourceParameter& Parameter, NvHair::ShaderResourceType::Enum HairSrvType)
		{
			if(!Parameter.IsBound())
				return;

			HairWorks::GetD3DHelper().GetDeviceContext(RHICmdList.GetContext())->PSSetShaderResources(Parameter.GetBaseIndex(), 1, &HairSrvs[HairSrvType]);
		};

		BindSrv(NvHair_resourceFaceHairIndices, NvHair::ShaderResourceType::HAIR_INDICES);
		BindSrv(NvHair_resourceTangents, NvHair::ShaderResourceType::TANGENTS);
		BindSrv(NvHair_resourceNormals, NvHair::ShaderResourceType::NORMALS);
		BindSrv(NvHair_resourceMasterPositions, NvHair::ShaderResourceType::MASTER_POSITIONS);
		BindSrv(NvHair_resourceMasterPrevPositions, NvHair::ShaderResourceType::PREV_MASTER_POSITIONS);
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << HairConstantBuffer << TextureSampler << RootColorTexture << TipColorTexture << SpecularColorTexture << StrandTexture << NvHair_resourceFaceHairIndices << NvHair_resourceTangents << NvHair_resourceNormals << NvHair_resourceMasterPositions << NvHair_resourceMasterPrevPositions;

		return bShaderHasOutdatedParameters;
	}

	FShaderParameter HairConstantBuffer;

	FShaderResourceParameter TextureSampler;

	FShaderResourceParameter RootColorTexture;
	FShaderResourceParameter TipColorTexture;
	FShaderResourceParameter SpecularColorTexture;
	FShaderResourceParameter StrandTexture;

	FShaderResourceParameter	NvHair_resourceFaceHairIndices;
	FShaderResourceParameter	NvHair_resourceTangents;
	FShaderResourceParameter	NvHair_resourceNormals;
	FShaderResourceParameter	NvHair_resourceMasterPositions;
	FShaderResourceParameter	NvHair_resourceMasterPrevPositions;

};

class FHairWorksBasePassPs: public FHairWorksBasePs, public FHairWorksBaseShader
{
	DECLARE_SHADER_TYPE(FHairWorksBasePassPs, Global);

	FHairWorksBasePassPs()
	{}

	FHairWorksBasePassPs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FHairWorksBasePs(Initializer)
	{
		CubemapShaderParameters.Bind(Initializer.ParameterMap);
		CubemapAmbient.Bind(Initializer.ParameterMap, TEXT("bCubemapAmbient"));
		PrecomputedLightingBuffer.Bind(Initializer.ParameterMap, TEXT("PrecomputedLightingBuffer"));
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FHairWorksBasePs::Serialize(Ar);

		Ar << CubemapShaderParameters << CubemapAmbient << PrecomputedLightingBuffer;

		return bShaderHasOutdatedParameters;
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const NvHair_ConstantBuffer& HairConstBuffer, const TArray<FTexture2DRHIRef>& HairTextures, ID3D11ShaderResourceView* HairSrvs[NvHair::ShaderResourceType::COUNT_OF], FUniformBufferRHIRef InPrecomputedLightingBuffer)
	{
		FHairWorksBasePs::SetParameters(RHICmdList, View, HairConstBuffer, HairTextures, HairSrvs);

		const bool bCubemapAmbient = View.FinalPostProcessSettings.ContributingCubemaps.Num() > 0;
		SetShaderValue(RHICmdList, GetPixelShader(), CubemapAmbient, bCubemapAmbient);
		CubemapShaderParameters.SetParameters(RHICmdList, GetPixelShader(), bCubemapAmbient ? View.FinalPostProcessSettings.ContributingCubemaps[0] : FFinalPostProcessSettings::FCubemapEntry());

		SetUniformBufferParameter(RHICmdList, GetPixelShader(), PrecomputedLightingBuffer, InPrecomputedLightingBuffer);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

	FCubemapShaderParameters CubemapShaderParameters;
	FShaderParameter CubemapAmbient;
	FShaderUniformBufferParameter PrecomputedLightingBuffer;
};

IMPLEMENT_SHADER_TYPE(, FHairWorksBasePassPs, TEXT("HairWorks"), TEXT("BasePassPs"), SF_Pixel);

class FHairWorksColorizePs: public FHairWorksBasePs, public FHairWorksBaseShader
{
	DECLARE_SHADER_TYPE(FHairWorksColorizePs, Global);

	FHairWorksColorizePs()
	{}

	FHairWorksColorizePs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FHairWorksBasePs(Initializer)
	{}

	using FHairWorksBasePs::SetParameters;

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}
};

IMPLEMENT_SHADER_TYPE(, FHairWorksColorizePs, TEXT("HairWorks"), TEXT("ColorizePs"), SF_Pixel);

class FHairWorksShadowDepthPs: public FGlobalShader, public FHairWorksBaseShader
{
	DECLARE_SHADER_TYPE(FHairWorksShadowDepthPs, Global);

	FHairWorksShadowDepthPs()
	{}

	FHairWorksShadowDepthPs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ShadowParams.Bind(Initializer.ParameterMap, TEXT("ShadowParams"));
	}

	bool Serialize(FArchive& Ar) override
	{
		bool bSerialized = FGlobalShader::Serialize(Ar);
		Ar << ShadowParams;
		return bSerialized;
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

	FShaderParameter ShadowParams;
};

IMPLEMENT_SHADER_TYPE(, FHairWorksShadowDepthPs, TEXT("HairWorks"), TEXT("ShadowDepthMain"), SF_Pixel);

class FHairWorksCopyDepthPs: public FGlobalShader, public FHairWorksBaseShader
{
	DECLARE_SHADER_TYPE(FHairWorksCopyDepthPs, Global);

	FHairWorksCopyDepthPs()
	{}

	FHairWorksCopyDepthPs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SceneDepthTexture.Bind(Initializer.ParameterMap, TEXT("SceneDepthTexture"));
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SceneDepthTexture;
		return bShaderHasOutdatedParameters;
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

	FShaderResourceParameter SceneDepthTexture;
};

IMPLEMENT_SHADER_TYPE(, FHairWorksCopyDepthPs, TEXT("HairWorks"), TEXT("CopyDepthPs"), SF_Pixel);

class FHairWorksResolveDepthShader: public FGlobalShader, public FHairWorksBaseShader	// Original class name is FResolveDepthPs. But it causes streaming error with FResolveDepthPS, which allocates numerous memory.
{
	DECLARE_SHADER_TYPE(FHairWorksResolveDepthShader, Global);

	FHairWorksResolveDepthShader()
	{}

	FHairWorksResolveDepthShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		DepthTexture.Bind(Initializer.ParameterMap, TEXT("DepthTexture"));
		StencilTexture.Bind(Initializer.ParameterMap, TEXT("StencilTexture"));
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DepthTexture << StencilTexture;
		return bShaderHasOutdatedParameters;
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

	FShaderResourceParameter DepthTexture;
	FShaderResourceParameter StencilTexture;
};

IMPLEMENT_SHADER_TYPE(, FHairWorksResolveDepthShader, TEXT("HairWorks"), TEXT("ResolveDepthPs"), SF_Pixel);

class FHairWorksResolveOpaqueDepthPs: public FGlobalShader, public FHairWorksBaseShader
{
	DECLARE_SHADER_TYPE(FHairWorksResolveOpaqueDepthPs, Global);

	FHairWorksResolveOpaqueDepthPs()
	{}

	FHairWorksResolveOpaqueDepthPs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		DepthTexture.Bind(Initializer.ParameterMap, TEXT("DepthTexture"));
		HairColorTexture.Bind(Initializer.ParameterMap, TEXT("HairColorTexture"));
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DepthTexture << HairColorTexture;
		return bShaderHasOutdatedParameters;
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

	FShaderResourceParameter DepthTexture;
	FShaderResourceParameter HairColorTexture;
};

IMPLEMENT_SHADER_TYPE(, FHairWorksResolveOpaqueDepthPs, TEXT("HairWorks"), TEXT("ResolveOpaqueDepthPs"), SF_Pixel);

class FHairWorksCopyVelocityPs: public FGlobalShader, public FHairWorksBaseShader
{
	DECLARE_SHADER_TYPE(FHairWorksCopyVelocityPs, Global);

	FHairWorksCopyVelocityPs()
	{}

	FHairWorksCopyVelocityPs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		VelocityTexture.Bind(Initializer.ParameterMap, TEXT("VelocityTexture"));
		DepthTexture.Bind(Initializer.ParameterMap, TEXT("DepthTexture"));
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << VelocityTexture << DepthTexture;
		return bShaderHasOutdatedParameters;
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

	FShaderResourceParameter VelocityTexture;
	FShaderResourceParameter DepthTexture;
};

IMPLEMENT_SHADER_TYPE(, FHairWorksCopyVelocityPs, TEXT("HairWorks"), TEXT("CopyVelocityPs"), SF_Pixel);

class FHairWorksBlendLightingColorPs: public FGlobalShader, public FHairWorksBaseShader
{
	DECLARE_SHADER_TYPE(FHairWorksBlendLightingColorPs, Global);

	FHairWorksBlendLightingColorPs()
	{}

	FHairWorksBlendLightingColorPs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		AccumulatedColorTexture.Bind(Initializer.ParameterMap, TEXT("AccumulatedColorTexture"));
		PrecomputedLightTexture.Bind(Initializer.ParameterMap, TEXT("PrecomputedLightTexture"));
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << AccumulatedColorTexture << PrecomputedLightTexture;
		return bShaderHasOutdatedParameters;
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

	FShaderResourceParameter AccumulatedColorTexture;
	FShaderResourceParameter PrecomputedLightTexture;
};

IMPLEMENT_SHADER_TYPE(, FHairWorksBlendLightingColorPs, TEXT("HairWorks"), TEXT("BlendLightingColorPs"), SF_Pixel);

class FHairWorksHitProxyPs: public FGlobalShader, public FHairWorksBaseShader
{
	DECLARE_SHADER_TYPE(FHairWorksHitProxyPs, Global);

	FHairWorksHitProxyPs()
	{}

	FHairWorksHitProxyPs(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		HitProxyId.Bind(Initializer.ParameterMap, TEXT("HitProxyId"));
	}

	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << HitProxyId;
		return bShaderHasOutdatedParameters;
	}

	void SetParameters(FRHICommandList& RHICmdList, const FHitProxyId HitProxyIdValue, const FSceneView& View)
	{
		FGlobalShader::SetParameters(RHICmdList, GetPixelShader(), View);
		SetShaderValue(RHICmdList, GetPixelShader(), HitProxyId, HitProxyIdValue.GetColor().ReinterpretAsLinear());
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

protected:
	FShaderParameter HitProxyId;
};

IMPLEMENT_SHADER_TYPE(, FHairWorksHitProxyPs, TEXT("HairWorks"), TEXT("HitProxyPs"), SF_Pixel);

namespace HairWorksRenderer
{
	// Configuration console variables.
	TAutoConsoleVariable<float> CVarHairShadowTexelsScale(TEXT("r.HairWorks.Shadow.TexelsScale"), 5, TEXT(""), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarHairShadowBiasScale(TEXT("r.HairWorks.Shadow.BiasScale"), 0, TEXT(""), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<int> CVarHairMsaaLevel(TEXT("r.HairWorks.MsaaLevel"), 4, TEXT(""), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarHairOutputVelocity(TEXT("r.HairWorks.OutputVelocity"), 1, TEXT(""), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<int> CVarHairAlwaysCreateRenderTargets(TEXT("r.HairWorks.AlwaysCreateRenderTargets"), 0, TEXT(""), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<int> CVarHairFrameRateIndependentRendering(TEXT("r.HairWorks.FrameRateIndependentRendering"), 0, TEXT(""), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarHairSimulateFps(TEXT("r.HairWorks.SimulateFps"), 60, TEXT(""), ECVF_RenderThreadSafe);

	// Buffers
	TSharedRef<FRenderTargets> HairRenderTargets(new FRenderTargets);

	// To release buffers.
	class FHairGlobalResource: public FRenderResource
	{
	public:
		void ReleaseDynamicRHI()override
		{
			HairRenderTargets = MakeShareable(new FRenderTargets);
		}
	};

	static TGlobalResource<FHairGlobalResource> HairGlobalResource;

	// Constant buffer for per instance data
	IMPLEMENT_UNIFORM_BUFFER_STRUCT(FHairInstanceDataShaderUniform, TEXT("HairInstanceData"))

	// 
	template<typename FPixelShader, typename Funtion>
	void DrawFullScreen(FRHICommandList& RHICmdList, Funtion SetShaderParameters, bool bBlend = false, bool bDepth = false)
	{
		// Set render states
		RHICmdList.SetRasterizerState(GetStaticRasterizerState<false>(FM_Solid, CM_None));

		if(bDepth)
			RHICmdList.SetDepthStencilState(TStaticDepthStencilState<true, CF_Always>::GetRHI());
		else
			RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

		if(bBlend)
			RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI());
		else
			RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());

		// Set shader
		TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
		TShaderMapRef<FPixelShader> PixelShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));

		static FGlobalBoundShaderState BoundShaderState;
		SetGlobalBoundShaderState(RHICmdList, ERHIFeatureLevel::SM5, BoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);

		// Set shader parameters
		SetShaderParameters(**PixelShader);

		// Draw
		const auto Size = FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY();
		RHICmdList.SetViewport(0, 0, 0, Size.X, Size.Y, 1);

		DrawRectangle(
			RHICmdList,
			0, 0,
			Size.X, Size.Y,
			0, 0,
			Size.X, Size.Y,
			Size,
			Size,
			*VertexShader
			);
	}

	void AccumulateStats(const FHairWorksSceneProxy& HairSceneProxy)
	{
#if STATS
		static const auto& CVarHairStats = *IConsoleManager::Get().FindConsoleVariable(TEXT("r.HairWorks.Stats"));
		if(CVarHairStats.GetInt() == 0)
			return;

		NvHair::Stats HairStats;
		HairWorks::GetSDK()->computeStats(nullptr, false, HairSceneProxy.GetHairInstanceId(), HairStats);
		HairWorks::AccumulateStats(HairStats);
#endif
	}

	void SetProjViewInfo(NvHair::Sdk& sdk, const FViewInfo& View)
	{
		const auto& ViewRect = View.ViewRect;
		NvHair::Viewport HairViewPort;
		HairViewPort.init(ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Width(), ViewRect.Height());

		const auto& ViewMatrices = View.ViewMatrices;

		sdk.setViewProjection(HairViewPort, reinterpret_cast<const gfsdk_float4x4&>(ViewMatrices.ViewMatrix.M), reinterpret_cast<const gfsdk_float4x4&>(ViewMatrices.ProjMatrix.M), NvHair::HandednessHint::LEFT);
		sdk.setPrevViewProjection(HairViewPort, reinterpret_cast<const gfsdk_float4x4&>(View.PrevViewMatrices.ViewMatrix.M), reinterpret_cast<const gfsdk_float4x4&>(View.PrevViewMatrices.ProjMatrix.M), NvHair::HandednessHint::LEFT);
	}

	void SetupViews(TArray<FViewInfo>& Views)
	{
		for(auto& View : Views)
		{
			check(View.VisibleHairs.Num() == 0);

			for(auto* PrimitiveInfo: View.VisibleDynamicPrimitives)
			{
				auto& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveInfo->GetIndex()];
				if(ViewRelevance.bHairWorks)
				{
					View.VisibleHairs.Add(PrimitiveInfo);
				}
			}
		}
	}

	void FindFreeElementInPool(FRHICommandList& RHICmdList, const FPooledRenderTargetDesc& Desc, TRefCountPtr<IPooledRenderTarget> &Out, const TCHAR* InDebugName)
	{
		// There is bug. When a render target is created from a existing pointer, AllocationLevelInKB is not decreased. This cause assertion failure in FRenderTargetPool::GetStats(). So we have to release it first.
		if(Out != nullptr)
		{
			if(!Out->GetDesc().Compare(Desc, true))
			{
				GRenderTargetPool.FreeUnusedResource(Out);
				Out = nullptr;
			}
		}

		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, Out, InDebugName);

		// Release useless resolved render resource. Because of the reason mentioned above, we do in only in the macro.
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
		if(Out->GetDesc().NumSamples > 1)
			Out->GetRenderTargetItem().ShaderResourceTexture = nullptr;
#endif
	}

	// Create velocity buffer if necessary
	void AllocVelocityBuffer(FRHICommandList& RHICmdList, const TArray<FViewInfo>& Views)
	{
		HairRenderTargets->VelocityBuffer = nullptr;

		if (!CVarHairOutputVelocity.GetValueOnRenderThread())
			return;

		bool bNeedsVelocity = false;

		for(int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];

			bool bTemporalAA = (View.FinalPostProcessSettings.AntiAliasingMethod == AAM_TemporalAA) && !View.bCameraCut;
			bool bMotionBlur = IsMotionBlurEnabled(View);

			bNeedsVelocity |= bMotionBlur || bTemporalAA;
		}

		if(bNeedsVelocity)
		{
			check(HairRenderTargets->GBufferA);

			auto Desc = HairRenderTargets->GBufferA->GetDesc();
			Desc.Format = PF_G16R16;
			FindFreeElementInPool(RHICmdList, Desc, HairRenderTargets->VelocityBuffer, TEXT("HairGBufferC"));
		}
	}

	void AllocRenderTargets(FRHICommandList& RHICmdList, const FIntPoint& Size)
	{
		// Get MSAA level
		int SampleCount = CVarHairMsaaLevel.GetValueOnRenderThread();
		if(SampleCount >= 8)
			SampleCount = 8;
		else if(SampleCount >= 4)
			SampleCount = 4;
		else if(SampleCount >= 2)
			SampleCount = 2;
		else
			SampleCount = 1;

		// GBuffers
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Size, PF_B8G8R8A8, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable, false));
		Desc.NumSamples = SampleCount;
		FindFreeElementInPool(RHICmdList, Desc, HairRenderTargets->GBufferA, TEXT("HairGBufferA"));
		Desc.Flags |= ETextureCreateFlags::TexCreate_SRGB;	// SRGB for diffuse
		FindFreeElementInPool(RHICmdList, Desc, HairRenderTargets->GBufferB, TEXT("HairGBufferB"));
		Desc.Flags &= ~ETextureCreateFlags::TexCreate_SRGB;
		FindFreeElementInPool(RHICmdList, Desc, HairRenderTargets->GBufferC, TEXT("HairGBufferC"));
		Desc.Format = PF_FloatRGBA;
		FindFreeElementInPool(RHICmdList, Desc, HairRenderTargets->PrecomputedLight, TEXT("HairPrecomputedLight"));

		// Color buffer
		Desc.NumSamples = 1;
		Desc.Format = PF_FloatRGBA;
		FindFreeElementInPool(RHICmdList, Desc, HairRenderTargets->AccumulatedColor, TEXT("HairAccumulatedColor"));

		// Depth buffer
		Desc = FPooledRenderTargetDesc::Create2DDesc(Size, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_None, TexCreate_DepthStencilTargetable, false);
		Desc.NumSamples = SampleCount;
		FindFreeElementInPool(RHICmdList, Desc, HairRenderTargets->HairDepthZ, TEXT("HairDepthZ"));

		HairRenderTargets->StencilSRV = RHICreateShaderResourceView((FTexture2DRHIRef&)HairRenderTargets->HairDepthZ->GetRenderTargetItem().TargetableTexture, 0, 1, PF_X24_G8);

		Desc.NumSamples = 1;
		FindFreeElementInPool(RHICmdList, Desc, HairRenderTargets->HairDepthZForShadow, TEXT("HairDepthZForShadow"));

		// Reset light attenuation
		HairRenderTargets->LightAttenuation = nullptr;
	}

	void CopySceneDepth(FRHICommandList& RHICmdList)
	{
		DrawFullScreen<FHairWorksCopyDepthPs>(
			RHICmdList,
			[&](FHairWorksCopyDepthPs& Shader){SetTextureParameter(RHICmdList, Shader.GetPixelShader(), Shader.SceneDepthTexture, FSceneRenderTargets::Get(RHICmdList).GetSceneDepthTexture()); },
			false,
			true
			);
	}

	bool ViewsHasHair(const TArray<FViewInfo>& Views)
	{
		for(auto& View : Views)
		{
			if(View.VisibleHairs.Num())
				return true;
		}

		return false;
	}

	void RenderBasePass(FRHICommandList& RHICmdList, TArray<FViewInfo>& Views)
	{
		// Clear accumulated color
		SCOPED_DRAW_EVENT(RHICmdList, RenderHairBasePass);

		SetRenderTarget(RHICmdList, HairRenderTargets->AccumulatedColor->GetRenderTargetItem().TargetableTexture, nullptr, ESimpleRenderTargetMode::EClearColorExistingDepth);

		// Prepare velocity buffer
		AllocVelocityBuffer(RHICmdList, Views);

		// Setup render targets
		FRHIRenderTargetView RenderTargetViews[5] = {
			FRHIRenderTargetView(HairRenderTargets->GBufferA->GetRenderTargetItem().TargetableTexture), 
			FRHIRenderTargetView(HairRenderTargets->GBufferB->GetRenderTargetItem().TargetableTexture), 
			FRHIRenderTargetView(HairRenderTargets->GBufferC->GetRenderTargetItem().TargetableTexture),
			FRHIRenderTargetView(HairRenderTargets->PrecomputedLight->GetRenderTargetItem().TargetableTexture),
			FRHIRenderTargetView(HairRenderTargets->VelocityBuffer ? HairRenderTargets->VelocityBuffer->GetRenderTargetItem().TargetableTexture : nullptr),
		};

		// UE4 doesn't clear all targets if there's a null render target in the array. So let's manually clear each of them.
		for(auto& RenderTarget : RenderTargetViews)
		{
			if(RenderTarget.Texture != nullptr)
				SetRenderTarget(RHICmdList, RenderTarget.Texture, nullptr, ESimpleRenderTargetMode::EClearColorExistingDepth);
		}

		FRHISetRenderTargetsInfo RenderTargetsInfo(5, RenderTargetViews, FRHIDepthRenderTargetView(HairRenderTargets->HairDepthZ->GetRenderTargetItem().TargetableTexture));
		RenderTargetsInfo.SetClearDepthStencil(true, 0);

		RHICmdList.SetRenderTargetsAndClear(RenderTargetsInfo);

		// Copy scene depth to hair depth buffer.
		DrawFullScreen<FHairWorksCopyDepthPs>(
			RHICmdList,
			[&](FHairWorksCopyDepthPs& Shader){SetTextureParameter(RHICmdList, Shader.GetPixelShader(), Shader.SceneDepthTexture, FSceneRenderTargets::Get(RHICmdList).GetSceneDepthTexture()); },
			false,
			true
			);

		// Render states
		RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());
		auto DepthStencilState = TStaticDepthStencilState<true, CF_GreaterEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI();

		// Draw hairs
		HairWorks::GetSDK()->setCurrentContext(NvCo::Dx11Type::wrap(HairWorks::GetD3DHelper().GetDeviceContext(RHICmdList.GetContext())));

		FHairInstanceDataShaderUniform HairShaderUniformStruct;
		TArray<TPair<FHairWorksSceneProxy*, int>, SceneRenderingAllocator> HairStencilValues;	// We use the same stencil value for a hair existing in multiple views

		for(auto& View : Views)
		{
			// Set render states
			const auto& ViewRect = View.ViewRect;

			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0, ViewRect.Max.X, ViewRect.Max.Y, 1);

			// Pass camera information
			SetProjViewInfo(*HairWorks::GetSDK(), View);

			// Draw hair instances
			int NewStencilValue = 1;
			HairStencilValues.Reserve(View.VisibleHairs.Num());

			for(auto& PrimitiveInfo : View.VisibleHairs)
			{
				auto& HairSceneProxy = static_cast<FHairWorksSceneProxy&>(*PrimitiveInfo->Proxy);
				if(HairSceneProxy.GetHairInstanceId() == NvHair::INSTANCE_ID_NULL)
					continue;

				// Skip colorize
				NvHair::InstanceDescriptor HairDescriptor;
				HairWorks::GetSDK()->getInstanceDescriptor(HairSceneProxy.GetHairInstanceId(), HairDescriptor);

				if(HairDescriptor.m_colorizeMode != NvHair::ColorizeMode::NONE)
				{
					if(View.Family->EngineShowFlags.CompositeEditorPrimitives)
					{
						continue;
					}
					else
					{
						HairDescriptor.m_colorizeMode = NvHair::ColorizeMode::NONE;
						HairWorks::GetSDK()->updateInstanceDescriptor(HairSceneProxy.GetHairInstanceId(), HairDescriptor);
					}
				}

				// Find stencil value for this hair
				auto* UsedStencil = HairStencilValues.FindByPredicate(
					[&](const TPair<FHairWorksSceneProxy*, int>& HairAndStencil)
				{
					return HairAndStencil.Key == &HairSceneProxy;
				}
				);

				int StencilValue;

				if(UsedStencil != nullptr)
				{
					StencilValue = UsedStencil->Value;
				}
				else
				{
					StencilValue = NewStencilValue;

					// Add for later use
					HairStencilValues.Add(TPairInitializer<FHairWorksSceneProxy*, int>(&HairSceneProxy, StencilValue));

					// Accumulate stencil value
					check(NewStencilValue <= UCHAR_MAX);
					NewStencilValue = (NewStencilValue + 1) % HairInstanceMaterialArraySize;
				}

				// Set stencil state
				RHICmdList.SetDepthStencilState(DepthStencilState, StencilValue);

				// Setup hair instance data uniform
				HairShaderUniformStruct.Spec0_SpecPower0_Spec1_SpecPower1[StencilValue] = FVector4(
					HairDescriptor.m_specularPrimary,
					HairDescriptor.m_specularPowerPrimary,
					HairDescriptor.m_specularSecondary,
					HairDescriptor.m_specularPowerSecondary
					);
				HairShaderUniformStruct.Spec1Offset_DiffuseBlend_ReceiveShadows_ShadowSigma[StencilValue] = FVector4(
					HairDescriptor.m_specularSecondaryOffset,
					HairDescriptor.m_diffuseBlend,
					HairDescriptor.m_receiveShadows,
					HairDescriptor.m_shadowSigma * (254.f / 255.f)
					);
				HairShaderUniformStruct.GlintStrength[StencilValue] = FVector4(
					HairDescriptor.m_glintStrength
					);

				// Setup shader
				TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
				TShaderMapRef<FHairWorksBasePassPs> PixelShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));

				static FGlobalBoundShaderState BoundShaderState;

				SetGlobalBoundShaderState(
					RHICmdList,
					ERHIFeatureLevel::SM5,
					BoundShaderState,
					GSimpleElementVertexDeclaration.VertexDeclarationRHI,
					*VertexShader,
					*PixelShader
					);

				// Setup shader constants
				FUniformBufferRHIParamRef PrecomputedLightingBuffer = GEmptyPrecomputedLightingUniformBuffer.GetUniformBufferRHI();

				if(View.Family->EngineShowFlags.GlobalIllumination)
				{
					PrecomputedLightingBuffer = PrimitiveInfo->IndirectLightingCacheUniformBuffer;
				}

				NvHair::ShaderConstantBuffer ConstantBuffer;
				HairWorks::GetSDK()->prepareShaderConstantBuffer(HairSceneProxy.GetHairInstanceId(), ConstantBuffer);

				ID3D11ShaderResourceView* HairSrvs[NvHair::ShaderResourceType::COUNT_OF] = {};
				HairWorks::GetSDK()->getShaderResources(HairSceneProxy.GetHairInstanceId(), NV_NULL, NvHair::ShaderResourceType::COUNT_OF, NvCo::Dx11Type::wrapPtr(HairSrvs));


				PixelShader->SetParameters(RHICmdList, View, reinterpret_cast<NvHair_ConstantBuffer&>(ConstantBuffer), HairSceneProxy.GetTextures(), HairSrvs, PrecomputedLightingBuffer);

				// Flush render states
				HairWorks::GetD3DHelper().CommitShaderResources(RHICmdList.GetContext());

				// Draw
				HairSceneProxy.Draw(FHairWorksSceneProxy::EDrawType::Normal);
				AccumulateStats(HairSceneProxy);
			}
		}

		// Setup hair materials lookup table
		HairRenderTargets->HairInstanceDataShaderUniform = TUniformBufferRef<FHairInstanceDataShaderUniform>::CreateUniformBufferImmediate(HairShaderUniformStruct, UniformBuffer_SingleFrame);
		
		// Copy hair depth to receive shadow
		SetRenderTarget(RHICmdList, nullptr, HairRenderTargets->HairDepthZForShadow->GetRenderTargetItem().TargetableTexture);

		DrawFullScreen<FHairWorksResolveDepthShader>(
			RHICmdList,
			[&](FHairWorksResolveDepthShader& Shader)
		{
			SetTextureParameter(RHICmdList, Shader.GetPixelShader(), Shader.DepthTexture, HairRenderTargets->HairDepthZ->GetRenderTargetItem().TargetableTexture); 
			SetSRVParameter(RHICmdList, Shader.GetPixelShader(), Shader.StencilTexture, HairRenderTargets->StencilSRV);
		},
			false,
			true
			);

		// Copy depth for translucency occlusion
		SetRenderTarget(RHICmdList, nullptr, FSceneRenderTargets::Get(RHICmdList).GetSceneDepthSurface());

		DrawFullScreen<FHairWorksResolveOpaqueDepthPs>(
			RHICmdList,
			[&](FHairWorksResolveOpaqueDepthPs& Shader)
		{
			SetTextureParameter(RHICmdList, Shader.GetPixelShader(), Shader.DepthTexture, HairRenderTargets->HairDepthZ->GetRenderTargetItem().TargetableTexture);
			SetTextureParameter(RHICmdList, Shader.GetPixelShader(), Shader.HairColorTexture, HairRenderTargets->PrecomputedLight->GetRenderTargetItem().TargetableTexture);
		},
			false,
			true
			);
	}

	void RenderVelocities(FRHICommandList& RHICmdList, TRefCountPtr<IPooledRenderTarget>& VelocityRT)
	{
		// Resolve MSAA velocity
		if(!HairRenderTargets->VelocityBuffer)
			return;

		auto SetShaderParameters = [&](FHairWorksCopyVelocityPs& Shader)
		{
			SetTextureParameter(RHICmdList, Shader.GetPixelShader(), Shader.VelocityTexture, HairRenderTargets->VelocityBuffer->GetRenderTargetItem().TargetableTexture);
			SetTextureParameter(RHICmdList, Shader.GetPixelShader(), Shader.DepthTexture, HairRenderTargets->HairDepthZ->GetRenderTargetItem().TargetableTexture);
		};

		DrawFullScreen<FHairWorksCopyVelocityPs>(RHICmdList, SetShaderParameters);
	}

	void BeginRenderingSceneColor(FRHICommandList& RHICmdList)
	{
		FTextureRHIParamRef RenderTargetsRHIs[2] = {
			FSceneRenderTargets::Get(RHICmdList).GetSceneColorSurface(),
			HairRenderTargets->AccumulatedColor->GetRenderTargetItem().TargetableTexture,
		};

		SetRenderTargets(RHICmdList, 2, RenderTargetsRHIs, FSceneRenderTargets::Get(RHICmdList).GetSceneDepthSurface(), ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite);
	}

	void BlendLightingColor(FRHICommandList& RHICmdList)
	{
		FSceneRenderTargets::Get(RHICmdList).BeginRenderingSceneColor(RHICmdList);

		DrawFullScreen<FHairWorksBlendLightingColorPs>(
			RHICmdList,
			[&](FHairWorksBlendLightingColorPs& Shader)
		{
			SetTextureParameter(RHICmdList, Shader.GetPixelShader(), Shader.AccumulatedColorTexture, HairRenderTargets->AccumulatedColor->GetRenderTargetItem().TargetableTexture);
			SetTextureParameter(RHICmdList, Shader.GetPixelShader(), Shader.PrecomputedLightTexture, HairRenderTargets->PrecomputedLight->GetRenderTargetItem().TargetableTexture);
		},
			true
			);
	}

	bool IsLightAffectHair(const FLightSceneInfo& LightSceneInfo, const FViewInfo& View)
	{
		// No visible hairs, return false;
		if(View.VisibleHairs.Num() == 0)
			return false;

		// Check shadow caster list
		for(auto* Primitive = LightSceneInfo.DynamicPrimitiveList;
			Primitive != nullptr;
			Primitive = Primitive->GetNextPrimitive()
			)
		{
			auto& PrimitiveSceneInfo = *Primitive->GetPrimitiveSceneInfo();
			auto& PrimitiveViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveSceneInfo.GetIndex()];
			if(PrimitiveViewRelevance.bHairWorks)
			{
				return true;
			}
		}

		// If a light is not shadowed, its primitive list is null. So we check bounds.
		if(LightSceneInfo.DynamicPrimitiveList == nullptr)
		{
			for(auto& PrimitiveInfo : View.VisibleHairs)
			{
				if(LightSceneInfo.Proxy->AffectsBounds(PrimitiveInfo->Proxy->GetBounds()))
				{
					return true;
				}
			}
		}

		return false;
	}

	void RenderVisualization(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		// Render hairs
		SCOPED_DRAW_EVENT(RHICmdList, RenderHairVisualization);

		// Setup shader for colorize
		TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
		TShaderMapRef<FHairWorksColorizePs> PixelShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));

		static FGlobalBoundShaderState BoundShaderState;

		SetGlobalBoundShaderState(
			RHICmdList,
			ERHIFeatureLevel::SM5,
			BoundShaderState,
			GSimpleElementVertexDeclaration.VertexDeclarationRHI,
			*VertexShader,
			*PixelShader
			);

		// Setup render state
		RHICmdList.SetDepthStencilState(TStaticDepthStencilState<>::GetRHI());

		// Setup camera
		HairWorks::GetSDK()->setCurrentContext(NvCo::Dx11Type::wrap(HairWorks::GetD3DHelper().GetDeviceContext(RHICmdList.GetContext())));

		SetProjViewInfo(*HairWorks::GetSDK(), View);

		// Flush render states
		HairWorks::GetD3DHelper().CommitShaderResources(RHICmdList.GetContext());

		// Render colorize
		for(auto& PrimitiveInfo : View.VisibleHairs)
		{
			// Skin none colorize
			auto& HairSceneProxy = static_cast<FHairWorksSceneProxy&>(*PrimitiveInfo->Proxy);

			NvHair::InstanceDescriptor HairDescriptor;
			HairWorks::GetSDK()->getInstanceDescriptor(HairSceneProxy.GetHairInstanceId(), HairDescriptor);

			if (HairDescriptor.m_colorizeMode == NvHair::ColorizeMode::NONE)
				continue;

			// Setup shader constants
			NvHair::ShaderConstantBuffer ConstantBuffer;
			HairWorks::GetSDK()->prepareShaderConstantBuffer(HairSceneProxy.GetHairInstanceId(), ConstantBuffer);

			ID3D11ShaderResourceView* HairSrvs[NvHair::ShaderResourceType::COUNT_OF];
			HairWorks::GetSDK()->getShaderResources(HairSceneProxy.GetHairInstanceId(), NV_NULL, NvHair::ShaderResourceType::COUNT_OF, NvCo::Dx11Type::wrapPtr(HairSrvs));

			PixelShader->SetParameters(RHICmdList, View, reinterpret_cast<NvHair_ConstantBuffer&>(ConstantBuffer), HairSceneProxy.GetTextures(), HairSrvs);

			// Flush render states
			HairWorks::GetD3DHelper().CommitShaderResources(RHICmdList.GetContext());

			// Draw
			HairSceneProxy.Draw(FHairWorksSceneProxy::EDrawType::Normal);
		}

		// Render visualization
		for(auto& PrimitiveInfo : View.VisibleHairs)
		{
			// Draw hair
			auto& HairSceneProxy = static_cast<FHairWorksSceneProxy&>(*PrimitiveInfo->Proxy);

			HairSceneProxy.Draw(FHairWorksSceneProxy::EDrawType::Visualization);
		}
	}

	void RenderHitProxies(FRHICommandList & RHICmdList, const TArray<FViewInfo>& Views)
	{
		SCOPED_DRAW_EVENT(RHICmdList, RenderHairHitProxies);

		HairWorks::GetSDK()->setCurrentContext(NvCo::Dx11Type::wrap(HairWorks::GetD3DHelper().GetDeviceContext(RHICmdList.GetContext())));

		for(auto& View : Views)
		{
			// Pass camera information
			SetProjViewInfo(*HairWorks::GetSDK(), View);

			for(auto& PrimitiveInfo : View.VisibleHairs)
			{
				auto& HairSceneProxy = static_cast<FHairWorksSceneProxy&>(*PrimitiveInfo->Proxy);
				if(HairSceneProxy.GetHairInstanceId() == NvHair::INSTANCE_ID_NULL)
					continue;

				// Setup shader
				TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
				TShaderMapRef<FHairWorksHitProxyPs> PixelShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));

				static FGlobalBoundShaderState BoundShaderState;

				SetGlobalBoundShaderState(
					RHICmdList,
					ERHIFeatureLevel::SM5,
					BoundShaderState,
					GSimpleElementVertexDeclaration.VertexDeclarationRHI,
					*VertexShader,
					*PixelShader
					);

				// Setup shader constants
				PixelShader->SetParameters(RHICmdList, PrimitiveInfo->DefaultDynamicHitProxyId, View);

				// Flush render states
				HairWorks::GetD3DHelper().CommitShaderResources(RHICmdList.GetContext());

				// Draw
				HairSceneProxy.Draw(FHairWorksSceneProxy::EDrawType::Normal);
			}
		}
	}

	void StepSimulation(FRHICommandList& RHICmdList, const float CurrentWorldTime, const float DeltaWorldTime)
	{
		// To disable instances that should not animate
		SCOPED_DRAW_EVENT(RHICmdList, SimulateHair);

		if(HairWorks::GetSDK() == nullptr)
			return;

		static TArray<NvHair::InstanceId> ReEnableHairInstances;
		checkSlow(ReEnableHairInstances.Num() == 0);

		auto AdvanceHairAnimation = [&]()
		{
			for(FHairWorksSceneProxy::TIterator Itr(FHairWorksSceneProxy::GetHairInstances()); Itr; Itr.Next())
			{
				auto& HairSceneProxy = *Itr;

				NvHair::InstanceDescriptor InstDesc;
				HairWorks::GetSDK()->getInstanceDescriptor(HairSceneProxy.GetHairInstanceId(), InstDesc);
				if(!InstDesc.m_enable)
					continue;

				if(HairSceneProxy.AdvanceAnimation())
					continue;

				InstDesc.m_enable = false;
				HairWorks::GetSDK()->updateInstanceDescriptor(HairSceneProxy.GetHairInstanceId(), InstDesc);

				if(!InstDesc.m_drawRenderHairs)
					continue;

				ReEnableHairInstances.Add(HairSceneProxy.GetHairInstanceId());
			}
		};

		// Trigger simulation
		HairWorks::GetSDK()->setCurrentContext(NvCo::Dx11Type::wrap(HairWorks::GetD3DHelper().GetDeviceContext(RHICmdList.GetContext())));

		// Handle frame rate independent rendering
		const float SimulateStepTime = 1.f / CVarHairSimulateFps.GetValueOnRenderThread();

		float RenderInterp = 1;

		if(CVarHairFrameRateIndependentRendering.GetValueOnRenderThread() != 0)
		{
			// Fix simulation time
			static float SimulateTime = 0;

			if(SimulateTime > CurrentWorldTime)
				SimulateTime = CurrentWorldTime - SimulateStepTime;

			if(SimulateTime <= CurrentWorldTime - DeltaWorldTime - SimulateStepTime)
				SimulateTime = CurrentWorldTime - DeltaWorldTime;

			// Do sub step simulation
			bool bAdvancedAnimation = false;

			while(SimulateTime + SimulateStepTime <= CurrentWorldTime)
			{
				// Advance animation
				if(!bAdvancedAnimation)
				{
					AdvanceHairAnimation();

					bAdvancedAnimation = true;
				}

				// Consume time
				SimulateTime += SimulateStepTime;

				// Set interpolated skinning
				const float SkinningBlend = DeltaWorldTime != 0 ? (1 - (CurrentWorldTime - SimulateTime) / DeltaWorldTime) : 0;
				checkSlow(SkinningBlend >= 0 && SkinningBlend <= 1);

				TArray<FMatrix> InterpolatedSkinningMatrices;

				for(FHairWorksSceneProxy::TConstIterator Itr(FHairWorksSceneProxy::GetHairInstances()); Itr; Itr.Next())
				{
					auto& HairSceneProxy = *Itr;
					if(HairSceneProxy.GetSkinningMatrices().Num() == 0)
						continue;

					NvHair::InstanceDescriptor InstDesc;
					HairWorks::GetSDK()->getInstanceDescriptor(HairSceneProxy.GetHairInstanceId(), InstDesc);
					if(!InstDesc.m_simulate)
						continue;

					InterpolatedSkinningMatrices.SetNumUninitialized(HairSceneProxy.GetSkinningMatrices().Num());

					for(int Idx = 0; Idx < InterpolatedSkinningMatrices.Num(); ++Idx)
					{
						FTransform BlendedTransform;
						BlendedTransform.Blend(
							FTransform(HairSceneProxy.GetPrevSkinningMatrices()[Idx]), 
							FTransform(HairSceneProxy.GetSkinningMatrices()[Idx]), 
							SkinningBlend
						);
						InterpolatedSkinningMatrices[Idx] = BlendedTransform.ToMatrixWithScale();
					}

					HairWorks::GetSDK()->updateSkinningMatrices(
						HairSceneProxy.GetHairInstanceId(),
						InterpolatedSkinningMatrices.Num(),
						reinterpret_cast<gfsdk_float4x4*>(InterpolatedSkinningMatrices.GetData())
					);		
				}

				// Do simulation
				HairWorks::GetSDK()->stepSimulation(SimulateStepTime, nullptr, true);
			}

			// Set current skinning
			for(FHairWorksSceneProxy::TConstIterator Itr(FHairWorksSceneProxy::GetHairInstances()); Itr; Itr.Next())
			{
				auto& HairSceneProxy = *Itr;
				if(HairSceneProxy.GetSkinningMatrices().Num() == 0)
					continue;

				HairWorks::GetSDK()->updateSkinningMatrices(
					HairSceneProxy.GetHairInstanceId(),
					HairSceneProxy.GetSkinningMatrices().Num(),
					reinterpret_cast<const gfsdk_float4x4*>(HairSceneProxy.GetSkinningMatrices().GetData())
				);
			}

			// Calculate render interpolation value
			RenderInterp = (CurrentWorldTime - SimulateTime) / SimulateStepTime;
			checkSlow(RenderInterp >= 0 && RenderInterp <= 1);
		}
		else	// Without frame rate independent rendering
		{
			AdvanceHairAnimation();

			HairWorks::GetSDK()->stepSimulation(SimulateStepTime, nullptr, true);
		}

		// Re-enable none-animating hairs
		for(auto HairInstId : ReEnableHairInstances)
		{
			NvHair::InstanceDescriptor InstDesc;
			HairWorks::GetSDK()->getInstanceDescriptor(HairInstId, InstDesc);

			InstDesc.m_enable = true;
			HairWorks::GetSDK()->updateInstanceDescriptor(HairInstId, InstDesc);
		}

		ReEnableHairInstances.Empty(ReEnableHairInstances.Num());

		// Prepare for rendering
		HairWorks::GetSDK()->preRender(RenderInterp);

		// Update pin mesh transform
		for(FHairWorksSceneProxy::TIterator Itr(FHairWorksSceneProxy::GetHairInstances()); Itr; Itr.Next())
		{
			// Get pin matrices
			auto& HairSceneProxy = *Itr;

			auto& Pins = HairSceneProxy.GetPinMeshes();
			if(Pins.Num() == 0)
				continue;

			TArray<FMatrix> PinMatrices;
			PinMatrices.SetNumUninitialized(Pins.Num());

			HairWorks::GetSDK()->getPinMatrices(nullptr, false, HairSceneProxy.GetHairInstanceId(), 0, PinMatrices.Num(), reinterpret_cast<gfsdk_float4x4*>(PinMatrices.GetData()));

			// UE4 uses left hand system.
			for(auto& PinMatrix : PinMatrices)
			{
				FTransform PinTransform(PinMatrix);
				FVector Scale = PinTransform.GetScale3D();
				Scale.X = -Scale.X;
				PinTransform.SetScale3D(Scale);
				PinMatrix = PinTransform.ToMatrixWithScale();
			}

			// Set pin mesh transform
			for(auto PinIndex = 0; PinIndex < Pins.Num(); ++PinIndex)
			{
				const auto& PinMeshes = Pins[PinIndex];

				// Update mesh transform
				for(const auto& PinMesh : PinMeshes)
				{
					const FMatrix NewLocalToWorld = PinMesh.LocalTransform * PinMatrices[PinIndex];

					PinMesh.Mesh->ApplyLateUpdateTransform(PinMesh.Mesh->GetLocalToWorld().Inverse() * NewLocalToWorld);
				}
			}

			// Set pin matrices for access from game thread. Mainly for editor.
			HairSceneProxy.SetPinMatrices(PinMatrices);
		}
	}

	void RenderShadow(FRHICommandList& RHICmdList, const FProjectedShadowInfo& Shadow, const FProjectedShadowInfo::PrimitiveArrayType& SubjectPrimitives, const FViewInfo& View)
	{
		SCOPED_DRAW_EVENT(RHICmdList, RenderHairShadow);

		for (auto PrimitiveIdx = 0; PrimitiveIdx < SubjectPrimitives.Num(); ++PrimitiveIdx)
		{
			// Skip
			auto* PrimitiveInfo = SubjectPrimitives[PrimitiveIdx];
			auto& ViewRelavence = View.PrimitiveViewRelevanceMap[PrimitiveInfo->GetIndex()];
			if (!ViewRelavence.bHairWorks)
				continue;

			auto& HairSceneProxy = static_cast<FHairWorksSceneProxy&>(*PrimitiveInfo->Proxy);
			if (HairSceneProxy.GetHairInstanceId() == NvHair::INSTANCE_ID_NULL)
				continue;

			NvHair::InstanceDescriptor HairDesc;
			HairWorks::GetSDK()->getInstanceDescriptor(HairSceneProxy.GetHairInstanceId(), HairDesc);
			if(!HairDesc.m_castShadows)
				continue;

			// Setup render states and shaders
			TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));

			if(Shadow.CascadeSettings.bOnePassPointLightShadow)
			{
				// Setup camera
				const FBoxSphereBounds& PrimitiveBounds = HairSceneProxy.GetBounds();

				FViewMatrices ViewMatrices[6];
				bool Visible[6];
				for (int32 FaceIndex = 0; FaceIndex < 6; FaceIndex++)
				{
					ViewMatrices[FaceIndex].ViewMatrix = Shadow.OnePassShadowViewProjectionMatrices[FaceIndex];
					Visible[FaceIndex] = Shadow.OnePassShadowFrustums[FaceIndex].IntersectBox(PrimitiveBounds.Origin, PrimitiveBounds.BoxExtent);
				}

				gfsdk_float4x4 HairViewMatrices[6];
				gfsdk_float4x4 HairProjMatrices[6];
				for (int FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
				{
					HairViewMatrices[FaceIdx] = *(gfsdk_float4x4*)ViewMatrices[FaceIdx].ViewMatrix.M;
					HairProjMatrices[FaceIdx] = *(gfsdk_float4x4*)ViewMatrices[FaceIdx].ProjMatrix.M;
				}

				NvHair::Viewport viewPorts[6];

				for(auto& viewPort : viewPorts)
				{
					viewPort.init(0, 0, Shadow.ResolutionX, Shadow.ResolutionX);
				}

				HairWorks::GetSDK()->setCubeMapViewProjection(viewPorts, HairViewMatrices, HairProjMatrices, Visible, NvHair::HandednessHint::LEFT);

				// Setup shader
				static FGlobalBoundShaderState BoundShaderState;
				SetGlobalBoundShaderState(RHICmdList, ERHIFeatureLevel::SM5, BoundShaderState, GSimpleElementVertexDeclaration.VertexDeclarationRHI,
					*VertexShader, nullptr);
			}
			else
			{
				// Setup camera
				const auto& ViewRect = View.ViewRect;
				NvHair::Viewport HairViewPort;
				HairViewPort.init(ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Width(), ViewRect.Height());

				FViewMatrices ViewMatrices;
				ViewMatrices.ViewMatrix = FTranslationMatrix(Shadow.PreShadowTranslation) * Shadow.SubjectAndReceiverMatrix;
				HairWorks::GetSDK()->setViewProjection(HairViewPort, reinterpret_cast<const gfsdk_float4x4&>(ViewMatrices.ViewMatrix.M), reinterpret_cast<const gfsdk_float4x4&>(ViewMatrices.ProjMatrix.M), NvHair::HandednessHint::LEFT);

				// Setup shader
				TShaderMapRef<FHairWorksShadowDepthPs> PixelShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));

				static FGlobalBoundShaderState BoundShaderState;
				SetGlobalBoundShaderState(RHICmdList, ERHIFeatureLevel::SM5, BoundShaderState, GSimpleElementVertexDeclaration.VertexDeclarationRHI,
					*VertexShader, *PixelShader);

				SetShaderValue(RHICmdList, PixelShader->GetPixelShader(), PixelShader->ShadowParams, FVector2D(Shadow.GetShaderDepthBias() * CVarHairShadowBiasScale.GetValueOnRenderThread(), Shadow.InvMaxSubjectDepth));
			}

			// Flush render states
			HairWorks::GetD3DHelper().CommitShaderResources(RHICmdList.GetContext());

			// Draw hair
			HairSceneProxy.Draw(FHairWorksSceneProxy::EDrawType::Shadow);
			AccumulateStats(HairSceneProxy);
		}
	}
}
// @third party code - END HairWorks
