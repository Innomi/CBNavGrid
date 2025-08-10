#include "CBNavGridQueryFilter.h"

FCBNavGridQueryFilter::FCBNavGridQueryFilter(float const InHeuristicScale, FVector2f const InAxiswiseHeuristicScale)
	: HeuristicScale(InHeuristicScale)
	, AxiswiseHeuristicScale(InAxiswiseHeuristicScale)
{
}

void FCBNavGridQueryFilter::Reset()
{
}

void FCBNavGridQueryFilter::SetAreaCost(uint8 const AreaType, float const Cost)
{
}

void FCBNavGridQueryFilter::SetFixedAreaEnteringCost(uint8 const AreaType, float const Cost)
{
}

void FCBNavGridQueryFilter::SetExcludedArea(uint8 const AreaType)
{
}

void FCBNavGridQueryFilter::SetAllAreaCosts(float const * const CostArray, int32 const Count)
{
}

void FCBNavGridQueryFilter::GetAllAreaCosts(float * const CostArray, float * const FixedCostArray, int32 const Count) const
{
}

void FCBNavGridQueryFilter::SetBacktrackingEnabled(bool const bBacktracking)
{
}

bool FCBNavGridQueryFilter::IsBacktrackingEnabled() const
{
	return false;
}

float FCBNavGridQueryFilter::GetHeuristicScale() const
{
	return HeuristicScale;
}

bool FCBNavGridQueryFilter::IsEqual(INavigationQueryFilterInterface const * const Other) const
{
	return Other == this;
}

void FCBNavGridQueryFilter::SetIncludeFlags(uint16 const Flags)
{
}

uint16 FCBNavGridQueryFilter::GetIncludeFlags() const
{
	return 0;
}

void FCBNavGridQueryFilter::SetExcludeFlags(uint16 const Flags)
{
}

uint16 FCBNavGridQueryFilter::GetExcludeFlags() const
{
	return 0;
}

FVector FCBNavGridQueryFilter::GetAdjustedEndLocation(FVector const & EndLocation) const
{
	return EndLocation;
}

INavigationQueryFilterInterface * FCBNavGridQueryFilter::CreateCopy() const
{
	return new FCBNavGridQueryFilter(*this);
}
