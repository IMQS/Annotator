#pragma once

namespace imqs {
namespace roadproc {

struct CloudStorageDetails {
	std::string Platform;     // Must be "gs" for Google Storage
	std::string AuthFilename; // Filename of an authentication file, such as a GCP JSON token file
	std::string AuthToken;
	std::string Bucket;
	time::Time  LastLogin;
};

Error GCSLoginWithString(const std::string& gcsJsonKey, std::string& authHeader);
Error GCSLoginWithFile(const std::string& gcsJsonKeyFile, std::string& authHeader);
Error UploadToCloudStorage(http::Connection& hc, const CloudStorageDetails& details, std::string path, std::string mimeType, const std::string& body);

Error SplitCloudStorageURL(const std::string& url, std::string& bucket, std::string& path);

} // namespace roadproc
} // namespace imqs