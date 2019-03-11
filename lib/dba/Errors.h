#pragma once

namespace imqs {
namespace dba {

extern IMQS_DBA_API StaticError ErrDriverUnknown;
extern IMQS_DBA_API StaticError ErrInvalidNumberOfParameters;
extern IMQS_DBA_API StaticError ErrUnsupported;
extern IMQS_DBA_API StaticError ErrNotOneResult;
extern IMQS_DBA_API StaticError ErrTransactionAborted;

// Send this if the network connection has dropped. If the dba library can be
// sure that the failed function didn't alter any DB state (for example it was
// a "create prepared statement" call), then the dba library will attempt
// to rerun the operation on a new connection.
// If it is not possible to automatically rerun the operation, the dba library
// will return this error to the caller.
extern IMQS_DBA_API StaticError ErrBadCon;

// Uses these prefixes for error messages, so that clients can detect these conditions
// For example, a key violation error can be formed as Error(std::string(ErrStubKeyViolation) + ": ID field already contains a value 123")
// Use the functions IsFieldNotFound(), IsTableNotFound(), etc, to check for these well known errors.
extern IMQS_DBA_API const char* ErrStubConnectFailed;
extern IMQS_DBA_API const char* ErrStubKeyViolation;
extern IMQS_DBA_API const char* ErrStubFieldNotFound;
extern IMQS_DBA_API const char* ErrStubTableNotFound;
extern IMQS_DBA_API const char* ErrStubRelationAlreadyExists;
extern IMQS_DBA_API const char* ErrStubDatabaseBusy;

IMQS_DBA_API bool IsConnectFailed(Error err);         // Returns true if the error is a DB connect failure
IMQS_DBA_API bool IsKeyViolation(Error err);          // Returns true if the error is a key violation
IMQS_DBA_API bool IsFieldNotFound(Error err);         // Returns true if the error is a "field not found" error
IMQS_DBA_API bool IsTableNotFound(Error err);         // Returns true if the error is a "table not found" error
IMQS_DBA_API bool IsRelationAlreadyExists(Error err); // Returns true if the error is a "table, field, index, etc, already exists" error
IMQS_DBA_API bool IsDatabaseBusy(Error err);          // Returns true if the error is an Sqlite error about a database being busy or locked, and retrying might succeed
} // namespace dba
} // namespace imqs
