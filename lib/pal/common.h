// #includes common to the pal library itself, and external users of pal

#ifndef _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif

///////////////////////////////////////////////////////////////////////////////////
// Windows 10 SDK 10240 has some /analyze warnings inside it's headers (BEGIN)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 28020)
#endif
///////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <assert.h>
#include <time.h>

#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>

#ifdef _WIN32
#include <BaseTsd.h>
#include <WinSock2.h>
#include <windows.h>
#endif

///////////////////////////////////////////////////////////////////////////////////
// Windows 10 SDK 10240 has some /analyze warnings inside it's headers (END)
#ifdef _MSC_VER
#pragma warning(pop)
#endif
///////////////////////////////////////////////////////////////////////////////////

#include <tsf/tsf.h>

#ifndef IMQS_PAL_DISABLE_RAPIDJSON
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6313 6282)
#endif
#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/allocators.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif

// This is unfortunate, but on VS 2015, if you enclose the inclusion of
// nlohmann-json/json.hpp inside a push/pop statement, then the warnings
// still appear. This breaks compilation, because we compile with /WX.
// Warning 4503 is about a decorated identifier length being too long,
// typically because of complex templates, so I don't feel too bad about
// disabling this for the rest of our code too.
#ifdef _MSC_VER
#pragma warning(disable : 4503)
#pragma warning(push)
#pragma warning(disable : 6326 6031)
#endif
#include <nlohmann-json/json.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <tinyxml2/tinyxml2.h>
#include <zlib.h>
#include <lz4.h>
#include <lz4frame.h>

#include "defs.h"
#include "asserts.h"
#include "Error.h"

#include "hash/siphash.h"
#include "hash/xxhash.h"

#include "containers/ohashmap.h"
#include "containers/ohashset.h"
#include "std_utils.h"
