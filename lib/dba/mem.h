#pragma once

// copied from tlsf.h, to avoid introducing tlsf as a dependency for users of dba
typedef void* tlsf_t;

namespace imqs {
namespace dba {

// Garbage-collected allocator
class Allocator {
public:
	virtual void* Alloc(size_t bytes) = 0;
};

template <int ItemSizeShift>
class MemPoolList {
public:
	// This is the upper limit on how many Attribs you can have in-flight on your stack
	static const int Capacity = 200;

	// The number of bytes returned by each call to Alloc()
	static const int ItemSize = 1 << ItemSizeShift;

	// Maximum 256 items. By making pos_t uint8, our freelist remains small. If you need
	// to raise the capacity above 255, then you'll need to change pos_t to a uint16.
	typedef uint8_t pos_t;

	void* Block;
	pos_t FreeList[Capacity];
	int   NumFree;

	MemPoolList() {
		for (int i = 0; i < Capacity; i++)
			FreeList[i] = i;
		Block   = malloc(Capacity << ItemSizeShift);
		NumFree = Capacity;
	}
	~MemPoolList() {
		free(Block);
	}

	void* Alloc(size_t size) {
		if (size > (size_t) ItemSize)
			return nullptr;
		return AllocNoSizeCheck();
	}

	void* AllocNoSizeCheck() {
		if (NumFree == 0)
			return nullptr;
		NumFree--;
		return PtrFromPos(FreeList[NumFree]);
	}

	void Free(void* ptr) {
		if (ptr == nullptr)
			return;
		pos_t pos = PosFromPtr(ptr);
		IMQS_ASSERT(pos >= 0 && pos < Capacity && NumFree < Capacity);
		FreeList[NumFree++] = pos;
	}

	bool  BelongsTo(void* ptr) const { return (uintptr_t) ptr - (uintptr_t) Block < (uintptr_t)(Capacity << ItemSizeShift); }
	pos_t PosFromPtr(void* ptr) const { return (pos_t)(((intptr_t) ptr - (intptr_t) Block) >> ItemSizeShift); }
	void* PtrFromPos(pos_t pos) const { return (char*) Block + ((intptr_t) pos << ItemSizeShift); }
};

/* Very custom heap that Attrib uses to store Text, Binary blobs, Geometry, Guids.
This is only designed to hold as many attribs as there are "in flight" on the
stack. If you're storing attribs anywhere other than on the stack, then you
should be using purpose-built containers.

Allocations are tried in the following order:
1. If small enough (depends on type), from the fixed-size MemPoolList<> buckets
2. If less than 16384, from the tlsf heap, which is thread-local
3. From malloc

The thread-local heap uses https://github.com/mattconte/tlsf. We pre-allocate 1MB per thread,
and never grow that amount. On Windows, it would probably be better to use HeapCreate to
create the custom heaps. I suspect that heap will have better performance than tlsf.

There is no scientific basis for the many constants in here - they are purely thumbsuck.
The constants are:
- MemPoolList.Capacity       200 (max number of slots from the fixed-size pools)
- Max tlsf allocation size 16384 (arbitrary decision. reasoning is that as size grows large, malloc overhead becomes trivially small)

According to @rygorous (2016-06-27): a 4k memcpy that's in L1/L2 is ~150-300 cycles.
From tlsf websites, we can say that a tlsf alloc is around 150 cycles.

On Windows, our default stack size is 1MB. On Linux x86-32, it is 2MB. The storage we
define here can be thought of as extra stack space, so it seems reasonable for a MemPool
object to consume around 1MB.
*/
class IMQS_DBA_API MemPool {
public:
	static const size_t TlsfHeapSize = 1024 * 1024;
	static const size_t MaxTlsfSize  = 16384;

	MemPoolList<4> Guid;      // 16 bytes per item (obviously, because it's a GUID)
	MemPoolList<6> Text;      // 64 bytes per item
	MemPoolList<6> Bin;       // 64 bytes per item
	MemPoolList<5> Point;     // 32 bytes per item (can hold XYZ double vertex)
	MemPoolList<8> Geom;      // 256 bytes per item (16 XY double vertices)
	tlsf_t         Heap;      // Small thread-local heap for minor overflows
	void*          HeapBlock; // Memory block assigned to Heap

	// Size of fixed buckets = 200 * (16 + 64 + 64 + 32 + 256) = 84 KB

	static void     Initialize();
	static void     Shutdown();
	static MemPool* GetPoolForThread();

	static imqs::Guid* AllocGuid();
	static char*       AllocText(size_t len);
	static void*       AllocBin(size_t len);
	static void*       AllocPoint(size_t len);
	static void*       AllocGeom(size_t len);

	static void FreeGuid(imqs::Guid* p);
	static void FreeText(void* p);
	static void FreeBin(void* p);
	static void FreePoint(void* p);
	static void FreeGeom(void* p);

	bool IsInTlsfHeap(void* p); // Exposed for unit tests

	MemPool();
	~MemPool();
};
} // namespace dba
} // namespace imqs