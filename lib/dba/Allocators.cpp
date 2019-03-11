#include "pch.h"
#include "Allocators.h"

namespace imqs {
namespace dba {

const size_t RepeatCycleAllocator::MaxChunkSize;

RepeatCycleAllocator::RepeatCycleAllocator() {
}

RepeatCycleAllocator::~RepeatCycleAllocator() {
	Reset(false);
}

void* RepeatCycleAllocator::Alloc(size_t bytes) {
	IMQS_ASSERT(bytes != 0); // will crash if bytes == 0 and Chunks is empty.
	if (bytes > RemainingInLastChunk) {
		// Allocate a new chunk.
		size_t target = std::min(TotalAllocated, MaxChunkSize);
		size_t need   = std::max(bytes, target);
		while (ChunkSize < need)
			ChunkSize *= 2;

		Chunks.push_back((uint8_t*) imqs_malloc_or_die(ChunkSize));
		RemainingInLastChunk = ChunkSize;
		TotalAllocated += ChunkSize;
	}

	auto p = Chunks[Chunks.size() - 1] + (ChunkSize - RemainingInLastChunk);
	IMQS_ASSERT(RemainingInLastChunk >= bytes);
	RemainingInLastChunk -= bytes;
	return p;
}

void RepeatCycleAllocator::Reset() {
	Reset(PreserveMemoryOnReset);
}

void RepeatCycleAllocator::Reset(bool preserveMemory) {
	if (preserveMemory && Chunks.size() == 1) {
		RemainingInLastChunk = ChunkSize;
	} else {
		for (auto p : Chunks)
			free(p);
		Chunks.clear();
		if (preserveMemory) {
			// Grow chunksize aggressively, so that we can quickly get to the point
			// where our single chunk is large enough to hold enough memory for
			// every iteration of whatever it is we're doing.
			while (ChunkSize < TotalAllocated)
				ChunkSize *= 2;
			ChunkSize = std::min(ChunkSize, MaxChunkSize);
		}
		TotalAllocated       = 0;
		RemainingInLastChunk = 0;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

SimpleAllocator::SimpleAllocator() {
	PreserveMemoryOnReset = false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

StackAlloc::~StackAlloc() {
	if (IsBufOnHeap)
		free(Buf);
}

void* StackAlloc::Alloc(size_t bytes) {
	if (Pos + bytes > Cap) {
		size_t newCap = Cap;
		while (Pos + bytes > newCap)
			newCap *= 2;
		void* nbuf = realloc(IsBufOnHeap ? Buf : nullptr, newCap);
		IMQS_ASSERT(nbuf != nullptr);
		if (!IsBufOnHeap)
			memcpy(nbuf, Buf, Pos);
		Buf = (uint8_t*) nbuf;
		Cap = newCap;
	}
	void* p = Buf + Pos;
	Pos += bytes;
	return p;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

OnceOffAllocator::OnceOffAllocator() {
}

OnceOffAllocator::~OnceOffAllocator() {
	free(Buf);
}

void* OnceOffAllocator::Alloc(size_t bytes) {
	IMQS_ASSERT(bytes != 0); // sanity
	IMQS_ASSERT(Ready);
	Ready = false;
	if (bytes > Cap) {
		if (Cap == 0)
			Cap = 64;
		while (Cap < bytes)
			Cap *= 2;
		free(Buf);
		Buf = malloc(Cap);
		if (Buf == nullptr)
			IMQS_DIE_MSG(tsf::fmt("OnceOffAllocator: Out of memory allocating %v bytes", bytes).c_str());
	}
	return Buf;
}

void OnceOffAllocator::Free() {
	free(Buf);
	Buf   = nullptr;
	Ready = true;
	Cap   = 0;
}

OnceOffAllocator& OnceOffAllocator::operator=(OnceOffAllocator&& m) {
	Free();

	Buf   = m.Buf;
	Ready = m.Ready;
	Cap   = m.Cap;

	m.Buf   = nullptr;
	m.Ready = true;
	m.Cap   = 0;

	return *this;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

IdentityAllocator::IdentityAllocator(const void* buf, size_t len) : Buf(buf), Len(len) {
}

IdentityAllocator::~IdentityAllocator() {
}

void* IdentityAllocator::Alloc(size_t bytes) {
	IMQS_ASSERT(bytes <= Len);
	return const_cast<void*>(Buf);
}
} // namespace dba
} // namespace imqs