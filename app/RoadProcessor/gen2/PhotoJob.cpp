#include "pch.h"
#include "PhotoJob.h"
#include "CloudStorage.h"

using namespace std;

namespace imqs {
namespace roadproc {

std::string PhotoJob::CloudStorageBucket() const {
	string bucket, path;
	SplitCloudStorageURL(PhotoURL, bucket, path);
	return bucket;
}

std::string PhotoJob::CloudStoragePath() const {
	string bucket, path;
	SplitCloudStorageURL(PhotoURL, bucket, path);
	return path;
}

} // namespace roadproc
} // namespace imqs