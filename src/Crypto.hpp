#pragma once

#include "Common.hpp"
#include "StringView.hpp"

namespace wr {

fn sha256(StringView input, unsigned char output[32]) -> void;

fn hmac_sha256(StringView key, StringView message, unsigned char output[32])
    -> void;

} // namespace wr
