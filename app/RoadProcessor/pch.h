#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1
#define _CRT_SECURE_NO_WARNINGS 1

#include <xo/xo/xo.h>
#include <lib/pal/pal.h>

#include <algorithm>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4503)
#endif
#include <nlohmann-json/json.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <lib/Video/Video.h>
#include <lib/AI/AI.h>
