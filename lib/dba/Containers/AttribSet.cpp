#include "pch.h"
#include "AttribSet.h"

namespace imqs {
namespace dba {

AttribSet::~AttribSet() {
}

AttribSet::AttribSet() {
}

void AttribSet::Clear() {
	Set.clear();
	Allocator.Reset(false);
}

void AttribSet::Reset() {
	Set.clear_noalloc();
	Allocator.Reset(true);
}

void AttribSet::Insert(const Attrib& v) {
	if (Contains(v))
		return;
	Attrib* copy = new (Allocator.Alloc(sizeof(Attrib))) Attrib();
	copy->CopyFrom(v, &Allocator);
	Set.insert(copy);
}

void AttribSet::InsertNoCopy(const Attrib* v) {
	if (Contains(*v))
		return;
	Set.insert(v);
}

bool AttribSet::Contains(const Attrib& v) const {
	return Set.contains(&v);
}
} // namespace dba
} // namespace imqs
