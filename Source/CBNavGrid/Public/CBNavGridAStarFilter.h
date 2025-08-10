#pragma once 

#include "CBNavGridToGraphAdapter.h"
#include "CoreMinimal.h"

struct CBNAVGRID_API FCBNavGridAStarFilter
{
public:
	using FGraph = FCBNavGridToGraphAdapter;
	using FNodeRef = FGraph::FNodeRef;
	using FAStarSearchNode = FCBNavGridAStarNode;

	FORCEINLINE explicit FCBNavGridAStarFilter(
		FVector::FReal const InHeuristicScale = 1.,
		FVector2d const InAxiswiseHeuristicScale = FVector2d{ 1., 1. },
		FVector::FReal const InCostLimit = TNumericLimits<FVector::FReal>::Max(),
		uint32 const InMaxSearchNodes = 2048,
		bool const bInWantsPartialSolution = false);
	FORCEINLINE FVector::FReal GetHeuristicScale() const;
	FORCEINLINE FVector::FReal GetHeuristicCost(FAStarSearchNode const & StartNode, FAStarSearchNode const & EndNode) const;
	FORCEINLINE FVector::FReal GetTraversalCost(FAStarSearchNode const & StartNode, FAStarSearchNode const & EndNode) const;
	FORCEINLINE bool IsTraversalAllowed(FNodeRef const NodeA, FNodeRef const NodeB) const;
	FORCEINLINE bool WantsPartialSolution() const;
	FORCEINLINE bool ShouldIgnoreClosedNodes() const;
	FORCEINLINE bool ShouldIncludeStartNodeInPath() const;
	FORCEINLINE uint32 GetMaxSearchNodes() const;
	FORCEINLINE FVector::FReal GetCostLimit() const;

	FVector::FReal HeuristicScale;
	FVector2d AxiswiseHeuristicScale;
	FVector::FReal CostLimit;
	uint32 MaxSearchNodes;
	uint8 bWantsPartialSolution : 1;
};

FCBNavGridAStarFilter::FCBNavGridAStarFilter(
	FVector::FReal const InHeuristicScale,
	FVector2d const InAxiswiseHeuristicScale,
	FVector::FReal const InCostLimit,
	uint32 const InMaxSearchNodes,
	bool const bInWantsPartialSolution)
	: HeuristicScale(InHeuristicScale)
	, AxiswiseHeuristicScale(InAxiswiseHeuristicScale)
	, CostLimit(InCostLimit)
	, MaxSearchNodes(InMaxSearchNodes)
	, bWantsPartialSolution(bInWantsPartialSolution)
{
}

FVector::FReal FCBNavGridAStarFilter::GetHeuristicScale() const
{
	return HeuristicScale;
}

FVector::FReal FCBNavGridAStarFilter::GetHeuristicCost(FAStarSearchNode const & StartNode, FAStarSearchNode const & EndNode) const
{
	FNodeRef const StartNodeRef = StartNode.NodeRef;
	FNodeRef const EndNodeRef = EndNode.NodeRef;
	FVector::FReal const XAxisPart = static_cast<FVector::FReal>(FMath::Abs(StartNodeRef.X - EndNodeRef.X)) * AxiswiseHeuristicScale.X;
	FVector::FReal const YAxisPart = static_cast<FVector::FReal>(FMath::Abs(StartNodeRef.Y - EndNodeRef.Y)) * AxiswiseHeuristicScale.Y;
	return XAxisPart + YAxisPart;
}

FVector::FReal FCBNavGridAStarFilter::GetTraversalCost(FAStarSearchNode const & StartNode, FAStarSearchNode const & EndNode) const
{
	check((EndNode.NodeRef - StartNode.NodeRef).SizeSquared() == 1);
	return 1.;
}

bool FCBNavGridAStarFilter::IsTraversalAllowed(FNodeRef const NodeA, FNodeRef const NodeB) const
{
	check((NodeB - NodeA).SizeSquared() == 1);
	return true;
}

bool FCBNavGridAStarFilter::WantsPartialSolution() const
{
	return bWantsPartialSolution;
}

bool FCBNavGridAStarFilter::ShouldIgnoreClosedNodes() const
{
	return true;
}

bool FCBNavGridAStarFilter::ShouldIncludeStartNodeInPath() const
{
	return true;
}

uint32 FCBNavGridAStarFilter::GetMaxSearchNodes() const
{
	return MaxSearchNodes;
}

inline FVector::FReal FCBNavGridAStarFilter::GetCostLimit() const
{
	return CostLimit;
}
