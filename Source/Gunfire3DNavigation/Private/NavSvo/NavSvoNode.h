// Copyright Gunfire Games, LLC. All Rights Reserved.
// Modified version of Recast/Detour's source file

//
// Copyright (c) 2009-2010 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

#pragma once

#include "SparseVoxelOctree/SparseVoxelOctreeNode.h"
#include "SparseVoxelOctree/SparseVoxelOctreeUtils.h"

enum ENavSvoNodeFlags
{
	NAVSVONODE_OPEN = 1 << 0,
	NAVSVONODE_CLOSED = 1 << 1,
};

typedef uint16 TNavSvoNodeIndex;

struct FNavSvoNode
{
	FSvoNodeLink NodeLink = SVO_INVALID_NODELINK;
	uint32 ParentIdx = 0;
	uint32 Flags = 0;

	float FCost = 0.f;
	float GCost = 0.f;
	float Heuristic = MAX_flt;
	ESvoNeighbor Neighbor = ESvoNeighbor::Front;
	FVector PortalLocation = FVector::ZeroVector;
	float TravelDistSqrd = 0.f;

	void Reset()
	{
		NodeLink = SVO_INVALID_NODELINK;
		ParentIdx = 0;
		Flags = 0;

		FCost = 0.f;
		GCost = 0.f;
		Heuristic = MAX_flt;
		Neighbor = ESvoNeighbor::Front;
		PortalLocation = FVector::ZeroVector;
		TravelDistSqrd = 0.f;
	}

	bool operator >(const FNavSvoNode& RHS) const
	{
		return (FCost > RHS.FCost);
	}
};

class FNavSvoNodePool
{
public:
	FNavSvoNodePool(uint32 InMaxNodes, uint32 InHashSize);
	~FNavSvoNodePool();

	void operator =(const FNavSvoNodePool&) {}

	void Clear();

	inline FNavSvoNode* GetNode(FSvoNodeLink NodeLink);
	inline FNavSvoNode* FindNode(FSvoNodeLink NodeLink);

	inline uint32 GetNodeIndex(const FNavSvoNode* Node) const;
	inline FNavSvoNode* GetNodeAtIndex(uint32 Idx);
	inline const FNavSvoNode* GetNodeAtIndex(uint32 Idx) const;

	uint32 GetMemUsed() const;

	uint32 GetMaxNodes() const { return MaxNodes; }
	uint32 GetNodeCount() const { return NodeCount; }

	uint32 GetHashSize() const { return HashSize; }
	TNavSvoNodeIndex GetFirst(uint32 Bucket) const { return First[Bucket]; }
	TNavSvoNodeIndex GetNext(uint32 NodeIdx) const { return Next[NodeIdx]; }

private:
	static uint32 HashNodeLink(FSvoNodeLink NodeLink)
	{
		uint64 ID = NodeLink.GetID();

		ID += ~(ID << 31);
		ID ^= (ID >> 20);
		ID += (ID << 6);
		ID ^= (ID >> 12);
		ID += ~(ID << 22);
		ID ^= (ID >> 32);

		return static_cast<uint32>(ID);
	}

private:
	TArray<FNavSvoNode> Nodes;
	TNavSvoNodeIndex* First;
	TNavSvoNodeIndex* Next;
	const uint32 MaxNodes;
	const uint32 HashSize;
	uint32 NodeCount;
};

class FNavSvoNodeQueue
{
public:
	FNavSvoNodeQueue(uint32 InCapacity);
	~FNavSvoNodeQueue();
	void operator =(FNavSvoNodeQueue&) {}

	void Clear() { Size = 0; }
	bool IsEmpty() const { return (Size == 0); }

	FNavSvoNode* Top() { return Heap[0]; }
	inline FNavSvoNode* Pop();
	inline void Push(FNavSvoNode* Node);
	inline void Modify(FNavSvoNode* Node) const;

	uint32 GetMemUsed() const;

	uint32 GetCapacity() const { return Capacity; }

private:
	inline void BubbleUp(uint32 NodeID, FNavSvoNode* Node) const;
	inline void TrickleDown(uint32 NodeID, FNavSvoNode* Node) const;

	FNavSvoNode** Heap;
	const uint32 Capacity;
	uint32 Size;
};

//////////////////////////////////////////////////////////////////////////
// NavSvoNodePool
//////////////////////////////////////////////////////////////////////////

FNavSvoNode* FNavSvoNodePool::GetNode(FSvoNodeLink NodeLink)
{
	if (NodeCount >= MaxNodes)
	{
		return nullptr;
	}

	const uint32 Bucket = HashNodeLink(NodeLink) & (HashSize - 1);
	TNavSvoNodeIndex NodeIdx = First[Bucket];
	FNavSvoNode* Node = nullptr;

	NodeIdx = static_cast<TNavSvoNodeIndex>(NodeCount);
	++NodeCount;

	// Init node
	Node = &Nodes[NodeIdx];
	Node->Reset();
	Node->NodeLink = NodeLink;

	Next[NodeIdx] = First[Bucket];
	First[Bucket] = NodeIdx;

	return Node;
}

FNavSvoNode* FNavSvoNodePool::FindNode(FSvoNodeLink NodeLink)
{
	const uint32 Bucket = HashNodeLink(NodeLink) & (HashSize - 1);
	TNavSvoNodeIndex NodeIdx = First[Bucket];
	while (NodeIdx != static_cast<TNavSvoNodeIndex>(~0))
	{
		if (Nodes[NodeIdx].NodeLink == NodeLink)
		{
			return &Nodes[NodeIdx];
		}
		NodeIdx = Next[NodeIdx];
	}
	return nullptr;
}

uint32 FNavSvoNodePool::GetNodeIndex(const FNavSvoNode* Node) const
{
	if (Node == nullptr)
	{
		return 0;
	}

	return static_cast<uint32>(Node - Nodes.GetData()) + 1;
}

FNavSvoNode* FNavSvoNodePool::GetNodeAtIndex(uint32 Idx)
{
	if (Idx == 0)
	{
		return nullptr;
	}

	return &Nodes[Idx - 1];
}

const FNavSvoNode* FNavSvoNodePool::GetNodeAtIndex(uint32 Idx) const
{
	if (Idx == 0)
	{
		return nullptr;
	}

	return &Nodes[Idx - 1];
}

//////////////////////////////////////////////////////////////////////////
// NavSvoNodeQueue
//////////////////////////////////////////////////////////////////////////

FNavSvoNode* FNavSvoNodeQueue::Pop()
{
	FNavSvoNode* Result = Heap[0];
	--Size;
	TrickleDown(0, Heap[Size]);
	return Result;
}

void FNavSvoNodeQueue::Push(FNavSvoNode* Node)
{
	++Size;
	BubbleUp(Size - 1, Node);
}

void FNavSvoNodeQueue::Modify(FNavSvoNode* Node) const
{
	for (uint32 NodeIdx = 0; NodeIdx < Size; ++NodeIdx)
	{
		if (Heap[NodeIdx] == Node)
		{
			BubbleUp(NodeIdx, Node);
			return;
		}
	}
}

void FNavSvoNodeQueue::BubbleUp(uint32 NodeID, FNavSvoNode* Node) const
{
	uint32 Parent = (NodeID - 1) / 2;
	// NOTE: (NodeID > 0) means there is a parent
	while ((NodeID > 0) && (*(Heap[Parent]) > *Node))
	{
		Heap[NodeID] = Heap[Parent];
		NodeID = Parent;
		Parent = (NodeID - 1) / 2;
	}
	Heap[NodeID] = Node;
}

void FNavSvoNodeQueue::TrickleDown(uint32 NodeID, FNavSvoNode* Node) const
{
	uint32 Child = (NodeID * 2) + 1;
	while (Child < Size)
	{
		if (((Child + 1) < Size) && (*(Heap[Child]) > *(Heap[Child + 1])))
		{
			++Child;
		}
		Heap[NodeID] = Heap[Child];
		NodeID = Child;
		Child = (NodeID * 2) + 1;
	}
	BubbleUp(NodeID, Node);
}
