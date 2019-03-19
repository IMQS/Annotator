
-- MSVC /analyze is very slow, so we don't want it turned on all the time,
-- which is why we keep a separate SubVariant called "analyze"

local win_linker = {
	{ "/NXCOMPAT /DYNAMICBASE /DEBUG:FASTLINK";				Config = "win*" },
	{ "/DEBUG";												Config = "win*-*-debug" },
	{ "/DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF /RELEASE";	Config = "win*-*-release" },
}

local common = {
	Env = {
		TARGETARCH = {
			{ "x86"; Config = "win32-*"; },
			{ "x64"; Config = "win64-*"; },
			{ "x64"; Config = "linux-*"; },
		},
		CPPPATH = {
			"third_party",
			".", -- The root directory of this project, so that you can say #include <app/Foo/foo.h>
		},
	},
}

local unix_common = {
	Inherit = common,
	Env = {
		CXXOPTS = {
			{ "-std=c++11" },
			{ "-fPIC" },
			{ "-ggdb" },
			{ "-O3"; Config = "linux-*-release-*" },
			{ "-mavx2" },
			{ "-fopenmp=libomp" },
			-- _GLIBCXX_DEBUG gives us debug information on std types, such as string and vector
			-- BUT then we need to compile OpenCV with this option enabled
			-- { "-D_GLIBCXX_DEBUG"; Config = "linux-*-debug-*" },
		},
		CCOPTS = {
			{ "-fPIC" },
			{ "-ggdb" },
			{ "-O3"; Config = "linux-*-release-*" },
			{ "-mavx2" },
			{ "-fopenmp=libomp" },
		},
	}
}

local win_common = {
	Inherit = common,
	Env = {
		PROGOPTS = win_linker,
		SHLIBOPTS = win_linker,
		GENERATE_PDB = {
			{ "1"; Config = "win*" },
		},
		INCREMENTAL = {
			{ "1";  Config = "win*-*-debug" },
		},
		CXXOPTS = {
			{ "/analyze /WX"; Config = "win*-*-*-analyze" },
			{ "/EHsc"; Config = "win*" },
			{ "/W3"; Config = "win*" },
			{ "/wd4251"; Config = "win*" },			-- class needs to have DLL-interface...
			{ "/wd6387"; Config = "win*" },			-- 'data' could be '0':  this does not adhere to the specification for the function 'foo'
			--{ "/analyze"; Config = "win*" },
			{ "/Gm-"; Config = "win*" },
			{ "/GS"; Config = "win*" },
			{ "/RTC1"; Config = "win*-*-debug" },
			{ "/Ox"; Config = "win*-*-release" },
			{ "/arch:SSE2"; Config = "win32-*" },
			{ "/openmp"; Config = "win*" },
			-- { "/Zc:inline"; Config = "win*" },		-- This requires VC 2013 Update 2, but it's really just a compiler/linker performance improvement.
		},
		CPPDEFS = {
			{ "_DEBUG";					Config = "win*-*-debug" },
			{ "NOMINMAX";				Config = "win*" },
		},
		CPPPATH = {
			{ "third_party/vcpkg/installed/x64-windows/include"; Config = "win64-*" },
		},
		LIBPATH = {
			{ "third_party/vcpkg/installed/x64-windows/debug/lib"; Config = "win64-*-debug-*" },
			{ "third_party/vcpkg/installed/x64-windows/lib"; Config = "win64-*-release-*" },
		},		
	},
}

Build {
	Units = {
		"units.lua",
	},
	ScriptDirs = { "build" },
	Passes= {
		PchGen = { Name = "Precompiled Header Generation", BuildOrder = 1 },
	},
	Variants = { "debug", "release" },
	SubVariants = { "default", "analyze" },
	DefaultSubVariant = "default",
	Configs = {
		{
			Name = "macosx-gcc",
			DefaultOnHost = "macosx",
			Tools = { "gcc" },
		},
		{
			Name = "linux-gcc",
			SupportedHosts = { "linux" },
			Inherit = unix_common,
			Tools = { "gcc" },
		},
		{
			Name = "linux-clang",
			DefaultOnHost = "linux",
			Inherit = unix_common,
			Tools = { "clang", "cuda" },
		},
		{
			Name = "win32-msvc2015",
			SupportedHosts = { "windows" },
			Inherit = win_common,
			Tools = { {"msvc-vs2015"; TargetArch = "x86"}, "cuda" },
		},
		{
			Name = "win64-msvc2015",
			DefaultOnHost = "windows",
			Inherit = win_common,
			Tools = { {"msvc-vs2015"; TargetArch = "x64"}, "cuda" },
		},
	},
	IdeGenerationHints = {
		Msvc = {
			-- Remap config names to MSVC platform names (affects things like header scanning & debugging)
			PlatformMappings = {
				['win64-msvc2015'] = 'x64',
				['win32-msvc2015'] = 'Win32',
			},
			-- Remap variant names to MSVC friendly names
			VariantMappings = {
				['release-default']    = 'Release',
				['debug-default']      = 'Debug',
				['release-analyze']    = 'Release Analyze',
				['debug-analyze']      = 'Debug Analyze',
			},
		},
		-- Override solutions to generate and what units to put where.
		MsvcSolutions = {
			['MachineVision.sln'] = {}, -- receives all the units due to empty set
		},
		-- Override output directory for sln/vcxproj files.
		MsvcSolutionDir = 'ide',
		BuildAllByDefault = true,
	},
}
