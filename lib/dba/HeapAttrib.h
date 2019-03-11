#pragma once

#include "Attrib.h"
#include "Allocators.h"

namespace imqs {
namespace dba {

/* A wrapper for an Attrib that uses a dumb allocator that
always allocates storage from the heap. This is necessary
for storing Attrib objects inside long-lived data structures,
so that they don't occupy the limited thread-local storage pools
that are designed to speed up the allocation of Attribs who's
lifetimes are limited to the stack.

We could improve the efficiency of this thing by replacing
OnceOffAllocator with an allocator that has just one piece
of internal state: A pointer to a block of memory that
has been malloc'ed.
*/
class IMQS_DBA_API HeapAttrib {
private:
	Attrib           Val;
	OnceOffAllocator Alloc;

public:
	HeapAttrib() {}
	HeapAttrib(const HeapAttrib& val);
	HeapAttrib(HeapAttrib&& val);
	HeapAttrib(const Attrib& val);
	~HeapAttrib() {}
	HeapAttrib& operator=(const HeapAttrib& val);
	HeapAttrib& operator=(HeapAttrib&& val);
	HeapAttrib& operator=(const Attrib& val);

	bool operator<(const HeapAttrib& b) const { return Val.Compare(b) < 0; }

	const Attrib& GetVal() const { return Val; }

	operator const Attrib&() const { return Val; }

private:
	void Move(HeapAttrib&& val);
};
} // namespace dba
} // namespace imqs
