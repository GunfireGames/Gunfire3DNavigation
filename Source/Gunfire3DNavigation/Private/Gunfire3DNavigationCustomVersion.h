// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

// Custom serialization version for all plugin relevant classes/structs
struct FGunfire3DNavigationCustomVersion
{
	enum Type
	{
		// Rebooted the file format
		InitialVersion = 9,

		// Changed how the non-leaf node properties are stored
		NodePropsChanged,

		// Added 32-bit neighbor node links to free up some memory for other data
		NodeLinkBaseAdded,

		// -----<new versions can be added above this line>-----
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

	// Config of the current octree being loaded
	static struct FSvoConfig* SvoConfig;
};