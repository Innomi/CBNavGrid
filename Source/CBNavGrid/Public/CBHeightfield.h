#pragma once

#include "CoreMinimal.h"

class CBNAVGRID_API FCBSpan
{
public:
	FORCEINLINE void Init(float const InMin, float const InMax, FCBSpan * const InNext = nullptr);
	FORCEINLINE FCBSpan const * const & GetNext() const;
	FORCEINLINE FCBSpan * & GetNext();

	float Min;
	float Max;

private:
	FCBSpan * Next;
};

class CBNAVGRID_API FCBHeightfield
{
public:
	FCBHeightfield();
	explicit FCBHeightfield(FIntRect const & InRect, float const InCellSize, float const InSpanMergeTolerance = UE_DOUBLE_SMALL_NUMBER);
	~FCBHeightfield();

	FCBHeightfield(FCBHeightfield const & Other);
	FCBHeightfield(FCBHeightfield && Other);
	FCBHeightfield & operator =(FCBHeightfield const & Other);
	FCBHeightfield & operator =(FCBHeightfield && Other);

	void Serialize(FArchive & Archive);
	FCBSpan const * GetSpans(FIntPoint const Coord) const;
	void RasterizeTriangles(TArrayView<FVector const> const Vertices, TArrayView<int32 const> const Indices);
	void Clear();
	void Clear(FIntRect RectToClear);
	
	/**
	 * Leaves heightfield with empty FreeSpanList and MaxSpansPerCell highest spans in each cell.
	 * If MaxSpansPerCell is less than or equal to zero preserves all spans.
	 */
	void Shrink(int32 const MaxSpansPerCell = 0);

private:
	/** 
	 * Power of two to make division faster by applying bit masks.
	 * All modern compilers are capable of such optimization, no need to write it manually.
	 */
	static constexpr uint32 SpansPerPoolNum = 1 << 11;

	struct FSpanPool
	{
		FCBSpan Spans[SpansPerPoolNum];
		FSpanPool * Next;
	};

	FCBSpan * AllocateSpan();
	FCBSpan * AllocateSpan(float const Min, float const Max, FCBSpan * const Next = nullptr);
	void FreeSpan(FCBSpan * Span);
	void FreeCellUnsafe(int32 const Index);
	void RasterizeTriangle(FVector const & Vertex0, FVector const & Vertex1, FVector const & Vertex2, FBox2d const & HeightfieldBB, float const InvertedCellSize);
	
	/** Assumes Cells.Num() == Other.Cells.Num(). */
	void CopySpans(FCBHeightfield const & Other);

	/** Deletes all allocated span pools and sets SpanPool to nullptr. */
	void DeleteSpanPool();

	/** Calls DeleteSpanPool() and reset associated with span pool variables. */
	void EmptySpanPool();
	int32 GetCellIndexUnsafe(FIntPoint Coord) const;
	void AddSpanUnsafe(FIntPoint const Coord, float Min, float Max);

	void CheckRange(FIntPoint const Coord) const;

	TArray<FCBSpan *> Cells;
	FSpanPool * SpanPool;
	FCBSpan * FreeSpanList;
	FIntRect Rect;
	float CellSize;
	float SpanMergeTolerance;
	uint32 SpanPoolSize;
};

FORCEINLINE FArchive & operator <<(FArchive & Archive, FCBHeightfield & Heightfield)
{
	Heightfield.Serialize(Archive);
	return Archive;
}

void FCBSpan::Init(float const InMin, float const InMax, FCBSpan * const InNext)
{
	check(InMin <= InMax);
	Min = InMin;
	Max = InMax;
	Next = InNext;
}

FCBSpan const * const & FCBSpan::GetNext() const
{
	return Next;
}

FCBSpan * & FCBSpan::GetNext()
{
	return Next;
}