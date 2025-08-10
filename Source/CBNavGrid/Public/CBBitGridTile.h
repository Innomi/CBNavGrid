#pragma once

#include <climits>
#include <concepts>

#include "CoreMinimal.h"

template <std::integral WordType, uint32 WordsNum>
struct TCBBitGridTile
{
public:
	TCBBitGridTile();
	explicit TCBBitGridTile(bool const bValue);

	WordType & operator [](uint32 const WordIndex);
	WordType const & operator [](uint32 const WordIndex) const;
	FBitReference operator [](FUintPoint const Coord);
	FConstBitReference const operator [](FUintPoint const Coord) const;

	static FUintPoint GetSize();
	static constexpr uint32 GetXSize();
	static constexpr uint32 GetYSize();

	static constexpr WordType FullWordMask = ~0u;

	void Serialize(FArchive & Archive);
	bool Contains(bool const bValue, uint32 const FromWordIndex = 0, uint32 const ToWordIndex = WordsNum, WordType const Mask = FullWordMask) const;
	void SetCells(bool const bValue, uint32 const FromWordIndex = 0, uint32 const ToWordIndex = WordsNum, WordType const Mask = FullWordMask);

private:
	void CheckRange(FUintPoint const Coord) const;

	WordType GridCells[WordsNum];
};

template <typename WordType, uint32 WordsNum>
FArchive & operator <<(FArchive & Archive, TCBBitGridTile<WordType, WordsNum> & Tile)
{
	Tile.Serialize(Archive);
	return Archive;
}

template <std::integral WordType, uint32 WordsNum>
TCBBitGridTile<WordType, WordsNum>::TCBBitGridTile() = default;

template <std::integral WordType, uint32 WordsNum>
TCBBitGridTile<WordType, WordsNum>::TCBBitGridTile(bool const bValue)
{
	WordType const Word = bValue ? FullWordMask : 0;
	for (uint32 WordIndex = 0; WordIndex < WordsNum; ++WordIndex)
	{
		GridCells[WordIndex] = Word;
	}
}

template <std::integral WordType, uint32 WordsNum>
WordType & TCBBitGridTile<WordType, WordsNum>::operator [](uint32 const WordIndex)
{
	check(WordIndex < GetXSize());
	return GridCells[WordIndex];
}

template <std::integral WordType, uint32 WordsNum>
WordType const & TCBBitGridTile<WordType, WordsNum>::operator [](uint32 const WordIndex) const
{
	check(WordIndex < GetXSize());
	return GridCells[WordIndex];
}

template <std::integral WordType, uint32 WordsNum>
FBitReference TCBBitGridTile<WordType, WordsNum>::operator [](FUintPoint const Coord)
{
	CheckRange(Coord);
	return FBitReference(GridCells[Coord.X], 1 << Coord.Y);
}

template <std::integral WordType, uint32 WordsNum>
FConstBitReference const TCBBitGridTile<WordType, WordsNum>::operator [](FUintPoint const Coord) const
{
	CheckRange(Coord);
	return FConstBitReference(GridCells[Coord.X], 1 << Coord.Y);
}

template <std::integral WordType, uint32 WordsNum>
void TCBBitGridTile<WordType, WordsNum>::Serialize(FArchive & Archive)
{
	for (WordType & Word : GridCells)
	{
		Archive << Word;
	}
}

template <std::integral WordType, uint32 WordsNum>
bool TCBBitGridTile<WordType, WordsNum>::Contains(bool const bValue, uint32 const FromWordIndex, uint32 const ToWordIndex, WordType const Mask) const
{
	check(ToWordIndex <= WordsNum);
	WordType const Test = bValue ? 0 : FullWordMask;
	for (uint32 WordIndex = FromWordIndex; WordIndex < ToWordIndex; ++WordIndex)
	{
		if ((GridCells[WordIndex] & Mask) != (Test & Mask))
		{
			return true;
		}
	}
	return false;
}

template <std::integral WordType, uint32 WordsNum>
void TCBBitGridTile<WordType, WordsNum>::SetCells(bool const bValue, uint32 const FromWordIndex, uint32 const ToWordIndex, WordType const Mask)
{
	check(ToWordIndex <= WordsNum);
	if (bValue)
	{
		for (uint32 WordIndex = FromWordIndex; WordIndex < ToWordIndex; ++WordIndex)
		{
			GridCells[WordIndex] |= Mask;
		}
	}
	else
	{
		for (uint32 WordIndex = FromWordIndex; WordIndex < ToWordIndex; ++WordIndex)
		{
			GridCells[WordIndex] &= ~Mask;
		}
	}
}

template <std::integral WordType, uint32 WordsNum>
FUintPoint TCBBitGridTile<WordType, WordsNum>::GetSize()
{
	return FUintPoint{ GetXSize(), GetYSize() };
}

template <std::integral WordType, uint32 WordsNum>
constexpr uint32 TCBBitGridTile<WordType, WordsNum>::GetXSize()
{
	return WordsNum;
}

template <std::integral WordType, uint32 WordsNum>
constexpr uint32 TCBBitGridTile<WordType, WordsNum>::GetYSize()
{
	return sizeof(WordType) * CHAR_BIT;
}

template <std::integral WordType, uint32 WordsNum>
void TCBBitGridTile<WordType, WordsNum>::CheckRange(FUintPoint const Coord) const
{
	check((Coord.X < GetXSize()) && (Coord.Y < GetYSize()));
}
