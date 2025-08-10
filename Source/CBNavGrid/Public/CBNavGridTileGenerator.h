#pragma once

#include "CBNavGridLayer.h"
#include "CBNavGridGenerator.h"
#include "CoreMinimal.h"

struct FAreaNavModifier;
class FCBHeightfield;
class UNavigationSystemV1;

struct FCBPreparedNavigationRelevantData
{
	TSharedRef<FNavigationRelevantData, ESPMode::ThreadSafe> NavigationRelevantData;
	/* Per instance transforms, if empty, navigation data is in world space. */
	TArray<FTransform> PerInstanceTransform;
};

struct FCBGeometry
{
	/** Instance geometry. */
	TArray<FVector> Vertices;
	TArray<int32> Indices;
	/* Per instance transforms, if empty, geometry is in world space. */
	TArray<FTransform> PerInstanceTransform;
};

struct FCBAreaNavModifierCollection
{
	TArray<FAreaNavModifier> Areas;
	/* Per instance transforms, if empty, areas are in world space. */
	TArray<FTransform> PerInstanceTransform;
};

/**
 * Handles generation of a single tile.
 */
class CBNAVGRID_API FCBNavGridTileGenerator
{
public:
	explicit FCBNavGridTileGenerator(FCBNavGridGenerator const & InParentGenerator, FIntPoint const InTileCoord);

	/** Prevents copying. */
	FCBNavGridTileGenerator(FCBNavGridTileGenerator const &) = delete;
	FCBNavGridTileGenerator & operator =(FCBNavGridTileGenerator const &) = delete;

	/** Starts navigation data generation in async way. */
	void GenerateNavigationDataAsync(TArray<FCBNavigationDirtyArea> const & DirtyAreas);

	FORCEINLINE FIntPoint GetTileCoord() const;
	FORCEINLINE FIntRect GetTileGridRect() const;
	FORCEINLINE TUniquePtr<FCBNavGridLayer> & GetNavigationData();
	FORCEINLINE TUniquePtr<FCBHeightfield> & GetHeightfield();
	FORCEINLINE bool IsGenerationCompleted() const;
	FORCEINLINE bool WaitForNavigationDataGeneration() const;
	FORCEINLINE bool HasDataToGenerate() const;
	FORCEINLINE bool HasGenerationStarted() const;
	FORCEINLINE bool IntersectsNavigationGridRects() const;

private:
	void GatherGeometry();
	void RasterizeGeometry(FCBHeightfield & OutHeightfield) const;
	void AppendGeometry(TArrayView<uint8 const> const RowCollisionData, TArray<FTransform> && PerInstanceTransform);
	void AppendAreaNavModifiers(TArrayView<FAreaNavModifier const> const Areas, TArray<FTransform> && PerInstanceTransform);
	void GatherNavigationRelevantData(TArray<FCBNavigationDirtyArea> const & DirtyAreas);
	void GatherNavigationRelevantData(FIntRect const & GridRect, FNavDataConfig const & NavDataConfig, UNavigationSystemV1 & NavSystem, FNavigationOctree const & NavOctree, bool const bExportGeometry);
	void GatherTileOverlappingNavigationGridRects(TArray<FIntRect> const & InNavigationGridRects);
	void GenerateNavigationData();
	void GenerateNavigationDataLayer(FCBNavGridLayer & OutNavGridLayer) const;
	void FilterNavigableGridCells(FCBNavGridLayer & OutNavGridLayer) const;
	void MarkDynamicAreas(FCBNavGridLayer & OutNavGridLayer) const;

	TUniquePtr<FCBNavGridLayer> GeneratedNavigationData;
	TSharedPtr<FCBNavGridLayer const> PreviousNavigationData;
	TUniquePtr<FCBHeightfield> GeneratedHeightfield;
	TSharedPtr<FCBHeightfield const> PreviousHeightfield;

	FCBNavGridGenerator const & ParentGenerator;
	FIntPoint const TileCoord;
	TArray<FCBPreparedNavigationRelevantData> PreparedNavigationRelevantData;
	TArray<FCBGeometry> CollisionGeometry;
	TArray<FCBAreaNavModifierCollection> AreaNavModifierCollections;
	TArray<FIntRect> GeometryDirtyGridRects;
	TArray<FIntRect> ModifiersOnlyDirtyGridRects;
	TArray<FIntRect> NavigationGridRects;
	UE::Tasks::FTask GenerateNavigationDataTask;
	FCBNavGridBuildConfig const Config;
	uint8 bIsFullyEncapsulatedByNavigationGridRect : 1;
};

FIntPoint FCBNavGridTileGenerator::GetTileCoord() const
{
	return TileCoord;
}

FIntRect FCBNavGridTileGenerator::GetTileGridRect() const
{
	FIntPoint const Min = Config.GridTileSize * TileCoord;
	FIntPoint const Max = Min + Config.GridTileSize;
	return FIntRect{ Min, Max };
}

bool FCBNavGridTileGenerator::WaitForNavigationDataGeneration() const
{
	return GenerateNavigationDataTask.Wait();
}

TUniquePtr<FCBNavGridLayer> & FCBNavGridTileGenerator::GetNavigationData()
{
	return GeneratedNavigationData;
}

TUniquePtr<FCBHeightfield> & FCBNavGridTileGenerator::GetHeightfield()
{
	return GeneratedHeightfield;
}

bool FCBNavGridTileGenerator::IsGenerationCompleted() const
{
	return GenerateNavigationDataTask.IsCompleted();
}

bool FCBNavGridTileGenerator::HasDataToGenerate() const
{
	return IntersectsNavigationGridRects() && (!GeometryDirtyGridRects.IsEmpty() || !ModifiersOnlyDirtyGridRects.IsEmpty());
}

bool FCBNavGridTileGenerator::HasGenerationStarted() const
{
	return GenerateNavigationDataTask.IsValid();
}

bool FCBNavGridTileGenerator::IntersectsNavigationGridRects() const
{
	return bIsFullyEncapsulatedByNavigationGridRect || !NavigationGridRects.IsEmpty();
}
