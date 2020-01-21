#include "pch.h"
#include "../Tx.h"
#include "../Conn.h"
#include "SchemaReader.h"

namespace imqs {
namespace dba {

Error SchemaReader::ReadSchemaInTx(uint32_t readFlags, Conn* con, schema::DB& db, const std::vector<std::string>* restrictTables, std::string tableSpace) {
	Tx*  tx  = nullptr;
	auto err = con->Begin(tx);
	if (!err.OK())
		return err;
	TxAutoCloser txCloser(tx);

	return ReadSchema(readFlags, tx, db, restrictTables, tableSpace);
}
} // namespace dba
} // namespace imqs
