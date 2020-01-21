#include "pch.h"
#include "Postgres.h"
#include "PostgresSchema.h"
#include "../Attrib.h"
#include "../AttribGeom.h"

using namespace tsf;

namespace imqs {
namespace dba {

static StaticError ErrExecEmptyQuery("Postgres exec error: Empty query");
static StaticError ErrExecCommandOK("Command OK");
static StaticError ErrExecTuplesOK("Tuples OK");
static StaticError ErrExecCopyOut("Postgres exec error: Copy out");
static StaticError ErrExecCopyIn("Postgres exec error: Copy in");
static StaticError ErrExecBadResponse("Postgres exec error: Bad response");
static StaticError ErrExecNonFatal("Postgres exec error: Non fatal");
static StaticError ErrExecFatal("Postgres exec error: Fatal");

static StaticError ErrMoreThanOneResultRow("More than one result row");

static time::Time Date2000Jan1 = time::Time(2000, time::Month::January, 1, 0, 0, 0, 0);

static ohash::set<StaticString> PostgresSoftKeywords = {"CURRENT_DATE"};

const char* PostgresDriver::DefaultTableSpace = "public";

// I can't figure out where to include these from, without bringing in the entire namespace of the server bindings.
// You can find these in <postgres/include/server/catalog/pg_type.h>
// You can also find these by looking at the contents of the pg_type table.
// The OIDs that we use, and which are constant, are written here. The dynamic OIDs are cached inside PostgresConn.
enum PostgresTypes {
	BOOLOID        = 16,
	BYTEAOID       = 17,
	CHAROID        = 18,
	NAMEOID        = 19,
	INT8OID        = 20,
	INT2OID        = 21,
	INT2VECTOROID  = 22,
	INT4OID        = 23,
	OIDOID         = 26,
	TEXTOID        = 25,
	VARCHAROID     = 1043,
	FLOAT4OID      = 700,
	FLOAT8OID      = 701,
	UNKNOWNOID     = 705, // We see this when doing: SELECT 'literal' AS column. For now, we treat this as text, always, but not sure if this assumption is universally true.
	ARRAYOID       = 1022,
	DATEOID        = 1082,
	TIMETZOID      = 1266,
	TIMEOID        = 1083,
	TIMESTAMPOID   = 1114,
	TIMESTAMPTZOID = 1184,
	BITOID         = 1560,
	VARBITOID      = 1562,
	NUMERICOID     = 1700,
	UUIDOID        = 2950,
	JSONBOID       = 3802,
};

enum class PostgresValueFormat {
	Text   = 0,
	Binary = 1,
};

static Oid ToPostgresType(Type t) {
	switch (t) {
	case Type::Null:
		return (Oid) 0; // PQprepare accepts 0 values in the paramTypes array to mean "automatic". For null values, this is fine.
	case Type::Bool: return (Oid) BOOLOID;
	case Type::Int16: return (Oid) INT2OID;
	case Type::Int32: return (Oid) INT4OID;
	case Type::Int64: return (Oid) INT8OID;
	case Type::Float: return (Oid) FLOAT4OID;
	case Type::Double: return (Oid) FLOAT8OID;
	case Type::Text: return (Oid) VARCHAROID;
	case Type::Guid: return (Oid) UUIDOID;
	case Type::Date: return (Oid) TIMESTAMPOID;
	case Type::Time: return (Oid) TIMEOID;
	case Type::Bin: return (Oid) BYTEAOID;
	case Type::JSONB: return (Oid) JSONBOID;
	case Type::GeomPoint:
	case Type::GeomMultiPoint:
	case Type::GeomPolyline:
	case Type::GeomPolygon:
	case Type::GeomAny:
		return (Oid) BYTEAOID;
	default:
		break;
	}
	IMQS_DIE_MSG("Invalid field type");
	return (Oid) 0;
}

static Error ToError(ExecStatusType t) {
	switch (t) {
	case PGRES_EMPTY_QUERY: return ErrExecEmptyQuery;
	case PGRES_COMMAND_OK: return ErrExecCommandOK;
	case PGRES_TUPLES_OK: return ErrExecTuplesOK;
	case PGRES_COPY_OUT: return ErrExecCopyOut;
	case PGRES_COPY_IN: return ErrExecCopyIn;
	case PGRES_BAD_RESPONSE: return ErrExecBadResponse;
	case PGRES_NONFATAL_ERROR: return ErrExecNonFatal;
	case PGRES_FATAL_ERROR: return ErrExecFatal;
	default:
		break;
	}
	return Error("Unknown Postgres ExecStatusType");
}

static Error MakeDetailedError(const char* stub, const std::string& detail) {
	return Error(std::string(stub) + ": " + detail);
}

static std::string PreparedStatementName(int slot) {
	return fmt("ps_%v", slot);
}

inline uint16_t ReadUInt16BE(const uint8_t* p) {
	return (((uint16_t) p[0]) << 8) | (uint16_t) p[1];
}

inline uint32_t ReadUInt32BE(const uint8_t* p) {
	return (((uint32_t) p[0]) << 24) |
	       (((uint32_t) p[1]) << 16) |
	       (((uint32_t) p[2]) << 8) |
	       (((uint32_t) p[3]));
}

inline uint64_t ReadUInt64BE(const uint8_t* p) {
	return (((uint64_t) p[0]) << 56) |
	       (((uint64_t) p[1]) << 48) |
	       (((uint64_t) p[2]) << 40) |
	       (((uint64_t) p[3]) << 32) |
	       (((uint64_t) p[4]) << 24) |
	       (((uint64_t) p[5]) << 16) |
	       (((uint64_t) p[6]) << 8) |
	       (((uint64_t) p[7]));
}

inline Guid ReadGuidBE(const uint8_t* p) {
	Guid g;
	g.MS.Data1 = ReadUInt32BE(p);
	g.MS.Data2 = ReadUInt16BE(p + 4);
	g.MS.Data3 = ReadUInt16BE(p + 6);
	memcpy(g.MS.Data4, p + 8, 8);
	return g;
}

inline void WriteUInt16BE(uint16_t v, io::Buffer& buf) {
	uint8_t bytes[2] = {
	    uint8_t((v >> 8) & 0xff),
	    uint8_t(v & 0xff),
	};
	buf.Add(bytes, 2);
}

inline void MakeUInt32BE(uint32_t v, uint8_t bytes[4]) {
	bytes[0] = (uint8_t)(v >> 24);
	bytes[1] = (uint8_t)(v >> 16);
	bytes[2] = (uint8_t)(v >> 8);
	bytes[3] = (uint8_t) v;
}

inline void WriteUInt32BE(uint32_t v, io::Buffer& buf) {
	uint8_t bytes[4];
	MakeUInt32BE(v, bytes);
	buf.Add(bytes, 4);
}

inline void WriteUInt64BE(uint64_t v, io::Buffer& buf) {
	uint8_t bytes[8] = {
	    uint8_t((v >> 56) & 0xff),
	    uint8_t((v >> 48) & 0xff),
	    uint8_t((v >> 40) & 0xff),
	    uint8_t((v >> 32) & 0xff),
	    uint8_t((v >> 24) & 0xff),
	    uint8_t((v >> 16) & 0xff),
	    uint8_t((v >> 8) & 0xff),
	    uint8_t(v & 0xff),
	};
	buf.Add(bytes, 8);
}

inline void WriteGuidBE(const Guid& v, io::Buffer& buf) {
	WriteUInt32BE(v.MS.Data1, buf);
	WriteUInt16BE(v.MS.Data2, buf);
	WriteUInt16BE(v.MS.Data3, buf);
	buf.Add(v.MS.Data4, 8);
}

inline int64_t DateToMicroseconds2000UTC(time::Time v) {
	return v.SubMicro(Date2000Jan1);
}

inline time::Time DateFromMicroseconds2000UTC(int64_t micros) {
	return Date2000Jan1.PlusMicro(micros);
}

// The following code snippet was based upon pqt_get_numeric from libpqtypes

typedef int16_t NumericDigit;
struct NumericVar {
	int           ndigits; // # of digits in digits[] - can be 0!
	int           weight;  // weight of first digit
	int           sign;    // NUMERIC_POS, NUMERIC_NEG, or NUMERIC_NAN
	int           dscale;  // display scale
	NumericDigit* buf;     // start of palloc'd space for digits[]
	NumericDigit* digits;  // base-NBASE digits
};

// This function was based on pqt_get_numeric from libpqtypes
static double DecodePGNumeric(const void* bytes, int len) {
	const int16_t* s = (const int16_t*) bytes;

	NumericVar num;
	num.ndigits = ntohs(*s);
	s++; // the total number of shorts. Every short encodes a value from 0 to 9999
	num.weight = (int16_t) ntohs(*s);
	s++; // the number of shorts left of the decimal point, minus 1. So 0 = one short to the left of the decimal.
	num.sign = ntohs(*s);
	s++;
	num.dscale = ntohs(*s);
	s++; // The number of digits to the right of the decimal

	// Useful for debugging. Was used to produce the table below
	//short digits[100];
	//for ( int i = 0; i < num.ndigits; i++ )
	//	digits[i] = ntohs(s[i]);

	if (num.ndigits == 0)
		return 0;

	double val   = 0;
	double scale = 1;
	for (int i = num.weight; i >= 0; i--) {
		val = val + scale * ntohs(s[i]);
		scale *= 10000.0;
	}

	scale = 1.0 / 10000.0;
	for (int i = num.weight + 1; i < num.ndigits; i++) {
		if (i >= 0)
			val = val + scale * ntohs(s[i]);
		scale *= 1.0 / 10000.0;
	}

	if (num.sign == 0x4000)
		val = -val;

	return val;
}
/* Notes for NUMERIC:
value		ndigits	weight	dscale	digits
1.3			2		0		1		1,3000
10.3		2		0		1		10,3000
10.34		2		0		2		10,3400
10.9999		2		0		4		10,9999
10.99997	3		0		5		10,9999,7000
9999.3		2		0		1		9999,3000
10000.3		3		1		1		1,0,3000
*/

// Ripped from 'c.h'
typedef struct
{
	int32_t vl_len_;    /* these fields must match ArrayType! */
	int     ndim;       /* always 1 for int2vector */
	int32_t dataoffset; /* always 0 for int2vector */
	Oid     elemtype;
	int     dim1;
	int     lbound1;
	int16_t values[1]; /* VARIABLE LENGTH ARRAY */
} int2vector;          /* VARIABLE LENGTH STRUCT */

static void ClearResult(PGresult*& res) {
	PQclear(res);
	res = nullptr;
}

// This is used when the standard Postgres error message isn't giving us any details, in the hope
// that we'll get some usable information out of these detailed error fields.
static std::string DetailedPostgresError(const PGresult* r) {
	struct ErrField {
		int         Code;
		const char* Title;
	};

	ErrField fields[] = {
	    {PG_DIAG_MESSAGE_PRIMARY, "Primary"},
	    {PG_DIAG_MESSAGE_DETAIL, "Detail"},
	    {PG_DIAG_SEVERITY, "Severity"},
	    {PG_DIAG_SQLSTATE, "SQLState"},
	    {PG_DIAG_MESSAGE_HINT, "Hint"},
	    {PG_DIAG_STATEMENT_POSITION, "StatementPosition"},
	    {PG_DIAG_INTERNAL_POSITION, "InternalPosition"},
	    {PG_DIAG_INTERNAL_QUERY, "InternalQuery"},
	    {PG_DIAG_CONTEXT, "Context"},
	    {PG_DIAG_SOURCE_FILE, "SourceFile"},
	    {PG_DIAG_SOURCE_LINE, "SourceLine"},
	    {PG_DIAG_SOURCE_FUNCTION, "SourceFunction"},
	};
	std::string combined;
	for (size_t i = 0; i < arraysize(fields); i++) {
		const char* msg = PQresultErrorField(r, fields[i].Code);
		if (msg) {
			combined += tsf::fmt("%v: %v, ", fields[i].Title, msg);
		}
	}
	// chop trailing comma space
	if (combined.size() != 0)
		combined = combined.substr(0, combined.size() - 2);
	return combined;
}

static Error ToError(PGresult* r) {
	Error e = ToError(PQresultStatus(r));
	if (e == ErrExecCommandOK || e == ErrExecTuplesOK)
		return e;

	if (e == ErrExecNonFatal || e == ErrExecFatal) {
		std::string msg               = PQresultErrorMessage(r);
		auto        pTable            = msg.find("table ");
		auto        pRelation         = msg.find("relation ");
		auto        pColumn           = msg.find("column ");
		auto        pDoesNotExist     = msg.find(" does not exist");
		auto        pAlreadyExists    = msg.find(" already exists");
		auto        pClosedConnection = msg.find("closed the connection");

		if (msg.find("duplicate key value violates unique constraint") != -1) {
			// ERROR:  duplicate key value violates unique constraint "AssetCapType_pkey"
			// DETAIL:  Key (rowid)=(0) already exists.
			return MakeDetailedError(ErrStubKeyViolation, msg);
		} else if (msg.find("contains null values") != -1) {
			// ERROR:  column "three" contains null values
			return MakeDetailedError(ErrStubKeyViolation, msg);
		} else if (pColumn < pDoesNotExist && pColumn != -1 && pDoesNotExist != -1) {
			// ERROR:  column "version" does not exist
			// LINE 1: SELECT "version" FROM "modtrack_meta"
			//                ^
			return MakeDetailedError(ErrStubFieldNotFound, msg);
		} else if (pRelation < pDoesNotExist && pRelation != -1 && pDoesNotExist != -1) {
			// ERROR:  relation "modtrack_meta" does not exist
			// LINE 1: SELECT "version" FROM "modtrack_meta"
			//                               ^
			return MakeDetailedError(ErrStubTableNotFound, msg);
		} else if (pTable < pDoesNotExist && pTable != -1 && pDoesNotExist != -1) {
			// ERROR:  table "modtrack_meta" does not exist
			return MakeDetailedError(ErrStubTableNotFound, msg);
		} else if (msg.find("bind message supplies") != -1 && msg.find("parameters, but prepared statement") != -1) {
			return ErrInvalidNumberOfParameters;
		} else if (msg.find("current transaction is aborted, commands ignored until") != -1) {
			// ERROR:  current transaction is aborted, commands ignored until end of transaction block
			return ErrTransactionAborted;
		} else if (pRelation != -1 && pAlreadyExists != -1) {
			// ERROR:  relation \"modtrack_meta\" already exists"
			return MakeDetailedError(ErrStubRelationAlreadyExists, msg);
		} else if (pClosedConnection != -1) {
			// "server closed the connection unexpectedly\n\tThis probably means the server terminated abnormally\n\tbefore or while processing the request.\n"
			// NOTE: This case is not covered by any unit test, because I couldn't figure out how to construct such a test.
			// You could probably do it by terminating the TCP socket, but that's not trivial to figure out how to setup such a test.
			// The way I tested this manually, was to run MapServer, and get it to generate a few tiles. Then, I stopped the DB, and
			// got MapServer to try and generate some more tiles. By that stage, it had some connections open, which were now in the state.
			return ErrBadCon;
		}

		auto detail = DetailedPostgresError(r);
		if (detail != "")
			msg += "\n" + detail;

		if (e == ErrExecNonFatal)
			return Error(fmt("Postgres nonfatal error: %v", msg));

		if (e == ErrExecFatal)
			return Error(fmt("Postgres fatal error: %v", msg));
	}

	return e;
}

Error PostgresConn::Connect(const ConnDesc& desc) {
	std::string cs = fmt("host=%v user=%v password=%v dbname=%v", desc.Host, desc.Username, desc.Password, desc.Database);
	if (desc.Port != "" && desc.Port != "0")
		cs += fmt(" port=%v", desc.Port);

	if (desc.SSL)
		cs += fmt(" sslmode=require sslrootcert=%v sslcert=%v sslkey=%v", desc.SSLRootCert, desc.SSLCert, desc.SSLKey);

	HDB         = PQconnectdb(cs.c_str());
	auto status = PQstatus(HDB);
	if (status != CONNECTION_OK) {
		Close();
		return Error::Fmt("%v: %v %v", ErrStubConnectFailed, (int) status, desc.ToLogSafeString());
	}

	// NOTE: If more things like the following are added during connect, and the time
	// to run all these queries starts to become significant, then you'll want to implement
	// a cache of these values. Imagine opening up 100 connections, and every one of them
	// having a 5 second startup time.
	Attrib val;
	auto   err = QuerySingle("SELECT typelem FROM pg_type WHERE typname = '_geometry'", val);
	if (!err.OK() && err != ErrEOF)
		return err;

	if (err.OK())
		GeometryOid = val.ToInt32();

	return Error();
}

void PostgresConn::Close() {
	if (HDB != nullptr) {
		PQfinish(HDB);
		HDB = nullptr;
	}
}

PostgresConn::~PostgresConn() {
	Close();
}

Error PostgresConn::Prepare(const char* sql, size_t nParams, const Type* paramTypes, DriverStmt*& stmt) {
	auto err = Precheck();
	if (!err.OK())
		return err;

	if (strstr(sql, "COPY") == sql) {
		PostgresStmt* pstmt = new PostgresStmt(this, nParams);
		err                 = pstmt->CopyStart(sql);
		if (!err.OK()) {
			delete pstmt;
			return err;
		}
		stmt = pstmt;
		return Error();
	}

	int slot = 0;
	if (FreeSlots.size() != 0) {
		slot = FreeSlots.back();
		FreeSlots.pop_back();
	} else {
		MaxSlotNumber++;
		slot = MaxSlotNumber;
	}

	auto name = PreparedStatementName(slot);

	smallvec<Oid> pTypes;
	pTypes.resize(nParams);
	for (int i = 0; i < nParams; i++)
		pTypes[i] = ToPostgresType(paramTypes[i]);
	PGresult* res = PQprepare(HDB, name.c_str(), sql, (int) nParams, nParams ? &pTypes[0] : nullptr);
	DecrementFailAfter();
	if (res == nullptr) {
		FreeSlots.push_back(slot);
		return Error(PQerrorMessage(HDB));
	}
	err = ToError(res);
	ClearResult(res);
	if (err != ErrExecCommandOK) {
		FreeSlots.push_back(slot);
		return err;
	}

	PostgresStmt* pstmt = new PostgresStmt(this, slot, name, nParams, paramTypes);
	stmt                = pstmt;

	return Error();
}

SqlDialect* PostgresConn::Dialect() {
	return new PostgresDialect();
}

Error PostgresConn::Exec(const char* sql, size_t nParams, const Attrib** params, DriverRows*& rowsOut) {
	if (nParams != 0)
		return ErrUnsupported;

	auto err = Precheck();
	if (!err.OK())
		return err;

	// PQexecParams only supports execution of a single statement (ie no more than one semicolon).
	// PQexec supports execution of multiple statements, but the problem with PQexec, is that it
	// returns it's results in the text (not binary) format, so that's a non-starter for us.
	PGresult* res = PQexecParams(HDB, sql, 0, nullptr, nullptr, nullptr, nullptr, 1);

	// This code should look very similar to that inside PostgresStmt::Exec()
	if (res == nullptr)
		return Error(PQerrorMessage(HDB));
	err = ToError(res);

	if (err == ErrExecTuplesOK) {
		rowsOut = new PostgresRows(this, res);
		err     = Error();
	} else {
		ClearResult(res);
	}

	if (err == ErrExecCommandOK)
		return Error();

	return err;
}

Error PostgresConn::Exec(const char* sql) {
	DriverRows* drows = nullptr;
	auto        err   = Exec(sql, 0, nullptr, drows);
	if (!err.OK())
		return err;
	delete drows;
	return Error();
}

// Checks for FailAfter and DeadWithError conditions, and only returns OK if both those are clear.
Error PostgresConn::Precheck() {
	if (FailAfter == 1)
		return FailAfterWith;

	if (!DeadWithError.OK())
		return DeadWithError;

	return Error();
}

Error PostgresConn::QuerySingle(const char* sql, Attrib& result) {
	PGresult* res = PQexecParams(HDB, sql, 0, nullptr, nullptr, nullptr, nullptr, 1);
	Error     err = ToError(res);
	if (!err.OK() && err != ErrExecTuplesOK) {
		PQclear(res);
		return err;
	}

	auto rows = PostgresRows(this, res);

	if (rows.NumRows == 0)
		return ErrEOF;

	if (rows.NumRows > 1)
		return ErrMoreThanOneResultRow;

	err = rows.NextRow();
	if (!err.OK())
		return err;

	return rows.Get(0, result, nullptr);
}

void PostgresConn::DeleteRetiredPreparedStatements() {
	for (int slot : RetiredSlots) {
		SqlStr sql(&StaticDialect);
		sql.Fmt("DEALLOCATE %Q", PreparedStatementName(slot));
		auto err = Exec(sql);

		if (err.OK()) {
			FreeSlots.push_back(slot);
		} else {
			DeadWithError = Error::Fmt("Error while trying to DEALLOCATE retired prepared statement %v: %v", slot, err.Message());
		}
	}
	RetiredSlots.clear();
}

Error PostgresConn::Begin() {
	return Exec("BEGIN");
}

Error PostgresConn::Commit() {
	auto err = Exec("COMMIT");
	DeleteRetiredPreparedStatements();
	return err;
}

Error PostgresConn::Rollback() {
	auto err = Exec("ROLLBACK");
	DeleteRetiredPreparedStatements();
	return err;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

SqlDialectFlags PostgresDialect::Flags() {
	return SqlDialectFlags::MultiRowInsert |
	       SqlDialectFlags::AlterSchemaInsideTransaction |
	       SqlDialectFlags::UUID |
	       SqlDialectFlags::GeomZ |
	       SqlDialectFlags::GeomM |
	       SqlDialectFlags::SpatialIndex |
	       SqlDialectFlags::GeomSpecificFieldTypes |
	       SqlDialectFlags::Int16 |
	       SqlDialectFlags::Float |
	       SqlDialectFlags::JSONB |
	       SqlDialectFlags::NamedSchemas;
}

imqs::dba::Syntax PostgresDialect::Syntax() {
	return Syntax::ANSI;
}

void PostgresDialect::NativeHexLiteral(const char* hexLiteral, SqlStr& s) {
	s += fmt("E'\\\\x%v'", hexLiteral);
}

void PostgresDialect::NativeFunc(const sqlparser::SqlAST& ast, SqlStr& s, uint32_t& printFlags) {
	auto funcName = ast.FuncName.c_str();
	switch (hash::crc32(funcName, strlen(funcName))) {
	case "dba_ST_GeomFrom"_crc32:
		s += "ST_GeomFromEWKB"; // This must match the output produced by ST_GeomFrom
		break;
	case "dba_ST_GeomFromText"_crc32:
		s += "ST_GeomFromText";
		break;
	case "dba_ST_AsGeom"_crc32:
	case "dba_AsGUID"_crc32:
	case "dba_AsINT32"_crc32:
		break;
	case "dba_ST_Intersects"_crc32:
		s += "ST_Intersects";
		break;
	case "dba_ST_Contains"_crc32:
		s += "ST_Contains";
		break;
	case "dba_Unix_Timestamp"_crc32:
		s += "(EXTRACT(EPOCH FROM ";
		s.Identifier(ast.Params[0]->Variable);
		s += "))";
		printFlags = sqlparser::SqlAST::PrintFlags::PrintExcludeParameters;
		break;
	case "dba_ST_CoarseIntersect"_crc32:
		s += "(";
		s.Identifier(ast.Params[0]->Variable);
		// expecting X1, Y1, X2, Y2
		s.Fmt(" && ST_SetSRID('BOX3D(%v %v, ", ast.Params[1]->Value.NumberVal, ast.Params[2]->Value.NumberVal);
		s.Fmt(" %v %v)'::BOX3D, 4326)) ", ast.Params[3]->Value.NumberVal, ast.Params[4]->Value.NumberVal);
		printFlags = sqlparser::SqlAST::PrintFlags::PrintExcludeParameters;
		break;
	case "NOT"_crc32:
		if (ast.Params.size() == 2 && ast.Params[1]->IsNullValue())
			s += "IS NOT"; // IS NOT NULL
		else
			s += "NOT";
		break;
	default:
		s += funcName;
	}
}

void PostgresDialect::ST_GeomFrom(SqlStr& s, const char* insertElement) {
	s += "ST_GeomFromEWKB(";
	s += insertElement;
	s += ")";
}

bool PostgresDialect::UseThisCall(const char* funcName) {
	return false;
}

void PostgresDialect::FormatType(SqlStr& s, Type type, int width_or_srid, TypeFlags flags) {
	const char* geomSuffix = "";
	if (!!(flags & TypeFlags::GeomHasZ) && !!(flags & TypeFlags::GeomHasM))
		geomSuffix = "ZM";
	else if (!!(flags & TypeFlags::GeomHasZ))
		geomSuffix = "Z";
	else if (!!(flags & TypeFlags::GeomHasM))
		geomSuffix = "M";

	bool multi = !(flags & TypeFlags::GeomNotMulti);

	switch (type) {
	case Type::Int16:
		if (!!(flags & TypeFlags::AutoIncrement))
			s += "SMALLSERIAL";
		else
			s += "SMALLINT";
		break;
	case Type::Int32:
		if (!!(flags & TypeFlags::AutoIncrement))
			s += "SERIAL";
		else
			s += "INTEGER";
		break;
	case Type::Int64:
		if (!!(flags & TypeFlags::AutoIncrement))
			s += "BIGSERIAL";
		else
			s += "BIGINT";
		break;
	case Type::Float: s += "REAL"; break;
	case Type::Double: s += "DOUBLE PRECISION"; break;
	case Type::Text:
		if (width_or_srid == 1)
			s += "\"char\"";
		else if (width_or_srid != 0)
			s += tsf::fmt("VARCHAR(%v)", width_or_srid);
		else
			s += "VARCHAR";
		break;
	case Type::Date: s += "TIMESTAMP WITHOUT TIME ZONE"; break;
	case Type::Time: s += "TIME WITHOUT TIME ZONE"; break;
	case Type::Bin: s += "BYTEA"; break;
	case Type::JSONB: s += "JSONB"; break;
	case Type::GeomPoint: s.Fmt("geometry(Point%v, %v)", geomSuffix, width_or_srid); break;
	case Type::GeomMultiPoint: s.Fmt("geometry(MultiPoint%v, %v)", geomSuffix, width_or_srid); break;
	case Type::GeomPolyline:
		if (multi)
			s.Fmt("geometry(MultiLineString%v, %v)", geomSuffix, width_or_srid);
		else
			s.Fmt("geometry(LineString%v, %v)", geomSuffix, width_or_srid);
		break;
	case Type::GeomPolygon:
		if (multi)
			s.Fmt("geometry(MultiPolygon%v, %v)", geomSuffix, width_or_srid);
		else
			s.Fmt("geometry(Polygon%v, %v)", geomSuffix, width_or_srid);
		break;
	case Type::GeomAny: s.Fmt("geometry(Geometry%v, %v)", geomSuffix, width_or_srid); break;
	default:
		SqlDialect::FormatType(s, type, width_or_srid, flags);
		break;
	}
}

void PostgresDialect::TruncateTable(SqlStr& s, const std::string& table, bool resetSequences) {
	s.Fmt("TRUNCATE %Q", table);
	if (resetSequences)
		s.Fmt(" RESTART IDENTITY");
}

bool PostgresDialect::IsSoftKeyword(const char* name) {
	StaticString ss;
	ss.Z        = const_cast<char*>(name);
	bool isSoft = PostgresSoftKeywords.contains(ss);
	ss.Z        = nullptr;
	return isSoft;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

PostgresRows::PostgresRows(DriverConn* dcon, PGresult* res) : DriverRows(dcon), Res(res) {
	NumRows = PQntuples(res);
}

PostgresRows::~PostgresRows() {
	ClearResult(Res);
}

Error PostgresRows::NextRow() {
	if (Row + 1 == NumRows)
		return ErrEOF;
	Row++;
	return Error();
}

Error PostgresRows::Get(size_t col, Attrib& val, Allocator* alloc) {
	auto err = PgConn()->Precheck();
	if (!err.OK())
		return err;

	if (PQgetisnull(Res, (int) Row, (int) col) != 0) {
		val.SetNull();
		return Error();
	}

	int            ct   = PQftype(Res, (int) col);
	const uint8_t* pval = (const uint8_t*) PQgetvalue(Res, (int) Row, (int) col);
	int            len  = PQgetlength(Res, (int) Row, (int) col);

	PgConn()->DecrementFailAfter();

	// dynamic OIDs
	if (ct == PgConn()->GeometryOid)
		return WKB::Decode(pval, len, val, alloc);

	// static OIDs
	switch (ct) {
	case BOOLOID:
		val.SetBool(*pval != 0);
		break;
	case INT2OID:
		val.SetInt16((int16_t) ReadUInt16BE(pval));
		break;
	case OIDOID:
	case INT4OID:
		val.SetInt32((int32_t) ReadUInt32BE(pval));
		break;
	case INT8OID: val.SetInt64((int64_t) ReadUInt64BE(pval)); break;
	case FLOAT4OID: {
		uint32_t x = ReadUInt32BE(pval);
		val.SetFloat(*((float*) &x));
		break;
	}
	case FLOAT8OID: {
		uint64_t x = ReadUInt64BE(pval);
		val.SetDouble(*((double*) &x));
		break;
	}
	case NUMERICOID:
		val.SetDouble(DecodePGNumeric(pval, len));
		break;
	case DATEOID: {
		int64_t iv = ReadUInt32BE(pval);
		val.SetDate(DateFromMicroseconds2000UTC(iv * 86400 * 1000 * 1000));
		break;
	}
	case TIMESTAMPTZOID:
	case TIMESTAMPOID: {
		// TIMESTAMPTZOID, which is "with time zone", is stored internally as UTC, same as "without time zone".
		// The only difference between those two formats is the way Postgres displays them when doing a plain
		// old textual SELECT statement.
		int64_t iv = (int64_t) ReadUInt64BE(pval);
		val.SetDate(DateFromMicroseconds2000UTC(iv));
		break;
	}
	case CHAROID:
	case NAMEOID:
	case TEXTOID:
	case VARCHAROID:
	case ARRAYOID:
		val.SetText((const char*) pval, (size_t) len, alloc);
		break;
	case JSONBOID:
		if (len >= 1) {
			if (pval[0] == 1)
				val.SetJSONB((const char*) pval + 1, (size_t) len - 1, alloc);
			else
				IMQS_DIE_MSG(tsf::fmt("Unrecognized JSONB encoding (%v) in PostgresRows::Get", (int) pval[0]).c_str());
		}
		break;
	case UNKNOWNOID:
		// See comment at UNKNOWNOID definition
		val.SetText((const char*) pval, (size_t) len, alloc);
		break;
	case UUIDOID:
		val.SetGuid(ReadGuidBE(pval), alloc);
		break;
	case BYTEAOID:
		val.SetBin(pval, len, alloc);
		break;
	case INT2VECTOROID: {
		// We use this when reading the index schema information
		// The values in 'iv' are big endian.
		int2vector* iv    = (int2vector*) pval;
		int         count = ReadUInt32BE((uint8_t*) &iv->elemtype);
		val.SetBin(nullptr, count * sizeof(int32_t), alloc); // we expand everything to 4-byte integers
		int32_t* dat = (int32_t*) val.Value.Bin.Data;
		// This shit is mega-weird. I don't understand the scheme.. I just deconstructed it.
		// They have 1 value, then they skip 4 bytes, then the next value.
		// Basically you have a value at every 6 bytes.
		for (int i = 0; i < count; i++)
			dat[i] = ReadUInt16BE((uint8_t*) &iv->values[i * 3]);
		break;
	}
	default:
		IMQS_DIE_MSG(tsf::fmt("Unrecognized OID (%v) in PostgresRows::Get", ct).c_str());
	}

	return Error();
}

Error PostgresRows::Columns(std::vector<ColumnInfo>& cols) {
	int n = PQnfields(Res);
	for (int i = 0; i < n; i++) {
		ColumnInfo inf;
		inf.Name = PQfname(Res, i);
		inf.Type = FromPostgresType(PQftype(Res, i));
		cols.push_back(std::move(inf));
	}
	return Error();
}

size_t PostgresRows::ColumnCount() {
	return PQnfields(Res);
}

Type PostgresRows::FromPostgresType(Oid t) {
	if (t == PgConn()->GeometryOid)
		return Type::GeomAny;

	switch (t) {
	case BOOLOID: return Type::Bool;
	case INT2OID: return Type::Int16;
	case INT4OID: return Type::Int32;
	case INT8OID: return Type::Int64;
	case FLOAT8OID:
		return Type::Float;
	case NUMERICOID:
		return Type::Double;
	case VARCHAROID:
	case NAMEOID:
	case TEXTOID:
	case CHAROID:
		return Type::Text;
	case JSONBOID:
		return Type::JSONB;
	case UNKNOWNOID:
		// See comment at UNKNOWNOID definition
		return Type::Text;
	case UUIDOID: return Type::Guid;
	case TIMESTAMPOID: return Type::Date;
	case TIMEOID: return Type::Time;
	case BYTEAOID: return Type::Bin;
	}
	return Type::Null;
}

PostgresConn* PostgresRows::PgConn() {
	return (PostgresConn*) DCon;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

PostgresStmt::PostgresStmt(PostgresConn* dcon, int slot, const std::string& name, size_t nParams, const Type* paramTypes) : DriverStmt(dcon), Slot(slot), Name(name) {
	ParamTypes.resize(nParams);
	ParamFormats.resize(nParams);
	ParamLengths.resize(nParams);
	ParamValues.resize(nParams);

	for (int i = 0; i < nParams; i++) {
		ParamTypes[i]   = paramTypes[i];
		ParamFormats[i] = (int) PostgresValueFormat::Binary;
	}
}

PostgresStmt::PostgresStmt(PostgresConn* dcon, size_t nFieldsInCopy) : DriverStmt(dcon) {
	IsCopy         = true;
	CopyFieldCount = nFieldsInCopy;
}

PostgresStmt::~PostgresStmt() {
	if (PgConn()->DeadWithError.OK()) {
		if (PgConn()->FailAfter == 1) {
			PgConn()->DeadWithError = PgConn()->FailAfterWith;
		} else if (!IsCopy) {
			// We just assume that if the DEALLOCATE call fails, then the entire connection is dead.
			// I don't know if this is optimal, but it is conservative.
			SqlStr sql(&PgConn()->StaticDialect);
			sql.Fmt("DEALLOCATE %Q", Name);
			auto err = PgConn()->Exec(sql);

			if (err.OK()) {
				PgConn()->FreeSlots.push_back(Slot);
			} else if (err == ErrTransactionAborted) {
				// The DEALLOCATE call failed, because we're inside an aborted transaction. We see prepared statements
				// as "out of band" data, that lies outside the scope of transactions. However, Postgres will refuse
				// to execute our DEALLOCATE statement above, if we're inside a transaction that has been aborted, because
				// a previous statement produced an error.
				// In order to get our statement deallocated, we need to exit the transaction. This violates the assumptions
				// that the Tx object makes, because the Tx doesn't know that the transaction has been aborted under it's feet.
				PgConn()->RetiredSlots.push_back(Slot);
			} else {
				PgConn()->DeadWithError = Error::Fmt("Error while trying to DEALLOCATE prepared statement %v: %v", Slot, err.Message());
			}
		}
	}
}

Error PostgresStmt::Exec(size_t nParams, const Attrib** params, DriverRows*& rowsOut) {
	auto err = PgConn()->Precheck();
	if (!err.OK())
		return err;

	if (IsCopy) {
		if (IsCopyFinished)
			return Error("Copy has already been finished");
		if (nParams == 0) {
			IsCopyFinished = true;
			return CopyEnd();
		}
		if (nParams % CopyFieldCount != 0)
			return Error::Fmt("COPY needs an integer number of fields with each Exec (received %v, which is not a multiple of %v)", nParams, CopyFieldCount);
		return CopyIn(nParams / CopyFieldCount, params);
	}

	if (nParams != ParamTypes.size())
		return ErrInvalidNumberOfParameters;

	ResetParamBuffers();
	PackParams(params);
	MakeParamValuePointers();

	PGresult* res = PQexecPrepared(PgConn()->HDB, Name.c_str(), (int) nParams, nParams ? (const char* const*) &ParamValues[0] : nullptr, nParams ? &ParamLengths[0] : nullptr, nParams ? &ParamFormats[0] : nullptr, 1);

	// This code should look very similar to that inside PostgresConn::Exec()
	PgConn()->DecrementFailAfter();
	if (res == nullptr)
		return Error(PQerrorMessage(PgConn()->HDB));
	err = ToError(res);

	if (err == ErrExecTuplesOK) {
		rowsOut = new PostgresRows(PgConn(), res);
		err     = Error();
	} else {
		ClearResult(res);
	}

	if (err == ErrExecCommandOK)
		return Error();

	return err;
}

void PostgresStmt::ResetParamBuffers() {
	ParamValues.resize(0);
	ParamLengths.resize(0);
	ParamBuf.Len = 0;
}

Error PostgresStmt::PackParams(const Attrib** params) {
	uint8_t  vBool;
	uint64_t vInt64;
	Attrib   tmp;

	// ParamValues and ParamLengths have special meanings in between the time when
	// PackParams() and MakeParamValuePointers() are called.
	// See MakeParamValuePointers for those meanings.
	for (size_t i = 0; i < ParamTypes.size(); i++) {
		const Attrib* p        = params[i];
		int           len      = 0;
		intptr_t      paramPos = ParamBuf.Len;
		if (p != nullptr && !p->IsNull() && p->Type != ParamTypes[i] && !(IsTypeGeom(p->Type) && ParamTypes[i] == Type::GeomAny)) {
			ConvertedAttribBuf.Reset();
			p->CopyTo(ParamTypes[i], tmp, &ConvertedAttribBuf);
			p = &tmp;
		}

		if (p == nullptr || p->Type == Type::Null) {
			len      = -1;
			paramPos = 0;
		} else {
			switch (p->Type) {
			case Type::Bool:
				vBool = p->Value.Bool ? 1 : 0;
				ParamBuf.Add(&vBool, 1);
				len = 1;
				break;
			case Type::Int16:
				WriteUInt16BE((uint16_t) p->Value.Int16, ParamBuf);
				len = 2;
				break;
			case Type::Int32:
				WriteUInt32BE((uint32_t) p->Value.Int32, ParamBuf);
				len = 4;
				break;
			case Type::Int64:
				WriteUInt64BE((uint64_t) p->Value.Int64, ParamBuf);
				len = 8;
				break;
			case Type::Float:
				WriteUInt32BE(*((uint32_t*) &p->Value.Float), ParamBuf);
				len = 4;
				break;
			case Type::Double:
				WriteUInt64BE(*((uint64_t*) &p->Value.Double), ParamBuf);
				len = 8;
				break;
			case Type::Text:
				paramPos = (intptr_t) p->Value.Text.Data;
				len      = -(p->Value.Text.Size + 2);
				break;
			case Type::JSONB:
				// We need to prepend the JSONB output with a single '1' byte, which is the version of the transport.
				// See jsonb_recv in https://github.com/postgres/postgres/blob/master/src/backend/utils/adt/jsonb.c#L113 for details.
				// When libpq sends JSONB back to us, it doesn't include that byte.
				ParamBuf.WriteUint8(1);
				ParamBuf.Write(p->Value.Text.Data, p->Value.Text.Size);
				len = p->Value.Text.Size + 1;
				break;
			case Type::Guid:
				WriteGuidBE(*p->Value.Guid, ParamBuf);
				len = 16;
				break;
			case Type::Date:
				vInt64 = DateToMicroseconds2000UTC(p->Date());
				WriteUInt64BE((uint64_t) vInt64, ParamBuf);
				len = 8;
				break;
			//case Type::Time:
			case Type::Bin:
				if (p->Value.Bin.Size == 0) {
					// point to a bogus position. It doesn't matter where, so long as it's not NULL, otherwise Postgres accepts that as a null attribute
					paramPos = (intptr_t) 1;
				} else {
					paramPos = (intptr_t) p->Value.Bin.Data;
				}
				len = -(p->Value.Bin.Size + 2);
				break;
			case Type::GeomPoint:
			case Type::GeomMultiPoint:
			case Type::GeomPolyline:
			case Type::GeomPolygon: {
				// NOTE: This code is identical to that inside PackBinaryCopy(), and shares the same concepts. So keep these two in sync.
				auto wflags = WKB::WriterFlags::EWKB;
				// Note that omitting the SRID doesn't do much good. If the target field has a non-zero SRID, then Postgres
				// will refuse to write an incoming geometry if it's SRID doesn't match the target field.
				if (p->Value.Geom.Head->SRID != 0)
					wflags |= WKB::WriterFlags::SRID;
				// The following line is true for the way in which we always create PostGIS schema. ie.. we always
				// set the field types to the "multi" variety. We never use linestring or polygon for the field type.
				// In order to support databases that we haven't created ourselves, we'll need better knowledge here,
				// informing whether we should toggle the following flag or not.
				// See detailed explanation in main docs under "MultiPolygon / MultiLineString Issue"
				wflags |= WKB::WriterFlags::Force_Multi;
				if (p->GeomHasZ())
					wflags |= WKB::WriterFlags::Z;
				if (p->GeomHasM())
					wflags |= WKB::WriterFlags::M;
				len = (int) WKB::Encode(wflags, *p, ParamBuf);
				break;
			}
			default:
				IMQS_DIE_MSG("Unhandled parameter type");
				break;
			}
		}
		ParamValues.push_back((void*) paramPos);
		ParamLengths.push_back(len);
	}
	return Error();
}

// We initially store size_t offsets (with various special meanings) inside ParamValues.
// ParamLengths also stores some of this metadata. Here we turn them into proper pointers.
// The reason we can't just load up ParamValues with proper pointers during PackParams(),
// is that we don't know when we're busy packing, how large ParamBuf is going to end up
// being, so we might have to grow ParamBuf during the pack operation, which would invalidate
// all interior pointers into it.
void PostgresStmt::MakeParamValuePointers() {
	// We cannot allow ParamBuf.Buf to be null, because that would cause an empty
	// string or binary blob to look the same to libpq as a proper null attribute,
	// because len = 0 and ptr = zero signify a null attribute.
	// However len = 0 and ptr = nonzero signify an empty attribute.
	if (ParamValues.size() != 0 && ParamBuf.Buf == nullptr)
		ParamBuf.Ensure(1);

	for (size_t i = 0; i < ParamValues.size(); i++) {
		int      len = ParamLengths[i];
		intptr_t raw = (intptr_t) ParamValues[i];
		if (len < -1) {
			// raw pointer
			len = -(len + 2);
		} else if (len == -1) {
			// null attribute
			len            = 0;
			ParamValues[i] = nullptr;
		} else if (len >= 0) {
			// offset into ParamBuf
			ParamValues[i] = (void*) (ParamBuf.Buf + raw);
		}
		ParamLengths[i] = len;
	}
}

PostgresConn* PostgresStmt::PgConn() {
	return static_cast<PostgresConn*>(DCon);
}

Error PostgresStmt::CopyStart(const char* sql) {
	auto err = PgConn()->Exec(sql);
	if (err == ErrExecCopyIn)
		err = Error();
	if (!err.OK())
		return err;
	CopyBuffer.Len = 0;
	CopyBuffer.Add("PGCOPY\n\xff\r\n\0", 11);
	CopyBuffer.WriteUint32BE(0); // flags
	CopyBuffer.WriteUint32BE(0); // header extension area length
	return CopyInRaw(CopyBuffer.Buf, CopyBuffer.Len);
}

static void PackBinaryCopy(io::Buffer& buf, const Attrib* val) {
	uint8_t vBool;
	int64_t vInt64;
	switch (val->Type) {
	case Type::Bool:
		vBool = val->Value.Bool ? 1 : 0;
		buf.WriteUint32BE(1);
		buf.Add(&vBool, 1);
		break;
	case Type::Int16:
		buf.WriteUint32BE(2);
		buf.WriteInt16BE(val->Value.Int16);
		break;
	case Type::Int32:
		buf.WriteUint32BE(4);
		buf.WriteInt32BE(val->Value.Int32);
		break;
	case Type::Int64:
		buf.WriteUint32BE(8);
		buf.WriteInt64BE(val->Value.Int64);
		break;
	case Type::Float:
		buf.WriteUint32BE(4);
		buf.WriteInt32BE(*((uint32_t*) &val->Value.Float));
		break;
	case Type::Double:
		buf.WriteUint32BE(8);
		buf.WriteInt64BE(*((uint64_t*) &val->Value.Double));
		break;
	case Type::Text:
		buf.WriteUint32BE(val->Value.Text.Size);
		buf.Add(val->Value.Text.Data, val->Value.Text.Size);
		break;
	case Type::JSONB:
		buf.WriteUint32BE(val->Value.Text.Size + 1); // version of JSON encoding
		buf.WriteUint8(1);
		buf.Add(val->Value.Text.Data, val->Value.Text.Size);
		break;
	case Type::Bin:
		buf.WriteUint32BE(val->Value.Bin.Size);
		buf.Add(val->Value.Bin.Data, val->Value.Bin.Size);
		break;
	case Type::Guid:
		buf.WriteUint32BE(16);
		WriteGuidBE(*val->Value.Guid, buf);
		break;
	case Type::Date:
		vInt64 = DateToMicroseconds2000UTC(val->Date());
		buf.WriteUint32BE(8);
		buf.WriteInt64BE(vInt64);
		break;
	case Type::GeomPoint:
	case Type::GeomMultiPoint:
	case Type::GeomPolyline:
	case Type::GeomPolygon: {
		// NOTE: This code is identical to that inside PostgresStmt::PackParams(), and shares the same concepts. So keep these two in sync.
		auto wflags = WKB::WriterFlags::EWKB;
		if (val->Value.Geom.Head->SRID != 0)
			wflags |= WKB::WriterFlags::SRID;
		wflags |= WKB::WriterFlags::Force_Multi;
		if (val->GeomHasZ())
			wflags |= WKB::WriterFlags::Z;
		if (val->GeomHasM())
			wflags |= WKB::WriterFlags::M;
		size_t sizePos = buf.Len;
		buf.WriteUint32BE(0); // we'll come back to this
		uint32_t len = (uint32_t) WKB::Encode(wflags, *val, buf);
		uint8_t  size4[4];
		MakeUInt32BE(len, size4);
		memcpy(buf.Buf + sizePos, size4, 4);
		break;
	}
	default:
		IMQS_DIE();
	}
}

Error PostgresStmt::CopyIn(size_t nRecs, const Attrib** values) {
	CopyBuffer.Len = 0;
	for (size_t i = 0; i < nRecs; i++) {
		CopyBuffer.WriteUint16BE((uint16_t) CopyFieldCount);
		for (size_t j = 0; j < CopyFieldCount; j++, values++) {
			const Attrib* val = *values;
			if (val->IsNull()) {
				CopyBuffer.WriteInt32BE(-1);
			} else {
				PackBinaryCopy(CopyBuffer, val);
			}
		}
	}
	return CopyInRaw(CopyBuffer.Buf, CopyBuffer.Len);
}

Error PostgresStmt::CopyInRaw(const void* buf, size_t len) {
	IMQS_ASSERT(len < 0x7fffffff);
	int res = PQputCopyData(PgConn()->HDB, (const char*) buf, (int) len);
	if (res != 1)
		return Error::Fmt("PQputCopyData failed: %v", PQerrorMessage(PgConn()->HDB));
	return Error();
}

Error PostgresStmt::CopyEnd() {
	CopyBuffer.Free();
	int r = PQputCopyEnd(PgConn()->HDB, nullptr);
	if (r != 1)
		return Error::Fmt("PQputCopyEnd failed: %v", PQerrorMessage(PgConn()->HDB));
	auto res = PQgetResult(PgConn()->HDB);
	auto err = ToError(res);
	PQclear(res);
	if (err == ErrExecCommandOK)
		err = Error();
	return err;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

PostgresDriver::PostgresDriver() {
}

PostgresDriver::~PostgresDriver() {
}

Error PostgresDriver::Open(const ConnDesc& desc, DriverConn*& con) {
	auto mycon = new PostgresConn();
	auto err   = mycon->Connect(desc);
	if (!err.OK()) {
		delete mycon;
		return err;
	}
	con = mycon;
	return Error();
}

imqs::dba::SchemaReader* PostgresDriver::SchemaReader() {
	return &PGSchemaReader;
}

imqs::dba::SchemaWriter* PostgresDriver::SchemaWriter() {
	return &PGSchemaWriter;
}

SqlDialect* PostgresDriver::DefaultDialect() {
	return &StaticDialect;
}
} // namespace dba
} // namespace imqs
