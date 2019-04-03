#include "pch.h"
#include "Globals.h"

namespace imqs {
namespace roadproc {
namespace global {
LensCorrector* Lens;
uint16_t*      LensFixedtoRaw;

Error Initialize() {
	return Error();
}

void Shutdown() {
}

gfx::Vec3d ConvertLLToMerc(const gfx::Vec3d& p) {
	return p;
}

Error Login(http::Connection& con, std::string username, std::string password, std::string& sessionCookie) {
	http::Request req = http::Request::POST("http://roads.imqs.co.za/api/auth/login");
	req.SetBasicAuth(username, password);
	auto resp = con.Perform(req);
	if (!resp.Is200())
		return Error::Fmt("Login failed: %v", resp.ToError().Message());

	http::Cookie cookie;
	if (!resp.FirstSetCookie("session", cookie))
		return Error("No session cookie in login response");
	sessionCookie = cookie.Value;
	return Error();
}

} // namespace global
} // namespace roadproc
} // namespace imqs