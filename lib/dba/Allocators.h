#pragma once
#include "mem.h"

namespace imqs {
namespace dba {

// A really dumb little allocator built for repeated use of a single allocation
// After every call to Alloc, you must call Reset, before you can call Alloc again.
// The memory will only be freed when OnceOffAllocator is destroyed.
// This thing's job could probably be subsumed by RepeatCycleAllocator. They have
// an identical API.
class IMQS_DBA_API OnceOffAllocator : public Allocator {
public:
	void*  Buf   = nullptr;
	size_t Cap   = 0;
	bool   Ready = true;

	OnceOffAllocator();
	~OnceOffAllocator();

	void* Alloc(size_t bytes) override;
	void  Reset() { Ready = true; }
	void  Free();

	OnceOffAllocator& operator=(OnceOffAllocator&& m);
};

// A very simple allocator that uses a fixed amount of space from the stack.
// If that stack space runs out, then it switches to the heap.
// Example:
//     uint8_t buf[128];
//     StackAlloc stalloc(buf);
class IMQS_DBA_API StackAlloc : public Allocator {
public:
	uint8_t* Buf         = nullptr;
	size_t   Pos         = 0;
	size_t   Cap         = 0;
	bool     IsBufOnHeap = false;

	template <size_t n>
	StackAlloc(uint8_t (&buf)[n]) {
		Buf = buf;
		Cap = n;
	}

	~StackAlloc();

	void* Alloc(size_t bytes) override;
};

// Allocator that is designed to be frequently flushed and reused. This was
// built to hold a single row from a result set. The memory stored inside here
// is recycled every time a new row is fetched.
// Every time a new allocation is made, ChunkSize grows exponentially,
// until the limit of MaxChunkSize
class IMQS_DBA_API RepeatCycleAllocator : public Allocator {
public:
	static const size_t MaxChunkSize = 16 * 1024 * 1024;

	std::vector<uint8_t*> Chunks;
	size_t                TotalAllocated        = 0;
	size_t                RemainingInLastChunk  = 0;
	size_t                ChunkSize             = 256;
	bool                  PreserveMemoryOnReset = true; // Set this to false, to release all heap memory when Reset() is called.

	RepeatCycleAllocator();
	~RepeatCycleAllocator();
	void* Alloc(size_t bytes) override;
	void  Reset();
	void  Reset(bool preserveMemory); // override PreserveMemoryOnReset with preserveMemory
};

// This is just RepeatCycleAllocator, but PreserveMemoryOnReset is switched off by default
class IMQS_DBA_API SimpleAllocator : public RepeatCycleAllocator {
public:
	SimpleAllocator();
};

// Not really an allocator. Just returns a single buffer that you give it.
// Use this to avoid allocations for temporary BLOB objects, for example:
//
//     func(const void* buf, size_t len)
//     {
//         IdentityAllocator alloc(buf, len);
//         Attrib val;
//         val.SetBin(nullptr, len, alloc);
//         dosomething(val);
//     }
//
// The above sequence avoids an unnecessary memory copy.
class IMQS_DBA_API IdentityAllocator : public Allocator {
public:
	const void* Buf = nullptr;
	size_t      Len = 0;

	IdentityAllocator(const void* buf, size_t len);
	~IdentityAllocator();
	void* Alloc(size_t bytes) override;
};
} // namespace dba
} // namespace imqs
