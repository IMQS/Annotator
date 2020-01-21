#include "pch.h"
#include "Copy.h"
#include "../FlatFiles/FlatFile.h"
#include "../Executor.h"
#include "../Conn.h"
#include "../Drivers/SchemaWriter.h"
#include "../Tx.h"
#include "../Containers/AttribList.h"
#include "../Stmt.h"
#include "../Rows.h"
#include "../CrudOps.h"
#include "../Geom.h"

using namespace std;

namespace imqs {
namespace dba {
namespace etl {

IMQS_DBA_API Error Copy(FlatFile* src, const CopyParams& params, Executor* dstExec, std::string dstTable) {
	auto           fields = src->Fields();
	vector<string> fieldNames;
	for (const auto& f : fields) {
		auto dstName = params.FieldMap.get(f.Name);
		if (dstName == "")
			dstName = f.Name;
		fieldNames.push_back(dstName);
	}

	Error      err;
	size_t     nrec = src->RecordCount();
	AttribList values;
	size_t     lastFlush          = -1;
	size_t     maxRecordsPerBatch = 5000;
	for (size_t r = 0; r < nrec && err.OK(); r++) {
		for (size_t fi = 0; fi < fields.size(); fi++) {
			Attrib* v = values.Add();
			err |= src->Read(fi, r, *v, &values);
			if (v->IsGeom() && params.OverrideNullSRID != 0 && v->Value.Geom.Head->SRID == 0)
				v->Value.Geom.Head->SRID = params.OverrideNullSRID;
		}
		if (!err.OK())
			break;
		if (r - lastFlush > maxRecordsPerBatch) {
			err |= CrudOps::Insert(dstExec, dstTable, r - lastFlush, fieldNames, values.ValuesPtr());
			values.Reset();
			lastFlush = r;
		}
	}
	if (err.OK())
		err |= CrudOps::Insert(dstExec, dstTable, nrec - lastFlush - 1, fieldNames, values.ValuesPtr());

	return err;
}

IMQS_DBA_API Error CopyTable(Executor* srcExec, std::string srcTable, const CopyParams& params, Executor* dstExec, std::string dstTable) {
	vector<string> dstFields;
	vector<Type>   dstTypes;
	auto           s = srcExec->Sql();
	s += "SELECT ";
	for (auto p : params.FieldMap) {
		s.Fmt("%Q,", p.first);
		dstFields.push_back(p.second);

		if (params.FieldMap.contains(p.first))
			dstTypes.push_back(params.FieldTypes.get(p.first));
		else
			dstTypes.push_back(Type::Null);
	}
	s.Chop();
	s.Fmt(" FROM %Q ", srcTable);
	auto err = s.BakeBuiltinFuncs();
	if (!err.OK())
		return err;

	AttribList buf;
	for (int64_t rec = 0; true; rec += params.ReadChunkSize) {
		buf.Reset();
		auto limited = s;
		limited.Dialect->AddLimit(limited, {}, params.ReadChunkSize, rec, {});
		auto   rows = srcExec->Query(limited);
		size_t nrec = 0;
		for (auto row : rows) {
			for (size_t i = 0; i < dstFields.size(); i++) {
				auto dst = buf.Add();
				row[i].CopyTo(dstTypes[i], *dst, &buf);
				if (dst->IsGeom() && dst->Value.Geom.Head->SRID == 0)
					dst->Value.Geom.Head->SRID = params.OverrideNullSRID;
			}
			nrec++;
		}
		if (nrec == 0)
			break;
		err = CrudOps::Insert(dstExec, dstTable, nrec, dstFields, buf.ValuesPtr());
		if (!err.OK())
			return Error::Fmt("Error inserting records into %v, from batch [%v .. %v]", dstTable, rec, rec + params.ReadChunkSize);
		if (nrec < params.ReadChunkSize)
			break;
	}
	return Error();
}

IMQS_DBA_API Error CreateTableFromFlatFile(FlatFile* src, Conn* dst, Tx* tx, std::string dstTable, CreateTableFromFlatFileFlags flags) {
	auto writer = dst->SchemaWriter();
	if (!writer)
		return Error("Driver doesn't support SchemaWriter");

	Executor* ex = dst;
	if (tx)
		ex = tx;

	auto           fields = src->Fields();
	vector<string> pkeys;

	if (flags & CreateTableFromFlatFileFlags::AddRowId) {
		schema::Field pkey("rowid", dba::Type::Int64, 0, TypeFlags::AutoIncrement);
		fields.insert(fields.begin(), pkey);
		pkeys.push_back(pkey.Name);
	}
	if (flags & CreateTableFromFlatFileFlags::AssumeLatLonDegreesWGS84) {
		for (auto& f : fields) {
			if (f.IsTypeGeom() && f.SRID == 0)
				f.SRID = geom::SRID_WGS84LatLonDegrees;
		}
	}
	auto err = writer->CreateTable(ex, dstTable, fields.size(), &fields[0], pkeys);
	if (!err.OK())
		return err;

	if (flags & CreateTableFromFlatFileFlags::AddSpatialIndex) {
		for (auto& f : fields) {
			if (f.IsTypeGeom()) {
				schema::Index idx;
				idx.Fields.push_back(f.Name);
				idx.IsSpatial = true;
				err |= writer->CreateIndex(ex, dstTable, idx);
			}
		}
	}

	return err;
}
} // namespace etl
} // namespace dba
} // namespace imqs
