#pragma once
#include "../Generated/Parser.h"

namespace imqs {
namespace dba {
namespace sqlparser {

// Cache of AST representation of an SQL expression
// To get an AST from an SQL string, use GetAST().
// Once finished with it, you must call ReleaseAST().
class IMQS_DBA_API ASTCache {
public:
	size_t MaxSize = 500;

	ASTCache();
	~ASTCache();

	// Retreive an AST for the SQL string.
	// If successful, you must call ReleastAST() when finished
	const SqlAST* GetAST(const char* src, std::string& error);

	// This is a variant of GetAST which adds a dummy "WHERE" prefix,
	// and then strips that out of the generated AST, so that you end up
	// with the AST for just the pure expression. For example, you can
	// send this "a < 10" and you will get an AST with three nodes.
	// Call ReleaseAST when finished.
	const SqlAST* GetAST_Expression(const char* src, std::string& error);

	void ReleaseAST(const SqlAST*& ast);

	void        Clear();
	size_t      GetCacheSize();
	static Guid HashSQLSource(const char* src); // Abuse the GUID type to store a 16-byte fingerprint for the string

	static SqlAST* Parse(const char* src, std::string* error = nullptr); // If successful, you must delete the returned AST when finished

private:
	std::mutex              Lock; // guards access to our state (ie Map)
	ohash::map<Guid, void*> Map;  // Not really a GUID. We just use GUID as a hash key

	const SqlAST*  GetASTInternal(int srcPrefixSize, const char* src, std::string& error);
	static SqlAST* ParseAndValidate(int srcPrefixSize, const char* src, std::string* error = nullptr);
	static bool    ValidateAST(const SqlAST* ast, std::string* error);
};
}
}
}