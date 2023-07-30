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

#include "NavSvoNode.h"

//////////////////////////////////////////////////////////////////////////
// NodePool
//////////////////////////////////////////////////////////////////////////
FNavSvoNodePool::FNavSvoNodePool(uint32 InMaxNodes, uint32 InHashSize)
	: First(nullptr)
	, Next(nullptr)
	, MaxNodes(InMaxNodes)
	, HashSize(InHashSize)
	, NodeCount(0)
{
	check(FMath::RoundUpToPowerOfTwo(HashSize) == HashSize);
	check(MaxNodes > 0);

	Nodes.SetNum(MaxNodes);
	First = static_cast<TNavSvoNodeIndex*>(FMemory::Malloc(sizeof(TNavSvoNodeIndex) * InHashSize));
	Next = static_cast<TNavSvoNodeIndex*>(FMemory::Malloc(sizeof(TNavSvoNodeIndex) * MaxNodes));

	// Halt execution if allocation fails so we don't stomp valid memory on the next few lines and corrupt memory.
	check(Next != nullptr && First != nullptr);

	FMemory::Memset(First, 0xFF, sizeof(TNavSvoNodeIndex) * HashSize);
	FMemory::Memset(Next, 0xFF, sizeof(TNavSvoNodeIndex) * MaxNodes);
}

FNavSvoNodePool::~FNavSvoNodePool()
{
	FMemory::Free(Next);
	FMemory::Free(First);
}

void FNavSvoNodePool::Clear()
{
	FMemory::Memset(First, 0xFF, sizeof(TNavSvoNodeIndex) * HashSize);
	NodeCount = 0;
}

uint32 FNavSvoNodePool::GetMemUsed() const
{
	return sizeof(FNavSvoNode) * MaxNodes
		+ sizeof(TNavSvoNodeIndex) * MaxNodes
		+ sizeof(TNavSvoNodeIndex) * HashSize;
}

//////////////////////////////////////////////////////////////////////////
// NodeQueue
//////////////////////////////////////////////////////////////////////////
FNavSvoNodeQueue::FNavSvoNodeQueue(uint32 InCapacity)
	: Heap(nullptr)
	, Capacity(InCapacity)
	, Size(0)
{
	ensureMsgf(Capacity > 0, TEXT("Attempting to create node queue with size of zero!"));

	Heap = static_cast<FNavSvoNode**>(FMemory::Malloc(sizeof(FNavSvoNode*) * (Capacity + 1)));
	checkf(Heap, TEXT("Failed to create heap for node queue!"));
}

FNavSvoNodeQueue::~FNavSvoNodeQueue()
{
	FMemory::Free(Heap);
}

uint32 FNavSvoNodeQueue::GetMemUsed() const
{
	return sizeof(FNavSvoNode*) * (Capacity + 1);
}