#include "pch.h"
#include "Rows.h"
#include "Drivers/Driver.h"

using namespace std;

namespace imqs {
namespace dba {

const Attrib& Row::operator[](size_t col) const {
	return Iter->Val(col);
}

int64_t Row::Num() const {
	return Iter->RowNum;
}

Error Row::ScanVarArgPack(size_t num_args, varargs::OutArg* pack_array) {
	if (num_args != Iter->Cols.size())
		return Error::Fmt("Row.Scan with %v arguments, but query produces %v columns", num_args, Iter->Cols.size());

	for (size_t i = 0; i < num_args; i++) {
		if (!Iter->Val(i).AssignTo(pack_array[i]).OK())
			return Error::Fmt("Column %v (%v) of type %v is not assignable to argument of type %v", i, Iter->Cols[i].Name, dba::FieldTypeToString(Iter->Cols[i].Type), dba::FieldTypeToString(pack_array[i].Type));
	}

	return Error();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

RowIterator::RowIterator(Rows* rows, int64_t row) {
	// 0 = begin
	// -1 = end
	IMQS_ASSERT(row == 0 || row == -1);

	_Rows  = rows;
	RowNum = row;
	if (RowNum == 0) {
		if (_Rows->DeadWithError.OK()) {
			auto err = _Rows->DRows->Columns(Cols);
			if (!err.OK()) {
				SetDead(err);
			} else {
				Values = new Attrib[Cols.size()];
			}
			// Set RowNum to -1 to start the iteration
			RowNum = -1;
			Next();
		} else {
			// Setting RowNum to -1 makes us equal to end(), so we don't even start iteration
			RowNum = -1;
		}
	}
}

RowIterator::~RowIterator() {
	delete[] Values;
}

const Attrib& RowIterator::Val(size_t col) const {
	IMQS_ASSERT((size_t) col < Cols.size());
	return Values[col];
}

Error RowIterator::Next() {
	if (!_Rows) {
		// Iterate has already finished, possibly with an error
		return ErrEOF;
	}
	Error err = _Rows->DRows->NextRow();
	if (!err.OK()) {
		// Free our DB connection. This is important for typical error-free use cases, where you iterate
		// the result of a query, and then the 'rows' object is still in scope. If you then execute
		// another query in that same scope, dba needs a free connection object. If we didn't release our
		// DB connection here, then dba would need to open another one.
		// Note that Reset() asserts on DeadWithError.OK(), so we must call Reset() before calling SetDead()
		_Rows->Reset(nullptr, nullptr);

		// Set RowNum to -1 so that we become equal to Rows::end()
		if (err == ErrEOF)
			RowNum = -1;
		else
			SetDead(err);
		return err;
	}
	RowNum++;
	Alloc.Reset();
	for (size_t col = 0; col < Cols.size(); col++) {
		err = _Rows->DRows->Get(col, Values[col], &Alloc);
		if (!err.OK()) {
			SetDead(err);
			return err;
		}
	}
	return Error();
}

void RowIterator::SetDead(Error err) {
	// Set RowNum to -1 so that we become equal to Rows::end()
	RowNum               = -1;
	_Rows->DeadWithError = err;
}

RowIterator& RowIterator::operator++() {
	Next();
	return *this;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

Rows::Rows() {
}

Rows::Rows(Rows&& r) {
	std::swap(OwnStmt, r.OwnStmt);
	std::swap(DRows, r.DRows);
	std::swap(DeadWithError, r.DeadWithError);
}

Rows&& Rows::operator=(Rows&& r) {
	if (this != &r) {
		std::swap(OwnStmt, r.OwnStmt);
		std::swap(DRows, r.DRows);
		std::swap(DeadWithError, r.DeadWithError);
	}
	return std::move(*this);
}

Rows::~Rows() {
	delete DRows;
	delete OwnStmt;
}

void Rows::Reset(DriverRows* drows, DriverStmt* ownStmt) {
	delete DRows;
	delete OwnStmt;
	DRows   = drows;
	OwnStmt = ownStmt;
	IMQS_ASSERT(DeadWithError.OK());
}

Error Rows::ReadAll(ResultSink* sink) {
	if (!OK())
		return Err();
	std::vector<ColumnInfo> cols;
	DRows->Columns(cols);

	vector<Type> types;
	for (const auto& c : cols)
		types.push_back(c.Type);

	sink->Initialize(types.size(), types.size() ? &types[0] : nullptr);

	int64_t row = 0;
	Error   rowErr;
	for (rowErr = DRows->NextRow(); rowErr.OK(); rowErr = DRows->NextRow(), row++) {
		for (size_t col = 0; col < cols.size(); col++) {
			Attrib* val     = sink->AllocAttrib((int) col, row);
			Error   errItem = DRows->Get((int) col, *val, sink);
			if (!errItem.OK())
				return errItem;
		}
	}
	if (rowErr != ErrEOF)
		return rowErr;

	return Error();
}

std::vector<ColumnInfo> Rows::GetColumns() {
	// DRows can be null if the query failed
	std::vector<ColumnInfo> cols;
	if (DRows)
		DRows->Columns(cols);
	return cols;
}

size_t Rows::ColumnCount() {
	// DRows can be null if the query failed
	if (!DRows)
		return 0;
	return DRows->ColumnCount();
}

RowIterator Rows::begin() {
	return RowIterator(this, 0);
}

RowIterator Rows::end() {
	return RowIterator(this, -1);
}
} // namespace dba
} // namespace imqs