#pragma once

#include "../Allocators.h"
#include "../Attrib.h"
#include "VarArgs.h"

namespace imqs {
namespace dba {

/* A linear array of attributes.

You MUST use the container as the allocator for the attribs inside this container.
If you do not, then you will get memory leaks, because the attributes are never destroyed.

AttribList uses RepeatCycleAllocator, which means that whenever Reset() is called,
it remembers the number of bytes that were allocated, and makes sure that it increases
it's chunk size so as to accommodate at least that number of bytes in the next round.
This makes repeated use of an AttribList efficient.

Example:

	AttribList list;
	list.Add()->SetText("hello", &list);
	list.Add()->SetText("world", &list);
	list.Add()->SetText(123);

or, using AddV:

	AttribList list;
	list.AddV("hello", "world", 123);

*/
class IMQS_DBA_API AttribList : public Allocator {
public:
	AttribList();
	AttribList(const AttribList& other);
	AttribList(AttribList&& other);
	~AttribList();

	void* Alloc(size_t bytes) override;

	AttribList& operator=(const AttribList& other);
	AttribList& operator=(AttribList&& other);

	void        Clear(); // Clear and release all memory
	void        Reset(); // Reset for repeated usage - keeps heap memory allocated
	AttribList* Clone() const;
	hash::Sig16 Signature() const;
	void        BytesAllocated(size_t& attribs, size_t& attribPtrs, size_t& dynamic) const;

	Attrib* Add(); // Add a new attribute. You must set it's value with this AttribList as the allocator.

	size_t        Size() const { return Len; }
	const Attrib* operator[](size_t i) const { return &Values[i]; }
	const Attrib* At(size_t i) const { return &Values[i]; }
	Attrib*       Back() { return &Values[Len - 1]; }

	// Generate a list of pointers to the values in the list.
	// Some APIs, such as Executor::Exec, take a list of pointers.
	// This class stores a list of concrete values, so if you want a list
	// of pointers, then we need to produce that.
	const Attrib** ValuesPtr();

	// Add a variable number of attributes, for example list.AddV("hello", 123);
	template <typename... Args>
	void AddV(const Args&... args) {
		const auto   num_args = sizeof...(Args);
		varargs::Arg pack_array[num_args + 1]; // +1 for zero args case
		varargs::PackArgs(pack_array, args...);
		AddVarArgPack(num_args, pack_array);
	}

private:
	RepeatCycleAllocator Allocator;
	Attrib*              Values         = nullptr;
	size_t               Cap            = 0;
	size_t               Len            = 0;
	Attrib**             ValuePtrs      = nullptr;
	size_t               ValuePtrsSize  = 0;
	bool                 ValuePtrsDirty = true;

	void GrowValues();
	void AddVarArgPack(size_t n, const varargs::Arg* args);
};
} // namespace dba
} // namespace imqs
