// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"


UFbxSceneImportOptionsMaterial::UFbxSceneImportOptionsMaterial(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bImportMaterials(true)
	, bImportTextures(true)
	, bInvertNormalMaps(false)
{
}

