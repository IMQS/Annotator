#include "pch.h"
#include "ASTCache.h"

using namespace std;

namespace imqs {
namespace dba {
namespace sqlparser {

ASTCache::ASTCache() {
}

ASTCache::~ASTCache() {
	Clear();
	IMQS_ASSERT(Map.size() == 0);
}

Guid ASTCache::HashSQLSource(const char* src) {
	uint64_t h1 = siphash24(src, strlen(src), Glob.SipHashKey1);
	uint64_t h2 = siphash24(src, strlen(src), Glob.SipHashKey2);
	Guid     g;
	memcpy(g.Bytes + 0, &h1, 8);
	memcpy(g.Bytes + 8, &h2, 8);
	return g;
}

const SqlAST* ASTCache::GetAST(const char* src, std::string& error) {
	return GetASTInternal(0, src, error);
}

const SqlAST* ASTCache::GetASTInternal(int srcPrefixSize, const char* src, std::string& error) {
	Guid src_guid = HashSQLSource(src);

	{
		// Check to see if we have the item in cache.
		std::lock_guard<std::mutex> lock(Lock);
		SqlAST*                     cached = (SqlAST*) Map.get(src_guid);
		if (cached != nullptr) {
			cached->RefCount++;
			return cached;
		}

		if (Map.size() > MaxSize)
			Clear();
	}

	SqlAST* parsed = ParseAndValidate(srcPrefixSize, src, &error);
	if (parsed == nullptr)
		return nullptr;

	// insert into hashmap
	std::lock_guard<std::mutex> lock(Lock);
	if (Map.contains(src_guid)) {
		// Another thread has beaten us to it. This is a very real case, brought to light by the MT test.
		// This ends up producing memory leaks if we don't cater for it.
		delete parsed;
		parsed = (SqlAST*) Map[src_guid];
	} else {
		Map.insert(src_guid, parsed);
	}
	parsed->RefCount++;
	return parsed;
}

const SqlAST* ASTCache::GetAST_Expression(const char* src, std::string& error) {
	std::string prefix = "DUMMY_WHERE ";
	auto        ast    = GetASTInternal((int) prefix.length(), (prefix + src).c_str(), error);
	if (ast == nullptr)
		return ast;

	return ast;
}

void ASTCache::ReleaseAST(const SqlAST*& ast) {
	// How do we know this const_cast is OK?
	// Trust me, I'm a doctor!
	// Long story: We want the user of ASTCache to receive a const SqlAST object, so that
	// he knows he is not allowed to mutate it. It's a shared resource. However, now that
	// it's back in our hands, we DO need to mutate it. We do that in a MT-safe manner though,
	// because RefCount is an atomic<int>.
	SqlAST*& ast_mutable = const_cast<SqlAST*&>(ast);

	// Important that this assertion runs BEFORE we decrement, because the moment we decrement,
	// the object is liable to be destroyed.
	IMQS_ASSERT(ast_mutable->RefCount.load() >= 1);

	ast_mutable->RefCount--;
	// ast_mutable may be dead by now (ie if Clear has run between our previous line and this line)

	ast = nullptr;
}

void ASTCache::Clear() {
	decltype(Map) remain;
	for (auto it : Map) {
		auto ast = (SqlAST*) it.second;
		if (ast->RefCount == 0)
			delete ast;
		else
			remain.insert(it.first, ast);
	}
	Map = remain;
}

size_t ASTCache::GetCacheSize() {
	std::lock_guard<std::mutex> lock(Lock);
	return Map.size();
}

SqlAST* ASTCache::Parse(const char* src, std::string* error) {
	return ParseAndValidate(0, src, error);
}

SqlAST* ASTCache::ParseAndValidate(int srcPrefixSize, const char* src, std::string* error) {
	Scanner scanner(src);
	Parser  parser(&scanner);
	parser.Parse();

	// I don't understand enough of the design of the Parser/Scanner to know why it's happy to not consume
	// all of it's input. It looks like when it detects a token that it doesn't understand, it happily finishes.
	// This is not what we want, in particular when we use the parser to detect the classic "; drop table" injection
	// attack. So, we add our own check here to see whether the parser has indeed consumed all of the input.
	// If not, then it's an error. [BMH 2015-10-27]
	if (parser.errors->Errors.size() != 0 || scanner.GetPos() != strlen(src)) {
		if (error != nullptr) {
			int         line = scanner.GetLine();
			int         col  = scanner.GetColumn();
			std::string msg;
			if (parser.errors->Errors.size() != 0) {
				line = parser.errors->Errors[0].Line;
				col  = parser.errors->Errors[0].Col;
				msg  = strings::utf::ConvertWideToUTF8(parser.errors->Errors[0].Error);
			}

			// This srcPrefixSize is for cases where we add in dummy prefixes such as "DUMMY_WHERE".
			// That logic only applies if this is the first line.
			if (line == 1)
				col -= srcPrefixSize;

			string srcClean = src;
			if (srcClean.find("DUMMY_WHERE ") == 0)
				srcClean = srcClean.substr(12);
			*error = tsf::fmt("Error parsing SQL: line %d col %d \"%s\"", line, col, srcClean);
			if (msg != "")
				*error += ": " + msg;
		}
		parser.gen->Clear();
		return nullptr;
	} else {
		// At this point we are in possession of a successfully parsed AST.
		if (!ValidateAST(parser.rootnode, error)) {
			parser.gen->Clear();
			return nullptr;
		}
		parser.rootnode->RefCount = 0;
		return parser.rootnode;
	}
}

bool ASTCache::ValidateAST(const SqlAST* ast, std::string* error) {
	for (SqlAST* child : ast->Params) {
		bool ok = ValidateAST(child, error);
		if (!ok)
			return false;
	}

	if (ast->IsHexValue()) {
		if (ast->Value.StrVal.length() % 2 != 0) {
			*error = tsf::fmt("Hex literal (0x%v) needs to contain an even amount of digits.", ast->Value.StrVal);
			return false;
		}
	}
	return true;
}
} // namespace sqlparser
} // namespace dba
} // namespace imqs