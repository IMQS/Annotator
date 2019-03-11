#include "pch.h"
#include "../Tx.h"
#include "../Conn.h"
#include "SchemaReader.h"

namespace imqs {
namespace dba {

Error SchemaReader::ReadSchemaInTx(uint32_t readFlags, Conn* con, std::string tableSpace, schema::DB& db, const std::vector<std::string>* restrictTables) {
	Tx*  tx  = nullptr;
	auto err = con->Begin(tx);
	if (!err.OK())
		return err;
	TxAutoCloser txCloser(tx);

	return ReadSchema(readFlags, tx, tableSpace, db, restrictTables);
}
}
}
