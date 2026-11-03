#pragma once
#include <string>
namespace adr {
// Complete single-source kernel text: dialect.h, params.h and all *.kern files.
std::string kernel_source();
const char* kernel_source_sha256();
}
