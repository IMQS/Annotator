#include "pch.h"
#include "html.h"

namespace imqs {
namespace html {

IMQS_PAL_API std::string EscapeInnerText(const std::string& src) {
	std::string rep;
	for (auto c : utfz::cp(src)) {
		switch (c) {
		case '<': rep += "&lt;"; break;
		case '&': rep += "&amp;"; break;
		default: utfz::encode(rep, c); break;
		}
	}
	return rep;
}

} // namespace html
} // namespace imqs
