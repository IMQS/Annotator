#include "pch.h"
#include "HashBuilder.h"

namespace imqs {

void HashBuilder::GrowForAdditional(size_t len) {
	size_t newCap = Cap * 2;
	newCap        = std::max<size_t>(newCap, 64);
	while (newCap < Len + len)
		newCap *= 2;
	uint8_t* nbuf = (uint8_t*) imqs_malloc_or_die(newCap);
	memcpy(nbuf, Buf, Len);
	if (OwnBuf)
		free(Buf);
	OwnBuf = true;
	Buf    = nbuf;
	Cap    = newCap;
}
} // namespace imqs
