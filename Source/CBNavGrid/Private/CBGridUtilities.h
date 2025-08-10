#pragma once

#include "CoreMinimal.h"

UENUM()
enum class ECBGridDirection : uint8
{
	PositiveX,
	PositiveY,
	NegativeX,
	NegativeY,
	DIRECTIONS_NUM UMETA(Hidden),
	NONE
};

ENUM_RANGE_BY_COUNT(ECBGridDirection, ECBGridDirection::DIRECTIONS_NUM)

namespace CBGridUtilities
{
	FVector2d GetGridCellCenter(FIntPoint const Coord, float const GridCellSize);
	FIntPoint GetGridCellCoord(FVector2d const Location, float const GridCellSize);
	FIntRect GetGridRectFromBoundingBox2d(FBox2d const & BoundingBox, float const GridCellSize);
	FIntRect GetGridRectFromBoundingBox(FBox const & BoundingBox, float const GridCellSize);
	FIntPoint GetTileCoord(FIntPoint const GridCoord, FIntPoint const TileSize);
	FIntRect GetTileRect(FIntRect const & GridRect, FIntPoint const TileSize);

	FIntPoint GetAdjacentCoordChecked(FIntPoint const Coord, ECBGridDirection const GridDirection);
	void GetEdgeCoordsChecked(FIntPoint const CellCoord, ECBGridDirection const GridDirection, FIntPoint & OutEdgeStart, FIntPoint & OutEdgeEnd);
}
