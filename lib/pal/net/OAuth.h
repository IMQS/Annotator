#pragma once

namespace imqs {
namespace OAuth {

IMQS_PAL_API Error SignJWT(const std::string& jwt, const std::string rsaPrivateKey, std::string& signedJWT);
IMQS_PAL_API Error GetToken(const std::string& rsaPrivateKey, const std::string& clientEmail, const std::string& tokenURI, const std::string& scope, nlohmann::json& respJBody);

} // namespace OAuth
} // namespace imqs
