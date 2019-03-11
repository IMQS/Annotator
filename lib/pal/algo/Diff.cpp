#include "pch.h"
#include "Diff.h"

using namespace std;

namespace imqs {
namespace diff {

IMQS_PAL_API std::string DiffTest(const char* a, const char* b, std::vector<int>& ops) {
	std::string r = a;

	auto patch = [&](PatchOp op, size_t pos, size_t len, const char* el) {
		if (op == PatchOp::Delete) {
			ops.push_back(-(int) len);
			r.erase(pos, len);
		} else if (op == PatchOp::Insert) {
			ops.push_back((int) len);
			r.insert(pos, el, len);
		}
	};

	DiffCore   d;
	CharTraits traits;
	d.Diff<char, CharTraits>(strlen(a), strlen(b), a, b, traits, patch);
	return r;
}

} // namespace diff
} // namespace imqs
