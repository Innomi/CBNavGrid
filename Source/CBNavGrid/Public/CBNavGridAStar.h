#pragma once 

#include "CBNavGrid.h"
#include "CBNavGridLayer.h"

struct FCBNavGridAStarNode;

class CBNAVGRID_API FCBNavGridToGraphAdapter
{
public:
	using FLocation = FIntPoint;
	using FNodeRef = FLocation;
	using FAStarSearchNode = FCBNavGridAStarNode;

	FORCEINLINE explicit FCBNavGridToGraphAdapter(ACBNavGrid const & InNavGrid);

	FORCEINLINE bool IsValidRef(FNodeRef const NodeRef) const;
	int32 GetNeighbourCount(FNodeRef const NodeRef) const;
	FNodeRef GetNeighbour(FAStarSearchNode const & SearchNode, int32 const NeighbourIndex) const;

private:
	ACBNavGrid const & NavGrid;
	mutable FCBNavGridLayer const * NavGridLayer;
};

struct CBNAVGRID_API FCBNavGridAStarNode
{
	using FGraphNodeRef = typename FCBNavGridToGraphAdapter::FNodeRef;

	FORCEINLINE FCBNavGridAStarNode(FGraphNodeRef const & InNodeRef);
	FORCEINLINE void MarkOpened();
	FORCEINLINE void MarkNotOpened();
	FORCEINLINE void MarkClosed();
	FORCEINLINE void MarkNotClosed();
	FORCEINLINE bool IsOpened() const;
	FORCEINLINE bool IsClosed() const;

	FGraphNodeRef const NodeRef;
	FGraphNodeRef ParentRef;
	FVector::FReal TraversalCost;
	FVector::FReal TotalCost;
	int32 SearchNodeIndex;
	int32 ParentNodeIndex;
	uint8 bIsOpened : 1;
	uint8 bIsClosed : 1;
};

class CBNAVGRID_API FCBNavGridAStarFilter
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
	FORCEINLINE FVector2d const & GetAxiswiseHeuristicScale() const;
	FORCEINLINE FVector::FReal GetHeuristicScale() const;
	FORCEINLINE FVector::FReal GetHeuristicCost(FAStarSearchNode const & StartNode, FAStarSearchNode const & EndNode) const;
	FORCEINLINE FVector::FReal GetTraversalCost(FAStarSearchNode const & StartNode, FAStarSearchNode const & EndNode) const;
	FORCEINLINE bool IsTraversalAllowed(FNodeRef const NodeA, FNodeRef const NodeB) const;
	FORCEINLINE bool WantsPartialSolution() const;
	FORCEINLINE bool ShouldIgnoreClosedNodes() const;
	FORCEINLINE bool ShouldIncludeStartNodeInPath() const;
	FORCEINLINE uint32 GetMaxSearchNodes() const;
	FORCEINLINE FVector::FReal GetCostLimit() const;

private:
	FVector::FReal HeuristicScale;
	FVector2d AxiswiseHeuristicScale;
	FVector::FReal CostLimit;
	uint32 MaxSearchNodes;
	uint8 bWantsPartialSolution : 1;
};

FCBNavGridToGraphAdapter::FCBNavGridToGraphAdapter(ACBNavGrid const & InNavGrid)
	: NavGrid(InNavGrid)
	, NavGridLayer(nullptr)
{
}

bool FCBNavGridToGraphAdapter::IsValidRef(FNodeRef const NodeRef) const
{
	if (NavGridLayer && NavGridLayer->IsInGrid(NodeRef))
	{
		return !NavGridLayer->IsCellOccupied(NodeRef);
	}
	NavGridLayer = NavGrid.GetTileNavigationData(NavGrid.GetTileCoord(NodeRef)).Get();
	return NavGridLayer && !NavGridLayer->IsCellOccupied(NodeRef);
}

FCBNavGridAStarNode::FCBNavGridAStarNode(FGraphNodeRef const & InNodeRef)
	: NodeRef(InNodeRef)
	, ParentRef(TNumericLimits<FIntPoint::IntType>::Min()) // Some value that won't be ever used as ref.
	, TraversalCost(TNumericLimits<FVector::FReal>::Max())
	, TotalCost(TNumericLimits<FVector::FReal>::Max())
	, SearchNodeIndex(INDEX_NONE)
	, ParentNodeIndex(INDEX_NONE)
	, bIsOpened(false)
	, bIsClosed(false)
{
}

void FCBNavGridAStarNode::MarkOpened()
{
	bIsOpened = true;
}

void FCBNavGridAStarNode::MarkNotOpened()
{
	bIsOpened = false;
}

void FCBNavGridAStarNode::MarkClosed()
{
	bIsClosed = true;
}

void FCBNavGridAStarNode::MarkNotClosed()
{
	bIsClosed = false;
}

bool FCBNavGridAStarNode::IsOpened() const
{
	return bIsOpened;
}

bool FCBNavGridAStarNode::IsClosed() const
{
	return bIsClosed;
}

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

FVector2d const & FCBNavGridAStarFilter::GetAxiswiseHeuristicScale() const
{
	return AxiswiseHeuristicScale;
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

FVector::FReal FCBNavGridAStarFilter::GetCostLimit() const
{
	return CostLimit;
}
