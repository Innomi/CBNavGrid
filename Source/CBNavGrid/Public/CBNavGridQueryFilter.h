#pragma once 

#include "AI/Navigation/NavQueryFilter.h"
#include "CoreMinimal.h"

class CBNAVGRID_API FCBNavGridQueryFilter : public INavigationQueryFilterInterface
{
public:
	explicit FCBNavGridQueryFilter(float const InHeuristicScale = 1.f, FVector2f const InAxiswiseHeuristicScale = FVector2f{ 1.f, 1.f });
	FORCEINLINE void SetHeuristicScale(float const InHeuristicScale);
	FORCEINLINE FVector2f GetAxiswiseHeuristicScale() const;
	FORCEINLINE void SetAxiswiseHeuristicScale(FVector2f const InAxiswiseHeuristicScale);

	virtual void Reset() override;
	virtual void SetAreaCost(uint8 const AreaType, float const Cost) override;
	virtual void SetFixedAreaEnteringCost(uint8 const AreaType, float const Cost) override;
	virtual void SetExcludedArea(uint8 const AreaType) override;
	virtual void SetAllAreaCosts(float const * const CostArray, int32 const Count) override;
	virtual void GetAllAreaCosts(float * const CostArray, float * const FixedCostArray, int32 const Count) const override;
	virtual void SetBacktrackingEnabled(bool const bBacktracking) override;
	virtual bool IsBacktrackingEnabled() const override;
	virtual float GetHeuristicScale() const override;
	virtual bool IsEqual(INavigationQueryFilterInterface const * const Other) const override;
	virtual void SetIncludeFlags(uint16 const Flags) override;
	virtual uint16 GetIncludeFlags() const override;
	virtual void SetExcludeFlags(uint16 const Flags) override;
	virtual uint16 GetExcludeFlags() const override;
	virtual FVector GetAdjustedEndLocation(FVector const & EndLocation) const override;
	virtual INavigationQueryFilterInterface * CreateCopy() const override;

private:
	float HeuristicScale;
	FVector2f AxiswiseHeuristicScale;
};

void FCBNavGridQueryFilter::SetHeuristicScale(float const InHeuristicScale)
{
	HeuristicScale = InHeuristicScale;
}

FVector2f FCBNavGridQueryFilter::GetAxiswiseHeuristicScale() const
{
	return AxiswiseHeuristicScale;
}

void FCBNavGridQueryFilter::SetAxiswiseHeuristicScale(FVector2f const InAxiswiseHeuristicScale)
{
	AxiswiseHeuristicScale = InAxiswiseHeuristicScale;
}
