#include "CBNavGridLayer.h"
#include "CBGridUtilities.h"
#include "GeomTools.h"

namespace
{
	void CheckRect(FIntRect const & GridRect)
	{
		check(GridRect.Min.X <= GridRect.Max.X && GridRect.Min.Y <= GridRect.Max.Y);
	}

	bool IsPointInConvexPolygon(FVector2d const Point, TArray<FVector2d> const & CCWConvex)
	{
		checkSlow(FGeomTools2D::IsPolygonWindingCCW(CCWConvex));
		check(CCWConvex.Num() > 2);
		FVector2d const * PreviousConvexVertex = &(CCWConvex.Last());
		for (FVector2d const & ConvexVertex : CCWConvex)
		{
			if (FVector2d::CrossProduct(ConvexVertex - *PreviousConvexVertex, Point - *PreviousConvexVertex) < 0.)
			{
				return false;
			}
			PreviousConvexVertex = &ConvexVertex;
		}
		return true;
	}

	template <std::integral IntType, std::invocable<typename UE::Math::TIntRect<IntType>::IntPointType> BodyType>
	void ForRect(UE::Math::TIntRect<IntType> const & Rect, BodyType && Body)
	{
		for (IntType X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			for (IntType Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
			{
				Body(typename UE::Math::TIntRect<IntType>::IntPointType{ X, Y });
			}
		}
	}
} // namespace

FCBNavGridLayer::FCBNavGridLayer()
	: FCBBitGridLayer{}
	, Origin{ 0 , 0 }
	, CellSize{ 0.f }
{
}

FCBNavGridLayer::FCBNavGridLayer(FIntRect const & InGridRect, float const InGridCellSize, bool const bIsOccupied, float const InitHeights)
	: FCBBitGridLayer(static_cast<FUintPoint>(InGridRect.Size()), bIsOccupied)
	, Origin(InGridRect.Min)
	, CellSize(InGridCellSize)
{
	CheckRect(InGridRect);
	CellHeights.Init(InitHeights, InGridRect.Area());
}

FCBNavGridLayer::FCBNavGridLayer(FIntRect const & InGridRect, float const InGridCellSize, bool const bIsOccupied, ENoInit)
	: FCBBitGridLayer(static_cast<FUintPoint>(InGridRect.Size()), bIsOccupied)
	, Origin(InGridRect.Min)
	, CellSize(InGridCellSize)
{
	CheckRect(InGridRect);
	CellHeights.AddUninitialized(InGridRect.Area());
}

FCBNavGridLayer::FCBNavGridLayer(FIntRect const & InGridRect, float const InGridCellSize, ENoInit)
	: FCBBitGridLayer(static_cast<FUintPoint>(InGridRect.Size()), ENoInit::NoInit)
	, Origin(InGridRect.Min)
	, CellSize(InGridCellSize)
{
	CheckRect(InGridRect);
	CellHeights.AddUninitialized(InGridRect.Area());
}

void FCBNavGridLayer::Serialize(FArchive & Archive)
{
	FCBBitGridLayer::Serialize(Archive);

	Archive << Origin << CellSize << CellHeights;
}

bool FCBNavGridLayer::IsCellOccupied(FIntPoint const Coord) const
{
	if (!IsInGrid(Coord))
	{
		return false;
	}
	return operator [](GetUnsignedCoordUnsafe(Coord));
}

bool FCBNavGridLayer::SetCellState(FIntPoint const Coord, bool const bIsOccupied)
{
	if (!IsInGrid(Coord))
	{
		return false;
	}
	operator [](GetUnsignedCoordUnsafe(Coord)) = bIsOccupied;
	return true;
}

float FCBNavGridLayer::GetCellHeight(FIntPoint const Coord) const
{
	if (IsInGrid(Coord))
	{
		return CellHeights[GetCellIndexUnsafe(Coord)];
	}
	return 0.;
}

void FCBNavGridLayer::SetCellHeight(FIntPoint const Coord, float const Height)
{
	if (IsInGrid(Coord))
	{
		CellHeights[GetCellIndexUnsafe(Coord)] = Height;
	}
}

float FCBNavGridLayer::GetCellSize() const
{
	return CellSize;
}

FIntPoint FCBNavGridLayer::GetGridSize() const
{
	return static_cast<FIntPoint>(GetSize());
}

FIntRect FCBNavGridLayer::GetGridRect() const
{
	return FIntRect{ Origin, Origin + static_cast<FIntPoint>(GetSize()) };
}

FIntRect FCBNavGridLayer::ClipWithGridRect(FIntRect const & Rect) const
{
	FIntRect ClippedRect = GetGridRect();
	ClippedRect.Clip(Rect);
	return ClippedRect;
}

bool FCBNavGridLayer::IsInGrid(FIntPoint const Coord) const
{
	return GetGridRect().Contains(Coord);
}

bool FCBNavGridLayer::HasOccupiedCell(FIntRect const & Rect) const
{
	bool const bValue = true;
	return Contains(GetUnsignedRectUnsafe(ClipWithGridRect(Rect)), bValue);
}

void FCBNavGridLayer::SetCellsState(FIntRect const & Rect, bool const bIsOccupied)
{
	SetCells(GetUnsignedRectUnsafe(ClipWithGridRect(Rect)), bIsOccupied);
}

void FCBNavGridLayer::SetCellsStateInBox(FBox2d const & Box, bool const bIsOccupied)
{
	SetCellsState(CBGridUtilities::GetGridRectFromBoundingBox2d(Box, CellSize), bIsOccupied);
}

void FCBNavGridLayer::SetCellsStateInCircle(FVector2d const CircleOrigin, double const Radius, bool const bIsOccupied)
{
	FIntRect const BoundingRect = CBGridUtilities::GetGridRectFromBoundingBox2d(FBox2d{ CircleOrigin - Radius, CircleOrigin + Radius }, CellSize);
	FIntRect const ClippedBoundingRect = ClipWithGridRect(BoundingRect);
	ForRect(ClippedBoundingRect, [this, CircleOrigin, Radius, bIsOccupied](FIntPoint const Coord)
		{
			double const CellCenterToOriginDistSquared = FVector2d::DistSquared(CBGridUtilities::GetGridCellCenter(Coord, CellSize), CircleOrigin);
			if (FMath::Square(Radius) >= CellCenterToOriginDistSquared)
			{
				operator [](GetUnsignedCoordUnsafe(Coord)) = bIsOccupied;
			}
		});
}

void FCBNavGridLayer::SetCellsStateInConvex(TArray<FVector2d> const & CCWConvex, bool const bIsOccupied)
{
	if (CCWConvex.Num() < 3)
	{
		return;
	}

	FIntRect const BoundingRect = CBGridUtilities::GetGridRectFromBoundingBox2d(FBox2d{ CCWConvex }, CellSize);
	FIntRect const ClippedBoundingRect = ClipWithGridRect(BoundingRect);
	ForRect(ClippedBoundingRect, [this, &CCWConvex, bIsOccupied](FIntPoint const Coord)
		{
			if (IsPointInConvexPolygon(CBGridUtilities::GetGridCellCenter(Coord, CellSize), CCWConvex))
			{
				operator [](GetUnsignedCoordUnsafe(Coord)) = bIsOccupied;
			}
		});
}

void FCBNavGridLayer::Copy(FCBNavGridLayer & Dst, FCBNavGridLayer const & Src, FIntRect const & Rect)
{
	FIntRect RectToCopy = Rect;
	RectToCopy.Clip(Dst.GetGridRect());
	ForRect(RectToCopy, [&Dst, &Src](FIntPoint const Coord)
		{
			Dst.SetCellHeight(Coord, Src.GetCellHeight(Coord));
			Dst.SetCellState(Coord, Src.IsCellOccupied(Coord));
		});
}

void FCBNavGridLayer::Copy(FCBNavGridLayer & Dst, FCBNavGridLayer const & Src)
{
	Copy(Dst, Src, Src.GetGridRect());
}

uint32 FCBNavGridLayer::GetCellIndexUnsafe(FIntPoint const Coord) const
{
	check(IsInGrid(Coord));
	return (Coord.X - Origin.X) * GetYSize() + (Coord.Y - Origin.Y);
}

FUintPoint FCBNavGridLayer::GetUnsignedCoordUnsafe(FIntPoint const SignedCoord) const
{
	check(IsInGrid(SignedCoord));
	return static_cast<FUintPoint>(SignedCoord - Origin);
}

FUintRect FCBNavGridLayer::GetUnsignedRectUnsafe(FIntRect const & SignedRect) const
{
	return FUintRect{ GetUnsignedCoordUnsafe(SignedRect.Min), GetUnsignedCoordUnsafe(SignedRect.Max) };
}
