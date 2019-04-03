#pragma once

#include "LensCorrection.h"

namespace imqs {
namespace roadproc {
namespace global {
extern LensCorrector* Lens;
extern uint16_t*      LensFixedtoRaw;

Error      Initialize();
void       Shutdown();
gfx::Vec3d ConvertLLToMerc(const gfx::Vec3d& p);
Error      Login(http::Connection& con, std::string username, std::string password, std::string& sessionCookie);
} // namespace global
} // namespace roadproc
} // namespace imqs