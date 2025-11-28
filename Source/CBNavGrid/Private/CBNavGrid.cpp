#include "CBNavGrid.h"
#include "CBGridUtilities.h"
#include "CBHeightfield.h"
#include "CBNavGridAStar.h"
#include "CBNavGridCustomVersion.h"
#include "CBNavGridGenerator.h"
#include "CBNavGridLayer.h"
#include "CBNavGridPath.h"
#include "CBNavGridQueryFilter.h"
#include "CBNavGridRenderingComponent.h"
#include "GeomUtils.h"
#include "GraphAStar.h"
#include "NavAreas/NavArea_Default.h"
#include "NavAreas/NavArea_Null.h"
#include "NavigationSystem.h"

#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif

#include <type_traits>

namespace
{
	bool IsValidRect(FIntRect const & Rect)
	{
		return Rect.Min.X <= Rect.Max.X && Rect.Min.Y <= Rect.Max.Y;
	}

	void InvalidateRect(FIntRect & Rect)
	{
		Rect.Max = Rect.Min - FIntPoint{ 1, 0 };
	}

	FBox GetBoundingBox(FIntRect const & BoundingGridRect, FVector::FReal const CellSize, FVector::FReal const MinZ, FVector::FReal const MaxZ)
	{
		FVector const Min{ BoundingGridRect.Min.X * CellSize, BoundingGridRect.Min.Y * CellSize, MinZ };
		FVector const Max{ BoundingGridRect.Max.X * CellSize, BoundingGridRect.Max.Y * CellSize, MaxZ };
		return FBox{ Min, Max };
	}

	bool HaveCommonBorder(FIntRect const & Rect1, FIntRect const & Rect2)
	{
		return Rect1.Min.X == Rect2.Min.X || Rect1.Min.Y == Rect1.Min.Y
			|| Rect1.Max.X == Rect1.Max.X || Rect1.Max.Y == Rect1.Max.Y;
	}

	bool IntersectSegmentPoly2D(FVector2d const & Start2d, FVector2d const & End2d, TConstArrayView<FVector> ConvexPolygon)
	{
		FVector::FReal TMin, TMax;
		int32 SegMin, SegMax;
		FVector Start{ Start2d, 0. };
		FVector End{ End2d, 0. };
		return UE::AI::IntersectSegmentPoly2D(Start, End, ConvexPolygon, TMin, TMax, SegMin, SegMax);
	}

	void GetEdgeCoords2D(FIntPoint const CellCoord, ECBGridDirection const GridDirection, float const CellSize, FVector2d & OutEdgeStart, FVector2d & OutEdgeEnd)
	{
		FIntPoint EdgeStartCoord, EdgeEndCoord;
		CBGridUtilities::GetEdgeCoordsChecked(CellCoord, GridDirection, EdgeStartCoord, EdgeEndCoord);
		OutEdgeStart = FVector2d{ EdgeStartCoord.X * CellSize, EdgeStartCoord.Y * CellSize };
		OutEdgeEnd = FVector2d{ EdgeEndCoord.X * CellSize, EdgeEndCoord.Y * CellSize };
	}
} // namespace


FCBNavGridDebugSettings::FCBNavGridDebugSettings()
	: DrawDistance(15000.f)
	, DrawOffset(30.f)
	, bDrawCellEdges(false)
	, bDrawFilledCells(true)
	, bDrawNavGridEdges(true)
	, bDrawTileEdges(false)
{
}

ACBNavGrid::ACBNavGrid()
	: TileSize(128, 128)
	, GridCellSize(100.f)
	, MaxNavigableCellHeightsDifference(50.f)
	, MinZ(-1e9f)
	, MaxZ(1e9f)
	, DefaultMaxSearchNodes(2048)
	, DefaultHeuristicScale(1.00001f)
	, DefaultAxiswiseHeuristicScale(1.f, 1.00001f)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FindPathImplementation = FindPath;
		FindHierarchicalPathImplementation = FindPath;
		TestPathImplementation = TestPath;
		TestHierarchicalPathImplementation = TestPath;
		RaycastImplementationWithAdditionalResults = Raycast;

		SupportedAreas.Add(FSupportedAreaData{ UNavArea_Default::StaticClass(), 0 });
		SupportedAreas.Add(FSupportedAreaData{ UNavArea_Null::StaticClass(), 1 });
	}
}

ACBNavGrid::~ACBNavGrid() = default;

void ACBNavGrid::PostInitProperties()
{
	Super::PostInitProperties();

	if (!HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad))
	{
		RecreateDefaultFilter();
	}
}

void ACBNavGrid::PostLoad()
{
	Super::PostLoad();

	RecreateDefaultFilter();
}

void ACBNavGrid::Serialize(FArchive & Archive)
{
	Super::Serialize(Archive);

	Archive.UsingCustomVersion(FCBNavGridCustomVersion::GUID);

	if (!Archive.IsTransacting())
	{
		Archive << Tiles;
	}
}

void ACBNavGrid::CleanUp()
{
	Super::CleanUp();

	if (NavDataGenerator.IsValid())
	{
		NavDataGenerator->CancelBuild();
		NavDataGenerator.Reset();
	}
	Tiles.Empty();
	BoundingGridRect = FIntRect{};
}

bool ACBNavGrid::NeedsRebuild() const
{
	return NavDataGenerator.IsValid() && NavDataGenerator->GetNumRemaningBuildTasks() > 0;
}

bool ACBNavGrid::SupportsRuntimeGeneration() const
{
	return (RuntimeGeneration != ERuntimeGenerationType::Static);
}

void ACBNavGrid::ConditionalConstructGenerator()
{
	if (NavDataGenerator.IsValid())
	{
		NavDataGenerator->CancelBuild();
		NavDataGenerator.Reset();
	}

	check(GetWorld());
	if (SupportsRuntimeGeneration() || !GetWorld()->IsGameWorld())
	{
		NavDataGenerator = MakeShared<FCBNavGridGenerator, ESPMode::ThreadSafe>(*this);
		StaticCastSharedPtr<FCBNavGridGenerator>(NavDataGenerator)->Init();
	}
}

void ACBNavGrid::RebuildAll()
{
	Tiles.Empty();
	BoundingGridRect = FIntRect{};

	Super::RebuildAll();
}

void ACBNavGrid::BatchRaycast(TArray<FNavigationRaycastWork> & Workload, FSharedConstNavQueryFilter QueryFilter, UObject const * Querier) const
{
	for (FNavigationRaycastWork & Work : Workload)
	{
		Work.bDidHit = Raycast(Work.RayStart, Work.RayEnd, Work.HitLocation, Work.bIsRayEndInCorridor);
	}
}

bool ACBNavGrid::FindMoveAlongSurface(FNavLocation const & StartLocation, FVector const & TargetPosition, FNavLocation & OutLocation, FSharedConstNavQueryFilter const Filter, UObject const * const Querier) const
{
	if (!DoesNodeContainLocation(StartLocation.NodeRef, StartLocation.Location))
	{
		return false;
	}

	bool bDummyIsRayEndInCorridor;
	Raycast(StartLocation.Location, TargetPosition, OutLocation, bDummyIsRayEndInCorridor);
	return true;
}

bool ACBNavGrid::FindOverlappingEdges(FNavLocation const & StartLocation, TConstArrayView<FVector> const ConvexPolygon, TArray<FVector> & OutEdges, FSharedConstNavQueryFilter const Filter, UObject const * const Querier) const
{
	if (!StartLocation.HasNodeRef())
	{
		return false;
	}

	FIntPoint const StartGridCoord = GetGridCoord(StartLocation.NodeRef);
	FCBNavGridLayer const * Tile = GetTileNavigationData(GetTileCoord(StartGridCoord)).Get();
	if (!Tile || Tile->IsCellOccupied(StartGridCoord))
	{
		return false;
	}

	FindOverlappingEdgesUnsafe({ StartGridCoord }, ConvexPolygon, OutEdges);

	return true;
}

bool ACBNavGrid::GetPathSegmentBoundaryEdges(FNavigationPath const & Path, FNavPathPoint const & StartPoint, FNavPathPoint const & EndPoint, TConstArrayView<FVector> const SearchArea, TArray<FVector> & OutEdges, float const MaxAreaEnterCost, FSharedConstNavQueryFilter const Filter, UObject const * const Querier) const
{
	FCBNavGridPath const * const NavGridPath = Path.CastPath<FCBNavGridPath const>();
	if (!StartPoint.HasNodeRef() || !EndPoint.HasNodeRef() || !NavGridPath)
	{
		return false;
	}

	TArray<FNavPathPoint> const & PathPoints = NavGridPath->GetPathPoints();
	int32 StartPointIndex = 0;
	for (FNavPathPoint const & PathPoint : PathPoints)
	{
		if (PathPoint.NodeRef == StartPoint.NodeRef)
		{
			break;
		}
		++StartPointIndex;
	}

	if (StartPointIndex == PathPoints.Num())
	{
		return false;
	}

	TArray<FIntPoint> PathSegmentGridCells;
	PathSegmentGridCells.Reserve(PathPoints.Num() - StartPointIndex);
	for (int32 PointIndex = StartPointIndex; PointIndex < PathPoints.Num(); ++PointIndex)
	{
		FNavPathPoint const & PathPoint = PathPoints[PointIndex];
		PathSegmentGridCells.Add(GetGridCoord(PathPoint.NodeRef));
		if (PathPoint.NodeRef == EndPoint.NodeRef)
		{
			break;
		}
	}

	FindOverlappingEdgesUnsafe(PathSegmentGridCells, SearchArea, OutEdges);

	return true;
}

FNavLocation ACBNavGrid::GetRandomPoint(FSharedConstNavQueryFilter Filter, UObject const * Querier) const
{
	FNavLocation NavLocation;
	if (Tiles.IsEmpty())
	{
		return NavLocation;
	}

	// Chooses random tile assuming they have almost equal navigable area.
	FCBNavGridLayer const * RandomTile = nullptr;
	{
		int32 TileIndex = FMath::RandHelper(Tiles.Num());
		for (TPair<FIntPoint, FTileData> const & TileEntry : Tiles)
		{
			if (TileIndex == 0)
			{
				RandomTile = TileEntry.Value.NavigationData.Get();
				break;
			}
			--TileIndex;
		}
	}
	check(RandomTile);

	FIntPoint RandomCellCoord{};
	{
		int32 NavigableCellsNum = 0;
		FIntRect const & TileGridRect = RandomTile->GetGridRect();
		for (int32 X = TileGridRect.Min.X; X < TileGridRect.Max.X; ++X)
		{
			for (int32 Y = TileGridRect.Min.Y; Y < TileGridRect.Max.Y; ++Y)
			{
				FIntPoint const CellCoord{ X, Y };
				if (RandomTile->IsCellOccupied(CellCoord))
				{
					continue;
				}
				++NavigableCellsNum;
				if (FMath::RandHelper(NavigableCellsNum) == 0)
				{
					RandomCellCoord = CellCoord;
				}
			}
		}

		if (NavigableCellsNum == 0)
		{
			return NavLocation;
		}
	}

	NavLocation.Location.X = (RandomCellCoord.X + FMath::FRand()) * GridCellSize;
	NavLocation.Location.Y = (RandomCellCoord.Y + FMath::FRand()) * GridCellSize;
	NavLocation.Location.Z = RandomTile->GetCellHeight(RandomCellCoord);
	NavLocation.NodeRef = GetNodeRef(RandomCellCoord);

	return NavLocation;
}

bool ACBNavGrid::GetRandomReachablePointInRadius(FVector const & Origin, float const Radius, FNavLocation & OutResult, FSharedConstNavQueryFilter Filter, UObject const * Querier) const
{
	FIntPoint RandomCellCoord{};
	{
		FVector ProjectedOrigin;
		FIntPoint OriginGridCoord;
		
		{
			FVector Extent = GetDefaultQueryExtent();
			Extent.Z = TNumericLimits<FVector::FReal>::Max();
			if (!ProjectPoint(Origin, Extent, &ProjectedOrigin, &OriginGridCoord))
			{
				return false;
			}
		}

		TSet<FIntPoint> VisitedSet;
		TArray<FIntPoint> OpenList;
		{
			int32 const ToReserveNum = FMath::Square(FMath::CeilToInt(2.f * Radius / GridCellSize));
			OpenList.Reserve(ToReserveNum);
			VisitedSet.Reserve(ToReserveNum);
		}
		OpenList.Add(OriginGridCoord);
		VisitedSet.Add(OriginGridCoord);
		RandomCellCoord = OriginGridCoord;

		float const SqRadius = FMath::Square(Radius);
		FCBNavGridLayer const * Tile = GetTileNavigationData(GetTileCoord(OriginGridCoord)).Get();
		check(Tile);

		while (!OpenList.IsEmpty())
		{
			FIntPoint const CellCoord = OpenList.Pop(EAllowShrinking::No);
			for (ECBGridDirection const GridDirection : TEnumRange<ECBGridDirection>())
			{
				FIntPoint const AdjacentCellCoord = CBGridUtilities::GetAdjacentCoordChecked(CellCoord, GridDirection);
				FVector2d const AdjacentCellCenter = CBGridUtilities::GetGridCellCenter(AdjacentCellCoord, GridCellSize);
				float const AdjacentCellCenterToOriginSqDist = FVector2d::DistSquared(AdjacentCellCenter, static_cast<FVector2d>(ProjectedOrigin));
				if (SqRadius < AdjacentCellCenterToOriginSqDist || VisitedSet.Contains(AdjacentCellCoord))
				{
					continue;
				}
				if (!Tile->IsInGrid(AdjacentCellCoord))
				{
					FCBNavGridLayer const * NewTile = GetTileNavigationData(GetTileCoord(AdjacentCellCoord)).Get();
					if (!NewTile)
					{
						continue;
					}
					Tile = NewTile;
				}
				if (!Tile->IsCellOccupied(AdjacentCellCoord))
				{
					OpenList.Push(AdjacentCellCoord);
					VisitedSet.Add(AdjacentCellCoord);
					if (FMath::RandHelper(VisitedSet.Num()) == 0)
					{
						RandomCellCoord = AdjacentCellCoord;
					}
				}
			}
		}
	}

	OutResult.Location.X = (RandomCellCoord.X + FMath::FRand()) * GridCellSize;
	OutResult.Location.Y = (RandomCellCoord.Y + FMath::FRand()) * GridCellSize;
	OutResult.Location.Z = GetTileNavigationData(GetTileCoord(RandomCellCoord))->GetCellHeight(RandomCellCoord);
	OutResult.NodeRef = GetNodeRef(RandomCellCoord);

	return true;
}

bool ACBNavGrid::GetRandomPointInNavigableRadius(FVector const & Origin, float const Radius, FNavLocation & OutResult, FSharedConstNavQueryFilter Filter, UObject const * Querier) const
{
	{
		FVector2d const RandomOffset = FMath::RandPointInCircle(Radius);
		FVector const RandomPointInRadius{ Origin.X + RandomOffset.X, Origin.Y + RandomOffset.Y, 0. };
		FVector Extent = GetDefaultQueryExtent();
		Extent.Z = TNumericLimits<FVector::FReal>::Max();
		FVector ProjectedPoint;
		FIntPoint GridCoord;
		if (ProjectPoint(RandomPointInRadius, Extent, &ProjectedPoint, &GridCoord))
		{
			OutResult.Location = ProjectedPoint;
			OutResult.NodeRef = GetNodeRef(GridCoord);
			return true;
		}
	}

	FIntPoint RandomCellCoord{};
	{
		int32 NavigableCellsNum = 0;
		double const SqRadius = FMath::Square(Radius);
		FVector2d const Min{ Origin.X - Radius, Origin.Y - Radius };
		FVector2d const Max{ Origin.X + Radius, Origin.Y + Radius };
		FIntRect const GridRect{ CBGridUtilities::GetGridCellCoord(Min, GridCellSize), CBGridUtilities::GetGridCellCoord(Max, GridCellSize) + FIntPoint{ 1, 1 } };
		FIntRect const TileRect = CBGridUtilities::GetTileRect(GridRect, TileSize);
		for (int32 TileX = TileRect.Min.X; TileX < TileRect.Max.X; ++TileX)
		{
			for (int32 TileY = TileRect.Min.Y; TileY < TileRect.Max.Y; ++TileY)
			{
				FCBNavGridLayer const * NavGridLayer = GetTileNavigationData(FIntPoint{ TileX, TileY }).Get();
				if (!NavGridLayer)
				{
					continue;
				}
				FIntRect const ClippedGridRect = NavGridLayer->ClipWithGridRect(GridRect);
				for (int32 X = ClippedGridRect.Min.X; X < ClippedGridRect.Max.X; ++X)
				{
					for (int32 Y = ClippedGridRect.Min.Y; Y < ClippedGridRect.Max.Y; ++Y)
					{
						FIntPoint const CellCoord{ X, Y };
						if (NavGridLayer->IsCellOccupied(CellCoord))
						{
							continue;
						}
						FVector2d const CellCenter = CBGridUtilities::GetGridCellCenter(CellCoord, GridCellSize);
						double const SqDistToCellCenter = FVector2d::DistSquared(static_cast<FVector2d>(Origin), CellCenter);
						if (SqDistToCellCenter > SqRadius)
						{
							continue;
						}
						++NavigableCellsNum;
						if (FMath::RandHelper(NavigableCellsNum) == 0)
						{
							RandomCellCoord = CellCoord;
						}
					}
				}
			}
		}

		if (NavigableCellsNum == 0)
		{
			return false;
		}
	}

	OutResult.Location.X = (RandomCellCoord.X + FMath::FRand()) * GridCellSize;
	OutResult.Location.Y = (RandomCellCoord.Y + FMath::FRand()) * GridCellSize;
	OutResult.Location.Z = GetTileNavigationData(GetTileCoord(RandomCellCoord))->GetCellHeight(RandomCellCoord);
	OutResult.NodeRef = GetNodeRef(RandomCellCoord);

	return true;
}

bool ACBNavGrid::ProjectPoint(FVector const & Point, FNavLocation & OutLocation, FVector const & Extent, FSharedConstNavQueryFilter Filter, UObject const * Querier) const
{
	FIntPoint GridCoord;
	bool const bResult = ProjectPoint(Point, Extent, &OutLocation.Location, &GridCoord);
	if (bResult)
	{
		OutLocation.NodeRef = GetNodeRef(GridCoord);
	}
	return bResult;
}

bool ACBNavGrid::IsNodeRefValid(NavNodeRef const NodeRef) const
{
	if (NodeRef == INVALID_NAVNODEREF)
	{
		return false;
	}
	FIntPoint const GridCoord = GetGridCoord(NodeRef);
	FCBNavGridLayer const * NavGridLayer = GetTileNavigationData(GetTileCoord(GridCoord)).Get();
	if (NavGridLayer && !NavGridLayer->IsCellOccupied(GridCoord))
	{
		return true;
	}
	return false;
}

void ACBNavGrid::BatchProjectPoints(TArray<FNavigationProjectionWork> & Workload, FVector const & Extent, FSharedConstNavQueryFilter Filter, UObject const * Querier) const
{
	for (FNavigationProjectionWork & Work : Workload)
	{
		if (Work.bIsValid)
		{
			Work.bResult = ProjectPoint(Work.Point, Work.OutLocation, Extent, Filter, Querier);
		}
	}
}

void ACBNavGrid::BatchProjectPoints(TArray<FNavigationProjectionWork> & Workload, FSharedConstNavQueryFilter Filter, UObject const * Querier) const
{
	for (FNavigationProjectionWork & Work : Workload)
	{
		if (Work.bIsValid)
		{
			Work.bResult = ProjectPoint(Work.Point, Work.OutLocation, Work.ProjectionLimit.GetExtent(), Filter, Querier);
		}
	}
}

ENavigationQueryResult::Type ACBNavGrid::CalcPathCost(FVector const & PathStart, FVector const & PathEnd, FVector::FReal & OutPathCost, FSharedConstNavQueryFilter const QueryFilter, UObject const * const Querier) const
{
	FVector::FReal DummyPathLength = 0.f;
	return CalcPathLengthAndCost(PathStart, PathEnd, DummyPathLength, OutPathCost, QueryFilter, Querier);
}

ENavigationQueryResult::Type ACBNavGrid::CalcPathLength(FVector const & PathStart, FVector const & PathEnd, FVector::FReal & OutPathLength, FSharedConstNavQueryFilter const QueryFilter, UObject const * const Querier) const
{
	FVector::FReal DummyPathCost = 0.f;
	return CalcPathLengthAndCost(PathStart, PathEnd, OutPathLength, DummyPathCost, QueryFilter, Querier);
}

ENavigationQueryResult::Type ACBNavGrid::CalcPathLengthAndCost(FVector const & PathStart, FVector const & PathEnd, FVector::FReal & OutPathLength, FVector::FReal & OutPathCost, FSharedConstNavQueryFilter const QueryFilter, UObject const * const Querier) const
{
	FCBNavGridPath Path;
	FPathFindingQuery Query(Querier, *this, PathStart, PathEnd, QueryFilter);
	ENavigationQueryResult::Type Result = FindPath(Path, Query);

	if (Result == ENavigationQueryResult::Success || (Result == ENavigationQueryResult::Fail && Path.IsPartial()))
	{
		OutPathLength = Path.GetLength();
		OutPathCost = Path.GetCost();
	}

	return Result;
}

bool ACBNavGrid::DoesNodeContainLocation(NavNodeRef const NodeRef, FVector const & WorldSpaceLocation) const
{
	if (NodeRef == INVALID_NAVNODEREF)
	{
		return false;
	}
	FIntPoint const GridCoord = GetGridCoord(NodeRef);
	FCBNavGridLayer const * NavGridLayer = GetTileNavigationData(GetTileCoord(GridCoord)).Get();
	if (!NavGridLayer || NavGridLayer->IsCellOccupied(GridCoord))
	{
		return false;
	}
	FVector2d const CellCornerCoord{ GridCoord.X * GridCellSize, GridCoord.Y * GridCellSize };
	return FMath::IsWithin(WorldSpaceLocation.X, CellCornerCoord.X, CellCornerCoord.X + GridCellSize) &&
		FMath::IsWithin(WorldSpaceLocation.Y, CellCornerCoord.Y, CellCornerCoord.Y + GridCellSize) &&
		FMath::IsNearlyEqual(WorldSpaceLocation.Z, NavGridLayer->GetCellHeight(GridCoord));
}

UPrimitiveComponent * ACBNavGrid::ConstructRenderingComponent()
{
	return NewObject<UCBNavGridRenderingComponent>(this, TEXT("CBNavGridRendering"), RF_Transient);
}

FBox ACBNavGrid::GetBounds() const
{
	if (!IsValidRect(BoundingGridRect))
	{
		BoundingGridRect = CalculateBoundingGridRect();
	}
	return GetBoundingBox(BoundingGridRect, GridCellSize, MinZ, MaxZ);
}

int32 ACBNavGrid::GetMaxSupportedAreas() const
{
	return 2;
}

#if WITH_EDITOR
void ACBNavGrid::PostEditChangeChainProperty(FPropertyChangedChainEvent & PropertyChangedChainEvent)
{
	if (FEditPropertyChain::TDoubleLinkedListNode const * const PropertyNode = PropertyChangedChainEvent.PropertyChain.GetActiveMemberNode())
	{
		HandleChangePropertyFromCategory(FObjectEditorUtils::GetCategoryFName(PropertyNode->GetValue()));
	}

	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);
}

void ACBNavGrid::PostEditChangeProperty(FPropertyChangedEvent & PropertyChangedEvent)
{
	HandleChangePropertyFromCategory(FObjectEditorUtils::GetCategoryFName(PropertyChangedEvent.Property));

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

void ACBNavGrid::RecreateDefaultFilter()
{
	DefaultQueryFilter->SetFilterType<FCBNavGridQueryFilter>();
	DefaultQueryFilter->SetMaxSearchNodes(DefaultMaxSearchNodes);

	FCBNavGridQueryFilter * const UENavGridFilter = static_cast<FCBNavGridQueryFilter *>(DefaultQueryFilter->GetImplementation());
	UENavGridFilter->SetHeuristicScale(DefaultHeuristicScale);
	UENavGridFilter->SetAxiswiseHeuristicScale(DefaultAxiswiseHeuristicScale);
}

bool ACBNavGrid::Raycast2d(FVector2d const & RayStart, FVector2d const & RayEnd, FVector2d * const OutHitLocation, FIntPoint * const OutHitGridCoord) const
{
	FIntPoint const StartGridCoord = CBGridUtilities::GetGridCellCoord(RayStart, GridCellSize);
	FIntPoint const StartTileCoord = GetTileCoord(StartGridCoord);
	FCBNavGridLayer const * const StartTile = GetTileNavigationData(StartTileCoord).Get();
	if (!StartTile || StartTile->IsCellOccupied(StartGridCoord))
	{
		if (OutHitLocation)
		{
			*OutHitLocation = RayStart;
		}
		if (OutHitGridCoord)
		{
			*OutHitGridCoord = INVALID_GRIDCOORD;
		}
		return true;
	}

	FIntPoint const EndGridCoord = CBGridUtilities::GetGridCellCoord(RayEnd, GridCellSize);
	FVector2d const Direction{ RayEnd - RayStart };
	FVector2d const DeltaDistanceScale{ FMath::Abs(1. / Direction.X), FMath::Abs(1. / Direction.Y) };
	FVector2d const DeltaDistance{ GridCellSize * DeltaDistanceScale.X, GridCellSize * DeltaDistanceScale.Y };

	FIntPoint Step;
	FVector2d SideDistance;

	if (Direction.X < 0.)
	{
		Step.X = -1;
		SideDistance.X = (RayStart.X - StartGridCoord.X * GridCellSize) * DeltaDistanceScale.X;
	}
	else
	{
		Step.X = 1;
		SideDistance.X = ((StartGridCoord.X + 1) * GridCellSize - RayStart.X) * DeltaDistanceScale.X;
	}

	if (Direction.Y < 0.)
	{
		Step.Y = -1;
		SideDistance.Y = (RayStart.Y - StartGridCoord.Y * GridCellSize) * DeltaDistanceScale.Y;
	}
	else
	{
		Step.Y = 1;
		SideDistance.Y = ((StartGridCoord.Y + 1) * GridCellSize - RayStart.Y) * DeltaDistanceScale.Y;
	}

	FIntPoint GridCoord = StartGridCoord;
	FIntPoint TileCoord = StartTileCoord;
	FCBNavGridLayer const * Tile = StartTile;
	check(Tile);

	while (GridCoord != EndGridCoord)
	{
		int32 const CoordToUpdateIndex = SideDistance.X < SideDistance.Y ? 0 : 1;
		FIntPoint const PreviousGridCoord = GridCoord;
		GridCoord[CoordToUpdateIndex] += Step[CoordToUpdateIndex];
		bool bDidHit = false;

		if (Tile->IsInGrid(GridCoord))
		{
			bDidHit = Tile->IsCellOccupied(GridCoord);
		}
		else
		{
			TileCoord[CoordToUpdateIndex] += Step[CoordToUpdateIndex];
			Tile = GetTileNavigationData(TileCoord).Get();
			bDidHit = !Tile || Tile->IsCellOccupied(GridCoord);
		}

		if (bDidHit)
		{
			if (OutHitLocation)
			{
				OutHitLocation->X = RayStart.X + Direction.X * SideDistance[CoordToUpdateIndex];
				OutHitLocation->Y = RayStart.Y + Direction.Y * SideDistance[CoordToUpdateIndex];
			}
			if (OutHitGridCoord)
			{
				*OutHitGridCoord = PreviousGridCoord;
			}
			return true;
		}

		SideDistance[CoordToUpdateIndex] += DeltaDistance[CoordToUpdateIndex];
	}

	if (OutHitLocation)
	{
		*OutHitLocation = RayEnd;
	}
	if (OutHitGridCoord)
	{
		*OutHitGridCoord = EndGridCoord;
	}
	return false;
}

bool ACBNavGrid::Raycast(FVector const & RayStart, FVector const & RayEnd, FNavLocation & OutHitLocation, bool & bOutIsRayEndInCorridor) const
{
	FVector const Extent = GetDefaultQueryExtent();
	
	FVector StartLocation;
	if (!ProjectPoint(RayStart, Extent, &StartLocation))
	{
		OutHitLocation.Location = RayStart;
		OutHitLocation.NodeRef = INVALID_NAVNODEREF;
		return true;
	}
	
	FVector EndLocation;
	if (!ProjectPoint(RayEnd, Extent, &EndLocation))
	{
		bOutIsRayEndInCorridor = false;
		EndLocation = RayEnd;
	}
	else
	{
		bOutIsRayEndInCorridor = true;
	}

	FVector2d HitLocation2d;
	FIntPoint HitGridCoord;
	bool const bDidHit = Raycast2d(static_cast<FVector2d>(StartLocation), static_cast<FVector2d>(EndLocation), &HitLocation2d, &HitGridCoord);
	OutHitLocation.Location.X = HitLocation2d.X;
	OutHitLocation.Location.Y = HitLocation2d.Y;
	FCBNavGridLayer const * const HitTile = GetTileNavigationData(GetTileCoord(HitGridCoord)).Get();
	check(HitTile);
	OutHitLocation.Location.Z = HitTile->GetCellHeight(HitGridCoord);
	OutHitLocation.NodeRef = GetNodeRef(HitGridCoord);
	return bDidHit;
}

bool ACBNavGrid::ProjectPoint(FVector const & Point, FVector const & Extent, FVector * const OutLocation, FIntPoint * const OutGridCoord) const
{
	FBox const QueryBoundingBox{ Point - Extent, Point + Extent };
	FIntRect const QueryGridRect = CBGridUtilities::GetGridRectFromBoundingBox(QueryBoundingBox, GridCellSize);
	FIntRect const QueryTileRect = CBGridUtilities::GetTileRect(QueryGridRect, TileSize);
	FVector ClosestLocation;
	FIntPoint ClosestLocationGridCoord{};
	double SqDistToClosestLocation = TNumericLimits<double>::Max();
	for (int32 TileX = QueryTileRect.Min.X; TileX < QueryTileRect.Max.X; ++TileX)
	{
		for (int32 TileY = QueryTileRect.Min.Y; TileY < QueryTileRect.Max.Y; ++TileY)
		{
			FCBNavGridLayer const * NavGridLayer = GetTileNavigationData(FIntPoint{ TileX, TileY }).Get();
			if (!NavGridLayer)
			{
				continue;
			}
			FIntRect const ClippedGridRect = NavGridLayer->ClipWithGridRect(QueryGridRect);
			for (int32 X = ClippedGridRect.Min.X; X < ClippedGridRect.Max.X; ++X)
			{
				for (int32 Y = ClippedGridRect.Min.Y; Y < ClippedGridRect.Max.Y; ++Y)
				{
					FIntPoint const CellCoord{ X, Y };
					if (NavGridLayer->IsCellOccupied(CellCoord))
					{
						continue;
					}

					FVector2d const CellCornerCoord{ CellCoord.X * GridCellSize, CellCoord.Y * GridCellSize };
					FVector const CellPointClosestToPoint{
						FMath::Clamp(Point.X, CellCornerCoord.X, CellCornerCoord.X + GridCellSize),
						FMath::Clamp(Point.Y, CellCornerCoord.Y, CellCornerCoord.Y + GridCellSize),
						NavGridLayer->GetCellHeight(CellCoord)
					};
					double const SqDistToCell = FVector::DistSquared(Point, CellPointClosestToPoint);
					if (SqDistToCell < SqDistToClosestLocation)
					{
						SqDistToClosestLocation = SqDistToCell;
						ClosestLocation = CellPointClosestToPoint;
						ClosestLocationGridCoord = CellCoord;
					}
				}
			}
		}
	}
	if (SqDistToClosestLocation < TNumericLimits<double>::Max() && QueryBoundingBox.IsInsideOrOn(ClosestLocation))
	{
		if (OutLocation)
		{
			*OutLocation = ClosestLocation;
		}
		if (OutGridCoord)
		{
			*OutGridCoord = ClosestLocationGridCoord;
		}
		return true;
	}
	return false;
}

ENavigationQueryResult::Type ACBNavGrid::FindPath(FIntPoint const StartGridCoord, FIntPoint const EndGridCoord, FCBNavGridAStarFilter const & Filter, TArray<FIntPoint> & OutPath) const
{
	FGraphAStar<FCBNavGridToGraphAdapter, FGraphAStarDefaultPolicy, FCBNavGridAStarNode> AStar{ FCBNavGridToGraphAdapter{ *this } };
	EGraphAStarResult const AStarResult = AStar.FindPath(StartGridCoord, EndGridCoord, Filter, OutPath);
	if (AStarResult == EGraphAStarResult::SearchFail)
	{
		return ENavigationQueryResult::Error;
	}
	if (AStarResult == EGraphAStarResult::SearchSuccess && OutPath.IsEmpty())
	{
		OutPath.Add(StartGridCoord);
	}
	if (OutPath.IsEmpty())
	{
		return ENavigationQueryResult::Fail;
	}
	return ENavigationQueryResult::Success;
}

FIntPoint ACBNavGrid::GetTileCoord(FIntPoint const GridCoord) const
{
	return CBGridUtilities::GetTileCoord(GridCoord, TileSize);
}

TSharedPtr<FCBNavGridLayer const> ACBNavGrid::GetTileNavigationData(FIntPoint const TileCoord) const
{
	FTileData const * const TileData = Tiles.Find(TileCoord);
	return TileData ? TileData->NavigationData : nullptr;
}

TSharedPtr<FCBHeightfield const> ACBNavGrid::GetTileHeightfield(FIntPoint const TileCoord) const
{
	FTileData const * const TileData = Tiles.Find(TileCoord);
	return TileData ? TileData->Heightfield : nullptr;
}

void ACBNavGrid::OnTileGenerationCompleted(FIntPoint const TileCoord, TUniquePtr<FCBNavGridLayer const> GeneratedNavGridLayer, TUniquePtr<FCBHeightfield const> GeneratedHeightfield)
{
	if (TileCoord == INVALID_GRIDCOORD)
	{
		return;
	}

	InvalidateAffectedPaths(TileCoord);

	if (!GeneratedNavGridLayer.IsValid())
	{
		FTileData TileData;
		if (Tiles.RemoveAndCopyValue(TileCoord, TileData) && IsValidRect(BoundingGridRect))
		{
			check(TileData.NavigationData);
			FIntRect const & TileBoundingRect = TileData.NavigationData->GetGridRect();
			// If removed tile bounding rect touches grid bounds, grid bounding rect cache should be recalculated.
			if (HaveCommonBorder(BoundingGridRect, TileBoundingRect))
			{
				InvalidateRect(BoundingGridRect);
			}
		}
		return;
	}

	if (IsValidRect(BoundingGridRect))
	{
		if (Tiles.IsEmpty())
		{
			BoundingGridRect = GeneratedNavGridLayer->GetGridRect();
		}
		else
		{
			BoundingGridRect.Union(GeneratedNavGridLayer->GetGridRect());
		}
	}

	FTileData & TileData = Tiles.FindOrAdd(TileCoord);
	TileData.NavigationData = MakeShareable(GeneratedNavGridLayer.Release());

	if (GeneratedHeightfield.IsValid())
	{
		TileData.Heightfield = MakeShareable(GeneratedHeightfield.Release());
	}

	RequestDrawingUpdate();
}

FIntPoint ACBNavGrid::GetGridCoord(NavNodeRef const NodeRef) const
{
	int32 const X = static_cast<int64>(NodeRef >> 32) + INT32_MIN;
	int32 const Y = static_cast<int64>(NodeRef & 0xffffffff) + INT32_MIN;
	return FIntPoint{ X, Y };
}

NavNodeRef ACBNavGrid::GetNodeRef(FIntPoint const GridCoord) const
{
	uint64 const XPart = static_cast<int64>(GridCoord.X) - INT32_MIN;
	uint64 const YPart = static_cast<int64>(GridCoord.Y) - INT32_MIN;
	return (XPart << 32) | YPart;
}

FIntPoint const ACBNavGrid::INVALID_GRIDCOORD{ INT32_MIN, INT32_MIN };

// Semantic of the function is not fully clear, technically Query can have PathInstanceToFill created with another
// FPathFindingQueryData and another ANavigationData. What query data should the function use?
// For now made to be similar to RecastNavMesh, so can have similar bugs.
FPathFindingResult ACBNavGrid::FindPath(FNavAgentProperties const & AgentProperties, FPathFindingQuery const & Query)
{
	FPathFindingResult Result(ENavigationQueryResult::Error);

	ACBNavGrid const * UENavGrid;
	{
		ANavigationData const * Self = Query.NavData.Get();
		if (!Self)
		{
			return Result;
		}
		check(Cast<ACBNavGrid const>(Self));
		UENavGrid = static_cast<ACBNavGrid const *>(Self);
	}

	FCBNavGridPath * NavGridPath;
	{
		FNavPathSharedPtr const & PathInstanceToFill = Query.PathInstanceToFill;
		NavGridPath = PathInstanceToFill ? PathInstanceToFill->CastPath<FCBNavGridPath>() : nullptr;
		if (NavGridPath)
		{
			Result.Path = PathInstanceToFill;
			NavGridPath->ResetForRepath();
		}
		else
		{
			Result.Path = UENavGrid->CreatePathInstance<FCBNavGridPath>(Query);
			NavGridPath = Result.Path->CastPath<FCBNavGridPath>();
			NavGridPath->ApplyFlags(Query.NavDataFlags);
		}
	}

	Result.Result = UENavGrid->FindPath(*NavGridPath, Query);
	return Result;
}

bool ACBNavGrid::TestPath(FNavAgentProperties const & AgentProperties, FPathFindingQuery const & Query, int32 * const OutNumVisitedNodes)
{
	ACBNavGrid const * UENavGrid;
	{
		ANavigationData const * Self = Query.NavData.Get();
		if (!Self)
		{
			return false;
		}
		check(Cast<ACBNavGrid const>(Self));
		UENavGrid = static_cast<ACBNavGrid const *>(Self);
	}

	return UENavGrid->TestPath(Query, OutNumVisitedNodes);
}

bool ACBNavGrid::Raycast(ANavigationData const * Self, FVector const & RayStart, FVector const & RayEnd, FVector & OutHitLocation, FNavigationRaycastAdditionalResults * OutAdditionalResults, FSharedConstNavQueryFilter QueryFilter, UObject const * Querier)
{
	check(Cast<ACBNavGrid const>(Self));
	ACBNavGrid const * UENavGrid = static_cast<ACBNavGrid const *>(Self);
	FNavLocation HitLocation;
	bool bIsRayEndInCorridor;
	bool const bDidHit = UENavGrid->Raycast(RayStart, RayEnd, HitLocation, bIsRayEndInCorridor);
	OutHitLocation = HitLocation.Location;
	if (OutAdditionalResults)
	{
		OutAdditionalResults->bIsRayEndInCorridor = bIsRayEndInCorridor;
	}
	return bDidHit;
}

ENavigationQueryResult::Type ACBNavGrid::FindPath(FCBNavGridPath & OutPath, FPathFindingQuery const & Query) const
{
	check(!OutPath.IsReady());
	ENavigationQueryResult::Type Result;
	FNavigationQueryFilter const & QueryFilter = GetFilterRef(Query.QueryFilter.Get());
	FVector const AdjustedEndLocation = QueryFilter.GetAdjustedEndLocation(Query.EndLocation);

	if (AdjustedEndLocation.Equals(Query.StartLocation))
	{
		OutPath.GetPathPoints().Add(FNavPathPoint{ AdjustedEndLocation });
		Result = ENavigationQueryResult::Success;
	}
	else
	{
		FIntPoint const StartGridCoord = CBGridUtilities::GetGridCellCoord(static_cast<FVector2d>(Query.StartLocation), GridCellSize);
		FIntPoint const EndGridCoord = CBGridUtilities::GetGridCellCoord(static_cast<FVector2d>(AdjustedEndLocation), GridCellSize);
		// TODO: Should be dynamic_cast, but by default unreal projects compiles without rtti, so dynamic_cast won't work. Needs some workaround.
		FCBNavGridQueryFilter const * const NavGridQueryFilter = static_cast<FCBNavGridQueryFilter const *>(QueryFilter.GetImplementation());
		FVector2d const AxiswiseHeuristicScale = NavGridQueryFilter ? static_cast<FVector2d>(NavGridQueryFilter->GetAxiswiseHeuristicScale()) : FVector2d{ 1., 1. };
		FCBNavGridAStarFilter const AStarFilter{ QueryFilter.GetHeuristicScale(), AxiswiseHeuristicScale, Query.CostLimit, QueryFilter.GetMaxSearchNodes(), !!Query.bAllowPartialPaths };
		TArray<FIntPoint> GridPath;
		Result = FindPath(StartGridCoord, EndGridCoord, AStarFilter, GridPath);

		if (Result == ENavigationQueryResult::Error)
		{
			return Result;
		}

		PostprocessPath(Query.StartLocation, StartGridCoord, AdjustedEndLocation, EndGridCoord, GridPath, OutPath);
	}
	
	OutPath.MarkReady();
	return Result;
}

void ACBNavGrid::PostprocessPath(FVector const & StartLocation, FIntPoint const StartGridCoord, FVector const & EndLocation, FIntPoint const EndGridCoord, TConstArrayView<FIntPoint> const GridPath, FCBNavGridPath & OutPath) const
{
	TArray<FNavPathPoint> & PathPoints = OutPath.GetPathPoints();
	PathPoints.Add(FNavPathPoint{ StartLocation, GetNodeRef(StartGridCoord) });
	FIntRect & GridBoundingBox = OutPath.GetGridBoundingBox();
	GridBoundingBox = FIntRect{ StartGridCoord, StartGridCoord };

	if (OutPath.WantsStringPulling())
	{
		FVector2d PrevPointLocation2d{ StartLocation };
		for (int32 GridCoordIndex = 1; GridCoordIndex < GridPath.Num(); ++GridCoordIndex)
		{
			FVector2d const PointLocation2d = CBGridUtilities::GetGridCellCenter(GridPath[GridCoordIndex], GridCellSize);
			if (Raycast2d(PrevPointLocation2d, PointLocation2d))
			{
				FIntPoint const PrevGridCoord = GridPath[GridCoordIndex - 1];
				PrevPointLocation2d = CBGridUtilities::GetGridCellCenter(PrevGridCoord, GridCellSize);
				FVector::FReal const PrevCellHeight = GetTileNavigationData(GetTileCoord(PrevGridCoord))->GetCellHeight(PrevGridCoord);
				PathPoints.Add(FNavPathPoint{ FVector{ PrevPointLocation2d, PrevCellHeight }, GetNodeRef(PrevGridCoord) });
				GridBoundingBox.Include(PrevGridCoord);
			}
		}

		if (Raycast2d(PrevPointLocation2d, static_cast<FVector2d>(EndLocation)))
		{
			FVector2d const EndCellCenter2d = CBGridUtilities::GetGridCellCenter(EndGridCoord, GridCellSize);
			FVector::FReal const EndCellHeight = GetTileNavigationData(GetTileCoord(EndGridCoord))->GetCellHeight(EndGridCoord);
			PathPoints.Add(FNavPathPoint{ FVector{ EndCellCenter2d, EndCellHeight }, GetNodeRef(EndGridCoord) });
		}
	}
	else
	{
		FIntPoint PrevGridCoord = StartGridCoord;
		for (int32 GridCoordIndex = 2; GridCoordIndex < GridPath.Num(); ++GridCoordIndex)
		{
			FIntPoint const GridCoordsDiff = GridPath[GridCoordIndex] - PrevGridCoord;
			if (GridCoordsDiff.X != 0 && GridCoordsDiff.Y != 0)
			{
				PrevGridCoord = GridPath[GridCoordIndex - 1];
				FVector2d const PrevCellCenter = CBGridUtilities::GetGridCellCenter(PrevGridCoord, GridCellSize);
				FVector::FReal const PrevCellHeight = GetTileNavigationData(GetTileCoord(PrevGridCoord))->GetCellHeight(PrevGridCoord);
				PathPoints.Add(FNavPathPoint{ FVector{ PrevCellCenter, PrevCellHeight }, GetNodeRef(PrevGridCoord) });
				GridBoundingBox.Include(PrevGridCoord);
			}
		}
	}

	PathPoints.Add(FNavPathPoint{ EndLocation, GetNodeRef(EndGridCoord) });
	GridBoundingBox.Include(EndGridCoord);
	GridBoundingBox.Max += FIntPoint{ 1, 1 };
}

bool ACBNavGrid::TestPath(FPathFindingQuery const & Query, int32 * const OutNumVisitedNodes) const
{
	FNavigationQueryFilter const & QueryFilter = GetFilterRef(Query.QueryFilter.Get());
	FVector const AdjustedEndLocation = QueryFilter.GetAdjustedEndLocation(Query.EndLocation);

	FIntPoint const StartGridCoord = CBGridUtilities::GetGridCellCoord(static_cast<FVector2d>(Query.StartLocation), GridCellSize);
	FIntPoint const EndGridCoord = CBGridUtilities::GetGridCellCoord(static_cast<FVector2d>(AdjustedEndLocation), GridCellSize);

	if (StartGridCoord == EndGridCoord)
	{
		if (OutNumVisitedNodes)
		{
			*OutNumVisitedNodes = 1;
		}

		return true;
	}

	// TODO: Should be dynamic_cast, but by default unreal projects compiled without rtti, so dynamic_cast won't work. Needs some workaround.
	FCBNavGridQueryFilter const * const NavGridQueryFilter = static_cast<FCBNavGridQueryFilter const *>(QueryFilter.GetImplementation());
	FVector2d const AxiswiseHeuristicScale = NavGridQueryFilter ? static_cast<FVector2d>(NavGridQueryFilter->GetAxiswiseHeuristicScale()) : FVector2d{ 1., 1. };
	FCBNavGridAStarFilter const AStarFilter{ QueryFilter.GetHeuristicScale(), AxiswiseHeuristicScale, Query.CostLimit, QueryFilter.GetMaxSearchNodes() };
	TArray<FIntPoint> GridPath;
	FGraphAStar<FCBNavGridToGraphAdapter, FGraphAStarDefaultPolicy, FCBNavGridAStarNode> AStar{ FCBNavGridToGraphAdapter{ *this } };
	EGraphAStarResult const AStarResult = AStar.FindPath(StartGridCoord, EndGridCoord, AStarFilter, GridPath);

	if (OutNumVisitedNodes)
	{
		*OutNumVisitedNodes = AStar.NodePool.Num();
	}

	return AStarResult == EGraphAStarResult::SearchSuccess;
}

void ACBNavGrid::FindOverlappingEdgesUnsafe(TConstArrayView<FIntPoint> const StartGridCoords, TConstArrayView<FVector> const ConvexPolygon, TArray<FVector> & OutEdges) const
{
	TArray<FIntPoint> OpenList;
	TSet<FIntPoint> VisitedSet;
	OpenList.Append(StartGridCoords);
	VisitedSet.Append(StartGridCoords);

	FCBNavGridLayer const * Tile = GetTileNavigationData(GetTileCoord(OpenList.Last())).Get();
	check(Tile && !Tile->IsCellOccupied(OpenList.Last()));

	auto AddOutEdge = [this, &Tile, &OutEdges](FIntPoint const CellCoord, FVector2d const & EdgeStart2d, FVector2d const & EdgeEnd2d)
		{
			if (!Tile->IsInGrid(CellCoord))
			{
				Tile = GetTileNavigationData(GetTileCoord(CellCoord)).Get();
				check(Tile);
			}
			float const CellHeight = Tile->GetCellHeight(CellCoord);
			OutEdges.Emplace(EdgeStart2d, CellHeight);
			OutEdges.Emplace(EdgeEnd2d, CellHeight);
		};

	while (!OpenList.IsEmpty())
	{
		FIntPoint const CellCoord = OpenList.Pop(EAllowShrinking::No);
		for (ECBGridDirection const GridDirection : TEnumRange<ECBGridDirection>())
		{
			FIntPoint const AdjacentCellCoord = CBGridUtilities::GetAdjacentCoordChecked(CellCoord, GridDirection);
			FVector2d CellEdgeStart, CellEdgeEnd;
			GetEdgeCoords2D(CellCoord, GridDirection, GridCellSize, CellEdgeStart, CellEdgeEnd);

			if (VisitedSet.Contains(AdjacentCellCoord) || !IntersectSegmentPoly2D(CellEdgeStart, CellEdgeEnd, ConvexPolygon))
			{
				continue;
			}
			if (!Tile->IsInGrid(AdjacentCellCoord))
			{
				FCBNavGridLayer const * NewTile = GetTileNavigationData(GetTileCoord(AdjacentCellCoord)).Get();
				if (!NewTile)
				{
					AddOutEdge(CellCoord, CellEdgeStart, CellEdgeEnd);
					continue;
				}
				Tile = NewTile;
			}
			if (Tile->IsCellOccupied(AdjacentCellCoord))
			{
				AddOutEdge(CellCoord, CellEdgeStart, CellEdgeEnd);
			}
			else
			{
				OpenList.Push(AdjacentCellCoord);
				VisitedSet.Add(AdjacentCellCoord);
			}
		}
	}
}

void ACBNavGrid::InvalidateAffectedPaths(FIntPoint const ChangedTileCoord)
{
	if (ChangedTileCoord == INVALID_GRIDCOORD)
	{
		return;
	}

	// Paths can be registered from async pathfinding thread.
	// Theoretically paths are invalidated synchronously by the navigation system
	// before starting async queries task but protecting ActivePaths will make
	// the system safer in case of future timing changes.
	UE::TScopeLock PathLock(ActivePathsLock);

	FIntRect const TileGridBoundingBox{ ChangedTileCoord * TileSize, (ChangedTileCoord + FIntPoint{ 1, 1 }) * TileSize };
	for (int32 PathIndex = ActivePaths.Num() - 1; PathIndex >= 0; --PathIndex)
	{
		FNavPathSharedPtr const PathSharedPtr = ActivePaths[PathIndex].Pin();
		if (!PathSharedPtr.IsValid())
		{
			ActivePaths.RemoveAtSwap(PathIndex, EAllowShrinking::No);
			continue;
		}

		FCBNavGridPath const * const Path = PathSharedPtr.Get()->CastPath<FCBNavGridPath>();
		if (Path == nullptr || !Path->IsReady() || Path->GetIgnoreInvalidation())
		{
			continue;
		}

		if (TileGridBoundingBox.Intersect(Path->GetGridBoundingBox()))
		{
			PathSharedPtr->Invalidate();
			ActivePaths.RemoveAtSwap(PathIndex, EAllowShrinking::No);
		}
	}
}

FIntRect ACBNavGrid::CalculateBoundingGridRect() const
{
	FIntRect GridRect;
	for (TPair<FIntPoint, FTileData> const & Tile : Tiles)
	{
		check(Tile.Value.NavigationData.IsValid());
		GridRect.Union(Tile.Value.NavigationData->GetGridRect());
	}
	return GridRect;
}

void ACBNavGrid::RequestDrawingUpdate()
{
#if UE_ENABLE_DEBUG_DRAWING
	if (RenderingComp)
	{
		if (UCBNavGridRenderingComponent * const NavGridRenderingComp = Cast<UCBNavGridRenderingComponent>(RenderingComp))
		{
			NavGridRenderingComp->RequestDrawingUpdate();
		}
		else
		{
			RenderingComp->MarkRenderStateDirty();
		}
	}
#endif // UE_ENABLE_DEBUG_DRAWING
}

#if WITH_EDITOR
void ACBNavGrid::HandleChangePropertyFromCategory(FName const CategoryName)
{
	static FName const NAME_Generation{ "Generation" };
	static FName const NAME_Display{ "Display" };
	static FName const NAME_Query{ "Query" };

	if (CategoryName == NAME_Generation)
	{
		UNavigationSystemV1 const * const NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (!HasAnyFlags(RF_ClassDefaultObject) && NavSys && NavSys->GetIsAutoUpdateEnabled())
		{
			RebuildAll();
		}
	}
	else if (CategoryName == NAME_Display)
	{
		RequestDrawingUpdate();
	}
	else if (CategoryName == NAME_Query)
	{
		RecreateDefaultFilter();
	}
}
#endif // WITH_EDITOR

FArchive & operator <<(FArchive & Archive, ACBNavGrid::FTileData & TileData)
{
	auto SerializeSharedPtr = [&Archive]<typename T, ESPMode InMode>(TSharedPtr<T, InMode> & SharedPtr)
		{
			bool bIsValid = SharedPtr.IsValid();
			Archive << bIsValid;
			if (bIsValid)
			{
				if (Archive.IsLoading())
				{
					SharedPtr = MakeShared<T, InMode>();
				}
				Archive << *const_cast<std::remove_const_t<T> *>(SharedPtr.Get());
			}
			else if (Archive.IsLoading())
			{
				SharedPtr.Reset();
			}
		};

	SerializeSharedPtr(TileData.Heightfield);
	SerializeSharedPtr(TileData.NavigationData);

	return Archive;
}
