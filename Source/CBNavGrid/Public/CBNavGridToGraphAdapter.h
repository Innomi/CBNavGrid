#pragma once 

#include "CBNavGrid.h"
#include "CBNavGridLayer.h"
#include "CoreMinimal.h"

struct FCBNavGridAStarNode;

class CBNAVGRID_API FCBNavGridToGraphAdapter
{
public:
	using FLocation = FIntPoint;
	using FNodeRef = FLocation;
	using FAStarSearchNode = FCBNavGridAStarNode;

	FORCEINLINE explicit FCBNavGridToGraphAdapter(ACBNavGrid const & InNavGrid);

	FORCEINLINE bool IsValidRef(FNodeRef const NodeRef) const;
	constexpr int32 GetNeighbourCount(FNodeRef const NodeRef) const;
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
