// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

// Number of voxels stored per leaf node
#define SVO_VOXELS_PER_LEAF 64
// Extent of a an octant within the SVO
#define SVO_OCTANT_GRID_EXTENT 2
// Extent of a grid of voxels within a leaf node
#define SVO_VOXEL_GRID_EXTENT 4
// ID of the leaf layer
#define SVO_LEAF_LAYER 0
// Maximum number of layers supported (8^6, or 262,144, total nodes)
#define SVO_MAX_LAYERS 6
// Maximum supported nodes (8^6)
#define SVO_MAX_NODES 262144
// Minimum allowed node coordinate value (Morton codes only support positive values)
#define SVO_MIN_NODECOORD 0
// Maximum allowed node coordinate value (64*64*64 = 262,144)
#define SVO_MAX_NODECOORD 63

///> Values signifying invalid or unitialized data

#define SVO_INVALID_ID 0xFFFFFFFF
#define SVO_INVALID_NODELINK 0xFFFFFFFFFFFFFFFF
#define SVO_NO_VOXEL 0x7F

// Masks out the voxel of a Node ID or Link ID
#define SVO_NODE_VOXEL_MASK 0x000000000FE00000
// Masks out the user data of a Node ID or Link ID
#define SVO_NODE_USERDATA_MASK 0x00000000F0000000

// If set, SVO nodes, tiles, layers, etc. will be verified after various operations
#define SVO_VERIFY_NODES 0

// A helper for functions that implement a non-const getter function by calling a const
// getter function. Avoids duplicating all the const lookup code just to get a non-const
// return value.
#define MUTABLE_ACCESSOR(_ReturnType, ...) \
		const_cast<_ReturnType>(static_cast<const ThisClass&>(*this).__VA_ARGS__);

// Morton code size
typedef uint32 TMortonCode;

// All supported neighbor directions for a node.
enum class ESvoNeighbor : uint8
{
	Front,	// +X
	Right,	// +Y
	Top,	// +Z
	Back,	// -X
	Left,	// -Y
	Bottom,	// -Z
	Self,	// Helper for nodes within same parent
};

// Bit field of all neighbors
enum class ESvoNeighborFlags : uint8
{
	None,
	Front	= 1 << 0,
	Right	= 1 << 1,
	Top		= 1 << 2,
	Back	= 1 << 3,
	Left	= 1 << 4,
	Bottom	= 1 << 5,
};
ENUM_CLASS_FLAGS(ESvoNeighborFlags);

// Offsets to return when obtaining the location of a node
enum class ECellOffset : uint8
{
	Center,
	Min,
	Max,
};
