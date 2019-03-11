#pragma once
#include "Driver.h"
#include "../Allocators.h"
#include <postgresql/libpq-fe.h>
#include "PostgresSchema.h"

namespace imqs {
namespace dba {

class PostgresConn;

class PostgresRows : public DriverRows {
public:
	PGresult* Res     = nullptr;
	int64_t   Row     = -1;
	int64_t   NumRows = 0;

	PostgresRows(DriverConn* dcon, PGresult* res);
	~PostgresRows() override;

	Error  NextRow() override;
	Error  Get(size_t col, Attrib& val, Allocator* alloc) override;
	Error  Columns(std::vector<ColumnInfo>& cols) override;
	size_t ColumnCount() override;

	PostgresConn* PgConn();
	Type          FromPostgresType(Oid t);
};

class PostgresStmt : public DriverStmt {
public:
	std::string        Name;
	int                Slot = -1;
	std::vector<Type>  ParamTypes;
	std::vector<int>   ParamFormats;
	std::vector<void*> ParamValues;
	std::vector<int>   ParamLengths;
	io::Buffer         ParamBuf;
	OnceOffAllocator   ConvertedAttribBuf;

	size_t     CopyFieldCount = 0;     // Number of fields per record for COPY
	bool       IsCopy         = false; // True if this is a special pseudo-statement for doing COPY IN
	bool       IsCopyFinished = false; // True after the user calls Exec(0)
	io::Buffer CopyBuffer;             // Used during COPY operation

	PostgresStmt(PostgresConn* dcon, int slot, const std::string& name, size_t nParams, const Type* paramTypes); // For a regular prepared statement
	PostgresStmt(PostgresConn* dcon, size_t nFieldsInCopy);                                                      // For COPY
	~PostgresStmt() override;
	Error Exec(size_t nParams, const Attrib** params, DriverRows*& rowsOut) override;

	void          ResetParamBuffers();
	Error         PackParams(const Attrib** params);
	void          MakeParamValuePointers();
	PostgresConn* PgConn();

	Error CopyStart(const char* sql);
	Error CopyIn(size_t nRecs, const Attrib** values);
	Error CopyInRaw(const void* buf, size_t len);
	Error CopyEnd();
};

class PostgresDialect : public SqlDialect {
public:
	SqlDialectFlags   Flags() override;
	imqs::dba::Syntax Syntax() override;
	void              NativeFunc(const sqlparser::SqlAST& ast, SqlStr& s, uint32_t& printFlags) override;
	void              NativeHexLiteral(const char* hexLiteral, SqlStr& s) override;
	bool              UseThisCall(const char* funcName) override;
	void              ST_GeomFrom(SqlStr& s, const char* insertElement) override;
	void              FormatType(SqlStr& s, Type type, int width_or_srid, TypeFlags flags) override;
	void              TruncateTable(SqlStr& s, const std::string& table, bool resetSequences) override;
	bool              IsSoftKeyword(const char* name) override;
};

class PostgresConn : public DriverConn {
public:
	PGconn*          HDB = nullptr;
	std::vector<int> FreeSlots;           // Any slot in here is available to be used as a name for a prepared statement
	std::vector<int> RetiredSlots;        // See the destructor of PostgresStmt for why this is needed. Basically, we can't destroy a prepared statement inside an aborted transaction.
	int              MaxSlotNumber = 0;   // Highest number of any slot that we have ever used. First slot issued on a new connection is slot 1.
	Error            DeadWithError;       // If not OK, then then all subsequent calls fail with this error
	PostgresDialect  StaticDialect;       // Used internally by Postgres driver code
	int              GeometryOid = 0;     // The OID of the "geometry" type. Read during Connect(). This varies per server - created by PostGIS extension.
	bool             TxIsAborted = false; // Search for DEALLOCATE inside Postgres.cpp for an explanation of how this is used

	Error Connect(const ConnDesc& desc);
	void  Close();

	~PostgresConn() override;
	Error       Prepare(const char* sql, size_t nParams, const Type* paramTypes, DriverStmt*& stmt) override;
	Error       Exec(const char* sql, size_t nParams, const Attrib** params, DriverRows*& rowsOut) override;
	Error       Begin() override;
	Error       Commit() override;
	Error       Rollback() override;
	SqlDialect* Dialect() override;

	Error Exec(const char* sql);
	Error Precheck();
	Error QuerySingle(const char* sql, Attrib& result);
	void  DeleteRetiredPreparedStatements();
};

class PostgresDriver : public Driver {
public:
	static const char* DefaultTableSpace;

	PostgresSchemaReader PGSchemaReader;
	PostgresSchemaWriter PGSchemaWriter;
	PostgresDialect      StaticDialect;

	PostgresDriver();
	~PostgresDriver();
	Error                    Open(const ConnDesc& desc, DriverConn*& con) override;
	imqs::dba::SchemaReader* SchemaReader() override;
	imqs::dba::SchemaWriter* SchemaWriter() override;
	SqlDialect*              DefaultDialect() override;
};
} // namespace dba
} // namespace imqs
