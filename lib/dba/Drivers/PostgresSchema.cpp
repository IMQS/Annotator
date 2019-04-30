#include "pch.h"
#include "PostgresSchema.h"
#include "Postgres.h"
#include "../Conn.h"
#include "../Rows.h"
#include "../Tx.h"
#include "../Schema/DB.h"
#include "../CrudOps.h"

namespace imqs {
namespace dba {

static const char* StrValOrEmpty(const Attrib& a) {
	if (a.IsText())
		return a.Value.Text.Data;
	return "";
}

Error PostgresSchemaReader::ReadSchema(uint32_t readFlags, Executor* ex, std::string tableSpace, schema::DB& db, const std::vector<std::string>* restrictTables) {
	if (tableSpace == "")
		tableSpace = PostgresDriver::DefaultTableSpace;

	// If we don't exit early in this case, then we end up producing invalid SQL queries
	if (restrictTables && restrictTables->size() == 0)
		return Error();

	// We need to read Fields before we can read Indexes, because the indexes rely on the
	// attribute number being populated for each field.
	if (!!(readFlags & ReadFlagIndexes) && !(readFlags & ReadFlagFields)) {
		// how do we emit a warning here?
		readFlags |= ReadFlagFields;
	}

	// Read tables, and their OIDs.
	ohash::map<std::string, int32_t> tableToOid;
	ohash::map<int32_t, std::string> oidToTable;

	ohash::map<uint64_t, uint32_t> fieldNumToIndex; // (table_oid << 32) | (field_attnum) => (field index inside schema::Table::Fields, 1-based)

	SqlStr s = ex->Sql();
	s.Fmt(
	    "SELECT c.oid, c.relname, c.relkind FROM pg_class c, pg_namespace n WHERE"
	    "(c.relkind = 'r' OR c.relkind = 'v' OR c.relkind = 'm') AND "
	    "(c.relnamespace = n.oid) AND "
	    "(n.nspname = %q)",
	    tableSpace);

	if (restrictTables != nullptr) {
		s += " AND (c.relname in (";
		for (const auto& t : *restrictTables) {
			s.Squote(t);
			s += ",";
		}
		if (restrictTables->size() != 0)
			s.Chop();
		s += "))";
	}
	Rows rows = ex->Query(s);
	for (auto row : rows) {
		const auto& oid  = row[0];
		const auto& name = row[1];
		const auto& kind = row[2];
		if (!(oid.IsNumeric() && name.IsText() && kind.IsText()))
			continue;

		auto nameStr    = name.ToString();
		auto isInternal = false;
		if (nameStr == "spatial_ref_sys" ||
		    nameStr == "geometry_columns" ||
		    nameStr == "geography_columns" ||
		    nameStr == "raster_columns" ||
		    nameStr == "raster_overviews") {
			isInternal = true;
		}
		if (tableSpace != PostgresDriver::DefaultTableSpace)
			nameStr = tableSpace + "." + nameStr;

		auto kindStr = kind.ToString();
		auto tab     = db.TableByName(nameStr, true);
		if (isInternal)
			tab->Flags |= schema::TableFlags::Internal;
		if (kindStr == "v")
			tab->Flags |= schema::TableFlags::View;
		else if (kindStr == "m")
			tab->Flags |= schema::TableFlags::MaterializedView;

		tableToOid[nameStr]       = oid.ToInt32();
		oidToTable[oid.ToInt32()] = nameStr;
	}
	if (!rows.OK())
		return rows.Err();

	// exit early if there are no tables, which will happen if restrictTables has only non-existent tables inside it.
	if (tableToOid.size() == 0)
		return Error();

	// Read fields
	if (!!(readFlags & ReadFlagFields)) {
		s.Clear();
		s +=
		    "SELECT a.attrelid, a.attnum, a.attname, t.typname, a.atttypmod, a.attnotnull, d.adsrc"
		    " FROM pg_attribute a"
		    " LEFT OUTER JOIN pg_type t ON a.atttypid = t.oid"
		    " LEFT OUTER JOIN pg_attrdef d ON a.attrelid = d.adrelid AND a.attnum = d.adnum"
		    " WHERE a.attisdropped = FALSE AND a.attnum >= 1 AND a.attrelid IN (";
		for (auto& it : tableToOid)
			s.Fmt("%v,", it.second);
		if (tableToOid.size() != 0)
			s.Chop();
		s += ") ORDER BY a.attrelid, a.attnum";

		rows                        = std::move(ex->Query(s));
		int32_t        lastTableOid = -1;
		schema::Table* table        = nullptr;
		for (auto row : rows) {
			int32_t     tabOid  = row[0].ToInt32();
			int32_t     atnum   = row[1].ToInt32();
			const auto& cname   = row[2];
			const auto& dtype   = row[3];
			int32_t     typmod  = row[4].ToInt32();
			bool        notnull = row[5].ToBool();
			const auto& defval  = row[6];
			if (tabOid != lastTableOid || table == nullptr) {
				table = db.TableByName(oidToTable.get(tabOid).c_str(), false);
				if (table == nullptr)
					continue;
				lastTableOid = tabOid;
				table->Fields.clear();
			}
			table->Fields.push_back(schema::Field());
			schema::Field& f = table->Fields.back();
			DecodeField(f, cname, dtype, typmod, notnull, defval);
			if (f.Type == Type::Null)
				table->Fields.pop_back();
			else
				fieldNumToIndex.insert(((uint64_t) tabOid << 32) | atnum, (uint32_t) table->Fields.size()); // note that map 'value' is field index + 1
		}
		if (!rows.OK())
			return rows.Err();

		// Geometry fields
		std::vector<schema::Table*> geomTables;
		s.Clear();
		s += "SELECT f_table_name, f_geometry_column, coord_dimension, srid, type FROM geometry_columns";
		s.Fmt(" WHERE f_table_schema = %q", tableSpace);
		if (restrictTables) {
			s += " AND f_table_name IN (";
			for (const auto& t : *restrictTables) {
				s.Squote(t);
				s += ",";
			}
			if (restrictTables->size() != 0)
				s.Chop();
			s += ")";
		}
		rows = ex->Query(s);
		for (auto row : rows) {
			const auto& tabName   = row[0];
			const auto& fieldName = row[1];
			const auto& dims      = row[2];
			const auto& srid      = row[3];
			const auto& type      = row[4];
			auto        tab       = db.TableByName(StrValOrEmpty(tabName));
			if (!tab)
				continue;
			auto field = tab->FieldByName(StrValOrEmpty(fieldName));
			if (!field)
				continue;
			field->SRID = srid.ToInt32();
			DecodeGeomType(*field, dims, type);
			geomTables.push_back(tab);
		}
		if (!rows.OK())
			return rows.Err();

		// Map from DB SRID to projwrap SRID, which are identical when the SRID is an EPSG code,
		// but different for custom coordinate systems.
		// This might introduce a problem when trying to insert data into this table. I'm hoping
		// we can just leave the SRID unspecified for that case.
		ohash::map<int, int> sridMap;

		for (auto tab : geomTables) {
			for (auto& field : tab->Fields) {
				if (!(field.IsTypeGeom() && field.SRID != 0))
					continue;
				int dbSRID = field.SRID;
				if (!sridMap.contains(dbSRID)) {
					if (projwrap::FindEPSG(dbSRID)) {
						// For EPSG codes, we maintain the SRID
						sridMap.insert(dbSRID, dbSRID);
						continue;
					}
					// For non-EPSG codes, we get a unique negative number from projwrap, which is valid for the duration of the program
					std::string proj;
					auto        err = CrudOps::Query(ex, "SELECT proj4text FROM spatial_ref_sys WHERE srid = $1", {dbSRID}, proj);
					// Not sure this should be a fatal error, but where else do we report it?
					if (!err.OK())
						return Error::Fmt("Coordinate system %v, of table %v, not found in database (error %v)", dbSRID, tab->GetName(), err.Message());
					sridMap.insert(dbSRID, projwrap::RegisterCustomSRID(proj.c_str()));
				}
				field.SRID = sridMap.get(dbSRID);
			}
		}
	} // fields

	// Read indexes
	if (!!(readFlags & ReadFlagIndexes)) {
		// read index info
		s.Clear();
		s += "SELECT i.indrelid, c.relname, i.indnatts, i.indkey, i.indisprimary, i.indisunique FROM pg_index i, pg_class c WHERE c.oid = i.indexrelid AND i.indrelid IN (";
		for (auto& it : tableToOid)
			s.Fmt("%v,", it.second);
		if (tableToOid.size() != 0)
			s.Chop();
		s += ") ORDER BY i.indrelid";
		rows                      = ex->Query(s);
		schema::Table* tab        = nullptr;
		int32_t        lastTabOid = -1;
		for (auto row : rows) {
			const auto& tabOid  = row[0];
			const auto& idxName = row[1];
			const auto& nAtt    = row[2];
			const auto& fields  = row[3]; // Type int2vector
			const auto& prim    = row[4];
			const auto& unique  = row[5];
			if (tab == nullptr || tabOid.ToInt32() != lastTabOid) {
				tab = db.TableByName(oidToTable.get(tabOid.ToInt32()));
				if (tab == nullptr)
					continue;
				lastTabOid = tabOid.ToInt32();
				tab->Indexes.clear();
			}
			tab->Indexes.push_back(schema::Index());
			schema::Index& idx = tab->Indexes.back();
			idx.Name           = idxName.ToString();
			idx.IsPrimary      = prim.ToBool();
			idx.IsUnique       = unique.ToBool();
			auto att           = (const int32_t*) fields.Value.Bin.Data;
			for (int i = 0; i < nAtt.ToInt32(); i++) {
				int fieldIndex = fieldNumToIndex.get(((uint64_t) lastTabOid << 32) | att[i]);
				if (fieldIndex != 0)
					idx.Fields.push_back(tab->Fields[fieldIndex - 1].Name);
			}
		}
		if (!rows.OK())
			return rows.Err();
	}

	return Error();
}

/*
drop table if exists one;
create table one (id bigserial primary key, geom geometry(MultiPointM, 4326));

SELECT c.oid, c.relname, c.relkind FROM pg_class c, pg_namespace n WHERE(c.relkind = 'r' OR c.relkind = 'v' OR c.relkind = 'm') AND (c.relnamespace = n.oid) AND (n.nspname = 'public') AND (c.relname = 'one');

SELECT a.attrelid, a.attnum, a.attname, t.typname, a.atttypmod, a.attnotnull
	    FROM pg_attribute a
	     LEFT OUTER JOIN pg_type t ON a.atttypid = t.oid
	     WHERE a.attisdropped = FALSE AND a.attnum >= 1 AND a.attrelid = 1860855 order by a.attrelid
*/

void PostgresSchemaReader::DecodeField(schema::Field& f, const Attrib& name, const Attrib& datatype, int typmod, bool notNull, const Attrib& defval) {
	f.Type = Type::Null;

	char dtype[100];
	if (!datatype.IsText() || datatype.Value.Text.Size > sizeof(dtype) - 1)
		return;
	if (!name.IsText())
		return;

	modp_toupper_copy(dtype, datatype.Value.Text.Data, datatype.Value.Text.Size);

	bool isVarchar = false;
	f.Name         = name.Value.Text.Data;
	// clang-format off
	if (strcmp(dtype, "BIGINT") == 0)                              f.Type = Type::Int64;
	else if (strcmp(dtype, "INT8") == 0)                           f.Type = Type::Int64;
	else if (strcmp(dtype, "INT4") == 0)                           f.Type = Type::Int32;
	else if (strcmp(dtype, "INT2") == 0)                           f.Type = Type::Int16;
	else if (strcmp(dtype, "SMALLINT") == 0)                       f.Type = Type::Int32;
	else if (strcmp(dtype, "INTEGER") == 0)                        f.Type = Type::Int32;
	else if (strcmp(dtype, "CHARACTER VARYING") == 0)              f.Type = Type::Text;
	else if (strcmp(dtype, "CHAR") == 0)                           f.Type = Type::Text;
	else if (strcmp(dtype, "VARCHAR") == 0)                      { f.Type = Type::Text; isVarchar = true; }
	else if (strcmp(dtype, "TEXT") == 0)                           f.Type = Type::Text;
	else if (strcmp(dtype, "FLOAT4") == 0)                         f.Type = Type::Float;
	else if (strcmp(dtype, "FLOAT8") == 0)                         f.Type = Type::Double;
	else if (strcmp(dtype, "DOUBLE PRECISION") == 0)               f.Type = Type::Double;
	else if (strcmp(dtype, "REAL") == 0)                           f.Type = Type::Float;
	else if (strcmp(dtype, "BOOL") == 0)                           f.Type = Type::Bool;
	else if (strcmp(dtype, "BOOLEAN") == 0)                        f.Type = Type::Bool;
	else if (strcmp(dtype, "BYTEA") == 0)                          f.Type = Type::Bin;
	else if (strcmp(dtype, "UUID") == 0)                           f.Type = Type::Guid;
	else if (strcmp(dtype, "TIMESTAMP WITHOUT TIME ZONE") == 0)    f.Type = Type::Date;
	else if (strcmp(dtype, "TIMESTAMPTZ") == 0)                    f.Type = Type::Date;
	else if (strcmp(dtype, "TIMESTAMP") == 0)                      f.Type = Type::Date;
	else if (strcmp(dtype, "DATE") == 0)                           f.Type = Type::Date;
	else if (strcmp(dtype, "TIME WITHOUT TIME ZONE") == 0)         f.Type = Type::Time;
	else if (strcmp(dtype, "TIME") == 0)                           f.Type = Type::Time;
	else if (strcmp(dtype, "USER-DEFINED") == 0)                   f.Type = Type::Null;    // PostGIS geometry (from information_schema)
	else if (strcmp(dtype, "GEOMETRY") == 0)                       f.Type = Type::GeomAny; // PostGIS geometry (from pg_attribute)
	else if (strcmp(dtype, "NUMERIC") == 0)                        f.Type = Type::Null;
	else if (strcmp(dtype, "JSONB") == 0)                          f.Type = Type::JSONB;
	else
	{
		// this line left here for breakpoint settability
		f.Type = Type::Null;
	}
	// clang-format on

	if (notNull)
		f.Flags |= TypeFlags::NotNull;

	if (f.Type == Type::Text && typmod != -1) {
		if (isVarchar)
			f.Width = typmod - 4;
		else
			f.Width = typmod;
	}

	// typical value is nextval('tab0_id_seq'::regclass), or nextval('"tab0_id_seq"'::regclass)
	if (defval.IsText() && strstr(defval.RawString(), "nextval"))
		f.Flags |= TypeFlags::AutoIncrement;
}

void PostgresSchemaReader::DecodeGeomType(schema::Field& f, const Attrib& dims, const Attrib& type) {
	if (!(type.IsText() && dims.IsNumeric()))
		return;

	//                dims
	//    MULTIPOINT  2
	// Z  MULTIPOINT  3
	// M  MULTIPOINTM 3
	// ZM MULTIPOINT  4

	const char* t    = type.Value.Text.Data;
	size_t      tLen = (size_t) type.Value.Text.Size;
	if (t[tLen - 1] == 'M') {
		f.Flags |= TypeFlags::GeomHasM;
	} else if (dims.ToInt32() == 4) {
		f.Flags |= TypeFlags::GeomHasZ | TypeFlags::GeomHasM;
	} else if (dims.ToInt32() == 3) {
		f.Flags |= TypeFlags::GeomHasZ;
	}

	// clang-format off
	if (strstr(t, "MULTIPOINT"))		      f.Type = Type::GeomMultiPoint;
	else if (strstr(t, "MULTILINESTRING"))    f.Type = Type::GeomPolyline;
	else if (strstr(t, "MULTIPOLYGON"))		  f.Type = Type::GeomPolygon;
	else if (strstr(t, "POINT"))		      f.Type = Type::GeomPoint;
	else if (strstr(t, "LINESTRING"))		  f.Type = Type::GeomPolyline;
	else if (strstr(t, "POLYGON"))		      f.Type = Type::GeomPolygon;
	// clang-format on
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Error PostgresSchemaWriter::DropTable(Executor* ex, std::string tableSpace, const std::string& table) {
	if (tableSpace == "")
		tableSpace = PostgresDriver::DefaultTableSpace;

	auto s = ex->Sql();
	s.Fmt("DROP TABLE %Q.%Q", tableSpace, table);
	return ex->Exec(s);
}

Error PostgresSchemaWriter::CreateTable(Executor* ex, std::string tableSpace, const schema::Table& table) {
	auto s = ex->Sql();
	if (tableSpace == "")
		tableSpace = PostgresDriver::DefaultTableSpace;

	if (table.IsView() || table.IsMaterializedView())
		return Error("Views are not implemented in PostgresSchemaWriter");

	if (table.IsTemp())
		s.Fmt("CREATE TEMP TABLE %Q (", table.GetName());
	else
		s.Fmt("CREATE TABLE %Q.%Q (", tableSpace, table.GetName());

	CreateTable_Fields(s, table);
	s += ")";

	auto err = ex->Exec(s);
	if (!err.OK())
		return err;

	err = CreateTable_Indexes(ex, tableSpace, table);
	if (!err.OK())
		return err;

	return Error();
}

Error PostgresSchemaWriter::CreateIndex(Executor* ex, std::string tableSpace, const std::string& table, const schema::Index& idx) {
	auto s = ex->Sql();
	if (tableSpace == "")
		tableSpace = PostgresDriver::DefaultTableSpace;

	s.Fmt("CREATE %v INDEX ON %Q.%Q %v (", idx.IsUnique ? "UNIQUE" : "", tableSpace, table, idx.IsSpatial ? "USING GIST" : "");
	for (const auto& f : idx.Fields) {
		s.Identifier(f, true);
		s += ",";
	}
	s.Chop();
	s += ")";

	return ex->Exec(s);
}

Error PostgresSchemaWriter::AddField(Executor* ex, std::string tableSpace, const std::string& table, const schema::Field& field) {
	auto s = ex->Sql();
	if (tableSpace == "")
		tableSpace = PostgresDriver::DefaultTableSpace;

	s.Fmt("ALTER TABLE %Q ADD COLUMN %Q ", table, field.Name);
	s.FormatType(field.Type, field.IsTypeGeom() ? field.SRID : field.Width, field.Flags);
	if (field.NotNull())
		s += " NOT NULL";

	return ex->Exec(s);
}

Error PostgresSchemaWriter::AlterField(Executor* ex, std::string tableSpace, const std::string& table, const schema::Field& srcField, const schema::Field& dstField) {
	auto s = ex->Sql();
	if (tableSpace == "")
		tableSpace = PostgresDriver::DefaultTableSpace;

	s.Fmt("ALTER TABLE %Q ALTER COLUMN %Q TYPE ", table, dstField.Name);
	s.FormatType(srcField.Type, srcField.IsTypeGeom() ? srcField.SRID : srcField.Width, srcField.Flags);
	if (srcField.NotNull())
		s += " NOT NULL";

	return ex->Exec(s);
}

Error PostgresSchemaWriter::DropField(Executor* ex, std::string tableSpace, const std::string& table, const std::string& field) {
	auto s = ex->Sql();
	if (tableSpace == "")
		tableSpace = PostgresDriver::DefaultTableSpace;

	s.Fmt("ALTER TABLE %Q DROP COLUMN %Q ", table, field);

	return ex->Exec(s);
}

int PostgresSchemaWriter::DefaultFieldWidth(Type type) {
	return 0;
}

} // namespace dba
} // namespace imqs
