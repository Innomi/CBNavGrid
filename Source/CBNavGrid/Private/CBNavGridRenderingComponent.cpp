#include "CBNavGridRenderingComponent.h"
#include "CBNavGrid.h"
#include "CBNavGridSceneProxy.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "TimerManager.h"

namespace
{
	bool IsNavigationShowFlagSet(UWorld const * const World)
	{
		FWorldContext const * const WorldContext = GEngine ? GEngine->GetWorldContextFromWorld(World) : nullptr;

		if (!WorldContext)
		{
			return false;
		}

		bool bShowNavigation = WorldContext->GameViewport && WorldContext->GameViewport->EngineShowFlags.Navigation;

#if WITH_EDITOR
		EWorldType::Type const WorldType = WorldContext->WorldType;
		if (!bShowNavigation && GEditor && WorldType != EWorldType::Game && WorldType != EWorldType::PIE)
		{
			for (FEditorViewportClient const * const CurrentViewport : GEditor->GetAllViewportClients())
			{
				if (CurrentViewport && CurrentViewport->EngineShowFlags.Navigation)
				{
					bShowNavigation = true;
					break;
				}
			}
		}
#endif //WITH_EDITOR

		return bShowNavigation;
	}
} // namesace

UCBNavGridRenderingComponent::UCBNavGridRenderingComponent()
{
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	bSelectable = false;
}

void UCBNavGridRenderingComponent::OnRegister()
{
	Super::OnRegister();

	if (FTimerManager * TimerManager = GetTimerManager())
	{
		float const TimerRate = 1.f;
		bool const bLoop = true;
		TimerManager->SetTimer(TimerHandle, FTimerDelegate::CreateUObject(this, &UCBNavGridRenderingComponent::UpdateDrawing), TimerRate, bLoop);
	}
}

void UCBNavGridRenderingComponent::OnUnregister()
{
	if (FTimerManager * TimerManager = GetTimerManager())
	{
		TimerManager->ClearTimer(TimerHandle);
	}

	Super::OnUnregister();
}

void UCBNavGridRenderingComponent::RequestDrawingUpdate()
{
	bDrawingUpdateRequested = true;
}

FBoxSphereBounds UCBNavGridRenderingComponent::CalcBounds(FTransform const & LocalToWorld) const
{
	ACBNavGrid const * const NavGrid = Cast<ACBNavGrid>(GetOwner());
	return NavGrid ? FBoxSphereBounds{ NavGrid->GetBounds() } : Super::CalcBounds(LocalToWorld);
}

FPrimitiveSceneProxy * UCBNavGridRenderingComponent::CreateSceneProxy()
{
	FCBNavGridSceneProxy * NavGridSceneProxy = nullptr;

	ACBNavGrid const * const NavGrid = Cast<ACBNavGrid>(GetOwner());
	if (IsVisible() && NavGrid && NavGrid->IsDrawingEnabled())
	{
		NavGridSceneProxy = new FCBNavGridSceneProxy{ this };
	}

	return NavGridSceneProxy;
}

void UCBNavGridRenderingComponent::UpdateDrawing()
{
	if (bDrawingUpdateRequested && IsNavigationShowFlagSet(GetWorld()))
	{
		MarkRenderStateDirty();
		bDrawingUpdateRequested = false;
	}
}

FTimerManager * UCBNavGridRenderingComponent::GetTimerManager() const
{
#if WITH_EDITOR
	if (GEditor)
	{
		return &(GEditor->GetTimerManager().Get());
	}
	else
#endif
	if (UWorld const * const World = GetWorld())
	{
		return &(World->GetTimerManager());
	}

	return nullptr;
}
