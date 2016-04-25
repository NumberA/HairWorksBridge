// @third party code - BEGIN HairWorks
#include "EnginePrivate.h"
#include "HairWorksSDK.h"
#include "Engine/HairWorksMaterial.h"

UHairWorksMaterial::UHairWorksMaterial(const class FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UHairWorksMaterial::PostLoad()
{
	Super::PostLoad();

	// Compile shader
	if(HairWorks::GetSDK() == nullptr)
		return;

	NvHair::InstanceDescriptor HairDesc;
	TArray<UTexture2D*> HairTextures;
	SyncHairDescriptor(HairDesc, HairTextures, false);

	NvHair::ShaderCacheSettings ShaderCacheSettings;
	ShaderCacheSettings.setFromInstanceDescriptor(HairDesc);

	for(int Index = 0; Index < HairTextures.Num(); ++Index)
	{
		const auto* Texture = HairTextures[Index];
		ShaderCacheSettings.setTextureUsed(Index, Texture != nullptr);
	}

	ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
		HairUpdateDynamicData,
		const NvHair::ShaderCacheSettings, ShaderCacheSettings, ShaderCacheSettings,
		{
			HairWorks::GetSDK()->addToShaderCache(ShaderCacheSettings);
		}
	);
}

void UHairWorksMaterial::SyncHairDescriptor(NvHair::InstanceDescriptor& HairDescriptor, TArray<UTexture2D*>& HairTextures, bool bFromDescriptor)
{
	HairTextures.SetNum(NvHair::ETextureType::COUNT_OF, false);

#pragma region Visualization
	auto SyncHairVisualizationFlag = [&](bool& HairFlag, bool& Property)
	{
		if(bFromDescriptor)
			Property = HairFlag;
		else
			HairFlag |= Property;
	};

	if(bFromDescriptor)
		bHair = HairDescriptor.m_drawRenderHairs;
	else
		HairDescriptor.m_drawRenderHairs &= bHair;
	SyncHairVisualizationFlag(HairDescriptor.m_visualizeBones, bBones);
	SyncHairVisualizationFlag(HairDescriptor.m_visualizeBoundingBox, bBoundingBox);
	SyncHairVisualizationFlag(HairDescriptor.m_visualizeCapsules, bCollisionCapsules);
	SyncHairVisualizationFlag(HairDescriptor.m_visualizeControlVertices, bControlPoints);
	SyncHairVisualizationFlag(HairDescriptor.m_visualizeGrowthMesh, bGrowthMesh);
	SyncHairVisualizationFlag(HairDescriptor.m_visualizeGuideHairs, bGuideCurves);
	SyncHairVisualizationFlag(HairDescriptor.m_visualizeHairInteractions, bHairInteraction);
	SyncHairVisualizationFlag(HairDescriptor.m_visualizePinConstraints, bPinConstraints);
	SyncHairVisualizationFlag(HairDescriptor.m_visualizeShadingNormals, bShadingNormal);
	SyncHairVisualizationFlag(HairDescriptor.m_visualizeShadingNormalBone, bShadingNormalCenter);
	SyncHairVisualizationFlag(HairDescriptor.m_visualizeSkinnedGuideHairs, bSkinnedGuideCurves);
	if(bFromDescriptor)
		ColorizeOptions = static_cast<EHairWorksColorizeMode>(HairDescriptor.m_colorizeMode);
	else if(HairDescriptor.m_colorizeMode == NvHair::EColorizeMode::NONE)
		HairDescriptor.m_colorizeMode = static_cast<unsigned>(ColorizeOptions);
#pragma endregion

#pragma region General
	SyncHairParameter(HairDescriptor.m_enable, bEnable, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_splineMultiplier, SplineMultiplier, bFromDescriptor);
#pragma endregion

#pragma region Physical
	SyncHairParameter(HairDescriptor.m_simulate, bSimulate, bFromDescriptor);
	FVector GravityDir = FVector(0, 0, -1);
	SyncHairParameter(HairDescriptor.m_gravityDir, (gfsdk_float3&)GravityDir, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_massScale, MassScale, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_damping, Damping, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_inertiaScale, InertiaScale, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_inertiaLimit, InertiaLimit, bFromDescriptor);
#pragma endregion

#pragma region Wind
	FVector WindVector = WindDirection.Vector() * Wind;
	SyncHairParameter(HairDescriptor.m_wind, (gfsdk_float3&)WindVector, bFromDescriptor);
	if(bFromDescriptor)
	{
		Wind = WindVector.Size();
		WindDirection = FRotator(FQuat(FRotationMatrix::MakeFromX(WindVector)));
	}
	SyncHairParameter(HairDescriptor.m_windNoise, WindNoise, bFromDescriptor);
#pragma endregion

#pragma region Stiffness
	SyncHairParameter(HairDescriptor.m_stiffness, StiffnessGlobal, bFromDescriptor);
	SyncHairParameter(HairTextures[NvHair::ETextureType::STIFFNESS], StiffnessGlobalMap, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_stiffnessCurve, (gfsdk_float4&)StiffnessGlobalCurve, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_stiffnessStrength, StiffnessStrength, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_stiffnessStrengthCurve, (gfsdk_float4&)StiffnessStrengthCurve, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_stiffnessDamping, StiffnessDamping, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_stiffnessDampingCurve, (gfsdk_float4&)StiffnessDampingCurve, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_rootStiffness, StiffnessRoot, bFromDescriptor);
	SyncHairParameter(HairTextures[NvHair::ETextureType::ROOT_STIFFNESS], StiffnessRootMap, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_tipStiffness, StiffnessTip, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_bendStiffness, StiffnessBend, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_bendStiffnessCurve, (gfsdk_float4&)StiffnessBendCurve, bFromDescriptor);
#pragma endregion

#pragma region Collision
	SyncHairParameter(HairDescriptor.m_backStopRadius, Backstop, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_friction, Friction, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_useCollision, bCapsuleCollision, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_interactionStiffness, StiffnessInteraction, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_interactionStiffnessCurve, (gfsdk_float4&)StiffnessInteractionCurve, bFromDescriptor);
#pragma endregion

//#pragma region Pin
//	SyncHairParameter(HairDescriptor.m_pinStiffness, PinStiffness, bFromDescriptor);
//#pragma endregion

#pragma region Volume
	SyncHairParameter(HairDescriptor.m_density, Density, bFromDescriptor);
	SyncHairParameter(HairTextures[NvHair::ETextureType::DENSITY], DensityMap, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_usePixelDensity, bUsePixelDensity, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_lengthScale, LengthScale, bFromDescriptor);
	SyncHairParameter(HairTextures[NvHair::ETextureType::LENGTH], LengthScaleMap, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_lengthNoise, LengthNoise, bFromDescriptor);
#pragma endregion

#pragma region Strand Width
	SyncHairParameter(HairDescriptor.m_width, WidthScale, bFromDescriptor);
	SyncHairParameter(HairTextures[NvHair::ETextureType::WIDTH], WidthScaleMap, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_widthRootScale, WidthRootScale, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_widthTipScale, WidthTipScale, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_widthNoise, WidthNoise, bFromDescriptor);
#pragma endregion

#pragma region Clumping
	SyncHairParameter(HairDescriptor.m_clumpScale, ClumpingScale, bFromDescriptor);
	SyncHairParameter(HairTextures[NvHair::ETextureType::CLUMP_SCALE], ClumpingScaleMap, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_clumpRoundness, ClumpingRoundness, bFromDescriptor);
	SyncHairParameter(HairTextures[NvHair::ETextureType::CLUMP_ROUNDNESS], ClumpingRoundnessMap, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_clumpNoise, ClumpingNoise, bFromDescriptor);
#pragma endregion

#pragma region Waveness
	SyncHairParameter(HairDescriptor.m_waveScale, WavinessScale, bFromDescriptor);
	SyncHairParameter(HairTextures[NvHair::ETextureType::WAVE_SCALE], WavinessScaleMap, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_waveScaleNoise, WavinessScaleNoise, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_waveScaleStrand, WavinessScaleStrand, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_waveScaleClump, WavinessScaleClump, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_waveFreq, WavinessFreq, bFromDescriptor);
	SyncHairParameter(HairTextures[NvHair::ETextureType::WAVE_FREQ], WavinessFreqMap, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_waveFreqNoise, WavinessFreqNoise, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_waveRootStraighten, WavinessRootStraigthen, bFromDescriptor);
#pragma endregion

#pragma region Color
	SyncHairParameter(HairDescriptor.m_rootColor, (gfsdk_float4&)RootColor, bFromDescriptor);
	SyncHairParameter(HairTextures[NvHair::ETextureType::ROOT_COLOR], RootColorMap, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_tipColor, (gfsdk_float4&)TipColor, bFromDescriptor);
	SyncHairParameter(HairTextures[NvHair::ETextureType::TIP_COLOR], TipColorMap, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_rootTipColorWeight, RootTipColorWeight, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_rootTipColorFalloff, RootTipColorFalloff, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_rootAlphaFalloff, RootAlphaFalloff, bFromDescriptor);
#pragma endregion

#pragma region Strand
	SyncHairParameter(HairTextures[NvHair::ETextureType::STRAND], PerStrandTexture, bFromDescriptor);

	switch(StrandBlendMode)
	{
	case EHairWorksStrandBlendMode::Overwrite:
		HairDescriptor.m_strandBlendMode = NvHair::EStrandBlendMode::OVERWRITE;
		break;
	case EHairWorksStrandBlendMode::Multiply:
		HairDescriptor.m_strandBlendMode = NvHair::EStrandBlendMode::MULTIPLY;
		break;
	case EHairWorksStrandBlendMode::Add:
		HairDescriptor.m_strandBlendMode = NvHair::EStrandBlendMode::ADD;
		break;
	case EHairWorksStrandBlendMode::Modulate:
		HairDescriptor.m_strandBlendMode = NvHair::EStrandBlendMode::MODULATE;
		break;
	}

	SyncHairParameter(HairDescriptor.m_strandBlendScale, StrandBlendScale, bFromDescriptor);
#pragma endregion

#pragma region Diffuse
	SyncHairParameter(HairDescriptor.m_diffuseBlend, DiffuseBlend, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_hairNormalWeight, HairNormalWeight, bFromDescriptor);
#pragma endregion

#pragma region Specular
	SyncHairParameter(HairDescriptor.m_specularColor, (gfsdk_float4&)SpecularColor, bFromDescriptor);
	SyncHairParameter(HairTextures[NvHair::ETextureType::SPECULAR], SpecularColorMap, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_specularPrimary, PrimaryScale, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_specularPowerPrimary, PrimaryShininess, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_specularPrimaryBreakup, PrimaryBreakup, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_specularSecondary, SecondaryScale, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_specularPowerSecondary, SecondaryShininess, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_specularSecondaryOffset, SecondaryOffset, bFromDescriptor);
#pragma endregion

#pragma region Glint
	SyncHairParameter(HairDescriptor.m_glintStrength, GlintStrength, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_glintCount, GlintSize, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_glintExponent, GlintPowerExponent, bFromDescriptor);
#pragma endregion

#pragma region Shadow
	SyncHairParameter(HairDescriptor.m_shadowSigma, ShadowAttenuation, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_shadowDensityScale, ShadowDensityScale, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_castShadows, bCastShadows, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_receiveShadows, bReceiveShadows, bFromDescriptor);
#pragma endregion

#pragma region Culling
	SyncHairParameter(HairDescriptor.m_useViewfrustrumCulling, bViewFrustumCulling, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_useBackfaceCulling, bBackfaceCulling, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_backfaceCullingThreshold, BackfaceCullingThreshold, bFromDescriptor);
#pragma endregion

#pragma region LOD
	if(!bFromDescriptor)
		HairDescriptor.m_enableLod = true;
#pragma endregion

#pragma region Distance LOD
	SyncHairParameter(HairDescriptor.m_enableDistanceLod, bDistanceLodEnable, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_distanceLodStart, DistanceLodStart, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_distanceLodEnd, DistanceLodEnd, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_distanceLodFadeStart, FadeStartDistance, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_distanceLodWidth, DistanceLodBaseWidthScale, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_distanceLodDensity, DistanceLodBaseDensityScale, bFromDescriptor);
#pragma endregion

#pragma region Detail LOD
	SyncHairParameter(HairDescriptor.m_enableDetailLod, bDetailLodEnable, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_detailLodStart, DetailLodStart, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_detailLodEnd, DetailLodEnd, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_detailLodWidth, DetailLodBaseWidthScale, bFromDescriptor);
	SyncHairParameter(HairDescriptor.m_detailLodDensity, DetailLodBaseDensityScale, bFromDescriptor);
#pragma endregion
}
// @third party code - END HairWorks
