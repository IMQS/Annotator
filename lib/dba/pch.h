#ifdef _WIN32
#define IMQS_DBA_API __declspec(dllexport)
#else
#define IMQS_DBA_API
#endif

#include "common.h"

#include "tlsf/tlsf.h"
#include "utfz/utfz.h"

#include <lib/projwrap/projwrap.h>
#include <pdqsort/pdqsort.h>

#ifdef _MSC_VER
#include <Winsock2.h> // for ntohs in Postgres.cpp
#else
#include <arpa/inet.h> // for ntohs in Postgres.cpp
#endif

#include <regex>
