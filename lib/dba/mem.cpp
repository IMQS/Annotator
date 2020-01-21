#include "pch.h"
#include "mem.h"

namespace imqs {
namespace dba {

const int            MaxPools = 256;
std::atomic<int32_t> NextThreadId;
static MemPool*      Pools[MaxPools];

#if defined(_MSC_VER) && _MSC_VER < 1900
static __declspec(thread) int32_t MyThreadId = 0; // Just for intellisense on VS 2013
#else
static thread_local int32_t MyThreadId = 0;
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const size_t MemPool::TlsfHeapSize;
const size_t MemPool::MaxTlsfSize;

MemPool::MemPool() {
	HeapBlock = malloc(TlsfHeapSize);
	IMQS_ASSERT(HeapBlock != nullptr);
	Heap = tlsf_create_with_pool(HeapBlock, TlsfHeapSize);
}

MemPool::~MemPool() {
	tlsf_destroy(Heap);
	free(Heap);
}

void MemPool::Initialize() {
	NextThreadId = 1;
}

void MemPool::Shutdown() {
	for (int i = 0; i < MaxPools; i++)
		delete Pools[i];
}

MemPool* MemPool::GetPoolForThread() {
	if (MyThreadId == 0) {
		MyThreadId = NextThreadId++;

		// You must use a pool of threads which gets reused. If your machine truly needs more
		// than 256 threads, then raise MaxPools.
		IMQS_ASSERT(MyThreadId < MaxPools);

		Pools[MyThreadId] = new MemPool();
	}
	return Pools[MyThreadId];
}

template <typename TPool>
void* AllocAny(tlsf_t heap, TPool& pool, size_t len) {
	void* p = pool.Alloc(len);
	if (p != nullptr)
		return (char*) p;

	if (len <= MemPool::MaxTlsfSize) {
		p = tlsf_malloc(heap, len);
		if (p != nullptr)
			return (char*) p;
	}

	p = malloc(len);
	IMQS_ASSERT(p != nullptr);
	return (char*) p;
}

template <typename TPool>
void FreeAny(tlsf_t heap, void* heapBlock, TPool& pool, void* p) {
	if (p == nullptr)
		return;

	if (pool.BelongsTo(p))
		pool.Free(p);
	else if ((uintptr_t) p - (uintptr_t) heapBlock < (uintptr_t) MemPool::TlsfHeapSize)
		tlsf_free(heap, p);
	else
		free(p);
}

imqs::Guid* MemPool::AllocGuid() {
	auto pool = GetPoolForThread();
	return (imqs::Guid*) AllocAny(pool->Heap, pool->Guid, 16);
}

char* MemPool::AllocText(size_t len) {
	auto pool = GetPoolForThread();
	return (char*) AllocAny(pool->Heap, pool->Text, len);
}

void* MemPool::AllocBin(size_t len) {
	auto pool = GetPoolForThread();
	return AllocAny(pool->Heap, pool->Bin, len);
}

void* MemPool::AllocPoint(size_t len) {
	auto pool = GetPoolForThread();
	return AllocAny(pool->Heap, pool->Point, len);
}

void* MemPool::AllocGeom(size_t len) {
	auto pool = GetPoolForThread();
	return AllocAny(pool->Heap, pool->Geom, len);
}

void MemPool::FreeGuid(imqs::Guid* p) {
	auto pool = GetPoolForThread();
	FreeAny(pool->Heap, pool->HeapBlock, pool->Guid, p);
}

void MemPool::FreeText(void* p) {
	auto pool = GetPoolForThread();
	FreeAny(pool->Heap, pool->HeapBlock, pool->Text, p);
}

void MemPool::FreeBin(void* p) {
	auto pool = GetPoolForThread();
	FreeAny(pool->Heap, pool->HeapBlock, pool->Bin, p);
}

void MemPool::FreePoint(void* p) {
	auto pool = GetPoolForThread();
	FreeAny(pool->Heap, pool->HeapBlock, pool->Point, p);
}

void MemPool::FreeGeom(void* p) {
	auto pool = GetPoolForThread();
	FreeAny(pool->Heap, pool->HeapBlock, pool->Geom, p);
}

bool MemPool::IsInTlsfHeap(void* p) {
	return ((uintptr_t) p - (uintptr_t) Heap) < (uintptr_t) TlsfHeapSize;
}
} // namespace dba
} // namespace imqs