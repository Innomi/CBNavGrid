#pragma once

#include "CoreMinimal.h"
#include "DebugRenderSceneProxy.h"
#include "DynamicMeshBuilder.h"
#include "MeshBatch.h"
#include "PrimitiveSceneProxy.h"

class ACBNavGrid;
class FColoredMaterialRenderProxy;
class UCBNavGridRenderingComponent;

class CBNAVGRID_API FCBNavGridSceneProxy : public FPrimitiveSceneProxy
{
private:
	using Super = FPrimitiveSceneProxy;

public:
	FCBNavGridSceneProxy(UCBNavGridRenderingComponent const * const Component);
	virtual ~FCBNavGridSceneProxy() override;

	virtual SIZE_T GetTypeHash() const override;
	virtual void GetDynamicMeshElements(TArray<FSceneView const *> const & Views, FSceneViewFamily const & ViewFamily, uint32 const VisibilityMap, FMeshElementCollector & Collector) const override;
	virtual uint32 GetMemoryFootprint() const override;
	SIZE_T GetAllocatedSize() const;

protected:
	virtual FPrimitiveViewRelevance GetViewRelevance(FSceneView const * const View) const override;
	void GetDynamicMeshElementsForView(FSceneView const * const View, int32 const ViewIndex, FMeshElementCollector & Collector) const;
	void GatherData(ACBNavGrid const & NavGrid);

	struct FTileRenderData
	{
		FMeshBatchElement MeshBatchElement;
		FBox BoundingBox;
		int32 FirstLineIndex;
		int32 LinesNum;
	};

	TArray<FTileRenderData> Tiles;
	TArray<FDebugRenderSceneProxy::FDebugLine> Lines;
	FLocalVertexFactory VertexFactory;
	FStaticMeshVertexBuffers VertexBuffers;
	FDynamicMeshIndexBuffer32 IndexBuffer;
	TUniquePtr<FColoredMaterialRenderProxy> MaterialRenderProxy;
	double DrawDistanceSq;
};
