#include "Crypto.hpp"

#include "Errors.hpp"
#include "String.hpp"

#pragma push_macro("fn")
#undef fn
#include <openssl/err.h>
#include <openssl/evp.h>
#pragma pop_macro("fn")

namespace wr {

namespace {

fn openssl_error(StringView context) -> Error
{
  char detail[256];
  ERR_error_string_n(ERR_get_error(), detail, sizeof(detail));

  String message{};
  message.append(context);
  message.append(", ");
  message.append(detail);
  return Error{message.view()};
}

} // namespace

fn sha256(StringView input, unsigned char output[32]) -> ErrorOr<Ok>
{
  usize digest_length = 0;
  let const hash_result = EVP_Q_digest(nullptr, "SHA256", nullptr, input.data,
                                       input.count(), output, &digest_length);
  if (hash_result != 1) return openssl_error("SHA-256 failed");
  return Success;
}

fn hmac_sha256(StringView key, StringView message, unsigned char output[32])
    -> ErrorOr<Ok>
{
  usize output_length = 0;
  let const digest = EVP_Q_mac(
      nullptr, "HMAC", nullptr, "SHA256", nullptr, key.data, key.count(),
      reinterpret_cast<const unsigned char *>(message.data), message.count(),
      output, 32, &output_length);
  if (digest == nullptr) return openssl_error("HMAC-SHA256 failed");
  return Success;
}

} // namespace wr
