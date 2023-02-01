// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

#include "Gunfire3DNavigationCustomVersion.h"
#include "SparseVoxelOctreeCommon.h"

struct FSvoNodeLinkBase
{
	// TODO: Remove the extra bit from VOXELIDX_SIZE and move it to the USERDATA_SIZE.
	// Then make layer 0 the voxel layer and 1 the leaf layer. We can then just check for
	// the voxel layer on a link to know if we should be checking the voxel index or not.
	// This would also make some array indexing line up better than having to check if
	// it's a voxel and index differently. -JMM

	// 3 bits for the layer index to support 6 layers per tile [0, 5]
	static const uint32 LAYERIDX_SIZE = 3;
	// 18 bits to support 6 layers of nodes with 8^6 (262,142 or max coord component value
	// of 63) Morton Codes at the leaf layer
	static const uint32 NODEIDX_SIZE = 18;
	// 6 bits for 64 voxels plus 1 significant bit to signify an unset state
	static const uint32 VOXELIDX_SIZE = 7;
	// 4 spare bits for user data
	static const uint32 USERDATA_SIZE = 4;

	union
	{
		uint32 NodeID;

		struct
		{
			uint32 LayerIdx : LAYERIDX_SIZE;
			uint32 NodeIdx : NODEIDX_SIZE;
			uint32 VoxelIdx : VOXELIDX_SIZE;
			uint32 UserData : USERDATA_SIZE;
		};
	};

	FSvoNodeLinkBase()
		: NodeID(SVO_INVALID_ID)
	{}

	FSvoNodeLinkBase(uint32 _LayerIdx, uint32 _NodeIdx, uint8 _VoxelIdx = SVO_NO_VOXEL)
		: LayerIdx(_LayerIdx)
		, NodeIdx(_NodeIdx)
		, VoxelIdx(_VoxelIdx)
		, UserData(0)
	{}

	inline friend FArchive& operator<<(FArchive& Ar, FSvoNodeLinkBase& NodeLinkBase);

	// Determines if the link contains valid data, ignoring any user data.
	bool IsValid() const
	{
		return (LayerIdx >= 0 && LayerIdx < SVO_MAX_LAYERS) &&
			   (NodeIdx >= 0 && NodeIdx < SVO_MAX_NODES) &&
			   ((VoxelIdx >= 0 && VoxelIdx < SVO_VOXELS_PER_LEAF) || (VoxelIdx == SVO_NO_VOXEL));
	}
	bool IsLeafNode() const { return (LayerIdx == SVO_LEAF_LAYER); }
	bool IsVoxelNode() const { return IsLeafNode() && VoxelIdx != SVO_NO_VOXEL; }
};

struct FSvoNodeLink : FSvoNodeLinkBase
{
	uint32 TileID;

	FSvoNodeLink()
	{
		SetID(SVO_INVALID_NODELINK);
	}

	FSvoNodeLink(uint64 ID)
	{
		SetID(ID);
	}

	FSvoNodeLink(uint32 _TileID, FSvoNodeLinkBase _Base)
		: FSvoNodeLinkBase(_Base)
		, TileID(_TileID)
	{}

	FSvoNodeLink(uint32 _TileID, uint32 _LayerIdx, uint32 _NodeIdx, uint8 _VoxelIdx = SVO_NO_VOXEL)
		: FSvoNodeLinkBase(_LayerIdx, _NodeIdx, _VoxelIdx)
		, TileID(_TileID)
	{}

	bool operator==(const FSvoNodeLink& Other) const
	{
		return (GetID() == Other.GetID());
	}

	bool operator!=(const FSvoNodeLink& Other) const
	{
		return (GetID() != Other.GetID());
	}

	inline friend FArchive& operator<<(FArchive& Ar, FSvoNodeLink& NodeLink);

	friend uint32 GetTypeHash(const FSvoNodeLink& Link) { return GetTypeHash(Link.GetID()); }

	// Returns the unique 64-bit ID for this node link, ignoring any user data
	uint64 GetID() const { return ((uint64)TileID << 32) | (NodeID | SVO_NODE_USERDATA_MASK); }

	// Sets the 64-bit ID for this node link. This is the composition of the TileID and
	// NodeID. While user data is applied, it is not considered during comparisons and is
	// only for external use.
	void SetID(uint64 ID) { TileID = (ID >> 32); NodeID = (uint32)ID; }

	// Calculates the parent link based on the current link. Note that this does not know
	// what the tile layer is, so if you call it with a tile link you will generate an
	// invalid link.
	FSvoNodeLink CalculateParent() const
	{
		return IsValid() ? FSvoNodeLink(TileID, LayerIdx + 1, NodeIdx >> 3) : FSvoNodeLink();
	}

	FSvoNodeLink CalculateChild(uint8 ChildIndex) const
	{
		// Ensure the child index is in a valid range (0, 8) and that we aren't trying to
		// calculate a child for a leaf node.
		ensure(LayerIdx > 0 && ChildIndex < 8);
		return IsValid() ? FSvoNodeLink(TileID, LayerIdx - 1, (NodeIdx << 3) + ChildIndex) : FSvoNodeLink();
	}
};

enum class ENodeState : uint8
{
	// This node has no blocked collision in it, so it has no children and this is the
	// highest resolution node for this region of the octree.
	Open,

	// Some of the space in this node is blocked, you need to recurse into the children to
	// figure out which.
	PartiallyBlocked,

	// This node is completely filled with collision, so it has no children and this is
	// the highest resolution node for this region of the octree.
	Blocked,
};

//
// A node represents a region of space in our collision octree. Nodes will have 8 higher
// resolution children (or none, if there is no blocked space inside them), except at the
// lowest level where the node is divided into 64 voxels. Those voxels define the
// navigable space.
//
class FSvoNode
{
public:
	FSvoNode() : Voxels(0ull) {}

	// Determines if a node is valid and currently being used by the octree.
	bool IsActive() const { return SelfLink.IsValid(); }

	// Returns a link to this node. This can be used to figure out the tile id, layer, etc.
	FSvoNodeLink GetSelfLink() const { return SelfLink; }

	// Returns a link to the parent of this node, or an invalid link if this is a tile
	// (tiles have no parent).
	inline FSvoNodeLink GetParentLink() const;

	// Returns whether the node is open, blocked, or partially blocked.
	// Open or blocked nodes are the highest resolution nodes, they will never have
	// children.
	inline ENodeState GetNodeState() const;

	// Returns true if this node has children. Leaf nodes will return false for this since
	// their children should be accessed through the voxel accessors. Also, a non-leaf
	// node will return false if there is no blocked space inside it.
	bool HasChildren() const { return IsLeafNode() ? false : (GetNodeState() == ENodeState::PartiallyBlocked); }

	// Returns the specified child link (0-7) for this node.
	inline FSvoNodeLink GetChildLink(uint8 Index) const;

	// Returns the link to a neighbor of this node. A node can have a lower resolution
	// neighbor if that neighbor is either open or blocked but it cannot have a higher
	// resolution neighbor (it would link to the parent node in that case).
	FSvoNodeLink GetNeighborLink(const class FSvoTile& ParentTile, ESvoNeighbor Neighbor) const;
	FSvoNodeLink GetNeighborLink(const class FSparseVoxelOctree& Octree, ESvoNeighbor Neighbor) const;

	// Leaf node utility
	bool IsLeafNode() const { return (SelfLink.LayerIdx == SVO_LEAF_LAYER); }
	bool IsVoxelBlocked(uint8 VoxelIdx) const { ensure(IsLeafNode()); ensure(VoxelIdx < 64); return ((Voxels & (uint64(1) << VoxelIdx)) != 0); }
	void SetVoxelBlocked(uint8 VoxelIdx) { ensure(IsLeafNode()); ensure(VoxelIdx < 64); Voxels |= (uint64(1) << VoxelIdx); }
	void SetVoxelEmpty(uint8 VoxelIdx) { ensure(IsLeafNode()); ensure(VoxelIdx < 64); Voxels &= ~(uint64(1) << VoxelIdx); }
	void ClearVoxels() { ensure(IsLeafNode()); Voxels = 0; }

	//////////////////////////////////////////////////////////////////////////////////////
	//
	// Non-const functions used for building the octree
	//

	// Initializes a node to be used in the octree. A valid link should be passed in,
	// along with whether this node is a tile or not.
	inline void Init(FSvoNodeLink SelfLinkIn, bool IsTile);

	// Resets a node so it can be returned to the pool.
	inline void Reset();

	void SetNodeState(ENodeState NodeStateIn) { ensure(!IsLeafNode()); NodeState = NodeStateIn; }

	inline void SetNeighborLink(ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink);

	// These shouldn't be normally used, they're just for serialization
	uint64 GetVoxelsForSerialization() const { ensure(IsLeafNode()); return Voxels; }
	void SetVoxelsForSerialization(uint64 VoxelsIn) { ensure(IsLeafNode()); Voxels |= VoxelsIn; }

	// Unsafe functions should only be used internally.
	void SetVoxelBlockedUnsafe(uint8 VoxelIdx) { ensure(VoxelIdx < 64); Voxels |= (1ull << VoxelIdx); }
	void ClearVoxelsUnsafe() { ensure(Voxels == 0ull); Voxels = 0ull; }

	inline FSvoNode& operator=(const FSvoNode& Other);

	inline void Serialize(FArchive& Ar);
	inline friend FArchive& operator<<(FArchive& Ar, FSvoNode& Node);

	inline void UpdateOldNode();

private:
	// Our node info
	FSvoNodeLink SelfLink;

	// All neighbors to this node.
	FSvoNodeLinkBase NeighborLinks[6];

	// Retain padding to store additional data in the future
	uint8 Padding[24];

	// This is data that is relevant to the specific node.  Basically if it is a leaf node
	// then this will be the voxel data otherwise it will be extra info about the node.
	union
	{
		// All leaf nodes are represented by 64 individual voxels.
		// This is simply a 64 bit field to store whether a given voxel (our highest graph resolution)
		// have something obstructing them or not.
		uint64 Voxels;
		// Non-leaf node flags
		struct
		{
			bool NodeIsTile;
			ENodeState NodeState;
		};
	};
};

//////////////////////////////////////////////////////////////////////////////////////////

static_assert(sizeof(FSvoNodeLink) == 8, "Expected node link to be 64 bits");

// This structure is optimized to fit in a cache line, so we should avoid bloating up the
// size with extra data or a virtual function table. There should be plenty of space for
// anything we need to add to non-leaf nodes in the voxel space, and there's extra space
// in the neighbors user data if we needed it for leaf nodes (although would be a hassle
// to use since we currently look for invalid nodes by checking for all the link data bits
// being set (SVO_INVALID_NODELINK).
static_assert(sizeof(FSvoNode) == 64, "Expected node to be 64 bytes");

FArchive& operator<<(FArchive& Ar, FSvoNodeLinkBase& NodeLinkBase)
{
	Ar << NodeLinkBase.NodeID;

	ensureAlways(NodeLinkBase.IsValid() || NodeLinkBase.NodeID == SVO_INVALID_ID);

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FSvoNodeLink& NodeLink)
{
	Ar << NodeLink.TileID;
	Ar << NodeLink.NodeID;

	ensureAlways(NodeLink.IsValid() || NodeLink == SVO_INVALID_NODELINK);

	return Ar;
}

FSvoNodeLink FSvoNode::GetParentLink() const
{
	return (SelfLink.IsValid() && (IsLeafNode() || !NodeIsTile)) ? SelfLink.CalculateParent() : FSvoNodeLink();
}

ENodeState FSvoNode::GetNodeState() const
{
	if (IsLeafNode())
	{
		// Open
		if (Voxels == 0ull)
		{
			return ENodeState::Open;
		}

		// Blocked
		if (Voxels == ~0ull)
		{
			return ENodeState::Blocked;
		}

		return ENodeState::PartiallyBlocked;
	}
	else
	{
		return NodeState;
	}
}


FSvoNodeLink FSvoNode::GetChildLink(uint8 Index) const
{
	ensure(HasChildren() && Index < 8);
	return SelfLink.CalculateChild(Index);
}

void FSvoNode::Init(FSvoNodeLink SelfLinkIn, bool IsTile)
{
	SelfLink = SelfLinkIn;
	NodeIsTile = IsTile;

	// We should always be initializing nodes with a valid link.
	ensureAlways(SelfLink.IsValid());
}

void FSvoNode::Reset()
{
	SelfLink = SVO_INVALID_NODELINK;
	for (FSvoNodeLinkBase& NeighborLink : NeighborLinks)
	{
		NeighborLink.NodeID = SVO_INVALID_ID;
	}
	Voxels = 0ull;
}

void FSvoNode::SetNeighborLink(ESvoNeighbor Neighbor, FSvoNodeLink NeighborLink)
{
	ensure(Neighbor < ESvoNeighbor::Self);

	NeighborLinks[(uint8)Neighbor].NodeID = NeighborLink.NodeID;

	// Utilize the user data to identify whether this neighboring link is a part of the
	// same tile or not.
	NeighborLinks[(uint8)Neighbor].UserData = (NeighborLink.TileID == GetSelfLink().TileID) ? (uint8)ESvoNeighbor::Self : (uint8)Neighbor;
}

FSvoNode& FSvoNode::operator=(const FSvoNode& Other)
{
	FMemory::Memcpy(this, &Other, sizeof(FSvoNode));
	return *this;
}

void FSvoNode::Serialize(FArchive& Ar)
{
	// Get the custom version from the archive
	int32 Version = Ar.CustomVer(FGunfire3DNavigationCustomVersion::GUID);

	Ar << SelfLink;
	ensureAlways(SelfLink.IsValid() || SelfLink == SVO_INVALID_NODELINK);

	if (Version < FGunfire3DNavigationCustomVersion::NodeLinkBaseAdded)
	{
		for (uint8 NeighborIdx = 0; NeighborIdx < 6; ++NeighborIdx)
		{
			// The order of the IDs has been flipped with this update so we need to
			// manually load and re-apply the values.
			uint32 NeighborTileID, NeighborNodeID;
			Ar << NeighborTileID;
			Ar << NeighborNodeID;

			NeighborLinks[NeighborIdx].NodeID = NeighborNodeID;
			if (NeighborTileID == SelfLink.TileID)
			{
				NeighborLinks[NeighborIdx].UserData = (uint8)ESvoNeighbor::Self;
			}
			else
			{
				NeighborLinks[NeighborIdx].UserData = NeighborIdx;
			}
		}
	}
	else
	{
		Ar.Serialize(&NeighborLinks, sizeof(NeighborLinks));
	}

	Ar << Voxels;
}

FArchive& operator<<(FArchive& Ar, FSvoNode& Node)
{
	Node.Serialize(Ar);
	return Ar;
}

void FSvoNode::UpdateOldNode()
{
	if (!IsLeafNode())
	{
		const bool bIsTile = (Voxels & (1ull << 0)) != 0;
		const bool bHasChildren = (Voxels & (1ull << 1)) != 0;

		NodeIsTile = bIsTile;
		NodeState = bHasChildren ? ENodeState::PartiallyBlocked : ENodeState::Open;
	}
}
