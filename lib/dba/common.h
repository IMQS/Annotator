// This file has includes shared by dba itself, and users of dba

#include <lib/pal/pal.h>
#include <lib/gfx/gfx.h>

///////////////////////////////////////////////////////////////////////////////////
// Windows 10 SDK 10240 has some /analyze warnings inside it's headers (BEGIN)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 28020)
#endif
///////////////////////////////////////////////////////////////////////////////////

#include <atomic>
#include <vector>
#include <mutex>

#ifdef _WIN32
#include <WinSock2.h>
#include <windows.h>
#endif

///////////////////////////////////////////////////////////////////////////////////
// Windows 10 SDK 10240 has some /analyze warnings inside it's headers (END)
#ifdef _MSC_VER
#pragma warning(pop)
#endif
///////////////////////////////////////////////////////////////////////////////////

#include "Types.h"
#include "Errors.h"
#include "Global.h"
