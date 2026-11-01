# Embeds a binary file as adr::cuda_fatbin(). cmake -DINPUT=... -DOUTPUT=... -P
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR byte_count "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}.tmp"
"// Generated CUDA fatbin; do not edit.\n#include <cstddef>\nnamespace adr {\nalignas(64) static const unsigned char kFatbin[] = {\n${bytes}0x00};\nconst unsigned char* cuda_fatbin() { return kFatbin; }\nsize_t cuda_fatbin_size() { return ${byte_count}; }\n}\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
