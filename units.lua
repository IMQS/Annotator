require 'tundra.syntax.glob'
require 'tundra.syntax.files'

local winFilter = "win*"
local winDebugFilter = "win*-*-debug"
local winReleaseFilter = "win*-*-release"
local linuxFilter = "linux-*"

local winKernelLibs = { "kernel32.lib", "user32.lib", "gdi32.lib", "winspool.lib", "advapi32.lib", "shell32.lib", "comctl32.lib", 
						"uuid.lib", "ole32.lib", "oleaut32.lib", "shlwapi.lib", "OLDNAMES.lib", "wldap32.lib", "wsock32.lib",
						"Psapi.lib", "Msimg32.lib", "Comdlg32.lib", "RpcRT4.lib", "Iphlpapi.lib", "Delayimp.lib" }

-- Dynamic (msvcr110.dll etc) CRT linkage
local winLibsDynamicCRTDebug = tundra.util.merge_arrays( { "msvcrtd.lib", "msvcprtd.lib", "comsuppwd.lib" }, winKernelLibs )
local winLibsDynamicCRTRelease = tundra.util.merge_arrays( { "msvcrt.lib", "msvcprt.lib", "comsuppw.lib" }, winKernelLibs )

winLibsDynamicCRTDebug.Config = winDebugFilter
winLibsDynamicCRTRelease.Config = winReleaseFilter

-- Static CRT linkage
local winLibsStaticCRTDebug = tundra.util.merge_arrays( { "libcmtd.lib", "libcpmtd.lib", "comsuppwd.lib" }, winKernelLibs )
local winLibsStaticCRTRelease = tundra.util.merge_arrays( { "libcmt.lib", "libcpmt.lib", "comsuppw.lib" }, winKernelLibs )

winLibsStaticCRTDebug.Config = winDebugFilter
winLibsStaticCRTRelease.Config = winReleaseFilter

local winDynamicOpts = {
	{ "/MDd";					Config = winDebugFilter },
	{ "/MD";					Config = winReleaseFilter },
}

local winStaticOpts = {
	{ "/MTd";					Config = winDebugFilter },
	{ "/MT";					Config = winReleaseFilter },
}

local winDefs = {
	{ "_CRTDBG_MAP_ALLOC";		Config = winDebugFilter },
}

local winDynamicEnv = {
	CCOPTS = winDynamicOpts,
	CXXOPTS = winDynamicOpts,
	CCDEFS = winDefs,
	CPPDEFS = winDefs,
}

local winStaticEnv = {
	CCOPTS = winStaticOpts,
	CXXOPTS = winStaticOpts,
	CCDEFS = winDefs,
	CPPDEFS = winDefs,
}

local crtDynamic = ExternalLibrary {
	Name = "crtdynamic",
	Propagate = {
		Env = winDynamicEnv,
		Libs = {
			winLibsDynamicCRTDebug,
			winLibsDynamicCRTRelease,
		},
	},
}

local crtStatic = ExternalLibrary {
	Name = "crtstatic",
	Propagate = {
		Env = winStaticEnv,
		Libs = {
			winLibsStaticCRTDebug,
			winLibsStaticCRTRelease,
		},
	},
}

local staticAnalysis = ExternalLibrary {
	Name = "staticAnalysis",
	Propagate = {
		Env = {
			CXXOPTS = {
				{ "/analyze"; Config = "win*-*-*-analyze" },
			},
		},
	},
}

-- Swapping this out will change linkage to use MSVCR120.dll and its cousins,
-- instead of statically linking to the required MSVC runtime libraries.
-- I don't understand why, but if we build with crtStatic, then tests fail
-- due to heap corruption. I can only guess it's the old issue of two different
-- libraries using different heap properties. For example, library A allocated
-- memory with debug_alloc(), and then library B tries to free that memory with
-- regular free(). However, I cannot figure out how that is happening.
--local winCrt = crtStatic
local winCrt = crtDynamic

local linuxCrt = ExternalLibrary {
	Name = "linuxCrt",
	Propagate = {
		Libs = {
			{ "stdc++"; Config = linuxFilter }
		},
	},
}

-- Return an FGlob node that has our standard filters applied
local function makeGlob(dir, options)
	local filters = {
		{ Pattern = "_windows"; Config = winFilter },
		{ Pattern = "_linux"; Config = linuxFilter },
		{ Pattern = "_android"; Config = "ignore" },       -- Android stuff is built with a different build system
		{ Pattern = "[/\\]_[^/\\]*$"; Config = "ignore" }, -- Any file that starts with an underscore is ignored
	}
	if options.Ignore ~= nil then
		for _, ignore in ipairs(options.Ignore) do
			filters[#filters + 1] = { Pattern = ignore; Config = "ignore" }
		end
	end

	local recursive = true
	if options.Recursive ~= nil then
		recursive = options.Recursive
	end

	return FGlob {
		Dir = dir,
		Extensions = { ".c", ".cpp", ".cu", ".h" },
		Filters = filters,
		Recursive = recursive,
	}
end

local function makePrecompiledHeader(dir)
	return {
		Source = dir .. "/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	}
end

local function copyfile_to_output(source, config)
	-- extract just the final part of the path (ie the filename)
	local filename = source:match("/([^/$]+)$")

	if config then
		return CopyFile { Source = source, Target = "$(OBJECTDIR)$(SEP)" .. filename; Config = config }
	else
		return CopyFile { Source = source, Target = "$(OBJECTDIR)$(SEP)" .. filename }
	end
end

local ideHintThirdParty = {
	Msvc = {
		SolutionFolder = "Third Party"
	}
}

local ideHintLibrary = {
	Msvc = {
		SolutionFolder = "Libraries"
	}
}

local ideHintApp = {
	Msvc = {
		SolutionFolder = "Applications"
	}
}

local unicode = ExternalLibrary {
	Name = "unicode",
	Propagate = {
		Defines = { "UNICODE", "_UNICODE" },
	},
}

local vcpkg_bin = "third_party/vcpkg/installed/x64-windows/"

local deploy_sqlite = copyfile_to_output(vcpkg_bin .. "bin/sqlite3.dll", winFilter)

local deploy_libcurl_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/libcurl-d.dll", winDebugFilter)
local deploy_libcurl_release = copyfile_to_output(vcpkg_bin .. "bin/libcurl.dll", winReleaseFilter)

local deploy_libssh2_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/libssh2.dll", winDebugFilter)
local deploy_libssh2_release = copyfile_to_output(vcpkg_bin .. "bin/libssh2.dll", winReleaseFilter)

local deploy_libeay32_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/libeay32.dll", winDebugFilter)
local deploy_libeay32_release = copyfile_to_output(vcpkg_bin .. "bin/libeay32.dll", winReleaseFilter)

local deploy_ssleay32_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/ssleay32.dll", winDebugFilter)
local deploy_ssleay32_release = copyfile_to_output(vcpkg_bin .. "bin/ssleay32.dll", winReleaseFilter)

local deploy_zlib_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/zlibd1.dll", winDebugFilter)
local deploy_zlib_release = copyfile_to_output(vcpkg_bin .. "bin/zlib1.dll", winReleaseFilter)

local deploy_png_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/libpng16d.dll", winDebugFilter)
local deploy_png_release = copyfile_to_output(vcpkg_bin .. "bin/libpng16.dll", winReleaseFilter)

local deploy_tiff_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/tiffd.dll", winDebugFilter)
local deploy_tiff_release = copyfile_to_output(vcpkg_bin .. "bin/tiff.dll", winReleaseFilter)

local deploy_jpeg_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/jpeg62.dll", winDebugFilter)
local deploy_jpeg_release = copyfile_to_output(vcpkg_bin .. "bin/jpeg62.dll", winReleaseFilter)

local deploy_libjpeg_turbo = copyfile_to_output(vcpkg_bin .. "bin/turbojpeg.dll", winFilter)

local deploy_lz4_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/lz4d.dll", winDebugFilter)
local deploy_lz4_release = copyfile_to_output(vcpkg_bin .. "bin/lz4.dll", winReleaseFilter)

local deploy_avcodec_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/avcodec-57.dll", winDebugFilter)
local deploy_avdevice_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/avdevice-57.dll", winDebugFilter)
local deploy_avfilter_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/avfilter-6.dll", winDebugFilter)
local deploy_avformat_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/avformat-57.dll", winDebugFilter)
local deploy_avutil_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/avutil-55.dll", winDebugFilter)
local deploy_swresample_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/swresample-2.dll", winDebugFilter)
local deploy_swscale_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/swscale-4.dll", winDebugFilter)

local deploy_avcodec_release = copyfile_to_output(vcpkg_bin .. "bin/avcodec-57.dll", winReleaseFilter)
local deploy_avdevice_release = copyfile_to_output(vcpkg_bin .. "bin/avdevice-57.dll", winReleaseFilter)
local deploy_avfilter_release = copyfile_to_output(vcpkg_bin .. "bin/avfilter-6.dll", winReleaseFilter)
local deploy_avformat_release = copyfile_to_output(vcpkg_bin .. "bin/avformat-57.dll", winReleaseFilter)
local deploy_avutil_release = copyfile_to_output(vcpkg_bin .. "bin/avutil-55.dll", winReleaseFilter)
local deploy_swresample_release = copyfile_to_output(vcpkg_bin .. "bin/swresample-2.dll", winReleaseFilter)
local deploy_swscale_release = copyfile_to_output(vcpkg_bin .. "bin/swscale-4.dll", winReleaseFilter)

local deploy_opencv_core_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/opencv_core341d.dll", winDebugFilter)
local deploy_opencv_core_release = copyfile_to_output(vcpkg_bin .. "bin/opencv_core341.dll", winReleaseFilter)
local deploy_opencv_flann_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/opencv_flann341d.dll", winDebugFilter)
local deploy_opencv_flann_release = copyfile_to_output(vcpkg_bin .. "bin/opencv_flann341.dll", winReleaseFilter)
local deploy_opencv_features2d_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/opencv_features2d341d.dll", winDebugFilter)
local deploy_opencv_features2d_release = copyfile_to_output(vcpkg_bin .. "bin/opencv_features2d341.dll", winReleaseFilter)
local deploy_opencv_imgproc_debug = copyfile_to_output(vcpkg_bin .. "debug/bin/opencv_imgproc341d.dll", winDebugFilter)
local deploy_opencv_imgproc_release = copyfile_to_output(vcpkg_bin .. "bin/opencv_imgproc341.dll", winReleaseFilter)

local deploy_xo_debug = copyfile_to_output("third_party/xo/t2-output/win64-msvc2015-debug-default/xo.dll", winDebugFilter)
local deploy_xo_release = copyfile_to_output("third_party/xo/t2-output/win64-msvc2015-release-default/xo.dll", winReleaseFilter)

local ffmpeg = ExternalLibrary {
	Name = "ffmpeg",
	Depends = {
		deploy_avcodec_debug,
		deploy_avdevice_debug,
		deploy_avfilter_debug,
		deploy_avformat_debug,
		deploy_avutil_debug,
		deploy_swresample_debug,
		deploy_swscale_debug,
		deploy_avcodec_release,
		deploy_avdevice_release,
		deploy_avfilter_release,
		deploy_avformat_release,
		deploy_avutil_release,
		deploy_swresample_release,
		deploy_swscale_release,
	},
	Propagate = {
		Libs = {
			{ "avcodec.lib", "avdevice.lib", "avfilter.lib", "avformat.lib", "avutil.lib", "swresample.lib", "swscale.lib"; Config = winFilter },
			{ "avcodec", "avdevice", "avfilter", "avformat", "avutil", "swresample", "swscale"; Config = linuxFilter },
		},
	}
}

local libcurl = ExternalLibrary {
	Name = "libcurl",
	Depends = {
		deploy_libcurl_debug,
		deploy_libcurl_release,
		deploy_libssh2_debug,
		deploy_libssh2_release,
		deploy_libeay32_debug,
		deploy_libeay32_release,
		deploy_ssleay32_debug,
		deploy_ssleay32_release,
		deploy_zlib_debug,
		deploy_zlib_release,
	},
	Propagate = {
		Libs = {
			{ "libcurl.lib"; Config = winFilter },
			{ "curl"; Config = linuxFilter },
		}
	}
}

local cntk_bin = "third_party/cntk/Windows/lib/" 

local cntk_dlls_raw = {
	"Cntk.BinaryConvolutionExample-2.2",
	"Cntk.Composite-2.2",
	"Cntk.Core-2.2",
	--"Cntk.Core.CSBinding-2.2",
	--"Cntk.Core.JavaBinding-2.2",
	"Cntk.Core.Managed-2.2",
	"Cntk.Deserializers.Binary-2.2",
	"Cntk.Deserializers.HTK-2.2",
	"Cntk.Deserializers.Image-2.2",
	"Cntk.Deserializers.TextFormat-2.2",
	"Cntk.Eval-2.2",
	--"Cntk.Eval.Wrapper-2.2",
	"Cntk.ExtensibilityExamples-2.2",
	"Cntk.Math-2.2",
	"Cntk.PerformanceProfiler-2.2",
	--"Cntk.Reader.Binary.Deprecated-2.2",
	"Cntk.Reader.DSSM-2.2",
	"Cntk.Reader.HTKMLF-2.2",
	"Cntk.Reader.LMSequence-2.2",
	"Cntk.Reader.LUSequence-2.2",
	"Cntk.Reader.SparsePC-2.2",
	"Cntk.Reader.SVMBinary-2.2",
	"Cntk.Reader.UCIFast-2.2",
}

local cntk_libs_raw = {
	"Cntk.Actions-2.2",
	"Cntk.BinaryConvolutionExample-2.2",
	"Cntk.Common-2.2",
	"Cntk.Composite-2.2",
	"Cntk.ComputationNetwork-2.2",
	"Cntk.Core-2.2",
	--"Cntk.Core.CSBinding-2.2",
	--"Cntk.Core.JavaBinding-2.2",
	"Cntk.Deserializers.Binary-2.2",
	"Cntk.Deserializers.HTK-2.2",
	"Cntk.Deserializers.Image-2.2",
	"Cntk.Deserializers.TextFormat-2.2",
	"Cntk.Eval-2.2",
	--"Cntk.ExtensibilityExamples-2.2",
	"Cntk.Math-2.2",
	"Cntk.Math.Cuda-2.2",
	"Cntk.PerformanceProfiler-2.2",
	"Cntk.Reader-2.2",
	--"Cntk.Reader.Binary.Deprecated-2.2",
	"Cntk.Reader.DSSM-2.2",
	"Cntk.Reader.HTKMLF-2.2",
	"Cntk.Reader.LMSequence-2.2",
	"Cntk.Reader.LUSequence-2.2",
	"Cntk.Reader.SparsePC-2.2",
	"Cntk.Reader.SVMBinary-2.2",
	"Cntk.Reader.UCIFast-2.2",
	"Cntk.SequenceTrainingLib-2.2",
	"Cntk.SGD-2.2",
}

local cntk_deploy = {}
for i, dll in ipairs(cntk_dlls_raw) do
	cntk_deploy[#cntk_deploy + 1] = copyfile_to_output(cntk_bin .. dll .. "d.dll", winDebugFilter)
	cntk_deploy[#cntk_deploy + 1] = copyfile_to_output(cntk_bin .. dll .. ".dll", winReleaseFilter)
end

cntk_deploy[#cntk_deploy + 1] = copyfile_to_output(cntk_bin .. "cublas64_80.dll", winFilter)
cntk_deploy[#cntk_deploy + 1] = copyfile_to_output(cntk_bin .. "cudart64_80.dll", winFilter)
cntk_deploy[#cntk_deploy + 1] = copyfile_to_output(cntk_bin .. "cudnn64_5.dll", winFilter)
cntk_deploy[#cntk_deploy + 1] = copyfile_to_output(cntk_bin .. "curand64_80.dll", winFilter)
cntk_deploy[#cntk_deploy + 1] = copyfile_to_output(cntk_bin .. "cusparse64_80.dll", winFilter)
cntk_deploy[#cntk_deploy + 1] = copyfile_to_output(cntk_bin .. "nvml.dll", winFilter)
cntk_deploy[#cntk_deploy + 1] = copyfile_to_output(cntk_bin .. "mkl_cntk_p.dll", winFilter) -- Used in CPU mode
cntk_deploy[#cntk_deploy + 1] = copyfile_to_output(cntk_bin .. "libiomp5md.dll", winFilter) -- Used in CPU mode (used by mkl_cntk_p.dll)

local cntk_libs = {}
for i, lib in ipairs(cntk_libs_raw) do
	cntk_libs[#cntk_libs + 1] = { lib .. "d.lib"; Config = winDebugFilter }
	cntk_libs[#cntk_libs + 1] = { lib .. ".lib"; Config = winReleaseFilter }
end

local cntk = ExternalLibrary {
	Name = "cntk",
	Depends = cntk_deploy,
	Propagate = {
		Libs = cntk_libs,
		Env = {
			LIBPATH = {
				{ "third_party/cntk/Windows/lib"; Config = "win64-*" },
			}
		}
	}
}

local zlib = ExternalLibrary {
	Name = "zlib",
	Depends = {
		deploy_zlib_debug,
		deploy_zlib_release,
	},
	Propagate = {
		Libs = {
			{ "zlibd.lib"; Config = winDebugFilter },
			{ "zlib.lib"; Config = winReleaseFilter },
			{ "z"; Config = linuxFilter },
		}
	}
}

local png = ExternalLibrary {
	Name = "png",
	Depends = {
		deploy_png_debug,
		deploy_png_release,
	},
	Propagate = {
		Libs = {
			{ "libpng16d.lib"; Config = winDebugFilter },
			{ "libpng16.lib"; Config = winReleaseFilter },
			{ "png"; Config = linuxFilter },
		}
	}
}

local libjpeg_turbo = ExternalLibrary {
	Name = "libjpeg_turbo",
	Depends = {
		deploy_libjpeg_turbo,
	},
	Propagate = {
		Libs = {
			{ "turbojpeg.lib"; Config = winFilter },
			{ "turbojpeg"; Config = linuxFilter },
		}
	}
}

local lz4 = ExternalLibrary {
	Name = "lz4",
	Depends = {
		deploy_lz4_debug,
		deploy_lz4_release,
	},
	Propagate = {
		Libs = {
			{ "lz4d.lib"; Config = winDebugFilter },
			{ "lz4.lib"; Config = winReleaseFilter },
			{ "lz4"; Config = linuxFilter },
		}
	}
}

local proj4 = ExternalLibrary {
	Name = "proj4",
	Propagate = {
		Libs = {
			{ "proj"; Config = linuxFilter },
		}
	}
}

local opencv = ExternalLibrary {
	Name = "opencv",
	Depends = {
		deploy_opencv_core_debug,
		deploy_opencv_core_release,
		deploy_opencv_flann_debug,
		deploy_opencv_flann_release,
		deploy_opencv_features2d_debug,
		deploy_opencv_features2d_release,
		deploy_opencv_imgproc_debug,
		deploy_opencv_imgproc_release,
		deploy_jpeg_debug,
		deploy_jpeg_release,
		deploy_png_debug,
		deploy_png_release,
		deploy_tiff_debug,
		deploy_tiff_release,
		deploy_zlib_debug,
		deploy_zlib_release,
	},
	Propagate = {
		Libs = {
			{"opencv_core341d.lib", "opencv_features2d341d.lib", "opencv_imgproc341d.lib"; Config = winDebugFilter },
			{"opencv_core341.lib", "opencv_features2d341.lib", "opencv_imgproc341.lib"; Config = winReleaseFilter },
			{"opencv_core", "opencv_features2d", "opencv_xfeatures2d", "opencv_imgproc"; Config = linuxFilter },
		},
		Includes = {
			"/usr/local/include/opencv4" -- TESTING
		}
	},
}

-- See comment inside build/copy-libtorch-to-deps for the thinking around why we
-- choose to reference LibTorch from a 'deps' directory inside our source tree.
local torch_root = "deps/torch" 
local deploy_libtorch_c10_cuda = copyfile_to_output(torch_root .. "/lib/libc10_cuda.so", linuxFilter)
local deploy_libtorch_c10 = copyfile_to_output(torch_root .. "/lib/libc10.so", linuxFilter)
local deploy_libtorch_caffe2 = copyfile_to_output(torch_root .. "/lib/libcaffe2.so", linuxFilter)
--local deploy_libtorch_torch = copyfile_to_output(torch_root .. "/lib/libtorch.so", linuxFilter)
local deploy_libtorch_torch = copyfile_to_output(torch_root .. "/lib/libtorch.so.1", linuxFilter)

local torch = ExternalLibrary {
	Name = "torch",
	Depends = {
		deploy_libtorch_c10_cuda,
		deploy_libtorch_c10,
		deploy_libtorch_caffe2,
		deploy_libtorch_torch,
	},
	Propagate = {
		Env = {
			LIBPATH = {
				{ "deps/torch/lib"; Config = linuxFilter },
			},
		},
		Libs = {
			{ "torch", "caffe2", "c10", "c10_cuda", "nvrtc"; Config = linuxFilter },
		},
		Includes = {
			"deps/torch/include",
		},
	},
}

local glfw = ExternalLibrary {
	Name = "glfw",
	Propagate = {
		Env = {
--			CPPPATH = {
--
--			},
			LIBPATH = {
				{ "third_party/glfw/build/src"; Config = linuxFilter },
			},
		},
		Includes = {
			"third_party/glfw/deps",
		},
		Libs = {
			{ "glfw3"; Config = linuxFilter },
		}
	}
}

local tlsf = StaticLibrary {
	Name = "tlsf",
	Depends = { winCrt, },
	Sources = {
		"third_party/tlsf/tlsf.c",
		"third_party/tlsf/tlsf.h",
	},
	IdeGenerationHints = ideHintThirdParty,
}

local tsf = StaticLibrary {
	Name = "tsf",
	Depends = { winCrt, },
	Sources = {
		"third_party/tsf/tsf.cpp",
		"third_party/tsf/tsf.h",
	},
	IdeGenerationHints = ideHintThirdParty,
}

local utfz = StaticLibrary {
	Name = "utfz",
	Depends = { winCrt, },
	Sources = {
		"third_party/utfz/utfz.cpp",
		"third_party/utfz/utfz.h",
	},
	IdeGenerationHints = ideHintThirdParty,
}

local sqlite = StaticLibrary {
	Name = "sqlite",
	Depends = { winCrt, },
	Sources = {
		"third_party/sqlite/sqlite3.c",
		"third_party/sqlite/sqlite3.h",
	},
	IdeGenerationHints = ideHintThirdParty,
}

--[[
local xo = ExternalLibrary {
	Name = "xo",
	Depends = {
		deploy_xo_debug,
		deploy_xo_release,
	},
	Propagate = {
		Env = {
			LIBPATH = {
				{ "third_party/xo/t2-output/win64-msvc2015-debug-default", Config = winDebugFilter },
				{ "third_party/xo/t2-output/win64-msvc2015-release-default", Config = winReleaseFilter },
			},
		},
		Libs = { "xo.lib" },
	},
}
--]]

local freetype = StaticLibrary {
	Name = "freetype",
	Defines = {
		"FT2_BUILD_LIBRARY",
	},
	Depends = { winCrt, },
	SourceDir = "third_party/xo/dependencies/freetype",
	Includes = "third_party/xo/dependencies/freetype/include",
	Sources = {
		"src/autofit/autofit.c",
		"src/base/ftbase.c",
		"src/base/ftbitmap.c",
		"src/bdf/bdf.c",
		"src/cff/cff.c",
		"src/cache/ftcache.c",
		"src/base/ftgasp.c",
		"src/base/ftglyph.c",
		"src/gzip/ftgzip.c",
		"src/base/ftinit.c",
		"src/base/ftlcdfil.c",
		"src/lzw/ftlzw.c",
		"src/base/ftstroke.c",
		"src/base/ftsystem.c",
		"src/smooth/smooth.c",
		"src/base/ftbbox.c",
		"src/base/ftmm.c",
		"src/base/ftpfr.c",
		"src/base/ftsynth.c",
		"src/base/fttype1.c",
		"src/base/ftwinfnt.c",
		"src/pcf/pcf.c",
		"src/pfr/pfr.c",
		"src/psaux/psaux.c",
		"src/pshinter/pshinter.c",
		"src/psnames/psmodule.c",
		"src/raster/raster.c",
		"src/sfnt/sfnt.c",
		"src/truetype/truetype.c",
		"src/type1/type1.c",
		"src/cid/type1cid.c",
		"src/type42/type42.c",
		"src/winfonts/winfnt.c",
	}
}

local directx = ExternalLibrary {
	Name = "directx",
	Propagate = {
		Libs = {
			{ "D3D11.lib", "d3dcompiler.lib"; Config = "win*" },
		},
	},
}

-- collection of stb libraries
local stb = StaticLibrary {
	Name = "stb",
	Sources = {
		"third_party/stb/stb.cpp"
	}
}

-- This is not used on Linux Desktop
local expat = StaticLibrary {
	Name = "expat",
	Depends = { winCrt, },
	Defines = {
		"XML_STATIC",
		{ "WIN32"; Config = winFilter },
	},
	Sources = {
		makeGlob("third_party/xo/dependencies/expat", {})
	}
}

local xo = SharedLibrary {
	Name = "xo",
	Libs = {
		{ "opengl32.lib", "user32.lib", "gdi32.lib", "winmm.lib" ; Config = winFilter },
		{ "X11", "GL", "GLU", "stdc++", "expat", "pthread"; Config = linuxFilter },
	},
	Defines = {
		"XML_STATIC", -- for expat
		"XO_NO_STB_IMAGE",
		"XO_NO_STB_IMAGE_WRITE",
		"XO_NO_TSF",
	},
	Includes = {
		"third_party/xo/xo",
		"third_party/xo/dependencies/freetype/include",
		"third_party/xo/dependencies/agg/include",
		"third_party/xo/dependencies/expat",
	},
	Depends = {
		winCrt, freetype, directx, utfz, stb, tsf,
		{ expat; Config = winFilter },
	},
	PrecompiledHeader = {
		Source = "third_party/xo/xo/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Sources = {
		makeGlob("third_party/xo/xo", {}),
		makeGlob("third_party/xo/dependencies/agg", {}),
		makeGlob("third_party/xo/dependencies/ConvertUTF", {}),
		makeGlob("third_party/xo/dependencies/GL", {}),
		makeGlob("third_party/xo/dependencies/hash", {}),
	},
}

local uberlogger = Program {
	Name = "uberlogger",
	Depends = { winCrt, linuxCrt },
	Libs = {
		{ "rt"; Config = linuxFilter },
	},
	SourceDir = "third_party/uberlog",
	Sources = {
		"uberlog.cpp",
		"uberlog.h",
		"uberlogger.cpp",
		"tsf.cpp",
		"tsf.h",
	},
	IdeGenerationHints = ideHintApp,
}

local uberlog = StaticLibrary {
	Name = "uberlog",
	Depends = { winCrt, uberlogger },
	SourceDir = "third_party/uberlog",
	Sources = {
		"uberlog.cpp",
		"uberlog.h",
		"tsf.cpp",
		"tsf.h",
	},
	IdeGenerationHints = ideHintThirdParty,
}

local phttp = StaticLibrary {
	Name = "phttp",
	Depends = { winCrt, },
	Sources = {
		"third_party/phttp/phttp.cpp",
		"third_party/phttp/phttp.h",
		"third_party/phttp/sha1.c",
		"third_party/phttp/http11/http11_parser.c",
	},
	IdeGenerationHints = ideHintThirdParty,
}

local tinyxml2 = StaticLibrary {
	Name = "tinyxml2",
	Depends = { winCrt, },
	Sources = {
		"third_party/tinyxml2/tinyxml2.cpp",
		"third_party/tinyxml2/tinyxml2.h",
	},
	IdeGenerationHints = ideHintThirdParty,
}

local agg = StaticLibrary {
	Name = "agg",
	Depends = { winCrt, },
	Includes = {
		"third_party/agg/include",
	},
	Sources = {
		FGlob {
			--Dir = "third_party/agg",
			Dir = "third_party/agg/src",
			Extensions = { ".cpp" },
			Filters = {},
			Recursive = true,
		}
	},
	IdeGenerationHints = ideHintThirdParty,
}

local minizip = StaticLibrary {
	Name = "minizip",
	Depends = { winCrt, },
	Defines = {
		"HAVE_ZLIB",
		{	"__USE_FILE_OFFSET64",
			"__USE_LARGEFILE64",
			"_LARGEFILE64_SOURCE",
			"_FILE_OFFSET_BIT=64"; Config = linuxFilter }
	},
	Sources = {
		FGlob {
			Dir = "third_party/minizip/src",
			Extensions = { ".c", ".h" },
			Filters = { 
				{ Pattern = "_lzma"; Config = "ignore" },
				{ Pattern = "_aes"; Config = "ignore" },
				{ Pattern = "_bzip"; Config = "ignore" },
				{ Pattern = "_win32"; Config = winFilter },
				{ Pattern = "_posix"; Config = linuxFilter },
			},
			Recursive = false,
		}
	},
	IdeGenerationHints = ideHintThirdParty,
}

local pal = SharedLibrary {
	Name = "pal",
	Depends = { winCrt, uberlog, utfz, tsf, libcurl, minizip, zlib, lz4, tinyxml2 },
	Includes = {
		"lib/pal",
	},
	Libs = {
		{ "Dbghelp.lib"; Config = winFilter },
		{ "uuid", "curl", "unwind"; Config = linuxFilter },
	},
	PrecompiledHeader = {
		Source = "lib/pal/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Sources = {
		makeGlob("lib/pal", {}),
	},
	IdeGenerationHints = ideHintLibrary,
}

local projwrap = SharedLibrary {
	Name = "projwrap",
	Depends = { staticAnalysis, winCrt, pal, tsf },
	PrecompiledHeader = {
		Source = "lib/projwrap/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Defines = {
		"IMQS_PROJWRAP_EXCLUDE_GDAL"
	},
	Includes = {
		"lib/projwrap",
	},
	Sources = {
		makeGlob("lib/projwrap", {})
	},
	IdeGenerationHints = ideHintLibrary,
}

local gfx = StaticLibrary {
	Name = "gfx",
	Depends = { winCrt, pal, libjpeg_turbo, png, stb },
	PrecompiledHeader = {
		Source = "lib/gfx/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Includes = {
		"lib/gfx",
	},
	Sources = {
		makeGlob("lib/gfx", {})
	},
	IdeGenerationHints = ideHintLibrary,
}

local CUDA = ExternalLibrary {
	Name = "CUDA",
	Propagate = {
		Env = {
			LIBPATH = {
				{ "/usr/local/cuda/lib64",
				  "/usr/lib/nvidia-396"; Config = linuxFilter },
				{ "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v9.1/lib/x64"; Config = winFilter },
			},
			NVCCOPTS = {
				{ "-std=c++11 -Xcompiler -fPIC"; Config = linuxFilter },
				{ "--cl-version 2015 -Xcompiler \"$(CCOPTS)\""; Config = winFilter },
			}
		},
		Includes = {
			{ "/usr/local/cuda/include",
			  "/usr/include/nvidia-396/cuda"; Config = linuxFilter },
			{ '"$(CUDA_PATH)\\include"'; Config = winFilter },
		},
		Libs = { 
			{ "cuda", "cudart", "nvcuvid"; Config = linuxFilter },
			{ "cuda.lib", "cudart.lib", "nvcuvid.lib"; Config = winFilter },
		},	
	}	
}

local dba = SharedLibrary {
	Name = "dba",
	Depends = { staticAnalysis, winCrt, pal, tlsf, utfz, tsf, deploy_sqlite, projwrap },
	Libs = {
		-- version.lib is needed by SQLAPI
		{ "libpq.lib", "x64-windows/lib/sqlite3.lib", "Ws2_32.lib", "Secur32.lib", "version.lib"; Config = winFilter },
		{ "pq", "sqlite3"; Config = linuxFilter },
	},
	Defines = {
		"IMQS_DBA_EXCLUDE_SQLAPI"
	},
	Env = {
		-- libpq is linked against the static CRT. We are squashing a linker warning here. A better solution
		-- might be to build libpq so that it links against the dynamic CRT. We happen to be safe in this case,
		-- because libpq is good about keeping keeping mallocs and frees to itself.
		SHLIBOPTS = { "/NODEFAULTLIB:MSVCRT.lib"; Config = winFilter },
	},
	PrecompiledHeader = {
		Source = "lib/dba/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Includes = {
		"lib/dba",
	},
	Sources = {
		makeGlob("lib/dba", { Ignore = {"/Coco/"} }),
	},
	IdeGenerationHints = ideHintLibrary,
}

local dbutil = StaticLibrary {
	Name = "dbutil",
	Depends = { staticAnalysis, winCrt },
	PrecompiledHeader = makePrecompiledHeader("lib/dbutil"),
	Includes = {
		"lib/dbutil",
	},
	Sources = {
		makeGlob("lib/dbutil", {}),
	},
	IdeGenerationHints = ideHintLibrary,
}

local Video = SharedLibrary {
	Name = "Video",
	Depends = {
		winCrt, CUDA, ffmpeg, pal, tsf, gfx, libjpeg_turbo, png, stb
	},
	PrecompiledHeader = {
		Source = "lib/Video/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Includes = {
		"lib/Video",
	},
	Sources = {
		makeGlob("lib/Video", {}),
		--"lib/Video/NVidia_linux/Utils/ColorSpace.cu"
	},
	IdeGenerationHints = ideHintLibrary,
}

local Train = SharedLibrary {
	Name = "Train",
	Depends = {
		winCrt, pal, tsf, Video, CUDA, gfx, png, lz4, agg, libjpeg_turbo, stb
	},
	Libs = {
		-- This stuff is weird. Gotta do it this way to maintain linux and windows compatibility
		{ "turbojpeg.lib"; Config = winFilter },
	},
	PrecompiledHeader = {
		Source = "lib/Train/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Includes = {
		"lib/Train",
	},
	Sources = {
		makeGlob("lib/Train", {}),
	},
	IdeGenerationHints = ideHintLibrary,
}

local AI = SharedLibrary {
	Name = "AI",
	Depends = {
		winCrt, pal, tsf, cntk
	},
	PrecompiledHeader = {
		Source = "lib/AI/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Includes = {
		"lib/AI",
	},
	Sources = {
		makeGlob("lib/AI", {}),
	},
	IdeGenerationHints = ideHintLibrary,
}

local Labeler = Program {
	Name = "Labeler",
	Depends = {
		winCrt, xo, ffmpeg, Train, Video, CUDA, pal, tsf, png, libjpeg_turbo
	},
	Libs = { 
		{ "omp", "pthread", "m", "stdc++"; Config = linuxFilter },
	},
	--Env = {
	--	PROGOPTS = { "/SUBSYSTEM:CONSOLE"; Config = winFilter },
	--},
	PrecompiledHeader = {
		Source = "app/Labeler/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Includes = {
		"app/Labeler", -- This is purely here for VS intellisense. With this, VS can't find pch.h from cpp files that are not in the same dir as pch.h
	},
	Sources = {
		makeGlob("app/Labeler", {}),
	},
	IdeGenerationHints = ideHintApp,
}

local RoadProcessor = Program {
	Name = "RoadProcessor",
	Depends = {
		--winCrt, Video, gfx, opencv, ffmpeg, pal, libjpeg_turbo, png, stb, tsf, agg, glfw, lz4, proj4
		winCrt, Video, gfx, opencv, ffmpeg, torch, CUDA, pal, libjpeg_turbo, png, stb, tsf, agg, lz4, proj4
	},
	Env = {
		--PROGOPTS = { "/SUBSYSTEM:CONSOLE"; Config = winFilter },
		-- If you want asan, you also need to add "asan" to the front of the Libs list
		--CXXOPTS = {
		--	{ "-fsanitize=address" },
		--},
	},
	Libs = { 
		--{ "lensfun", "dl", "pthread", "X11", "rt", "m", "stdc++", "omp"; Config = linuxFilter },
		--{ "lensfun", "dl", "pthread", "EGL", "OpenGL", "rt", "m", "stdc++", "omp"; Config = linuxFilter },
		{ "lensfun", "EGL", "GL", "dl", "pthread", "rt", "m", "stdc++", "omp"; Config = linuxFilter },
	},
	PrecompiledHeader = {
		Source = "app/RoadProcessor/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Includes = {
		"app/RoadProcessor", -- This is purely here for VS intellisense. With this, VS can't find pch.h from cpp files that are not in the same dir as pch.h
		"third_party/glad/include",
	},
	Sources = {
		makeGlob("app/RoadProcessor", {}),
	},
	IdeGenerationHints = ideHintApp,
}

local CameraCalibrator = Program {
	Name = "CameraCalibrator",
	Depends = {
		winCrt, gfx, pal, opencv, xo, libjpeg_turbo, png, tsf, stb
	},
	Env = {
		-- PROGOPTS = { "/SUBSYSTEM:CONSOLE"; Config = winFilter },
	},
	Libs = { 
		{ "m", "stdc++", "omp"; Config = linuxFilter },
	},
	PrecompiledHeader = {
		Source = "app/CameraCalibrator/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Includes = {
		"app/CameraCalibrator", -- This is purely here for VS intellisense
	},
	Sources = {
		makeGlob("app/CameraCalibrator", {}),
	},
	IdeGenerationHints = ideHintApp,
}

local FrameServer = Program {
	Name = "FrameServer",
	Depends = {
		winCrt, Video, CUDA, Train, phttp, uberlog, tsf, gfx, pal, libjpeg_turbo, png, lz4
	},
	Env = {
		PROGOPTS = { "/SUBSYSTEM:CONSOLE"; Config = winFilter },
	},
	Libs = { 
		{ "Ws2_32.lib"; Config = winFilter },
		{ "omp", "m", "rt", "stdc++"; Config = linuxFilter },
	},
	PrecompiledHeader = {
		Source = "app/FrameServer/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Includes = {
		"app/FrameServer", -- This is purely here for VS intellisense
	},
	Sources = {
		makeGlob("app/FrameServer", {}),
	},
	IdeGenerationHints = ideHintApp,
}

local LabelServer = Program {
	Name = "LabelServer",
	Depends = {
		winCrt, Video, dbutil, dba, projwrap, phttp, uberlog, tsf, gfx, pal, sqlite
	},
	Env = {
		PROGOPTS = { "/SUBSYSTEM:CONSOLE"; Config = winFilter },
	},
	Libs = { 
		{ "Ws2_32.lib"; Config = winFilter },
		{ "proj", "omp", "m", "rt", "stdc++"; Config = linuxFilter },
	},
	PrecompiledHeader = {
		Source = "app/LabelServer/pch.cpp",
		Header = "pch.h",
		Pass = "PchGen",
	},
	Includes = {
		"app/LabelServer", -- This is purely here for VS intellisense
	},
	Sources = {
		makeGlob("app/LabelServer", {}),
	},
	IdeGenerationHints = ideHintApp,
}

Default(Labeler)
Default(RoadProcessor)
Default(CameraCalibrator)
Default(FrameServer)
