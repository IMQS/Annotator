#include "pch.h"

namespace imqs {
namespace dba {

IMQS_DBA_API StaticError ErrDriverUnknown("Driver unknown");
IMQS_DBA_API StaticError ErrInvalidNumberOfParameters("Invalid number of parameters to a prepared statement");
IMQS_DBA_API StaticError ErrUnsupported("Unsupported");
IMQS_DBA_API StaticError ErrNotOneResult("A query that was expected to return one result exactly, returned either zero results, or more than one result");
IMQS_DBA_API StaticError ErrTransactionAborted("Transaction aborted. All subsequent queries inside the transaction will fail");
IMQS_DBA_API StaticError ErrBadCon("Connection dropped");

IMQS_DBA_API const char* ErrStubConnectFailed         = "DB connect failed";
IMQS_DBA_API const char* ErrStubKeyViolation          = "Key violation";
IMQS_DBA_API const char* ErrStubFieldNotFound         = "Field not found";
IMQS_DBA_API const char* ErrStubTableNotFound         = "Table not found";
IMQS_DBA_API const char* ErrStubRelationAlreadyExists = "Relation already exists";
IMQS_DBA_API const char* ErrStubDatabaseBusy          = "Database busy or locked";

static bool ErrorStartsWith(Error err, const char* prefix) {
	if (err.OK())
		return false;

	const char* msg = err.Message();
	size_t      i   = 0;
	for (; msg[i] && prefix[i]; i++) {
		if (msg[i] != prefix[i])
			return false;
	}
	return prefix[i] == 0;
}

IMQS_DBA_API bool IsConnectFailed(Error err) {
	return ErrorStartsWith(err, ErrStubConnectFailed);
}

IMQS_DBA_API bool IsKeyViolation(Error err) {
	return ErrorStartsWith(err, ErrStubKeyViolation);
}

IMQS_DBA_API bool IsFieldNotFound(Error err) {
	return ErrorStartsWith(err, ErrStubFieldNotFound);
}

IMQS_DBA_API bool IsTableNotFound(Error err) {
	return ErrorStartsWith(err, ErrStubTableNotFound);
}

IMQS_DBA_API bool IsRelationAlreadyExists(Error err) {
	return ErrorStartsWith(err, ErrStubRelationAlreadyExists);
}

IMQS_DBA_API bool IsDatabaseBusy(Error err) {
	return ErrorStartsWith(err, ErrStubDatabaseBusy);
}

} // namespace dba
} // namespace imqs
