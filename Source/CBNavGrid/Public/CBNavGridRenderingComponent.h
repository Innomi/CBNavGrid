#pragma once

#include "CoreMinimal.h"
#include "CBNavGridRenderingComponent.generated.h"

UCLASS(EditInLineNew, ClassGroup = Debug)
class CBNAVGRID_API UCBNavGridRenderingComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UCBNavGridRenderingComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	void RequestDrawingUpdate();

protected:
	virtual FBoxSphereBounds CalcBounds(FTransform const & LocalToWorld) const override;
	virtual FPrimitiveSceneProxy * CreateSceneProxy() override;

	void UpdateDrawing();
	FTimerManager * GetTimerManager() const;

	FTimerHandle TimerHandle;
	uint8 bDrawingUpdateRequested : 1 = false;
};
