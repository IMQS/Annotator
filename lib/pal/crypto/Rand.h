#pragma once

namespace imqs {
namespace crypto {

// Generate len bytes of cryptographic quality entropy
IMQS_PAL_API void RandomBytes(void* buf, size_t len);
} // namespace crypto
} // namespace imqs