#pragma once

#include "../Allocators.h"
#include "../Attrib.h"

namespace imqs {
namespace dba {

// Hashed set of attributes
class IMQS_DBA_API AttribSet {
public:
	typedef ohash::set<const Attrib*, AttribPtrGetHashCode, AttribPtrGetKey_Set<const Attrib*>> TSet;

	~AttribSet();
	AttribSet();

	void Clear(); // Clear and release all memory
	void Reset(); // Reset for repeated usage - keeps heap memory allocated

	void Insert(const Attrib& v);       // Insert a copy of 'v'
	void InsertNoCopy(const Attrib* v); // Insert 'v' directly (no copy)
	bool Contains(const Attrib& v) const;

	size_t Size() const { return Set.size(); }

	TSet::iterator begin() const { return Set.begin(); }
	TSet::iterator end() const { return Set.end(); }

private:
	SimpleAllocator Allocator;
	TSet            Set;

	AttribSet(const AttribSet& other) = delete;
	AttribSet& operator=(const AttribSet& other) = delete;
};
} // namespace dba
} // namespace imqs
