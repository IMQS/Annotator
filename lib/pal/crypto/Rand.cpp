#include "pch.h"
#include "Rand.h"

namespace imqs {
namespace crypto {

#ifdef _WIN32
static std::atomic<HCRYPTPROV> WinRandomProvider;

void Initialize() {
	while (true) {
		if (WinRandomProvider.load() != 0)
			return;

		HCRYPTPROV prov = NULL;
		BOOL       ok   = CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_SILENT | CRYPT_VERIFYCONTEXT);
		IMQS_ASSERT(ok);

		// I believe the following second attempt was only necessary because I was initially not using the CRYPT_VERIFYCONTEXT flag.
		//if ( !ok )
		//	ok = CryptAcquireContext( &crypto_win_crypt_provider, NULL, NULL, PROV_RSA_FULL, CRYPT_NEWKEYSET | CRYPT_SILENT | CRYPT_VERIFYCONTEXT );

		HCRYPTPROV old = 0;
		if (WinRandomProvider.compare_exchange_strong(old, prov)) {
			return;
		} else {
			CryptReleaseContext(prov, 0);
		}
	}
}

IMQS_PAL_API void RandomBytes(void* buf, size_t len) {
	Initialize();
	if (TRUE != CryptGenRandom(WinRandomProvider.load(std::memory_order_relaxed), (DWORD) len, (BYTE*) buf))
		IMQS_DIE_MSG("Unable to generate crypto random bytes");
}
#else

// Linux

// Copied from NACL

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static int fd = -1;

IMQS_PAL_API void RandomBytes(void* buf, size_t len) {
	unsigned char* x    = (unsigned char*) buf;
	size_t         xlen = len;

	if (fd == -1) {
		for (;;) {
			fd = open("/dev/urandom", O_RDONLY);
			if (fd != -1)
				break;
			sleep(1);
		}
	}

	while (xlen > 0) {
		int i;
		if (xlen < 1048576)
			i = xlen;
		else
			i = 1048576;

		i = read(fd, x, i);
		if (i < 1) {
			sleep(1);
			continue;
		}

		x += i;
		xlen -= i;
	}
}

#endif
} // namespace crypto
} // namespace imqs
