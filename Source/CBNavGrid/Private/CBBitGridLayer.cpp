#include "CBBitGridLayer.h"

FCBBitGridLayer::FCBBitGridLayer()
	: Size{ 0, 0 }
{
}

FCBBitGridLayer::FCBBitGridLayer(FUintPoint const InSize, bool const bValue)
{
	SetSize(InSize, bValue);
}

FCBBitGridLayer::FCBBitGridLayer(FUintPoint const InSize, ENoInit)
{
	SetSize(InSize);
}

FBitReference FCBBitGridLayer::operator [](FUintPoint const Coord)
{
	CheckRange(Coord);
	return GetTileByCellCoord(Coord)[GetCoordInTile(Coord)];
}

FConstBitReference const FCBBitGridLayer::operator [](FUintPoint const Coord) const
{
	CheckRange(Coord);
	return GetTileByCellCoord(Coord)[GetCoordInTile(Coord)];
}

void FCBBitGridLayer::Serialize(FArchive & Archive)
{
	Archive << Size << GridLayerData;
}

bool FCBBitGridLayer::Contains(FUintRect const & Rect, bool const bValue) const
{
	// TODO: It's ugly as fuck, rewrite it. Have no time right now.
	if (Rect.IsEmpty())
	{
		return false;
	}
	CheckRange(Rect);

	FUintRect const TilesToCheck{ Rect.Min / FBitGridTile::GetSize(), (Rect.Max - FUintPoint{ 1, 1 }) / FBitGridTile::GetSize() };
	uint32 const StartWordIndex = Rect.Min.X % FBitGridTile::GetXSize();
	WordType const StartMask = FullWordMask << (Rect.Min.Y % FBitGridTile::GetYSize());
	uint32 const EndWordIndex = ((Rect.Max.X - 1) % FBitGridTile::GetXSize()) + 1;
	WordType const EndMask = FullWordMask >> ((FBitGridTile::GetYSize() - (Rect.Max.Y % FBitGridTile::GetYSize())) % FBitGridTile::GetYSize());

	auto CheckColumn = [this, StartWordIndex, EndWordIndex, bValue] (uint32 const FirstTileIndex, uint32 const LastTileIndex, WordType const Mask) -> bool
		{
			if (GridLayerData[FirstTileIndex].Contains(bValue, StartWordIndex, FBitGridTile::GetXSize(), Mask))
			{
				return true;
			}
			for (uint32 TileIndex = FirstTileIndex + 1; TileIndex < LastTileIndex; ++TileIndex)
			{
				if (GridLayerData[TileIndex].Contains(bValue, 0, FBitGridTile::GetXSize(), Mask))
				{
					return true;
				}
			}
			return GridLayerData[LastTileIndex].Contains(bValue, 0, EndWordIndex, Mask);
		};

	if (TilesToCheck.Width() == 0)
	{
		if (TilesToCheck.Height() == 0)
		{
			return GetTile(TilesToCheck.Min).Contains(bValue, StartWordIndex, EndWordIndex, StartMask & EndMask);
		}
		else
		{
			uint32 const FirstTileIndex = GetTileIndex(TilesToCheck.Min);
			uint32 const LastTileIndex = GetTileIndex(TilesToCheck.Max);
			if (GridLayerData[FirstTileIndex].Contains(bValue, StartWordIndex, EndWordIndex, StartMask))
			{
				return true;
			}
			for (uint32 TileIndex = FirstTileIndex + GetXTileNum(); TileIndex < LastTileIndex; TileIndex += GetXTileNum())
			{
				if (GridLayerData[TileIndex].Contains(bValue, StartWordIndex, EndWordIndex, FullWordMask))
				{
					return true;
				}
			}
			return GridLayerData[LastTileIndex].Contains(bValue, StartWordIndex, EndWordIndex, EndMask);
		}
	}
	else
	{
		if (TilesToCheck.Height() == 0)
		{
			uint32 const FirstTileIndex = GetTileIndex(TilesToCheck.Min);
			uint32 const LastTileIndex = GetTileIndex(TilesToCheck.Max);
			return CheckColumn(FirstTileIndex, LastTileIndex, StartMask & EndMask);
		}
		else
		{
			// Checks first column.
			{
				uint32 const FirstTileIndex = GetTileIndex(TilesToCheck.Min);
				uint32 const LastTileIndex = GetTileIndex(FUintPoint{ TilesToCheck.Max.X, TilesToCheck.Min.Y });
				if (CheckColumn(FirstTileIndex, LastTileIndex, StartMask))
				{
					return true;
				}
			}

			// Checks internal columns.
			{
				uint32 const InnerXTilesNum = TilesToCheck.Width() - 1;
				uint32 const FirstColumnTileIndex = GetTileIndex(FUintPoint{ TilesToCheck.Min.X, TilesToCheck.Min.Y + 1 });
				uint32 const LastColumnTileIndex = GetTileIndex(FUintPoint{ TilesToCheck.Min.X, TilesToCheck.Max.Y });
				BYTE const Test = bValue ? 0u : ~0u;
				for (uint32 ColumnTileIndex = FirstColumnTileIndex; ColumnTileIndex < LastColumnTileIndex; ColumnTileIndex += GetXTileNum())
				{
					if (GridLayerData[ColumnTileIndex].Contains(bValue, StartWordIndex, FBitGridTile::GetXSize(), FullWordMask))
					{
						return true;
					}
					void const * const Start = static_cast<void const *>(GridLayerData.GetData() + ColumnTileIndex + 1);
					void const * const End = static_cast<void const *>(GridLayerData.GetData() + ColumnTileIndex + 1 + InnerXTilesNum);
					for (BYTE const * Byte = static_cast<BYTE const *>(Start); Byte < End; ++Byte)
					{
						if (*Byte != Test)
						{
							return true;
						}
					}
					if (GridLayerData[ColumnTileIndex + 1 + InnerXTilesNum].Contains(bValue, 0, EndWordIndex, FullWordMask))
					{
						return true;
					}
				}
			}

			// Checks last column.
			{
				uint32 const FirstTileIndex = GetTileIndex(FUintPoint{ TilesToCheck.Min.X, TilesToCheck.Max.Y });
				uint32 const LastTileIndex = GetTileIndex(TilesToCheck.Max);
				return CheckColumn(FirstTileIndex, LastTileIndex, EndMask);
			}
		}
	}
}

void FCBBitGridLayer::SetCells(FUintRect const & Rect, bool const bValue)
{
	if (Rect.IsEmpty())
	{
		return;
	}
	CheckRange(Rect);

	FUintRect const TilesToSet{ Rect.Min / FBitGridTile::GetSize(), (Rect.Max - FUintPoint{ 1, 1 }) / FBitGridTile::GetSize() };
	uint32 const StartWordIndex = Rect.Min.X % FBitGridTile::GetXSize();
	WordType const StartMask = FullWordMask << (Rect.Min.Y % FBitGridTile::GetYSize());
	uint32 const EndWordIndex = ((Rect.Max.X - 1) % FBitGridTile::GetXSize()) + 1;
	WordType const EndMask = FullWordMask >> ((FBitGridTile::GetYSize() - (Rect.Max.Y % FBitGridTile::GetYSize())) % FBitGridTile::GetYSize());

	auto SetColumn = [this, StartWordIndex, EndWordIndex, bValue] (uint32 const FirstTileIndex, uint32 const LastTileIndex, WordType const Mask)
		{
			GridLayerData[FirstTileIndex].SetCells(bValue, StartWordIndex, FBitGridTile::GetXSize(), Mask);
			for (uint32 TileIndex = FirstTileIndex + 1; TileIndex < LastTileIndex; ++TileIndex)
			{
				GridLayerData[TileIndex].SetCells(bValue, 0, FBitGridTile::GetXSize(), Mask);
			}
			GridLayerData[LastTileIndex].SetCells(bValue, 0, EndWordIndex, Mask);
		};

	if (TilesToSet.Width() == 0)
	{
		if (TilesToSet.Height() == 0)
		{
			GetTile(TilesToSet.Min).SetCells(bValue, StartWordIndex, EndWordIndex, StartMask & EndMask);
		}
		else
		{
			uint32 const FirstTileIndex = GetTileIndex(TilesToSet.Min);
			uint32 const LastTileIndex = GetTileIndex(TilesToSet.Max);
			GridLayerData[FirstTileIndex].SetCells(bValue, StartWordIndex, EndWordIndex, StartMask);
			for (uint32 TileIndex = FirstTileIndex + GetXTileNum(); TileIndex < LastTileIndex; TileIndex += GetXTileNum())
			{
				GridLayerData[TileIndex].SetCells(bValue, StartWordIndex, EndWordIndex, FullWordMask);
			}
			GridLayerData[LastTileIndex].SetCells(bValue, StartWordIndex, EndWordIndex, EndMask);
		}
	}
	else
	{
		if (TilesToSet.Height() == 0)
		{
			uint32 const FirstTileIndex = GetTileIndex(TilesToSet.Min);
			uint32 const LastTileIndex = GetTileIndex(TilesToSet.Max);
			SetColumn(FirstTileIndex, LastTileIndex, StartMask & EndMask);
		}
		else
		{
			// Sets first column.
			{
				uint32 const FirstTileIndex = GetTileIndex(TilesToSet.Min);
				uint32 const LastTileIndex = GetTileIndex(FUintPoint{ TilesToSet.Max.X, TilesToSet.Min.Y });
				SetColumn(FirstTileIndex, LastTileIndex, StartMask);
			}

			// Sets internal columns.
			{
				uint32 const InnerXTilesNum = TilesToSet.Width() - 1;
				uint32 const FirstColumnTileIndex = GetTileIndex(FUintPoint{ TilesToSet.Min.X, TilesToSet.Min.Y + 1 });
				uint32 const LastColumnTileIndex = GetTileIndex(FUintPoint{ TilesToSet.Min.X, TilesToSet.Max.Y });
				for (uint32 ColumnTileIndex = FirstColumnTileIndex; ColumnTileIndex < LastColumnTileIndex; ColumnTileIndex += GetXTileNum())
				{
					GridLayerData[ColumnTileIndex].SetCells(bValue, StartWordIndex, FBitGridTile::GetXSize(), FullWordMask);
					// Set fully covered tiles.
					FMemory::Memset(GridLayerData.GetData() + ColumnTileIndex + 1, (bValue ? 0xff : 0), InnerXTilesNum * sizeof(FBitGridTile));
					GridLayerData[ColumnTileIndex + 1 + InnerXTilesNum].SetCells(bValue, 0, EndWordIndex, FullWordMask);
				}
			}

			// Sets last column.
			{
				uint32 const FirstTileIndex = GetTileIndex(FUintPoint{ TilesToSet.Min.X, TilesToSet.Max.Y });
				uint32 const LastTileIndex = GetTileIndex(TilesToSet.Max);
				SetColumn(FirstTileIndex, LastTileIndex, EndMask);
			}
		}
	}
}

FUintPoint FCBBitGridLayer::GetSize() const
{
	return Size;
}

uint32 FCBBitGridLayer::GetXSize() const
{
	return Size.X;
}

uint32 FCBBitGridLayer::GetYSize() const
{
	return Size.Y;
}

FUintPoint FCBBitGridLayer::AdjustSize(FUintPoint const InSize)
{
	check((InSize.X <= MAX_uint32 - (FBitGridTile::GetXSize() - 1)) && (InSize.Y <= MAX_uint32 - (FBitGridTile::GetYSize() - 1)));
	uint32 const X = ((InSize.X + (FBitGridTile::GetXSize() - 1)) / FBitGridTile::GetXSize()) * FBitGridTile::GetXSize();
	return FUintPoint{ X, ((InSize.Y + (FBitGridTile::GetYSize() - 1)) / FBitGridTile::GetYSize()) * FBitGridTile::GetYSize() };
}

void FCBBitGridLayer::CheckRange(FUintPoint const Coord) const
{
	check((Coord.X < GetXSize()) && (Coord.Y < GetYSize()));
}

void FCBBitGridLayer::CheckRange(FUintRect const & Rect) const
{
	CheckRange(Rect.Min);
	CheckRange(Rect.IsEmpty() ? Rect.Max : Rect.Max - FUintPoint{ 1, 1 });
	check(Rect.Min.X <= Rect.Max.X && Rect.Min.Y <= Rect.Max.Y);
}

void FCBBitGridLayer::SetSize(FUintPoint const NewSize, bool const bValue)
{
	Size = AdjustSize(NewSize);
	int32 const NewTilesNum = GetXTileNum() * GetYTileNum();
	GridLayerData.Empty(NewTilesNum);
	GridLayerData.Init(FBitGridTile{ bValue }, NewTilesNum);
}

void FCBBitGridLayer::SetSize(FUintPoint const NewSize)
{
	Size = AdjustSize(NewSize);
	int32 const NewTilesNum = GetXTileNum() * GetYTileNum();
	GridLayerData.Empty(NewTilesNum);
	GridLayerData.SetNumUninitialized(NewTilesNum);
}

FUintPoint FCBBitGridLayer::GetCoordInTile(FUintPoint const Coord) const
{
	return FUintPoint{ Coord.X % FBitGridTile::GetXSize(), Coord.Y % FBitGridTile::GetYSize() };
}

FCBBitGridLayer::FBitGridTile const & FCBBitGridLayer::GetTile(FUintPoint const Coord) const
{
	return GridLayerData[GetTileIndex(Coord)];
}

FCBBitGridLayer::FBitGridTile & FCBBitGridLayer::GetTile(FUintPoint const Coord)
{
	return const_cast<FBitGridTile &>(static_cast<FCBBitGridLayer const &>(*this).GetTile(Coord));
}

FCBBitGridLayer::FBitGridTile const & FCBBitGridLayer::GetTileByCellCoord(FUintPoint const Coord) const
{
	CheckRange(Coord);
	uint32 const TileX = Coord.X / FBitGridTile::GetXSize();
	uint32 const TileY = Coord.Y / FBitGridTile::GetYSize();
	return GetTile(FUintPoint{ TileX, TileY });
}

FCBBitGridLayer::FBitGridTile & FCBBitGridLayer::GetTileByCellCoord(FUintPoint const Coord)
{
	return const_cast<FBitGridTile &>(static_cast<FCBBitGridLayer const &>(*this).GetTileByCellCoord(Coord));
}

uint32 FCBBitGridLayer::GetTileIndex(FUintPoint const Coord) const
{
	return Coord.Y * GetXTileNum() + Coord.X;
}

FUintPoint FCBBitGridLayer::GetTileNum() const
{
	return FUintPoint{ GetXTileNum(), GetYTileNum() };
}

uint32 FCBBitGridLayer::GetXTileNum() const
{
	return GetXSize() / FBitGridTile::GetXSize();
}

uint32 FCBBitGridLayer::GetYTileNum() const
{
	return GetYSize() / FBitGridTile::GetYSize();
}

FCBBitGridLayer::FBitGridTile::FBitGridTile() = default;

FCBBitGridLayer::FBitGridTile::FBitGridTile(bool const bValue)
	: TCBBitGridTile<WordType, WordsPerTileNum>{ bValue }
{
}
