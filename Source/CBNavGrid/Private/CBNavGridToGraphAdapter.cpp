#include "CBNavGridToGraphAdapter.h"
#include "CBGridUtilities.h"

constexpr int32 FCBNavGridToGraphAdapter::GetNeighbourCount(FNodeRef const NodeRef) const
{
	return static_cast<int32>(ECBGridDirection::DIRECTIONS_NUM);
}

FCBNavGridToGraphAdapter::FNodeRef FCBNavGridToGraphAdapter::GetNeighbour(FAStarSearchNode const & SearchNode, int32 const NeighbourIndex) const
{
	return CBGridUtilities::GetAdjacentCoordChecked(SearchNode.NodeRef, static_cast<ECBGridDirection>(NeighbourIndex));
}
