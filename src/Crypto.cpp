#include "Crypto.hpp"

#include "Errors.hpp"
#include "String.hpp"

#include <mbedtls/error.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

namespace wr {

namespace {

fn mbedtls_error(StringView context, int code) -> Error
{
  char detail[128];
  mbedtls_strerror(code, detail, sizeof(detail));

  String message{};
  message.append(context);
  message.append(", ");
  message.append(detail);
  return Error{message.view()};
}

} // namespace

fn sha256(StringView input, unsigned char output[32]) -> ErrorOr<Ok>
{
  let const code =
      mbedtls_sha256(reinterpret_cast<const unsigned char *>(input.data),
                     input.count(), output, 0);
  if (code != 0) return mbedtls_error("SHA-256 failed", code);
  return Success;
}

fn hmac_sha256(StringView key, StringView message, unsigned char output[32])
    -> ErrorOr<Ok>
{
  let const info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) return Error{"The SHA-256 digest is unavailable"};

  let const code = mbedtls_md_hmac(
      info, reinterpret_cast<const unsigned char *>(key.data), key.count(),
      reinterpret_cast<const unsigned char *>(message.data), message.count(),
      output);
  if (code != 0) return mbedtls_error("HMAC-SHA256 failed", code);
  return Success;
}

} // namespace wr
