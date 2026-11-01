# Concatenates the dialect, shared parameter structs and every kernel file into
# one translation unit embedded as a byte array. Run as a CMake script:
#   cmake -DOUTPUT=... -DINPUT_LIST=list.txt -P embed_kernels.cmake
file(READ "${INPUT_LIST}" INPUTS)
set(text "")
foreach(path IN LISTS INPUTS)
  file(READ "${path}" content)
  get_filename_component(name "${path}" NAME)
  string(APPEND text "\n/* ---- ${name} ---- */\n${content}\n")
endforeach()
string(SHA256 digest "${text}")
file(WRITE "${OUTPUT}.txt" "${text}")
file(READ "${OUTPUT}.txt" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR byte_count "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}.tmp"
"// Generated from engine/src/kernels; do not edit.\n#include <string>\n#include \"kernels/kernel_source.h\"\nnamespace adr {\nstatic const unsigned char kSource[] = {\n${bytes}0x00};\nstd::string kernel_source() { return std::string(reinterpret_cast<const char*>(kSource), ${byte_count}); }\nconst char* kernel_source_sha256() { return \"${digest}\"; }\n}\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
