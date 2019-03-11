#pragma once

#include "../Allocators.h"
#include "../Attrib.h"

namespace imqs {
namespace dba {

// Hash map from Attrib to TVal
template <typename TVal>
class AttribMap {
public:
	typedef ohash::map<const Attrib*, TVal, AttribPtrGetHashCode, AttribPtrGetKey_Pair<const Attrib*, TVal>> TMap;
	typedef typename TMap::iterator                                                                          iterator;

	AttribMap() {
	}

	void Insert(const Attrib& key, const TVal& val) {
		Attrib* copy = new (Allocator.Alloc(sizeof(Attrib))) Attrib();
		copy->CopyFrom(key, &Allocator);
		Map.insert(copy, val, true);
	}

	TVal Get(const Attrib& key) const {
		return Map.get(&key);
	}

	TVal Get(const Attrib& key, TVal ifNotExist) const {
		TVal* p = Map.getp(&key);
		return p ? *p : ifNotExist;
	}

	size_t Size() const { return Map.size(); }

	iterator begin() const { return Map.begin(); }
	iterator end() const { return Map.end(); }

private:
	SimpleAllocator Allocator;
	TMap            Map;

	AttribMap(const AttribMap& other) = delete;
	AttribMap& operator=(const AttribMap& other) = delete;
};
} // namespace dba
} // namespace imqs
