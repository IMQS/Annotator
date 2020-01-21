#ifdef _WIN32
#define IMQS_PAL_API __declspec(dllexport)
#else
#define IMQS_PAL_API
#endif

#include "common.h"
#include <utfz/utfz.h>
#include <curl/curl.h>

#include <minizip/src/mz.h>
//#include <minizip/src/mz_strm.h>
#include <minizip/src/mz_strm_mem.h>
#include <minizip/src/mz_zip.h>

#include <openssl/rsa.h>
#include <openssl/pem.h>
