#pragma once

namespace imqs {
namespace html {

// Escape only < and &
IMQS_PAL_API std::string EscapeInnerText(const std::string& src);

} // namespace html
} // namespace imqs