#pragma once

namespace imqs {
namespace dbutil {
namespace test {
dba::Conn* CreateTempDB(std::string tempDir);
void       CreateTable(dba::Conn* con, std::string table, std::vector<dba::schema::Field> fields);                             // First field is primary key
void       CreateTableWithData(dba::Conn* con, std::string table, std::vector<dba::schema::Field> fields, std::string data);   // First field is primary key. data is comma separated, zero or more lines
void       CreateIndexOnTable(dba::Conn* con, std::string table, bool isUnique, std::vector<std::string> fields);              // create index
void       InsertRawSQL(dba::Conn* con, std::string table, std::vector<std::string> fields, std::vector<std::string> records); // records are raw SQL, that end up inside a VALUES clause
} // namespace test

} // namespace dbutil
} // namespace imqs