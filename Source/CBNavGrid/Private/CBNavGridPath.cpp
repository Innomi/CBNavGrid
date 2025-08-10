#include "CBNavGridPath.h"
#include "CBNavGrid.h"

FNavPathType const FCBNavGridPath::Type;

FCBNavGridPath::FCBNavGridPath()
	: GridBoundingBox()
	, bWantsStringPulling(true)
{
	PathType = FCBNavGridPath::Type;
}

void FCBNavGridPath::ApplyFlags(int32 const NavDataFlags)
{
	bWantsStringPulling = !(NavDataFlags & static_cast<int32>(ECBNavGridPathFlags::SkipStringPulling));
}

bool FCBNavGridPath::ContainsCustomLink(FNavLinkId const UniqueLinkId) const
{
	return false;
}

bool FCBNavGridPath::ContainsAnyCustomLink() const
{
	return false;
}

FVector::FReal FCBNavGridPath::GetCostFromIndex(int32 const PathPointIndex) const
{
	return GetLengthFromPosition(PathPoints[PathPointIndex].Location, PathPointIndex + 1);
}
