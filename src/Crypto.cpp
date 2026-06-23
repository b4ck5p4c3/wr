#include "Crypto.hpp"

#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

namespace wr {

fn sha256(StringView input, unsigned char output[32]) -> void
{
  mbedtls_sha256(reinterpret_cast<const unsigned char *>(input.data),
                 input.count(), output, 0);
}

fn hmac_sha256(StringView key, StringView message, unsigned char output[32])
    -> void
{
  let const info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, reinterpret_cast<const unsigned char *>(key.data),
                  key.count(),
                  reinterpret_cast<const unsigned char *>(message.data),
                  message.count(), output);
}

} // namespace wr
