// @third party code - BEGIN HairWorks
#pragma once

#include "HairWorksFactory.generated.h"

enum GFSDK_HairAssetID;
struct GFSDK_HairInstanceDescriptor;
class UHairWorksAsset;

UCLASS()
class UHairWorksFactory : public UFactory, public FReimportHandler
{
	GENERATED_UCLASS_BODY()

	//Begin UFactory Interface
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateBinary(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn) override;
	//End UFactory Interface

	//Begin FReimportHandler Interface
	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
	virtual EReimportResult::Type Reimport(UObject* Obj) override;
	//End FReimportHandler Interface

protected:
	static void InitHairAssetInfo(UHairWorksAsset& Hair, GFSDK_HairAssetID HairAssetId, const GFSDK_HairInstanceDescriptor* NewInstanceDesc = nullptr);
};
// @third party code - END HairWorks
