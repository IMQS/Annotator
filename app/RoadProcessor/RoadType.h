#pragma once

namespace imqs {
namespace roadproc {

class RoadTypeModel {
public:
	enum class Type {
		// PyTorch trainer sorts the classes alphabetically, so we just do the same here,
		// and we don't need to worry about anything further.
		Gravel,
		Other,
		Tar,
	};

	std::shared_ptr<torch::jit::script::Module> Model;

	Error Load(std::string filename);
	Error Classify(const gfx::Image& img, Type& type);

	static int Test(argparse::Args& args);
};

} // namespace roadproc
} // namespace imqs