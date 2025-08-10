#pragma once

#include "CBBitGridTile.h"
#include "CoreMinimal.h"

class CBNAVGRID_API FCBBitGridLayer
{
public:
	FCBBitGridLayer();

	/**
	 * Sets size of layer and fills it with bValue.
	 * @param InSize - X and Y will be padded to be multiple of tile size.
	 * @param bValue - value to fill grid with.
	 */
	explicit FCBBitGridLayer(FUintPoint const InSize, bool const bValue);
	explicit FCBBitGridLayer(FUintPoint const InSize, ENoInit);

	FBitReference operator [](FUintPoint const Coord);
	FConstBitReference const operator [](FUintPoint const Coord) const;

	void Serialize(FArchive & Archive);
	bool Contains(FUintRect const & Rect, bool const bValue) const;
	void SetCells(FUintRect const & Rect, bool const bValue);

	/** Size getters. */
	FUintPoint GetSize() const;
	uint32 GetXSize() const;
	uint32 GetYSize() const;

protected:
	static FUintPoint AdjustSize(FUintPoint const InSize);
	void CheckRange(FUintPoint const Coord) const;
	void CheckRange(FUintRect const & Rect) const;
	void SetSize(FUintPoint const NewSize, bool const bValue);
	void SetSize(FUintPoint const NewSize);

private:
	using WordType = uint32;
	static constexpr uint32 WordsPerTileNum = 64 / sizeof(WordType);
	static constexpr WordType FullWordMask = ~0u;

	/** Tile of grid. Contains info about 16 x 32 grid cells. 64 bytes to fit in one cache line of most modern CPUs. */
	struct alignas(64) FBitGridTile : public TCBBitGridTile<WordType, WordsPerTileNum>
	{
	public:
		FBitGridTile();
		explicit FBitGridTile(bool const bValue);
	};

	FORCEINLINE friend FArchive & operator <<(FArchive & Archive, FBitGridTile & Tile);

	FUintPoint GetCoordInTile(FUintPoint const Coord) const;
	FBitGridTile const & GetTile(FUintPoint const Coord) const;
	FBitGridTile & GetTile(FUintPoint const Coord);
	FBitGridTile const & GetTileByCellCoord(FUintPoint const Coord) const;
	FBitGridTile & GetTileByCellCoord(FUintPoint const Coord);
	uint32 GetTileIndex(FUintPoint const Coord) const;
	FUintPoint GetTileNum() const;
	uint32 GetXTileNum() const;
	uint32 GetYTileNum() const;

	TArray<FBitGridTile> GridLayerData;
	FUintPoint Size;
};

FORCEINLINE FArchive & operator <<(FArchive & Archive, FCBBitGridLayer & GridLayer)
{
	GridLayer.Serialize(Archive);
	return Archive;
}

FArchive & operator <<(FArchive & Archive, FCBBitGridLayer::FBitGridTile & Tile)
{
	Tile.Serialize(Archive);
	return Archive;
}
