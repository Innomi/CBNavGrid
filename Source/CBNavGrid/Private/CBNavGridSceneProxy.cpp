#include "CBNavGridSceneProxy.h"
#include "CBNavGrid.h"
#include "CBNavGridLayer.h"
#include "CBNavGridRenderingComponent.h"
#include "Engine/Engine.h"
#include "Materials/MaterialRenderProxy.h"

namespace
{
	FColor DarkenColor(FColor const & Base)
	{
		uint32 const Color = Base.DWColor();
		return FColor{ ((Color >> 1) & 0x007f7f7f) | (Color & 0xff000000) };
	}

	bool IsBoundingBoxInView(FBox const & BoundingBox, FSceneView const * const View, double const DrawDistanceSq)
	{
		check(View);
		FVector const & ViewOrigin = View->ViewMatrices.GetViewOrigin();
		if (FVector::DistSquaredXY(BoundingBox.GetClosestPointTo(ViewOrigin), ViewOrigin) > DrawDistanceSq)
		{
			return false;
		}

		return View->ViewFrustum.IntersectBox(BoundingBox.GetCenter(), BoundingBox.GetExtent());
	}

	bool IsLineInView(FVector const & Start, FVector const & End, FSceneView const * const View, double const DrawDistanceSq)
	{
		check(View);
		FVector const & ViewOrigin = View->ViewMatrices.GetViewOrigin();
		if (FVector::DistSquaredXY(Start, ViewOrigin) > DrawDistanceSq && FVector::DistSquaredXY(End, ViewOrigin) > DrawDistanceSq)
		{
			return false;
		}

		return View->ViewFrustum.IntersectLineSegment(Start, End);
	}
} // namespace

FCBNavGridSceneProxy::FCBNavGridSceneProxy(UCBNavGridRenderingComponent const * const Component)
	: FPrimitiveSceneProxy{ Component }
	, VertexFactory(GetScene().GetFeatureLevel(), "FCBNavGridSceneProxy")
	, DrawDistanceSq(0.)
{
	ACBNavGrid const * const NavGrid = Component ? Cast<ACBNavGrid>(Component->GetOwner()) : nullptr;
	if (!NavGrid)
	{
		return;
	}

	GatherData(*NavGrid);
}

FCBNavGridSceneProxy::~FCBNavGridSceneProxy()
{
	VertexBuffers.PositionVertexBuffer.ReleaseResource();
	VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
	VertexBuffers.ColorVertexBuffer.ReleaseResource();
	IndexBuffer.ReleaseResource();
	VertexFactory.ReleaseResource();
}

SIZE_T FCBNavGridSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FCBNavGridSceneProxy::GetDynamicMeshElements(TArray<FSceneView const *> const & Views, FSceneViewFamily const & ViewFamily, uint32 const VisibilityMap, FMeshElementCollector & Collector) const
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			FSceneView const * const View = Views[ViewIndex];
			GetDynamicMeshElementsForView(View, ViewIndex, Collector);
		}
	}
}

uint32 FCBNavGridSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize();
}

SIZE_T FCBNavGridSceneProxy::GetAllocatedSize() const
{
	return FPrimitiveSceneProxy::GetAllocatedSize() +
		Tiles.GetAllocatedSize() +
		Lines.GetAllocatedSize() +
		IndexBuffer.Indices.GetAllocatedSize() +
		VertexBuffers.PositionVertexBuffer.GetNumVertices() * VertexBuffers.PositionVertexBuffer.GetStride() +
		VertexBuffers.StaticMeshVertexBuffer.GetResourceSize() +
		VertexBuffers.ColorVertexBuffer.GetNumVertices() * VertexBuffers.ColorVertexBuffer.GetStride() +
		sizeof(FColoredMaterialRenderProxy);
}

FPrimitiveViewRelevance FCBNavGridSceneProxy::GetViewRelevance(FSceneView const * const View) const
{
	bool const bIsVisible = View->Family->EngineShowFlags.Navigation && IsShown(View);
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = bIsVisible;
	Result.bDynamicRelevance = true;
	Result.bSeparateTranslucency = bIsVisible;
	Result.bNormalTranslucency = bIsVisible;
	return Result;
}

void FCBNavGridSceneProxy::GetDynamicMeshElementsForView(FSceneView const * const View, int32 const ViewIndex, FMeshElementCollector & Collector) const
{
	FPrimitiveDrawInterface * const PDI = Collector.GetPDI(ViewIndex);

	for (FTileRenderData const & Tile : Tiles)
	{
		if (!IsBoundingBoxInView(Tile.BoundingBox, View, DrawDistanceSq))
		{
			continue;
		}

		PDI->AddReserveLines(SDPG_World, Tile.LinesNum);
		for (int32 LineIndex = Tile.FirstLineIndex; LineIndex < Tile.FirstLineIndex + Tile.LinesNum; ++LineIndex)
		{
			FDebugRenderSceneProxy::FDebugLine const & Line = Lines[LineIndex];
			if (IsLineInView(Line.Start, Line.End, View, DrawDistanceSq))
			{
				Line.Draw(PDI);
			}
		}

		if (Tile.MeshBatchElement.NumPrimitives > 0)
		{
			FMeshBatch & Mesh = Collector.AllocateMesh();
			FMeshBatchElement & MeshBatchElement = Mesh.Elements[0];
			MeshBatchElement = Tile.MeshBatchElement;

			FDynamicPrimitiveUniformBuffer & DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), FMatrix::Identity, FMatrix::Identity, GetBounds(), GetLocalBounds(), false, false, AlwaysHasVelocity());
			MeshBatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

			Mesh.bWireframe = false;
			Mesh.VertexFactory = &VertexFactory;
			Mesh.MaterialRenderProxy = MaterialRenderProxy.Get();
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = SDPG_World;
			Mesh.bCanApplyViewModeOverrides = false;
			Collector.AddMesh(ViewIndex, Mesh);
		}
	}
}

void FCBNavGridSceneProxy::GatherData(ACBNavGrid const & NavGrid)
{
	FCBNavGridDebugSettings const & DebugSettings = NavGrid.GetDebugSettings();

	FColor const WalkableAreaColor = NavGrid.GetConfig().Color;
	float const CellSize = NavGrid.GetGridCellSize();
	FIntPoint const TileSize = NavGrid.GetTileSize();
	int32 const JunctionsNum = (TileSize.X + 1) * (TileSize.Y + 1);
	TArray<float> JunctionHeights;
	JunctionHeights.AddUninitialized(JunctionsNum);
	TArray<FDynamicMeshVertex> Vertices;

	DrawDistanceSq = FMath::Square(DebugSettings.DrawDistance);
	if (DebugSettings.bDrawFilledCells)
	{
		check(GEngine && GEngine->DebugMeshMaterial);
		MaterialRenderProxy = MakeUnique<FColoredMaterialRenderProxy>(GEngine->DebugMeshMaterial->GetRenderProxy(), WalkableAreaColor);
	}

	for (FIntPoint const & TileCoord : NavGrid.GetTileCoords())
	{
		FCBNavGridLayer const * const NavigationData = NavGrid.GetTileNavigationData(TileCoord).Get();
		if (!NavigationData)
		{
			continue;
		}

		FIntRect const TileRect = NavigationData->GetGridRect();

		auto GetJunctionIndex = [&TileRect, &TileSize](int32 const X, int32 const Y) -> int32
			{
				return (X - TileRect.Min.X) * (TileSize.Y + 1) + (Y - TileRect.Min.Y);
			};

		auto GetJunctionPoint = [CellSize, &JunctionHeights, &GetJunctionIndex](int32 const X, int32 const Y) -> FVector
			{
				return FVector{ X * CellSize, Y * CellSize, JunctionHeights[GetJunctionIndex(X, Y)] };
			};

		float MinTileHeight = TNumericLimits<float>::Max();
		float MaxTileHeight = TNumericLimits<float>::Lowest();

		// Calculates junction heights adding draw offset to them.
		for (int32 X = TileRect.Min.X; X <= TileRect.Max.X; ++X)
		{
			for (int32 Y = TileRect.Min.Y; Y <= TileRect.Max.Y; ++Y)
			{
				float FreeCellsHeightSum = 0.f;
				uint32 FreeCellsNum = 0;

				auto GetCellHeight = [&FreeCellsHeightSum, &FreeCellsNum, &NavGrid, NavigationData](int32 const X, int32 const Y)
					{
						if (NavigationData->IsInGrid(X, Y))
						{
							if (!NavigationData->IsCellOccupied(X, Y))
							{
								FreeCellsHeightSum += NavigationData->GetCellHeight(X, Y);
								++FreeCellsNum;
							}
						}
						else
						{
							FIntPoint const OtherTileCoord = NavGrid.GetTileCoord(FIntPoint{ X, Y });
							FCBNavGridLayer const * const OtherNavigationData = NavGrid.GetTileNavigationData(OtherTileCoord).Get();
							if (OtherNavigationData && !OtherNavigationData->IsCellOccupied(X, Y))
							{
								FreeCellsHeightSum += OtherNavigationData->GetCellHeight(X, Y);
								++FreeCellsNum;
							}
						}
					};

				GetCellHeight(X, Y);
				GetCellHeight(X, Y - 1);
				GetCellHeight(X - 1, Y);
				GetCellHeight(X - 1, Y - 1);

				float JunctionHeight = DebugSettings.DrawOffset;
				if (FreeCellsNum > 0)
				{
					JunctionHeight += FreeCellsHeightSum / FreeCellsNum;
					MinTileHeight = FMath::Min(MinTileHeight, JunctionHeight);
					MaxTileHeight = FMath::Max(MaxTileHeight, JunctionHeight);
				}
				JunctionHeights[GetJunctionIndex(X, Y)] = JunctionHeight;
			}
		}

		// No free cells in tile, nothing to render.
		if (MinTileHeight == TNumericLimits<float>::Max())
		{
			continue;
		}

		FVector3f const TileMin{ TileRect.Min.X * CellSize, TileRect.Min.Y * CellSize, MinTileHeight };
		FVector3f const TileMax{ TileRect.Max.X * CellSize, TileRect.Max.Y * CellSize, MaxTileHeight };
		FTileRenderData TileRenderData{ .BoundingBox{ TileMin, TileMax }, .FirstLineIndex = Lines.Num() };

		if (DebugSettings.bDrawCellEdges)
		{
			FColor const CellEdgeColor = WalkableAreaColor;
			float const CellEdgeThickness = 0.f;

			auto AddCellEdge = [this, CellEdgeColor, CellEdgeThickness, &GetJunctionPoint](int32 const StartX, int32 const StartY, int32 const EndX, int32 const EndY)
				{
					Lines.Emplace(GetJunctionPoint(StartX, StartY), GetJunctionPoint(EndX, EndY), CellEdgeColor, CellEdgeThickness);
				};

			FCBNavGridLayer const * const YNegativeNavigationData = NavGrid.GetTileNavigationData(TileCoord + FIntPoint{ 0, -1 }).Get();
			bool const bHasYPositiveNavigationData = NavGrid.IsValidTileCoord(TileCoord + FIntPoint{ 0, 1 });
			for (int32 X = TileRect.Min.X; X < TileRect.Max.X; ++X)
			{
				if (!NavigationData->IsCellOccupied(X, TileRect.Min.Y) || (YNegativeNavigationData && !YNegativeNavigationData->IsCellOccupied(X, TileRect.Min.Y - 1)))
				{
					AddCellEdge(X, TileRect.Min.Y, X + 1, TileRect.Min.Y);
				}

				for (int32 Y = TileRect.Min.Y + 1; Y < TileRect.Max.Y; ++Y)
				{
					if (!NavigationData->IsCellOccupied(X, Y) || !NavigationData->IsCellOccupied(X, Y - 1))
					{
						AddCellEdge(X, Y, X + 1, Y);
					}
				}

				if (!bHasYPositiveNavigationData && !NavigationData->IsCellOccupied(X, TileRect.Max.Y - 1))
				{
					AddCellEdge(X, TileRect.Max.Y, X + 1, TileRect.Max.Y);
				}
			}

			FCBNavGridLayer const * const XNegativeNavigationData = NavGrid.GetTileNavigationData(TileCoord + FIntPoint{ -1, 0 }).Get();
			bool const bHasXPositiveNavigationData = NavGrid.IsValidTileCoord(TileCoord + FIntPoint{ 1, 0 });
			for (int32 Y = TileRect.Min.Y; Y < TileRect.Max.Y; ++Y)
			{
				if (!NavigationData->IsCellOccupied(TileRect.Min.X, Y) || (XNegativeNavigationData && !XNegativeNavigationData->IsCellOccupied(TileRect.Min.X - 1, Y)))
				{
					AddCellEdge(TileRect.Min.X, Y, TileRect.Min.X, Y + 1);
				}

				for (int32 X = TileRect.Min.X + 1; X < TileRect.Max.X; ++X)
				{
					if (!NavigationData->IsCellOccupied(X, Y) || !NavigationData->IsCellOccupied(X - 1, Y))
					{
						AddCellEdge(X, Y, X, Y + 1);
					}
				}

				if (!bHasXPositiveNavigationData && !NavigationData->IsCellOccupied(TileRect.Max.X - 1, Y))
				{
					AddCellEdge(TileRect.Max.X, Y, TileRect.Max.X, Y + 1);
				}
			}
		}

		if (DebugSettings.bDrawFilledCells)
		{
			FMeshBatchElement & MeshBatchElement = TileRenderData.MeshBatchElement;
			MeshBatchElement.FirstIndex = IndexBuffer.Indices.Num();
			MeshBatchElement.MinVertexIndex = Vertices.Num();
			MeshBatchElement.IndexBuffer = &IndexBuffer;

			uint32 const InvalidJunctionVertexIndex = TNumericLimits<uint32>::Max();
			TArray<uint32> JunctionVertexIndices;
			JunctionVertexIndices.Init(InvalidJunctionVertexIndex, JunctionsNum);

			auto FindOrAddJunctionVertex = [&Vertices, CellSize, &JunctionHeights, &GetJunctionIndex, &JunctionVertexIndices](int32 const X, int32 const Y) -> uint32
				{
					uint32 const JunctionIndex = GetJunctionIndex(X, Y);
					uint32 & JunctionVertexIndex = JunctionVertexIndices[JunctionIndex];
					if (JunctionVertexIndex == InvalidJunctionVertexIndex)
					{
						JunctionVertexIndex = Vertices.Num();
						Vertices.Emplace(FVector3f{ X * CellSize, Y * CellSize, JunctionHeights[JunctionIndex] });
					}
					return JunctionVertexIndex;
				};

			for (int32 X = TileRect.Min.X; X < TileRect.Max.X; ++X)
			{
				for (int32 Y = TileRect.Min.Y; Y < TileRect.Max.Y; ++Y)
				{
					if (!NavigationData->IsCellOccupied(X, Y))
					{
						uint32 const Vertex00 = FindOrAddJunctionVertex(X, Y);
						uint32 const Vertex01 = FindOrAddJunctionVertex(X, Y + 1);
						uint32 const Vertex10 = FindOrAddJunctionVertex(X + 1, Y);
						uint32 const Vertex11 = FindOrAddJunctionVertex(X + 1, Y + 1);
						IndexBuffer.Indices.Append({
							Vertex00, Vertex01, Vertex11,
							Vertex00, Vertex11, Vertex10
							});
					}
				}
			}

			MeshBatchElement.NumPrimitives = (IndexBuffer.Indices.Num() - MeshBatchElement.FirstIndex) / 3;
			MeshBatchElement.MaxVertexIndex = Vertices.Num() - 1;
		}

		if (DebugSettings.bDrawNavGridEdges)
		{
			FColor const NavGridEdgeColor = DarkenColor(WalkableAreaColor);
			float const NavGridEdgeThickness = 3.f;

			auto AddNavGridEdge = [this, NavGridEdgeColor, NavGridEdgeThickness, &GetJunctionPoint](int32 const StartX, int32 const StartY, int32 const EndX, int32 const EndY)
				{
					Lines.Emplace(GetJunctionPoint(StartX, StartY), GetJunctionPoint(EndX, EndY), NavGridEdgeColor, NavGridEdgeThickness);
				};

			FCBNavGridLayer const * const YNegativeNavigationData = NavGrid.GetTileNavigationData(TileCoord + FIntPoint{ 0, -1 }).Get();
			bool const bHasYPositiveNavigationData = NavGrid.IsValidTileCoord(TileCoord + FIntPoint{ 0, 1 });
			for (int32 X = TileRect.Min.X; X < TileRect.Max.X; ++X)
			{
				if (NavigationData->IsCellOccupied(X, TileRect.Min.Y) != (!YNegativeNavigationData || YNegativeNavigationData->IsCellOccupied(X, TileRect.Min.Y - 1)))
				{
					AddNavGridEdge(X, TileRect.Min.Y, X + 1, TileRect.Min.Y);
				}

				for (int32 Y = TileRect.Min.Y + 1; Y < TileRect.Max.Y; ++Y)
				{
					if (NavigationData->IsCellOccupied(X, Y) != NavigationData->IsCellOccupied(X, Y - 1))
					{
						AddNavGridEdge(X, Y, X + 1, Y);
					}
				}

				if (!bHasYPositiveNavigationData && !NavigationData->IsCellOccupied(X, TileRect.Max.Y - 1))
				{
					AddNavGridEdge(X, TileRect.Max.Y, X + 1, TileRect.Max.Y);
				}
			}

			FCBNavGridLayer const * const XNegativeNavigationData = NavGrid.GetTileNavigationData(TileCoord + FIntPoint{ -1, 0 }).Get();
			bool const bHasXPositiveNavigationData = NavGrid.IsValidTileCoord(TileCoord + FIntPoint{ 1, 0 });
			for (int32 Y = TileRect.Min.Y; Y < TileRect.Max.Y; ++Y)
			{
				if (NavigationData->IsCellOccupied(TileRect.Min.X, Y) != (!XNegativeNavigationData || XNegativeNavigationData->IsCellOccupied(TileRect.Min.X - 1, Y)))
				{
					AddNavGridEdge(TileRect.Min.X, Y, TileRect.Min.X, Y + 1);
				}

				for (int32 X = TileRect.Min.X + 1; X < TileRect.Max.X; ++X)
				{
					if (NavigationData->IsCellOccupied(X, Y) != NavigationData->IsCellOccupied(X - 1, Y))
					{
						AddNavGridEdge(X, Y, X, Y + 1);
					}
				}

				if (!bHasXPositiveNavigationData && !NavigationData->IsCellOccupied(TileRect.Max.X - 1, Y))
				{
					AddNavGridEdge(TileRect.Max.X, Y, TileRect.Max.X, Y + 1);
				}
			}
		}

		if (DebugSettings.bDrawTileEdges)
		{
			FColor const TileEdgeColor = FColorList::White;
			float const TileEdgeThickness = 1.5f;

			double const TileHeight = (MinTileHeight + MaxTileHeight) / 2.;

			auto GetTileEdgePoint = [CellSize, TileHeight](int32 const X, int32 const Y) -> FVector
				{
					return FVector{ X * CellSize, Y * CellSize, TileHeight };
				};

			auto AddTileEdge = [this, TileEdgeColor, TileEdgeThickness, &GetTileEdgePoint](int32 const StartX, int32 const StartY, int32 const EndX, int32 const EndY)
				{
					Lines.Emplace(GetTileEdgePoint(StartX, StartY), GetTileEdgePoint(EndX, EndY), TileEdgeColor, TileEdgeThickness);
				};

			AddTileEdge(TileRect.Min.X, TileRect.Min.Y, TileRect.Max.X, TileRect.Min.Y);
			AddTileEdge(TileRect.Min.X, TileRect.Min.Y, TileRect.Min.X, TileRect.Max.Y);
			AddTileEdge(TileRect.Max.X, TileRect.Max.Y, TileRect.Max.X, TileRect.Min.Y);
			AddTileEdge(TileRect.Max.X, TileRect.Max.Y, TileRect.Min.X, TileRect.Max.Y);
		}

		TileRenderData.LinesNum = Lines.Num() - TileRenderData.FirstLineIndex;
		Tiles.Add(MoveTemp(TileRenderData));
	}

	if (!Vertices.IsEmpty())
	{
		VertexBuffers.InitFromDynamicVertex(&VertexFactory, Vertices);
	}
	if (!IndexBuffer.Indices.IsEmpty())
	{
		BeginInitResource(&IndexBuffer);
	}
}
