#pragma once

#include "LabelDB.h"

namespace imqs {
namespace label {

Error ExportWholeImages(LabelDB& db, std::string photoRoot, std::string exportDir);
}
} // namespace imqs