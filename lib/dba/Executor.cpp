#include "pch.h"
#include "Executor.h"
#include "Rows.h"

namespace imqs {
namespace dba {

Error Executor::Exec(const char* sql, std::initializer_list<Attrib> params) {
	smallvec<const Attrib*> ptr;
	for (size_t i = 0; i < params.size(); i++)
		ptr.push(params.begin() + i);
	return Exec(sql, ptr.size(), &ptr[0]);
}

Rows Executor::Query(const char* sql, std::initializer_list<Attrib> params) {
	smallvec<const Attrib*> ptr;
	for (size_t i = 0; i < params.size(); i++)
		ptr.push(params.begin() + i);
	return Query(sql, ptr.size(), &ptr[0]);
}

Error Executor::Prepare(const char* sql, std::initializer_list<Type> paramTypes, Stmt& stmt) {
	return Prepare(sql, paramTypes.size(), paramTypes.begin(), stmt);
}
} // namespace dba
} // namespace imqs
