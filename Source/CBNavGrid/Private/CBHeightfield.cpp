#include "CBHeightfield.h"

namespace
{
	enum class ECBAxis : int32
	{
		X,
		Y,
		Z
	};

	template <ECBAxis Axis>
	void GetMinMax(FVector const * const Verts, int32 const VertsNum, FVector::FReal & OutMin, FVector::FReal & OutMax)
	{
		int32 const AxisIndex = static_cast<int32>(Axis);
		OutMin = Verts[0][AxisIndex];
		OutMax = OutMin;
		for (int32 VertIndex = 1; VertIndex < VertsNum; ++VertIndex)
		{
			OutMin = FMath::Min(OutMin, Verts[VertIndex][AxisIndex]);
			OutMax = FMath::Max(OutMax, Verts[VertIndex][AxisIndex]);
		}
	}

	template <ECBAxis Axis>
	void SplitConvexPolygonByAAPlane(
		TArrayView<FVector const> const InPolygon,
		FVector * OutPolygon1, int32 & OutPolygon1VertsNum,
		FVector * OutPolygon2, int32 & OutPolygon2VertsNum,
		FVector::FReal const PlaneOffset)
	{
		OutPolygon1VertsNum = 0;
		OutPolygon2VertsNum = 0;
		int32 PrevVertIndex = InPolygon.Num() - 1;
		FVector::FReal PrevPlaneVertDist = PlaneOffset - InPolygon[PrevVertIndex][static_cast<int32>(Axis)];
		bool bPrevVertOnNegativeSide = PrevPlaneVertDist < 0.;
		for (int32 VertIndex = 0; VertIndex < InPolygon.Num(); ++VertIndex)
		{
			FVector const & Vert = InPolygon[VertIndex];
			FVector const & PrevVert = InPolygon[PrevVertIndex];
			FVector::FReal const PlaneVertDist = PlaneOffset - Vert[static_cast<int32>(Axis)];

			bool const bVertOnNegativeSide = PlaneVertDist < 0.;
			if (bVertOnNegativeSide == bPrevVertOnNegativeSide)
			{
				if (bVertOnNegativeSide)
				{
					OutPolygon2[OutPolygon2VertsNum++] = Vert;
				}
				else
				{
					OutPolygon1[OutPolygon1VertsNum++] = Vert;
					if (PlaneVertDist == 0.)
					{
						OutPolygon2[OutPolygon2VertsNum++] = Vert;
					}
				}
			}
			else
			{
				FVector const NewVert = PrevVert + (Vert - PrevVert) * (PrevPlaneVertDist / (PrevPlaneVertDist - PlaneVertDist));
				OutPolygon1[OutPolygon1VertsNum++] = NewVert;
				OutPolygon2[OutPolygon2VertsNum++] = NewVert;
				if (bVertOnNegativeSide)
				{
					OutPolygon2[OutPolygon2VertsNum++] = Vert;
				}
				else if (PlaneVertDist != 0.)
				{
					OutPolygon1[OutPolygon1VertsNum++] = Vert;
				}
			}

			PrevVertIndex = VertIndex;
			PrevPlaneVertDist = PlaneVertDist;
			bPrevVertOnNegativeSide = bVertOnNegativeSide;
		}
	}
} // namespace

FCBHeightfield::FCBHeightfield()
	: SpanPool(nullptr)
	, FreeSpanList(nullptr)
	, CellSize(0.)
	, SpanMergeTolerance(0.)
	, SpanPoolSize(0)
{
}

FCBHeightfield::FCBHeightfield(FIntRect const & InRect, float const InCellSize, float const InSpanMergeTolerance)
	: SpanPool(nullptr)
	, FreeSpanList(nullptr)
	, Rect(InRect)
	, CellSize(InCellSize)
	, SpanMergeTolerance(InSpanMergeTolerance)
	, SpanPoolSize(0)
{
	Cells.Init(nullptr, Rect.Area());
}

FCBHeightfield::~FCBHeightfield()
{
	DeleteSpanPool();
}

FCBHeightfield::FCBHeightfield(FCBHeightfield const & Other)
	: SpanPool(nullptr)
	, FreeSpanList(nullptr)
	, Rect(Other.Rect)
	, CellSize(Other.CellSize)
	, SpanMergeTolerance(Other.SpanMergeTolerance)
	, SpanPoolSize(0)
{
	Cells.AddUninitialized(Other.Cells.Num());
	CopySpans(Other);
}

FCBHeightfield::FCBHeightfield(FCBHeightfield && Other)
	: Cells(MoveTemp(Other.Cells))
	, SpanPool(Other.SpanPool)
	, FreeSpanList(Other.FreeSpanList)
	, Rect(Other.Rect)
	, CellSize(Other.CellSize)
	, SpanMergeTolerance(Other.SpanMergeTolerance)
	, SpanPoolSize(Other.SpanPoolSize)
{
	Other.SpanPool = nullptr;
	Other.FreeSpanList = nullptr;
	Other.Rect = {};
	Other.SpanPoolSize = 0;
}

FCBHeightfield & FCBHeightfield::operator=(FCBHeightfield const & Other)
{
	if (this != &Other)
	{
		EmptySpanPool();
		Rect = Other.Rect;
		CellSize = Other.CellSize;
		SpanMergeTolerance = Other.SpanMergeTolerance;
		Cells.SetNumUninitialized(Other.Cells.Num());
		CopySpans(Other);
	}
	return *this;
}

FCBHeightfield & FCBHeightfield::operator=(FCBHeightfield && Other)
{
	if (this != &Other)
	{
		DeleteSpanPool();
		Cells = MoveTemp(Other.Cells);
		SpanPool = Other.SpanPool;
		FreeSpanList = Other.FreeSpanList;
		Rect = Other.Rect;
		CellSize = Other.CellSize;
		SpanMergeTolerance = Other.SpanMergeTolerance;
		SpanPoolSize = Other.SpanPoolSize;

		Other.SpanPool = nullptr;
		Other.FreeSpanList = nullptr;
		Other.Rect = {};
		Other.SpanPoolSize = 0;
	}
	return *this;
}

void FCBHeightfield::Serialize(FArchive & Archive)
{
	Archive << Rect << CellSize << SpanMergeTolerance;

	if (Archive.IsLoading())
	{
		EmptySpanPool();
		Cells.Init(nullptr, Rect.Area());
	}

	for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
	{
		for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
		{
			FCBSpan * & FirstSpan = Cells[GetCellIndexUnsafe(FIntPoint{ X, Y })];

			int32 SpansNum = 0;
			for (FCBSpan const * Span = FirstSpan; Span; Span = Span->GetNext())
			{
				++SpansNum;
			}

			Archive << SpansNum;

			if (Archive.IsLoading())
			{
				FCBSpan ** SpanPtr = &FirstSpan;
				for (int32 SpanIndex = 0; SpanIndex < SpansNum; ++SpanIndex)
				{
					*SpanPtr = AllocateSpan();
					SpanPtr = &(*SpanPtr)->GetNext();
				}
				*SpanPtr = nullptr;
			}

			FCBSpan * Span = FirstSpan;
			for (int32 SpanIndex = 0; SpanIndex < SpansNum; ++SpanIndex)
			{
				Archive << Span->Min << Span->Max;
			}
		}
	}
}

FCBSpan const * FCBHeightfield::GetSpans(FIntPoint const Coord) const
{
	if (!Rect.Contains(Coord))
	{
		return nullptr;
	}
	return Cells[GetCellIndexUnsafe(Coord)];
}

void FCBHeightfield::RasterizeTriangles(TArrayView<FVector const> const Vertices, TArrayView<int32 const> const Indices)
{
	check(Indices.Num() % 3 == 0);
	FVector2d const Min{ Rect.Min.X * CellSize, Rect.Min.Y * CellSize };
	FVector2d const Max{ Rect.Max.X * CellSize, Rect.Max.Y * CellSize };
	FBox2d const HeightfieldBB{ Min, Max };
	float const InvertedCellSize = 1.f / CellSize;
	for (int32 IndexIndex = 0; IndexIndex < Indices.Num(); )
	{
		FVector const & Vertex0 = Vertices[Indices[IndexIndex++]];
		FVector const & Vertex1 = Vertices[Indices[IndexIndex++]];
		FVector const & Vertex2 = Vertices[Indices[IndexIndex++]];
		RasterizeTriangle(Vertex0, Vertex1, Vertex2, HeightfieldBB, InvertedCellSize);
	}
}

void FCBHeightfield::Clear()
{
	EmptySpanPool();
	Cells.Init(nullptr, Rect.Area());
}

void FCBHeightfield::Clear(FIntRect RectToClear)
{
	RectToClear.Clip(Rect);
	int32 CellIndex = GetCellIndexUnsafe(RectToClear.Min);
	int32 const IndexXIncrement = Rect.Height() - RectToClear.Height();
	for (int32 X = RectToClear.Min.X; X < RectToClear.Max.X; ++X, CellIndex += IndexXIncrement)
	{
		for (int32 Y = RectToClear.Min.Y; Y < RectToClear.Max.Y; ++Y, ++CellIndex)
		{
			FreeCellUnsafe(CellIndex);
		}
	}
}

void FCBHeightfield::Shrink(int32 const MaxSpansPerCell)
{
	if (MaxSpansPerCell <= 0)
	{
		*this = FCBHeightfield{ *this };
		return;
	}
	FCBHeightfield CompactHeightfield{ Rect, CellSize, SpanMergeTolerance };
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		FCBSpan const * Span = Cells[CellIndex];
		if (!Span)
		{
			continue;
		}

		FCBSpan * CompactHeightfieldSpan = CompactHeightfield.AllocateSpan(Span->Min, Span->Max);
		CompactHeightfield.Cells[CellIndex] = CompactHeightfieldSpan;
		Span = Span->GetNext();
		for (int32 SpanIndex = 1; SpanIndex < MaxSpansPerCell && Span; ++SpanIndex, Span = Span->GetNext())
		{
			CompactHeightfieldSpan->GetNext() = CompactHeightfield.AllocateSpan(Span->Min, Span->Max);
			CompactHeightfieldSpan = CompactHeightfieldSpan->GetNext();
		}
	}
	*this = MoveTemp(CompactHeightfield);
}

FCBSpan * FCBHeightfield::AllocateSpan()
{
	FCBSpan * AllocatedSpan;
	if (FreeSpanList)
	{
		AllocatedSpan = FreeSpanList;
		FreeSpanList = FreeSpanList->GetNext();
	}
	else
	{
		uint32 const NewSpanIndex = SpanPoolSize % SpansPerPoolNum;
		++SpanPoolSize;
		// Last pool is filled.
		if (NewSpanIndex == 0)
		{
			FSpanPool * NewPool = new FSpanPool;
			NewPool->Next = SpanPool;
			SpanPool = NewPool;
		}
		AllocatedSpan = &SpanPool->Spans[NewSpanIndex];
	}
	return AllocatedSpan;
}

FCBSpan * FCBHeightfield::AllocateSpan(float const Min, float const Max, FCBSpan * const Next)
{
	FCBSpan * const Span = AllocateSpan();
	Span->Init(Min, Max, Next);
	return Span;
}

void FCBHeightfield::FreeSpan(FCBSpan * Span)
{
	check(Span);
	Span->GetNext() = FreeSpanList;
	FreeSpanList = Span;
}

void FCBHeightfield::FreeCellUnsafe(int32 const Index)
{
	FCBSpan * Span = Cells[Index];
	if (!Span)
	{
		return;
	}
	while (Span->GetNext())
	{
		Span = Span->GetNext();
	}
	Span->GetNext() = FreeSpanList;
	FreeSpanList = Cells[Index];
	Cells[Index] = nullptr;
}

void FCBHeightfield::RasterizeTriangle(FVector const & Vertex0, FVector const & Vertex1, FVector const & Vertex2, FBox2d const & HeightfieldBB, float const InvertedCellSize)
{
	FBox2d TriangleBB{};
	TriangleBB += static_cast<FVector2d>(Vertex0);
	TriangleBB += static_cast<FVector2d>(Vertex1);
	TriangleBB += static_cast<FVector2d>(Vertex2);

	if (!HeightfieldBB.Intersect(TriangleBB))
	{
		return;
	}

	int32 const MaxPolygonVerts = 7;
	FVector VertsBuffer[MaxPolygonVerts * 3];
	FVector * InPolygon = VertsBuffer;
	FVector * RowPolygon = InPolygon + MaxPolygonVerts;
	FVector * ResidualPolygon = RowPolygon + MaxPolygonVerts;

	InPolygon[0] = Vertex0;
	InPolygon[1] = Vertex1;
	InPolygon[2] = Vertex2;
	int32 InVertsNum = 3;

	int32 StartX = FMath::RoundToNegativeInfinity(TriangleBB.Min.X * InvertedCellSize);
	int32 EndX = FMath::RoundToNegativeInfinity(TriangleBB.Max.X * InvertedCellSize);
	StartX = FMath::Clamp(StartX, Rect.Min.X - 1, Rect.Max.X - 1);
	EndX = FMath::Clamp(EndX, Rect.Min.X, Rect.Max.X - 1);
	for (int32 X = StartX; X <= EndX; ++X)
	{
		int32 RowVertsNum;
		TArrayView<FVector const> const InPolygonView{ InPolygon, InVertsNum };
		FVector::FReal const NextRowX = (X + 1) * CellSize;
		SplitConvexPolygonByAAPlane<ECBAxis::X>(InPolygonView, RowPolygon, RowVertsNum, ResidualPolygon, InVertsNum, NextRowX);
		Swap(InPolygon, ResidualPolygon);
		
		if (RowVertsNum < 3 || X < Rect.Min.X)
		{
			continue;
		}

		FVector::FReal MinY;
		FVector::FReal MaxY;
		GetMinMax<ECBAxis::Y>(RowPolygon, RowVertsNum, MinY, MaxY);
		int32 StartY = FMath::RoundToNegativeInfinity(MinY * InvertedCellSize);
		int32 EndY = FMath::RoundToNegativeInfinity(MaxY * InvertedCellSize);
		if (StartY >= Rect.Max.Y || EndY < Rect.Min.Y)
		{
			continue;
		}
		StartY = FMath::Clamp(StartY, Rect.Min.Y - 1, Rect.Max.Y - 1);
		EndY = FMath::Clamp(EndY, Rect.Min.Y, Rect.Max.Y - 1);

		for (int32 Y = StartY; Y <= EndY; ++Y)
		{
			int32 CellVertsNum;
			FVector CellVertsBuffer[MaxPolygonVerts];
			TArrayView<FVector const> const RowPolygonView{ RowPolygon, RowVertsNum };
			FVector::FReal const NextCellY = (Y + 1) * CellSize;
			SplitConvexPolygonByAAPlane<ECBAxis::Y>(RowPolygonView, CellVertsBuffer, CellVertsNum, ResidualPolygon, RowVertsNum, NextCellY);
			Swap(RowPolygon, ResidualPolygon);

			if (CellVertsNum < 3 || Y < Rect.Min.Y)
			{
				continue;
			}

			FVector::FReal MinZ;
			FVector::FReal MaxZ;
			GetMinMax<ECBAxis::Z>(CellVertsBuffer, CellVertsNum, MinZ, MaxZ);
			AddSpanUnsafe(FIntPoint{ X, Y }, MinZ, MaxZ);
		}
	}
}

void FCBHeightfield::CopySpans(FCBHeightfield const & Other)
{
	check(Cells.Num() == Other.Cells.Num());
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		FCBSpan const * OtherSpan = Other.Cells[CellIndex];
		if (!OtherSpan)
		{
			Cells[CellIndex] = nullptr;
			continue;
		}

		FCBSpan * Span = AllocateSpan(OtherSpan->Min, OtherSpan->Max);
		Cells[CellIndex] = Span;
		OtherSpan = OtherSpan->GetNext();
		while (OtherSpan)
		{
			Span->GetNext() = AllocateSpan(OtherSpan->Min, OtherSpan->Max);
			Span = Span->GetNext();
			OtherSpan = OtherSpan->GetNext();
		}
	}
}

void FCBHeightfield::DeleteSpanPool()
{
	while (SpanPool)
	{
		FSpanPool * const PoolToDelete = SpanPool;
		SpanPool = SpanPool->Next;
		delete PoolToDelete;
	}
}

void FCBHeightfield::EmptySpanPool()
{
	DeleteSpanPool();
	FreeSpanList = nullptr;
	SpanPoolSize = 0;
}

int32 FCBHeightfield::GetCellIndexUnsafe(FIntPoint Coord) const
{
	CheckRange(Coord);
	Coord -= Rect.Min;
	return Coord.X * Rect.Height() + Coord.Y;
}

void FCBHeightfield::AddSpanUnsafe(FIntPoint const Coord, float Min, float Max)
{
	check(Min <= Max);
	int32 const CellIndex = GetCellIndexUnsafe(Coord);
	FCBSpan * PreviousSpan = nullptr;
	FCBSpan * CurrentSpan = Cells[CellIndex];

	// Skips all spans that are completely higher. So PreviousSpan is last which is completely higher.
	while (CurrentSpan && Max + SpanMergeTolerance < CurrentSpan->Min)
	{
		PreviousSpan = CurrentSpan;
		CurrentSpan = CurrentSpan->GetNext();
	}

	if (CurrentSpan && CurrentSpan->Max + SpanMergeTolerance > Min)
	{
		// Merges intersecting spans to new one.
		Max = FMath::Max(Max, CurrentSpan->Max);
		FCBSpan * LastMergedSpan = CurrentSpan;
		CurrentSpan = CurrentSpan->GetNext();
		while (CurrentSpan && CurrentSpan->Max + SpanMergeTolerance > Min)
		{
			FreeSpan(LastMergedSpan);
			LastMergedSpan = CurrentSpan;
			CurrentSpan = CurrentSpan->GetNext();
		}
		Min = FMath::Min(Min, LastMergedSpan->Min);
		FreeSpan(LastMergedSpan);
	}
	// Now CurrentSpan is the first which is completely lower.

	FCBSpan * const NewSpan = AllocateSpan(Min, Max, CurrentSpan);
	if (PreviousSpan)
	{
		PreviousSpan->GetNext() = NewSpan;
	}
	else
	{
		Cells[CellIndex] = NewSpan;
	}
}

void FCBHeightfield::CheckRange(FIntPoint const Coord) const
{
	check(Rect.Min.X <= Coord.X && Rect.Min.Y <= Coord.Y && Coord.X < Rect.Max.X && Coord.Y < Rect.Max.Y);
}
