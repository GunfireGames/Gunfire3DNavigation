// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

namespace Gunfire3DNavigation
{
	// Parent struct for various coord based structs
	struct FNodeCoord
	{
		FIntVector Coord;

		FNodeCoord() { /* For Serialization */ }
		FNodeCoord(const FIntVector& InCoord) : Coord(InCoord) {}
		virtual ~FNodeCoord() {}

		bool operator==(const FNodeCoord& Other) const { return (Coord == Other.Coord); }
		friend uint32 GetTypeHash(const FNodeCoord& Tile) { return GetTypeHash(Tile.Coord); }

		virtual void Serialize(FArchive& Ar)
		{
			Ar << Coord;
		}

		friend FArchive& operator<<(FArchive& Ar, FNodeCoord& NodeCoord)
		{
			NodeCoord.Serialize(Ar);
			return Ar;
		}
	};

	struct FRaycastResult
	{
		float HitTime = MAX_flt;
		FNavLocation HitLocation;

		bool HasHit() const { return HitTime != MAX_flt; }
	};
}