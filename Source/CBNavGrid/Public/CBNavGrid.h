#pragma once

#include "NavigationData.h"
#include "CBNavGrid.generated.h"

class FCBHeightfield;
class FCBNavGridLayer;
class FCBNavGridAStarFilter;
struct FCBNavGridPath;

enum class ECBNavGridPathFlags : int32
{
	SkipStringPulling = (1 << 0)
};
ENUM_CLASS_FLAGS(ECBNavGridPathFlags);

USTRUCT()
struct CBNAVGRID_API FCBNavGridDebugSettings
{
	GENERATED_BODY()

	FCBNavGridDebugSettings();

	UPROPERTY(EditAnywhere)
	float DrawDistance;

	UPROPERTY(EditAnywhere)
	float DrawOffset;

	UPROPERTY(EditAnywhere)
	uint8 bDrawCellEdges : 1;

	UPROPERTY(EditAnywhere)
	uint8 bDrawFilledCells : 1;

	UPROPERTY(EditAnywhere)
	uint8 bDrawNavGridEdges : 1;

	UPROPERTY(EditAnywhere)
	uint8 bDrawTileEdges : 1;
};

/**
 * One-layered squared navigation grid. Supports only default and null nav areas.
 * Ignores nav areas requirements passed to methods in filter. Doesen't support any links.
 * Treats navigation bounds and dynamic areas as 2d rectangles projected on grid.
 * Creates navigation only for the highest collision geometry.
 */
UCLASS(Config = Engine, Defaultconfig, Hidecategories = (Input, Rendering, Tags, Transformation, Actor, Layers, Replication), Notplaceable)
class CBNAVGRID_API ACBNavGrid : public ANavigationData
{
	GENERATED_BODY()

public:
	ACBNavGrid();
	virtual ~ACBNavGrid() override;

	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void Serialize(FArchive & Archive) override;
	virtual void CleanUp() override;
	virtual bool NeedsRebuild() const override;
	virtual bool SupportsRuntimeGeneration() const override;
	virtual void ConditionalConstructGenerator() override;
	virtual void RebuildAll() override;
	virtual void BatchRaycast(TArray<FNavigationRaycastWork> & Workload, FSharedConstNavQueryFilter QueryFilter, UObject const * Querier = nullptr) const override;
	virtual bool FindMoveAlongSurface(FNavLocation const & StartLocation, FVector const & TargetPosition, FNavLocation & OutLocation, FSharedConstNavQueryFilter const Filter = nullptr, UObject const * const Querier = nullptr) const override;
	virtual bool FindOverlappingEdges(FNavLocation const & StartLocation, TConstArrayView<FVector> const ConvexPolygon, TArray<FVector> & OutEdges, FSharedConstNavQueryFilter const Filter = nullptr, UObject const * const Querier = nullptr) const override;
	virtual bool GetPathSegmentBoundaryEdges(FNavigationPath const & Path, FNavPathPoint const & StartPoint, FNavPathPoint const & EndPoint, TConstArrayView<FVector> const SearchArea, TArray<FVector> & OutEdges, float const MaxAreaEnterCost, FSharedConstNavQueryFilter const Filter = nullptr, UObject const * const Querier = nullptr) const override;
	virtual FNavLocation GetRandomPoint(FSharedConstNavQueryFilter Filter = nullptr, UObject const * Querier = nullptr) const override;

	/** Finds a random location in Radius, reachable from Origin. */
	virtual bool GetRandomReachablePointInRadius(FVector const & Origin, float const Radius, FNavLocation & OutResult, FSharedConstNavQueryFilter Filter = nullptr, UObject const * Querier = nullptr) const override;

	/** Finds a random location in navigable space, in given Radius. */
	virtual bool GetRandomPointInNavigableRadius(FVector const & Origin, float const Radius, FNavLocation & OutResult, FSharedConstNavQueryFilter Filter = nullptr, UObject const * Querier = nullptr) const override;

	virtual bool ProjectPoint(FVector const & Point, FNavLocation & OutLocation, FVector const & Extent, FSharedConstNavQueryFilter Filter = nullptr, UObject const * Querier = nullptr) const override;
	virtual bool IsNodeRefValid(NavNodeRef const NodeRef) const override;

	/** Projects batch of points using shared search extent and filter. */
	virtual void BatchProjectPoints(TArray<FNavigationProjectionWork> & Workload, FVector const & Extent, FSharedConstNavQueryFilter Filter = nullptr, UObject const * Querier = nullptr) const override;

	/**
	 * Projects batch of points using shared search filter. This version is not requiring user to pass in Extent,
	 * and is instead relying on FNavigationProjectionWork.ProjectionLimit.
	 */
	virtual void BatchProjectPoints(TArray<FNavigationProjectionWork> & Workload, FSharedConstNavQueryFilter Filter = nullptr, UObject const * Querier = nullptr) const override;

	virtual ENavigationQueryResult::Type CalcPathCost(FVector const & PathStart, FVector const & PathEnd, FVector::FReal & OutPathCost, FSharedConstNavQueryFilter const QueryFilter = nullptr, UObject const * const Querier = nullptr) const override;
	virtual ENavigationQueryResult::Type CalcPathLength(FVector const & PathStart, FVector const & PathEnd, FVector::FReal & OutPathLength, FSharedConstNavQueryFilter const QueryFilter = nullptr, UObject const * const Querier = nullptr) const override;
	virtual ENavigationQueryResult::Type CalcPathLengthAndCost(FVector const & PathStart, FVector const & PathEnd, FVector::FReal & OutPathLength, FVector::FReal & OutPathCost, FSharedConstNavQueryFilter const QueryFilter = nullptr, UObject const * const Querier = nullptr) const override;
	virtual bool DoesNodeContainLocation(NavNodeRef const NodeRef, FVector const & WorldSpaceLocation) const override;
	virtual UPrimitiveComponent * ConstructRenderingComponent() override;

	/** Returns bounding box for the NavGrid. */
	virtual FBox GetBounds() const override;

	/** Gets max areas supported by this navigation data. */
	virtual int32 GetMaxSupportedAreas() const override;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent & PropertyChangedChainEvent) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent & PropertyChangedEvent) override;
#endif // WITH_EDITOR

	virtual void RecreateDefaultFilter();

	bool Raycast2d(FVector2d const & RayStart, FVector2d const & RayEnd, FVector2d * const OutHitLocation = nullptr, FIntPoint * const OutHitGridCoord = nullptr) const;
	bool Raycast(FVector const & RayStart, FVector const & RayEnd, FNavLocation & OutHitLocation, bool & bOutIsRayEndInCorridor) const;
	bool ProjectPoint(FVector const & Point, FVector const & Extent, FVector * const OutLocation = nullptr, FIntPoint * const OutGridCoord = nullptr) const;
	ENavigationQueryResult::Type FindPath(FIntPoint const StartGridCoord, FIntPoint const EndGridCoord, FCBNavGridAStarFilter const & Filter, TArray<FIntPoint> & OutPath) const;

	FORCEINLINE TArray<FIntPoint> GetTileCoords() const;
	FORCEINLINE FIntPoint GetTileSize() const;
	FORCEINLINE bool IsValidTileCoord(FIntPoint const TileCoord) const;
	FIntPoint GetTileCoord(FIntPoint const GridCoord) const;
	TSharedPtr<FCBNavGridLayer const> GetTileNavigationData(FIntPoint const TileCoord) const;
	TSharedPtr<FCBHeightfield const> GetTileHeightfield(FIntPoint const TileCoord) const;
	void OnTileGenerationCompleted(FIntPoint const TileCoord, TUniquePtr<FCBNavGridLayer const> GeneratedNavGridLayer, TUniquePtr<FCBHeightfield const> GeneratedHeightfield);

	FORCEINLINE float GetGridCellSize() const;
	FORCEINLINE float GetMaxNavigableCellHeightsDifference() const;
	FORCEINLINE float GetMinZ() const;
	FORCEINLINE float GetMaxZ() const;
	FORCEINLINE FCBNavGridDebugSettings const & GetDebugSettings() const;
	FIntPoint GetGridCoord(NavNodeRef const NodeRef) const;
	NavNodeRef GetNodeRef(FIntPoint const GridCoord) const;

	static FIntPoint const INVALID_GRIDCOORD;

protected:
	static FPathFindingResult FindPath(FNavAgentProperties const & AgentProperties, FPathFindingQuery const & Query);
	static bool TestPath(FNavAgentProperties const & AgentProperties, FPathFindingQuery const & Query, int32 * const OutNumVisitedNodes);
	static bool Raycast(ANavigationData const * Self, FVector const & RayStart, FVector const & RayEnd, FVector & OutHitLocation, FNavigationRaycastAdditionalResults * OutAdditionalResults, FSharedConstNavQueryFilter QueryFilter, UObject const * Querier);

	ENavigationQueryResult::Type FindPath(FCBNavGridPath & OutPath, FPathFindingQuery const & Query) const;
	void PostprocessPath(FVector const & StartLocation, FIntPoint const StartGridCoord, FVector const & EndLocation, FIntPoint const EndGridCoord, TConstArrayView<FIntPoint> const GridPath, FCBNavGridPath & OutPath) const;
	bool TestPath(FPathFindingQuery const & Query, int32 * const OutNumVisitedNodes) const;

	/** Relies on StartGridCoords being valid and traversable. */
	void FindOverlappingEdgesUnsafe(TConstArrayView<FIntPoint> const StartGridCoords, TConstArrayView<FVector> const SearchArea, TArray<FVector> & OutEdges) const;

	/** Invalidates active paths that go through changed tile. */
	void InvalidateAffectedPaths(FIntPoint const ChangedTileCoord);
	FIntRect CalculateBoundingGridRect() const;
	void RequestDrawingUpdate();
	FORCEINLINE FNavigationQueryFilter const & GetFilterRef(FNavigationQueryFilter const * const Filter) const;

#if WITH_EDITOR
	void HandleChangePropertyFromCategory(FName const CategoryName);
#endif // WITH_EDITOR

private:
	struct FTileData
	{
		TSharedPtr<FCBNavGridLayer const> NavigationData;
		TSharedPtr<FCBHeightfield const> Heightfield;
	};

	friend FArchive & operator <<(FArchive & Archive, FTileData & TileData);

	/** If tile is in the map, it must always have valid(not nullptr) NavigationData field of FTileData. */
	TMap<FIntPoint, FTileData> Tiles;

	/** Cached bounding grid rect. */
	mutable FIntRect BoundingGridRect;

protected:
	UPROPERTY(EditAnywhere, Category = Display)
	FCBNavGridDebugSettings DebugSettings;

	UPROPERTY(EditAnywhere, Category = Generation, Config, meta = (ClampMin = "32", UIMin = "32", Delta = "32", Multiple = "32"))
	FIntPoint TileSize;

	UPROPERTY(EditAnywhere, Category = Generation, Config, meta = (ClampMin = "1", UIMin = "1"))
	float GridCellSize;

	UPROPERTY(EditAnywhere, Category = Generation, Config, meta = (ClampMin = "1", UIMin = "1"))
	float MaxNavigableCellHeightsDifference;

	UPROPERTY(EditAnywhere, Category = Generation, Config)
	float MinZ;

	UPROPERTY(EditAnywhere, Category = Generation, Config)
	float MaxZ;

	UPROPERTY(EditAnywhere, Category = Query, Config)
	uint32 DefaultMaxSearchNodes;

	UPROPERTY(EditAnywhere, Category = Query, Config, meta = (ClampMin = "0.1", UIMin = "0.1"))
	float DefaultHeuristicScale;

	UPROPERTY(EditAnywhere, Category = Query, Config, meta = (ClampMin = "0.1", UIMin = "0.1"))
	FVector2f DefaultAxiswiseHeuristicScale;
};

FIntPoint ACBNavGrid::GetTileSize() const
{
	return TileSize;
}

TArray<FIntPoint> ACBNavGrid::GetTileCoords() const
{
	TArray<FIntPoint> TileCoords;
	Tiles.GenerateKeyArray(TileCoords);
	return TileCoords;
}

bool ACBNavGrid::IsValidTileCoord(FIntPoint const TileCoord) const
{
	return Tiles.Contains(TileCoord);
}

float ACBNavGrid::GetGridCellSize() const
{
	return GridCellSize;
}

float ACBNavGrid::GetMaxNavigableCellHeightsDifference() const
{
	return MaxNavigableCellHeightsDifference;
}

float ACBNavGrid::GetMinZ() const
{
	return MinZ;
}

float ACBNavGrid::GetMaxZ() const
{
	return MaxZ;
}

FCBNavGridDebugSettings const & ACBNavGrid::GetDebugSettings() const
{
	return DebugSettings;
}

FNavigationQueryFilter const & ACBNavGrid::GetFilterRef(FNavigationQueryFilter const * const Filter) const
{
	return *(Filter ? Filter : GetDefaultQueryFilter().Get());
}
