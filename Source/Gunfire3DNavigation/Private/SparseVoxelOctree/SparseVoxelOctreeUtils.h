// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "SparseVoxelOctreeNode.h"
#include "libmorton/morton.h"

class FSvoTile;

struct FSvoUtils
{
	static FIntVector VoxelGridExtents;

	#define MORTON_X_MASK 0x09249249	// CoordToMorton(FIntVector(1023,0,0)) (basically, -1)
	#define MORTON_Y_MASK 0x12492492
	#define MORTON_Z_MASK 0x24924924

	static bool IsValidMortonCoord(const FIntVector& Coord)
	{
		// NOTE: LibMorton does *not* handle negative values and we only allow a maximum
		// size that will fit into our node links.
		return (Coord.X >= SVO_MIN_NODECOORD && Coord.X <= SVO_MAX_NODECOORD &&
				Coord.Y >= SVO_MIN_NODECOORD && Coord.Y <= SVO_MAX_NODECOORD &&
				Coord.Z >= SVO_MIN_NODECOORD && Coord.Z <= SVO_MAX_NODECOORD);
	}

	static bool IsValidMortonCode(const uint32& Code)
	{
		// NOTE: The maximum Morton code size is governed by our node links.
		return (Code < SVO_MAX_NODES);
	}

	static uint32 CoordToMorton(const FIntVector& Coord)
	{
		ensure(IsValidMortonCoord(Coord));

		return libmorton::morton3D_32_encode(Coord.X, Coord.Y, Coord.Z);
	}

	static FIntVector MortonToCoord(uint32 MortonCode)
	{
		ensure(IsValidMortonCode(MortonCode));

		uint_fast16_t X, Y, Z;
		libmorton::morton3D_32_decode(MortonCode, X, Y, Z);
		return FIntVector(X, Y, Z);
	}

	// Gets the neighbor for a Morton code. This is significantly faster than converting
	// back to a coordinate, offsetting, and converting back.
	// This has error checking so if the neighbor would wrap around it returns the same
	// code that was passed in. Note that it can only tell if it goes negative or past the
	// maximum coord (1023). We typically cap our maximum coord below the theoretical one,
	// so you still need to check for that case.
	static uint32 MortonNeighbor(uint32 Code, ESvoNeighbor Neighbor)
	{
		// The mask for the axis this neighbor is in
		const uint32 AxisMask = MortonNeighborLUT[(uint8)Neighbor][0];
		// The Morton code for the offset in this axis (+1 or -1)
		const uint32 Offset = MortonNeighborLUT[(uint8)Neighbor][1];
		// The Morton code where there is no neighbor in this direction
		const uint32 AxisEdge = MortonNeighborLUT[(uint8)Neighbor][2];

		const uint32 AxisValue = (Code & AxisMask);

		return (AxisValue == AxisEdge) ? Code : (Code & ~AxisMask) | (((Code & AxisMask) - Offset) & AxisMask);
	}

	// Converts a positive or negative offset into a special code that can be used with OffsetMorton.
	static uint32 CalculateMortonOffset(const FIntVector& Offset)
	{
		// Convert the offset into an absolute value, and depend on the fact that the
		// Morton code will wrap around if we subtract a larger number from a smaller one.
		// That way we don't have to worry about whether we should use the addition or
		// subtraction algorithm.
		uint_fast16_t X = Offset.X <= 0 ? -Offset.X : 1024 - Offset.X;
		uint_fast16_t Y = Offset.Y <= 0 ? -Offset.Y : 1024 - Offset.Y;
		uint_fast16_t Z = Offset.Z <= 0 ? -Offset.Z : 1024 - Offset.Z;

		return libmorton::morton3D_32_encode(X, Y, Z);
	}

	// Offsets a Morton code by the given offset, calculated with CalculateMortonOffset.
	// Note that this has no safety checking like MortonNeighbor, if you pass in an offset
	// that would wrap around it will.
	static uint32 OffsetMorton(uint32 Code, uint32 OffsetCode)
	{
		const uint32 XOut = ((Code & MORTON_X_MASK) - (OffsetCode & MORTON_X_MASK)) & MORTON_X_MASK;
		const uint32 YOut = ((Code & MORTON_Y_MASK) - (OffsetCode & MORTON_Y_MASK)) & MORTON_Y_MASK;
		const uint32 ZOut = ((Code & MORTON_Z_MASK) - (OffsetCode & MORTON_Z_MASK)) & MORTON_Z_MASK;
		return XOut | YOut | ZOut;
	}

	// Computes the next Morton code greater than Code in the range MinCode-MaxCode, where
	// Code is a value in that range.
	static uint32 NextMorton(uint32 Code, uint32 MinCode, uint32 MaxCode);

	// Converts a real voxel coord to a fixed one. This isn't anything particularly fancy,
	// it's just her to clean up some boilerplate and formalize that we always floor our
	// values.
	static FIntVector CoordToFixed(const FVector& Coord)
	{
		return FIntVector(
			FMath::FloorToInt(Coord.X),
			FMath::FloorToInt(Coord.Y),
			FMath::FloorToInt(Coord.Z));
	}

	static FIntVector LocationToCoord(const FVector& SeedLocation, const FVector& Location, float Resolution)
	{
		const FVector SeedRelativeLocation = (Location - SeedLocation);

		// IMPORTANT: First floor the location prior to calculating the coordinate.  We do
		//			  this so very small numbers aren't divided by very large numbers which
		//			  can generate floating point errors.
		const FVector SeedRelativeLocation_Floored(
			FMath::FloorToFloat(SeedRelativeLocation.X),
			FMath::FloorToFloat(SeedRelativeLocation.Y),
			FMath::FloorToFloat(SeedRelativeLocation.Z));

		// Calculate the coordinate for the location at the specified resolution
		const FVector Coord_Float = (SeedRelativeLocation_Floored / Resolution);

		// Now safely floor all values to integers
		return CoordToFixed(Coord_Float);
	}

	static FVector CoordToLocation(const FVector& SeedLocation, const FIntVector& Coord, float Resolution, ECellOffset Offset = ECellOffset::Center)
	{
		FVector Location = SeedLocation + (FVector(Coord) * Resolution);

		if (Offset == ECellOffset::Center)
		{
			Location += FVector(Resolution * 0.5f);
		}
		else if (Offset == ECellOffset::Max)
		{
			Location += FVector(Resolution);
		}

		return Location;
	}

	static FIntVector ChangeCoordSpace(const FVector& SeedLocation, const FIntVector& Coord, float CurrentResolution, float DesiredResolution)
	{
		FVector Location = CoordToLocation(SeedLocation, Coord, CurrentResolution);
		check(LocationToCoord(SeedLocation, Location, CurrentResolution) == Coord);
		return LocationToCoord(SeedLocation, Location, DesiredResolution);
	}

	static uint32 GetIndexForCoord(const FIntVector& Coord, const FIntVector& Extents)
	{
		return Coord.X + (Coord.Y * Extents.X) + (Coord.Z * Extents.X * Extents.Y);
	}

	static uint32 GetIndexForCoord2D(const FIntPoint& Coord, const FIntPoint& Extents)
	{
		return Coord.X + (Coord.Y * Extents.X);
	}

	static void GetCoordFromIndex(uint32 Index, FIntVector& OutCoord, const FIntVector& Extents)
	{
		OutCoord.X = (Index % Extents.X);
		Index /= Extents.X;
		OutCoord.Y = (Index % Extents.Y);
		Index /= Extents.Y;
		OutCoord.Z = Index;
	}

	static bool IsCoordValid(const FIntVector& VoxelCoord, const FIntVector& Extents)
	{
		return
			VoxelCoord.X >= 0 && VoxelCoord.X < Extents.X &&
			VoxelCoord.Y >= 0 && VoxelCoord.Y < Extents.Y &&
			VoxelCoord.Z >= 0 && VoxelCoord.Z < Extents.Z;
	}

	static uint8 GetVoxelIndexForCoord(const FIntVector& VoxelCoord)
	{
		return (uint8)GetIndexForCoord(VoxelCoord, VoxelGridExtents);
	}

	static void GetVoxelCoordFromIndex(uint8 Index, FIntVector& OutVoxelCoord)
	{
		return GetCoordFromIndex((uint32)Index, OutVoxelCoord, VoxelGridExtents);
	}

	static bool IsVoxelCoordValid(const FIntVector& VoxelCoord)
	{
		return IsCoordValid(VoxelCoord, VoxelGridExtents);
	}

	static void GetCoordsForBounds(const FVector& SeedLocation, const FBox& Bounds, float Resolution, FIntVector& MinCoordOut, FIntVector& MaxCoordOut)
	{
		MinCoordOut = LocationToCoord(SeedLocation, Bounds.Min, Resolution);
		MaxCoordOut = LocationToCoord(SeedLocation, Bounds.Max, Resolution);

		// If the max coord is greater than the min, we need to be sure the correct max
		// coord was found.  The reason we do this is because the max coord can be along
		// the exact edge of a cell and can return that cell (as a product of the min)
		// instead of the cell outside of it (as a product of the max).  We do this by
		// checking the original max location against the cell min location.  For every
		// axis that is equal or less, we assume it was meant to maximum value, not a
		// minimum and subtract one.  This will result in the desired cell as if the max
		// bounds location were at the max end of cell and not the min.
		if (MinCoordOut != MaxCoordOut)
		{
			const FVector MaxLocation = CoordToLocation(SeedLocation, MaxCoordOut, Resolution, ECellOffset::Min);

			for (uint8 AxisIdx = 0; AxisIdx < 3; ++AxisIdx)
			{
				if (Bounds.Max[AxisIdx] <= MaxLocation[AxisIdx] && MaxCoordOut[AxisIdx] > MinCoordOut[AxisIdx])
				{
					MaxCoordOut[AxisIdx] -= 1;
				}
			}
		}

		ensure(MaxCoordOut.X >= MinCoordOut.X && MaxCoordOut.Y >= MinCoordOut.Y && MaxCoordOut.Z >= MinCoordOut.Z);
	}

	static uint32 GetNumCoordsForBounds(const FVector& SeedLocation, const FBox& Bounds, float Resolution)
	{
		FIntVector CoordMin, MaxTileCoord;
		GetCoordsForBounds(SeedLocation, Bounds, Resolution, CoordMin, MaxTileCoord);
		FIntVector CoordExtents = (MaxTileCoord - CoordMin + FIntVector(1, 1, 1));
		return (CoordExtents.X * CoordExtents.Y * CoordExtents.Z);
	}

	// Returns true if the coord is in the given bounds
	static bool IsCoordInBounds(const FIntVector& Coord, const FIntVector& BoundsMin, const FIntVector& BoundsMax)
	{
		return
			(Coord.X >= BoundsMin.X) &&
			(Coord.X <= BoundsMax.X) &&
			(Coord.Y >= BoundsMin.Y) &&
			(Coord.Y <= BoundsMax.Y) &&
			(Coord.Z >= BoundsMin.Z) &&
			(Coord.Z <= BoundsMax.Z);
	}

	// Calculates the resolution for a specified layer
	static float CalcResolutionForLayer(uint8 LayerIdx, float VoxelSize)
	{
		// A leaf contains a 4x4x4 cube of voxels, our finest resolution, which is twice
		// the density of octree nodes. By multiplying by 4 we get the size of the leaf
		// nodes (layer 0). From there, multiplying by a power of 2 puts us at the desired
		// level in the octree.
		float Layer0Resolution = (VoxelSize * SVO_VOXEL_GRID_EXTENT);
		return (LayerIdx == SVO_LEAF_LAYER) ? Layer0Resolution : Layer0Resolution * FMath::Pow(2.0f, float(LayerIdx));
	}

	static TArrayView<const ESvoNeighbor> GetAllNeighbors()
	{
		return AllNeighbors;
	}

	static uint8 GetChildIndex(TMortonCode NodeMortonCode)
	{
		static const uint32 SiblingMask = 0x7;
		return (NodeMortonCode & SiblingMask);
	}

	// Returns true if both Morton codes are siblings, ie, have the same direct parent.
	static bool AreSiblings(TMortonCode NodeMortonCodeA, TMortonCode NodeMortonCodeB)
	{
		static const uint32 ParentMask = 0xFFFFFFF8;
		return (NodeMortonCodeA & ParentMask) == (NodeMortonCodeB & ParentMask);
	}

	static ESvoNeighbor GetOppositeNeighbor(ESvoNeighbor Neighbor)
	{
		return (ESvoNeighbor)(((uint8)Neighbor + 3) % 6);
	}

	static const FIntVector& GetNeighborDirection(ESvoNeighbor Neighbor)
	{
		return DirectionLUT[(uint8)Neighbor];
	}

	// Returns the indices (0-7) for the 4 child nodes that touch the given neighbor
	// (also, this is the worst name ever)
	static TArrayView<const uint8> GetChildrenTouchingNeighbor(ESvoNeighbor Neighbor)
	{
		return ChildTouchingNeighborLUT[(uint8)Neighbor];
	}

	// Given the index for a node and one of its siblings, returns the neighbor
	// relationship between them
	static ESvoNeighbor GetNeighborType(uint8 NodeIndex, uint8 SiblingIndex)
	{
		return NodeNeighborLUT[SiblingIndex][NodeIndex];
	}

	// Look-up table for finding the compliment voxel index.
	// e.g. Left face voxel touching right face voxel.
	// (See FNavSvoQuery::OpenNeighborLeafFace::LeafFaceVoxelsLUT)
	// (See ESvoNeighbor enum for index order)
	static uint8 GetNeighborVoxel(uint8 VoxelIndex, ESvoNeighbor Neighbor)
	{
		return VoxelIndex + OppositeLeafFaceVoxelOffsetLUT[(uint8)Neighbor];
	}

	static TArrayView<const uint8> GetTouchingNeighborVoxels(ESvoNeighbor Neighbor)
	{
		return LeafFaceVoxelsLUT[(uint8)GetOppositeNeighbor(Neighbor)];
	}

private:
	static const ESvoNeighbor AllNeighbors[6];
	static const FIntVector DirectionLUT[6];
	static const uint32 MortonNeighborLUT[6][3];
	static const uint8 ChildTouchingNeighborLUT[6][4];
	static const ESvoNeighbor NodeNeighborLUT[8][8];
	static const int8 OppositeLeafFaceVoxelOffsetLUT[6];
	static const uint8 LeafFaceVoxelsLUT[6][16];
};

// Iterator for moving an extent of coords
class FCoordIterator
{
public:
	FCoordIterator(const FIntVector& InCoordMin, const FIntVector& InCoordMax)
		: CoordMin(InCoordMin)
		, CoordMax(InCoordMax)
		, Coord(CoordMin)
		, CoordIndex(0)
	{
		CoordExtents = (CoordMax - CoordMin + FIntVector(1, 1, 1));
		NumCoords = (CoordExtents.X * CoordExtents.Y * CoordExtents.Z);
	}

	const FIntVector& GetCoord() const { return Coord; }
	const uint32 GetNumCoords() const { return NumCoords; }
	uint8 GetIndex() const { return CoordIndex; }

	explicit operator bool() const { return !IsComplete(); }

	FCoordIterator& operator++()
	{
		if (!IsComplete())
		{
			if (++Coord.X > CoordMax.X)
			{
				Coord.X = CoordMin.X;
				if (++Coord.Y > CoordMax.Y)
				{
					Coord.Y = CoordMin.Y;
					++Coord.Z;
				}
			}

			++CoordIndex;
			check(CoordIndex == FSvoUtils::GetIndexForCoord(Coord - CoordMin, CoordExtents));
		}

		return *this;
	}

private:
	bool IsComplete() const { return (NumCoords == 0 || Coord.Z > CoordMax.Z); }

private:
	const FIntVector CoordMin;
	const FIntVector CoordMax;
	FIntVector CoordExtents;
	uint32 NumCoords;

	FIntVector Coord;
	int32 CoordIndex;
};

// Iterator for moving an extent of coords
class FCoordIterator2D
{
public:
	FCoordIterator2D(const FIntPoint& InCoordMin, const FIntPoint& InCoordMax)
		: CoordMin(InCoordMin)
		, CoordMax(InCoordMax)
		, Coord(CoordMin)
		, CoordIndex(0)
	{
		CoordExtents = (CoordMax - CoordMin + FIntPoint(1, 1));
		NumCoords = (CoordExtents.X * CoordExtents.Y);
	}

	FCoordIterator2D(const FIntVector& InCoordMin, const FIntVector& InCoordMax)
		: CoordMin(InCoordMin.X, InCoordMin.Y)
		, CoordMax(InCoordMax.X, InCoordMax.Y)
		, Coord(CoordMin)
		, CoordIndex(0)
	{
		CoordExtents = (CoordMax - CoordMin + FIntPoint(1, 1));
		NumCoords = (CoordExtents.X * CoordExtents.Y);
	}

	const FIntPoint& GetCoord() const { return Coord; }
	const uint32 GetNumCoords() const { return NumCoords; }
	uint8 GetIndex() const { return CoordIndex; }

	explicit operator bool() const { return !IsComplete(); }

	FCoordIterator2D& operator++()
	{
		if (!IsComplete())
		{
			if (++Coord.X > CoordMax.X)
			{
				Coord.X = CoordMin.X;
				++Coord.Y;
			}

			++CoordIndex;
			check(CoordIndex == FSvoUtils::GetIndexForCoord2D(Coord - CoordMin, CoordExtents));
		}

		return *this;
	}

private:
	bool IsComplete() const { return (NumCoords == 0 || Coord.Y > CoordMax.Y); }

private:
	const FIntPoint CoordMin;
	const FIntPoint CoordMax;
	FIntPoint CoordExtents;
	uint32 NumCoords;

	FIntPoint Coord;
	int32 CoordIndex;
};

// Iterator for moving through the 64 voxels of a leaf
class FSvoVoxelIterator : public FCoordIterator
{
public:
	FSvoVoxelIterator()
		: FCoordIterator(FIntVector::ZeroValue, FIntVector(SVO_VOXEL_GRID_EXTENT - 1))
	{}
};

// Iterator for evaluating each neighbor of a specific node
template<typename TOctree, typename TTile, typename TNode>
class FSvoNeighborIteratorBase
{
public:
	explicit FSvoNeighborIteratorBase(TOctree& Octree, FSvoNodeLink InNodeLink, bool bInSkipInvalid = true);
	explicit FSvoNeighborIteratorBase(TOctree& Octree, TNode& InNode, bool bInSkipInvalid = true);
	explicit FSvoNeighborIteratorBase(TOctree& Octree, TTile& Tile, bool bInSkipInvalid = true);

	// Returns the current neighbor
	ESvoNeighbor GetNeighbor() const { return Neighbor; }

	// Returns a link to the current neighbor. Note that this may be an invalid link if
	// the voxel we're iterating the neighbors of is at the edge of a tile and there is no
	// neighboring tile on that side.
	FSvoNodeLink GetNeighborLink() const { return NeighborLink; }

	// Returns the node for the current neighbor. If the neighbor is a voxel this will
	// return the leaf node and you can use GetNeighborLink to find the correct voxel to
	// reference. It can also return a lower resolution node if there are no blocked
	// voxels in this area.
	TNode* GetNeighborNode() { return NeighborNode; }
	TNode& GetNeighborNodeChecked() { checkSlow(NeighborNode != nullptr); return *NeighborNode; }

	explicit operator bool() const { return !IsComplete(); }

	FSvoNeighborIteratorBase& operator++()
	{
		// If we're not at the final entry in the ESvoNeighbor enum (e.g. 'Self'), move to
		// the next neighbor.
		if (!IsComplete())
		{
			Neighbor = (ESvoNeighbor)((uint8)Neighbor + 1);
			UpdateNeighbor();
		}

		return *this;
	}

private:
	FIntVector GetNeighborVoxelCoord() const
	{
		FIntVector VoxelCoord;
		FSvoUtils::GetVoxelCoordFromIndex(NodeLink.VoxelIdx, VoxelCoord);
		return VoxelCoord + FSvoUtils::GetNeighborDirection(Neighbor);
	}

	uint8 GetNeighborVoxelIndex() const
	{
		FIntVector NeighborVoxelCoord = GetNeighborVoxelCoord();
		return FSvoUtils::GetVoxelIndexForCoord(NeighborVoxelCoord);
	}

	bool IsComplete() const
	{
		// 'Self' is the final entry in the ESvoNeighbor enum so we can use it as an
		// indicator that we've iterated over all neighbors.
		return (Neighbor == ESvoNeighbor::Self);
	}

	void ForceComplete()
	{
		// Force the neighbor to the final entry in the ESvoNeighbor enum which indicates
		// we have iterated over all neighbors.
		Neighbor = ESvoNeighbor::Self;
	}

	// This performs the actual iteration amongst the neighbors
	void UpdateNeighbor();

private:
	TOctree& Octree;

	TNode* Node;
	FSvoNodeLink NodeLink;

	ESvoNeighbor Neighbor;
	FSvoNodeLink NeighborLink;
	TNode* NeighborNode;

	bool bSkipInvalid;
};

typedef FSvoNeighborIteratorBase<class FSparseVoxelOctree, class FSvoTile, FSvoNode> FSvoNeighborIterator;
typedef FSvoNeighborIteratorBase<const class FSparseVoxelOctree, const class FSvoTile, const FSvoNode> FSvoNeighborConstIterator;

//
// Iterates through a box defined by a min and max Morton code, returning every Morton
// code. This is inclusive, so both min and max will be returned.
//
// This is faster than just iterating through coordinates in x/y/z and computing Morton
// codes for them, and it has the advantage of always moving forward in memory so it's
// better for cache coherency.
//
// Designed to be used in a ranged for loop:
//
//   for (uint32 CurCode : FMortonIterator(MinCode, MaxCode))
//     ...
//
class FMortonIterator
{
public:
	FMortonIterator(uint32 MinCodeIn, uint32 MaxCodeIn) : MinCode(MinCodeIn), MaxCode(MaxCodeIn) {}

	struct FRangedForIterator
	{
		explicit FRangedForIterator(uint32 MinCodeIn, uint32 MaxCodeIn)
			: MinCode(MinCodeIn), MaxCode(MaxCodeIn), CurCode(MinCodeIn)
		{
		}

		uint32 operator*() const
		{
			return CurCode;
		}

		FORCEINLINE FRangedForIterator& operator++()
		{
			if (CurCode == MaxCode)
			{
				CurCode = MaxCode + 1;
				return *this;
			}

			// First, attempt to just do a naive increment of the Morton code. This can
			// walk outside of the bounds if we're at a discontinuity so we have to check
			// if it's still valid.
			for (uint32 i = 1; i < 4; ++i)
			{
				if (IsInRange(CurCode + i))
				{
					CurCode += i;
					return *this;
				}
			}

			// If we still haven't found a valid code for this range we're in a big enough
			// discontinuity that it will be worth the cost to calculate the next code.
			CurCode = FSvoUtils::NextMorton(CurCode, MinCode, MaxCode);
			return *this;
		}

		bool Valid() const
		{
			return CurCode <= MaxCode;
		}

		friend bool operator!=(const FRangedForIterator& A, const FRangedForIterator& B)
		{
			return A.Valid();
		}

	private:
		bool IsInRange(uint32 Code)
		{
			uint32 CoordX = Code & MORTON_X_MASK;
			uint32 CoordY = Code & MORTON_Y_MASK;
			uint32 CoordZ = Code & MORTON_Z_MASK;

			return
				CoordX >= (MinCode & MORTON_X_MASK) && CoordX <= (MaxCode & MORTON_X_MASK) &&
				CoordY >= (MinCode & MORTON_Y_MASK) && CoordY <= (MaxCode & MORTON_Y_MASK) &&
				CoordZ >= (MinCode & MORTON_Z_MASK) && CoordZ <= (MaxCode & MORTON_Z_MASK);
		}

	private:
		uint32 MinCode;
		uint32 MaxCode;
		uint32 CurCode;
	};

	FORCEINLINE FRangedForIterator begin() { return FRangedForIterator(MinCode, MaxCode); }
	FORCEINLINE FRangedForIterator end() { return FRangedForIterator(0, 0); }

private:
	uint32 MinCode;
	uint32 MaxCode;
};

#include "SparseVoxelOctreeUtils.inl"