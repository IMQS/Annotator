#include "pch.h"
#include "Server.h"

using namespace std;

int main(int argc, const char** argv) {
	using namespace imqs;

	dba::Initialize();
	uberlog::Logger log;
	log.OpenStdOut();

	argparse::Args args("LabelServer <photo dir> <dimensions>");
	if (!args.Parse(argc, argv) || args.Params.size() != 2) {
		if (!args.WasHelpShown)
			args.ShowHelp();
		return 1;
	}

	imqs::label::Server s;
	auto                err = s.Initialize(&log, args.Params[0], args.Params[1]);
	if (!err.OK()) {
		tsf::print("Error: %v\n", err.Message());
		return 1;
	}
	s.ListenAndRun();

	return 0;
}