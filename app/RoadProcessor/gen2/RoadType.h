#pragma once

namespace imqs {
namespace roadproc {

class RoadTypeModel {
public:
	enum class Types {
		// PyTorch trainer sorts the classes alphabetically, so we just do the same here,
		// and we don't need to worry about anything further.
		Gravel,
		Ignore,
		JeepTrack,
		Tar,
		NONE,
	};

	torch::jit::script::Module Model;

	Error Load(std::string filename);
	Error Classify(const gfx::Image& img, Types& type);

	static char TypeToChar(Types t);
	static int  Test(argparse::Args& args);
};

} // namespace roadproc
} // namespace imqs