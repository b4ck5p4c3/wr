#include "Crypto.hpp"

#include "Errors.hpp"

#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

namespace wr {

fn sha256(StringView input, unsigned char output[32]) -> ErrorOr<Ok>
{
  if (mbedtls_sha256(reinterpret_cast<const unsigned char *>(input.data),
                     input.count(), output, 0) != 0)
    return Error{"SHA-256 failed"};
  return Success;
}

fn hmac_sha256(StringView key, StringView message, unsigned char output[32])
    -> ErrorOr<Ok>
{
  let const info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) return Error{"The SHA-256 digest is unavailable"};

  if (mbedtls_md_hmac(info, reinterpret_cast<const unsigned char *>(key.data),
                      key.count(),
                      reinterpret_cast<const unsigned char *>(message.data),
                      message.count(), output) != 0)
    return Error{"HMAC-SHA256 failed"};
  return Success;
}

} // namespace wr
