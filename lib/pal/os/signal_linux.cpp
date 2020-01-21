#include "pch.h"
#include "signal_linux.h"

namespace imqs {
namespace os {

IMQS_PAL_API bool RegisterSignalHandler(int sig, void (*handler)(int sig, siginfo_t* siginfo, void* context)) {
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_sigaction = handler;
	act.sa_flags     = SA_SIGINFO;
	return sigaction(sig, &act, nullptr) == 0;
}
} // namespace os
} // namespace imqs