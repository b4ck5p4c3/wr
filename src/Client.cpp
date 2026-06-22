#include "Client.hpp"

namespace wr {

/* Anchored here so the vtable is emitted in one translation unit. */
HttpClient::~HttpClient() = default;

} // namespace wr
