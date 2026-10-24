#pragma once
#include <cstddef>
#include <string>

namespace echolab::host {
std::string sha256Bytes(const unsigned char* bytes, std::size_t count);
std::string sha256File(const std::string& path);
}
