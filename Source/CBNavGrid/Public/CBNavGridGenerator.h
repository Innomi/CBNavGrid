#pragma once

#include "AI/NavDataGenerator.h"
#include "AI/Navigation/NavigationDirtyArea.h"
#include "CBNavGrid.h"
#include "Containers/RingBuffer.h"
#include "CoreMinimal.h"

class FCBNavGridTileGenerator;

struct CBNAVGRID_API FCBNavGridBuildConfig
{
	FCBNavGridBuildConfig();

	FIntPoint GridTileSize;
	float GridCellSize;
	float MaxNavigableCellHeightsDifference;
	float MinZ;
	float MaxZ;
};

/** Contains data about dirty area relevant for FCBNavGridTileGenerator. */
struct CBNAVGRID_API FCBNavigationDirtyArea
{
	FORCEINLINE bool HasFlag(ENavigationDirtyFlag const Flag) const;

	FIntRect GridRect;
	ENavigationDirtyFlag Flags;
};

/**
 * Class that handles generation of the whole navigation grid.
 */
class CBNAVGRID_API FCBNavGridGenerator : public FNavDataGenerator
{
public:
	explicit FCBNavGridGenerator(ACBNavGrid & InDestNavGrid);
	virtual ~FCBNavGridGenerator();

	/** Prevents copying. */
	FCBNavGridGenerator(FCBNavGridGenerator const &) = delete;
	FCBNavGridGenerator & operator =(FCBNavGridGenerator const &) = delete;

	/**
	 * Performs initial setup of member variables so that generator is ready to
	 * do its thing from this point on. Called just after construction by ACBNavGrid.
	 */
	virtual void Init();

	virtual bool RebuildAll() override;
	virtual void EnsureBuildCompletion() override;
	virtual void CancelBuild() override;
	virtual void TickAsyncBuild(float const DeltaSeconds) override;
	virtual void OnNavigationBoundsChanged() override;

	/** Asks generator to generate navigation data for tiles affected by DirtyAreas. */
	virtual void RebuildDirtyAreas(TArray<FNavigationDirtyArea> const & DirtyAreas) override;

	/** Determines whether this generator is performing navigation building actions at the moment. */
	virtual bool IsBuildInProgressCheckDirty() const override;

	virtual int32 GetNumRemaningBuildTasks() const override;
	virtual int32 GetNumRunningBuildTasks() const override;
	FORCEINLINE ACBNavGrid const & GetOwner() const;
	FORCEINLINE UWorld * GetWorld() const;
	FORCEINLINE FCBNavGridBuildConfig const & GetConfig() const;
	FORCEINLINE TArray<FIntRect> const & GetNavigationGridRects() const;

protected:
	/** Used to configure Config. Override to influence build properties. */
	virtual void ConfigureBuildProperties(FCBNavGridBuildConfig & OutConfig);
	void RebuildDirtyAreas(TArray<FCBNavigationDirtyArea> const & DirtyAreas);

	/** Updates cached list of navigation bounds. */
	void UpdateNavigationBounds();

	/**
	 * Launches pending tile generation tasks.
	 * @param TasksToLaunchMaxNum Maximum number of tasks to launch.
	 * @return Number of tasks launched.
	 */
	int32 LaunchPendingTileGenerationTasks(int32 const MaxTasksToLaunch);

	/**
	 * Iterates over running tile generation tasks, and processes completed ones.
	 * @return Num of precessed tasks.
	 */
	int32 ProcessCompletedTileGenerationTasks();

	void WaitForRunningTileGenerationTasks() const;

	struct FPendingTile
	{
		explicit FPendingTile(FIntPoint const InCoord);
		bool operator ==(FPendingTile const & Other) const;

		FIntPoint Coord;
		TArray<FCBNavigationDirtyArea> DirtyAreas;
	};

	friend uint32 GetTypeHash(FPendingTile const & Tile);

	struct FRunningTile
	{
		explicit FRunningTile(FIntPoint const InCoord);
		bool operator ==(FIntPoint const InCoord) const;

		FIntPoint Coord;
		TUniquePtr<FCBNavGridTileGenerator> TileGenerator;
	};

	/** Navigation grid that owns this generator. */
	ACBNavGrid & DestNavGrid;

	/** Cached list of navigation grid rects. */
	TArray<FIntRect> NavigationGridRects;

	/** List of tiles that need to be regenerated. */
	TRingBuffer<FPendingTile> PendingTiles;

	/** List of tiles currently being regenerated. */
	TArray<FRunningTile> RunningTiles;
	
	/** Parameters defining NavGrid. */
	FCBNavGridBuildConfig Config;

	/** The limit to number of asynchronous tile generators running at one time. */
	int32 MaxTileGeneratorTasks;
};

bool FCBNavigationDirtyArea::HasFlag(ENavigationDirtyFlag const Flag) const
{
	return (Flags & Flag) != ENavigationDirtyFlag::None;
}

ACBNavGrid const & FCBNavGridGenerator::GetOwner() const
{
	return DestNavGrid;
}

UWorld * FCBNavGridGenerator::GetWorld() const
{
	return GetOwner().GetWorld();
}

FCBNavGridBuildConfig const & FCBNavGridGenerator::GetConfig() const
{
	return Config;
}

TArray<FIntRect> const & FCBNavGridGenerator::GetNavigationGridRects() const
{
	return NavigationGridRects;
}
