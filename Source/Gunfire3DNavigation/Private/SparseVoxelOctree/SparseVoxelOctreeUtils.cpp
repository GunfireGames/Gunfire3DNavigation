// Copyright Gunfire Games, LLC. All Rights Reserved.

#include "SparseVoxelOctreeUtils.h"

const ESvoNeighbor FSvoUtils::AllNeighbors[] =
{
	ESvoNeighbor::Front, ESvoNeighbor::Right, ESvoNeighbor::Top, ESvoNeighbor::Back, ESvoNeighbor::Left, ESvoNeighbor::Bottom
};

// Look-up table neighbor directional vectors
// 6 Neighbors
const FIntVector FSvoUtils::DirectionLUT[] =
{
	FIntVector(1,  0,  0),	// Front
	FIntVector(0,  1,  0),	// Right
	FIntVector(0,  0,  1),	// Top
	FIntVector(-1,  0,  0),	// Back
	FIntVector(0, -1,  0),	// Left
	FIntVector(0,  0, -1),	// Bottom
};

#define MORTON_MINUS_X 0x1			// FSvoUtils::CoordToMorton(FIntVector(1,0,0))
#define MORTON_MINUS_Y 0x2
#define MORTON_MINUS_Z 0x4

// Look-up table for the mask to just get the bits for a particular axis from a Morton
// code, and the offset value to subtract from those bits. We just have hardcoded offsets
// for 1 unit in each axis, but technically we could support larger offsets if we computed
// the Morton codes for them.
const uint32 FSvoUtils::MortonNeighborLUT[6][3] =
{
	{ MORTON_X_MASK, MORTON_X_MASK, MORTON_X_MASK },	// Front
	{ MORTON_Y_MASK, MORTON_Y_MASK, MORTON_Y_MASK },	// Right
	{ MORTON_Z_MASK, MORTON_Z_MASK, MORTON_Y_MASK },	// Top
	{ MORTON_X_MASK, MORTON_MINUS_X, 0 },				// Back
	{ MORTON_Y_MASK, MORTON_MINUS_Y, 0 },				// Left
	{ MORTON_Z_MASK, MORTON_MINUS_Z, 0 },				// Bottom
};

// Look-up table to find the children which touch a specific face
// (See ESvoNeighbor enum for index order)
// 6 Neighbors, 4 Children
const uint8 FSvoUtils::ChildTouchingNeighborLUT[6][4] =
{
	{ 1, 3, 5, 7 },  // Front
	{ 2, 3, 6, 7 },  // Right
	{ 4, 5, 6, 7 },  // Top
	{ 0, 2, 4, 6 },  // Back
	{ 0, 1, 4, 5 },  // Left
	{ 0, 1, 2, 3 },  // Bottom
};

const ESvoNeighbor FSvoUtils::NodeNeighborLUT[8][8] =
{
	{ ESvoNeighbor::Self,   ESvoNeighbor::Front,  ESvoNeighbor::Right,  ESvoNeighbor::Self,   ESvoNeighbor::Top,  ESvoNeighbor::Self,  ESvoNeighbor::Self,	ESvoNeighbor::Self  },	// Sibling 0
	{ ESvoNeighbor::Back,   ESvoNeighbor::Self,   ESvoNeighbor::Self,   ESvoNeighbor::Right,  ESvoNeighbor::Self, ESvoNeighbor::Top,   ESvoNeighbor::Self,  ESvoNeighbor::Self  },	// Sibling 1
	{ ESvoNeighbor::Left,   ESvoNeighbor::Self,   ESvoNeighbor::Self,   ESvoNeighbor::Front,  ESvoNeighbor::Self, ESvoNeighbor::Self,  ESvoNeighbor::Top,   ESvoNeighbor::Self  },	// Sibling 2
	{ ESvoNeighbor::Self,   ESvoNeighbor::Left,   ESvoNeighbor::Back,   ESvoNeighbor::Self,   ESvoNeighbor::Self, ESvoNeighbor::Self,  ESvoNeighbor::Self,  ESvoNeighbor::Top   },	// Sibling 3
	{ ESvoNeighbor::Bottom, ESvoNeighbor::Self,   ESvoNeighbor::Self,   ESvoNeighbor::Self,   ESvoNeighbor::Self, ESvoNeighbor::Front, ESvoNeighbor::Right, ESvoNeighbor::Self  },	// Sibling 4
	{ ESvoNeighbor::Self,   ESvoNeighbor::Bottom, ESvoNeighbor::Self,   ESvoNeighbor::Self,   ESvoNeighbor::Back, ESvoNeighbor::Self,  ESvoNeighbor::Self,  ESvoNeighbor::Right },	// Sibling 5
	{ ESvoNeighbor::Self,   ESvoNeighbor::Self,   ESvoNeighbor::Bottom, ESvoNeighbor::Self,   ESvoNeighbor::Left, ESvoNeighbor::Self,  ESvoNeighbor::Self,  ESvoNeighbor::Front },	// Sibling 6
	{ ESvoNeighbor::Self,   ESvoNeighbor::Self,   ESvoNeighbor::Self,   ESvoNeighbor::Bottom, ESvoNeighbor::Self, ESvoNeighbor::Left,  ESvoNeighbor::Back,  ESvoNeighbor::Self  },	// Sibling 7
};

// Look-up table for the voxels that make up the faces of a leaf node
// (See ESvoNeighbor for index order)
// 6 Neighbors, 16 voxels per face
const uint8 FSvoUtils::LeafFaceVoxelsLUT[6][16] =
{
	// Front
	{
		3,  7, 11, 15,
		19, 23, 27, 31,
		35, 39, 43, 47,
		51, 55, 59, 63
	},
	// Right
	{
		12, 13, 14, 15,
		28, 29, 30, 31,
		44, 45, 46, 47,
		60, 61, 62, 63
	},
	// Top
	{
		48, 49, 50, 51,
		52, 53, 54, 55,
		56, 57, 58, 59,
		60, 61, 62, 63
	},
	// Back
	{
		0, 4, 8, 12,
		16, 20, 24, 28,
		32, 36, 40, 44,
		48, 52, 56, 60,
	},
	// Left
	{
		0, 1, 2, 3,
		16, 17, 18, 19,
		32, 33, 34, 35,
		48, 49, 50, 51
	},
	// Bottom
	{
		0, 1, 2, 3,
		4, 5, 6, 7,
		8, 9, 10, 11,
		12, 13, 14, 15
	}
};

const int8 FSvoUtils::OppositeLeafFaceVoxelOffsetLUT[] =
{
	-3, -12, -48, 3, 12, 48
};

FIntVector FSvoUtils::VoxelGridExtents(SVO_VOXEL_GRID_EXTENT);

uint32 FSvoUtils::NextMorton(uint32 Code, uint32 MinCode, uint32 MaxCode)
{
	// Based on "Multidimensional Range Search in Dynamically Balanced Trees"
	// Implements the "BIGMIN decision table" from the paper, with some minor tweaks to
	// lock it to just supporting our 3D 10-bit codes, so we can use precomputed masks.
	// https://www.vision-tools.com/h-tropf/multidimensionalrangequery.pdf

	uint32 BigMin = 0;

	const uint32 AxisMasks[] = { MORTON_X_MASK, MORTON_Y_MASK, MORTON_Z_MASK };

	for (int32 CurBit = 29; CurBit >= 0; --CurBit)
	{
		const uint32 Mask = 1 << CurBit;
		const bool MinSet = MinCode & Mask;
		const bool MaxSet = MaxCode & Mask;
		const bool CodeSet = Code & Mask;

		if (!CodeSet && !MinSet && MaxSet)
		{
			// Compute a mask of just the bits for the current axis that are lower than
			// our current bit
			const uint32 LowerAxisBits = ((Mask - 1) & AxisMasks[CurBit % 3]);

			// Set BigMin to min with all the bits for this axis below the current bit
			// cleared
			BigMin = (MinCode & ~LowerAxisBits) | Mask;

			// Set all the bits for this axis below the current one in MaxCode, and clear
			// the current bit
			MaxCode = (MaxCode | LowerAxisBits) & ~Mask;
		}
		else if (CodeSet && !MinSet && MaxSet)
		{
			const uint32 LowerAxisBits = ((Mask - 1) & AxisMasks[CurBit % 3]);

			// Clear all the bits for this axis below the current one, and set the current
			MinCode = Mask | (MinCode & ~LowerAxisBits);
		}
		else if (!CodeSet && MinSet && MaxSet)
		{
			return MinCode;
		}
		else if (CodeSet && !MinSet && !MaxSet)
		{
			return BigMin;
		}
		// This case is not possible because min <= max
		else if ((!CodeSet && MinSet && !MaxSet) || (CodeSet && MinSet && !MaxSet))
		{
			checkNoEntry();
		}
	}

	return BigMin;
}

