#pragma once

#include "../alloc.h"

namespace imqs {

/* A replacement for std::vector, which has some small fixed amount
of storage allocated on the stack. Items stored in here must be
copyable with a memcpy.
*/
template <typename T, size_t StaticSize = 32>
class smallvec {
public:
	T* Data;
	// We declare Static space as just a bunch of bytes, so that we have identical constructor/destructor
	// behaviour whether we're dealing with static or dynamic space.
	uint8_t Static[StaticSize * sizeof(T)];
	size_t  Len = 0;
	size_t  Cap = StaticSize;

	smallvec() {
		Data = (T*) Static;
	}

	~smallvec() {
		for (size_t i = 0; i < Len; i++)
			Data[i].T::~T();

		if (Data != (T*) Static)
			free(Data);
	}

	void push(const T& t) {
		growfor(Len + 1);
		new (&Data[Len]) T(t);
		Len++;
	}

	void push_back(const T& t) {
		push(t);
	}

	void pop() {
		Len--;
		Data[Len].T::~T();
	}

	void pop_back() {
		pop();
	}

	size_t size() const {
		return Len;
	}

	void resize(size_t size) {
		if (Len == size)
			return;
		growfor(size);
		for (size_t i = Len; i < size; i++)
			new (&Data[i]) T();
		Len = size;
	}

	T&       operator[](size_t i) { return Data[i]; }
	const T& operator[](size_t i) const { return Data[i]; }
	class iterator {
	private:
		smallvec* vec;
		size_t    pos;

	public:
		iterator(smallvec* _vec, size_t _pos) : vec(_vec), pos(_pos) {}
		bool      operator!=(const iterator& b) const { return pos != b.pos; }
		T&        operator*() const { return vec->Data[pos]; }
		iterator& operator++() {
			pos++;
			return *this;
		}
	};
	friend class iterator;

	class const_iterator {
	private:
		const smallvec* vec;
		size_t          pos;

	public:
		const_iterator(const smallvec* _vec, size_t _pos) : vec(_vec), pos(_pos) {}
		bool            operator!=(const const_iterator& b) const { return pos != b.pos; }
		const T&        operator*() const { return vec->Data[pos]; }
		const_iterator& operator++() {
			pos++;
			return *this;
		}
	};
	friend class const_iterator;

	iterator       begin() { return iterator(this, 0); }
	iterator       end() { return iterator(this, Len); }
	const_iterator begin() const { return const_iterator(this, 0); }
	const_iterator end() const { return const_iterator(this, Len); }

private:
	void growfor(size_t total) {
		if (total <= Cap)
			return;
		size_t newCap = Cap;
		while (newCap < total)
			newCap *= 2;
		T* newData = (T*) imqs_malloc_or_die(sizeof(T) * newCap);
		for (size_t i = 0; i < Len; i++) {
			// Use empty constructor and swap, which will avoid reallocs if the type has an
			// appropriate swap function defined for it.
			new (&newData[i]) T();
			std::swap(newData[i], Data[i]);
		}
		if (Data != (T*) Static)
			free(Data);
		Data = newData;
		Cap  = newCap;
	}
};
} // namespace imqs
