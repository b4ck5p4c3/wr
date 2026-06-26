#pragma once

#include "Common.hpp"
#include "StringView.hpp"

namespace wr {

/* One file from the built frontend, embedded into the binary. The path is the
   url it is served at, the data and the size are the raw bytes. The table is
   defined in the generated EmbeddedAssets.gen.cpp. */
struct embedded_asset
{
  const char *path;
  const u8 *data;
  usize size;
};

extern const embedded_asset EMBEDDED_ASSETS[];
extern const usize EMBEDDED_ASSET_COUNT;

mustuse fn find_embedded_asset(StringView path) -> const embedded_asset *;

} // namespace wr
