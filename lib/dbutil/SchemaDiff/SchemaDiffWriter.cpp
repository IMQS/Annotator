#include "pch.h"
#include "SchemaDiffWriter.h"

using namespace std;

namespace imqs {
namespace dbutil {

SchemaDiffWriter::SchemaDiffWriter(dba::SchemaWriter* writer, dba::Executor* ex) {
	Writer = writer;
	Ex     = ex;
}

Error SchemaDiffWriter::CreateTableSpace(const dba::schema::TableSpace& ts) {
	return Writer->CreateTableSpace(Ex, ts);
}

Error SchemaDiffWriter::CreateTable(const dba::schema::Table& table) {
	return Writer->CreateTable(Ex, table);
}

Error SchemaDiffWriter::CreateField(std::string table, const dba::schema::Field& field) {
	return Writer->AddField(Ex, table, field);
}

Error SchemaDiffWriter::CreateIndex(std::string table, const dba::schema::Index& idx) {
	return Writer->CreateIndex(Ex, table, idx);
}

Error SchemaDiffWriter::AlterFieldType(std::string table, const dba::schema::Field& field) {
	return Error("AlterFieldType is not supported by SchemaDiffWriter");
}

Error SchemaDiffWriter::AlterFieldName(std::string table, std::string oldName, std::string newName) {
	return Error("AlterFieldName is not supported by SchemaDiffWriter");
}

Error SchemaDiffWriter::DropTableSpace(std::string ts) {
	return Writer->DropTableSpace(Ex, ts);
}

Error SchemaDiffWriter::DropTable(std::string table) {
	return Writer->DropTable(Ex, table);
}

Error SchemaDiffWriter::DropField(std::string table, std::string field) {
	return Writer->DropField(Ex, table, field);
}

Error SchemaDiffWriter::DropIndex(std::string table, const dba::schema::Index& idx) {
	return Error("DropIndex is not supported by SchemaDiffWriter");
}

} // namespace dbutil
} // namespace imqs
