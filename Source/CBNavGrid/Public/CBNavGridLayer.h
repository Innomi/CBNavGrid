#pragma once

#include "CBBitGridLayer.h"
#include "CoreMinimal.h"

class CBNAVGRID_API FCBNavGridLayer : protected FCBBitGridLayer
{
public:
	FCBNavGridLayer();
	explicit FCBNavGridLayer(FIntRect const & InGridRect, float const InGridCellSize, bool const bIsOccupied = false, float const InitHeights = 0.f);
	explicit FCBNavGridLayer(FIntRect const & InGridRect, float const InGridCellSize, bool const bIsOccupied, ENoInit);
	explicit FCBNavGridLayer(FIntRect const & InGridRect, float const InGridCellSize, ENoInit);

	void Serialize(FArchive & Archive);
	bool IsCellOccupied(FIntPoint const Coord) const;
	FORCEINLINE bool IsCellOccupied(int32 const X, int32 const Y) const;
	bool SetCellState(FIntPoint const Coord, bool const bIsOccupied);
	FORCEINLINE bool SetCellState(int32 const X, int32 const Y, bool const bIsOccupied);
	float GetCellHeight(FIntPoint const Coord) const;
	FORCEINLINE float GetCellHeight(int32 const X, int32 const Y) const;
	void SetCellHeight(FIntPoint const Coord, float const Height);
	FORCEINLINE void SetCellHeight(int32 const X, int32 const Y, float const Height);

	float GetCellSize() const;
	FIntPoint GetGridSize() const;
	FIntRect GetGridRect() const;
	FIntRect ClipWithGridRect(FIntRect const & Rect) const;
	bool IsInGrid(FIntPoint const Coord) const;
	FORCEINLINE bool IsInGrid(int32 const X, int32 const Y) const;

	/** Checks if specified rectangle is containing occupied cell. */
	bool HasOccupiedCell(FIntRect const & Rect) const;

	/** Sets cells state in specified rectangle. */
	void SetCellsState(FIntRect const & Rect, bool const bIsOccupied);

	void SetCellsStateInBox(FBox2d const & Box, bool const bIsOccupied);
	void SetCellsStateInCircle(FVector2d const CircleOrigin, double const Radius, bool const bIsOccupied);
	void SetCellsStateInConvex(TArray<FVector2d> const & CCWConvex, bool const bIsOccupied);

	static void Copy(FCBNavGridLayer & Dst, FCBNavGridLayer const & Src, FIntRect const & Rect);
	static void Copy(FCBNavGridLayer & Dst, FCBNavGridLayer const & Src);

protected:
	uint32 GetCellIndexUnsafe(FIntPoint const Coord) const;
	
	/** Converts coords to be used with FCBBitGridLayer. */
	FUintPoint GetUnsignedCoordUnsafe(FIntPoint const SignedCoord) const;
	FUintRect GetUnsignedRectUnsafe(FIntRect const & SignedRect) const;

	TArray<float> CellHeights;
	FIntPoint Origin;
	float CellSize;
};

FORCEINLINE FArchive & operator <<(FArchive & Archive, FCBNavGridLayer & NavGridLayer)
{
	NavGridLayer.Serialize(Archive);
	return Archive;
}

bool FCBNavGridLayer::IsCellOccupied(int32 const X, int32 const Y) const
{
	return IsCellOccupied(FIntPoint{ X, Y });
}

bool FCBNavGridLayer::SetCellState(int32 const X, int32 const Y, bool const bIsOccupied)
{
	return SetCellState(FIntPoint{ X, Y }, bIsOccupied);
}

float FCBNavGridLayer::GetCellHeight(int32 const X, int32 const Y) const
{
	return GetCellHeight(FIntPoint{ X, Y });
}

void FCBNavGridLayer::SetCellHeight(int32 const X, int32 const Y, float const Height)
{
	return SetCellHeight(FIntPoint{ X, Y }, Height);
}

bool FCBNavGridLayer::IsInGrid(int32 const X, int32 const Y) const
{
	return IsInGrid(FIntPoint{ X, Y });
}
