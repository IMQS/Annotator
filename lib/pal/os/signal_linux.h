#pragma once

#ifdef __linux__

#include <signal.h>

namespace imqs {
namespace os {

IMQS_PAL_API bool RegisterSignalHandler(int sig, void (*handler)(int sig, siginfo_t* siginfo, void* context));
}
} // namespace imqs
#endif