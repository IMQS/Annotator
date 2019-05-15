#include "pch.h"
#include "Server.h"
#include "LabelDB.h"

using namespace std;

int main(int argc, const char** argv) {
	using namespace imqs;

	dba::Initialize();
	uberlog::Logger log;
	log.OpenStdOut();

	//{
	//	auto err = imqs::label::LabelDB::MergeOnceOff();
	//	tsf::print("Result: %v\n", err.Message());
	//	return 0;
	//}

	argparse::Args args("LabelServer <photo dir> <dimensions>");
	args.AddValue("e", "export", "Export labels to a hierarchy of folders ready to train a neural network");
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

	if (args.Has("export")) {
		err = s.Export(args.Get("export"));
		if (!err.OK()) {
			tsf::print("%v\n", err.Message());
			return 1;
		}
	} else {
		s.ListenAndRun();
	}

	return 0;
}