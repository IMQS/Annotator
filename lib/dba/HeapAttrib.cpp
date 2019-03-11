#include "pch.h"
#include "HeapAttrib.h"

namespace imqs {
namespace dba {

HeapAttrib::HeapAttrib(const HeapAttrib& val) {
	*this = val;
}

HeapAttrib::HeapAttrib(HeapAttrib&& val) {
	Move(std::move(val));
}

HeapAttrib::HeapAttrib(const Attrib& val) {
	Val.CopyFrom(val, &Alloc);
}

HeapAttrib& HeapAttrib::operator=(HeapAttrib&& val) {
	if (this != &val) {
		Alloc.Free();
		Move(std::move(val));
	}
	return *this;
}

HeapAttrib& HeapAttrib::operator=(const HeapAttrib& val) {
	Alloc.Reset();
	Val.CopyFrom(val, &Alloc);
	return *this;
}

HeapAttrib& HeapAttrib::operator=(const Attrib& val) {
	Alloc.Reset();
	Val.CopyFrom(val, &Alloc);
	return *this;
}

void HeapAttrib::Move(HeapAttrib&& val) {
	memcpy(&Val, &val.Val, sizeof(Val));
	memset(&val.Val, 0, sizeof(val.Val));

	Alloc = std::move(val.Alloc);
}

} // namespace dba
} // namespace imqs
