#include "pch.h"
#include "AnalysisModel.h"
#include "TorchUtils.h"

using namespace std;

namespace imqs {
namespace roadproc {

void ImageCropParams::SaveConsoleServerJson(nlohmann::json& jmeta, int srcWidth, int srcHeight) {
	nlohmann::json j = {
	    {"SourceWidth", srcWidth},
	    {"SourceHeight", srcHeight},
	    {"TargetWidth", TargetWidth},
	    {"TargetHeight", TargetHeight},
	    {"BottomDiscard", BottomDiscard},
	};
	jmeta["Crop"] = move(j);
}

Error ModelMeta::LoadJson(const std::string& jStr) {
	nlohmann::json j;
	auto           err = nj::ParseString(jStr, j);
	if (!err.OK())
		return err;
	Version = nj::GetString(j, "version", Version);
	NameToCategory.clear();
	CategoryToName.clear();
	if (j.find("categories") != j.end()) {
		int maxCat = 0;
		for (const auto& p : j["categories"].items()) {
			int v                   = p.value().get<int>();
			NameToCategory[p.key()] = v;
			maxCat                  = max(maxCat, v);
		}
		CategoryToName.resize(maxCat + 1);
		for (const auto& p : NameToCategory)
			CategoryToName[p.second] = p.first;
	}
	return Error();
}

void ModelMeta::SaveConsoleServerJson(nlohmann::json& jmeta) {
	nlohmann::json jcats;
	for (const auto& c : CategoryToName) {
		nlohmann::json jcat;
		jcat["Name"] = c;
		jcats.push_back(move(jcat));
	}
	jmeta["Categories"] = move(jcats);
}

Error AnalysisModel::Load(std::string baseFilename) {
	return Load(baseFilename, Model, Meta);
}

Error AnalysisModel::Load(std::string baseFilename, torch::jit::script::Module& model, ModelMeta& meta) {
	try {
		model = torch::jit::load((baseFilename + ".tm").c_str());
	} catch (std::exception& e) {
		return Error(e.what());
	}
	string metaStr;
	auto   err = os::ReadWholeFile(baseFilename + ".json", metaStr);
	if (!err.OK())
		return err;
	return meta.LoadJson(metaStr);
}

// Sum up the analysis results from the image 'result', counting the number of times that
// each category occurs. Write that into jcount.
// Example output:
// {"croc-crac": 34, "none": 1561, "curb": 120}
void AnalysisModel::SegmentationSummaryToJSON(const gfx::Image& result, nlohmann::json& jcount) const {
	// Sum up the area of each category
	IMQS_ASSERT(Meta.NumCategories() <= 255);
	vector<int> sum;
	sum.resize(Meta.NumCategories());
	uint8_t nCats = (uint8_t) Meta.NumCategories();

	for (int y = 0; y < result.Height; y++) {
		const uint8_t* res   = result.Line(y);
		int            width = result.Width;
		for (int x = 0; x < width; x++) {
			IMQS_ASSERT(res[x] < nCats);
			sum[res[x]]++;
		}
	}

	for (size_t i = 0; i < Meta.CategoryToName.size(); i++) {
		if (sum[i] != 0)
			jcount[Meta.CategoryToName[i]] = sum[i];
	}
}

} // namespace roadproc
} // namespace imqs