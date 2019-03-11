#include "pch.h"
#include "AttribList.h"

namespace imqs {
namespace dba {

// See some reasoning behind this number inside GrowValues, but basically
// this is just a thumb suck.
static const int DefaultChunkSize = 512;

AttribList::AttribList() {
	Allocator.ChunkSize = DefaultChunkSize;
}

AttribList::AttribList(const AttribList& other) : AttribList() {
	*this = other;
}

AttribList::AttribList(AttribList&& other) : AttribList() {
	*this = std::move(other);
}

AttribList::~AttribList() {
	free(ValuePtrs);
}

void* AttribList::Alloc(size_t bytes) {
	return Allocator.Alloc(bytes);
}

AttribList& AttribList::operator=(const AttribList& other) {
	Clear();
	for (size_t i = 0; i < other.Len; i++) {
		Add()->CopyFrom(*other.At(i), this);
	}
	return *this;
}

AttribList& AttribList::operator=(AttribList&& other) {
	Clear();
	std::swap(Allocator, other.Allocator);
	std::swap(Values, other.Values);
	std::swap(Cap, other.Cap);
	std::swap(Len, other.Len);
	std::swap(ValuePtrs, other.ValuePtrs);
	std::swap(ValuePtrsSize, other.ValuePtrsSize);
	std::swap(ValuePtrsDirty, other.ValuePtrsDirty);
	return *this;
}

void AttribList::Clear() {
	Allocator.Reset(false);
	Reset();
	free(ValuePtrs);
	ValuePtrs      = nullptr;
	ValuePtrsSize  = 0;
	ValuePtrsDirty = true;
}

void AttribList::Reset() {
	Allocator.Reset(true);
	Values         = nullptr;
	Cap            = 0;
	Len            = 0;
	ValuePtrsDirty = true;
}

AttribList* AttribList::Clone() const {
	AttribList* c = new AttribList();
	for (size_t i = 0; i < Len; i++) {
		c->Add()->CopyFrom(*At(i), c);
	}
	return c;
}

hash::Sig16 AttribList::Signature() const {
	SpookyHash h;
	h.Init(0, 0);

	h.Update(&Len, sizeof(size_t));
	for (size_t i = 0; i < Len; i++) {
		auto hash = Values[i].GetHashCode();
		h.Update(&hash, sizeof(hash));
	}

	hash::Sig16 sig;
	h.Final(&sig.QWords[0], &sig.QWords[1]);
	return sig;
}

void AttribList::BytesAllocated(size_t& attribs, size_t& attribPtrs, size_t& dynamic) const {
	attribs    = Len * sizeof(Attrib);
	attribPtrs = ValuePtrsSize * sizeof(Attrib*);
	dynamic    = Allocator.TotalAllocated;
}

Attrib* AttribList::Add() {
	ValuePtrsDirty = true;
	if (Len == Cap)
		GrowValues();
	return &Values[Len++];
}

const Attrib** AttribList::ValuesPtr() {
	if (!ValuePtrsDirty)
		return (const Attrib**) ValuePtrs;

	if (ValuePtrsSize != Len) {
		free(ValuePtrs);
		ValuePtrs     = (Attrib**) imqs_malloc_or_die(sizeof(Attrib*) * Len);
		ValuePtrsSize = Len;
	}

	for (size_t i = 0; i < Len; i++)
		ValuePtrs[i] = &Values[i];
	ValuePtrsDirty = false;
	return (const Attrib**) ValuePtrs;
}

void AttribList::GrowValues() {
	// 8 * sizeof(Attrib) = 8 * 16 = 128 bytes. For a chunk size of 512 bytes,
	// that leaves 384 bytes for dynamic storage.
	auto newCap    = Cap * 2;
	newCap         = std::max(newCap, (size_t) 8);
	auto newValues = Allocator.Alloc(sizeof(Attrib) * newCap);
	if (Cap)
		memcpy(newValues, Values, sizeof(Attrib) * Cap);
	Values = (Attrib*) newValues;
	for (size_t i = Cap; i < newCap; i++)
		new (&Values[i]) Attrib();
	Cap = newCap;
}

void AttribList::AddVarArgPack(size_t n, const varargs::Arg* args) {
	for (size_t i = 0; i < n; i++)
		Add()->Set(args[i], this);
}
} // namespace dba
} // namespace imqs
