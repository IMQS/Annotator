#include "pch.h"
#include "Export.h"

using namespace std;

namespace imqs {
namespace label {

static Error FindAllExtensionsInPath(string dir, ohash::set<string>& allExt) {
	auto err = os::FindFiles(dir, [&](const os::FindFileItem& item) -> bool {
		if (item.IsDir)
			return true;
		allExt.insert(strings::tolower(path::Extension(item.Name)));
		return true;
	});
	return err;
}

Error ExportWholeImages(LabelDB& db, std::string photoRoot, std::string exportDir) {
	// Check that the export directory contains ONLY jpegs, to safeguard the user against sending
	// the wrong path here, and we end up wiping his OS or something.
	ohash::set<string> allExt;
	auto               err = FindAllExtensionsInPath(exportDir, allExt);
	if (os::IsNotExist(err))
		err = Error();
	else if (!err.OK())
		return err;
	for (auto ext : allExt) {
		if (ext != ".jpg" && ext != ".jpeg")
			return Error::Fmt("Unexpected file extensions (%v) found in %v. You must delete the directory manually before exporting");
	}

	err = os::RemoveAll(exportDir);
	if (!err.OK())
		return err;
	err = os::MkDirAll(exportDir);
	if (!err.OK())
		return err;

	dba::Tx* tx = nullptr;
	err         = db.DB->Begin(tx);
	if (!err.OK())
		return err;
	dba::TxAutoCloser txCloser(tx);
	auto              rows = tx->Query("SELECT sample.image_path, label.dimension, label.category FROM sample INNER JOIN label ON sample.id = label.sample_id");
	size_t            nth  = 0;
	for (auto row : rows) {
		nth++;
		string path, dim, lab;
		err = row.Scan(path, dim, lab);
		if (!err.OK())
			return err;
		auto dir = path::Join(exportDir, dim, lab);
		err      = os::MkDirAll(dir);
		if (!err.OK())
			return err;
		auto shortName = path::ChangeExtension(path::Filename(path), "");
		auto extension = path::Extension(path);
		auto pathHash  = tsf::fmt("%08x", XXH64(path.c_str(), path.size(), 0));
		// PhotoRoot example: /stuff/mldata
		// path example:      2019/2019-02-27/148GOPRO/G0024551.JPG
		// shortName:         G0024551
		// pathHash:          deadbeefdeadbeef
		// dir example:       /stuff/train/road_type/gravel
		// desired output:    ln -s /stuff/mldata/2019/2019-02-27/148GOPRO/G0024551.JPG /stuff/train/road_type/gravel/G0024551_deadbeefdeadbeef.JPG
		auto cmd = tsf::fmt("ln -s '%v' '%v'", path::Join(photoRoot, path), path::Join(dir, shortName) + "_" + pathHash + extension);
		if (nth % 100 == 0)
			tsf::print("%v, %v, %v\n", path, dim, lab);
		//tsf::print("%v\n", cmd);
		int ret = system(cmd.c_str());
		if (ret != 0)
			return Error::Fmt("Failed to create symlink: '%v': %d", cmd, ret);
	}
	if (!rows.OK())
		return rows.Err();
	return Error();
}

} // namespace label
} // namespace imqs