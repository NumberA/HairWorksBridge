// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompositeFont.h"
#include "FontFaceInterface.generated.h"

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UFontFaceInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class IFontFaceInterface
{
	GENERATED_IINTERFACE_BODY()

#if WITH_EDITORONLY_DATA
	/** Initialize this font face from legacy bulk data. */
	virtual void InitializeFromBulkData(const FString& InFilename, const EFontHinting InHinting, const void* InBulkDataPtr, const int32 InBulkDataSizeBytes) = 0;
#endif // WITH_EDITORONLY_DATA

	/** Get the filename of the font to use. This may not actually exist on disk in editor builds and we should load the face buffer instead. */
	virtual const FString& GetFontFilename() const = 0;

	/** Get the hinting algorithm to use with the font. */
	virtual EFontHinting GetHinting() const = 0;

	/** Get the enum controlling how this font should be loaded at runtime. */
	virtual EFontLoadingPolicy GetLoadingPolicy() const = 0;

#if WITH_EDITORONLY_DATA
	/** Get the data buffer containing the data for the current font face. */
	virtual const TArray<uint8>& GetFontFaceData() const = 0;
#endif // WITH_EDITORONLY_DATA

	/** Get the filename of the font to use when cooked. This file will be generated during cook. */
	virtual FString GetCookedFilename() const = 0;
};
