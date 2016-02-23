// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.


#include "EnginePrivate.h"

/** Returns true if the Material and Vertex Factory combination require adjacency information. */
bool RequiresAdjacencyInformation( UMaterialInterface* Material, const FVertexFactoryType* VertexFactoryType, ERHIFeatureLevel::Type InFeatureLevel )
{
	EMaterialTessellationMode TessellationMode = MTM_NoTessellation;
	bool bEnableCrackFreeDisplacement = false;
	if ( RHISupportsTessellation(GShaderPlatformForFeatureLevel[InFeatureLevel]) && VertexFactoryType->SupportsTessellationShaders() && Material )
	{
		if ( IsInRenderingThread() )
		{
			FMaterialRenderProxy* MaterialRenderProxy = Material->GetRenderProxy( false, false );
			if (ensureMsgf( MaterialRenderProxy, TEXT("Could not determine if RequiresAdjacencyInformation. Invalid MaterialRenderProxy on Material '%s'"), *GetNameSafe(Material) ))
			{
				const FMaterial* MaterialResource = MaterialRenderProxy->GetMaterial(InFeatureLevel);
				if (ensureMsgf( MaterialResource, TEXT("Could not determine if RequiresAdjacencyInformation. Invalid MaterialResource on Material '%s'"), *GetNameSafe(Material) ))
				{
					TessellationMode = MaterialResource->GetTessellationMode();
					bEnableCrackFreeDisplacement = MaterialResource->IsCrackFreeDisplacementEnabled();
				}
			}
		}
		else if ( IsInGameThread() )
		{
			UMaterial* BaseMaterial = Material->GetMaterial();
			check( BaseMaterial );
			TessellationMode = (EMaterialTessellationMode)BaseMaterial->D3D11TessellationMode;
			bEnableCrackFreeDisplacement = BaseMaterial->bEnableCrackFreeDisplacement;
		}
		else
		{
			UMaterialInterface::TMicRecursionGuard RecursionGuard;
			const UMaterial* BaseMaterial = Material->GetMaterial_Concurrent(RecursionGuard);
			check( BaseMaterial );
			TessellationMode = (EMaterialTessellationMode)BaseMaterial->D3D11TessellationMode;
			bEnableCrackFreeDisplacement = BaseMaterial->bEnableCrackFreeDisplacement;
		}
	}

	return TessellationMode == MTM_PNTriangles || ( TessellationMode == MTM_FlatTessellation && bEnableCrackFreeDisplacement );
}
