#include "CBNavGridTileGenerator.h"
#include "AI/NavigationModifier.h"
#include "GeomTools.h"
#include "NavAreas/NavArea_Null.h"
#include "CBHeightfield.h"
#include "CBNavGrid.h"
#include "CBNavGridLayer.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastGeometryExport.h"

#include <concepts>

namespace
{
	struct FRecastGeometryCache
	{
		struct FHeader
		{
			FNavigationRelevantData::FCollisionDataHeader Validation;
			int32 NumVerts;
			int32 NumFaces;
			FWalkableSlopeOverride SlopeOverride;
		};

		FHeader Header;

		/** Recast coords of vertices (size: NumVerts * 3). */
		FVector::FReal const * Verts;

		/** Vert indices for triangles (size: NumFaces * 3). */
		int32 const * Indices;

		FRecastGeometryCache(uint8 const * const Memory);
	};

	FRecastGeometryCache::FRecastGeometryCache(uint8 const * const Memory)
	{
		Header = *reinterpret_cast<FHeader const *>(Memory);
		Verts = reinterpret_cast<FVector::FReal const *>(Memory + sizeof(FRecastGeometryCache));
		Indices = reinterpret_cast<int32 const *>(Memory + sizeof(FRecastGeometryCache) + (sizeof(FVector::FReal) * Header.NumVerts * 3));
	}

	FBox GetBox(FIntRect const & Rect, float const GridCellSize, float const MinZ, float const MaxZ)
	{
		FVector const Min{ Rect.Min.X * GridCellSize, Rect.Min.Y * GridCellSize, MinZ };
		FVector const Max{ Rect.Max.X * GridCellSize, Rect.Max.Y * GridCellSize, MaxZ };
		return FBox{ Min, Max };
	}

	bool IsPointInsideOrOnRect(FIntPoint const Point, FIntRect const & Rect)
	{
		return Point.X >= Rect.Min.X && Point.X <= Rect.Max.X && Point.Y >= Rect.Min.Y && Point.Y <= Rect.Max.Y;
	}

	bool IsRectInsideRect(FIntRect const & SmallRect, FIntRect const & BigRect)
	{
		return BigRect.Contains(SmallRect.Min) && IsPointInsideOrOnRect(SmallRect.Max, BigRect);
	}

	void ConvertCoordsRecastToUnreal(FVector * Vertices, FVector::FReal const * Coords, int32 const VerticesNum)
	{
		FVector const * const VerticesEnd = Vertices + VerticesNum;
		for (; Vertices != VerticesEnd; ++Vertices)
		{
			FVector & Vertex = *Vertices;
			Vertex[0] = -Coords[0];
			Vertex[1] = -Coords[2];
			Vertex[2] = Coords[1];
			Coords += 3;
		}
	}

	void ConvertCoordsToVertices(FVector * Vertices, FVector::FReal const * Coords, int32 const VerticesNum)
	{
		FVector const * const VerticesEnd = Vertices + VerticesNum;
		for (; Vertices != VerticesEnd; ++Vertices)
		{
			FVector & Vertex = *Vertices;
			Vertex[0] = Coords[0];
			Vertex[1] = Coords[1];
			Vertex[2] = Coords[2];
			Coords += 3;
		}
	}

	void TransformVertices(TArray<FVector> & OutVertices, TArrayView<FVector const> const InVertices, FTransform const & Transform)
	{
		OutVertices.Reset(InVertices.Num());
		for (FVector const & InVertex : InVertices)
		{
			OutVertices.Add(Transform.TransformPosition(InVertex));
		}
	}

	template <std::invocable<FIntPoint> FuncType>
	void ForRect(FIntRect const & Rect, FuncType && Func)
	{
		for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
			{
				FIntPoint const Coord{ X, Y };
				Func(Coord);
			}
		}
	}

	enum class EGridCellsUpdateMethod
	{
		ModifiersOnly,
		GeometryChanged
	};

	template <EGridCellsUpdateMethod UpdateMethod>
	void SetGridCellsData(FCBNavGridLayer & OutNavGridLayer, FCBHeightfield const & Heightfield, TArrayView<FIntRect const> const GridRects, float const MaxNavigableCellHeightsDifference)
	{
		for (FIntRect const & GridRect : GridRects)
		{
			ForRect(GridRect, [&OutNavGridLayer, &Heightfield, MaxNavigableCellHeightsDifference](FIntPoint const Coord)
				{
					FCBSpan const * Span = Heightfield.GetSpans(Coord);
					bool const bIsOccupied = Span ? Span->Max - Span->Min > MaxNavigableCellHeightsDifference : true;
					OutNavGridLayer.SetCellState(Coord, bIsOccupied);
					if constexpr (UpdateMethod == EGridCellsUpdateMethod::GeometryChanged)
					{
						if (Span)
						{
							float const CellHeight = (Span->Min + Span->Max) / 2.f;
							OutNavGridLayer.SetCellHeight(Coord, CellHeight);
						}
					}
				});
		}
	}

	void MarkDynamicArea(FAreaNavModifier const & Modifier, FTransform const & LocalToWorld, FCBNavGridLayer & OutLayer)
	{
		bool const bIsOccupied = true;
		switch (Modifier.GetShapeType())
		{
			case ENavigationShapeType::Cylinder:
			{
				FCylinderNavAreaData CylinderData;
				Modifier.GetCylinder(CylinderData);

				FVector Scale3D = LocalToWorld.GetScale3D().GetAbs();
				CylinderData.Radius = CylinderData.Radius * FMath::Max(Scale3D.X, Scale3D.Y);
				CylinderData.Origin = LocalToWorld.TransformPosition(CylinderData.Origin);

				OutLayer.SetCellsStateInCircle(static_cast<FVector2d>(CylinderData.Origin), CylinderData.Radius, bIsOccupied);
				break;
			}
			case ENavigationShapeType::Box:
			{
				FBoxNavAreaData BoxData;
				Modifier.GetBox(BoxData);

				FBox const WorldBox = FBox::BuildAABB(BoxData.Origin, BoxData.Extent).TransformBy(LocalToWorld);
				FBox2d const WorldBox2d{ static_cast<FVector2d>(WorldBox.Min), static_cast<FVector2d>(WorldBox.Max) };

				OutLayer.SetCellsStateInBox(WorldBox2d, bIsOccupied);
				break;
			}
			case ENavigationShapeType::Convex:
			case ENavigationShapeType::InstancedConvex:
			{
				FConvexNavAreaData ConvexData;
				if (Modifier.GetShapeType() == ENavigationShapeType::InstancedConvex)
				{
					Modifier.GetPerInstanceConvex(LocalToWorld, ConvexData);
				}
				else
				{
					Modifier.GetConvex(ConvexData);
				}

				// For some reason GenerateConvexHullFromPoints has Points argument with no const modifier as for now(UE5.5).
				TArray<FVector2d> Points = UE::LWC::ConvertArrayType<FVector2d>(ConvexData.Points);
				TArray<FVector2d> CCWConvex;
				FGeomTools2D::GenerateConvexHullFromPoints(CCWConvex, Points);

				OutLayer.SetCellsStateInConvex(CCWConvex, bIsOccupied);
				break;
			}
		}
	}
} // namespace

FCBNavGridTileGenerator::FCBNavGridTileGenerator(FCBNavGridGenerator const & InParentGenerator, FIntPoint const InTileCoord)
	: ParentGenerator(InParentGenerator)
	, TileCoord(InTileCoord)
	, Config(InParentGenerator.GetConfig())
	, bIsFullyEncapsulatedByNavigationGridRect(false)
{
	check(Config.MinZ <= Config.MaxZ);
	
	ACBNavGrid const & ParentGeneratorOwner = ParentGenerator.GetOwner();
	PreviousNavigationData = ParentGeneratorOwner.GetTileNavigationData(TileCoord);
	PreviousHeightfield = ParentGeneratorOwner.GetTileHeightfield(TileCoord);
	GatherTileOverlappingNavigationGridRects(ParentGenerator.GetNavigationGridRects());
}

void FCBNavGridTileGenerator::GenerateNavigationDataAsync(TArray<FCBNavigationDirtyArea> const & DirtyAreas)
{
	if (!IntersectsNavigationGridRects() || HasGenerationStarted())
	{
		return;
	}

	GatherNavigationRelevantData(DirtyAreas);

	if (GeometryDirtyGridRects.IsEmpty() && ModifiersOnlyDirtyGridRects.IsEmpty())
	{
		return;
	}

	GenerateNavigationDataTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [this]() { GenerateNavigationData(); });
}

void FCBNavGridTileGenerator::GatherGeometry()
{
	for (FCBPreparedNavigationRelevantData & PreparedData : PreparedNavigationRelevantData)
	{
		if (PreparedData.NavigationRelevantData->HasGeometry() && PreparedData.NavigationRelevantData->IsCollisionDataValid())
		{
			TNavStatArray<uint8> const & CollisionData = PreparedData.NavigationRelevantData->CollisionData;
			TArrayView<uint8 const> const CollisionDataView{ CollisionData.GetData(), CollisionData.Num() };
			AppendGeometry(CollisionDataView, MoveTemp(PreparedData.PerInstanceTransform));
		}
		if (PreparedData.NavigationRelevantData->Modifiers.HasAreas())
		{
			TArray<FAreaNavModifier> const & Areas = PreparedData.NavigationRelevantData->Modifiers.GetAreas();
			TArrayView<FAreaNavModifier const> const AreasView{ Areas.GetData(), Areas.Num() };
			AppendAreaNavModifiers(AreasView, MoveTemp(PreparedData.PerInstanceTransform));
		}
	}
	PreparedNavigationRelevantData.Empty();
}

void FCBNavGridTileGenerator::RasterizeGeometry(FCBHeightfield & OutHeightfield) const
{
	for (FCBGeometry const & Geometry : CollisionGeometry)
	{
		if (Geometry.PerInstanceTransform.IsEmpty())
		{
			OutHeightfield.RasterizeTriangles(Geometry.Vertices, Geometry.Indices);
		}
		else
		{
			TArray<FVector> TransformedVertices;
			for (FTransform const & Transform : Geometry.PerInstanceTransform)
			{
				TransformVertices(TransformedVertices, Geometry.Vertices, Transform);
				OutHeightfield.RasterizeTriangles(TransformedVertices, Geometry.Indices);
			}
		}
	}

	// Preserves only the highest layer.
	OutHeightfield.Shrink(1);
}

void FCBNavGridTileGenerator::AppendGeometry(TArrayView<uint8 const> const RawCollisionData, TArray<FTransform> && PerInstanceTransform)
{
	if (RawCollisionData.IsEmpty())
	{
		return;
	}

	FCBGeometry Geometry;
	// In UE5.5 geometry is cached in recast coordinates by navigation octree. Hopefully will be changed later.
	FRecastGeometryCache CollisionCache(RawCollisionData.GetData());
	int32 const VerticesNum = CollisionCache.Header.NumVerts;
	int32 const IndicesNum = CollisionCache.Header.NumFaces * 3;
	if (IndicesNum <= 0)
	{
		return;
	}
	Geometry.Vertices.SetNumUninitialized(VerticesNum);
	Geometry.Indices.SetNumUninitialized(IndicesNum);
	ConvertCoordsRecastToUnreal(Geometry.Vertices.GetData(), CollisionCache.Verts, VerticesNum);
	FMemory::Memcpy(Geometry.Indices.GetData(), CollisionCache.Indices, sizeof(int32) * IndicesNum);

	Geometry.PerInstanceTransform = MoveTemp(PerInstanceTransform);

	CollisionGeometry.Add(MoveTemp(Geometry));
}

void FCBNavGridTileGenerator::AppendAreaNavModifiers(TArrayView<FAreaNavModifier const> const Areas, TArray<FTransform> && PerInstanceTransform)
{
	FCBAreaNavModifierCollection AreaNavModifierCollection;

	for (FAreaNavModifier const & Area : Areas)
	{
		// Doesn't support any area classes except for null.
		if (Area.GetAreaClass() == UNavArea_Null::StaticClass())
		{
			AreaNavModifierCollection.Areas.Emplace(Area);
		}
	}
	if (AreaNavModifierCollection.Areas.IsEmpty())
	{
		return;
	}
	AreaNavModifierCollection.PerInstanceTransform = MoveTemp(PerInstanceTransform);

	AreaNavModifierCollections.Add(MoveTemp(AreaNavModifierCollection));
}

void FCBNavGridTileGenerator::GatherNavigationRelevantData(TArray<FCBNavigationDirtyArea> const & DirtyAreas)
{
	PreparedNavigationRelevantData.Reset(DirtyAreas.Num());
	CollisionGeometry.Reset(DirtyAreas.Num());
	AreaNavModifierCollections.Reset(DirtyAreas.Num());
	GeometryDirtyGridRects.Reset(DirtyAreas.Num());
	ModifiersOnlyDirtyGridRects.Reset(DirtyAreas.Num());

	UWorld * const World = ParentGenerator.GetWorld();
	UNavigationSystemV1 * const NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FNavigationOctree const * const NavOctree = NavSystem ? NavSystem->GetNavOctree() : nullptr;

	if (!NavOctree)
	{
		return;
	}

	FIntRect const TileGridRect = GetTileGridRect();
	ANavigationData const & NavigationData = ParentGenerator.GetOwner();
	bool const bExportGeometry = !World->IsGameWorld() || (NavigationData.GetRuntimeGenerationMode() == ERuntimeGenerationType::Dynamic);

	for (FCBNavigationDirtyArea const & DirtyArea : DirtyAreas)
	{
		FIntRect AdjustedDirtyGridRect = DirtyArea.GridRect;
		AdjustedDirtyGridRect.Clip(TileGridRect);
		
		if (AdjustedDirtyGridRect.IsEmpty())
		{
			continue;
		}

		if (bExportGeometry && (DirtyArea.HasFlag(ENavigationDirtyFlag::Geometry) || DirtyArea.HasFlag(ENavigationDirtyFlag::NavigationBounds)))
		{
			GeometryDirtyGridRects.Add(AdjustedDirtyGridRect);
			GatherNavigationRelevantData(AdjustedDirtyGridRect, NavigationData.GetConfig(), *NavSystem, *NavOctree, bExportGeometry);
		}
		else if (DirtyArea.HasFlag(ENavigationDirtyFlag::DynamicModifier))
		{
			ModifiersOnlyDirtyGridRects.Add(AdjustedDirtyGridRect);
			bool const bDontExportGeometry = false;
			GatherNavigationRelevantData(AdjustedDirtyGridRect, NavigationData.GetConfig(), *NavSystem, *NavOctree, bDontExportGeometry);
		}
	}
}

void FCBNavGridTileGenerator::GatherNavigationRelevantData(FIntRect const & GridRect, FNavDataConfig const & NavDataConfig, UNavigationSystemV1 & NavSystem, FNavigationOctree const & NavOctree, bool const bExportGeometry)
{
	// Demands lazy data gathering, gathers geometry slices and gathers transforms if delegates are provided.
	// In UE5.5 recast nav mesh generation it is made on background thread, it looks suspicious and not thread safe to do so.
	// Also there are reports about crashes during async lazy gathering on UE5.5 in "Unreal Source" discord.
	FBox const BoundingBox = GetBox(GridRect, Config.GridCellSize, Config.MinZ, Config.MaxZ);
	NavOctree.FindElementsWithBoundsTest(BoundingBox,
		[this, &BoundingBox, &NavSystem, &NavDataConfig, bExportGeometry](FNavigationOctreeElement const & Element)
		{
			if (Element.ShouldUseGeometry(NavDataConfig))
			{
				FNavigationRelevantData & NavigationRelevantData = Element.Data.Get();
				if (NavigationRelevantData.NeedAnyPendingLazyModifiersGathering())
				{
					NavSystem.DemandLazyDataGathering(NavigationRelevantData);
				}
				if (bExportGeometry && NavigationRelevantData.IsPendingLazyGeometryGathering())
				{
					if (NavigationRelevantData.SupportsGatheringGeometrySlices())
					{
						// Gathers geometry on game thread, since there are no guaranties that provided delegate is thread safe.
						FRecastGeometryExport GeometryExport(NavigationRelevantData);
						NavigationRelevantData.SourceElement->GeometrySliceExportDelegate.Execute(NavigationRelevantData.SourceElement.Get(), GeometryExport, BoundingBox);
						FCBGeometry Geometry;
						// Ugly convert. Yes, it could be done by simple reinterpret,
						// since array of coords has the same layout in memory as array of vertices.
						// But it is UB due to the cpp standard if i'm not mistaken. Have no time right now to investigate it.
						Geometry.Vertices.AddUninitialized(GeometryExport.VertexBuffer.Num() / 3);
						ConvertCoordsToVertices(Geometry.Vertices.GetData(), GeometryExport.VertexBuffer.GetData(), GeometryExport.VertexBuffer.Num() / 3);
						Geometry.Indices = MoveTemp(GeometryExport.IndexBuffer);
						CollisionGeometry.Add(MoveTemp(Geometry));
					}
					else
					{
						NavSystem.DemandLazyDataGathering(NavigationRelevantData);
					}
				}
				if ((bExportGeometry && NavigationRelevantData.HasGeometry()) || NavigationRelevantData.Modifiers.HasAreas())
				{
					TArray<FTransform> PerInstanceTransform;
					FNavDataPerInstanceTransformDelegate const & TransformDelegate = NavigationRelevantData.NavDataPerInstanceTransformDelegate;
					if (TransformDelegate.IsBound())
					{
						TransformDelegate.Execute(BoundingBox, PerInstanceTransform);
						// Skips in case there are no instances in the area.
						if (PerInstanceTransform.IsEmpty())
						{
							return;
						}
					}
					PreparedNavigationRelevantData.Emplace(Element.Data, MoveTemp(PerInstanceTransform));
				}
			}
		});
}

void FCBNavGridTileGenerator::GatherTileOverlappingNavigationGridRects(TArray<FIntRect> const & InNavigationGridRects)
{
	NavigationGridRects.Reset(InNavigationGridRects.Num());
	bIsFullyEncapsulatedByNavigationGridRect = false;

	FIntRect const TileGridRect = GetTileGridRect();
	for (FIntRect const & NavigationGridRect : InNavigationGridRects)
	{
		if (NavigationGridRect.Intersect(TileGridRect))
		{
			// If tile is fully encapsulated, no need to continue.
			if (IsRectInsideRect(TileGridRect, NavigationGridRect))
			{
				NavigationGridRects.Reset();
				bIsFullyEncapsulatedByNavigationGridRect = true;
				break;
			}
			NavigationGridRects.Add(NavigationGridRect);
		}
	}
}

void FCBNavGridTileGenerator::GenerateNavigationData()
{
	GatherGeometry();

	if (!GeometryDirtyGridRects.IsEmpty())
	{
		if (PreviousHeightfield)
		{
			GeneratedHeightfield = MakeUnique<FCBHeightfield>(*PreviousHeightfield);
			for (FIntRect const & DirtyGridRect : GeometryDirtyGridRects)
			{
				GeneratedHeightfield->Clear(DirtyGridRect);
			}
		}
		else
		{
			GeneratedHeightfield = MakeUnique<FCBHeightfield>(GetTileGridRect(), Config.GridCellSize);
		}
		RasterizeGeometry(*GeneratedHeightfield);
	}

	if (PreviousNavigationData)
	{
		GeneratedNavigationData = MakeUnique<FCBNavGridLayer>(*PreviousNavigationData);
	}
	else
	{
		GeneratedNavigationData = MakeUnique<FCBNavGridLayer>(GetTileGridRect(), Config.GridCellSize, ENoInit::NoInit);
	}

	GenerateNavigationDataLayer(*GeneratedNavigationData);
}

void FCBNavGridTileGenerator::GenerateNavigationDataLayer(FCBNavGridLayer & OutNavGridLayer) const
{
	FCBHeightfield const * const Heightfield = GeneratedHeightfield ? GeneratedHeightfield.Get() : PreviousHeightfield.Get();
	if (!Heightfield)
	{
		return;
	}

	SetGridCellsData<EGridCellsUpdateMethod::GeometryChanged>(OutNavGridLayer, *Heightfield, GeometryDirtyGridRects, Config.MaxNavigableCellHeightsDifference);
	SetGridCellsData<EGridCellsUpdateMethod::ModifiersOnly>(OutNavGridLayer, *Heightfield, ModifiersOnlyDirtyGridRects, Config.MaxNavigableCellHeightsDifference);
	MarkDynamicAreas(OutNavGridLayer);
	FilterNavigableGridCells(OutNavGridLayer);
}

void FCBNavGridTileGenerator::FilterNavigableGridCells(FCBNavGridLayer & OutNavGridLayer) const
{
	if (bIsFullyEncapsulatedByNavigationGridRect)
	{
		return;
	}

	ForRect(GetTileGridRect(), [this, &OutNavGridLayer](FIntPoint const Coord)
		{
			for (FIntRect const & NavigationGridRect : NavigationGridRects)
			{
				if (NavigationGridRect.Contains(Coord))
				{
					return;
				}
			}
			bool const bIsOccupied = true;
			OutNavGridLayer.SetCellState(Coord, bIsOccupied);
		});
}

void FCBNavGridTileGenerator::MarkDynamicAreas(FCBNavGridLayer & OutNavGridLayer) const
{
	for (FCBAreaNavModifierCollection const & AreaNavModifierCollection : AreaNavModifierCollections)
	{
		for (FAreaNavModifier const & Area : AreaNavModifierCollection.Areas)
		{
			if (AreaNavModifierCollection.PerInstanceTransform.IsEmpty())
			{
				MarkDynamicArea(Area, FTransform::Identity, OutNavGridLayer);
			}
			else
			{
				for (FTransform const & LocalToWorld : AreaNavModifierCollection.PerInstanceTransform)
				{
					MarkDynamicArea(Area, LocalToWorld, OutNavGridLayer);
				}
			}
		}
	}
}
