#include "CBGridUtilities.h"

#include <climits>
#include <concepts>

namespace
{
	template <std::signed_integral T>
	constexpr T DivideAndRoundToNegativeInfinity(T const Dividend, T const Divisor)
	{
		T const Temp = (Dividend >> (sizeof(T) * CHAR_BIT - 1));
		return (Dividend ^ Temp) / Divisor ^ Temp;
	}

	constexpr uint8 DirectionsNum = static_cast<uint8>(ECBGridDirection::DIRECTIONS_NUM);
	FIntPoint const AdjacentCoordShifts[DirectionsNum] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
	FIntPoint const EdgeStartCoordShifts[DirectionsNum] = { { 1, 1 }, { 0, 1 }, { 0, 0 }, { 1, 0 } };
	FIntPoint const EdgeEndCoordShifts[DirectionsNum] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0 } };
} // namespace

namespace CBGridUtilities
{
	FVector2d GetGridCellCenter(FIntPoint const Coord, float const GridCellSize)
	{
		double const CenterX = (Coord.X + 0.5) * GridCellSize;
		double const CenterY = (Coord.Y + 0.5) * GridCellSize;
		return FVector2D{ CenterX, CenterY };
	}

	FIntPoint GetGridCellCoord(FVector2d const Location, float const GridCellSize)
	{
		int32 X, Y;

		if (FMath::IsNearlyZero(GridCellSize))
		{
			X = FMath::FloorToInt32(Location.X);
			Y = FMath::FloorToInt32(Location.Y);
		}
		else
		{
			X = FMath::FloorToInt32(Location.X / GridCellSize);
			Y = FMath::FloorToInt32(Location.Y / GridCellSize);
		}

		return FIntPoint{ X, Y };
	}

	FIntRect GetGridRectFromBoundingBox(FBox const & BoundingBox, float const GridCellSize)
	{
		return GetGridRectFromBoundingBox2d(FBox2d{ static_cast<FVector2d>(BoundingBox.Min), static_cast<FVector2d>(BoundingBox.Max) }, GridCellSize);
	}

	FIntRect GetGridRectFromBoundingBox2d(FBox2d const & BoundingBox, float const GridCellSize)
	{
		FIntPoint const Min = GetGridCellCoord(BoundingBox.Min, GridCellSize);
		FIntPoint const Max = GetGridCellCoord(BoundingBox.Max, GridCellSize) + FIntPoint{ 1, 1 };
		return FIntRect{ Min, Max };
	}

	FIntPoint GetTileCoord(FIntPoint const GridCoord, FIntPoint const TileSize)
	{
		int32 const X = DivideAndRoundToNegativeInfinity(GridCoord.X, TileSize.X);
		int32 const Y = DivideAndRoundToNegativeInfinity(GridCoord.Y, TileSize.Y);
		return FIntPoint{ X, Y };
	}

	FIntRect GetTileRect(FIntRect const & GridRect, FIntPoint const TileSize)
	{
		FIntPoint const Min = GetTileCoord(GridRect.Min, TileSize);
		FIntPoint const Max = GetTileCoord(GridRect.Max - FIntPoint{ 1, 1 }, TileSize) + FIntPoint{ 1, 1 };
		return FIntRect{ Min, Max };
	}

	FIntPoint GetAdjacentCoordChecked(FIntPoint const Coord, ECBGridDirection const GridDirection)
	{
		check(static_cast<uint8>(GridDirection) < static_cast<uint8>(ECBGridDirection::DIRECTIONS_NUM));
		return Coord + AdjacentCoordShifts[static_cast<uint8>(GridDirection)];
	}

	void GetEdgeCoordsChecked(FIntPoint const CellCoord, ECBGridDirection const GridDirection, FIntPoint & OutEdgeStart, FIntPoint & OutEdgeEnd)
	{
		check(static_cast<uint8>(GridDirection) < static_cast<uint8>(ECBGridDirection::DIRECTIONS_NUM));
		OutEdgeStart = CellCoord + EdgeStartCoordShifts[static_cast<uint8>(GridDirection)];
		OutEdgeEnd = CellCoord + EdgeEndCoordShifts[static_cast<uint8>(GridDirection)];
	}
} // namespace CBGridUtilities
