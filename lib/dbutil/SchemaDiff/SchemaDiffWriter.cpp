#include "pch.h"
#include "SchemaDiffWriter.h"

using namespace std;

namespace imqs {
namespace dbutil {

SchemaDiffWriter::SchemaDiffWriter(dba::SchemaWriter* writer, dba::Executor* ex) {
	Writer = writer;
	Ex     = ex;
}

Error SchemaDiffWriter::CreateTable(std::string tableSpace, const dba::schema::Table& table) {
	return Writer->CreateTable(Ex, tableSpace, table);
}

Error SchemaDiffWriter::CreateField(std::string tableSpace, std::string table, const dba::schema::Field& field) {
	return Writer->AddField(Ex, tableSpace, table, field);
}

Error SchemaDiffWriter::CreateIndex(std::string tableSpace, std::string table, const dba::schema::Index& idx) {
	return Writer->CreateIndex(Ex, tableSpace, table, idx);
}

Error SchemaDiffWriter::AlterFieldType(std::string tableSpace, std::string table, const dba::schema::Field& field) {
	return Error("AlterFieldType is not supported by SchemaDiffWriter");
}

Error SchemaDiffWriter::AlterFieldName(std::string tableSpace, std::string table, std::string oldName, std::string newName) {
	return Error("AlterFieldName is not supported by SchemaDiffWriter");
}

Error SchemaDiffWriter::DropTable(std::string tableSpace, std::string table) {
	return Writer->DropTable(Ex, tableSpace, table);
}

Error SchemaDiffWriter::DropField(std::string tableSpace, std::string table, std::string field) {
	return Writer->DropField(Ex, tableSpace, table, field);
}

Error SchemaDiffWriter::DropIndex(std::string tableSpace, std::string table, const dba::schema::Index& idx) {
	return Error("DropIndex is not supported by SchemaDiffWriter");
}

} // namespace dbutil
} // namespace imqs
