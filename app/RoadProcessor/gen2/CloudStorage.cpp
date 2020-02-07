#include "pch.h"
#include "CloudStorage.h"

using namespace std;

namespace imqs {
namespace roadproc {

// Login to GCP, using a JSON key
Error GCSLoginWithString(const std::string& gcsJsonKey, std::string& authHeader) {
	auto j = nlohmann::json::parse(gcsJsonKey.c_str());
	if (j["private_key"].is_null())
		return Error("private key missing from google credentials json");

	nlohmann::json respJBody;
	auto           err = OAuth::GetToken(nj::GetString(j, "private_key"),
                               nj::GetString(j, "client_email"),
                               nj::GetString(j, "token_uri"),
                               "https://www.googleapis.com/auth/devstorage.full_control",
                               respJBody);

	if (!err.OK())
		return Error::Fmt("MakeGCPOAuthRequest failed: %v", err.Message());

	auto gTokenType   = nj::GetString(respJBody, "token_type");
	auto gAccessToken = nj::GetString(respJBody, "access_token");
	authHeader        = gTokenType + " " + gAccessToken;
	return Error();
}

Error GCSLoginWithFile(const std::string& gcsJsonKeyFile, std::string& authHeader) {
	// Read google credentials
	string raw;
	auto   err = os::ReadWholeFile(gcsJsonKeyFile, raw);
	if (!err.OK())
		return Error::Fmt("Error loading GCP deploy key from %v: %v", gcsJsonKeyFile, err.Message());
	return GCSLoginWithString(raw, authHeader);
}

Error UploadToCloudStorage(http::Connection& hc, const CloudStorageDetails& details, std::string path, std::string mimeType, const std::string& body) {
	if (details.Platform != "gcs")
		return Error::Fmt("Cloud storage platform must be 'gcs' (instead, it was '%v')", details.Platform);

	string          uploadUrl = "https://www.googleapis.com/upload/storage/v1/b/" + details.Bucket + "/o?uploadType=media&name=" + path;
	http::HeaderMap headers   = {{"Authorization", details.AuthToken},
                               {"Content-Type", mimeType}};

	auto resp = hc.Post(uploadUrl, body, headers);

	return Error();
}

Error SplitCloudStorageURL(const std::string& url, std::string& bucket, std::string& path) {
	if (url.find("gs://") != 0)
		return Error::Fmt("SplitCloudStorageURL: Invalid cloud storage url: '%v'", url);

	size_t bucketEnd = url.find('/', 5);
	if (bucketEnd == -1)
		return Error::Fmt("SplitCloudStorageURL: Invalid cloud storage url (no path): '%v'", url);

	bucket = url.substr(5, bucketEnd - 5);
	path   = url.substr(bucketEnd + 1); // GCS doesn't like a leading slash, so here we return path without a leading slash
	return Error();
}

} // namespace roadproc
} // namespace imqs