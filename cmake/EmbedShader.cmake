file(READ "${INPUT}" bytes HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${bytes}")
# MSL needs a terminating NUL. The array's last byte is excluded from code_size
# for both text and binary shaders.
file(WRITE "${OUTPUT}" "#pragma once\ninline constexpr unsigned char ${SYMBOL}[] = {${bytes}0};\n")
