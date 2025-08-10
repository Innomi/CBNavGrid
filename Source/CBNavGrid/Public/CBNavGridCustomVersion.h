#pragma once

#include "Misc/Guid.h"

struct CBNAVGRID_API FCBNavGridCustomVersion
{
	FCBNavGridCustomVersion() = delete;

	enum Type : int32
	{
		InitialVersion,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	static FGuid const GUID;
};
