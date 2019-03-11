#pragma once

#include "Attrib.h"
#include "Allocators.h"
#include "Containers/VarArgs.h"
#include "Drivers/Driver.h"

namespace imqs {
namespace dba {

class Rows;
class DriverRows;
class DriverStmt;
class RowIterator;

// Target of result attributes
// ResultSink will always be called in a row-major manner (ie rows are the outer loop).
// Rows always start at zero, as do columns.
class IMQS_DBA_API ResultSink : public Allocator {
public:
	virtual void    Initialize(size_t nCols, const Type* types) = 0; // Called at the start of iteration
	virtual void*   Alloc(size_t bytes)                         = 0; // Provide an allocator for Attrib internal storage
	virtual Attrib* AllocAttrib(size_t col, int64_t row)        = 0; // Allocate a new Attrib object
};

// A single row in an iteration over a result set
class IMQS_DBA_API Row {
public:
	Row() {}
	Row(RowIterator* iter) : Iter(iter) {}

	// Fetch the value at the given column. If col is invalid, the system panics.
	const Attrib& operator[](size_t col) const;

	// The zero-based row number in the result set
	int64_t Num() const;

	// Fetch all results from the row into a list of variables
	template <typename... Args>
	Error Scan(Args&... args);

private:
	RowIterator* Iter;

	Error ScanVarArgPack(size_t num_args, varargs::OutArg* pack_array);
};

template <typename... Args>
Error Row::Scan(Args&... args) {
	const auto      num_args = sizeof...(Args);
	varargs::OutArg pack_array[num_args + 1]; // +1 for zero args case
	varargs::PackOutArgs(pack_array, args...);
	return ScanVarArgPack(num_args, pack_array);
}

// Internal row iterator
class IMQS_DBA_API RowIterator {
public:
	friend class Rows;
	friend class Row;

	RowIterator(Rows* rows, int64_t row);
	~RowIterator();

	Row           operator*() { return Row(this); }
	RowIterator&  operator++();
	bool          operator==(const RowIterator& b) const { return RowNum == b.RowNum; }
	bool          operator!=(const RowIterator& b) const { return RowNum != b.RowNum; }
	const Attrib& Val(size_t col) const;

private:
	Rows*                   _Rows  = nullptr;
	int64_t                 RowNum = 0;
	Attrib*                 Values = nullptr;
	std::vector<ColumnInfo> Cols;
	RepeatCycleAllocator    Alloc;

	Error Next();
	void  SetDead(Error err);
};

/* Provides an iterator that fetches the resulting rows of a query.

There are two ways to use this:

Iteration
	
	for (auto row : rows) {
		Attrib val0 = row[0]; // Fetch the first column
		Attrib val1 = row[1]; // Fetch the second column

		// you can also scan the results directly into strongly types variables

		int         v0;
		std::string v1;
		row.Scan(v0, v1);     // Fetch first two columns directly into variables
	}

	// handle any error that occurred during execution of the query
	if (!rows.OK())
		return rows.Err();

Bulk Fetch

	// ResultSink* sink;
	rows.ReadAll(sink);

Bulk fetch is more efficient because it avoids copying results from
the iteration row into your own data structure. When performing a
bulk fetch, you can control the destination directly.
*/
class IMQS_DBA_API Rows {
public:
	friend class Conn;
	friend class Tx;
	friend class Stmt;
	friend class RowIterator;

	Rows();
	Rows(Rows&& r);
	Rows&& operator=(Rows&& r);
	~Rows();

	Error ReadAll(ResultSink* sink);
	Error Err() const { return DeadWithError; }
	bool  OK() const { return DeadWithError.OK(); }

	std::vector<ColumnInfo> GetColumns();
	size_t                  ColumnCount();

	RowIterator begin();
	RowIterator end();

private:
	/* OwnStmt is a prepared statement object that we own. This is used exclusively by
	the Query functions of Conn and Tx. Those functions create a prepared statement
	behind the scenes, which the user isn't aware of. We need to manage the lifecycle
	of that prepared statement, and the only reasonable way to do that, is via the
	Rows object, because when the user is done iterating over the results of his
	query, we can safely delete the prepared statement.
	*/
	DriverStmt* OwnStmt = nullptr;
	DriverRows* DRows   = nullptr;
	Error       DeadWithError;

	Rows(const Rows& r) = delete;
	Rows& operator=(const Rows& r) = delete;

	void Reset(DriverRows* drows, DriverStmt* ownStmt);
};
} // namespace dba
} // namespace imqs
