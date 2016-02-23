// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Components/PrimitiveComponent.h"
#include "ShapeComponent.generated.h"

/**
 * ShapeComponent is a PrimitiveComponent that is represented by a simple geometrical shape (sphere, capsule, box, etc).
 */
UCLASS(abstract, hidecategories=(Object,LOD,Lighting,TextureStreaming,Activation,"Components|Activation"), editinlinenew, meta=(BlueprintSpawnableComponent), showcategories=(Mobility))
class ENGINE_API UShapeComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

	/** Color used to draw the shape. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Shape)
	FColor ShapeColor;

	/** Description of collision */
	UPROPERTY(transient, duplicatetransient)
	class UBodySetup* ShapeBodySetup;

	/** Only show this component if the actor is selected */
	UPROPERTY()
	uint32 bDrawOnlyIfSelected:1;

	/** If true it allows Collision when placing even if collision is not enabled*/
	UPROPERTY()
	uint32 bShouldCollideWhenPlacing:1;

	/** If set, shape will be exported for navigation as dynamic modifier instead of using regular collision data */
	UPROPERTY(EditAnywhere, Category = Navigation)
	uint32 bDynamicObstacle : 1;

	/** Navigation area type (empty = default obstacle) */
	UPROPERTY(EditAnywhere, Category = Navigation)
	TSubclassOf<class UNavArea> AreaClass;

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual class UBodySetup* GetBodySetup() override;
	virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const override;
	virtual void GetNavigationData(FNavigationRelevantData& Data) const override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin INavRelevantInterface Interface
	virtual bool IsNavigationRelevant() const override;
	//~ End INavRelevantInterface Interface

	//~ Begin USceneComponent Interface
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual bool ShouldCollideWhenPlacing() const override
	{
		return bShouldCollideWhenPlacing || IsCollisionEnabled();
	}
	//~ End USceneComponent Interface

	//~ Begin UObject Interface.
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface.

	/** Update the body setup parameters based on shape information*/
	virtual void UpdateBodySetup();

};



