#pragma once

#include "Common.hpp"
#include "ErrorOr.hpp"
#include "StringView.hpp"

namespace wr {

fn sha256(StringView input, unsigned char output[32]) -> ErrorOr<Ok>;

fn hmac_sha256(StringView key, StringView message, unsigned char output[32])
    -> ErrorOr<Ok>;

} // namespace wr
