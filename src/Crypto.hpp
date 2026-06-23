#pragma once

#include "Common.hpp"
#include "StringView.hpp"

namespace wr {

/* SHA-256 of the input into a thirty-two byte output, over mbedtls. */
fn sha256(StringView input, unsigned char output[32]) -> void;

/* HMAC-SHA256 of the message under the key into a thirty-two byte output. */
fn hmac_sha256(StringView key, StringView message, unsigned char output[32])
    -> void;

} // namespace wr
