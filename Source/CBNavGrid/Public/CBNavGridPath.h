#pragma once

#include "CoreMinimal.h"
#include "NavigationPath.h"

struct CBNAVGRID_API FCBNavGridPath : public FNavigationPath
{
private:
	using Super = FNavigationPath;

public:
	FCBNavGridPath();

	FORCEINLINE void SetWantsStringPulling(bool const bNewWantsStringPulling);
	FORCEINLINE bool WantsStringPulling() const;
	FORCEINLINE FIntRect const & GetGridBoundingBox() const;
	FORCEINLINE FIntRect & GetGridBoundingBox();
	void ApplyFlags(int32 const NavDataFlags);

	virtual bool ContainsCustomLink(FNavLinkId const UniqueLinkId) const override;
	virtual bool ContainsAnyCustomLink() const override;
	virtual FVector::FReal GetCostFromIndex(int32 const PathPointIndex) const override;

	static FNavPathType const Type;

protected:
	/** Used for active path invalidation on grid rebuilds. */
	FIntRect GridBoundingBox;

	/** If set to true path instance will contain a string pulled version. Defaults to true. */
	uint8 bWantsStringPulling : 1;
};

void FCBNavGridPath::SetWantsStringPulling(bool const bNewWantsStringPulling)
{
	bWantsStringPulling = bNewWantsStringPulling;
}

bool FCBNavGridPath::WantsStringPulling() const
{
	return bWantsStringPulling;
}

FIntRect const & FCBNavGridPath::GetGridBoundingBox() const
{
	return GridBoundingBox;
}

FIntRect & FCBNavGridPath::GetGridBoundingBox()
{
	return GridBoundingBox;
}
