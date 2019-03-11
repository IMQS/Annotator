#pragma once

namespace imqs {
namespace dba {
namespace schema {

class Field;

IMQS_DBA_API Error       SchemaParse(const char* txt, DB& db);
IMQS_DBA_API const char* FieldTypeToSchemaFileType(const Field& f);
}
}
}
