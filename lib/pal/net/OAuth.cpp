#include "pch.h"
#include "common.h"
#include "modp/modp_b64w.h"
#include "net/url.h"
#include "net/HttpClient.h"
#include "Time_.h"
#include "encoding/json.h"

#include "OAuth.h"

namespace imqs {
namespace OAuth {

IMQS_PAL_API Error SignJWT(const std::string& jwt, const std::string rsaPrivateKey, std::string& signedJWT) {
	// Create base64url encoded JWT header and JWT claim set
	std::string jwtHeader = R"({"alg":"RS256","typ":"JWT"})";
	std::string jwtHeader64;
	jwtHeader64.resize(modp_b64w_encode_len(jwtHeader.length()));
	auto jwtHeader64Size = modp_b64w_encode((char*) jwtHeader64.data(), jwtHeader.c_str(), jwtHeader.length());
	jwtHeader64.resize(jwtHeader64Size);

	std::string jwt64;
	jwt64.resize(modp_b64w_encode_len(jwt.length()));
	size_t jwt64Size = modp_b64w_encode((char*) jwt64.data(), jwt.c_str(), jwt.length());
	jwt64.resize(jwt64Size);

	signedJWT = jwtHeader64 + "." + jwt64;

	// Create SHA256 JWT digest
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	if (!SHA256_Update(&sha256, signedJWT.c_str(), signedJWT.length()))
		return Error("while adding message to sha256 digest");

	unsigned char digest[SHA256_DIGEST_LENGTH];
	if (!SHA256_Final(digest, &sha256))
		return Error("while computing sha256 digest");

	// Create RSA from private key
	BIO* b = nullptr;
	if (!(b = BIO_new_mem_buf(rsaPrivateKey.c_str(), (int) rsaPrivateKey.length())))
		return Error("while writing the raw RSA private key into BIO");

	RSA* rsa = nullptr;
	if (!(rsa = PEM_read_bio_RSAPrivateKey(b, nullptr, nullptr, nullptr)))
		return Error("while reading the RSA private key from BIO");
	BIO_free(b);

	// Sign digest with rsa private key
	std::string sigbuf;
	sigbuf.resize(RSA_size(rsa));

	unsigned int siglen = 0;
	if (!RSA_sign(NID_sha256, digest, SHA256_DIGEST_LENGTH, (unsigned char*) sigbuf.data(), &siglen, rsa))
		return Error("while creating a signed digest with RSA private key");
	RSA_free(rsa);

	// Create base64url encoded signed digest
	std::string sigbuf64;
	sigbuf64.resize(modp_b64w_encode_len(siglen));
	auto sigbuf64Size = modp_b64w_encode((char*) sigbuf64.data(), sigbuf.c_str(), siglen);

	auto paddingStart = sigbuf64.find("=");
	if (paddingStart != std::string::npos)
		sigbuf64.erase(paddingStart, std::string::npos);

	signedJWT += "." + sigbuf64;

	return Error();
}

IMQS_PAL_API Error GetToken(const std::string& rsaPrivateKey, const std::string& clientEmail, const std::string& tokenURI, const std::string& scope, nlohmann::json& respJBody) {
	// Create JWT object
	nlohmann::json jwt;
	jwt["iss"]   = clientEmail;
	jwt["aud"]   = tokenURI;
	jwt["scope"] = scope; // See https://developers.google.com/identity/protocols/googlescopes for list of scopes
	auto t       = time::Time();
	jwt["iat"]   = std::to_string(t.Now().Unix());
	jwt["exp"]   = std::to_string(t.Now().Unix() + (59 * 60));

	std::string signedMessage;
	auto        err = SignJWT(jwt.dump(), rsaPrivateKey, signedMessage);
	if (!err.OK())
		return Error::Fmt("while signing jwt with private key: %v", err.Message());

	auto body = url::Encode({{"grant_type", "urn:ietf:params:oauth:grant-type:jwt-bearer"}}) +
	            "&assertion=" + signedMessage;

	http::HeaderMap headers = {{"Content-Type", "application/x-www-form-urlencoded"}};

	http::Connection hc;
	auto             resp = hc.Post(tokenURI, body, headers);
	if (!resp.Err.OK())
		return Error::Fmt("while getting OAuth token: %v", resp.Err.Message());

	respJBody = nlohmann::json::parse(resp.Body.c_str());
	if (!respJBody["error"].is_null())
		return Error::Fmt("while getting OAuth token: %v: %v", nj::GetString(respJBody, "error"), nj::GetString(respJBody, "error_description"));

	return Error();
}

} // namespace OAuth
} // namespace imqs
