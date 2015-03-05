// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "LandscapeEditorPrivatePCH.h"
#include "ObjectTools.h"
#include "LandscapeEdMode.h"
#include "ScopedTransaction.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeRender.h"
#include "LandscapeDataAccess.h"
#include "LandscapeSplineProxies.h"
#include "LandscapeEditorModule.h"
#include "Editor/PropertyEditor/Public/PropertyEditorModule.h"
#include "LandscapeEdMode.h"
#include "LandscapeEdModeTools.h"
#include "InstancedFoliageActor.h"
#include "ComponentReregisterContext.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

#define LOCTEXT_NAMESPACE "Landscape"

class FLandscapeToolStrokeSelect : public FLandscapeToolStrokeBase
{
	bool bInitializedComponentInvert;
	bool bComponentInvert;

public:
	FLandscapeToolStrokeSelect(FEdModeLandscape* InEdMode, const FLandscapeToolTarget& InTarget)
		: bInitializedComponentInvert(false)
		, LandscapeInfo(InTarget.LandscapeInfo.Get())
		, Cache(InTarget)
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FLandscapeBrush* Brush, const ULandscapeEditorObject* UISettings, const TArray<FLandscapeToolMousePosition>& MousePositions)
	{
		if (LandscapeInfo)
		{
			LandscapeInfo->Modify();

			// Invert when holding Shift
			bool bInvert = MousePositions[MousePositions.Num() - 1].bShiftDown;

			if (Brush->GetBrushType() == ELandscapeBrushType::Component)
			{
				// TODO - only retrieve bounds as we don't need the data
				const FLandscapeBrushData BrushInfo = Brush->ApplyBrush(MousePositions);
				if (!BrushInfo)
				{
					return;
				}

				int32 X1, Y1, X2, Y2;
				BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

				// Shrink bounds by 1,1 to avoid GetComponentsInRegion picking up extra components on all sides due to the overlap between components
				TSet<ULandscapeComponent*> NewComponents;
				LandscapeInfo->GetComponentsInRegion(X1 + 1, Y1 + 1, X2 - 1, Y2 - 1, NewComponents);

				if (!bInitializedComponentInvert)
				{
					// Get the component under the mouse location. Copied from FLandscapeBrushComponent::ApplyBrush()
					const float MouseX = MousePositions[0].Position.X;
					const float MouseY = MousePositions[0].Position.Y;
					const int32 MouseComponentIndexX = (MouseX >= 0.0f) ? FMath::FloorToInt(MouseX / LandscapeInfo->ComponentSizeQuads) : FMath::CeilToInt(MouseX / LandscapeInfo->ComponentSizeQuads);
					const int32 MouseComponentIndexY = (MouseY >= 0.0f) ? FMath::FloorToInt(MouseY / LandscapeInfo->ComponentSizeQuads) : FMath::CeilToInt(MouseY / LandscapeInfo->ComponentSizeQuads);
					ULandscapeComponent* MouseComponent = LandscapeInfo->XYtoComponentMap.FindRef(FIntPoint(MouseComponentIndexX, MouseComponentIndexY));

					if (MouseComponent != nullptr)
					{
						bComponentInvert = LandscapeInfo->GetSelectedComponents().Contains(MouseComponent);
					}
					else
					{
						bComponentInvert = false;
					}

					bInitializedComponentInvert = true;
				}

				bInvert = bComponentInvert;

				TSet<ULandscapeComponent*> NewSelection;
				if (bInvert)
				{
					NewSelection = LandscapeInfo->GetSelectedComponents().Difference(NewComponents);
				}
				else
				{
					NewSelection = LandscapeInfo->GetSelectedComponents().Union(NewComponents);
				}

				LandscapeInfo->Modify();
				LandscapeInfo->UpdateSelectedComponents(NewSelection);

				// Update Details tab with selection
				TArray<UObject*> Objects;
				Objects.Reset(NewSelection.Num());
				for (auto It = NewSelection.CreateConstIterator(); It; ++It)
				{
					Objects.Add(*It);
				}
				FPropertyEditorModule& PropertyModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
				PropertyModule.UpdatePropertyViews(Objects);
			}
			else // Select various shape regions
			{
				const FLandscapeBrushData BrushInfo = Brush->ApplyBrush(MousePositions);
				if (!BrushInfo)
				{
					return;
				}

				int32 X1, Y1, X2, Y2;
				BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

				// Tablet pressure
				float Pressure = ViewportClient->Viewport->IsPenActive() ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

				Cache.CacheData(X1, Y1, X2, Y2);
				TArray<uint8> Data;
				Cache.GetCachedData(X1, Y1, X2, Y2, Data);

				TSet<ULandscapeComponent*> NewComponents;
				LandscapeInfo->GetComponentsInRegion(X1, Y1, X2, Y2, NewComponents);
				LandscapeInfo->UpdateSelectedComponents(NewComponents, false);

				for (int32 Y = BrushInfo.GetBounds().Min.Y; Y < BrushInfo.GetBounds().Max.Y; Y++)
				{
					const float* BrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, Y));
					uint8* DataScanline = Data.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);

					for (int32 X = BrushInfo.GetBounds().Min.X; X < BrushInfo.GetBounds().Max.X; X++)
					{
						const FIntPoint Key = ALandscape::MakeKey(X, Y);
						const float BrushValue = BrushScanline[X];

						if (BrushValue > 0.0f && LandscapeInfo->IsValidPosition(X, Y))
						{
							float PaintValue = BrushValue * UISettings->ToolStrength * Pressure;
							float Value = DataScanline[X] / 255.0f;
							checkSlow(FMath::IsNearlyEqual(Value, LandscapeInfo->SelectedRegion.FindRef(Key), 1 / 255.0f));
							if (bInvert)
							{
								Value = FMath::Max(Value - PaintValue, 0.0f);
							}
							else
							{
								Value = FMath::Min(Value + PaintValue, 1.0f);
							}
							if (Value > 0.0f)
							{
								LandscapeInfo->SelectedRegion.Add(Key, Value);
							}
							else
							{
								LandscapeInfo->SelectedRegion.Remove(Key);
							}

							DataScanline[X] = FMath::Clamp<int32>(FMath::RoundToInt(Value * 255), 0, 255);
						}
					}
				}

				Cache.SetCachedData(X1, Y1, X2, Y2, Data);
				Cache.Flush();
			}
		}
	}

protected:
	ULandscapeInfo* LandscapeInfo;
	FLandscapeDataCache Cache;
};

// 
// FLandscapeToolSelect
//
template<class TStrokeClass>
class FLandscapeToolSelect : public FLandscapeToolBase<TStrokeClass>
{
public:
	FLandscapeToolSelect(FEdModeLandscape* InEdMode)
		: FLandscapeToolBase<TStrokeClass>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Select"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "LandscapeMode_Selection", "Component Selection"); };
	virtual void SetEditRenderType() override { GLandscapeEditRenderMode = ELandscapeEditRenderMode::SelectComponent | (GLandscapeEditRenderMode & ELandscapeEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	virtual ELandscapeToolType GetToolType() override { return ELandscapeToolType::Mask; }
};

template<class TStrokeClass>
class FLandscapeToolMask : public FLandscapeToolSelect<TStrokeClass>
{
public:
	FLandscapeToolMask(FEdModeLandscape* InEdMode)
		: FLandscapeToolSelect<TStrokeClass>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Mask"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "LandscapeMode_Mask", "Region Selection"); };
	virtual void SetEditRenderType() override { GLandscapeEditRenderMode = ELandscapeEditRenderMode::SelectRegion | (GLandscapeEditRenderMode & ELandscapeEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return true; }
};

class FLandscapeToolStrokeVisibility : public FLandscapeToolStrokeBase
{
public:
	enum { UseContinuousApply = false };

	FLandscapeToolStrokeVisibility(FEdModeLandscape* InEdMode, const FLandscapeToolTarget& InTarget)
		: LandscapeInfo(InTarget.LandscapeInfo.Get())
		, Cache(InTarget)
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FLandscapeBrush* Brush, const ULandscapeEditorObject* UISettings, const TArray<FLandscapeToolMousePosition>& MousePositions)
	{
		if (LandscapeInfo)
		{
			LandscapeInfo->Modify();
			// Get list of verts to update
			FLandscapeBrushData BrushInfo = Brush->ApplyBrush(MousePositions);
			if (!BrushInfo)
			{
				return;
			}

			int32 X1, Y1, X2, Y2;
			BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

			// Invert when holding Shift
			bool bInvert = MousePositions[MousePositions.Num() - 1].bShiftDown;

			// Tablet pressure
			float Pressure = ViewportClient->Viewport->IsPenActive() ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

			Cache.CacheData(X1, Y1, X2, Y2);
			TArray<uint8> Data;
			Cache.GetCachedData(X1, Y1, X2, Y2, Data);

			for (int32 Y = BrushInfo.GetBounds().Min.Y; Y < BrushInfo.GetBounds().Max.Y; Y++)
			{
				const float* BrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, Y));
				uint8* DataScanline = Data.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);

				for (int32 X = BrushInfo.GetBounds().Min.X; X < BrushInfo.GetBounds().Max.X; X++)
				{
					const float BrushValue = BrushScanline[X];

					if (BrushValue > 0.0f)
					{
						uint8 Value = bInvert ? 0 : 255; // Just on and off for visibility, for masking...
						DataScanline[X] = Value;
					}
				}
			}

			Cache.SetCachedData(X1, Y1, X2, Y2, Data);
			Cache.Flush();
		}
	}

protected:
	ULandscapeInfo* LandscapeInfo;
	FLandscapeVisCache Cache;
};

// 
// FLandscapeToolVisibility
//
class FLandscapeToolVisibility : public FLandscapeToolBase<FLandscapeToolStrokeVisibility>
{
public:
	FLandscapeToolVisibility(FEdModeLandscape* InEdMode)
		: FLandscapeToolBase<FLandscapeToolStrokeVisibility>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Visibility"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "LandscapeMode_Visibility", "Visibility"); };

	virtual void SetEditRenderType() override { GLandscapeEditRenderMode = ELandscapeEditRenderMode::None | (GLandscapeEditRenderMode & ELandscapeEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	virtual ELandscapeToolTargetTypeMask::Type GetSupportedTargetTypes() override
	{
		return ELandscapeToolTargetTypeMask::Visibility;
	}
};

class FLandscapeToolStrokeMoveToLevel
{
public:
	enum { UseContinuousApply = false };

	FLandscapeToolStrokeMoveToLevel(FEdModeLandscape* InEdMode, const FLandscapeToolTarget& InTarget)
		: LandscapeInfo(InTarget.LandscapeInfo.Get())
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FLandscapeBrush* Brush, const ULandscapeEditorObject* UISettings, const TArray<FLandscapeToolMousePosition>& MousePositions)
	{
		ALandscape* Landscape = LandscapeInfo ? LandscapeInfo->LandscapeActor.Get() : nullptr;

		if (Landscape)
		{
			Landscape->Modify();
			LandscapeInfo->Modify();

			TArray<UObject*> RenameObjects;
			FString MsgBoxList;

			// Check the Physical Material is same package with Landscape
			if (Landscape->DefaultPhysMaterial && Landscape->DefaultPhysMaterial->GetOutermost() == Landscape->GetOutermost())
			{
				//FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "LandscapePhyMaterial_Warning", "Landscape's DefaultPhysMaterial is in the same package as the Landscape Actor. To support streaming levels, you must move the PhysicalMaterial to another package.") );
				RenameObjects.AddUnique(Landscape->DefaultPhysMaterial);
				MsgBoxList += Landscape->DefaultPhysMaterial->GetPathName();
				MsgBoxList += FString::Printf(TEXT("\n"));
			}

			// Check the LayerInfoObjects are same package with Landscape
			for (int i = 0; i < LandscapeInfo->Layers.Num(); ++i)
			{
				ULandscapeLayerInfoObject* LayerInfo = LandscapeInfo->Layers[i].LayerInfoObj;
				if (LayerInfo && LayerInfo->GetOutermost() == Landscape->GetOutermost())
				{
					RenameObjects.AddUnique(LayerInfo);
					MsgBoxList += LayerInfo->GetPathName();
					MsgBoxList += FString::Printf(TEXT("\n"));
				}
			}

			auto SelectedComponents = LandscapeInfo->GetSelectedComponents();
			bool bBrush = false;
			if (!SelectedComponents.Num())
			{
				// Get list of verts to update
				// TODO - only retrieve bounds as we don't need the data
				FLandscapeBrushData BrushInfo = Brush->ApplyBrush(MousePositions);
				if (!BrushInfo)
				{
					return;
				}

				int32 X1, Y1, X2, Y2;
				BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

				// Shrink bounds by 1,1 to avoid GetComponentsInRegion picking up extra components on all sides due to the overlap between components
				LandscapeInfo->GetComponentsInRegion(X1 + 1, Y1 + 1, X2 - 1, Y2 - 1, SelectedComponents);
				bBrush = true;
			}

			check(ViewportClient->GetScene());
			UWorld* World = ViewportClient->GetScene()->GetWorld();
			check(World);
			if (SelectedComponents.Num())
			{
				bool bIsAllCurrentLevel = true;
				for (ULandscapeComponent* Component : SelectedComponents)
				{
					if (Component->GetLandscapeProxy()->GetLevel() != World->GetCurrentLevel())
					{
						bIsAllCurrentLevel = false;
					}
				}

				if (bIsAllCurrentLevel)
				{
					// Need to fix double WM
					if (!bBrush)
					{
						// Remove Selection
						LandscapeInfo->ClearSelectedRegion(true);
					}
					return;
				}

				for (ULandscapeComponent* Component : SelectedComponents)
				{
					UMaterialInterface* LandscapeMaterial = Component->GetLandscapeMaterial();
					if (LandscapeMaterial && LandscapeMaterial->GetOutermost() == Component->GetOutermost())
					{
						RenameObjects.AddUnique(LandscapeMaterial);
						MsgBoxList += Component->GetName() + TEXT("'s ") + LandscapeMaterial->GetPathName();
						MsgBoxList += FString::Printf(TEXT("\n"));
						//It.RemoveCurrent();
					}
				}

				if (RenameObjects.Num())
				{
					if (FMessageDialog::Open(EAppMsgType::OkCancel,
						FText::Format(
						NSLOCTEXT("UnrealEd", "LandscapeMoveToStreamingLevel_SharedResources", "The following items must be moved out of the persistent level and into a package that can be shared between multiple levels:\n\n{0}"),
						FText::FromString(MsgBoxList))))
					{
						FString Path = Landscape->GetOutermost()->GetName() + TEXT("_sharedassets/");
						bool bSucceed = ObjectTools::RenameObjects(RenameObjects, false, TEXT(""), Path);
						if (!bSucceed)
						{
							FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "LandscapeMoveToStreamingLevel_RenameFailed", "Move To Streaming Level did not succeed because shared resources could not be moved to a new package."));
							return;
						}
					}
					else
					{
						return;
					}
				}

				GWarn->BeginSlowTask(LOCTEXT("BeginMovingLandscapeComponentsToCurrentLevelTask", "Moving Landscape components to current level"), true);

				TSet<ALandscapeProxy*> SelectProxies;
				TSet<UTexture2D*> OldTextureSet;
				TSet<ULandscapeComponent*> TargetSelectedComponents;
				TArray<ULandscapeHeightfieldCollisionComponent*> TargetSelectedCollisionComponents;
				TSet<ULandscapeComponent*> HeightmapUpdateComponents;

				int32 Progress = 0;
				LandscapeInfo->SortSelectedComponents();
				int32 ComponentSizeVerts = Landscape->NumSubsections * (Landscape->SubsectionSizeQuads + 1);
				int32 NeedHeightmapSize = 1 << FMath::CeilLogTwo(ComponentSizeVerts);

				for (ULandscapeComponent* Component : SelectedComponents)
				{
					SelectProxies.Add(Component->GetLandscapeProxy());
					if (Component->GetLandscapeProxy()->GetOuter() != World->GetCurrentLevel())
					{
						TargetSelectedComponents.Add(Component);
					}

					ULandscapeHeightfieldCollisionComponent* CollisionComp = Component->CollisionComponent.Get();
					SelectProxies.Add(CollisionComp->GetLandscapeProxy());
					if (CollisionComp->GetLandscapeProxy()->GetOuter() != World->GetCurrentLevel())
					{
						TargetSelectedCollisionComponents.Add(CollisionComp);
					}
				}

				int32 TotalProgress = TargetSelectedComponents.Num() * TargetSelectedCollisionComponents.Num();

				// Check which ones are need for height map change
				for (ULandscapeComponent* Component : TargetSelectedComponents)
				{
					Component->Modify();
					OldTextureSet.Add(Component->HeightmapTexture);
				}

				// Need to split all the component which share Heightmap with selected components
				// Search neighbor only
				for (ULandscapeComponent* Component : TargetSelectedComponents)
				{
					int32 SearchX = Component->HeightmapTexture->Source.GetSizeX() / NeedHeightmapSize;
					int32 SearchY = Component->HeightmapTexture->Source.GetSizeY() / NeedHeightmapSize;
					FIntPoint ComponentBase = Component->GetSectionBase() / Component->ComponentSizeQuads;

					for (int32 Y = 0; Y < SearchY; ++Y)
					{
						for (int32 X = 0; X < SearchX; ++X)
						{
							// Search for four directions...
							for (int32 Dir = 0; Dir < 4; ++Dir)
							{
								int32 XDir = (Dir >> 1) ? 1 : -1;
								int32 YDir = (Dir % 2) ? 1 : -1;
								ULandscapeComponent* Neighbor = LandscapeInfo->XYtoComponentMap.FindRef(ComponentBase + FIntPoint(XDir*X, YDir*Y));
								if (Neighbor && Neighbor->HeightmapTexture == Component->HeightmapTexture && !HeightmapUpdateComponents.Contains(Neighbor))
								{
									Neighbor->Modify();
									if (!TargetSelectedComponents.Contains(Neighbor))
									{
										Neighbor->HeightmapScaleBias.X = -1.0f; // just mark this component is for original level, not current level
									}
									HeightmapUpdateComponents.Add(Neighbor);
								}
							}
						}
					}
				}

				// Changing Heightmap format for selected components
				for (ULandscapeComponent* Component : HeightmapUpdateComponents)
				{
					ALandscape::SplitHeightmap(Component, (Component->HeightmapScaleBias.X > 0.0f));
				}

				// Delete if it is no referenced textures...
				for (UTexture2D* Texture : OldTextureSet)
				{
					Texture->SetFlags(RF_Transactional);
					Texture->Modify();
					Texture->MarkPackageDirty();
					Texture->ClearFlags(RF_Standalone);
				}

				ALandscapeProxy* LandscapeProxy = LandscapeInfo->GetCurrentLevelLandscapeProxy(false);
				if (!LandscapeProxy)
				{
					LandscapeProxy = World->SpawnActor<ALandscapeProxy>();
					// copy shared properties to this new proxy
					LandscapeProxy->GetSharedProperties(Landscape);

					// set proxy location
					// by default first component location
					ULandscapeComponent* FirstComponent = *TargetSelectedComponents.CreateConstIterator();
					LandscapeProxy->GetRootComponent()->SetWorldLocationAndRotation(FirstComponent->GetComponentLocation(), FirstComponent->GetComponentRotation());
					LandscapeProxy->LandscapeSectionOffset = FirstComponent->GetSectionBase();

					// Hide(unregister) the new landscape if owning level currently in hidden state
					if (LandscapeProxy->GetLevel()->bIsVisible == false)
					{
						LandscapeProxy->UnregisterAllComponents();
					}
				}

				for (ALandscapeProxy* Proxy : SelectProxies)
				{
					Proxy->Modify();
				}

				LandscapeProxy->Modify();
				LandscapeProxy->MarkPackageDirty();

				// Change Weight maps...
				{
					FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
					for (ULandscapeComponent* Component : TargetSelectedComponents)
					{
						int32 TotalNeededChannels = Component->WeightmapLayerAllocations.Num();
						int32 CurrentLayer = 0;
						TArray<UTexture2D*> NewWeightmapTextures;

						// Code from ULandscapeComponent::ReallocateWeightmaps
						// Move to other channels left
						while (TotalNeededChannels > 0)
						{
							// UE_LOG(LogLandscape, Log, TEXT("Still need %d channels"), TotalNeededChannels);

							UTexture2D* CurrentWeightmapTexture = nullptr;
							FLandscapeWeightmapUsage* CurrentWeightmapUsage = nullptr;

							if (TotalNeededChannels < 4)
							{
								// UE_LOG(LogLandscape, Log, TEXT("Looking for nearest"));

								// see if we can find a suitable existing weightmap texture with sufficient channels
								int32 BestDistanceSquared = MAX_int32;
								for (auto& WeightmapUsagePair : LandscapeProxy->WeightmapUsageMap)
								{
									FLandscapeWeightmapUsage* TryWeightmapUsage = &WeightmapUsagePair.Value;
									if (TryWeightmapUsage->FreeChannelCount() >= TotalNeededChannels)
									{
										// See if this candidate is closer than any others we've found
										for (int32 ChanIdx = 0; ChanIdx < 4; ChanIdx++)
										{
											if (TryWeightmapUsage->ChannelUsage[ChanIdx] != nullptr)
											{
												int32 TryDistanceSquared = (TryWeightmapUsage->ChannelUsage[ChanIdx]->GetSectionBase() - Component->GetSectionBase()).SizeSquared();
												if (TryDistanceSquared < BestDistanceSquared)
												{
													CurrentWeightmapTexture = WeightmapUsagePair.Key;
													CurrentWeightmapUsage = TryWeightmapUsage;
													BestDistanceSquared = TryDistanceSquared;
												}
											}
										}
									}
								}
							}

							bool NeedsUpdateResource = false;
							// No suitable weightmap texture
							if (CurrentWeightmapTexture == nullptr)
							{
								Component->MarkPackageDirty();

								// Weightmap is sized the same as the component
								int32 WeightmapSize = (Component->SubsectionSizeQuads + 1) * Component->NumSubsections;

								// We need a new weightmap texture
								CurrentWeightmapTexture = LandscapeProxy->CreateLandscapeTexture(WeightmapSize, WeightmapSize, TEXTUREGROUP_Terrain_Weightmap, TSF_BGRA8);
								// Alloc dummy mips
								Component->CreateEmptyTextureMips(CurrentWeightmapTexture);
								CurrentWeightmapTexture->PostEditChange();

								// Store it in the usage map
								CurrentWeightmapUsage = &LandscapeProxy->WeightmapUsageMap.Add(CurrentWeightmapTexture, FLandscapeWeightmapUsage());

								// UE_LOG(LogLandscape, Log, TEXT("Making a new texture %s"), *CurrentWeightmapTexture->GetName());
							}

							NewWeightmapTextures.Add(CurrentWeightmapTexture);

							for (int32 ChanIdx = 0; ChanIdx < 4 && TotalNeededChannels > 0; ChanIdx++)
							{
								// UE_LOG(LogLandscape, Log, TEXT("Finding allocation for layer %d"), CurrentLayer);

								if (CurrentWeightmapUsage->ChannelUsage[ChanIdx] == nullptr)
								{
									// Use this allocation
									FWeightmapLayerAllocationInfo& AllocInfo = Component->WeightmapLayerAllocations[CurrentLayer];

									if (AllocInfo.WeightmapTextureIndex == 255)
									{
										// New layer - zero out the data for this texture channel
										LandscapeEdit.ZeroTextureChannel(CurrentWeightmapTexture, ChanIdx);
									}
									else
									{
										UTexture2D* OldWeightmapTexture = Component->WeightmapTextures[AllocInfo.WeightmapTextureIndex];

										// Copy the data
										LandscapeEdit.CopyTextureChannel(CurrentWeightmapTexture, ChanIdx, OldWeightmapTexture, AllocInfo.WeightmapTextureChannel);
										LandscapeEdit.ZeroTextureChannel(OldWeightmapTexture, AllocInfo.WeightmapTextureChannel);

										// Remove the old allocation
										FLandscapeWeightmapUsage* OldWeightmapUsage = Component->GetLandscapeProxy()->WeightmapUsageMap.Find(OldWeightmapTexture);
										OldWeightmapUsage->ChannelUsage[AllocInfo.WeightmapTextureChannel] = nullptr;
									}

									// Assign the new allocation
									CurrentWeightmapUsage->ChannelUsage[ChanIdx] = Component;
									AllocInfo.WeightmapTextureIndex = NewWeightmapTextures.Num() - 1;
									AllocInfo.WeightmapTextureChannel = ChanIdx;
									CurrentLayer++;
									TotalNeededChannels--;
								}
							}
						}

						// Replace the weightmap textures
						Component->WeightmapTextures = NewWeightmapTextures;

						// Update the mipmaps for the textures we edited
						for (UTexture2D* WeightmapTexture : Component->WeightmapTextures)
						{
							FLandscapeTextureDataInfo* WeightmapDataInfo = LandscapeEdit.GetTextureDataInfo(WeightmapTexture);

							int32 NumMips = WeightmapTexture->Source.GetNumMips();
							TArray<FColor*> WeightmapTextureMipData;
							WeightmapTextureMipData.AddUninitialized(NumMips);
							for (int32 MipIdx = 0; MipIdx < NumMips; MipIdx++)
							{
								WeightmapTextureMipData[MipIdx] = (FColor*)WeightmapDataInfo->GetMipData(MipIdx);
							}

							ULandscapeComponent::UpdateWeightmapMips(Component->NumSubsections, Component->SubsectionSizeQuads, WeightmapTexture, WeightmapTextureMipData, 0, 0, MAX_int32, MAX_int32, WeightmapDataInfo);
						}
					}
					// Need to Repacking all the Weight map (to make it packed well...)
					Landscape->RemoveInvalidWeightmaps();
				}

				// Move the components to the Proxy actor
				// This does not use the MoveSelectedActorsToCurrentLevel path as there is no support to only move certain components.
				for (ULandscapeComponent* Component :TargetSelectedComponents)
				{
					// Need to move or recreate all related data (Height map, Weight map, maybe collision components, allocation info)
					Component->GetLandscapeProxy()->LandscapeComponents.Remove(Component);
					Component->UnregisterComponent();
					Component->DetachFromParent(true);
					Component->InvalidateLightingCache();
					Component->Rename(nullptr, LandscapeProxy);
					LandscapeProxy->LandscapeComponents.Add(Component);
					Component->AttachTo(LandscapeProxy->GetRootComponent(), NAME_None, EAttachLocation::KeepWorldPosition);
					Component->UpdateMaterialInstances();

					FFormatNamedArguments Args;
					Args.Add(TEXT("ComponentName"), FText::FromString(Component->GetName()));
					GWarn->StatusUpdate(Progress++, TotalProgress, FText::Format(LOCTEXT("MovingComponentStatus", "Moving Component: {ComponentName}"), Args));
				}

				for (ULandscapeHeightfieldCollisionComponent* Component : TargetSelectedCollisionComponents)
				{
					// Need to move or recreate all related data (Height map, Weight map, maybe collision components, allocation info)

					// Move any foliage associated
					AInstancedFoliageActor::MoveInstancesForComponentToCurrentLevel(Component);

					Component->GetLandscapeProxy()->CollisionComponents.Remove(Component);
					Component->UnregisterComponent();
					Component->DetachFromParent(true);
					Component->Rename(nullptr, LandscapeProxy);
					LandscapeProxy->CollisionComponents.Add(Component);
					Component->AttachTo(LandscapeProxy->GetRootComponent(), NAME_None, EAttachLocation::KeepWorldPosition);

					FFormatNamedArguments Args;
					Args.Add(TEXT("ComponentName"), FText::FromString(Component->GetName()));
					GWarn->StatusUpdate(Progress++, TotalProgress, FText::Format(LOCTEXT("MovingComponentStatus", "Moving Component: {ComponentName}"), Args));
				}

				GEditor->SelectNone(false, true);
				GEditor->SelectActor(LandscapeProxy, true, false, true);

				GEditor->SelectNone(false, true);

				// Register our new components if destination landscape is registered in scene 
				if (LandscapeProxy->GetRootComponent()->IsRegistered())
				{
					LandscapeProxy->RegisterAllComponents();
				}

				for (ALandscapeProxy* Proxy : SelectProxies)
				{
					if (Proxy->GetRootComponent()->IsRegistered())
					{
						Proxy->RegisterAllComponents();
					}
				}

				//Landscape->bLockLocation = (LandscapeInfo->XYtoComponentMap.Num() != Landscape->LandscapeComponents.Num());

				GWarn->EndSlowTask();

				// Remove Selection
				LandscapeInfo->ClearSelectedRegion(true);

				//EdMode->SetMaskEnable(Landscape->SelectedRegion.Num());
			}
		}
	}

protected:
	ULandscapeInfo* LandscapeInfo;
};

// 
// FLandscapeToolMoveToLevel
//
class FLandscapeToolMoveToLevel : public FLandscapeToolBase<FLandscapeToolStrokeMoveToLevel>
{
public:
	FLandscapeToolMoveToLevel(FEdModeLandscape* InEdMode)
		: FLandscapeToolBase<FLandscapeToolStrokeMoveToLevel>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("MoveToLevel"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "LandscapeMode_MoveToLevel", "Move to Streaming Level"); };

	virtual void SetEditRenderType() override { GLandscapeEditRenderMode = ELandscapeEditRenderMode::SelectComponent | (GLandscapeEditRenderMode & ELandscapeEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }
};


class FLandscapeToolStrokeAddComponent : public FLandscapeToolStrokeBase
{
public:
	FLandscapeToolStrokeAddComponent(FEdModeLandscape* InEdMode, const FLandscapeToolTarget& InTarget)
		: EdMode(InEdMode)
		, LandscapeInfo(InTarget.LandscapeInfo.Get())
		, HeightCache(InTarget)
		, XYOffsetCache(InTarget)
	{
	}

	virtual ~FLandscapeToolStrokeAddComponent()
	{
		// We flush here so here ~FXYOffsetmapAccessor can safely lock the heightmap data to update bounds
		HeightCache.Flush();
		XYOffsetCache.Flush();
	}

	virtual void Apply(FEditorViewportClient* ViewportClient, FLandscapeBrush* Brush, const ULandscapeEditorObject* UISettings, const TArray<FLandscapeToolMousePosition>& MousePositions)
	{
		ALandscapeProxy* Landscape = LandscapeInfo ? LandscapeInfo->GetCurrentLevelLandscapeProxy(true) : nullptr;
		if (Landscape && EdMode->LandscapeRenderAddCollision)
		{
			check(Brush->GetBrushType() == ELandscapeBrushType::Component);

			// Get list of verts to update
			// TODO - only retrieve bounds as we don't need the data
			FLandscapeBrushData BrushInfo = Brush->ApplyBrush(MousePositions);
			if (!BrushInfo)
			{
				return;
			}

			int32 X1, Y1, X2, Y2;
			BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

			// Find component range for this block of data, non shared vertices
			int32 ComponentIndexX1, ComponentIndexY1, ComponentIndexX2, ComponentIndexY2;
			ALandscape::CalcComponentIndicesNoOverlap(X1, Y1, X2, Y2, Landscape->ComponentSizeQuads, ComponentIndexX1, ComponentIndexY1, ComponentIndexX2, ComponentIndexY2);

			// expand the area by one vertex in each direction to ensure normals are calculated correctly
			X1 -= 1;
			Y1 -= 1;
			X2 += 1;
			Y2 += 1;

			TArray<uint16> Data;
			TArray<FVector> XYOffsetData;
			HeightCache.CacheData(X1, Y1, X2, Y2);
			XYOffsetCache.CacheData(X1, Y1, X2, Y2);
			HeightCache.GetCachedData(X1, Y1, X2, Y2, Data);
			bool bHasXYOffset = XYOffsetCache.GetCachedData(X1, Y1, X2, Y2, XYOffsetData);

			TArray<ULandscapeComponent*> NewComponents;
			Landscape->Modify();
			LandscapeInfo->Modify();
			for (int32 ComponentIndexY = ComponentIndexY1; ComponentIndexY <= ComponentIndexY2; ComponentIndexY++)
			{
				for (int32 ComponentIndexX = ComponentIndexX1; ComponentIndexX <= ComponentIndexX2; ComponentIndexX++)
				{
					ULandscapeComponent* Component = LandscapeInfo->XYtoComponentMap.FindRef(FIntPoint(ComponentIndexX, ComponentIndexY));
					if (!Component)
					{
						// Add New component...
						FIntPoint ComponentBase = FIntPoint(ComponentIndexX, ComponentIndexY)*Landscape->ComponentSizeQuads;
						ULandscapeComponent* LandscapeComponent = NewObject<ULandscapeComponent>(Landscape, NAME_None, RF_Transactional);
						Landscape->LandscapeComponents.Add(LandscapeComponent);
						NewComponents.Add(LandscapeComponent);
						LandscapeComponent->Init(
							ComponentBase.X, ComponentBase.Y,
							Landscape->ComponentSizeQuads,
							Landscape->NumSubsections,
							Landscape->SubsectionSizeQuads
							);
						LandscapeComponent->AttachTo(Landscape->GetRootComponent(), NAME_None);

						// Assign shared properties
						LandscapeComponent->bCastStaticShadow = Landscape->bCastStaticShadow;
						LandscapeComponent->bCastShadowAsTwoSided = Landscape->bCastShadowAsTwoSided;

						int32 ComponentVerts = (Landscape->SubsectionSizeQuads + 1) * Landscape->NumSubsections;
						// Update Weightmap Scale Bias
						LandscapeComponent->WeightmapScaleBias = FVector4(1.0f / (float)ComponentVerts, 1.0f / (float)ComponentVerts, 0.5f / (float)ComponentVerts, 0.5f / (float)ComponentVerts);
						LandscapeComponent->WeightmapSubsectionOffset = (float)(LandscapeComponent->SubsectionSizeQuads + 1) / (float)ComponentVerts;

						TArray<FColor> HeightData;
						HeightData.Empty(FMath::Square(ComponentVerts));
						HeightData.AddZeroed(FMath::Square(ComponentVerts));
						LandscapeComponent->InitHeightmapData(HeightData, true);
						LandscapeComponent->UpdateMaterialInstances();
					}
				}
			}

			// Need to register to use general height/xyoffset data update
			for (int32 Idx = 0; Idx < NewComponents.Num(); Idx++)
			{
				NewComponents[Idx]->RegisterComponent();
			}

			if (bHasXYOffset)
			{
				XYOffsetCache.SetCachedData(X1, Y1, X2, Y2, XYOffsetData);
				XYOffsetCache.Flush();
			}

			HeightCache.SetCachedData(X1, Y1, X2, Y2, Data);
			HeightCache.Flush();

			for (int32 Idx = 0; Idx < NewComponents.Num(); Idx++)
			{
				// Update Collision
				NewComponents[Idx]->UpdateCachedBounds();
				NewComponents[Idx]->UpdateBounds();
				NewComponents[Idx]->MarkRenderStateDirty();
				ULandscapeHeightfieldCollisionComponent* CollisionComp = NewComponents[Idx]->CollisionComponent.Get();
				if (CollisionComp && !bHasXYOffset)
				{
					CollisionComp->MarkRenderStateDirty();
					CollisionComp->RecreateCollision();
				}
			}

			EdMode->LandscapeRenderAddCollision = nullptr;

			GEngine->BroadcastOnActorMoved(Landscape);
		}
	}

protected:
	FEdModeLandscape* EdMode;
	ULandscapeInfo* LandscapeInfo;
	FLandscapeHeightCache HeightCache;
	FLandscapeXYOffsetCache<true> XYOffsetCache;
};

// 
// FLandscapeToolAddComponent
//
class FLandscapeToolAddComponent : public FLandscapeToolBase<FLandscapeToolStrokeAddComponent>
{
public:
	FLandscapeToolAddComponent(FEdModeLandscape* InEdMode)
		: FLandscapeToolBase<FLandscapeToolStrokeAddComponent>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("AddComponent"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "LandscapeMode_AddComponent", "Add New Landscape Component"); };

	virtual void SetEditRenderType() override { GLandscapeEditRenderMode = ELandscapeEditRenderMode::None | (GLandscapeEditRenderMode & ELandscapeEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	virtual void ExitTool() override
	{
		FLandscapeToolBase<FLandscapeToolStrokeAddComponent>::ExitTool();

		EdMode->LandscapeRenderAddCollision = nullptr;
	}
};

class FLandscapeToolStrokeDeleteComponent : public FLandscapeToolStrokeBase
{
public:
	FLandscapeToolStrokeDeleteComponent(FEdModeLandscape* InEdMode, const FLandscapeToolTarget& InTarget)
		: LandscapeInfo(InTarget.LandscapeInfo.Get())
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FLandscapeBrush* Brush, const ULandscapeEditorObject* UISettings, const TArray<FLandscapeToolMousePosition>& MousePositions)
	{
		if (LandscapeInfo)
		{
			LandscapeInfo->Modify();

			auto SelectedComponents = LandscapeInfo->GetSelectedComponents();
			if (!SelectedComponents.Num())
			{
				// Get list of verts to update
				// TODO - only retrieve bounds as we don't need the data
				FLandscapeBrushData BrushInfo = Brush->ApplyBrush(MousePositions);
				if (!BrushInfo)
				{
					return;
				}

				int32 X1, Y1, X2, Y2;
				BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

				// Shrink bounds by 1,1 to avoid GetComponentsInRegion picking up extra components on all sides due to the overlap between components
				LandscapeInfo->GetComponentsInRegion(X1 + 1, Y1 + 1, X2 - 1, Y2 - 1, SelectedComponents);
			}

			int32 ComponentSizeVerts = LandscapeInfo->ComponentNumSubsections * (LandscapeInfo->SubsectionSizeQuads + 1);
			int32 NeedHeightmapSize = 1 << FMath::CeilLogTwo(ComponentSizeVerts);

			TSet<ULandscapeComponent*> HeightmapUpdateComponents;
			// Need to split all the component which share Heightmap with selected components
			// Search neighbor only
			for (ULandscapeComponent* Component : SelectedComponents)
			{
				int32 SearchX = Component->HeightmapTexture->Source.GetSizeX() / NeedHeightmapSize;
				int32 SearchY = Component->HeightmapTexture->Source.GetSizeY() / NeedHeightmapSize;
				FIntPoint ComponentBase = Component->GetSectionBase() / Component->ComponentSizeQuads;

				for (int32 Y = 0; Y < SearchY; ++Y)
				{
					for (int32 X = 0; X < SearchX; ++X)
					{
						// Search for four directions...
						for (int32 Dir = 0; Dir < 4; ++Dir)
						{
							int32 XDir = (Dir >> 1) ? 1 : -1;
							int32 YDir = (Dir % 2) ? 1 : -1;
							ULandscapeComponent* Neighbor = LandscapeInfo->XYtoComponentMap.FindRef(ComponentBase + FIntPoint(XDir*X, YDir*Y));
							if (Neighbor && Neighbor->HeightmapTexture == Component->HeightmapTexture && !HeightmapUpdateComponents.Contains(Neighbor))
							{
								Neighbor->Modify();
								HeightmapUpdateComponents.Add(Neighbor);
							}
						}
					}
				}
			}

			// Changing Heightmap format for selected components
			for (TSet<ULandscapeComponent*>::TConstIterator It(HeightmapUpdateComponents); It; ++It)
			{
				ULandscapeComponent* Component = *It;
				ALandscape::SplitHeightmap(Component, false);
			}

			// Remove attached foliage
			for (TSet<ULandscapeComponent*>::TIterator It(SelectedComponents); It; ++It)
			{
				ULandscapeHeightfieldCollisionComponent* CollisionComp = (*It)->CollisionComponent.Get();
				if (CollisionComp)
				{
					AInstancedFoliageActor::DeleteInstancesForComponent(ViewportClient->GetWorld(), CollisionComp);
				}
			}

			TArray<FIntPoint> DeletedNeighborKeys;
			// Check which ones are need for height map change
			for (TSet<ULandscapeComponent*>::TIterator It(SelectedComponents); It; ++It)
			{
				ULandscapeComponent* Component = *It;
				ALandscapeProxy* Proxy = Component->GetLandscapeProxy();
				Proxy->Modify();
				//Component->Modify();

				// Reset neighbors LOD information
				FIntPoint ComponentBase = Component->GetSectionBase() / Component->ComponentSizeQuads;
				FIntPoint LandscapeKey[8] =
				{
					ComponentBase + FIntPoint(-1, -1),
					ComponentBase + FIntPoint(+0, -1),
					ComponentBase + FIntPoint(+1, -1),
					ComponentBase + FIntPoint(-1, +0),
					ComponentBase + FIntPoint(+1, +0),
					ComponentBase + FIntPoint(-1, +1),
					ComponentBase + FIntPoint(+0, +1),
					ComponentBase + FIntPoint(+1, +1)
				};

				for (int32 Idx = 0; Idx < 8; ++Idx)
				{
					ULandscapeComponent* NeighborComp = LandscapeInfo->XYtoComponentMap.FindRef(LandscapeKey[Idx]);
					if (NeighborComp)
					{
						NeighborComp->Modify();
						NeighborComp->InvalidateLightingCache();
						FComponentReregisterContext ReregisterContext(NeighborComp);
					}
				}

				// Remove Selected Region in deleted Component
				for (int32 Y = 0; Y < Component->ComponentSizeQuads; ++Y)
				{
					for (int32 X = 0; X < Component->ComponentSizeQuads; ++X)
					{
						LandscapeInfo->SelectedRegion.Remove(FIntPoint(X, Y) + Component->GetSectionBase());
					}
				}

				if (Component->HeightmapTexture)
				{
					Component->HeightmapTexture->SetFlags(RF_Transactional);
					Component->HeightmapTexture->Modify();
					Component->HeightmapTexture->MarkPackageDirty();
					Component->HeightmapTexture->ClearFlags(RF_Standalone); // Remove when there is no reference for this Heightmap...
				}

				for (int32 i = 0; i < Component->WeightmapTextures.Num(); ++i)
				{
					Component->WeightmapTextures[i]->SetFlags(RF_Transactional);
					Component->WeightmapTextures[i]->Modify();
					Component->WeightmapTextures[i]->MarkPackageDirty();
					Component->WeightmapTextures[i]->ClearFlags(RF_Standalone);
				}

				if (Component->XYOffsetmapTexture)
				{
					Component->XYOffsetmapTexture->SetFlags(RF_Transactional);
					Component->XYOffsetmapTexture->Modify();
					Component->XYOffsetmapTexture->MarkPackageDirty();
					Component->XYOffsetmapTexture->ClearFlags(RF_Standalone);
				}

				FIntPoint Key = Component->GetSectionBase() / Component->ComponentSizeQuads;
				DeletedNeighborKeys.AddUnique(Key + FIntPoint(-1, -1));
				DeletedNeighborKeys.AddUnique(Key + FIntPoint(+0, -1));
				DeletedNeighborKeys.AddUnique(Key + FIntPoint(+1, -1));
				DeletedNeighborKeys.AddUnique(Key + FIntPoint(-1, +0));
				DeletedNeighborKeys.AddUnique(Key + FIntPoint(+1, +0));
				DeletedNeighborKeys.AddUnique(Key + FIntPoint(-1, +1));
				DeletedNeighborKeys.AddUnique(Key + FIntPoint(+0, +1));
				DeletedNeighborKeys.AddUnique(Key + FIntPoint(+1, +1));

				ULandscapeHeightfieldCollisionComponent* CollisionComp = Component->CollisionComponent.Get();
				if (CollisionComp)
				{
					CollisionComp->DestroyComponent();
				}
				Component->DestroyComponent();
			}

			// Update AddCollisions...
			for (int32 i = 0; i < DeletedNeighborKeys.Num(); ++i)
			{
				LandscapeInfo->XYtoAddCollisionMap.Remove(DeletedNeighborKeys[i]);
			}

			for (int32 i = 0; i < DeletedNeighborKeys.Num(); ++i)
			{
				ULandscapeComponent* Component = LandscapeInfo->XYtoComponentMap.FindRef(DeletedNeighborKeys[i]);
				if (Component)
				{
					ULandscapeHeightfieldCollisionComponent* CollisionComp = Component->CollisionComponent.Get();
					if (CollisionComp)
					{
						CollisionComp->UpdateAddCollisions();
					}
				}
			}


			// Remove Selection
			LandscapeInfo->ClearSelectedRegion(true);
			//EdMode->SetMaskEnable(Landscape->SelectedRegion.Num());
			GEngine->BroadcastLevelActorListChanged();
		}
	}

protected:
	ULandscapeInfo* LandscapeInfo;
};

// 
// FLandscapeToolDeleteComponent
//
class FLandscapeToolDeleteComponent : public FLandscapeToolBase<FLandscapeToolStrokeDeleteComponent>
{
public:
	FLandscapeToolDeleteComponent(FEdModeLandscape* InEdMode)
		: FLandscapeToolBase<FLandscapeToolStrokeDeleteComponent>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("DeleteComponent"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "LandscapeMode_DeleteComponent", "Delete Landscape Components"); };

	virtual void SetEditRenderType() override { GLandscapeEditRenderMode = ELandscapeEditRenderMode::SelectComponent | (GLandscapeEditRenderMode & ELandscapeEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }
};

template<class ToolTarget>
class FLandscapeToolStrokeCopy : public FLandscapeToolStrokeBase
{
public:
	FLandscapeToolStrokeCopy(FEdModeLandscape* InEdMode, const FLandscapeToolTarget& InTarget)
		: EdMode(InEdMode)
		, LandscapeInfo(InTarget.LandscapeInfo.Get())
		, Cache(InTarget)
		, HeightCache(InTarget)
		, WeightCache(InTarget)
	{
	}

	struct FGizmoPreData
	{
		float Ratio;
		float Data;
	};

	void Apply(FEditorViewportClient* ViewportClient, FLandscapeBrush* Brush, const ULandscapeEditorObject* UISettings, const TArray<FLandscapeToolMousePosition>& MousePositions)
	{
		//ULandscapeInfo* LandscapeInfo = EdMode->CurrentToolTarget.LandscapeInfo;
		ALandscapeGizmoActiveActor* Gizmo = EdMode->CurrentGizmoActor.Get();
		if (LandscapeInfo && Gizmo && Gizmo->GizmoTexture && Gizmo->GetRootComponent())
		{
			Gizmo->TargetLandscapeInfo = LandscapeInfo;

			// Get list of verts to update
			// TODO - only retrieve bounds as we don't need the data
			FLandscapeBrushData BrushInfo = Brush->ApplyBrush(MousePositions);
			if (!BrushInfo)
			{
				return;
			}

			int32 X1, Y1, X2, Y2;
			BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

			//Gizmo->Modify(); // No transaction for Copied data as other tools...
			//Gizmo->SelectedData.Empty();
			Gizmo->ClearGizmoData();

			// Tablet pressure
			//float Pressure = ViewportClient->Viewport->IsPenActive() ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

			bool bApplyToAll = EdMode->UISettings->bApplyToAllTargets;
			const int32 LayerNum = LandscapeInfo->Layers.Num();

			TArray<uint16> HeightData;
			TArray<uint8> WeightDatas; // Weight*Layers...
			TArray<typename ToolTarget::CacheClass::DataType> Data;

			TSet<ULandscapeLayerInfoObject*> LayerInfoSet;

			if (bApplyToAll)
			{
				HeightCache.CacheData(X1, Y1, X2, Y2);
				HeightCache.GetCachedData(X1, Y1, X2, Y2, HeightData);

				WeightCache.CacheData(X1, Y1, X2, Y2);
				WeightCache.GetCachedData(X1, Y1, X2, Y2, WeightDatas, LayerNum);
			}
			else
			{
				Cache.CacheData(X1, Y1, X2, Y2);
				Cache.GetCachedData(X1, Y1, X2, Y2, Data);
			}

			const float ScaleXY = LandscapeInfo->DrawScale.X;
			float Width = Gizmo->GetWidth();
			float Height = Gizmo->GetHeight();

			Gizmo->CachedWidth = Width;
			Gizmo->CachedHeight = Height;
			Gizmo->CachedScaleXY = ScaleXY;

			// Rasterize Gizmo regions
			int32 SizeX = FMath::CeilToInt(Width / ScaleXY);
			int32 SizeY = FMath::CeilToInt(Height / ScaleXY);

			const float W = (Width - ScaleXY) / (2 * ScaleXY);
			const float H = (Height - ScaleXY) / (2 * ScaleXY);

			FMatrix WToL = LandscapeInfo->GetLandscapeProxy()->LandscapeActorToWorld().ToMatrixWithScale().InverseFast();
			//FMatrix LToW = Landscape->LocalToWorld();

			FVector BaseLocation = WToL.TransformPosition(Gizmo->GetActorLocation());
			FMatrix GizmoLocalToLandscape = FRotationTranslationMatrix(FRotator(0, Gizmo->GetActorRotation().Yaw, 0), FVector(BaseLocation.X, BaseLocation.Y, 0));

			const int32 NeighborNum = 4;
			bool bDidCopy = false;
			bool bFullCopy = !EdMode->UISettings->bUseSelectedRegion || !LandscapeInfo->SelectedRegion.Num();
			//bool bInverted = EdMode->UISettings->bUseSelectedRegion && EdMode->UISettings->bUseNegativeMask;

			// TODO: This is a mess and badly needs refactoring
			for (int32 Y = 0; Y < SizeY; ++Y)
			{
				for (int32 X = 0; X < SizeX; ++X)
				{
					FVector LandscapeLocal = GizmoLocalToLandscape.TransformPosition(FVector(-W + X, -H + Y, 0));
					int32 LX = FMath::FloorToInt(LandscapeLocal.X);
					int32 LY = FMath::FloorToInt(LandscapeLocal.Y);

					{
						for (int32 i = -1; (!bApplyToAll && i < 0) || i < LayerNum; ++i)
						{
							// Don't try to copy data for null layers
							if ((bApplyToAll && i >= 0 && !LandscapeInfo->Layers[i].LayerInfoObj) ||
								(!bApplyToAll && !EdMode->CurrentToolTarget.LayerInfo.Get()))
							{
								continue;
							}

							FGizmoPreData GizmoPreData[NeighborNum];

							for (int32 LocalY = 0; LocalY < 2; ++LocalY)
							{
								for (int32 LocalX = 0; LocalX < 2; ++LocalX)
								{
									int32 x = FMath::Clamp(LX + LocalX, X1, X2);
									int32 y = FMath::Clamp(LY + LocalY, Y1, Y2);
									GizmoPreData[LocalX + LocalY * 2].Ratio = LandscapeInfo->SelectedRegion.FindRef(FIntPoint(x, y));
									int32 index = (x - X1) + (y - Y1)*(1 + X2 - X1);

									if (bApplyToAll)
									{
										if (i < 0)
										{
											GizmoPreData[LocalX + LocalY * 2].Data = Gizmo->GetNormalizedHeight(HeightData[index]);
										}
										else
										{
											GizmoPreData[LocalX + LocalY * 2].Data = WeightDatas[index*LayerNum + i];
										}
									}
									else
									{
										typename ToolTarget::CacheClass::DataType OriginalValue = Data[index];
										if (EdMode->CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap)
										{
											GizmoPreData[LocalX + LocalY * 2].Data = Gizmo->GetNormalizedHeight(OriginalValue);
										}
										else
										{
											GizmoPreData[LocalX + LocalY * 2].Data = OriginalValue;
										}
									}
								}
							}

							FGizmoPreData LerpedData;
							float FracX = LandscapeLocal.X - LX;
							float FracY = LandscapeLocal.Y - LY;
							LerpedData.Ratio = bFullCopy ? 1.0f :
								FMath::Lerp(
								FMath::Lerp(GizmoPreData[0].Ratio, GizmoPreData[1].Ratio, FracX),
								FMath::Lerp(GizmoPreData[2].Ratio, GizmoPreData[3].Ratio, FracX),
								FracY
								);

							LerpedData.Data = FMath::Lerp(
								FMath::Lerp(GizmoPreData[0].Data, GizmoPreData[1].Data, FracX),
								FMath::Lerp(GizmoPreData[2].Data, GizmoPreData[3].Data, FracX),
								FracY
								);

							if (!bDidCopy && LerpedData.Ratio > 0.0f)
							{
								bDidCopy = true;
							}

							if (LerpedData.Ratio > 0.0f)
							{
								// Added for LayerNames
								if (bApplyToAll)
								{
									if (i >= 0)
									{
										LayerInfoSet.Add(LandscapeInfo->Layers[i].LayerInfoObj);
									}
								}
								else
								{
									if (EdMode->CurrentToolTarget.TargetType == ELandscapeToolTargetType::Weightmap)
									{
										LayerInfoSet.Add(EdMode->CurrentToolTarget.LayerInfo.Get());
									}
								}

								FGizmoSelectData* GizmoSelectData = Gizmo->SelectedData.Find(ALandscape::MakeKey(X, Y));
								if (GizmoSelectData)
								{
									if (bApplyToAll)
									{
										if (i < 0)
										{
											GizmoSelectData->HeightData = LerpedData.Data;
										}
										else
										{
											GizmoSelectData->WeightDataMap.Add(LandscapeInfo->Layers[i].LayerInfoObj, LerpedData.Data);
										}
									}
									else
									{
										if (EdMode->CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap)
										{
											GizmoSelectData->HeightData = LerpedData.Data;
										}
										else
										{
											GizmoSelectData->WeightDataMap.Add(EdMode->CurrentToolTarget.LayerInfo.Get(), LerpedData.Data);
										}
									}
								}
								else
								{
									FGizmoSelectData NewData;
									NewData.Ratio = LerpedData.Ratio;
									if (bApplyToAll)
									{
										if (i < 0)
										{
											NewData.HeightData = LerpedData.Data;
										}
										else
										{
											NewData.WeightDataMap.Add(LandscapeInfo->Layers[i].LayerInfoObj, LerpedData.Data);
										}
									}
									else
									{
										if (EdMode->CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap)
										{
											NewData.HeightData = LerpedData.Data;
										}
										else
										{
											NewData.WeightDataMap.Add(EdMode->CurrentToolTarget.LayerInfo.Get(), LerpedData.Data);
										}
									}
									Gizmo->SelectedData.Add(ALandscape::MakeKey(X, Y), NewData);
								}
							}
						}
					}
				}
			}

			if (bDidCopy)
			{
				if (!bApplyToAll)
				{
					if (EdMode->CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap)
					{
						Gizmo->DataType = ELandscapeGizmoType(Gizmo->DataType | LGT_Height);
					}
					else
					{
						Gizmo->DataType = ELandscapeGizmoType(Gizmo->DataType | LGT_Weight);
					}
				}
				else
				{
					if (LayerNum > 0)
					{
						Gizmo->DataType = ELandscapeGizmoType(Gizmo->DataType | LGT_Height);
						Gizmo->DataType = ELandscapeGizmoType(Gizmo->DataType | LGT_Weight);
					}
					else
					{
						Gizmo->DataType = ELandscapeGizmoType(Gizmo->DataType | LGT_Height);
					}
				}

				Gizmo->SampleData(SizeX, SizeY);

				// Update LayerInfos
				for (ULandscapeLayerInfoObject* LayerInfo : LayerInfoSet)
				{
					Gizmo->LayerInfos.Add(LayerInfo);
				}
			}

			//// Clean up Ratio 0 regions... (That was for sampling...)
			//for ( TMap<uint64, FGizmoSelectData>::TIterator It(Gizmo->SelectedData); It; ++It )
			//{
			//	FGizmoSelectData& Data = It.Value();
			//	if (Data.Ratio <= 0.0f)
			//	{
			//		Gizmo->SelectedData.Remove(It.Key());
			//	}
			//}

			Gizmo->ExportToClipboard();

			GEngine->BroadcastLevelActorListChanged();
		}
	}

protected:
	FEdModeLandscape* EdMode;
	ULandscapeInfo* LandscapeInfo;
	typename ToolTarget::CacheClass Cache;
	FLandscapeHeightCache HeightCache;
	FLandscapeFullWeightCache WeightCache;
};

// 
// FLandscapeToolCopy
//
template<class ToolTarget>
class FLandscapeToolCopy : public FLandscapeToolBase<FLandscapeToolStrokeCopy<ToolTarget>>
{
public:
	FLandscapeToolCopy(FEdModeLandscape* InEdMode)
		: FLandscapeToolBase<FLandscapeToolStrokeCopy<ToolTarget> >(InEdMode)
		, BackupCurrentBrush(nullptr)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Copy"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "LandscapeMode_Copy", "Copy"); };

	virtual void SetEditRenderType() override
	{
		GLandscapeEditRenderMode = ELandscapeEditRenderMode::Gizmo | (GLandscapeEditRenderMode & ELandscapeEditRenderMode::BitMaskForMask);
		GLandscapeEditRenderMode |= (this->EdMode && this->EdMode->CurrentToolTarget.LandscapeInfo.IsValid() && this->EdMode->CurrentToolTarget.LandscapeInfo->SelectedRegion.Num()) ? ELandscapeEditRenderMode::SelectRegion : ELandscapeEditRenderMode::SelectComponent;
	}

	virtual ELandscapeToolTargetTypeMask::Type GetSupportedTargetTypes() override
	{
		return ELandscapeToolTargetTypeMask::FromType(ToolTarget::TargetType);
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FLandscapeToolTarget& InTarget, const FVector& InHitLocation) override
	{
		this->EdMode->GizmoBrush->Tick(ViewportClient, 0.1f);

		// horrible hack
		// (but avoids duplicating the code from FLandscapeToolBase)
		BackupCurrentBrush = this->EdMode->CurrentBrush;
		this->EdMode->CurrentBrush = this->EdMode->GizmoBrush;

		return FLandscapeToolBase<FLandscapeToolStrokeCopy<ToolTarget>>::BeginTool(ViewportClient, InTarget, InHitLocation);
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient) override
	{
		FLandscapeToolBase<FLandscapeToolStrokeCopy<ToolTarget>>::EndTool(ViewportClient);

		this->EdMode->CurrentBrush = BackupCurrentBrush;
	}

protected:
	FLandscapeBrush* BackupCurrentBrush;
};

template<class ToolTarget>
class FLandscapeToolStrokePaste : public FLandscapeToolStrokeBase
{
public:
	FLandscapeToolStrokePaste(FEdModeLandscape* InEdMode, const FLandscapeToolTarget& InTarget)
		: EdMode(InEdMode)
		, LandscapeInfo(InTarget.LandscapeInfo.Get())
		, Cache(InTarget)
		, HeightCache(InTarget)
		, WeightCache(InTarget)
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FLandscapeBrush* Brush, const ULandscapeEditorObject* UISettings, const TArray<FLandscapeToolMousePosition>& MousePositions)
	{
		//ULandscapeInfo* LandscapeInfo = EdMode->CurrentToolTarget.LandscapeInfo;
		ALandscapeGizmoActiveActor* Gizmo = EdMode->CurrentGizmoActor.Get();
		// Cache and copy in Gizmo's region...
		if (LandscapeInfo && Gizmo && Gizmo->GetRootComponent())
		{
			if (Gizmo->SelectedData.Num() == 0)
			{
				return;
			}

			// Automatically fill in any placeholder layers
			// This gives a much better user experience when copying data to a newly created landscape
			for (ULandscapeLayerInfoObject* LayerInfo : Gizmo->LayerInfos)
			{
				int32 LayerInfoIndex = LandscapeInfo->GetLayerInfoIndex(LayerInfo);
				if (LayerInfoIndex == INDEX_NONE)
				{
					LayerInfoIndex = LandscapeInfo->GetLayerInfoIndex(LayerInfo->LayerName);
					if (LayerInfoIndex != INDEX_NONE)
					{
						FLandscapeInfoLayerSettings& LayerSettings = LandscapeInfo->Layers[LayerInfoIndex];

						if (LayerSettings.LayerInfoObj == nullptr)
						{
							LayerSettings.Owner = LandscapeInfo->GetLandscapeProxy(); // this isn't strictly accurate, but close enough
							LayerSettings.LayerInfoObj = LayerInfo;
							LayerSettings.bValid = true;
						}
					}
				}
			}

			Gizmo->TargetLandscapeInfo = LandscapeInfo;
			float ScaleXY = LandscapeInfo->DrawScale.X;

			//LandscapeInfo->Modify();

			// Get list of verts to update
			FLandscapeBrushData BrushInfo = Brush->ApplyBrush(MousePositions);
			if (!BrushInfo)
			{
				return;
			}

			int32 X1, Y1, X2, Y2;
			BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

			// Tablet pressure
			float Pressure = (ViewportClient && ViewportClient->Viewport->IsPenActive()) ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

			// expand the area by one vertex in each direction to ensure normals are calculated correctly
			X1 -= 1;
			Y1 -= 1;
			X2 += 1;
			Y2 += 1;

			const bool bApplyToAll = EdMode->UISettings->bApplyToAllTargets;
			const int32 LayerNum = Gizmo->LayerInfos.Num() > 0 ? LandscapeInfo->Layers.Num() : 0;

			TArray<uint16> HeightData;
			TArray<uint8> WeightDatas; // Weight*Layers...
			TArray<typename ToolTarget::CacheClass::DataType> Data;

			if (bApplyToAll)
			{
				HeightCache.CacheData(X1, Y1, X2, Y2);
				HeightCache.GetCachedData(X1, Y1, X2, Y2, HeightData);

				if (LayerNum > 0)
				{
					WeightCache.CacheData(X1, Y1, X2, Y2);
					WeightCache.GetCachedData(X1, Y1, X2, Y2, WeightDatas, LayerNum);
				}
			}
			else
			{
				Cache.CacheData(X1, Y1, X2, Y2);
				Cache.GetCachedData(X1, Y1, X2, Y2, Data);
			}

			const float Width = Gizmo->GetWidth();
			const float Height = Gizmo->GetHeight();

			const float W = Gizmo->GetWidth() / (2 * ScaleXY);
			const float H = Gizmo->GetHeight() / (2 * ScaleXY);

			const float SignX = Gizmo->GetRootComponent()->RelativeScale3D.X > 0.0f ? 1.0f : -1.0f;
			const float SignY = Gizmo->GetRootComponent()->RelativeScale3D.Y > 0.0f ? 1.0f : -1.0f;

			const float ScaleX = Gizmo->CachedWidth / Width * ScaleXY / Gizmo->CachedScaleXY;
			const float ScaleY = Gizmo->CachedHeight / Height * ScaleXY / Gizmo->CachedScaleXY;

			FMatrix WToL = LandscapeInfo->GetLandscapeProxy()->LandscapeActorToWorld().ToMatrixWithScale().InverseFast();
			//FMatrix LToW = Landscape->LocalToWorld();
			FVector BaseLocation = WToL.TransformPosition(Gizmo->GetActorLocation());
			//FMatrix LandscapeLocalToGizmo = FRotationTranslationMatrix(FRotator(0, Gizmo->Rotation.Yaw, 0), FVector(BaseLocation.X - W + 0.5, BaseLocation.Y - H + 0.5, 0));
			FMatrix LandscapeToGizmoLocal =
				(FTranslationMatrix(FVector((-W + 0.5)*SignX, (-H + 0.5)*SignY, 0)) * FScaleRotationTranslationMatrix(FVector(SignX, SignY, 1.0f), FRotator(0, Gizmo->GetActorRotation().Yaw, 0), FVector(BaseLocation.X, BaseLocation.Y, 0))).InverseFast();

			for (int32 Y = BrushInfo.GetBounds().Min.Y; Y < BrushInfo.GetBounds().Max.Y; Y++)
			{
				const float* BrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, Y));

				for (int32 X = BrushInfo.GetBounds().Min.X; X < BrushInfo.GetBounds().Max.X; X++)
				{
					const float BrushValue = BrushScanline[X];

					if (BrushValue > 0.0f)
					{
						// TODO: This is a mess and badly needs refactoring

						// Value before we apply our painting
						int32 index = (X - X1) + (Y - Y1)*(1 + X2 - X1);
						float PaintAmount = (Brush->GetBrushType() == ELandscapeBrushType::Gizmo) ? BrushValue : BrushValue * EdMode->UISettings->ToolStrength * Pressure;

						FVector GizmoLocal = LandscapeToGizmoLocal.TransformPosition(FVector(X, Y, 0));
						GizmoLocal.X *= ScaleX * SignX;
						GizmoLocal.Y *= ScaleY * SignY;

						int32 LX = FMath::FloorToInt(GizmoLocal.X);
						int32 LY = FMath::FloorToInt(GizmoLocal.Y);

						float FracX = GizmoLocal.X - LX;
						float FracY = GizmoLocal.Y - LY;

						FGizmoSelectData* Data00 = Gizmo->SelectedData.Find(ALandscape::MakeKey(LX, LY));
						FGizmoSelectData* Data10 = Gizmo->SelectedData.Find(ALandscape::MakeKey(LX + 1, LY));
						FGizmoSelectData* Data01 = Gizmo->SelectedData.Find(ALandscape::MakeKey(LX, LY + 1));
						FGizmoSelectData* Data11 = Gizmo->SelectedData.Find(ALandscape::MakeKey(LX + 1, LY + 1));

						for (int32 i = -1; (!bApplyToAll && i < 0) || i < LayerNum; ++i)
						{
							if ((bApplyToAll && (i < 0)) || (!bApplyToAll && EdMode->CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap))
							{
								float OriginalValue;
								if (bApplyToAll)
								{
									OriginalValue = HeightData[index];
								}
								else
								{
									OriginalValue = Data[index];
								}

								float Value = LandscapeDataAccess::GetLocalHeight(OriginalValue);

								float DestValue = FLandscapeHeightCache::ClampValue(
									LandscapeDataAccess::GetTexHeight(
									FMath::Lerp(
									FMath::Lerp(Data00 ? FMath::Lerp(Value, Gizmo->GetLandscapeHeight(Data00->HeightData), Data00->Ratio) : Value,
									Data10 ? FMath::Lerp(Value, Gizmo->GetLandscapeHeight(Data10->HeightData), Data10->Ratio) : Value, FracX),
									FMath::Lerp(Data01 ? FMath::Lerp(Value, Gizmo->GetLandscapeHeight(Data01->HeightData), Data01->Ratio) : Value,
									Data11 ? FMath::Lerp(Value, Gizmo->GetLandscapeHeight(Data11->HeightData), Data11->Ratio) : Value, FracX),
									FracY
									))
									);

								switch (EdMode->UISettings->PasteMode)
								{
								case ELandscapeToolNoiseMode::Add:
									PaintAmount = OriginalValue < DestValue ? PaintAmount : 0.0f;
									break;
								case ELandscapeToolNoiseMode::Sub:
									PaintAmount = OriginalValue > DestValue ? PaintAmount : 0.0f;
									break;
								default:
									break;
								}

								if (bApplyToAll)
								{
									HeightData[index] = FMath::Lerp(OriginalValue, DestValue, PaintAmount);
								}
								else
								{
									Data[index] = FMath::Lerp(OriginalValue, DestValue, PaintAmount);
								}
							}
							else
							{
								ULandscapeLayerInfoObject* LayerInfo;
								float OriginalValue;
								if (bApplyToAll)
								{
									LayerInfo = LandscapeInfo->Layers[i].LayerInfoObj;
									OriginalValue = WeightDatas[index*LayerNum + i];
								}
								else
								{
									LayerInfo = EdMode->CurrentToolTarget.LayerInfo.Get();
									OriginalValue = Data[index];
								}

								float DestValue = FLandscapeAlphaCache::ClampValue(
									FMath::Lerp(
									FMath::Lerp(Data00 ? FMath::Lerp(OriginalValue, Data00->WeightDataMap.FindRef(LayerInfo), Data00->Ratio) : OriginalValue,
									Data10 ? FMath::Lerp(OriginalValue, Data10->WeightDataMap.FindRef(LayerInfo), Data10->Ratio) : OriginalValue, FracX),
									FMath::Lerp(Data01 ? FMath::Lerp(OriginalValue, Data01->WeightDataMap.FindRef(LayerInfo), Data01->Ratio) : OriginalValue,
									Data11 ? FMath::Lerp(OriginalValue, Data11->WeightDataMap.FindRef(LayerInfo), Data11->Ratio) : OriginalValue, FracX),
									FracY
									));

								if (bApplyToAll)
								{
									WeightDatas[index*LayerNum + i] = FMath::Lerp(OriginalValue, DestValue, PaintAmount);
								}
								else
								{
									Data[index] = FMath::Lerp(OriginalValue, DestValue, PaintAmount);
								}
							}
						}
					}
				}
			}

			for (ULandscapeLayerInfoObject* LayerInfo : Gizmo->LayerInfos)
			{
				if (LandscapeInfo->GetLayerInfoIndex(LayerInfo) != INDEX_NONE)
				{
					WeightCache.AddDirtyLayer(LayerInfo);
				}
			}

			if (bApplyToAll)
			{
				HeightCache.SetCachedData(X1, Y1, X2, Y2, HeightData);
				HeightCache.Flush();
				if (WeightDatas.Num())
				{
					// Set the layer data, bypassing painting restrictions because it doesn't work well when altering multiple layers
					WeightCache.SetCachedData(X1, Y1, X2, Y2, WeightDatas, LayerNum, ELandscapeLayerPaintingRestriction::None);
				}
				WeightCache.Flush();
			}
			else
			{
				Cache.SetCachedData(X1, Y1, X2, Y2, Data);
				Cache.Flush();
			}

			GEngine->BroadcastLevelActorListChanged();
		}
	}

protected:
	FEdModeLandscape* EdMode;
	ULandscapeInfo* LandscapeInfo;
	typename ToolTarget::CacheClass Cache;
	FLandscapeHeightCache HeightCache;
	FLandscapeFullWeightCache WeightCache;
};

// 
// FLandscapeToolPaste
//
template<class ToolTarget>
class FLandscapeToolPaste : public FLandscapeToolBase<FLandscapeToolStrokePaste<ToolTarget>>
{
public:
	FLandscapeToolPaste(FEdModeLandscape* InEdMode)
		: FLandscapeToolBase<FLandscapeToolStrokePaste<ToolTarget>>(InEdMode)
		, bUseGizmoRegion(false)
		, BackupCurrentBrush(nullptr)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Paste"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "LandscapeMode_Region", "Region Copy/Paste"); };

	virtual void SetEditRenderType() override
	{
		GLandscapeEditRenderMode = ELandscapeEditRenderMode::Gizmo | (GLandscapeEditRenderMode & ELandscapeEditRenderMode::BitMaskForMask);
		GLandscapeEditRenderMode |= (this->EdMode && this->EdMode->CurrentToolTarget.LandscapeInfo.IsValid() && this->EdMode->CurrentToolTarget.LandscapeInfo->SelectedRegion.Num()) ? ELandscapeEditRenderMode::SelectRegion : ELandscapeEditRenderMode::SelectComponent;
	}

	void SetGizmoMode(bool InbUseGizmoRegion)
	{
		bUseGizmoRegion = InbUseGizmoRegion;
	}

	virtual ELandscapeToolTargetTypeMask::Type GetSupportedTargetTypes() override
	{
		return ELandscapeToolTargetTypeMask::FromType(ToolTarget::TargetType);
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FLandscapeToolTarget& InTarget, const FVector& InHitLocation) override
	{
		this->EdMode->GizmoBrush->Tick(ViewportClient, 0.1f);

		// horrible hack
		// (but avoids duplicating the code from FLandscapeToolBase)
		BackupCurrentBrush = this->EdMode->CurrentBrush;
		if (bUseGizmoRegion)
		{
			this->EdMode->CurrentBrush = this->EdMode->GizmoBrush;
		}

		return FLandscapeToolBase<FLandscapeToolStrokePaste<ToolTarget>>::BeginTool(ViewportClient, InTarget, InHitLocation);
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient) override
	{
		FLandscapeToolBase<FLandscapeToolStrokePaste<ToolTarget>>::EndTool(ViewportClient);

		if (bUseGizmoRegion)
		{
			this->EdMode->CurrentBrush = BackupCurrentBrush;
		}
		check(this->EdMode->CurrentBrush == BackupCurrentBrush);
	}

	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override
	{
		if (bUseGizmoRegion)
		{
			return true;
		}

		return FLandscapeToolBase<FLandscapeToolStrokePaste<ToolTarget>>::MouseMove(ViewportClient, Viewport, x, y);
	}

protected:
	bool bUseGizmoRegion;
	FLandscapeBrush* BackupCurrentBrush;
};

template<class ToolTarget>
class FLandscapeToolCopyPaste : public FLandscapeToolPaste<ToolTarget>
{
public:
	FLandscapeToolCopyPaste(FEdModeLandscape* InEdMode)
		: FLandscapeToolPaste<ToolTarget>(InEdMode)
		, CopyTool(InEdMode)
	{
	}

	// Just hybrid of Copy and Paste tool
	virtual const TCHAR* GetToolName() override { return TEXT("CopyPaste"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "LandscapeMode_Region", "Region Copy/Paste"); };

	virtual void EnterTool()
	{
		// Make sure gizmo actor is selected
		ALandscapeGizmoActiveActor* Gizmo = this->EdMode->CurrentGizmoActor.Get();
		if (Gizmo)
		{
			GEditor->SelectNone(false, true);
			GEditor->SelectActor(Gizmo, true, false, true);
		}
	}

	// Copy tool doesn't use any view information, so just do it as one function
	void Copy()
	{
		CopyTool.BeginTool(nullptr, this->EdMode->CurrentToolTarget, FVector::ZeroVector);
		CopyTool.EndTool(nullptr);
	}

	void Paste()
	{
		this->SetGizmoMode(true);
		this->BeginTool(nullptr, this->EdMode->CurrentToolTarget, FVector::ZeroVector);
		this->EndTool(nullptr);
		this->SetGizmoMode(false);
	}

protected:
	FLandscapeToolCopy<ToolTarget> CopyTool;
};

void FEdModeLandscape::CopyDataToGizmo()
{
	// For Copy operation...
	if (CopyPasteTool /*&& CopyPasteTool == CurrentTool*/)
	{
		CopyPasteTool->Copy();
	}
	if (CurrentGizmoActor.IsValid())
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(CurrentGizmoActor.Get(), true, true, true);
	}
}

void FEdModeLandscape::PasteDataFromGizmo()
{
	// For Paste for Gizmo Region operation...
	if (CopyPasteTool /*&& CopyPasteTool == CurrentTool*/)
	{
		CopyPasteTool->Paste();
	}
	if (CurrentGizmoActor.IsValid())
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(CurrentGizmoActor.Get(), true, true, true);
	}
}

// 
// FLandscapeToolNewLandscape
//
class FLandscapeToolNewLandscape : public FLandscapeTool
{
public:
	FEdModeLandscape* EdMode;
	ENewLandscapePreviewMode::Type NewLandscapePreviewMode;

	FLandscapeToolNewLandscape(FEdModeLandscape* InEdMode)
		: FLandscapeTool()
		, EdMode(InEdMode)
		, NewLandscapePreviewMode(ENewLandscapePreviewMode::NewLandscape)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("NewLandscape"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "LandscapeMode_NewLandscape", "New Landscape"); };

	virtual void SetEditRenderType() override { GLandscapeEditRenderMode = ELandscapeEditRenderMode::None | (GLandscapeEditRenderMode & ELandscapeEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	virtual void EnterTool()
	{
		EdMode->NewLandscapePreviewMode = NewLandscapePreviewMode;
	}

	virtual void ExitTool()
	{
		NewLandscapePreviewMode = EdMode->NewLandscapePreviewMode;
		EdMode->NewLandscapePreviewMode = ENewLandscapePreviewMode::None;
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FLandscapeToolTarget& Target, const FVector& InHitLocation)
	{
		// does nothing
		return false;
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient)
	{
		// does nothing
	}

	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
	{
		// does nothing
		return false;
	}
};


// 
// FLandscapeToolResizeLandscape
//
class FLandscapeToolResizeLandscape : public FLandscapeTool
{
public:
	FEdModeLandscape* EdMode;

	FLandscapeToolResizeLandscape(FEdModeLandscape* InEdMode)
		: FLandscapeTool()
		, EdMode(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("ResizeLandscape"); }
	virtual FText GetDisplayName() override { return LOCTEXT("LandscapeMode_ResizeLandscape", "Change Landscape Component Size"); };

	virtual void SetEditRenderType() override { GLandscapeEditRenderMode = ELandscapeEditRenderMode::None | (GLandscapeEditRenderMode & ELandscapeEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	virtual void EnterTool()
	{
		const int32 ComponentSizeQuads = EdMode->CurrentToolTarget.LandscapeInfo->ComponentSizeQuads;
		int32 MinX, MinY, MaxX, MaxY;
		if (EdMode->CurrentToolTarget.LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
		{
			EdMode->UISettings->ResizeLandscape_Original_ComponentCount.X = (MaxX - MinX) / ComponentSizeQuads;
			EdMode->UISettings->ResizeLandscape_Original_ComponentCount.Y = (MaxY - MinY) / ComponentSizeQuads;
			EdMode->UISettings->ResizeLandscape_ComponentCount = EdMode->UISettings->ResizeLandscape_Original_ComponentCount;
		}
		else
		{
			EdMode->UISettings->ResizeLandscape_Original_ComponentCount = FIntPoint::ZeroValue;
			EdMode->UISettings->ResizeLandscape_ComponentCount = FIntPoint::ZeroValue;
		}
		EdMode->UISettings->ResizeLandscape_Original_QuadsPerSection = EdMode->CurrentToolTarget.LandscapeInfo->SubsectionSizeQuads;
		EdMode->UISettings->ResizeLandscape_Original_SectionsPerComponent = EdMode->CurrentToolTarget.LandscapeInfo->ComponentNumSubsections;
		EdMode->UISettings->ResizeLandscape_QuadsPerSection = EdMode->UISettings->ResizeLandscape_Original_QuadsPerSection;
		EdMode->UISettings->ResizeLandscape_SectionsPerComponent = EdMode->UISettings->ResizeLandscape_Original_SectionsPerComponent;
	}

	virtual void ExitTool()
	{
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FLandscapeToolTarget& Target, const FVector& InHitLocation)
	{
		// does nothing
		return false;
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient)
	{
		// does nothing
	}

	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
	{
		// does nothing
		return false;
	}
};

//////////////////////////////////////////////////////////////////////////

void FEdModeLandscape::InitializeTool_NewLandscape()
{
	auto Tool_NewLandscape = MakeUnique<FLandscapeToolNewLandscape>(this);
	Tool_NewLandscape->ValidBrushes.Add("BrushSet_Dummy");
	LandscapeTools.Add(MoveTemp(Tool_NewLandscape));
}

void FEdModeLandscape::InitializeTool_ResizeLandscape()
{
	auto Tool_ResizeLandscape = MakeUnique<FLandscapeToolResizeLandscape>(this);
	Tool_ResizeLandscape->ValidBrushes.Add("BrushSet_Dummy");
	LandscapeTools.Add(MoveTemp(Tool_ResizeLandscape));
}

void FEdModeLandscape::InitializeTool_Select()
{
	auto Tool_Select = MakeUnique<FLandscapeToolSelect<FLandscapeToolStrokeSelect>>(this);
	Tool_Select->ValidBrushes.Add("BrushSet_Component");
	LandscapeTools.Add(MoveTemp(Tool_Select));
}

void FEdModeLandscape::InitializeTool_AddComponent()
{
	auto Tool_AddComponent = MakeUnique<FLandscapeToolAddComponent>(this);
	Tool_AddComponent->ValidBrushes.Add("BrushSet_Component");
	LandscapeTools.Add(MoveTemp(Tool_AddComponent));
}

void FEdModeLandscape::InitializeTool_DeleteComponent()
{
	auto Tool_DeleteComponent = MakeUnique<FLandscapeToolDeleteComponent>(this);
	Tool_DeleteComponent->ValidBrushes.Add("BrushSet_Component");
	LandscapeTools.Add(MoveTemp(Tool_DeleteComponent));
}

void FEdModeLandscape::InitializeTool_MoveToLevel()
{
	auto Tool_MoveToLevel = MakeUnique<FLandscapeToolMoveToLevel>(this);
	Tool_MoveToLevel->ValidBrushes.Add("BrushSet_Component");
	LandscapeTools.Add(MoveTemp(Tool_MoveToLevel));
}

void FEdModeLandscape::InitializeTool_Mask()
{
	auto Tool_Mask = MakeUnique<FLandscapeToolMask<FLandscapeToolStrokeSelect>>(this);
	Tool_Mask->ValidBrushes.Add("BrushSet_Circle");
	Tool_Mask->ValidBrushes.Add("BrushSet_Alpha");
	Tool_Mask->ValidBrushes.Add("BrushSet_Pattern");
	LandscapeTools.Add(MoveTemp(Tool_Mask));
}

void FEdModeLandscape::InitializeTool_CopyPaste()
{
	auto Tool_CopyPaste_Heightmap = MakeUnique<FLandscapeToolCopyPaste<FHeightmapToolTarget>>(this);
	Tool_CopyPaste_Heightmap->ValidBrushes.Add("BrushSet_Circle");
	Tool_CopyPaste_Heightmap->ValidBrushes.Add("BrushSet_Alpha");
	Tool_CopyPaste_Heightmap->ValidBrushes.Add("BrushSet_Pattern");
	Tool_CopyPaste_Heightmap->ValidBrushes.Add("BrushSet_Gizmo");
	CopyPasteTool = Tool_CopyPaste_Heightmap.Get();
	LandscapeTools.Add(MoveTemp(Tool_CopyPaste_Heightmap));

	//auto Tool_CopyPaste_Weightmap = MakeUnique<FLandscapeToolCopyPaste<FWeightmapToolTarget>>(this);
	//Tool_CopyPaste_Weightmap->ValidBrushes.Add("BrushSet_Circle");
	//Tool_CopyPaste_Weightmap->ValidBrushes.Add("BrushSet_Alpha");
	//Tool_CopyPaste_Weightmap->ValidBrushes.Add("BrushSet_Pattern");
	//Tool_CopyPaste_Weightmap->ValidBrushes.Add("BrushSet_Gizmo");
	//LandscapeTools.Add(MoveTemp(Tool_CopyPaste_Weightmap));
}

void FEdModeLandscape::InitializeTool_Visibility()
{
	auto Tool_Visibility = MakeUnique<FLandscapeToolVisibility>(this);
	Tool_Visibility->ValidBrushes.Add("BrushSet_Circle");
	Tool_Visibility->ValidBrushes.Add("BrushSet_Alpha");
	Tool_Visibility->ValidBrushes.Add("BrushSet_Pattern");
	LandscapeTools.Add(MoveTemp(Tool_Visibility));
}

#undef LOCTEXT_NAMESPACE