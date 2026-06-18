execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${SRC}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()

file(WRITE ${DST} "#pragma once\n#define HOOKFW_GIT_HASH \"${GIT_HASH}\"\n")
