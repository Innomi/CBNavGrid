#include "CBNavGridGenerator.h"
#include "AI/Navigation/NavigationDirtyArea.h"
#include "AI/NavigationModifier.h"
#include "CBGridUtilities.h"
#include "CBHeightfield.h"
#include "CBNavGridTileGenerator.h"
#include "NavigationSystem.h"

FCBNavGridBuildConfig::FCBNavGridBuildConfig()
	: GridTileSize(128, 128)
	, GridCellSize(100.)
	, MaxNavigableCellHeightsDifference(50.)
	, MinZ(-1e9f)
	, MaxZ(1e9f)
{
}

FCBNavGridGenerator::FCBNavGridGenerator(ACBNavGrid & InDestNavGrid)
	: DestNavGrid(InDestNavGrid)
	, MaxTileGeneratorTasks(1)
{
}

FCBNavGridGenerator::~FCBNavGridGenerator()
{
	CancelBuild();
}

void FCBNavGridGenerator::Init()
{
	ConfigureBuildProperties(Config);

	MaxTileGeneratorTasks = FMath::Max(FTaskGraphInterface::Get().GetNumWorkerThreads() * 2, 1);
	UE_LOGFMT(LogNavigation, Log, "Using max of {0} workers to build navigation grid.", MaxTileGeneratorTasks);

	UpdateNavigationBounds();
}

bool FCBNavGridGenerator::RebuildAll()
{
	TArray<FCBNavigationDirtyArea> DirtyAreas;
	for (FIntRect const & NavigationGridRect : GetNavigationGridRects())
	{
		FCBNavigationDirtyArea const DirtyArea{ NavigationGridRect, ENavigationDirtyFlag::All | ENavigationDirtyFlag::NavigationBounds };
		DirtyAreas.Add(DirtyArea);
	}
	if (!DirtyAreas.IsEmpty())
	{
		RebuildDirtyAreas(DirtyAreas);
	}
	return true;
}

void FCBNavGridGenerator::EnsureBuildCompletion()
{
	while (GetNumRemaningBuildTasks() > 0)
	{
		WaitForRunningTileGenerationTasks();
		ProcessCompletedTileGenerationTasks();
		LaunchPendingTileGenerationTasks(MaxTileGeneratorTasks);
	}
}

void FCBNavGridGenerator::CancelBuild()
{
	PendingTiles.Empty();
	WaitForRunningTileGenerationTasks();
	RunningTiles.Empty();
}

void FCBNavGridGenerator::TickAsyncBuild(float const DeltaSeconds)
{
	ProcessCompletedTileGenerationTasks();
	LaunchPendingTileGenerationTasks(MaxTileGeneratorTasks - GetNumRunningBuildTasks());
}

void FCBNavGridGenerator::OnNavigationBoundsChanged()
{
	UpdateNavigationBounds();
}

void FCBNavGridGenerator::RebuildDirtyAreas(TArray<FNavigationDirtyArea> const & DirtyAreas)
{
	TArray<FCBNavigationDirtyArea> UEDirtyAreas;
	UEDirtyAreas.Reserve(DirtyAreas.Num());
	for (FNavigationDirtyArea const & DirtyArea : DirtyAreas)
	{
		FCBNavigationDirtyArea const UEDirtyArea{ CBGridUtilities::GetGridRectFromBoundingBox(DirtyArea.Bounds, Config.GridCellSize), DirtyArea.Flags };
		UEDirtyAreas.Add(UEDirtyArea);
	}
	RebuildDirtyAreas(UEDirtyAreas);
}

bool FCBNavGridGenerator::IsBuildInProgressCheckDirty() const
{
	return !RunningTiles.IsEmpty() || !PendingTiles.IsEmpty();
}

int32 FCBNavGridGenerator::GetNumRemaningBuildTasks() const
{
	return RunningTiles.Num() + PendingTiles.Num();
}

int32 FCBNavGridGenerator::GetNumRunningBuildTasks() const
{
	return RunningTiles.Num();
}

void FCBNavGridGenerator::ConfigureBuildProperties(FCBNavGridBuildConfig & OutConfig)
{
	OutConfig.GridCellSize = DestNavGrid.GetGridCellSize();
	OutConfig.GridTileSize = DestNavGrid.GetTileSize();
	OutConfig.MaxNavigableCellHeightsDifference = DestNavGrid.GetMaxNavigableCellHeightsDifference();
	OutConfig.MinZ = DestNavGrid.GetMinZ();
	OutConfig.MaxZ = DestNavGrid.GetMaxZ();
}

void FCBNavGridGenerator::RebuildDirtyAreas(TArray<FCBNavigationDirtyArea> const & DirtyAreas)
{
	// Finds all tiles that need regeneration.
	TSet<FPendingTile> DirtyTiles;

	for (FCBNavigationDirtyArea const & DirtyArea : DirtyAreas)
	{
		FIntRect const TileRect = CBGridUtilities::GetTileRect(DirtyArea.GridRect, Config.GridTileSize);
		for (int32 TileX = TileRect.Min.X; TileX < TileRect.Max.X; ++TileX)
		{
			for (int32 TileY = TileRect.Min.Y; TileY < TileRect.Max.Y; ++TileY)
			{
				FPendingTile Tile{ FIntPoint{ TileX, TileY } };
				FPendingTile * ExistingTile = DirtyTiles.Find(Tile);
				if (ExistingTile)
				{
					ExistingTile->DirtyAreas.Add(DirtyArea);
				}
				else
				{
					Tile.DirtyAreas.Add(DirtyArea);
					DirtyTiles.Add(MoveTemp(Tile));
				}
			}
		}
	}

	// Merges new dirty tiles info with existing pending tiles.
	for (FPendingTile & ExistingTile : PendingTiles)
	{
		FSetElementId const Id = DirtyTiles.FindId(ExistingTile);
		if (Id.IsValidId())
		{
			FPendingTile const & DirtyTile = DirtyTiles[Id];
			ExistingTile.DirtyAreas.Append(DirtyTile.DirtyAreas);
			DirtyTiles.Remove(Id);
		}
	}

	// Appends remaining new dirty tiles.
	PendingTiles.Reserve(PendingTiles.Num() + DirtyTiles.Num());
	for (FPendingTile const & Tile : DirtyTiles)
	{
		PendingTiles.Add(Tile);
	}
}

void FCBNavGridGenerator::UpdateNavigationBounds()
{
	NavigationGridRects.Reset();

	for (FBox const & NavigationBoundingBox : DestNavGrid.GetNavigableBounds())
	{
		NavigationGridRects.Add(CBGridUtilities::GetGridRectFromBoundingBox(NavigationBoundingBox, Config.GridCellSize));
	}
}

int32 FCBNavGridGenerator::LaunchPendingTileGenerationTasks(int32 const MaxTasksToLaunch)
{
	int32 LaunchedTasksNum = 0;
	for (int32 PendingTileIndex = 0; PendingTileIndex < PendingTiles.Num() && LaunchedTasksNum < MaxTasksToLaunch; ++PendingTileIndex)
	{
		FPendingTile const & PendingTile = PendingTiles[PendingTileIndex];

		// Cannot start generating data for same tile in parallel, since results may not be ordered in a right way.
		if (!RunningTiles.Contains(PendingTile.Coord))
		{
			RunningTiles.Emplace(PendingTile.Coord);
			FRunningTile & RunningTile = RunningTiles.Last();
			RunningTile.TileGenerator = MakeUnique<FCBNavGridTileGenerator>(*this, PendingTile.Coord);
			RunningTile.TileGenerator->GenerateNavigationDataAsync(PendingTile.DirtyAreas);
			PendingTiles.RemoveAt(PendingTileIndex);
			++LaunchedTasksNum;
		}
	}
	return LaunchedTasksNum;
}

int32 FCBNavGridGenerator::ProcessCompletedTileGenerationTasks()
{
	int32 CompletedTasksNum = 0;
	for (int32 RunningTileIndex = RunningTiles.Num() - 1; RunningTileIndex >= 0; --RunningTileIndex)
	{
		FRunningTile & RunningTile = RunningTiles[RunningTileIndex];
		check(RunningTile.TileGenerator);
		FCBNavGridTileGenerator & TileGenerator = *RunningTile.TileGenerator;

		if (TileGenerator.IsGenerationCompleted())
		{
			DestNavGrid.OnTileGenerationCompleted(TileGenerator.GetTileCoord(), MoveTemp(TileGenerator.GetNavigationData()), MoveTemp(TileGenerator.GetHeightfield()));
			RunningTiles.RemoveAtSwap(RunningTileIndex, EAllowShrinking::No);
			++CompletedTasksNum;
		}
	}
	return CompletedTasksNum;
}

void FCBNavGridGenerator::WaitForRunningTileGenerationTasks() const
{
	for (FRunningTile const & Tile : RunningTiles)
	{
		Tile.TileGenerator->WaitForNavigationDataGeneration();
	}
}

FCBNavGridGenerator::FPendingTile::FPendingTile(FIntPoint const InCoord)
	: Coord(InCoord)
{
}

bool FCBNavGridGenerator::FPendingTile::operator ==(FPendingTile const & Other) const
{
	return Coord == Other.Coord;
}

uint32 GetTypeHash(FCBNavGridGenerator::FPendingTile const & Tile)
{
	return GetTypeHash(Tile.Coord);
}

FCBNavGridGenerator::FRunningTile::FRunningTile(FIntPoint const InCoord)
	: Coord(InCoord)
	, TileGenerator(nullptr)
{
}

bool FCBNavGridGenerator::FRunningTile::operator ==(FIntPoint const InCoord) const
{
	return Coord == InCoord;
}
